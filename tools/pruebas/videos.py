"""Prueba cada FMV listado en GODOFWAR.TOC y mide imagen y audio.

Uso: python -I tools/pruebas/videos.py [--batch 12] [--only A01MP05,SEMO02]
     [--card TARJETA.ps2] [--keep-pcm]

El ejecutable debe corresponder al layout creado con
`python tools/iso_extract.py <ISO> game --layout`. Cada tanda usa una copia de
la tarjeta seleccionada por GOW_MC0; registros, PCM y capturas PPM se guardan
en logs/pruebas/videos/ (ignorado por Git).

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
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import zonas

RATE = 48000
BYTES_PER_SECOND = 0.69e6
LOAD_LINE = r"\[gow-fmv\] (?:override \d+/\d+ \S+ -> |load )(\S+)"
ERRORS = r"No exact recompiled|Unhandled|access violation|Access violation|did not return|missing-target"
ENV = {
    "PS2X_GS_WORKER": "1", "GOW_GS_FINISH_ASINCRONO": "1", "GOW_PAD_TEST": "1",
    "GOW_PERF_DIAG": "1", "GOW_PERF_FRAME_PC": "0x001837B8", "GOW_DELTA_DIAG": "1",
    "GOW_MPEG_DIAG": "1", "GOW_AUDIO_STATS": "1", "GOW_AUDIO": "0",
    "GOW_PAD_GUION": "99999:select", "GOW_BONUS_MOVIE_DELAY": "8",
}


def movie_list(toc):
    data = toc.read_bytes()
    names = {}
    for i in range(0, len(data) - 23, 24):
        name = data[i:i + 12].split(b"\0")[0].decode("ascii", "replace")
        size = struct.unpack_from("<I", data, i + 16)[0]
        if name.upper().endswith(".PSS"):
            names.setdefault(name[:-4].upper(), size)
    return names


def run_batch(movies, sizes, folder, index, runner, elf, source_card):
    log_path = folder / f"batch{index}.log"
    pcm_path = folder / f"batch{index}.pcm"
    card = folder / f"tarjeta{index}.ps2"
    shutil.copyfile(source_card, card)
    env = os.environ.copy()
    env.update(ENV)
    env.update({"GOW_MOVIE_OVERRIDE": ",".join(movies), "GOW_BONUS_MOVIE": "24",
                "GOW_AUDIO_PCM": str(pcm_path), "GOW_MC0": str(card)})
    env.pop("GOW_SKIP_FMV", None)
    env.pop("GOW_ISO", None)
    # El tiempo límite se calcula con el tamaño de los PSS y el caudal habitual de lectura.
    budget = 120 + sum(sizes[m] / BYTES_PER_SECOND + 30 for m in movies)
    for old in runner.parent.glob("gow_pad_test_*.ppm"):
        old.unlink()
    with log_path.open("wb") as log:
        proc = subprocess.Popen([str(runner), str(elf)], cwd=runner.parent, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
        start, idle_at = time.monotonic(), None
        while proc.poll() is None and time.monotonic() - start < budget:
            time.sleep(2)
            text = log_path.read_text(encoding="utf-8", errors="replace")
            overrides = text.count("[gow-fmv] override ")
            last = text.rfind("[gow-fmv] override ")
            # Tras la última tanda, el reproductor vuelve al estado inactivo.
            if overrides >= len(movies) and re.search(r"menu sees movie state \d+ -> 0", text[last:]):
                idle_at = idle_at or time.monotonic()
                if time.monotonic() - idle_at > 4:
                    break
            # Un único vídeo de la ranura de intro puede acabar antes de que el menú lo supervise.
            if len(movies) == 1 and overrides and "menu sees" not in text and re.search(r"\[MPEG:sync\]", text[last:]) \
                    and time.monotonic() - start > 60 + sizes[movies[0]] / BYTES_PER_SECOND:
                break
        ended_early = proc.poll() is not None
        code = proc.returncode
        if not ended_early:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            code = proc.returncode
    for capture in runner.parent.glob("gow_pad_test_*.ppm"):
        shutil.copy2(capture, folder / f"batch{index}_{capture.name}")
    return log_path, pcm_path, time.monotonic() - start, ended_early, code


def pcm_checks(path, p0, p1):
    result = {"segundos_silenciosos": 0, "muestras_recortadas": 0, "ruido_max": 0.0,
              "bloques_repetidos": 0, "rms_medio": 0.0}
    if not path.is_file():
        return result
    first, last = max(0, p0), min(p1, path.stat().st_size // 4)
    if last - first < RATE:
        return result
    with path.open("rb") as stream:
        stream.seek(first * 4)
        pcm = stream.read((last - first) * 4)
    frames = len(pcm) // 4
    stereo = np.frombuffer(pcm[:frames * 4], dtype="<i2").reshape(-1, 2)
    left = stereo[:, 0].astype(np.float64)
    seconds = len(left) // RATE
    windows = left[:seconds * RATE].reshape(seconds, RATE)
    rms = np.sqrt(np.mean(windows ** 2, axis=1))
    loud = rms >= 30
    result["segundos_silenciosos"] = int(np.count_nonzero(~loud))
    if loud.any():
        diff = np.sqrt(np.mean(np.diff(windows[loud], axis=1) ** 2, axis=1))
        result["ruido_max"] = float(np.max(diff / rms[loud]))
    result["rms_medio"] = float(np.mean(rms))
    result["muestras_recortadas"] = int(np.count_nonzero((stereo >= 32767) | (stereo <= -32768)))
    blocks = stereo[:len(stereo) // 512 * 512].reshape(-1, 512 * 2)
    if len(blocks):
        loud_blocks = np.max(np.abs(blocks[:, :128]), axis=1) > 64
        repeated = np.zeros(len(blocks), dtype=bool)
        for back in range(1, 9):
            repeated[back:] |= np.all(blocks[back:] == blocks[:-back], axis=1)
        result["bloques_repetidos"] = int(np.count_nonzero(repeated & loud_blocks))
    return result


def analyse(movies, log_path, pcm_path):
    text = log_path.read_text(encoding="utf-8", errors="replace")
    starts = [m.start() for m in re.finditer(LOAD_LINE, text)]
    pcm_frames = pcm_path.stat().st_size // 4 if pcm_path.is_file() else 0
    sync_re = r"\[MPEG:sync\] frame=(\d+) pts=(\d+) wall_ms=([0-9.]+) audio_played=\d+ audio_produced=(\d+)"
    rows = []
    for index, movie in enumerate(movies):
        row = {"video": movie, "resultado": "falló", "motivos": []}
        rows.append(row)
        if index >= len(starts):
            row["motivos"].append("no se cargó")
            continue
        segment = text[starts[index]:starts[index + 1] if index + 1 < len(starts) else len(text)]
        sync = [(int(f), int(pts), float(wall), int(audio))
                for f, pts, wall, audio in re.findall(sync_re, segment)]
        sync = [sample for sample in sync if sample[0] >= 30 and sample[1] < (1 << 33)]
        errors = len(re.findall(ERRORS, segment))
        marks = [m.start() for m in re.finditer(sync_re, segment)
                 if int(m.group(1)) >= 30 and int(m.group(2)) < (1 << 33)]
        playing = segment[marks[0]:segment.find("\n[audio-stats]", marks[-1]) + 200] if marks else segment
        stats = re.findall(r"\[audio-stats\] requested=\d+ missing=(\d+) dropped=(\d+)", playing)
        missing = sum(int(a) for a, _ in stats)
        dropped = sum(int(b) for _, b in stats)
        row.update({"errores_runtime": errors, "audio_perdido": missing, "audio_descartado": dropped})
        if len(sync) < 2:
            row["motivos"].append("sin imágenes MPEG observadas")
            continue
        (f0, pts0, wall0, audio0), (f1, pts1, wall1, audio1) = sync[0], sync[-1]
        picture_seconds = (pts1 - pts0) / 90000.0
        fps = (f1 - f0) / max(1e-6, (wall1 - wall0) / 1000.0)
        audio0 = min(audio0, pcm_frames)
        audio1 = min(audio1, pcm_frames)
        audio_seconds = (audio1 - audio0) / RATE
        drift = [(a - audio0) / RATE - (pts - pts0) / 90000.0 for _, pts, _, a in sync]
        row.update({"fotogramas": f1, "fps": fps, "segundos_imagen": picture_seconds,
                    "segundos_audio": audio_seconds, "deriva_av": max(drift) - min(drift)})
        row.update(pcm_checks(pcm_path, audio0, audio1))
        if not 29.0 <= fps <= 31.0:
            row["motivos"].append(f"{fps:.1f} imágenes/s")
        if abs(audio_seconds - picture_seconds) > 0.5:
            row["motivos"].append(f"audio {audio_seconds:.1f} s para {picture_seconds:.1f} s de imagen")
        if row["deriva_av"] > 0.25:
            row["motivos"].append(f"deriva A/V {row['deriva_av']:.2f} s")
        if row["rms_medio"] < 100:
            row["motivos"].append("sin audio audible")
        elif row["segundos_silenciosos"] > max(5, picture_seconds * 0.25):
            row["motivos"].append(f"{row['segundos_silenciosos']} s silenciosos")
        if row["ruido_max"] >= 1.2:
            row["motivos"].append(f"ruido (diferencia/RMS {row['ruido_max']:.2f})")
        if row["muestras_recortadas"] > picture_seconds * RATE * 2 * 0.005:
            row["motivos"].append(f"{row['muestras_recortadas']} muestras saturadas")
        if row["bloques_repetidos"] > 2:
            row["motivos"].append(f"{row['bloques_repetidos']} bloques repetidos")
        if dropped or missing > RATE // 10:
            row["motivos"].append(f"salida de audio: descartados {dropped}, faltantes {missing}")
        if errors:
            row["motivos"].append(f"{errors} errores de ejecución")
        if "menu sees movie state" in segment and not re.search(r"menu sees movie state 11 -> 0", segment):
            row["motivos"].append("el reproductor no volvió al estado inactivo")
        if not row["motivos"]:
            row["resultado"] = "correcto"
    return rows


def write_report(folder, rows, stamp):
    lines = [f"# Prueba de vídeos {stamp}", "",
             "| Vídeo | Tanda | Fotogramas | Imágenes/s | s imagen | s audio | deriva A/V | RMS | s silenciosos | recorte | repeticiones | descartados | resultado |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|"]
    for row in rows:
        def fmt(key, digits=2):
            return f"{row[key]:.{digits}f}" if key in row else "—"
        lines.append("| {video} | {batch} | {frames} | {fps} | {picture} | {audio} | {drift} | {rms} | {silent} | {clipped} | {repeat} | {dropped} | {result} |".format(
            video=row["video"], batch=row.get("tanda", "—"), frames=row.get("fotogramas", "—"),
            fps=fmt("fps"), picture=fmt("segundos_imagen"), audio=fmt("segundos_audio"), drift=fmt("deriva_av"),
            rms=fmt("rms_medio", 0), silent=row.get("segundos_silenciosos", "—"),
            clipped=row.get("muestras_recortadas", "—"), repeat=row.get("bloques_repetidos", "—"),
            dropped=row.get("audio_descartado", "—"),
            result=row["resultado"] + (": " + "; ".join(row["motivos"]) if row["motivos"] else "")))
    failures = [row["video"] for row in rows if row["resultado"] != "correcto"]
    lines += ["", f"{len(rows) - len(failures)} de {len(rows)} vídeos correctos" +
              ("; con fallos: " + ", ".join(failures) if failures else "")]
    (folder / "informe.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (folder / "informe.json").write_text(json.dumps(rows, indent=2, ensure_ascii=False), encoding="utf-8")
    print("\n".join(lines[-1:]))
    print(f"Informe: {folder / 'informe.md'}")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--only", help="nombres separados por comas; por defecto prueba todos los PSS del TOC")
    ap.add_argument("--batch", type=int, default=12, help="vídeos por sesión")
    ap.add_argument("--exe")
    ap.add_argument("--elf")
    ap.add_argument("--card", help="tarjeta origen; también se puede indicar en GOW_MC0")
    ap.add_argument("--out", default=str(zonas.ROOT / "logs" / "pruebas" / "videos"))
    ap.add_argument("--keep-pcm", action="store_true", help="conservar los archivos PCM de cada tanda")
    args = ap.parse_args()
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    folder = Path(args.out).resolve() / stamp
    folder.mkdir(parents=True, exist_ok=True)
    try:
        runner, elf, source_card = zonas.require_inputs(args.exe, args.elf, args.card)
    except (FileNotFoundError, OSError) as exc:
        (folder / "informe.md").write_text(f"# Prueba de vídeos {stamp}\n\n**No se inició el runner:**\n\n{exc}\n", encoding="utf-8")
        print(f"No ejecutado: {exc}\nInforme: {folder / 'informe.md'}")
        return 2
    sizes = movie_list(elf.parent / "GODOFWAR.TOC")
    movies = [name.upper() for name in args.only.split(",")] if args.only else sorted(sizes)
    for name in movies:
        sizes.setdefault(name, 50_000_000)
    all_rows = []
    for index, start in enumerate(range(0, len(movies), args.batch)):
        batch = movies[start:start + args.batch]
        print(f"Tanda {index}: {', '.join(batch)}", flush=True)
        log_path, pcm_path, elapsed, ended_early, code = run_batch(batch, sizes, folder, index, runner, elf, source_card)
        rows = analyse(batch, log_path, pcm_path)
        for row in rows:
            row["tanda"] = index
            all_rows.append(row)
            print(f"  {row['video']:12s} {row['resultado']}: {'; '.join(row['motivos'])}", flush=True)
        summary_path = folder / f"batch{index}.json"
        summary_path.write_text(json.dumps({"segundos": elapsed, "termino_antes": ended_early, "codigo": code,
                                            "videos_solicitados": batch, "resultados": rows}, indent=2, ensure_ascii=False),
                                encoding="utf-8")
        if not args.keep_pcm and pcm_path.exists():
            pcm_path.unlink()
    return write_report(folder, all_rows, stamp)


if __name__ == "__main__":
    sys.exit(main())
