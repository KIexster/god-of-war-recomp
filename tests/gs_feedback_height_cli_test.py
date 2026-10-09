"""Reduce patrones GS procedurales y verifica su equivalente GIF; sin datos del juego."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Uso: gs_feedback_height_cli_test.py generador verificador")
    generator, validator = (Path(arg).resolve() for arg in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="gow_feedback_height_") as temporary:
        root = Path(temporary).resolve()

        def run(executable, arguments, expected=0):
            result = subprocess.run([str(executable), *map(str, arguments)], capture_output=True,
                                    text=True, encoding="utf-8", errors="replace", timeout=60)
            if result.returncode != expected:
                raise AssertionError((arguments, result.returncode, result.stdout, result.stderr))

        baseline = root / "default"
        run(generator, [baseline, "--pcsx2"])
        run(validator, [baseline])
        for height in (1, 31, 32, 33, 64, 416):
            output = root / str(height)
            run(generator, [output, "--pcsx2", "--separaciones", "--altura", height])
            run(validator, [output, "--separaciones", "--altura", height])
            # El freeze y los registros iniciales son constantes: solo cambia el GIF de geometría.
            frozen_prefix = 8 + 4 * 1024 * 1024 + 448 + 8192
            for variant in ("self", "disjoint", "nearest"):
                name = f"feedback_gs_{variant}.gs"
                assert (baseline / name).read_bytes()[:frozen_prefix] == (output / name).read_bytes()[:frozen_prefix]
            # Un valor esperado distinto debe detectar la altura del payload.
            run(validator, [output, "--altura", 2 if height == 1 else 1], 1)
            if height == 1:
                # CT32: (32,0) está en el byte 4096; el End termina con los 4 MiB de VRAM.
                # Media entera de texels iniciales (31,0)/(32,0), con alfa conservada.
                data = (output / "feedback_gs_self.bin").read_bytes()
                offset = len(data) - 4 * 1024 * 1024 + 4096
                assert data[offset:offset + 4] == bytes((141, 90, 98, 128))
            if height == 416:
                for variant in ("self", "disjoint", "nearest"):
                    name = f"feedback_gs_{variant}.gs"
                    assert hashlib.sha256((baseline / name).read_bytes()).digest() == hashlib.sha256((output / name).read_bytes()).digest()
        invalid = [["--altura"], ["--altura", "0"], ["--altura", "417"],
                   ["--altura", "-1"], ["--altura", "1x"], ["--altura", "1.5"],
                   ["--altura", "nan"], ["--altura", "9999999999999999999999"],
                   ["--altura", "1", "--altura", "1"]]
        for arguments in invalid:
            output = root / "invalid"
            run(generator, [output, *arguments], 2)
            assert not output.exists(), "Una opción inválida creó archivos"
            run(validator, [baseline, *arguments], 2)
        run(validator, [baseline, "--oraculos", "--altura", 1], 2)
    print("Alturas 1/31/32/33/64/416: freeze/GIF/End CPU exactos; default intacto; opciones inválidas rechazadas")


if __name__ == "__main__":
    main()
