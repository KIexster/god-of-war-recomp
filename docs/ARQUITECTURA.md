# Arquitectura

## Pipeline de compilación

`scripts/compilar.ps1` ejecuta estos pasos:

1. **Comprobación de herramientas** — `git`, `cmake`, `ninja` (entorno de MSVC cargado por `2_compilar.cmd`).
2. **PS2Recomp** — clona `ran-j/PS2Recomp` en `<unidad>:\gowport\PS2Recomp`, hace checkout del commit
   `c5a9d02573410a2085a4b4b831b0b68ba3515440` e inicializa submódulos.
3. **Parches** — aplica, en este orden, `patches/ps2recomp-runtime.patch`, `ps2recomp-checkpoint.patch`
   (checkpoints que ceden en la entrada de una función), `ps2recomp-xgkick.patch` (`GOW_XGKICK_IMMEDIATE`,
   opcional) y `ps2recomp-vif-unpack.patch` (UNPACK V2 y V4-5) con `git apply --ignore-whitespace`.
4. **Ajustes de CMake** — añade `src/runner` a los includes de `ps2EntryRunner` y desactiva `/GL` y `/LTCG`
   para compilar en paralelo (con LTCG el enlazado de ~6 400 archivos es inviable).
5. **Recompilador** — compila el objetivo `ps2_recomp`.
6. **Generación de C++** — rellena `config/recomp.template.toml` (`@ELF@`, `@MAP@`, `@OUT@`) y ejecuta
   `ps2_recomp` sobre `SCUS_973.99`.
7. **Runtime** — copia el código generado y `src/gow_overrides.cpp` a `ps2xRuntime/src/runner` y compila
   `ps2EntryRunner` (unity build + PCH).

## Configuración del recompilador

`config/recomp.template.toml` contiene:

- **`stubs`** — funciones de librerías de Sony (`sceMpeg*`, `sceIpu*`, `sceGs*`, `sceDma*`, `sceCd*`, libc…)
  que el runtime implementa de forma nativa.
- **`skip`** y parches de instrucciones generados con `ps2xAnalyzer` y ajustados a mano.

`config/funcmap.csv` (`Name,Start,End,Size`) define los límites de las 6 414 funciones del ELF.

## Overrides (`src/gow_overrides.cpp`)

Se registran con `PS2_REGISTER_GAME_OVERRIDE` para el ELF `SCUS_973.99` (entry `0x00100008`).

| Dirección | Función | Motivo |
|---|---|---|
| `0x00296C48` | `sceSifInitRpc` | El recompilador la descarta: empieza en el delay slot de un `jr ra` suelto |
| `0x00294990` | `iWakeupThread` | Mismo caso que la anterior |
| `0x0027AB00` | `sceCdReadDvdDualInfo` | Sin handler en el runtime; devuelve doble capa con inicio de capa 1 en LBN `2080544` |
| `0x0026BF28` | `snd_SendIOPCommandAndWait` (`989snd`) | Registra cada comando (`[gow-snd]`) y llama al original; con `GOW_SND_STUB=1` responde 0 sin pasar por el IOP |
| `0x00298CE8` | `sceSifLoadStartModuleBuffer` | Módulo IOP embebido `ck01`: se responde como consola retail (`NO_RESIDENT_END`) |

## Parche del runtime (`patches/ps2recomp-runtime.patch`)

| Archivo | Cambio |
|---|---|
| `ps2xIOP/src/modules/gow_stub_services.cpp` *(nuevo)* | Servicio IOP silencioso para el motor de sonido **989snd** (`989nomid.irx`) |
| `ps2xIOP/src/modules/dbcman.cpp` | Responde a los servidores secundarios de `dbcman` (`0x8000131C/E/F`) |
| `ps2xIOP/src/iop_subsystem.cpp`, `module_factories.h`, `CMakeLists.txt` | Registro del nuevo servicio |
| `ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp` | El juego recibe su propio heap |
| `ps2xRuntime/src/lib/ps2_runtime.cpp` | Heap privado del runtime en `0x000A0000–0x000FF000`; anillo de traza de saltos |
| `ps2xRuntime/src/lib/Kernel/EeScheduler.cpp` | Ajuste en `makeRunning` |

## Integración continua

`.github/workflows/pruebas.yml` se ejecuta en cada push a `main` y en cada PR, sin necesitar el juego:

- **Pruebas de `src/`**: compila y ejecuta `tests/pad2_packet_test.cpp` con GCC.
- **Parches y suite del runtime**: descarga PS2Recomp en el commit de `scripts/common.ps1`, aplica los
  parches en el orden de `compilar.ps1` (`tools/ci/parches.py` los lee de ahí y falla si algún
  `patches/*.patch` no se aplica), compila `ps2x_tests` en Linux y ejecuta la suite.
  `tools/ci/comprobar_pruebas.py` solo falla por pruebas que no estén en `tests/fallos_conocidos.txt`
  (hoy, los dos fallos previos de heap/DMA). Cuando una de ellas pase, la CI lo avisa para quitarla de la lista.

`.github/workflows/estado.yml` regenera el mapa de estado (`docs/estado/`) cuando cambian sus datos.
