"""Control procedural optativo de FOG con la DLL GS original DX11; requiere Windows/GPU libre."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct


class CreateInfo(c.Structure):
    _fields_ = [('size', c.c_uint32), ('abi', c.c_uint32), ('resources', c.c_char_p),
                ('width', c.c_uint32), ('height', c.c_uint32),
                ('hidden_window', c.c_uint32), ('reserved', c.c_uint32)]


class FrameInfo(c.Structure):
    _fields_ = [('size', c.c_uint32), ('width', c.c_uint32), ('height', c.c_uint32),
                ('row_bytes', c.c_uint32), ('sequence', c.c_uint64), ('required_bytes', c.c_uint64)]


def require(value, message):
    if not value:
        raise RuntimeError(message)


def run(module, resources, left_only=False):
    require(c.sizeof(CreateInfo) == c.sizeof(FrameInfo) == 32, 'Se requiere ABI Windows x64')
    # GOW-Port: conservar la búsqueda de dependencias durante todas las llamadas a la DLL.
    with os.add_dll_directory(str(module.parent)):
        dll = c.WinDLL(str(module))
        byte_pointer = c.POINTER(c.c_uint8)
        signatures = {
            'abi': [], 'create': [c.POINTER(CreateInfo), c.POINTER(c.c_uint64)],
            'destroy': [c.c_uint64], 'submit': [c.c_uint64, c.c_uint32, byte_pointer, c.c_uint32],
            'vsync': [c.c_uint64, c.c_uint32, byte_pointer, c.c_uint32],
            'read_output': [c.c_uint64, byte_pointer, c.c_uint64, c.POINTER(FrameInfo)],
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
            registers = bytearray(8192)
            # GOW-Port: layout privilegiado de GSRegs.h fijado; mismo PCRTC que el probe C++.
            for offset, value in [(0x00, 1 | (1 << 5) | (1 << 7) | (255 << 8)), (0x10, 2 << 13),
                                  (0x70, 1 << 9), (0x80, (25 << 12) | (3 << 23) | (255 << 32) | (31 << 44))]:
                struct.pack_into('<Q', registers, offset, value)
            privileged = (c.c_uint8 * 8192).from_buffer_copy(registers)
            fixtures = [((240, 32, 64), (50, 100, 200),
                         [(50, 100, 200), (50, 99, 199), (107, 79, 159),
                          (145, 66, 132), (199, 46, 93), (239, 32, 64)]),
                        ((0, 255, 128), (255, 0, 128),
                         [(255, 0, 128), (254, 0, 128), (178, 76, 128),
                          (127, 127, 128), (54, 200, 128), (0, 254, 128)])]
            for source, fog_color, expected_colors in fixtures:
                cases = [(False, 0, source), *zip([True] * 6, (0, 1, 77, 128, 201, 255), expected_colors)]
                for enabled, fog, expected in cases:
                    pairs = [(1, 0x1a), (1 << 16, 0x4c), ((1 << 32) | 0x100, 0x4e), (0, 0x18),
                             ((63 << 16) | (31 << 48), 0x40), ((1 << 16) | (1 << 17), 0x47), (1, 0x46),
                             (6 | ((1 << 5) if enabled else 0), 0),
                             (sum(v << (8 * i) for i, v in enumerate(source)) | (128 << 24) | (0x3f800000 << 32), 1),
                             (sum(v << (8 * i) for i, v in enumerate(fog_color)), 0x3d),
                             (fog << 56, 0x0a), (0, 5), (32 * 16 | ((32 * 16) << 16), 5),
                             (((fog + 17) % 255) << 56, 0x0a), (32 * 16, 5), (64 * 16 | ((32 * 16) << 16), 5)]
                    # GOW-Port: dos sprites en un tag/lote con distinto FOG evitan el clear FOG=255.
                    words = [len(pairs) | (1 << 15) | (1 << 60), 0x0e]
                    for data, register in pairs:
                        words.extend((data, register))
                    raw = struct.pack('<' + 'Q' * len(words), *words)
                    packet = (c.c_uint8 * len(raw)).from_buffer_copy(raw)
                    require(dll.gow_gs_submit(handle, 3, packet, len(raw)) == 0, 'Enviar GIF')
                    require(dll.gow_gs_vsync(handle, 0, privileged, 8192) == 0, 'VSync')
                    info = FrameInfo(32, 0, 0, 0, 0, 0)
                    require(dll.gow_gs_read_output(handle, None, 0, c.byref(info)) == 7, 'Consultar salida')
                    require((info.width, info.height, info.row_bytes, info.required_bytes) == (64, 32, 256, 8192),
                            'Dimensiones/capacidad de salida inesperadas')
                    output = (c.c_uint8 * 8192)()
                    require(dll.gow_gs_read_output(handle, output, 8192, c.byref(info)) == 0, 'Leer PCRTC')
                    pixels = bytes(output)
                    other_fog = (fog + 17) % 255
                    # GOW-Port: la mitad derecha detecta que un clear hardware use el FOG del primer sprite.
                    other_expected = tuple((s * other_fog + f * (256 - other_fog)) >> 8
                                           for s, f in zip(source, fog_color)) if enabled else source
                    halves = [(0, fog, expected, 'left')]
                    if not left_only:
                        halves.append((32, other_fog, other_expected, 'right'))
                    for x0, value, rgb, half in halves:
                        observed = tuple(pixels[x0 * 4:x0 * 4 + 4])
                        uniform = all(tuple(pixels[(y * 64 + x) * 4:(y * 64 + x + 1) * 4]) == observed
                                      for y in range(32) for x in range(x0, x0 + 32))
                        rows.append(dict(source=source, fog_color=fog_color, fog=value, enabled=enabled,
                                         half=half, uniform=uniform, observed_rgba=observed,
                                         expected_rgba=(*rgb, 255), matches=uniform and observed == (*rgb, 255)))
        finally:
            require(dll.gow_gs_destroy(handle) == 0, 'Cerrar GS DX11')
    return dict(module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(), cases=rows,
                scope='left_only' if left_only else 'both_halves',
                limitation='Patrones sintéticos de RGB/PCRTC. No valida juego, interpolación variable, estabilidad ni FPS.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True, type=Path)
    parser.add_argument('--resources', required=True, type=Path)
    parser.add_argument('--left-only', action='store_true', help='Reproducir los 14 controles históricos de la mitad izquierda')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('El control requiere Windows x64 y hardware DX11')
    try:
        module, resources = args.module.resolve(strict=True), args.resources.resolve(strict=True)
        if not module.is_file() or not resources.is_dir():
            raise ValueError('Se requieren DLL local y directorio de recursos')
        result = run(module, resources, args.left_only)
    except (OSError, ValueError, RuntimeError, AttributeError) as error:
        parser.exit(1, f'FOG DX11: {error}\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if all(row['matches'] for row in result['cases']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
