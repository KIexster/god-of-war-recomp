"""32 patrones PCRTC sin juego, con la DLL GS original; Windows/GPU libre y ejecución secuencial."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct

from probe_fog_reference import CreateInfo, FrameInfo, require


def configurations():
    # EN1, EN2, MMOD, SLBG, ALP, alpha de RC1.
    basic = [(1, 0, 0, 0, 255, 64), (0, 1, 0, 0, 255, 64),
             (1, 1, 0, 0, 255, 64), (1, 1, 0, 1, 255, 64),
             (1, 0, 1, 0, 64, 128), (1, 1, 1, 0, 64, 128),
             (1, 1, 1, 1, 64, 128), (1, 0, 1, 0, 255, 128),
             (1, 0, 0, 0, 255, 128), (1, 1, 0, 0, 255, 128),
             (1, 0, 0, 0, 255, 0), (1, 1, 0, 0, 255, 0),
             (1, 1, 1, 0, 0, 128), (1, 1, 1, 1, 0, 128),
             (1, 0, 1, 0, 0, 128), (1, 1, 1, 0, 255, 128)]
    extended = [(1, 0, 1, 0, alp, 128) for alp in (0, 1, 37, 64, 127, 128, 129, 201, 254, 255)]
    extended += [(1, 1, 1, 1, alp, 128) for alp in (1, 37, 127, 129)]
    extended += [(1, 0, 0, 0, 255, alpha) for alpha in (1, 127)]
    return basic + extended


def run(module, resources):
    require(c.sizeof(CreateInfo) == c.sizeof(FrameInfo) == 32, 'ABI Windows x64 requerido')
    pointer = c.POINTER(c.c_uint8)
    with os.add_dll_directory(str(module.parent)):
        dll = c.WinDLL(str(module))
        signatures = {'abi': [], 'create': [c.POINTER(CreateInfo), c.POINTER(c.c_uint64)],
                      'destroy': [c.c_uint64], 'submit': [c.c_uint64, c.c_uint32, pointer, c.c_uint32],
                      'vsync': [c.c_uint64, c.c_uint32, pointer, c.c_uint32],
                      'read_output': [c.c_uint64, pointer, c.c_uint64, c.POINTER(FrameInfo)]}
        for name, arguments in signatures.items():
            function = getattr(dll, 'gow_gs_' + name)
            function.argtypes, function.restype = arguments, c.c_uint32
        require(dll.gow_gs_abi() == 1, 'ABI de módulo inesperada')
        info = CreateInfo(32, 1, str(resources).encode('utf-8'), 64, 64, 0, 0)
        handle = c.c_uint64()
        require(dll.gow_gs_create(c.byref(info), c.byref(handle)) == 0, 'Crear referencia')
        cases = []
        try:
            for sequence, (en1, en2, mmod, slbg, alp, alpha) in enumerate(configurations(), 1):
                pairs = [(1, 0x1a), ((1 << 32) | 256, 0x4e), (0, 0x18),
                         ((63 << 16) | (63 << 48), 0x40), (0x30000, 0x47), (1, 0x46), (6, 0)]
                # FRAME/DISPFB en páginas de8192B: fuentes0/32 sin solape, CT32 uniforme64x64.
                for base, rgb, a in ((0, 0x8040f0, alpha), (32, 0x20a010, 128)):
                    pairs += [((1 << 16) | base, 0x4c), (rgb | (a << 24) | (0x3f800000 << 32), 1),
                              (0, 5), (64 * 16 | ((64 * 16) << 16), 5)]
                words = [len(pairs) | (1 << 15) | (1 << 60), 0x0e]
                for value, register in pairs:
                    words.extend((value, register))
                raw = struct.pack('<' + 'Q' * len(words), *words)
                packet = (c.c_uint8 * len(raw)).from_buffer_copy(raw)
                require(dll.gow_gs_submit(handle, 3, packet, len(raw)) == 0, 'Enviar GIF')
                registers = bytearray(8192)
                pmode = en1 | (en2 << 1) | (mmod << 5) | (slbg << 7) | (alp << 8)
                display = (25 << 12) | (3 << 23) | (255 << 32) | (63 << 44)
                for offset, value in ((0, pmode), (0x10, 2 << 13), (0x70, 1 << 9),
                                      (0x90, 32 | (1 << 9)), (0x80, display), (0xa0, display),
                                      (0xe0, 200 | (50 << 8) | (90 << 16))):
                    struct.pack_into('<Q', registers, offset, value)
                privileged = (c.c_uint8 * 8192).from_buffer_copy(registers)
                require(dll.gow_gs_vsync(handle, 0, privileged, 8192) == 0, 'VSync progresivo')
                frame = FrameInfo(32, 0, 0, 0, 0, 0)
                require(dll.gow_gs_read_output(handle, None, 0, c.byref(frame)) == 7, 'Consultar salida')
                require((frame.width, frame.height, frame.row_bytes, frame.required_bytes) ==
                        (64, 64, 256, 16384), 'Dimensiones inesperadas')
                pixels = (c.c_uint8 * 16384)()
                require(dll.gow_gs_read_output(handle, pixels, len(pixels), c.byref(frame)) == 0, 'Leer salida')
                colors = {tuple(pixels[i:i + 4]) for i in range(0, len(pixels), 4)}
                require(len(colors) == 1, 'Patrón no uniforme')
                cases.append(dict(sequence=sequence, en1=en1, en2=en2, mmod=mmod, slbg=slbg,
                                  alp=alp, alpha=alpha, rgba=next(iter(colors))))
        finally:
            require(dll.gow_gs_destroy(handle) == 0, 'Cerrar referencia')
    return dict(module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(), cases=cases,
                limitation='RGB/alpha de patrones progresivos uniformes; no juego, campos, '
                           'rectángulos distintos, realimentación, estabilidad ni FPS. '
                           'El renderer hardware puede diferir del software original; '
                           'la mezcla UNORM DX11 permite precisión limitada.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True, type=Path)
    parser.add_argument('--resources', required=True, type=Path)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Requiere Windows x64 y DX11')
    try:
        module, resources = args.module.resolve(strict=True), args.resources.resolve(strict=True)
        require(module.is_file() and resources.is_dir(), 'DLL y recursos locales requeridos')
        result = run(module, resources)
    except (OSError, ValueError, RuntimeError, AttributeError) as error:
        parser.exit(1, f'PCRTC original: {error}\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
