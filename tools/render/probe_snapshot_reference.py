"""Consultar snapshots/modo del GS original con un patrón sin assets; Windows/GPU libre."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct

from probe_fog_reference import CreateInfo, FrameInfo, require


class VideoInfo(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in
                ('size', 'video_mode', 'really_interlaced', 'interlace_mode', 'pcrtc_anti_blur', 'reserved')]
    _fields_ += [(name, c.c_uint64) for name in ('sequence', 'smode1', 'syncv', 'smode2')]


class StreamInfo(c.Structure):
    _fields_ = [('size', c.c_uint32), ('flags', c.c_uint32)]
    _fields_ += [(name, c.c_uint64) for name in
                ('epoch', 'input_events', 'input_gif_bytes', 'last_vsync_events',
                 'last_vsync_gif_bytes', 'last_vsync_sequence')]
    _fields_ += [('last_vsync_field', c.c_uint32), ('reserved', c.c_uint32)]


def stream_reader(dll, requested):
    """Resolver el export sólo si se solicita; DLL anteriores siguen siendo válidas."""
    if not requested:
        return None
    function = dll.gow_gs_read_stream_info
    function.argtypes, function.restype = [c.c_uint64, c.POINTER(StreamInfo)], c.c_uint32
    return function


def read_stream(function, handle):
    info = StreamInfo()
    info.size = c.sizeof(info)
    require(function(handle, c.byref(info)) == 0, 'Consultar frontera de entrada/VSync')
    return {name: getattr(info, name) for name, _ in info._fields_}


def run(module, resources, output=None, include_stream=False):
    require(c.sizeof(CreateInfo) == c.sizeof(FrameInfo) == 32 and c.sizeof(VideoInfo) == 56,
            'ABI Windows x64 requerido')
    require(c.sizeof(StreamInfo) == 64, 'Layout de frontera Windows x64 requerido')
    pointer = c.POINTER(c.c_uint8)
    with os.add_dll_directory(str(module.parent)):
        dll = c.WinDLL(str(module))
        signatures = {'abi': [], 'create': [c.POINTER(CreateInfo), c.POINTER(c.c_uint64)],
                      'destroy': [c.c_uint64], 'submit': [c.c_uint64, c.c_uint32, pointer, c.c_uint32],
                      'vsync': [c.c_uint64, c.c_uint32, pointer, c.c_uint32],
                      'read_output': [c.c_uint64, pointer, c.c_uint64, c.POINTER(FrameInfo)],
                      'read_video_info': [c.c_uint64, c.POINTER(VideoInfo)],
                      'read_snapshot': [c.c_uint64, c.c_uint32, c.c_uint32, c.c_uint32,
                                        pointer, c.c_uint64, c.POINTER(FrameInfo)]}
        for name, arguments in signatures.items():
            function = getattr(dll, 'gow_gs_' + name)
            function.argtypes, function.restype = arguments, c.c_uint32
        cursor_function = stream_reader(dll, include_stream)
        require(dll.gow_gs_abi() == 1, 'ABI inesperada')
        handle = c.c_uint64()
        config = CreateInfo(32, 1, str(resources).encode('utf-8'), 64, 64, 0, 0)
        require(dll.gow_gs_create(c.byref(config), c.byref(handle)) == 0, 'Crear GS original')
        rows, stream = [], {}
        try:
            if cursor_function:
                stream['initial'] = read_stream(cursor_function, handle)
            pairs = [(1, 0x1a), ((1 << 32) | 256, 0x4e), (0, 0x18),
                     ((63 << 16) | (63 << 48), 0x40), (0x30000, 0x47), (1, 0x46),
                     (6, 0), (1 << 16, 0x4c), (0x80604020 | (0x3f800000 << 32), 1),
                     (0, 5), (64 * 16 | ((64 * 16) << 16), 5)]
            words = [len(pairs) | (1 << 15) | (1 << 60), 0x0e]
            for value, register in pairs:
                words.extend((value, register))
            raw = struct.pack('<' + 'Q' * len(words), *words)
            packet = (c.c_uint8 * len(raw)).from_buffer_copy(raw)
            require(dll.gow_gs_submit(handle, 3, packet, len(raw)) == 0, 'Enviar patrón CT32 uniforme')
            if cursor_function:
                stream['after_submit'] = read_stream(cursor_function, handle)
            registers = bytearray(8192)
            for offset, value in ((0, 0xff21), (0x10, 2 << 13), (0x70, 1 << 9),
                                  (0x80, (25 << 12) | (3 << 23) | (255 << 32) | (63 << 44))):
                struct.pack_into('<Q', registers, offset, value)
            privileged = (c.c_uint8 * 8192).from_buffer_copy(registers)
            require(dll.gow_gs_vsync(handle, 0, privileged, 8192) == 0, 'VSync progresivo sintético')
            if cursor_function:
                stream['after_vsync'] = read_stream(cursor_function, handle)
            video = VideoInfo()
            video.size = c.sizeof(video)
            require(dll.gow_gs_read_video_info(handle, c.byref(video)) == 0, 'Consultar modo efectivo')
            settings = {name: getattr(video, name) for name, _ in video._fields_}
            for width, height in ((0, 0), (640, 448), (960, 540), (400, 800)):
                for aspect in range(4):
                    frame = FrameInfo(32, 0, 0, 0, 0, 0)
                    require(dll.gow_gs_read_snapshot(handle, aspect, width, height, None, 0,
                                                   c.byref(frame)) == 7, 'Consultar snapshot')
                    require(0 < frame.required_bytes <= 64 << 20 and
                            frame.required_bytes == frame.width * frame.height * 4,
                            'Dimensiones de snapshot inválidas')
                    pixels = (c.c_uint8 * frame.required_bytes)()
                    require(dll.gow_gs_read_snapshot(handle, aspect, width, height, pixels,
                                                   len(pixels), c.byref(frame)) == 0, 'Leer captura consultada')
                    data = bytes(pixels)
                    filename = f'snapshot_{aspect}_{width}x{height}.png'
                    if output:
                        from PIL import Image
                        Image.frombytes('RGBA', (frame.width, frame.height), data).save(output / filename)
                    rows.append(dict(aspect=aspect, window=[width, height], image=[frame.width, frame.height],
                                     sequence=frame.sequence, rgba_sha256=hashlib.sha256(data).hexdigest(),
                                     filename=filename if output else None))
            if cursor_function:
                stream['after_snapshots'] = read_stream(cursor_function, handle)
                require(stream['after_snapshots'] == stream['after_vsync'],
                        'Las consultas/snapshots no deben avanzar ni cambiar el cursor')
        finally:
            require(dll.gow_gs_destroy(handle) == 0, 'Cerrar GS')
    result = dict(module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(), video_info=settings, cases=rows,
                limitation='Patrón sintético64x64: opciones y calculador del GS original, con filtro/bandas. '
                           'No establece temporización/encuadre PS2 del juego, frontera del host, FPS ni estabilidad.')
    if include_stream:
        result['stream_info'] = stream
        result['stream_limitation'] = ('Cursores locales de llamadas/bytes aceptados, no hashes de contenido. '
                                      'Alineación QWORD no implica tag GIF terminado. El último VSync no '
                                      'acredita composición nueva ni alineación con OpenGL.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True, type=Path)
    parser.add_argument('--resources', required=True, type=Path)
    parser.add_argument('--output', type=Path, help='Carpeta nueva para JSON y PNG del patrón; opcional')
    parser.add_argument('--stream-info', action='store_true',
                        help='Añadir fronteras de entrada/VSync; requiere el export optativo de la DLL')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Requiere Windows x64 y DX11')
    try:
        module, resources = args.module.resolve(strict=True), args.resources.resolve(strict=True)
        require(module.is_file() and resources.is_dir(), 'DLL y recursos locales requeridos')
        output = args.output.absolute() if args.output else None
        if output:
            require(not output.exists(), 'La carpeta de salida debe ser nueva')
            output.mkdir(parents=True)
        result = run(module, resources, output, args.stream_info)
        text = json.dumps(result, ensure_ascii=False, indent=2)
        if output:
            (output / 'result.json').write_text(text, encoding='utf-8')
        print(text)
    except (OSError, ValueError, RuntimeError, AttributeError) as error:
        parser.exit(1, f'Snapshot original: {error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
