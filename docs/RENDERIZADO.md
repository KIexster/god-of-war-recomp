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

`ps2recomp-gs-presentation.patch`, aplicado después del backend, incorpora la selección
`preferredSource` del frontend con las mismas condiciones que el CPU para un solo circuito
activo. Esa fuente es una textura con base en **bloques de 256 B**; `DISPFB` usa **páginas de
8 KB**. El shader recibe bases en bloques: convierte la base CRTC una vez y conserva todos
los bits de la fuente preferida. Usa su stride/formato y origen cero, sin alterar el destino
CRTC. Los formatos admitidos son CT32, CT24, CT16 y CT16S. Dos circuitos activos conservan
la composición CRTC. Se mantiene la procedencia de la imagen en el readback diferido y
se incluye la selección en la clave de la presentación compartida, aunque esta última
continúa desactivada en la integración del port.

Con `SMODE2 & 3 == 1`, el compositor duplica las filas del campo seleccionado por
`vsyncTick & 1`: par e impar se alternan usando la paridad real del GS. La clave de
presentación compartida también incluye el campo. El modo progresivo conserva sus filas
y no depende de esa paridad. Con un solo circuito activo se muestra su RGB directamente,
sin mezclarlo contra el fondo por el alfa del píxel; con dos se conserva la mezcla PMODE.

En la **prueba inicial anterior a esta corrección** se ve el menú con defectos y, en estado 11, agua oscura sin
Kratos ni el entorno completo. Las capturas tardías OpenGL de `56–130 s` son idénticas;
las del CPU de `70–130 s` cambian. La mejora de `vid::Flip` no equivale a más imágenes distintas.

Las regresiones de presentación se ejecutan con GPU real, tanto compute como rasterizado
gráfico. Comparan los cuatro formatos con el CPU, una fuente no alineada a páginas, un stride
distinto, origen CRTC no nulo y actualizaciones posteriores. También comprueban destino
incompatible, ausencia de fuente, formato no admitido y composición de dos circuitos. Comparan
campos par/impar, modo progresivo y píxeles sin alfa. Otra muestra usa la temporización
completa de GoW con un patrón sintético de 512×448, sin assets del juego. Antes del arreglo
fallan ambas pruebas GPU (**472/474**); con el arreglo pasan **474/474**.

La observación temporal del juego encontró dos circuitos activos, sin fuente preferida,
con `PMODE=0x8023` y modo de campos. Se compararon ambos compositores sobre la **misma VRAM
obtenida del GPU**: las imágenes diferían porque el CPU seleccionaba el campo y OpenGL
omitía esa paridad. Esto acota
esa diferencia al compositor, sin atribuirla a VU1 ni a un fallo general de escritura de
vértices. Los diagnósticos temporales se retiraron tras localizar la causa. La alternancia
de campos por sí sola no acredita animación ni una partida jugable.

Una ejecución de 155 s con el arreglo, antes de integrar EE/FPU, inicializa la GPU real
sin fallback y alcanza el estado 11. Las cuatro capturas de `70–130 s` son distintas,
frente a las capturas tardías idénticas anteriores. Sigue apareciendo agua oscura, sin
Kratos ni el entorno completo. Esta comprobación verifica la salida del compositor;
la geometría y una partida jugable siguen pendientes.

Tras integrar el parche EE/FPU de Opus y reconstruir las 6418 unidades, la suite nativa
con GPU real pasa **481/481**, y las pruebas separadas de caché GS **45/45**. Las cifras
anteriores de 474 corresponden al runtime previo a esa integración.

La prueba combinada del juego también llega al estado 11 sin fallback. Con los
diagnósticos activados guarda tres capturas distintas a `94,85 / 95,19 / 110,10 s`,
con agua y artefactos; todavía sin Kratos ni el escenario completo. No es un perfil
de rendimiento. Las posiciones observadas mantienen la plantilla deliberada del
loader y los dos buffers no nulos cambiantes; ver el registro en `docs/ESTADO.md`.

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

La traza posterior, con EE/FPU integrados, vincula mediante las direcciones DMA dos de
esas plantillas (`0x812390` y `0x7F3220`) con sus buffers de `LoadClient`. Llegan en cero
al UNPACK y se observan así en memoria VU1 antes de transformarse. Los dos buffers
actualizados por `0x1FB800` no aparecen como origen de payload en las ocho cadenas
muestreadas; su envío sigue pendiente de identificar. En los primeros 256 paquetes
PATH1 hay 2.654 vértices, 337 con kick y 2.317 sin kick (ADC/XYZ3), con casos de XYZ
repetidos y otros de posiciones distintas. Estos resultados localizan datos y descartes
anteriores al backend GS; no demuestran por sí solos un fallo de VIF/VU1 ni corrigen
la escena ausente. Direcciones, replay y límites: [`ESTADO.md`](ESTADO.md).

## Inspección offline de GIF

El inspector usa solo la biblioteca estándar de Python y recibe un paquete binario
o una carpeta de capturas locales `gow_geo_gif_*.bin`:

```powershell
python tools/gs/inspeccionar_paquetes.py logs/gs_geometry_probe --json logs/gif_summary.json
python -m unittest discover -s tests -p test_gif_inspector.py
```

Valida tamaños de PACKED, REGLIST (incluido su padding) e IMAGE; el formato 3 no está
soportado. Cuenta escrituras XYZF2/XYZ2/XYZF3/XYZ3 y A+D, distinguiendo kick de ADC/XYZ3.
El JSON contiene métricas, rangos XYZ, etiquetas y SHA-256 por archivo; no copia los
payloads. Los tipos PRIM solo cuentan etiquetas PACKED con PRE: no reconstruyen todo
el estado GS ni las primitivas que efectivamente rasteriza. Los valores X/Y conservan
sus unidades GS, antes de aplicar XYOFFSET. Un kick no garantiza un triángulo visible.

La captura de esta investigación usó sondas temporales VIF/VU1 y un replay local, ya
retirados. `GOW_GEOMETRY_DIAG` y `GOW_REPLAY_STEM` no son opciones del runtime publicado.
Conservar capturas e informes bajo `logs/`; no subir RAM, microcódigo ni datos del juego.
