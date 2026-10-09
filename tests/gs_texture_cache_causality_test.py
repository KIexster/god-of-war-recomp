"""Sonda causal de caché GS: referencia intacta, candidato CPU invalidado por Submit."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Uso: gs_texture_cache_causality_test.py generador replay")
    generator, replay = (Path(arg).resolve() for arg in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="gow_cache_causal_") as temporary:
        root = Path(temporary)

        def run(executable, arguments, expected=0):
            result = subprocess.run([str(executable), *map(str, arguments)], capture_output=True,
                                    text=True, encoding="utf-8", errors="replace", timeout=60)
            if result.returncode != expected:
                raise AssertionError((arguments, result.returncode, result.stdout, result.stderr))
            return result.stdout

        for height in (1, 31, 32, 33, 64, 416):
            fixtures = root / f"altura_{height}"
            run(generator, [fixtures, "--altura", height, "--separaciones"])
            for name in ("self", "disjoint", "nearest"):
                for suffix in ("", "_texflush", "_scissor"):
                    capture = fixtures / f"feedback_gs_{name}{suffix}.bin"
                    output = fixtures / f"resultado_{name}{suffix}"
                    ordinary = run(replay, [capture, "cpu", output])
                    assert "CPU vs captura final: bytes=0 " in ordinary
                    assert "CPU vs cpu final: bytes=0 " in ordinary
                    # TEXFLUSH recarga también en referencia; scissor no invalida.
                    count = 3 * height if name == "self" and height <= 32 and suffix != "_texflush" else 0
                    diagnostic = run(replay, [capture, "cpu", "--invalidar-cache-submit",
                                             "--repeticiones", 2, output], int(count != 0))
                    assert diagnostic.count("CPU vs captura final: bytes=0 ") == 2
                    actual = re.findall(r"CPU vs cpu final: bytes=(\d+) first=(\d+)", diagnostic)
                    assert len(actual) == 2 and all(int(n) == count for n, _ in actual), diagnostic
                    pages = re.findall(r"Cache tras Submit: registro=\d+ draws=(\d+) baseReferencia=(\d+) baseCandidato=(\d+) bytesDistintos=(\d+)", diagnostic)
                    # CT32 de ancho 512: cada nueva franja de 32 filas avanza
                    # ocho páginas físicas; la fuente disjunta suma 2 MiB.
                    page = ((height - 1) // 32) * 65536 + (2 * 1024 * 1024 if name == "disjoint" else 0)
                    assert len(pages) == 4 and [int(draw) for draw, *_ in pages] == [1, 2, 1, 2]
                    assert all(int(a) == page and int(b) == page for _, a, b, _ in pages), diagnostic
                    assert all((int(n) != 0) == (count != 0 and draw == "2") for draw, _, _, n in pages), diagnostic
                    if count:
                        assert all(int(first) == 4096 for _, first in actual), diagnostic
                        assert diagnostic.count("estado final distinto: pagina de cache") == 2
                    assert "Candidato pasada 1 vs 2: bytes=0 " in diagnostic
                    assert "estado=igual framesDistintos=0" in diagnostic
                    assert f"framesDistintos={int(count != 0)} " in diagnostic
                    if height == 1 and name == "self" and not suffix:
                        # RGB concreto del único píxel, independiente del recuento.
                        for candidate, rgb in (("cpu", bytes((141, 90, 98))),
                                               ("candidate", bytes((132, 87, 86)))):
                            image = (output / f"replay_pasada_2_last_{candidate}.ppm").read_bytes()
                            header, pixels = image.split(b"\n255\n", 1)
                            assert header == b"P6\n512 448" and pixels[32 * 3:33 * 3] == rgb

        capture = root / "altura_1" / "feedback_gs_self.bin"
        # Un End adulterado debe seguir devolviendo error de referencia (3).
        corrupt = root / "end_adulterado.bin"
        data = bytearray(capture.read_bytes())
        data[-1] ^= 1
        corrupt.write_bytes(data)
        invalid_end = run(replay, [corrupt, "cpu", "--invalidar-cache-submit", root / "corrupt"], 3)
        assert "CPU vs captura final: bytes=1 " in invalid_end
        assert "CPU vs cpu final:" not in invalid_end
        for mode, options in (("hardware", ["--invalidar-cache-submit"]),
                              ("compute", ["--invalidar-cache-submit"]),
                              ("cpu", ["--invalidar-cache-submit", "--invalidar-cache-submit"]),
                              ("cpu", ["--invalidar-cache-submit", "--snapshot-feedback"])):
            invalid_output = root / "invalid"
            run(replay, [capture, mode, *options, invalid_output], 2)
            assert not invalid_output.exists()
    print("54 patrones: persistencia hasta 32 filas, reemplazo desde 33, TEXFLUSH/scissor y controles exactos; End estricto intacto")


if __name__ == "__main__":
    main()
