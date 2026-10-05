#!/usr/bin/env python3
"""Resume ventanas del perfil opcional. Los tiempos son transcurridos y exclusivos por hilo."""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--desde', type=float, default=100)
    parser.add_argument('--hasta', type=float, default=145)
    parser.add_argument('--json', type=Path, dest='output')
    args = parser.parse_args()
    rows = []
    for line in args.log.read_text(encoding='utf-8', errors='replace').splitlines():
        if '[gow-perf]' not in line:
            continue
        row = {key: float(value) for key, value in re.findall(r'(\w+)=([0-9.]+)', line)}
        # Incluir solo ventanas completas dentro del intervalo elegido.
        if 'window' in row and row.get('t', 0) - row['window'] >= args.desde and row['t'] <= args.hasta:
            rows.append(row)
    if not rows:
        parser.error('no hay ventanas completas en el intervalo elegido')
    seconds = sum(row['window'] for row in rows)
    keys = ('ee_ms', 'vu_ms', 'gs_ms', 'iop_ms', 'guest_wait_ms', 'upload_ms', 'host_wait_ms')
    totals = {key: sum(row.get(key, 0) for row in rows) for key in keys}
    guest_ms = sum(totals[key] for key in keys[:5])
    result = {
        'ventanas': len(rows), 'segundos': seconds,
        'desde': rows[0]['t']-rows[0]['window'], 'hasta': rows[-1]['t'],
        'guest_flip_hz': sum(row.get('guest_flip_hz', 0)*row['window'] for row in rows)/seconds,
        'host_hz': sum(row.get('host_hz', 0)*row['window'] for row in rows)/seconds,
        'tiempo_ms': totals,
        'porcentaje_hilo_juego': {key: 100*totals[key]/guest_ms if guest_ms else 0 for key in keys[:5]},
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if args.output:
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
