import importlib.util
from pathlib import Path
import struct
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "tools/render/pcsx2_ee_reference.py"
SPEC = importlib.util.spec_from_file_location("pcsx2_reference", SOURCE)
reference = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reference)


class Connection:
    def __init__(self, payload, status=0, length=None):
        self.reply = bytearray(struct.pack("<I", length or len(payload) + 5)
                               + bytes([status]) + payload)
        self.sent = bytearray()

    def sendall(self, data):
        self.sent.extend(data)

    def recv(self, size):
        # Una cabecera y un valor pueden llegar en fragmentos de TCP separados.
        count = min(size, 3, len(self.reply))
        result = bytes(self.reply[:count])
        del self.reply[:count]
        return result


class SnapshotPine:
    def __init__(self, changed=False):
        self.identities = 0
        self.changed = changed

    def identity(self):
        self.identities += 1
        return {"serial": "SCUS-97399", "crc": "d6385328", "version": "test"}

    def read(self, address, size):
        if address == 0x29BDF8 and size == 8:
            return struct.pack("<Q", 2 if self.changed and self.identities else 0)
        return bytes(size)


class ReferenceTests(unittest.TestCase):
    def test_fragmented_read_and_only_read_opcode(self):
        connection = Connection(struct.pack("<2I", 0x12345678, 0xABCDEF01))
        self.assertEqual(reference.Pine(connection).read(0x100, 8),
                         struct.pack("<2I", 0x12345678, 0xABCDEF01))
        self.assertEqual(connection.sent, struct.pack("<I", 14)
                         + b"\x02" + struct.pack("<I", 0x100)
                         + b"\x02" + struct.pack("<I", 0x104))

    def test_invalid_ranges_send_nothing(self):
        for address, size in [(-4, 4), (1, 4), (0, 0), (0, 3),
                              (reference.RAM_SIZE, 4), (0, 65540)]:
            connection = Connection(b"")
            with self.assertRaises(ValueError):
                reference.Pine(connection).read(address, size)
            self.assertFalse(connection.sent)

    def test_bad_reply_sizes_and_failure(self):
        for connection in [Connection(b"", length=4),
                           Connection(b"", length=reference.MAX_REPLY),
                           Connection(b"", status=255), Connection(b"\0")]:
            with self.assertRaises(ValueError):
                reference.Pine(connection).read(0, 4)

    def test_disconnected_response(self):
        with self.assertRaises(EOFError):
            reference.Pine(Connection(b"", length=9)).read(0, 4)

    def test_identity_requires_pause(self):
        connection = Connection(struct.pack("<I", 0))
        with self.assertRaisesRegex(ValueError, "Pausa"):
            reference.Pine(connection).identity()
        self.assertEqual(connection.sent, struct.pack("<I", 5) + b"\x0f")

    def test_identity_must_match_edition(self):
        class OtherGame(reference.Pine):
            def status(self): return 1
            def string(self, command): return self.serial if command == 12 else self.crc
        for serial, crc in [("SCES-12345", "d6385328"), ("SCUS-97399", "00000000")]:
            pine = OtherGame(None)
            pine.serial, pine.crc = serial, crc
            with self.assertRaisesRegex(ValueError, "Edición"):
                pine.identity()

    def test_string_length_and_terminator(self):
        for payload in [b"", struct.pack("<I", 3) + b"ab",
                        struct.pack("<I", 2) + b"ab"]:
            with self.assertRaises(ValueError):
                reference.Pine(Connection(payload)).string(12)

    def test_capture_stable_and_changed(self):
        ram, metadata = reference.capture(SnapshotPine())
        self.assertEqual(len(ram), reference.RAM_SIZE)
        self.assertEqual(metadata["active_view"], "0x0")
        self.assertEqual(metadata["hierarchy_stamp"], 0)
        with self.assertRaisesRegex(ValueError, "cambió"):
            reference.capture(SnapshotPine(changed=True))

    def test_tracked_directory_rejected(self):
        repo = SOURCE.parents[2]
        with self.assertRaises(ValueError):
            reference.private_output(repo / "docs" / "ram")
        self.assertEqual(reference.private_output(repo / "logs" / "ram"),
                         (repo / "logs" / "ram").resolve())


if __name__ == "__main__":
    unittest.main()
