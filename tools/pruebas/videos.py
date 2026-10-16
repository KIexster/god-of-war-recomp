"""Comprueba la reproducción natural de FMV y enumera los vídeos del disco.

Uso: python -I tools/pruebas/videos.py [--secs 300] [--card TARJETA.ps2]

El runner de esta rama no permite forzar un nombre de película. El guion prueba
el arranque natural (intro y comienzo de partida); el informe separa los vídeos
observados de los que no llegaron a reproducirse en esa sesión. No se atribuye
una reproducción a un archivo sin un nombre reconocible en el registro.
El layout del disco debe proceder de tools/iso_extract.py --layout.

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


def movie_list(toc):
    data = toc.read_bytes()
    names = {}
    for offset in range(0, len(data) - 23, 24):
        name = data[offset:offset + 12].split(b"\0")[0].decode("ascii", "replace")
        size = struct.unpack_from("<I", data, offset + 16)[0]
        if name.upper().endswith(".PSS"):
            names.setdefault(name[:-4].upper(), size)
    return names


def pcm_summary(path):
    if not path.exists():
        return {"bytes": 0, "segundos": 0.0, "rms": None}
    size = path.stat().st_size
    # PCM s16le estéreo a 48 kHz: se recorren ventanas de 1 s, no se retiene todo en memoria.
    sums, frames = 0, 0
    with path.open("rb") as stream:
        while True:
            block = stream.read(48000 * 4)
            if len(block) < 4:
                break
            usable = len(block) - len(block) % 2
            values = struct.unpack("<" + "h" * (usable // 2), block[:usable])
            sums += sum(v * v for v in values)
            frames += usable // 4
    return {"bytes": size, "segundos": round(frames / 48000, 2),
            "rms": round((sums / max(1, frames * 2)) ** 0.5, 2)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--secs", type=float, default=300)
    ap.add_argument("--exe")
    ap.add_argument("--elf")
    ap.add_argument("--card")
    ap.add_argument("--out", default=str(zonas.ROOT / "logs" / "pruebas" / "videos"))
    args = ap.parse_args()
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    folder = Path(args.out).resolve() / stamp
    folder.mkdir(parents=True, exist_ok=True)
    try:
        runner, elf, source_card = zonas.require_inputs(args.exe, args.elf, args.card)
    except (FileNotFoundError, OSError) as exc:
        message = (f"# Vídeos {stamp}\n\n**No se inició el runner:** {exc}\n\n"
                   "No existe un `GODOFWAR.TOC` disponible para enumerar los nombres; no se pudo evaluar ningún vídeo.\n")
        (folder / "informe.md").write_text(message, encoding="utf-8")
        print(f"No ejecutado: {exc}\nInforme: {folder / 'informe.md'}")
        return 2
    toc = elf.parent / "GODOFWAR.TOC"
    movies = movie_list(toc)
    card = folder / "tarjeta.ps2"
    shutil.copyfile(source_card, card)
    log_path, pcm_path = folder / "ejecucion.log", folder / "audio.pcm"
    for old in runner.parent.glob("gow_pad_test_*.ppm"):
        old.unlink()
    env = os.environ.copy()
    env.update({"GOW_PAD_TEST": "1", "GOW_PAD_SOLO_GUION": "1", "GOW_FAST_BOOT": "1",
                "GOW_PAD_GUION": "5:start,14:x,20:x,26:x,32:x", "GOW_AUDIO": "0",
                "GOW_AUDIO_PCM": str(pcm_path), "GOW_MC0": str(card)})
    env.pop("GOW_SKIP_FMV", None)
    env.pop("GOW_ISO", None)
    with log_path.open("wb") as log:
        proc = subprocess.Popen([str(runner), str(elf)], cwd=runner.parent, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
        early, code = zonas.stop_after(proc, args.secs)
    text = log_path.read_text(encoding="utf-8", errors="replace")
    for capture in runner.parent.glob("gow_pad_test_*.ppm"):
        shutil.copy2(capture, folder / capture.name)
    movie_states = [int(n) for n in re.findall(r"\[gow-pad2:test\].*?movie=(\d+)", text)]
    # Este port aún no imprime el nombre del PSS en el log ni implementa el override de Waffle.
    logged_names = {m.upper() for m in re.findall(r"\b([A-Z0-9_]{3,12})\.PSS\b", text, re.IGNORECASE)}
    sound = pcm_summary(pcm_path)
    rows = []
    for name, size in sorted(movies.items()):
        observed = name in logged_names
        rows.append({"video": name, "tamaño_toc": size,
                     "resultado": "nombre detectado en el log" if observed else "no identificado ni forzado en esta sesión"})
    result = {"proceso_termino_antes": early, "codigo": code, "estados_movie": movie_states,
              "nombres_en_log": sorted(logged_names), "audio_pcm": sound,
              "capturas": sorted(p.name for p in folder.glob("gow_pad_test_*.ppm")), "videos": rows,
              "limite": "GOW_MOVIE_OVERRIDE no existe en este runner; estado movie distinto de cero no identifica por sí solo qué PSS se presentó."}
    (folder / "informe.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    lines = [f"# Prueba de vídeos {stamp}", "", f"Runner: `{runner}`  ", f"ELF/layout: `{elf.parent}`  ",
             f"Ejecución: {'terminó antes' if early else 'llegó al límite'} (código {code}).",
             f"Estados movie registrados: {', '.join(map(str, movie_states)) or 'ninguno'}.",
             f"Audio capturado: {sound['bytes']} bytes, {sound['segundos']} s emulados, RMS {sound['rms']}.",
             f"Capturas: {len(result['capturas'])} PPM en esta carpeta.", "",
             "| Vídeo del TOC | Tamaño (bytes) | Resultado |", "|---|---:|---|"]
    for row in rows:
        lines.append(f"| {row['video']} | {row['tamaño_toc']} | {row['resultado']} |")
    lines += ["", "El port de esta rama no implementa `GOW_MOVIE_OVERRIDE` ni registra el nombre del PSS durante la reproducción. Por eso los estados movie y el PCM global no se adjudican a un archivo concreto; la sesión solo prueba las películas que el propio flujo natural active."]
    (folder / "informe.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if not early else 1


if __name__ == "__main__":
    sys.exit(main())
