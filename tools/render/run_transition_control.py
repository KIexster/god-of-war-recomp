"""Control Windows de transición con directorios privados y estado de la ventana propia."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time

EXPECTED_ELF_SHA256 = 'ec9397d252f14d4412daffaad6be1f1931769e56222b5dbb9b39a947fc8ad71c'
CASES = ('sync_ram', 'sync_shared', 'async_ram', 'async_shared')


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def settings_for(case, folder, iso, shader_cache, vu1_thread='0', gs_front='0', diag_from_tick=0,
                 capture_acquired=0):
    if case not in CASES or vu1_thread not in ('0', '1') or gs_front not in ('0', '1'):
        raise ValueError('Caso o modo de hilos inválido')
    if gs_front == '1' and vu1_thread != '1':
        raise ValueError('El frente GS requiere el hilo VU1')
    if type(diag_from_tick) is not int or not 0 <= diag_from_tick <= 0xffffffffffffffff:
        raise ValueError('El inicio del diagnóstico debe ser un tick uint64')
    if type(capture_acquired) is not int or not 0 <= capture_acquired <= 16:
        raise ValueError('La captura adquirida requiere un cupo de 0..16')
    if capture_acquired and not case.endswith('shared'):
        raise ValueError('La captura adquirida requiere presentación compartida')
    settings = {
        'GOW_ISO': str(iso), 'GOW_MC0': str(folder / 'Mcd001.ps2'), 'GOW_MC1': '0',
        'GOW_GS_FINISH_ASINCRONO': '1' if case.startswith('async') else '0',
        'PS2X_GS_DIRECT_PRESENT': '1' if case.endswith('shared') else '0',
        'PS2X_GS_GPU': '1', 'PS2X_GS_PRESENT_DIAG': '1', 'PS2X_FRAME_HASH': '1',
        'PS2X_GS_PRESENT_DIAG_FROM_TICK': str(diag_from_tick),
        'PS2X_VU1_HILO': vu1_thread, 'PS2X_GS_FRENTE': gs_front,
        'GOW_PAD_TEST': '1', 'GOW_PAD_TEST_NO_CAPTURE': '1', 'GOW_PAD_SOLO_GUION': '1',
        'GOW_FAST_BOOT': '0', 'GOW_SKIP_FMV': '1',
        'PS2X_GS_SHADER_CACHE_DIR': str(shader_cache),
    }
    if capture_acquired:
        settings.update(PS2X_GS_ACQUIRED_CAPTURE_DIR=str(folder/'acquired'),
                        PS2X_GS_ACQUIRED_CAPTURE_FROM_TICK=str(diag_from_tick),
                        PS2X_GS_ACQUIRED_CAPTURE_COUNT=str(capture_acquired))
    return settings


def isolated_environment(inherited, settings):
    # Algunos diagnósticos se activan por presencia, incluso con valor "0".
    env = {k: v for k, v in inherited.items()
           if not k.upper().startswith(('GOW_', 'PS2X_'))}
    env.update(settings)
    return env


def manifest_digest(build, field):
    value = build.get(field)
    if not isinstance(value, str) or not re.fullmatch('[0-9a-fA-F]{64}', value):
        raise ValueError(f'El manifiesto requiere {field} con 64 cifras hexadecimales')
    return value.lower()


def private_output(directory):
    directory = Path(directory).resolve()
    repo = Path(__file__).resolve().parents[2]
    if directory.is_relative_to(repo) and not directory.is_relative_to(repo/'logs'):
        raise ValueError('Dentro del repositorio, usa logs/ para conservar privado el control')
    return directory


def pcsx2_settings(args, folder):
    """Espejo opcional: identidad explícita, captura privada y hilos aislados."""
    module = getattr(args, 'pcsx2_gs_module', None)
    if module is None:
        return {}, None
    if args.vu1_thread != '0' or args.gs_front != '0':
        raise ValueError('El espejo PCSX2 requiere VU1 y frente GS sin hilos')
    count, tick = args.pcsx2_capture_count, args.pcsx2_from_tick
    if type(count) is not int or not 0 <= count <= 16:
        raise ValueError('Capturas PCSX2: entero 0..16')
    if type(tick) is not int or not 0 <= tick <= 0xffffffffffffffff:
        raise ValueError('Inicio PCSX2: tick uint64')
    module = Path(module).resolve(strict=True)
    if not module.is_file():
        raise ValueError('Se requiere una DLL PCSX2 local')
    settings = dict(GOW_PCSX2_GS_DIAG='1', GOW_PCSX2_GS_DLL=str(module),
                    GOW_PCSX2_GS_CAPTURE_COUNT=str(count),
                    GOW_PCSX2_GS_CAPTURE_FROM_TICK=str(tick))
    if count:
        settings['GOW_PCSX2_GS_OUTPUT'] = str(folder/'pcsx2_frames')
    resources = getattr(args, 'pcsx2_resources', None)
    if resources is not None:
        resources = Path(resources).resolve(strict=True)
        if not resources.is_dir():
            raise ValueError('Recursos PCSX2: directorio local requerido')
        settings['GOW_PCSX2_GS_RESOURCES'] = str(resources)
    return settings, dict(module=str(module), module_sha256=digest(module),
                          resources=str(resources) if resources else None,
                          note='Espejo DX11/PCRTC; no reemplaza renderer, IRQ/FIFO ni mide FPS.')


class WindowsApi:
    """Solo lectura; no activa ventanas, manda entradas ni captura imágenes."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.wintypes = wintypes
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        signatures = {
            'EnumWindows': (wintypes.BOOL, [self.callback, wintypes.LPARAM]),
            'GetWindowThreadProcessId': (wintypes.DWORD, [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]),
            'GetForegroundWindow': (wintypes.HWND, []),
            'IsWindowVisible': (wintypes.BOOL, [wintypes.HWND]),
            'IsIconic': (wintypes.BOOL, [wintypes.HWND]),
        }
        for name, (restype, argtypes) in signatures.items():
            function = getattr(self.user, name)
            function.restype, function.argtypes = restype, argtypes

    def windows(self):
        handles = []
        callback = self.callback(lambda handle, _: handles.append(handle) or True)
        if not self.user.EnumWindows(callback, 0):
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        return handles

    def pid(self, handle):
        value = self.wintypes.DWORD()
        self.user.GetWindowThreadProcessId(handle, self.ctypes.byref(value))
        return value.value

    def visible(self, handle):
        return bool(self.user.IsWindowVisible(handle))

    def minimized(self, handle):
        return bool(self.user.IsIconic(handle))

    def foreground(self):
        return self.user.GetForegroundWindow()


def window_state(api, pid):
    # Filtrar por PID antes de leer visibilidad/minimización; no registrar otras apps.
    own = next((handle for handle in api.windows()
                if api.pid(handle) == pid and api.visible(handle)), None)
    return dict(window_found=own is not None, visible=own is not None,
                minimized=api.minimized(own) if own is not None else None,
                foreground=api.pid(api.foreground()) == pid)


class WindowMonitor:
    def __init__(self, pid, path, start):
        self.pid, self.path, self.start = pid, path, start
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        with self.path.open('w', encoding='utf-8') as output:
            try:
                api = WindowsApi()
                previous = None
                while not self.stop.is_set():
                    state = window_state(api, self.pid)
                    if state != previous:
                        output.write(json.dumps(dict(pid=self.pid, elapsed_s=time.monotonic()-self.start,
                                                     **state))+'\n')
                        output.flush()
                        previous = state
                    self.stop.wait(1)
            except Exception as error:
                output.write(json.dumps(dict(pid=self.pid, unavailable=str(error)))+'\n')

    def close(self):
        self.stop.set()
        self.thread.join()


def stop_child(child):
    # Popen conserva el proceso creado por este control; nunca buscar uno por nombre.
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=15)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()


def wait_for_control(child, seconds, stop_file):
    deadline = time.monotonic() + seconds
    reason = 'process_exit'
    try:
        while child.poll() is None:
            if stop_file.exists():
                reason = 'stop_file'
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                reason = 'limit'
                break
            try:
                child.wait(timeout=min(remaining, 1))
            except subprocess.TimeoutExpired:
                pass
    except KeyboardInterrupt:
        reason = 'keyboard_interrupt'
    finally:
        stop_child(child)
    return reason


def prepare_case(args):
    if not re.fullmatch('[a-z0-9_]+', args.label or args.case):
        raise ValueError('La etiqueta solo admite a..z, 0..9 y guion bajo')
    if not 1 <= args.seconds <= 1800:
        raise ValueError('La duración debe estar entre 1 y 1800 segundos')
    runner, elf, iso, modules = (Path(p).resolve(strict=True)
                                 for p in (args.runner, args.elf, args.iso, args.iop_mod))
    if not all(p.is_file() for p in (runner, elf, iso)) or not modules.is_dir():
        raise ValueError('Se requieren ejecutable, ELF, ISO y directorio IOP_MOD')
    build = json.loads(Path(args.build_manifest).read_text(encoding='utf-8'))
    if not isinstance(build, dict):
        raise ValueError('El manifiesto debe ser un objeto JSON')
    commit = build.get('commit')
    if not isinstance(commit, str) or not re.fullmatch('[0-9a-fA-F]{40}', commit):
        raise ValueError('El manifiesto debe registrar el commit completo de la compilación')
    if digest(runner) != manifest_digest(build, 'exe_sha256'):
        raise ValueError('El ejecutable no coincide con el manifiesto de compilación')
    if digest(elf) != EXPECTED_ELF_SHA256 or manifest_digest(build, 'elf_sha256') != EXPECTED_ELF_SHA256:
        raise ValueError('El ELF no corresponde a SCUS_973.99 o al manifiesto')
    card_source = Path(args.initial_card).resolve(strict=True) if args.initial_card else None
    if card_source is not None and not card_source.is_file():
        raise ValueError('La tarjeta inicial debe ser un archivo')
    root = private_output(args.output_root)
    folder = root / (args.label or args.case)
    if folder.is_relative_to(modules):
        raise ValueError('La salida no puede estar dentro del IOP_MOD de origen')
    settings = settings_for(args.case, folder, iso, root/'shader_cache',
                            args.vu1_thread, args.gs_front, args.diag_from_tick, args.capture_acquired)
    external_settings, external_identity = pcsx2_settings(args, folder)
    settings.update(external_settings)
    # No reutilizar una carpeta: evita sobrescribir una pasada o una tarjeta original.
    folder.mkdir(parents=True, exist_ok=False)
    if args.capture_acquired:
        (folder/'acquired').mkdir()
    shutil.copy2(elf, folder/'SCUS_973.99')
    shutil.copytree(modules, folder/'IOP_MOD')
    card = folder/'Mcd001.ps2'
    if card_source is None:
        card.write_bytes(b'\xff' * (528 * 0x4000))
    else:
        shutil.copy2(card_source, card)
    if args.collapsed_ui:
        (folder/'imgui.ini').write_text(
            '[Window][Runtime Debugger]\nPos=60,60\nSize=780,620\nCollapsed=1\n', encoding='utf-8')
    result = dict(case=args.case, label=args.label or args.case, build=build, settings=settings,
                  runner=str(runner), elf_source=str(elf), iso_bytes=iso.stat().st_size,
                  planned_seconds=args.seconds, collapsed_ui=args.collapsed_ui,
                  initial_card_sha256=digest(card),
                  card_origin='copy' if card_source else 'procedural_unformatted',
                  note='Control funcional; hashes y registros no miden FPS ni alinean cuadros.')
    if external_identity is not None:
        result['pcsx2'] = external_identity
    return folder, runner, card, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('case', choices=CASES)
    for name in ('runner', 'elf', 'iso', 'iop-mod', 'build-manifest', 'output-root'):
        parser.add_argument('--'+name, required=True, type=Path)
    parser.add_argument('--initial-card', type=Path)
    parser.add_argument('--seconds', type=int, default=360)
    parser.add_argument('--label')
    parser.add_argument('--collapsed-ui', action='store_true')
    parser.add_argument('--vu1-thread', choices=('0', '1'), default='0')
    parser.add_argument('--gs-front', choices=('0', '1'), default='0')
    parser.add_argument('--diag-from-tick', type=int, default=0,
                        help='Primer tick host registrado; no modifica la presentación ni su cupo')
    parser.add_argument('--capture-acquired', type=int, choices=range(17), default=0,
                        help='Capturas explícitas de la textura adquirida desde diag-from-tick; 0 desactiva')
    parser.add_argument('--pcsx2-gs-module', type=Path, help='DLL opcional del espejo GS original DX11')
    parser.add_argument('--pcsx2-resources', type=Path, help='Recursos/shaders de la revisión PCSX2 fijada')
    parser.add_argument('--pcsx2-capture-count', type=int, choices=range(17), default=8)
    parser.add_argument('--pcsx2-from-tick', type=int, default=900)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('La ejecución del juego y el monitor de ventana requieren Windows')
    try:
        folder, runner, card, result = prepare_case(args)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    env = isolated_environment(os.environ, result['settings'])
    with (folder/'out.log').open('wb') as output, (folder/'err.log').open('wb') as errors:
        start = time.monotonic()
        child = subprocess.Popen([str(runner), str(folder/'SCUS_973.99')], cwd=folder,
                                 env=env, stdout=output, stderr=errors, startupinfo=startup)
        result.update(pid=child.pid, start_time=time.strftime('%Y-%m-%dT%H:%M:%S%z'))
        monitor = WindowMonitor(child.pid, folder/'window_state.jsonl', start)
        reason = 'control_error'
        try:
            (folder/'running.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
            print(f'PID={child.pid}; control={folder}; crear {folder / "STOP"} para detenerlo', flush=True)
            monitor.thread.start()
            reason = wait_for_control(child, args.seconds, folder/'STOP')
        finally:
            stop_child(child)
            if monitor.thread.ident is not None:
                monitor.close()
            result.update(seconds=time.monotonic()-start, exit_code=child.returncode,
                          final_card_sha256=digest(card), stop_reason=reason)
            (folder/'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('pid', 'seconds', 'exit_code', 'stop_reason')}, indent=2))
    return child.returncode if reason == 'process_exit' else 0


if __name__ == '__main__':
    raise SystemExit(main())
