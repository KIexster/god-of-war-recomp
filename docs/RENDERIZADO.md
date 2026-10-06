# Renderer CPU y OpenGL

`patches/ps2recomp-gs-opengl.patch` adapta `GSGpuBackend` y `GSThreadedBackend` del fork
de [Taylor N. Albarnaz / LightVelox](https://github.com/LightVelox/PS2Recomp/tree/ac9efa070638ad3b3accd284de6f898d5ab271d1),
rama `sotc-port`, commit `ac9efa070638ad3b3accd284de6f898d5ab271d1`, bajo **GPL-3.0**.
Gracias a sus autores por el backend y el trabajo del port
[sotc-vibe-pc](https://github.com/LightVelox/sotc-vibe-pc). Los archivos importados llevan
`// GOW-Port:` y el commit completo de origen. Los helpers del GPU quedan separados de los
del renderer CPU existente para conservar la comparación.

El CPU sigue siendo la opción predeterminada. El parche solo modifica el GS, su integración
CMake y sus pruebas. No modifica EE, FPU, IOP, VIF, VU1 ni `ps2recomp-fpu-roots.patch`.

## Selección

Después de `scripts\2_compilar.cmd`, en PowerShell:

```powershell
# CPU directo: referencia conservada.
$env:PS2X_GS_GPU = '0'; $env:PS2X_GS_THREAD = '0'

# CPU en el hilo GS: permite separar el efecto de la cola del rasterizado GPU.
$env:PS2X_GS_GPU = '0'; $env:PS2X_GS_THREAD = '1'

# OpenGL en el hilo GS.
$env:PS2X_GS_GPU = '1'; $env:PS2X_GS_THREAD = '0'
scripts\ejecutar.ps1 -Segundos 150
```

Esta adaptación crea un contexto **WGL en Windows con shaders OpenGL 4.6**. Usa rasterizado
gráfico cuando están disponibles las funciones necesarias, con camino compute para los otros
casos. Si falla el contexto o la compilación inicial de shaders, el wrapper cambia al CPU y
emite `[gow-gs] OpenGL no disponible; usando CPU en el hilo GS.`. Ese fallback no cuenta como
validación de GPU. En Linux se compila el backend, pero su contexto es un stub y usa el fallback.

Las lecturas, FINISH y Flush respetan los comandos anteriores. La cola copia los datos enviados
por el EE, conserva cambios de estado y drena sus últimos comandos antes de destruir el contexto
en el hilo propietario. Las tablas de VRAM se inicializan una vez. La presentación se adapta a
las filas de 640 píxeles del frontend existente; aún no se integra la presentación compartida
entre contextos GL del fork. La salida pasa por la copia de píxeles habitual del port.

El GPU presenta los circuitos CRTC del GS. Todavía no incorpora la selección heurística
`preferredSource` específica del CPU de GoW; al comparar capturas hay que registrar ambos
framebuffers y sus direcciones. Una diferencia visual no demuestra por sí sola un fallo del
rasterizador. La prueba sintética usa CRTC explícito sin esa heurística.

En las ejecuciones del juego se ve el menú con defectos y, en estado 11, agua oscura sin
Kratos ni el entorno completo. Las capturas tardías OpenGL de `56–130 s` son idénticas;
las del CPU de `70–130 s` cambian. La imagen incompleta y la diferencia de presentación
siguen pendientes. La mejora de `vid::Flip` no equivale a más imágenes distintas.

## Comparación reproducible

```powershell
scripts\probar_rendimiento.ps1 -Renderer cpu -SoloCuadros -Etiqueta gs_cpu
scripts\probar_rendimiento.ps1 -Renderer cpu-hilo -SoloCuadros -Etiqueta gs_cpu_hilo
scripts\probar_rendimiento.ps1 -Renderer opengl -SoloCuadros -Etiqueta gs_opengl
```

Las ejecuciones deben ser consecutivas, con el mismo ELF, ISO, binario, mandos y escena, sin
capturas ni diagnósticos pesados durante el perfil. El script restaura las variables al terminar.
Comparar `vid::Flip` en estado 11 y presentación del host por separado. El reparto exclusivo
EE/IOP/GS/VU del perfil original describe el hilo del juego; al mover el GS a otro hilo ya no
contabiliza todo su trabajo y no sirve para comparar porcentajes de coste entre renderers.

Prueba local del 2026-10-05, AMD Radeon RX 5700 XT, mismo binario y tres ejecuciones
consecutivas. En cada una se toman tres ventanas completas de 5 s dentro de `120–136 s`,
con el juego en estado 11. Se omiten capturas y diagnósticos de geometría:

| Renderer | `vid::Flip`/s del juego | Presentaciones/s del host |
|---|---:|---:|
| CPU directo | 2,40 | 56,33 |
| CPU con hilo | 2,39 | 55,68 |
| OpenGL con hilo | 3,26 | 59,12 |

En esta muestra, OpenGL mejora aproximadamente un 36 % frente al CPU directo. Son llamadas
a `vid::Flip`, no imágenes distintas ni una medida de jugabilidad. Una ejecución por modo
y unos 15 s de observación son una comparación inicial, sin estimar variabilidad. El hilo GS
con CPU no aporta una mejora en esta muestra. Las capturas se revisan en ejecuciones separadas.

La suite normal comprueba propiedad de payloads, orden de transferencias, estados compactos,
barreras, destrucción de la cola y equivalencia CPU directo/CPU con hilo. Contrasta el
direccionamiento y máscaras de los 13 formatos de VRAM con la implementación existente.

Con una GPU local, habilitar las dos pruebas adicionales antes de ejecutar `ps2x_tests` desde
la raíz de PS2Recomp:

```powershell
$env:GOW_GS_GPU_TEST = '1'
out\build\ps2xTest\ps2x_tests.exe
Remove-Item Env:GOW_GS_GPU_TEST
```

Estas pruebas exigen que se inicialice el GPU real. Comparan clear, sprites, transferencias y
VRAM completa; comprueban el interior/exterior de un triángulo plano, textura CT32 y los píxeles
de presentación. Corren tanto con compute forzado como con rasterizado gráfico permitido.
Cubren esos casos sintéticos; no certifican toda la precisión GS ni una partida jugable.

## Productor de posiciones

`GOW_EE_PRIM_DIAG=1` añade observación limitada de `renEEPrim::InitUNPACKData` (`0x141350`) y
`GetUpdateAddress` (`0x1417D8`). Registra objeto, caller, tipo, chunk, buffer y dirección devuelta;
para posiciones, muestra el primer vector **antes de que el caller lo rellene**. Un checkpoint
se etiqueta como tal y no se trata como retorno. No cambia los datos del juego.

Las muestras se limitan a 64 inicializaciones, 64 direcciones antes del estado 11 y 256 en ese
estado. Con el mando automático se releen cada 5 s hasta 16 direcciones observadas para ver
cambios posteriores. Esos buffers pueden reutilizarse; la lectura tardía no garantiza que el
objeto original siga siendo su propietario. El perfil elimina esta variable para evitar mezclar
la investigación con la medición.
Los ceros previos a la escritura pueden ser una reserva válida: hay que contrastar el buffer
después del productor y el payload DMA/VIF antes de atribuir un fallo. Repetir la prueba cuando
Opus integre sus arreglos de semántica EE/FPU.

La primera ejecución de este diagnóstico registra 64 inicializaciones, 298 retornos de
posiciones y 192 lecturas posteriores. El caller `0x12E258`, dentro de `LoadClient`
(`0x12DE70`), obtiene 40 buffers. En las 14 direcciones de ese caller que conserva la
sonda, todas las lecturas posteriores muestran `(0,0,0,0x8000)`. La inspección del MIPS
original confirma que el bucle `0x12E270–0x12E288` escribe precisamente esa plantilla:
es una inicialización explícita del juego, no una posición calculada que el diagnóstico
haya visto perderse. La primera reserva incluye los chunks observados anteriormente en
el payload VIF con XYZ cero.

El caller `0x1FB800`, dentro de la función `0x1FB4B8`, devuelve dos buffers con XYZ no
nulo y cambiante, también en sus lecturas posteriores. No se observa en esta muestra un
retorno de `GetUpdateAddress` desde `goWater::InitEEPrim` o `UpdateEEPrim`. Estos resultados
acotan la investigación: seguir la transformación de esas plantillas, su selección de
datos y el resultado que VU1 entrega a GIF; no sustituirlas arbitrariamente por posiciones.
