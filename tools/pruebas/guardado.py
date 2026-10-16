"""Comprueba cargar una partida, guardarla en el altar y volver a cargarla.

Uso: python -I tools/pruebas/guardado.py --card TARJETA.ps2
     [--guardar-secs 480] [--cargar-secs 140]
La tarjeta se copia a logs/pruebas/ y nunca se modifica la original.

Adaptado de Waffle565/God-Of-War-Native-Port main@34f30d5e432751369177af115104f4d19af6d080.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import zonas

RAW = 528
DATA = 512


def page(card, p):
    return card[p * RAW:p * RAW + DATA]


def root_dir(card):
    if len(card) < RAW * 1024 or not page(card, 0).startswith(b"Sony PS2 Memory Card Format"):
        return ["(superbloque ausente o tarjeta truncada)"], False
    ppc = struct.unpack_from("<H", page(card, 0), 0x2A)[0]
    alloc, root = struct.unpack_from("<II", page(card, 0), 0x34)
    ifc = struct.unpack_from("<I", page(card, 0), 0x50)[0]

    def cluster(c):
        return b"".join(page(card, c * ppc + i) for i in range(ppc))

    try:
        fat_ptrs = struct.unpack_from("<256I", cluster(ifc))

        def fat(c):
            return struct.unpack_from("<256I", cluster(fat_ptrs[c // 256]))[c % 256]

        entries, c = [], root
        limit = struct.unpack_from("<I", cluster(alloc + root), 4)[0] + 2
        for _ in range(min(limit, 4096)):
            data = cluster(alloc + c)
            for k in range(2):
                e = data[k * 512:(k + 1) * 512]
                mode, length = struct.unpack_from("<II", e)
                name = e[0x40:0x60].split(b"\0")[0].decode(errors="replace")
                if mode & 0x8000:
                    entries.append(f"{name} modo={mode:x} tamaño={length}")
            nxt = fat(c)
            if nxt == 0xFFFFFFFF or not nxt & 0x80000000:
                break
            c = nxt & 0x7FFFFFFF
        return entries, True
    except (IndexError, struct.error, ZeroDivisionError):
        return ["(FAT o directorio raíz ilegible)"], False


def run_session(runner, elf, card, script, seconds, folder, label):
    log_path = folder / f"{label}.log"
    for old in runner.parent.glob("gow_pad_test_*.ppm"):
        old.unlink()
    env = os.environ.copy()
    env.update(zonas.PLAY_ENV)
    env.update({"GOW_MC0": str(card), "GOW_PAD_GUION": script})
    captures = [4, 10, 18, 26, 34, 44, 56, 70, 90, 110, 130, 160, 190, 220,
                230, 240, 250, 260, 270, 280, 295, 310, 320, 360, 480]
    captures += [f"g{second}" for second in (2, 5, 10, 15, 20, 25)]
    env["GOW_PAD_CAPTURAS"] = ",".join(map(str, captures))
    env.pop("GOW_ISO", None)
    with log_path.open("wb") as log:
        proc = subprocess.Popen([str(runner), str(elf)], cwd=runner.parent, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
        early, code = zonas.stop_after(proc, seconds)
    for capture in runner.parent.glob("gow_pad_test_*.ppm"):
        shutil.copy2(capture, folder / f"{label}_{capture.name}")
    text = log_path.read_text(encoding="utf-8", errors="replace")
    return {"log": str(log_path), "termino_antes": early, "codigo": code,
            "marcadores_carga": text.count("Load Complete"),
            "marcadores_guardado": text.count("Save Complete"),
            "errores": [name for pattern, name in zonas.ERROR_PATTERNS if re.search(pattern, text, re.IGNORECASE)]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--card", help="tarjeta origen; también puede indicarse en GOW_MC0")
    ap.add_argument("--exe")
    ap.add_argument("--elf")
    ap.add_argument("--guardar-secs", type=float, default=480)
    ap.add_argument("--cargar-secs", type=float, default=140)
    ap.add_argument("--out", default=str(zonas.ROOT / "logs" / "pruebas" / "guardado"))
    args = ap.parse_args()
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    folder = Path(args.out).resolve() / stamp
    folder.mkdir(parents=True, exist_ok=True)
    try:
        runner, elf, source = zonas.require_inputs(args.exe, args.elf, args.card)
    except (FileNotFoundError, OSError) as exc:
        message = (f"# Guardado y carga {stamp}\n\n**No se inició el runner:** {exc}\n\n"
                   "- Guardar: no intentado.\n- Recargar: no intentado.\n"
                   "- Tarjeta original: no copiada ni modificada.\n")
        (folder / "informe.md").write_text(message, encoding="utf-8")
        print(f"No ejecutado: {exc}\nInforme: {folder / 'informe.md'}")
        return 2
    card = folder / "tarjeta.ps2"
    shutil.copyfile(source, card)
    before = card.read_bytes()
    initial_root, valid_before = root_dir(before)
    save_script = "5:start,13:abajo,20:x,62:x,75:x,90:x,230:r2,250:x,270:x,295:arriba,310:x"
    load_script = "5:start,13:abajo,20:x,62:x,75:x,90:x"
    save = run_session(runner, elf, card, save_script, args.guardar_secs, folder, "partida_guardar")
    after_save = card.read_bytes()
    changed = [i for i in range(min(len(before), len(after_save)) // RAW)
               if before[i * RAW:(i + 1) * RAW] != after_save[i * RAW:(i + 1) * RAW]]
    saved_root, valid_saved = root_dir(after_save)
    reload_result = run_session(runner, elf, card, load_script, args.cargar_secs, folder, "partida_recargar")
    final = card.read_bytes()
    final_root, valid_final = root_dir(final)
    result = {"tarjeta_origen": str(source), "bytes": len(before), "paginas_cambiadas": len(changed),
              "primeras_paginas_cambiadas": changed[:16], "raiz_inicial": initial_root,
              "raiz_tras_guardar": saved_root, "raiz_tras_recargar": final_root,
              "tarjeta_valida_inicio": valid_before, "tarjeta_valida_tras_guardar": valid_saved,
              "tarjeta_valida_tras_recargar": valid_final, "sesion_guardar": save, "sesion_recargar": reload_result}
    (folder / "informe.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    lines = [f"# Guardado y carga {stamp}", "", f"Tarjeta: `{source}`; copia probada: `{card}`.", "",
             f"- Antes: {'válida' if valid_before else 'inválida'}; raíz: {', '.join(initial_root)}",
             f"- Tras guardar: {'válida' if valid_saved else 'inválida'}; {len(changed)} páginas de 528 bytes cambiaron; raíz: {', '.join(saved_root)}",
             f"- Tras recargar: {'válida' if valid_final else 'inválida'}; raíz: {', '.join(final_root)}",
             f"- Sesión de guardado: {'terminó antes' if save['termino_antes'] else 'llegó al límite'} (código {save['codigo']}); capturas y log en esta carpeta.",
             f"- Sesión de recarga: {'terminó antes' if reload_result['termino_antes'] else 'llegó al límite'} (código {reload_result['codigo']}); capturas y log en esta carpeta.",
             f"- Marcadores textuales: Save Complete={save['marcadores_guardado']}, Load Complete en recarga={reload_result['marcadores_carga']}.",
             f"- Errores detectados en guardar: {', '.join(save['errores']) or 'ninguno'}; recargar: {', '.join(reload_result['errores']) or 'ninguno'}.",
             "", "Los textos de interfaz no siempre se imprimen en el log; las capturas deben revisarse para confirmar las pantallas de carga y confirmación."]
    (folder / "informe.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if valid_saved and valid_final and changed else 1


if __name__ == "__main__":
    sys.exit(main())
