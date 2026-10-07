# Rendimiento

## Perfil por muestreo (`tools/perfil/muestrear.cpp`)

`GOW_PERF_DIAG` reparte el tiempo por subsistema, pero no dice qué función lo consume. Este
perfilador externo suspende cada hilo del juego cada ~1 ms, lee su contador de programa y agrupa
las muestras por función con DbgHelp. No modifica el juego.

Para ver nombres, el ejecutable necesita su PDB. En una carpeta de trabajo propia (`GOW_WORK`), tras
`scripts\2_compilar.cmd`, se puede reconfigurar con información de depuración sin regenerar el C++:

```bat
cmake -S %GOW_WORK%\PS2Recomp -B %GOW_WORK%\PS2Recomp\out\build "-DCMAKE_CXX_FLAGS=/Zi /DWIN32 /D_WINDOWS /EHsc" "-DCMAKE_C_FLAGS=/Zi /DWIN32 /D_WINDOWS" "-DCMAKE_EXE_LINKER_FLAGS=/DEBUG"
cmake --build %GOW_WORK%\PS2Recomp\out\build --target ps2EntryRunner
cl /nologo /O2 /EHsc /utf-8 /std:c++20 tools\perfil\muestrear.cpp /Fe:logs\muestrear.exe /Fo:logs\muestrear.obj
logs\muestrear.exe <pid de ps2EntryRunner> 30
```

Las unidades de C++ generado deben recompilarse para incluir la información de depuración (tocar los
archivos `Unity\*.cxx` de `ps2EntryRunner`, como hace `2_recompilar_rapido.cmd`). Las muestras en
DLLs del sistema se atribuyen, cuando la pila se puede recorrer, al primer marco del ejecutable.

## getenv en bucles calientes (2026-10-07)

Con OpenGL, `GOW_SKIP_FMV=1` y `GOW_FAST_BOOT=1`, el perfil de la partida mostraba un **38 % del
hilo del juego en `ucrtbase!strchr`**. Era `getenv`, que recorre todo el entorno en cada llamada:

- el bucle ocioso del IOP (`iop_emulator.cpp`, sin hilos listos) consultaba `PS2X_IOP_PC_EVERY`
  en cada vuelta;
- cada XGKICK de VU1 consultaba `GOW_XGKICK_IMMEDIATE`;
- algunos overrides llamados a menudo (`GOW_FAST_BOOT`, `GOW_PATH_DIAG`, `GOW_ANM_DIAG`,
  `GOW_EE_PRIM_DIAG`).

`patches/ps2recomp-getenv-hot.patch` lee esas variables una vez. Para conservar las pruebas que
cambian `GOW_XGKICK_IMMEDIATE` durante la ejecución, `ps2Vu1ReloadEnvironmentOptions()` vuelve a
leerla. `src/gow_overrides.cpp` guarda sus valores en variables estáticas.

Medición con `scripts\probar_rendimiento.ps1 -Renderer opengl -Segundos 240` (ventanas de 5 s en
estado 11, entre 220 y 235 s):

| | IOP | VU | `vid::Flip`/s |
|---|---:|---:|---:|
| Antes | 2,1–2,3 s | 2,4–2,6 s | 1,6–1,8 |
| Después | 0,26–0,38 s | 3,5–4,2 s | 1,8–2,6 |

El tiempo liberado del IOP pasa a VU1, que ahora ocupa más del 80 % del hilo del juego. En el perfil,
VU1 se reparte entre `commitReadyPipelines`, `normalizeOperand`, `calculatePairReadyCycle`,
`updateFmacFlags` y el resto del intérprete con modelo de latencias. La suite nativa pasa
**545/545** con el parche.
