"""Referencia local de EE por PINE; solo lectura, PCSX2 pausado, SCUS-97399."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import struct

RAM_SIZE = 0x2000000
MAX_REPLY = 450000
EXPECTED_ID = "SCUS-97399"
EXPECTED_CRC = "d6385328"


class Pine:
    # Formato de pcsx2/PINE.cpp (PCSX2, GPL-3.0+): longitud LE incluye cabecera.
    # No se implementan comandos de escritura ni carga/guardado de estados.
    def __init__(self, connection):
        self.connection = connection

    def exact(self, size):
        result = bytearray()
        while len(result) < size:
            chunk = self.connection.recv(size - len(result))
            if not chunk:
                raise EOFError("PINE cerró la conexión")
            result.extend(chunk)
        return bytes(result)

    def query(self, request):
        self.connection.sendall(struct.pack("<I", len(request) + 4) + request)
        size = struct.unpack("<I", self.exact(4))[0]
        if not 5 <= size < MAX_REPLY:
            raise ValueError("Longitud de respuesta PINE inválida")
        response = self.exact(size - 4)
        if response[0] != 0:
            raise ValueError("PCSX2 rechazó la lectura PINE")
        return response[1:]

    def status(self):
        response = self.query(b"\x0f")
        if len(response) != 4:
            raise ValueError("Estado PINE truncado")
        return struct.unpack("<I", response)[0]

    def string(self, command):
        if command not in (8, 12, 13):
            raise ValueError("Comando de identidad inválido")
        response = self.query(bytes([command]))
        if len(response) < 5:
            raise ValueError("Identidad PINE truncada")
        size = struct.unpack_from("<I", response)[0]
        value = response[4:]
        if size != len(value) or not value.endswith(b"\0"):
            raise ValueError("Longitud de identidad PINE inválida")
        return value[:-1].decode("utf-8")

    def read(self, address, size):
        if (address < 0 or size <= 0 or address % 4 or size % 4
                or address + size > RAM_SIZE or size > 65536):
            raise ValueError("Lectura fuera del rango/alineación de EE")
        request = b"".join(b"\x02" + struct.pack("<I", at)
                           for at in range(address, address + size, 4))
        response = self.query(request)
        if len(response) != size:
            raise ValueError("Lectura de RAM PINE truncada")
        return response

    def identity(self):
        if self.status() != 1:
            raise ValueError("Pausa PCSX2 antes de obtener la referencia")
        serial, crc = self.string(12), self.string(13).lower()
        if serial != EXPECTED_ID or crc != EXPECTED_CRC:
            raise ValueError(f"Edición incorrecta: {serial}, {crc}")
        return {"serial": serial, "crc": crc, "version": self.string(8)}


def capture(pine):
    identity = pine.identity()
    before = pine.read(0x29BDF8, 8)
    ram = b"".join(pine.read(at, 65536) for at in range(0, RAM_SIZE, 65536))
    after = pine.read(0x29BDF8, 8)
    if pine.identity() != identity or before != after or ram[0x29BDF8:0x29BE00] != before:
        raise ValueError("La identidad o la jerarquía cambió durante la lectura")
    metadata = dict(identity, ram_bytes=len(ram), sha256=hashlib.sha256(ram).hexdigest(),
                    state=struct.unpack_from("<I", ram, 0x29E560)[0],
                    hierarchy_stamp=struct.unpack("<Q", before)[0],
                    active_view=hex(struct.unpack_from("<I", ram, 0x33104C)[0]),
                    limitation="Pausa y marca estable; no garantiza el mismo cuadro que el port.")
    return ram, metadata


def private_output(directory):
    directory = directory.resolve()
    repo = Path(__file__).resolve().parents[2]
    if directory.is_relative_to(repo) and not directory.is_relative_to(repo / "logs"):
        raise ValueError("Dentro del repositorio, usa logs/ para mantener la RAM fuera de Git")
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=28012)
    parser.add_argument("--output", type=Path, required=True,
                        help="Directorio nuevo fuera del repositorio o dentro de logs/")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("Puerto inválido")
    directory = private_output(args.output)
    if directory.exists():
        parser.error("El directorio de salida ya existe; usa otro nombre")
    with socket.create_connection(("127.0.0.1", args.port), timeout=5) as connection:
        ram, metadata = capture(Pine(connection))
    # Crear solo después de validar la lectura y no sobrescribir referencias anteriores.
    directory.mkdir(parents=True, exist_ok=False)
    with (directory / "eeMemory.bin").open("xb") as output:
        output.write(ram)
    with (directory / "reference.json").open("x", encoding="utf-8") as output:
        json.dump(metadata, output, indent=2, ensure_ascii=False)
        output.write("\n")
    print(json.dumps(metadata, ensure_ascii=False))


if __name__ == "__main__":
    main()
