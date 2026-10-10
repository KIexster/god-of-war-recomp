"""Inspección de imágenes adquiridas; un retorno de hash no demuestra un fallo visual."""
import argparse
from collections import deque
import json
from pathlib import Path
import re

MARKER = '[gs-present:shared]'
FIELDS = ('seq render hostTick frameTick displayFbp sourceFbp width height hashValid hash blank').split()

def inspect(text):
    previous = None
    history = deque(maxlen=2)
    result = dict(acquisitions=0, retained=0, unknown_hashes=0,
                  sequence_regressions=[], inconsistent_repeats=[], content_returns=[])
    for number, line in enumerate(text.splitlines(), 1):
        if '[gs-present] host mode:' in line:
            previous = None
            history.clear()
        if MARKER not in line:
            continue
        tokens = line.split(MARKER, 1)[1].strip().split()
        if len(tokens) != len(FIELDS):
            raise ValueError(f'línea {number}: registro truncado o intercalado')
        values = dict(token.split('=', 1) for token in tokens if '=' in token)
        if set(values) != set(FIELDS) or not re.fullmatch(r'[0-9a-fA-F]{16}', values.get('hash', '')):
            raise ValueError(f'línea {number}: campos inválidos')
        for key in FIELDS:
            if key != 'hash' and not re.fullmatch(r'[0-9]+', values[key]):
                raise ValueError(f'línea {number}: {key} inválido')
        row = {k: int(v, 16 if k == 'hash' else 10) for k, v in values.items()}
        if row['hashValid'] > 1 or row['blank'] > 1 or not row['seq']:
            raise ValueError(f'línea {number}: flags o secuencia inválidos')
        if previous and row['seq'] == previous['seq']:
            result['retained'] += 1
            # El tick actual del host puede avanzar mientras retiene la misma textura.
            if any(row[k] != previous[k] for k in FIELDS if k != 'hostTick'):
                result['inconsistent_repeats'].append(number)
                history.clear()
            continue
        result['acquisitions'] += 1
        if previous and (row['width'], row['height']) != (previous['width'], previous['height']):
            history.clear()
        if previous and (row['seq'] < previous['seq'] or row['render'] < previous['render']):
            result['sequence_regressions'].append(number)
            history.clear()
        if not row['hashValid']:
            result['unknown_hashes'] += 1
            history.clear()
        else:
            signature = tuple(row[k] for k in ('hash', 'width', 'height', 'blank'))
            if len(history) == 2 and signature == history[0][0] and signature != history[1][0]:
                result['content_returns'].append(dict(line=number, seq=row['seq'],
                    prior_seq=history[0][1], sourceFbp=row['sourceFbp'], frameTick=row['frameTick']))
            history.append((signature, row['seq']))
        previous = row
    if not result['acquisitions']:
        raise ValueError('no hay imágenes compartidas registradas')
    result['limitation'] = 'Un patrón A-B-A de hashes puede ser legítimo; no prueba corrupción, igualdad de píxeles ni FPS. Solo cubre los registros capturados.'
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        result = inspect(args.log.read_text(encoding='utf-8', errors='replace'))
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    main()
