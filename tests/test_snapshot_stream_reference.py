import ctypes as c
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools/render'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('snapshot_reference', TOOLS / 'probe_snapshot_reference.py')
reference = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reference)


class SnapshotStreamTests(unittest.TestCase):
    def test_disabled_option_does_not_resolve_missing_export(self):
        class OldDll:
            def __getattr__(self, name):
                raise AssertionError('Una DLL antigua no debe necesitar ' + name)
        self.assertIsNone(reference.stream_reader(OldDll(), False))

    def test_requested_missing_export_fails_explicitly(self):
        with self.assertRaises(AttributeError):
            reference.stream_reader(object(), True)

    def test_native_layout_and_large_counters(self):
        values = (64, 15, 3, 0x100000001, 0x200000002, 0x300000003,
                  0x400000004, 0x500000005, 1, 0)
        packed = struct.pack('<II6QII', *values)
        cursor = reference.StreamInfo.from_buffer_copy(packed)
        self.assertEqual(c.sizeof(cursor), 64)
        self.assertEqual(tuple(getattr(cursor, name) for name, _ in cursor._fields_), values)
        self.assertEqual(reference.StreamInfo.last_vsync_sequence.offset, 48)
        self.assertEqual(reference.StreamInfo.last_vsync_field.offset, 56)

    def test_handle_and_output_pointer_cross_ctypes_without_truncation(self):
        seen = []
        callback_type = c.CFUNCTYPE(c.c_uint32, c.c_uint64, c.POINTER(reference.StreamInfo))
        def native(handle, target):
            seen.append((handle, target.contents.size))
            target.contents.flags = 15
            target.contents.epoch = 2
            target.contents.input_events = 0x100000002
            target.contents.last_vsync_events = 0x100000001
            return 0
        class Dll:
            gow_gs_read_stream_info = callback_type(native)
        function = reference.stream_reader(Dll(), True)
        data = reference.read_stream(function, c.c_uint64(0x123456789abcdef0))
        self.assertEqual(seen, [(0x123456789abcdef0, 64)])
        self.assertEqual(data['input_events'], 0x100000002)
        self.assertEqual(data['last_vsync_events'], 0x100000001)
        self.assertEqual(data['reserved'], 0)

    def test_native_failure_does_not_return_zero_metadata_as_success(self):
        with self.assertRaises(RuntimeError):
            reference.read_stream(lambda *_: 3, 123)


if __name__ == '__main__':
    unittest.main()
