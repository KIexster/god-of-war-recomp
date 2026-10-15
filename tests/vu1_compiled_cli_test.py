"""GOW-Port: el control debe ejecutar pares compilados reales, además de dar paridad."""
import os
import re
import subprocess
import sys

# GOW-Port: con el runtime de Waffle565 la VU1 nativa va primero; se apaga para ejercitar los pares compilados.
env = dict(os.environ, GOW_VU1C_DIAG="1", GOW_VU1_NATIVE_OFF="1")
for name in ("GOW_VU1C_RANGO", "GOW_VU1C_SIN_BLOQUES", "GOW_VU1_SIN_COMPILAR"):
    env.pop(name, None)
result = subprocess.run([sys.argv[1]], env=env, capture_output=True, text=True, timeout=120)
match = re.search(r"\[vu1c\] compilados=(\d+) interpretados=(\d+)", result.stderr)
if result.returncode or not match or any(int(value) == 0 for value in match.groups()):
    raise AssertionError((result.returncode, result.stdout, result.stderr))
div = re.search(r"VU1 DIV: (\d+) casos exactos", result.stdout)
if not div or int(div[1]) < 19072:
    raise AssertionError(("Falta el control DIV completo", result.stdout, result.stderr))
print(result.stdout.strip())
print(f"Ruta real: {match[1]} pares compilados, {match[2]} interpretados")
