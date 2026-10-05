# Arquitectura

## Pipeline de compilación

`scripts/compilar.ps1` ejecuta estos pasos:

1. **Comprobación de herramientas** — `git`, `cmake`, `ninja` (entorno de MSVC cargado por `2_compilar.cmd`).
2. **PS2Recomp** — clona `ran-j/PS2Recomp` en `<unidad>:\gowport\PS2Recomp`, hace checkout del commit
   `c5a9d02573410a2085a4b4b831b0b68ba3515440` e inicializa submódulos.
3. **Parches** — aplica, en este orden, `patches/ps2recomp-runtime.patch`, `ps2recomp-checkpoint.patch`
   (checkpoints que ceden en la entrada de una función), `ps2recomp-xgkick.patch` (`GOW_XGKICK_IMMEDIATE`,
   opcional), `ps2recomp-vif-unpack.patch` (UNPACK V2 y V4-5), `ps2recomp-vif-diagnostic.patch`
   (diagnósticos acotados de VIF y XGKICK), `ps2recomp-heap.patch` (heap privado del runtime configurable
   con `setPrivateGuestHeap`), `ps2recomp-vu-jump.patch` (JR/JALR leen el destino VI actual),
   `ps2recomp-vu-efu.patch` (coeficiente de la serie de `EATAN`),
   `ps2recomp-mpeg-nodata.patch` (`sceMpegGetPicture` llama al callback `sceMpegCbNodata`),
   `ps2recomp-spu2.patch` (emulación del SPU2 en el IOP, fase 1),
   `ps2recomp-fpu-roots.patch` (operandos de `SQRT.S` y `RSQRT.S` en el recompilador de COP1),
   `ps2recomp-spu2-output.patch` (salida del SPU2 por el audio del PC) y
   `ps2recomp-sio2.patch` (SIO2 y memory card emulados en el IOP),
   `ps2recomp-perf.patch` (perfil opcional de tiempos exclusivos y contadores de presentación),
   `ps2recomp-dma-chain.patch` (cadenas DMA de más de 4096 tags),
   `ps2recomp-vif-direct.patch` (una IMAGE de PATH2 continúa con el siguiente DIRECT),
   `ps2recomp-ee-branches.patch` (`BLEZ`/`BGTZ`/`BLTZ`/`BGEZ` comparan 64 bits) y
   `ps2recomp-vu0-div.patch` (`VDIV`/`VSQRT`/`VRSQRT` de VU0 con semántica de hardware)
   con `git apply --ignore-whitespace`. Antes de reaplicarlos borra los archivos que dejó la compilación
   anterior y que algún parche crea (`new file mode`), así un parche nuevo no necesita tocar esa limpieza.
4. **Ajustes de CMake** — añade `src/runner` a los includes de `ps2EntryRunner` y desactiva `/GL` y `/LTCG`
   para compilar en paralelo (con LTCG el enlazado de ~6 400 archivos es inviable).
5. **Recompilador** — compila el objetivo `ps2_recomp`.
6. **Generación de C++** — rellena `config/recomp.template.toml` (`@ELF@`, `@MAP@`, `@OUT@`) y ejecuta
   `ps2_recomp` sobre `SCUS_973.99`.
7. **Runtime** — copia el código generado y `src/gow_overrides.cpp` a `ps2xRuntime/src/runner` y compila
   `ps2EntryRunner` (unity build + PCH).

Las trazas `PS2X_ENABLE_RUNTIME_LOGS`, `PS2X_ENABLE_AGRESSIVE_LOGS` y `PS2X_ENABLE_IOP_RPC_TRACE`
se configuran en `OFF` por defecto; `scripts\2_compilar.cmd -Trazas` las activa para investigar.
El perfil `GOW_PERF_DIAG` es independiente de estas opciones de compilación; se describe en
[`ESTADO.md`](ESTADO.md#medicion-de-rendimiento-2026-10-05).

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

- **Comprobaciones rápidas** (segundos):
  - `tools/ci/validar_config.py` valida `config/funcmap.csv` (orden, solapes, tamaños, nombres) y
    `config/recomp.template.toml` (marcadores `@ELF@/@MAP@/@OUT@`, formato `nombre@0xDIRECCION`, direcciones
    dentro de alguna función). Los stubs que no empiezan una función y las direcciones repetidas son avisos.
  - `tools/ci/analizar_ps1.ps1` analiza sintácticamente todos los `.ps1` con el parser de PowerShell.
  - Compila y ejecuta `tests/pad2_packet_test.cpp` con GCC.
- **Parches y suite del runtime**: descarga PS2Recomp en el commit de `scripts/common.ps1`, aplica los
  parches en el orden de `compilar.ps1` (`tools/ci/parches.py` los lee de ahí y falla si algún
  `patches/*.patch` no se aplica), comprueba que cada stub de `recomp.template.toml` tiene handler en
  `ps2_call_list.h` (`validar_config.py --runtime`), compila `ps2x_tests` en Linux y ejecuta la suite. También compila
  `src/*.cpp` contra las cabeceras del runtime ya parcheado (declarando las `sub_*` que usan), así un override
  que use una función del runtime que ningún parche define falla aquí y no solo en Windows.
  `tools/ci/comprobar_pruebas.py` solo falla por pruebas que no estén en `tests/fallos_conocidos.txt`
  (hoy vacía: la suite pasa entera). Cuando una prueba de la lista pase, la CI lo avisa para quitarla.

`.github/workflows/estado.yml` regenera el mapa de estado (`docs/estado/`) cuando cambian sus datos.
