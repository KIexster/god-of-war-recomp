"""Prueba zonas de juego cargando las partidas de una tarjeta de memoria.

Uso: python -I tools/pruebas/zonas.py [--slots 1,2,3,4] [--card TARJETA.ps2]
     [--secs 200] [--out logs/pruebas/zonas]

La extracción del disco se prepara antes con tools/iso_extract.py --layout.
Las capturas y registros quedan en logs/ (ignorado por Git).

Adaptado de Waffle565/God-Of-War-Native-Port main@34f30d5e432751369177af115104f4d19af6d080.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAY_ENV = {
    "GOW_PAD_TEST": "1",
    "GOW_PAD_SOLO_GUION": "1",
    "GOW_SKIP_FMV": "1",
    "GOW_FAST_BOOT": "1",
    "GOW_PERF_DIAG": "1",
    "GOW_PERF_FRAME_PC": "0x001837B8",
    "GOW_DELTA_DIAG": "1",
}
ERROR_PATTERNS = [
    (r"No exact recompiled function", "función recompilada ausente"),
    (r"\[vif1\] (UNPACK|STMASK|STROW|STCOL|MPG) cut|MPG overruns", "paquete VIF1 truncado"),
    (r"\[vif1\] unhandled VIFcode", "VIFcode sin gestionar"),
    (r"chain on 0x[0-9a-f]+ stopped after", "cadena DMA interrumpida"),
    (r"Unhandled|unhandled exception|access violation|Access violation", "excepción"),
    (r"\[dma\] SPR .* not handled", "modo SPR DMA sin gestionar"),
    (r"gs-gpu\].*(failed|error)", "error del backend GS"),
]


def work_dir():
    return Path(os.environ.get("GOW_WORK", ROOT.drive + "\\gowport"))


def runner_path(explicit=None):
    if explicit:
        p = Path(explicit).resolve()
        if p.is_file():
            return p
        raise FileNotFoundError(f"No existe el ejecutable indicado: {p}")
    build = work_dir() / "PS2Recomp" / "out" / "build"
    candidates = list(build.rglob("ps2EntryRunner.exe")) if build.is_dir() else []
    if not candidates:
        raise FileNotFoundError(f"No existe ps2EntryRunner.exe bajo {build}; ejecuta scripts\\2_compilar.cmd")
    return max(candidates, key=lambda p: p.stat().st_mtime)


def elf_path(explicit=None):
    candidates = [Path(explicit)] if explicit else []
    if os.environ.get("GOW_ELF"):
        candidates.append(Path(os.environ["GOW_ELF"]))
    candidates += [ROOT / "game" / "SCUS_973.99", ROOT.parent / "GOW ISO extraida" / "SCUS_973.99"]
    for p in candidates:
        if p.is_file():
            return p.resolve()
    raise FileNotFoundError("No se encontró SCUS_973.99; define GOW_ELF dentro del layout extraído")


def validate_layout(elf):
    # PS2Recomp configura el CD root desde la carpeta del ELF: el modo --layout
    # debe dejar allí el TOC y los PAK que smpd abre por nombre.
    missing = [name for name in ("GODOFWAR.TOC", "PART1.PAK", "PART2.PAK") if not (elf.parent / name).is_file()]
    if missing:
        tool_note = "" if (ROOT / "tools" / "iso_extract.py").is_file() else "; tampoco existe tools/iso_extract.py en este checkout"
        raise FileNotFoundError(f"Layout incompleto junto al ELF ({', '.join(missing)}); prepara el disco con tools/iso_extract.py --layout y define GOW_ELF a ese layout{tool_note}")


def card_path(explicit=None):
    value = explicit or os.environ.get("GOW_MC0")
    if not value or value == "0":
        raise FileNotFoundError("Indica una tarjeta con --card o GOW_MC0")
    p = Path(value).resolve()
    if not p.is_file():
        raise FileNotFoundError(f"No existe la tarjeta indicada: {p}")
    return p


def require_inputs(exe=None, elf=None, card=None):
    errors, runner, executable, memory_card = [], None, None, None
    for getter, value, key in (
        (runner_path, exe, "runner"),
        (elf_path, elf, "ELF"),
        (card_path, card, "tarjeta"),
    ):
        try:
            item = getter(value)
            if key == "runner": runner = item
            elif key == "ELF": executable = item
            else: memory_card = item
        except (FileNotFoundError, OSError) as exc:
            errors.append(str(exc))
    if executable:
        try:
            validate_layout(executable)
        except FileNotFoundError as exc:
            errors.append(str(exc))
    if errors:
        raise FileNotFoundError("\n- " + "\n- ".join(errors))
    return runner, executable, memory_card


def stop_after(proc, seconds):
    deadline = time.monotonic() + seconds
    while proc.poll() is None and time.monotonic() < deadline:
        time.sleep(min(1.0, max(0.05, deadline - time.monotonic())))
    early = proc.poll() is not None
    code = proc.returncode
    if not early:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        code = proc.returncode
    return early, code


def read_ppm(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError("captura PPM no válida")
    # Saltar cabecera ASCII y comentarios, conservando exactamente el byte tras maxval.
    pos = 2
    tokens = []
    while len(tokens) < 3:
        while pos < len(data) and data[pos] in b" \t\r\n":
            pos += 1
        if pos < len(data) and data[pos] == 35:
            pos = data.find(b"\n", pos) + 1
            continue
        end = pos
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        tokens.append(int(data[pos:end]))
        pos = end
    if data[pos:pos + 2] == b"\r\n":
        pos += 2
    elif pos < len(data) and data[pos] in b" \t\r\n":
        pos += 1
    width, height, maximum = tokens
    pixels = data[pos:]
    if maximum != 255 or len(pixels) != width * height * 3:
        raise ValueError("dimensiones o profundidad PPM no válidas")
    return width, height, pixels


def image_stats(path):
    width, height, pixels = read_ppm(path)
    sample = pixels[::21]
    vals = [sum(sample[i:i + 3]) / 3 for i in range(0, len(sample) - 2, 3)]
    mean = sum(vals) / max(1, len(vals))
    var = sum((v - mean) ** 2 for v in vals) / max(1, len(vals))
    return width, height, mean, var ** 0.5


def run_zone(slot, args, runner, elf, source_card, outdir):
    zone = outdir / f"zona_slot{slot}"
    zone.mkdir(parents=True, exist_ok=True)
    card = zone / "tarjeta.ps2"
    shutil.copyfile(source_card, card)
    # Guía el menú de carga y recorre cuatro direcciones en la partida.
    keys = ["5:start", "13:abajo", "20:x", "62:x", "75:x"]
    t = 82.0
    for _ in range(slot - 1):
        keys.append(f"{t:g}:derecha:0.3")
        t += 1.5
    keys.append("90:x")
    t = 145.0
    for direction in ("mover_abajo", "mover_derecha", "mover_arriba", "mover_izquierda"):
        keys.append(f"{t:g}:{direction}:3")
        t += 5
    env = os.environ.copy()
    env.update(PLAY_ENV)
    env["GOW_MC0"] = str(card)
    env["GOW_PAD_GUION"] = ",".join(keys)
    env.pop("GOW_ISO", None)  # el runner lee los archivos ya extraídos junto al ELF
    log_path = zone / "ejecucion.log"
    for old in runner.parent.glob("gow_pad_test_*.ppm"):
        old.unlink()
    with log_path.open("wb") as log:
        proc = subprocess.Popen([str(runner), str(elf)], cwd=runner.parent, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
        died, code = stop_after(proc, args.secs)
    text = log_path.read_text(encoding="utf-8", errors="replace")
    failures, warnings = [], []
    if died:
        failures.append(f"el proceso terminó antes del límite (código {code})")
    for pattern, label in ERROR_PATTERNS:
        count = len(re.findall(pattern, text, re.IGNORECASE))
        if count:
            failures.append(f"{label}: {count}")
    perf = re.findall(r"guest_flip_hz=([0-9.]+)", text)
    state = re.findall(r"\[gow-pad2:test\].*?state=(\d+).*?levelReady=(\d+)", text)
    captures = []
    for path in sorted(runner.parent.glob("gow_pad_test_*.ppm")):
        dest = zone / path.name
        shutil.copy2(path, dest)
        try:
            w, h, mean, sd = image_stats(dest)
            captures.append({"archivo": dest.name, "ancho": w, "alto": h, "luminancia_media": round(mean, 2), "desviacion": round(sd, 2)})
        except (OSError, ValueError) as exc:
            warnings.append(f"captura {path.name} ilegible: {exc}")
    if not captures:
        failures.append("no se generaron capturas PPM")
    elif all(c["desviacion"] < 3 or c["luminancia_media"] < 4 for c in captures):
        failures.append("todas las capturas son negras o uniformes")
    else:
        warnings.append("las capturas requieren revisión visual; el log no identifica la zona por nombre")
    if state:
        warnings.append("se registró estado del juego: " + ", ".join(f"estado {s}/nivel listo {l}" for s, l in state[-4:]))
    return {"slot": slot, "termino_antes": died, "codigo": code, "fps_ultimos": [float(x) for x in perf[-4:]],
            "estados": state[-4:], "capturas": captures, "fallos": failures, "avisos": warnings,
            "errores_de_runtime": [label for pattern, label in ERROR_PATTERNS if re.search(pattern, text, re.IGNORECASE)]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--slots", default="1,2,3,4")
    ap.add_argument("--exe")
    ap.add_argument("--elf")
    ap.add_argument("--card")
    ap.add_argument("--secs", type=float, default=200)
    ap.add_argument("--out", default=str(ROOT / "logs" / "pruebas" / "zonas"))
    args = ap.parse_args()
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    outdir = Path(args.out).resolve() / stamp
    outdir.mkdir(parents=True, exist_ok=True)
    try:
        runner, elf, card = require_inputs(args.exe, args.elf, args.card)
    except (FileNotFoundError, OSError) as exc:
        slots = [int(s) for s in args.slots.split(",")]
        lines = [f"# Zonas {stamp}", "", "**No se inició el runner.**", "", str(exc), "",
                 "| Ranura | Resultado |", "|---:|---|"]
        lines.extend(f"| {slot} | No ejecutada: faltan requisitos previos; no hay log ni captura de juego. |" for slot in slots)
        (outdir / "informe.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (outdir / "informe.json").write_text(json.dumps(
            [{"slot": slot, "resultado": "no ejecutada", "motivo": str(exc)} for slot in slots],
            indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"No ejecutado: {exc}\nInforme: {outdir / 'informe.md'}")
        return 2
    results = []
    for slot in (int(s) for s in args.slots.split(",")):
        print(f"Zona/ranura {slot}…", flush=True)
        result = run_zone(slot, args, runner, elf, card, outdir)
        results.append(result)
        print("  ", "FALLO" if result["fallos"] else "sin fallos detectados", result["fallos"], flush=True)
    lines = [f"# Prueba de zonas {stamp}", "", f"Runner: `{runner}`  ", f"ELF/layout: `{elf.parent}`  ",
             f"Tarjeta de origen: `{card}` (se usó una copia por ranura).", "",
             "| Ranura | Proceso | FPS recientes | Capturas | Errores | Resultado |", "|---:|---|---|---:|---|---|"]
    for r in results:
        lines.append(f"| {r['slot']} | {'terminó temprano' if r['termino_antes'] else 'límite de tiempo'} | "
                     f"{', '.join(f'{v:.1f}' for v in r['fps_ultimos']) or 'sin dato'} | {len(r['capturas'])} | "
                     f"{', '.join(r['errores_de_runtime']) or 'ninguno detectado'} | "
                     f"{'FALLO: ' + '; '.join(r['fallos']) if r['fallos'] else 'sin fallos detectados'} |")
        lines += ["", f"## Ranura {r['slot']}", ""]
        lines += [f"- Captura `{c['archivo']}`: {c['ancho']}×{c['alto']}, luminancia media {c['luminancia_media']}, desviación {c['desviacion']}." for c in r["capturas"]]
        lines += [f"- Aviso: {v}" for v in r["avisos"]]
        lines += [f"- Fallo: {v}" for v in r["fallos"]]
    (outdir / "informe.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (outdir / "informe.json").write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"Informe: {outdir / 'informe.md'}")
    return 1 if any(r["fallos"] for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
