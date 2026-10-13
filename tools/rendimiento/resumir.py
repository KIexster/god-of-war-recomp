#!/usr/bin/env python3
"""Resume ventanas del perfil opcional. Los tiempos son transcurridos y exclusivos por hilo."""
from __future__ import annotations
import argparse
from bisect import bisect_left, bisect_right
import json
import math
import re
import sys
from pathlib import Path

TIME_KEYS = ('ee_ms', 'vu_ms', 'gs_ms', 'iop_ms', 'guest_wait_ms', 'upload_ms', 'host_wait_ms')
REQUIRED_KEYS = ('t', 'window', 'guest_flip_hz', 'host_hz')


def parse_window(line: str) -> dict:
    if line.count('[gow-perf]') != 1:
        raise ValueError('registro de perfil intercalado')
    fields = re.findall(r'(\w+)=([^\s]+)', line.split('[gow-perf]', 1)[1])
    row = {}
    for key, value in fields:
        if key not in (*REQUIRED_KEYS, *TIME_KEYS):
            continue
        if key in row:
            raise ValueError(f'campo repetido en perfil: {key}')
        try:
            number = float(value)
        except ValueError as error:
            raise ValueError(f'valor de perfil inválido: {key}') from error
        if not math.isfinite(number) or number < 0:
            raise ValueError(f'valor de perfil inválido: {key}')
        row[key] = number
    if not all(key in row for key in REQUIRED_KEYS) or row['window'] <= 0:
        raise ValueError('registro de perfil incompleto o ventana vacía')
    if row['t'] - row['window'] < -0.011:
        raise ValueError('ventana de perfil anterior a su origen')
    return row


def select_windows(text: str, desde: float, hasta: float, partida: bool = False) -> list[dict]:
    """Con partida, exige estados cargados que encierren cada ventana del perfil.

    El reloj del mando comienza en su primera lectura y el del perfil en la
    presentación. Se usa el orden de las líneas, nunca se igualan esos relojes.
    """
    if '[gow-perf:invalid]' in text:
        raise ValueError('perfil invalidado durante la ejecución; repetir sin carga externa')
    rows = []
    positions = []
    runs = []
    current = []
    for position, line in enumerate(text.splitlines()):
        if '[gow-perf]' in line:
            row = parse_window(line)
            # Ambos números se imprimen por separado a centésimas. Se admiten
            # huecos sin inventar muestras, pero no solapamientos/reinicios.
            if rows and (row['t'] <= rows[-1]['t'] or
                         row['t'] - row['window'] < rows[-1]['t'] - 0.011):
                raise ValueError('ventanas de perfil solapadas o reloj reiniciado')
            rows.append(row)
            positions.append(position)
        elif '[gow-pad2:state]' in line:
            state = {key: int(value) for key, value in re.findall(r'(state|pending|levelReady)=(\d+)', line)}
            if state == {'state': 11, 'pending': 0, 'levelReady': 1}:
                current.append(position)
            else:
                if len(current) >= 2:
                    runs.append(current)
                current = []
    if len(current) >= 2:
        runs.append(current)

    spans = []
    if partida:
        for run in runs:
            # El primer informe posterior al estado inicial limita por arriba
            # el inicio; el último anterior al estado final limita por abajo el
            # fin. Así se excluyen las ventanas que tocan carga o transición.
            first = bisect_right(positions, run[0])
            last = bisect_left(positions, run[-1]) - 1
            if first <= last and first < len(rows):
                spans.append((rows[first]['t'], rows[last]['t']))

    selected = []
    rounding = 0.011 if partida else 0
    for row in rows:
        start, end = row['t'] - row['window'], row['t']
        if start < desde - rounding or end > hasta + rounding:
            continue
        # t y window se imprimen redondeados a centésimas por separado.
        if partida and not any(start >= low - 0.011 and end <= high + 0.011 for low, high in spans):
            continue
        selected.append(row)
    return selected


def summarize_windows(rows: list[dict]) -> dict:
    if not rows:
        raise ValueError('no hay ventanas de perfil')
    seconds = sum(row['window'] for row in rows)
    # No sustituir ámbitos ausentes por una medición de cero. Los tiempos se
    # acumulan por ámbito desde varios hilos y no suman un solo hilo del juego.
    totals = {key: sum(row[key] for row in rows) if all(key in row for row in rows) else None
              for key in TIME_KEYS}
    measured = sum(value for value in totals.values() if value is not None)
    return {
        'ventanas': len(rows), 'segundos': seconds,
        'desde': rows[0]['t']-rows[0]['window'], 'hasta': rows[-1]['t'],
        'guest_flip_hz': sum(row['guest_flip_hz']*row['window'] for row in rows)/seconds,
        'host_hz': sum(row['host_hz']*row['window'] for row in rows)/seconds,
        'tiempo_ms': totals if measured > 0 else None,
        'reparto_tiempo_instrumentado_pct': {
            key: 100*value/measured if value is not None else None for key, value in totals.items()
        } if measured > 0 else None,
        'metricas': {
            'guest_flip_hz': 'Entradas a vid::Flip por segundo; no cuadros únicos mostrados.',
            'host_hz': 'Iteraciones host completadas por segundo; pueden repetir textura.',
        },
        'limite_tiempos': ('Tiempo transcurrido exclusivo dentro de cada hilo; se suman hilos y '
                          'pueden solaparse. No uso de CPU ni reparto de un solo hilo del juego.')
                         if measured > 0 else
                         ('Sin tiempos positivos registrados: temporizadores desactivados o ámbitos '
                          'sin muestras suficientes. No acredita ausencia de trabajo o esperas.'),
    }


def main() -> int:
    # Salida JSON y errores reproducibles también en Windows con rutas/textos españoles.
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--desde', type=float, default=100)
    parser.add_argument('--hasta', type=float, default=145)
    parser.add_argument('--partida', action='store_true',
                        help='exigir state=11, pending=0 y levelReady=1 antes y después de las ventanas')
    parser.add_argument('--json', type=Path, dest='output')
    args = parser.parse_args()
    try:
        rows = select_windows(args.log.read_text(encoding='utf-8', errors='replace'),
                              args.desde, args.hasta, args.partida)
    except ValueError as error:
        parser.error(str(error))
    if not rows:
        qualifier = ' de partida cargada' if args.partida else ''
        parser.error(f'no hay ventanas completas{qualifier} en el intervalo elegido')
    result = summarize_windows(rows)
    if args.partida:
        result['seleccion'] = 'partida observada: state=11, pending=0, levelReady=1'
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if args.output:
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
