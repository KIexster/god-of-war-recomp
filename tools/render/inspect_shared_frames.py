"""Inspección de imágenes adquiridas; un retorno de hash no demuestra un fallo visual."""
import argparse
from collections import deque
import json
from pathlib import Path
import re

MARKER = '[gs-present:shared]'
FIELDS = ('seq render hostTick frameTick displayFbp sourceFbp width height hashValid hash blank').split()
OPTIONAL_FIELDS = {'preferred', 'smode2'}

def inspect(text):
    previous = None
    history = deque(maxlen=2)
    metadata_by_sequence = {}
    last_dimensions = None
    result = dict(acquisitions=0, retained=0, unknown_hashes=0,
                  sequence_regressions=[], inconsistent_repeats=[], content_returns=[], dimension_changes=[],
                  content_return_parity=dict(alternating=0, same=0, other=0),
                  content_return_modes=dict(unknown=0, all_field=0, other=0),
                  tick_distance=dict(samples=0, min=None, max=None, max_sample=None),
                  source_units=dict(unknown=0, pages_8192=0, blocks_256=0))
    for number, line in enumerate(text.splitlines(), 1):
        if '[gs-present] host mode:' in line:
            previous = None
            history.clear()
            metadata_by_sequence.clear()
            last_dimensions = None
        if MARKER not in line:
            continue
        tokens = line.split(MARKER, 1)[1].strip().split()
        if not len(FIELDS) <= len(tokens) <= len(FIELDS)+len(OPTIONAL_FIELDS):
            raise ValueError(f'línea {number}: registro truncado o intercalado')
        values = dict(token.split('=', 1) for token in tokens if '=' in token)
        if (not set(FIELDS).issubset(values) or set(values)-set(FIELDS)-OPTIONAL_FIELDS or
                len(values) != len(tokens) or not re.fullmatch(r'[0-9a-fA-F]{16}', values.get('hash', ''))):
            raise ValueError(f'línea {number}: campos inválidos')
        for key in values:
            if key != 'hash' and not re.fullmatch(r'[0-9]+', values[key]):
                raise ValueError(f'línea {number}: {key} inválido')
        row = {k: int(v, 16 if k == 'hash' else 10) for k, v in values.items()}
        if row['hashValid'] > 1 or row['blank'] > 1 or row.get('preferred', 0) > 1 or not row['seq']:
            raise ValueError(f'línea {number}: flags o secuencia inválidos')
        if row.get('smode2', 0) > 0xffffffffffffffff:
            raise ValueError(f'línea {number}: smode2 fuera de uint64')
        # Una secuencia conserva sus metadatos aunque reaparezca después de otra.
        # El tick actual del host puede avanzar mientras retiene la misma textura.
        metadata = {k: v for k, v in row.items() if k != 'hostTick'}
        reference = metadata_by_sequence.get(row['seq'], {})
        # La ausencia del campo opcional en un registro antiguo no contradice un dato conocido.
        inconsistent = any(k in reference and reference[k] != v for k, v in metadata.items())
        if inconsistent:
            result['inconsistent_repeats'].append(number)
            history.clear()
        else:
            metadata_by_sequence.setdefault(row['seq'], {}).update(metadata)
            dimensions = (row['width'], row['height'])
            if last_dimensions is not None and dimensions != last_dimensions:
                result['dimension_changes'].append(dict(line=number, seq=row['seq'], render=row['render'],
                    from_size=list(last_dimensions), to_size=list(dimensions)))
            last_dimensions = dimensions
            unit = ('unknown' if 'preferred' not in row else
                    'blocks_256' if row['preferred'] else 'pages_8192')
            result['source_units'][unit] += 1
            # Distancia firmada entre dos muestras del contador; no es tiempo ni FPS.
            # El request puede muestrear el tick después que el host, dando un negativo.
            distance = row['hostTick'] - row['frameTick']
            ticks = result['tick_distance']
            ticks['samples'] += 1
            ticks['min'] = distance if ticks['min'] is None else min(ticks['min'], distance)
            if ticks['max'] is None or distance > ticks['max']:
                ticks['max'] = distance
                ticks['max_sample'] = dict(line=number, seq=row['seq'], render=row['render'],
                                          hostTick=row['hostTick'], frameTick=row['frameTick'])
                if 'preferred' in row:
                    ticks['max_sample']['preferred'] = row['preferred']
        if previous and row['seq'] == previous['seq']:
            result['retained'] += 1
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
        elif not inconsistent:
            signature = tuple(row[k] for k in ('hash', 'width', 'height', 'blank'))
            if len(history) == 2 and signature == history[0][0] and signature != history[1][0]:
                evidence = dict(line=number, seq=row['seq'], prior_seq=history[0][1],
                                sourceFbp=row['sourceFbp'], frameTick=row['frameTick'],
                                frame_ticks=[history[0][2], history[1][2], row['frameTick']])
                # Paridad del tick de la textura, no del host que la adquiere.
                # No conocemos SMODE2: este patrón no acredita modo de campos ni su causa.
                parities = [tick & 1 for tick in evidence['frame_ticks']]
                parity = ('same' if parities[0] == parities[1] == parities[2] else
                          'alternating' if parities[0] == parities[2] else 'other')
                result['content_return_parity'][parity] += 1
                modes = [history[0][3], history[1][3], row.get('smode2')]
                mode_class = ('unknown' if any(mode is None for mode in modes) else
                              'all_field' if all(mode & 3 == 1 for mode in modes) else 'other')
                result['content_return_modes'][mode_class] += 1
                if any(mode is not None for mode in modes):
                    evidence['smode2_values'] = modes
                if 'preferred' in row:
                    evidence['preferred'] = row['preferred']
                result['content_returns'].append(evidence)
            history.append((signature, row['seq'], row['frameTick'], row.get('smode2')))
        previous = row
    if not result['acquisitions']:
        raise ValueError('no hay imágenes compartidas registradas')
    result['limitation'] = 'Un patrón A-B-A de hashes puede ser legítimo; no prueba corrupción, igualdad de píxeles ni FPS. La paridad de frameTick no acredita SMODE2; aun con modo de campos registrado, no demuestra la causa del retorno ni ausencia de imagen antigua. La distancia entre ticks no mide tiempo ni demuestra latencia sostenida. Sin preferred no se conocen las unidades de sourceFbp. Solo cubre los registros capturados; cero filas retenidas no descarta retenciones sin registrar.'
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
