"""Control optativo de profundidad y FOG/mezcla por ABI DX11, sin archivos del juego."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct

from probe_fog_reference import CreateInfo, FrameInfo, require


def fixture():
    # GOW-Port: RGB esperados enteros, contrastados con CPU, compute, hardware y DX11 Maximum.
    # Primera fila: ZTE x ZTST. Segunda: sonda Z=75 tras Z=50 sobre fondo Z=100.
    expected = [
        [0x4020F0] * 4 + [0x302010, 0x4020F0, 0x302010, 0x302010],
        [0x4020F0] * 4 + [0x302010, 0x00FF00, 0x302010, 0x302010],
        [0x7C4221, 0x7B4121, 0x67373D, 0x5A3150, 0x46276B, 0x38207F, 0x382080, 0x382080],
    ]
    pairs = [(1, 0x1A), (1 << 16, 0x4C), (0, 0x18),
             ((63 << 16) | (31 << 48), 0x40), (1, 0x46),
             (50 | (100 << 8) | (200 << 16), 0x3D), (0x44, 0x42)]

    def sprite(x, y, z, color, fog=255, test=0x30000, zmask=False, fge=False, abe=False):
        pairs.extend([(256 | (int(zmask) << 32), 0x4E), (test, 0x47),
                      (6 | (int(fge) << 5) | (int(abe) << 6), 0),
                      (color | (0x3F800000 << 32), 1), (fog << 56, 0x0A),
                      (x * 16 | ((y * 16) << 16) | (z << 32), 5),
                      ((x + 8) * 16 | (((y + 8) * 16) << 16) | (z << 32), 5)])

    for y in range(0, 32, 8):
        for x in range(0, 64, 8):
            sprite(x, y, 100, 0x80302010)
    for enable in range(2):
        for method in range(4):
            x, test = (enable * 4 + method) * 8, (enable << 16) | (method << 17)
            sprite(x, 0, 50, 0x804020F0, test=test)
            sprite(x, 8, 50, 0x804020F0, test=test)
            sprite(x, 8, 75, 0x8000FF00, test=0x50000, zmask=True)
    for i, fog in enumerate((0, 1, 77, 128, 201, 255, 128, 255)):
        # ALPHA=(Cs-Cd)*As/128+Cd, As=64; primero FOG, después mezcla.
        sprite(i * 8, 16, 100, 0x404020F0, fog=fog, zmask=True, fge=i < 6, abe=True)
    words = [len(pairs) | (1 << 15) | (1 << 60), 0x0E]
    for data, register in pairs:
        words.extend((data, register))
    return struct.pack('<' + 'Q' * len(words), *words), expected


def run(module, resources):
    require(c.sizeof(CreateInfo) == c.sizeof(FrameInfo) == 32, 'Se requiere ABI Windows x64')
    raw, expected = fixture()
    with os.add_dll_directory(str(module.parent)):
        dll = c.WinDLL(str(module))
        pointer = c.POINTER(c.c_uint8)
        signatures = {
            'abi': [], 'create': [c.POINTER(CreateInfo), c.POINTER(c.c_uint64)],
            'destroy': [c.c_uint64], 'submit': [c.c_uint64, c.c_uint32, pointer, c.c_uint32],
            'vsync': [c.c_uint64, c.c_uint32, pointer, c.c_uint32],
            'read_output': [c.c_uint64, pointer, c.c_uint64, c.POINTER(FrameInfo)],
        }
        for name, arguments in signatures.items():
            function = getattr(dll, 'gow_gs_' + name)
            function.argtypes, function.restype = arguments, c.c_uint32
        require(dll.gow_gs_abi() == 1, 'ABI de la DLL inesperada')
        config = CreateInfo(32, 1, str(resources).encode('utf-8'), 64, 32, 0, 0)
        handle = c.c_uint64()
        require(dll.gow_gs_create(c.byref(config), c.byref(handle)) == 0, 'Crear GS DX11')
        rows = []
        try:
            packet = (c.c_uint8 * len(raw)).from_buffer_copy(raw)
            require(dll.gow_gs_submit(handle, 3, packet, len(raw)) == 0, 'Enviar GIF procedural')
            registers = bytearray(8192)
            # Mismo PCRTC no entrelazado del control FOG: alpha de salida siempre 255.
            for offset, value in [(0, 1 | (1 << 5) | (1 << 7) | (255 << 8)), (0x10, 2 << 13),
                                  (0x70, 1 << 9), (0x80, (25 << 12) | (3 << 23) | (255 << 32) | (31 << 44))]:
                struct.pack_into('<Q', registers, offset, value)
            privileged = (c.c_uint8 * 8192).from_buffer_copy(registers)
            require(dll.gow_gs_vsync(handle, 0, privileged, 8192) == 0, 'VSync')
            info = FrameInfo(32, 0, 0, 0, 0, 0)
            require(dll.gow_gs_read_output(handle, None, 0, c.byref(info)) == 7, 'Consultar salida')
            require((info.width, info.height, info.row_bytes, info.required_bytes) == (64, 32, 256, 8192),
                    'Dimensiones/capacidad de salida inesperadas')
            output = (c.c_uint8 * 8192)()
            require(dll.gow_gs_read_output(handle, output, 8192, c.byref(info)) == 0, 'Leer PCRTC')
            pixels = bytes(output)
            for row, colors in enumerate(expected):
                for index, rgb in enumerate(colors):
                    x, y = index * 8, row * 8
                    observed = tuple(pixels[(y * 64 + x) * 4:(y * 64 + x + 1) * 4])
                    uniform = all(tuple(pixels[(yy * 64 + xx) * 4:(yy * 64 + xx + 1) * 4]) == observed
                                  for yy in range(y, y + 8) for xx in range(x, x + 8))
                    rgba = (*(rgb >> (i * 8) & 255 for i in range(3)), 255)
                    rows.append(dict(row=row, case=index, expected_rgba=rgba, observed_rgba=observed,
                                     uniform=uniform, matches=uniform and observed == rgba))
        finally:
            require(dll.gow_gs_destroy(handle) == 0, 'Cerrar GS DX11')
    return dict(module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(), cases=rows,
                limitation='24 celdas sintéticas de 8x8. RGB/profundidad/FOG+mezcla; no prueba alpha framebuffer, '
                           'interpolación variable, escena, prefijo del juego, estabilidad ni FPS.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True, type=Path)
    parser.add_argument('--resources', required=True, type=Path)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('El control requiere Windows x64 y hardware DX11')
    try:
        module, resources = args.module.resolve(strict=True), args.resources.resolve(strict=True)
        if not module.is_file() or not resources.is_dir():
            raise ValueError('Se requieren DLL local y directorio de recursos')
        result = run(module, resources)
    except (OSError, ValueError, RuntimeError, AttributeError) as error:
        parser.exit(1, f'Pipeline DX11: {error}\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if all(row['matches'] for row in result['cases']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
