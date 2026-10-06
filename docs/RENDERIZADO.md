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

La misma variable también observa la entrada de `renEEPrimContext::ProcessServer`
(`0x141B78`). Guarda hasta 64 muestras antes del estado 11 y otras 64 dentro de él.
Por muestra recorre hasta 256 nodos, comprueba límites y ciclos, y registra cámara,
ID/máscara de vista y pertenencia de los objetos observados por `GetUpdateAddress`.
`candidates` solo cuenta los que pasan los primeros filtros de cámara, vista e índice
de DMA en esa fotografía; no acredita visitas posteriores, rasterizado ni dibujos.
Con `invalid`, `cycle` o `truncated` activos, una ausencia en la muestra no permite
afirmar que el objeto no pertenece al resto de la lista. Los registros usan las etiquetas
`[gow-eeprim:context]` y `[gow-eeprim:member]`; no modifican memoria ni índices y las
reanudaciones tras checkpoints llaman directamente al original. Tampoco se usan para medir FPS.

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

## Transferencias DIRECT de PATH2

`ps2recomp-vif-direct.patch` adapta la corrección de Claude en
[`540defd`](https://github.com/KIexster/god-of-war-recomp/commit/540defd): una IMAGE pendiente
solo consume el payload de los siguientes DIRECT/DIRECTHL. Los comandos VIF entre ellos
siguen ejecutándose. La prioridad corresponde al comando actual, incluso si cambia entre
DIRECT y DIRECTHL al continuar la misma imagen.

`ps2recomp-vif-direct-fragments.patch` conserva por separado el tamaño pendiente de un
DIRECT que llega dividido entre bloques DMA o escrituras FIFO. Su acumulador está limitado
a 65.536 QW (1 MiB, también para IMMEDIATE=0). Solo copia las cargas incompletas; la ruta
que recibe un DIRECT completo sigue enviando sus bytes directamente. Inicializar la memoria
o escribir VIF1_FBRST.RST descarta esa continuación y su prioridad.

Estos dos parches completan el DIRECT antes de entregarlo al parser GS. La continuación
PACKED/REGLIST entre distintos comandos DIRECT se añade después con `ps2recomp-gif-stream.patch`,
descrito abajo. No se reproducen todos los stalls ni los ciclos del hardware ni se añade
continuación de otros comandos VIF. La referencia de comportamiento
es [`_vifCode_Direct` de PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Vif_Codes.cpp),
que conserva el tamaño pendiente y distingue DIRECT de DIRECTHL; no se ha copiado su código.
Se mantiene el crédito del port GS a Taylor N. Albarnaz / LightVelox indicado al inicio.

Las regresiones comprueban píxeles IMAGE, comandos MARK/STCYCL/ITOP intermedios, nuevos
GIFtags después de una imagen, prioridad DIRECTHL, PACKED/REGLIST con todos los cortes
de byte en un payload de 32 B, escrituras FIFO, el tamaño máximo y reset. No contienen
datos del juego. `GOW_VIF_DIAG=1` añade hasta 16 mensajes `[gow-vif-direct]` de inicio y
otros 16 de finalización de transferencias fragmentadas. No habilitarlo para medir FPS.

## Orden de los paquetes GIF

`ps2recomp-gif-order.patch` conserva el orden FIFO dentro de PATH1, PATH2 y PATH3. La cola
agrupa por path con una comparación estricta y elige después entre sus cabeceras: PATH1
tiene prioridad; un DIRECTHL de PATH2 espera a una IMAGE que esté en la cabecera de PATH3.
Un DIRECT normal conserva su prioridad sobre PATH3. No se adelanta una IMAGE a su setup ni
un DIRECT posterior a un DIRECTHL previo del mismo canal.

La excepción DIRECTHL/IMAGE usada antes dentro de `std::stable_sort` no cumplía el
[orden débil estricto exigido por C++](https://eel.is/c++draft/alg.sorting.general#3).
Dos regresiones cubren todas las permutaciones de esos grupos y la prioridad PATH1.
Este arreglo ordena paquetes completos en la abstracción actual; no reproduce todos los
stalls, preempciones ni ciclos del GIF real. Las pruebas son sintéticas y no acreditan
por sí solas una mejora en la imagen o los FPS del juego.

## Etiquetas GIF vacías y PRE

`ps2recomp-gif-tag-semantics.patch` corrige el frontend que comparten los backends CPU y
OpenGL. Una etiqueta con `NLOOP=0` no emite registros: conserva PRIM, los vértices pendientes
y Q. `PRE/PRIM` de la etiqueta solo se aplica en PACKED con datos, y se ignora en REGLIST
e IMAGE. Se corrigen tanto `processGIFPacket` como la ruta PACKED nativa validada. El
atajo de subida IMAGE también respeta PRE del setup PACKED y el reinicio de Q de las
etiquetas no vacías, conservando los bytes subidos e ignorando PRE de IMAGE.

La regla coincide con `GSState::Transfer` de
[PCSX2, commit 32ac6e2](https://github.com/PCSX2/pcsx2/blob/32ac6e23e4aaf8c8c5e74a6c1ed750ee7672120e/pcsx2/GS/GSState.cpp#L3371),
que referencia la sección 7.2.2 del manual EE. El arreglo y las pruebas son propios;
no se incorpora código de PCSX2. Cuatro regresiones comprueban el estado, la conservación
de un triángulo entre etiquetas vacías y Q, los controles con PACKED no vacío y PRE,
y los efectos del setup en la subida IMAGE nativa.
Este parche no modifica VIF; la continuación entre DIRECT distintos se añade en el
siguiente parche.

## Continuidad del flujo GIF por PATH

`ps2recomp-gif-stream.patch` conserva por separado el cursor de PATH1, PATH2 y PATH3:
etiqueta incompleta, formato, registro actual, registros pendientes, padding REGLIST y
bytes IMAGE. Cada cursor retiene como máximo 15 bytes de una unidad incompleta. Las
subidas IMAGE contiguas siguen llegando al backend en bloques grandes; no se almacena
otra copia del payload completo en el frontend.

Un nuevo bloque continúa la etiqueta pendiente de su PATH, en lugar de leer sus datos
como otra GIFtag. Q y PRE se aplican al completar una etiqueta, conservando las reglas
de etiquetas vacías. El estado del GS y los registros siguen siendo compartidos; solo
el cursor de lectura es independiente. Reset elimina esos cursores. Los atajos PACKED
y DMA IMAGE se rechazan cuando PATH3 tiene un flujo incompleto, para que lo complete el
parser general.

El callback del árbitro conserva el identificador del PATH; los clientes anteriores de
dos argumentos mantienen su interfaz. VIF entrega los bytes originales de cada DIRECT:
se retiran las etiquetas IMAGE sintéticas que antes envolvían las continuaciones, pues
el frontend conserva ahora su tamaño pendiente. La prueba existente de continuación
comprueba por eso el payload original junto a la etiqueta siguiente, en vez del wrapper.
MARK, STCYCL e ITOP entre comandos VIF siguen ejecutándose.

Nueve regresiones sintéticas comprueban todos los cortes de byte de PACKED, ST/Q y
vértices, REGLIST impar y su relleno, IMAGE y la etiqueta siguiente, NREG=0 (16 registros),
reset, tres PATH intercalados, los atajos nativos y un PACKED repartido entre tres DIRECT.
El frontend es común a CPU y OpenGL; estas pruebas no incluyen archivos del juego.

## Prioridad de las continuaciones IMAGE

`ps2recomp-gif-image-order.patch` clasifica el flujo PATH3 mediante sus etiquetas y
tamaños pendientes. Un bloque de pixels IMAGE conserva su clasificación aunque no
empiece por una etiqueta. Los registros PACKED/REGLIST y el relleno no se interpretan
como etiquetas, aunque sus bits coincidan con IMAGE. También se reconoce IMAGE tras un
setup dentro del mismo bloque. NLOOP=0 no inicia una imagen ni bloquea DIRECTHL.

Se conserva el arbitraje FIFO entre cabeceras del parche anterior. La clasificación
abarca el bloque completo; sigue siendo la abstracción de paquetes del runtime, sin
reproducir la preempción o los ciclos del GIF real. Reset descarta el cursor PATH3 junto
a la cola. Seis regresiones cubren continuaciones entre drains, setup seguido de IMAGE,
datos que parecen etiquetas, IMAGE vacía, padding/etiqueta parcial y reset. Cuatro fixtures
anteriores de prioridad pasan a usar NLOOP=1 con pixels: sus expectativas de orden se
conservan y se elimina la suposición de que una IMAGE vacía transmite datos.

## Compatibilidad IMAGE2

`ps2recomp-gif-image2.patch` trata FLG=3 como IMAGE2, siguiendo la compatibilidad de
[`GSState::Transfer` de PCSX2, commit 32ac6e2](https://github.com/PCSX2/pcsx2/blob/32ac6e23e4aaf8c8c5e74a6c1ed750ee7672120e/pcsx2/GS/GSState.cpp#L3489).
Se consume su payload como IMAGE, sin aplicar PRE, en el frontend y el atajo de subida
completa. El árbitro conserva esa clasificación en sus continuaciones. El nombre
anterior `GIF_FMT_DISABLED` se mantiene como alias de API. El arreglo es propio; no se
copia código de PCSX2 ni se presenta FLG=3 como un modo documentado de uso normal del juego.

Tres regresiones verifican todos los cortes de byte y la etiqueta siguiente, los pixels
de la subida nativa en CPU y en un backend de observación, PRE del setup y el arbitraje
de la etiqueta y su continuación. No se ha demostrado que GoW emita IMAGE2 en la escena
observada; el cambio evita desincronizar el parser si recibe este formato.

## Estado de las etiquetas en el atajo DMA de texturas

`ps2recomp-gif-native-tag.patch` completa los efectos de las etiquetas en el atajo
`tryProcessNativeGifImageUploadChain`. Tras validar la cadena entera, pasa la etiqueta
PACKED del setup a `uploadImageNative`: reinicia Q y aplica PRE bajo el mismo lock que
la subida. PRE de la imagen se ignora. También admite IMAGE2, como el frontend general.
Un argumento opcional conserva la API anterior de subida directa sin etiqueta.

Una regresión reproduce antes del arreglo la pérdida de PRE y el Q antiguo en el
siguiente punto, además del rechazo de IMAGE2. Cubre IMAGE/IMAGE2 con PRE activado y
desactivado, pixels y conservación del color. Otra prueba rechaza una cadena con terminal
inválido y comprueba que no cambie PRE/Q, no suba pixels ni incremente el contador nativo.
El atajo sigue validando todos los datos antes de aplicar sus efectos.

## Pixels de 24 bits entre bloques IMAGE

`ps2recomp-gs-image-fragments.patch` conserva uno o dos bytes de un pixel CT24/Z24
cuando termina una carga `UploadImage`. El backend completa ese pixel con la carga
siguiente y entrega el resto en bloques alineados a tres bytes. CPU y OpenGL comparten
este pequeño acumulador; no se copia el payload completo ni se altera la ruta de otros
formatos. Terminar la transferencia descarta su padding. Reset y una nueva transferencia
descartan los bytes pendientes; exportar/importar OpenGL conserva el pixel parcial.

La reproducción inicial sube 48 bytes: en una carga produce 16 pixels, pero en tres
cargas de 16 B produce 15 y deja dirección 0 activa, tanto en CPU como en OpenGL real.
Cuatro regresiones cubren CT24/Z24 en todos los cortes de los 48 bytes, cargas repetidas
de un byte y de un quadword, preservación del byte alto de VRAM, reset/nueva transferencia
y exportación de uno o dos bytes pendientes. Dos pruebas requieren OpenGL real y dos
se ejecutan siempre en CPU. Antes del arreglo fallan tres; después pasan las 519 pruebas
nativas, también después de la compilación completa de 6.418 unidades. La auditoría
reproduce 26 parches y 73 fuentes sin diferencias. El control de partida de 175 s llega
al estado 11 sin fallback CPU; las capturas de 90,15 y 110,13 s siguen mostrando agua
sin Kratos ni el escenario completo. Las 64 llamadas observadas a Clip en esa fase
siguen descartando con `0x80000000` y Y/Z idénticas. No se acredita una mejora de FPS.

## Triángulos CPU como referencia para OpenGL

`ps2recomp-gs-triangle-sampling.patch` elimina el desplazamiento de medio pixel del CPU,
conserva los cuatro bits fraccionales de XYOFFSET y aplica la inclusión de bordes
superiores/izquierdos. Reutiliza `gs_triangle_rules.h`, adaptado de Taylor N. Albarnaz /
LightVelox, commit `ac9efa070638ad3b3accd284de6f898d5ab271d1`, igual que OpenGL.
Las aristas se calculan en entero y avanzan por sumas dentro de cada fila; la Z se
interpola por diferencias para conservar los valores planos de 32 bits.

La convención está descrita en las secciones 2.4.4 y 3.2.9 del
[GS User's Manual](https://www.scribd.com/document/784545197/GS-Users-Manual):
el centro del pixel de pantalla tiene coordenadas enteras y un borde compartido
pertenece a un solo triángulo. La conversión y el recorrido del renderer software de
[PCSX2](https://github.com/PCSX2/pcsx2/blob/32ac6e23e4aaf8c8c5e74a6c1ed750ee7672120e/pcsx2/GS/Renderers/SW/GSRasterizer.cpp)
sirven como comprobación independiente; no se copia su código.

Tres regresiones fallan en CPU antes del cambio y pasan ya en las dos rutas OpenGL:
color interpolado en un centro conocido, XYOFFSET fraccional y dos triángulos con
alpha que deben cubrir el borde compartido una sola vez, en ambos sentidos de giro.
Dos fixtures antiguos de STQ/filtro lineal se recalculan para el centro entero, conservando
su capacidad de distinguir interpolación homogénea y filtrado. El fixture de fan ahora
lee CT32 con su distribución swizzled de referencia y exige exactamente el rectángulo
interior de centros enteros; la lectura lineal anterior inventaba huecos.

La comparación sintética de VRAM completa pasa de 12 diferencias a **48/48 casos iguales**
entre CPU y OpenGL compute/hardware, con IIP, cuatro pruebas Z y valores Z32 altos.
Las pruebas del parche y la compilación completa se registran en `ESTADO.md`.

## Sprites CPU como referencia para OpenGL

`ps2recomp-gs-sprite-sampling.patch` reutiliza los ejes firmados de `gs_sprite_rules.h`,
adaptados del mismo commit `ac9efa070638ad3b3accd284de6f898d5ab271d1` de Taylor N.
Albarnaz / LightVelox. El CPU conserva XYOFFSET y UV fraccionales, muestrea en el
centro entero y calcula la textura desde los extremos originales incluso cuando se
invierten los ejes o se recorta con scissor. Un ancho o alto cero deja de dibujar.
Las reglas de cobertura y el recorrido se contrastan con el manual GS 3.2.9 y el
renderer software de PCSX2 enlazados en la sección anterior; OpenGL ya usaba esos ejes.

Cuatro pruebas CPU reproducen los fallos antes del cambio: cobertura fraccional,
área cero, UV con filtro lineal y textura invertida/recortada. Otra prueba exige
OpenGL real en compute y hardware para los mismos casos. La muestra constante
UV=0,75 da `0x80000707` en OpenGL y `0x80006666` en el CPU anterior; también se
contrasta la ruta STQ. La sonda separada de VRAM completa pasa de **14 diferencias
a cero en 16 casos**. Los datos son sintéticos.

Ocho fixtures antiguos de alias CT32, CLUT, alpha y scissor usaban sprites con dos
vértices iguales y dependían del ancho/alto mínimo de un pixel que inventaba el CPU.
Ahora especifican rectángulos de 1×1 sin cambiar las aserciones de esas propiedades.
La regresión nueva exige que una primitiva vacía no cambie la VRAM. No se alteran
el frontend, VIF, VU1, los shaders ni los parches de EE/IOP.

## Color, alpha y niebla constantes en triángulos

`ps2recomp-gs-triangle-constants.patch` interpola RGBA Gouraud y el coeficiente F
por diferencias entre vértices, en CPU y en el código común de los shaders compute/
hardware. Es un ajuste propio sobre el renderer adaptado de SotC. La suma de tres
pesos float redondeados podía quedar por debajo de uno: incluso con atributos iguales
en todos los vértices perdía una unidad de color, alpha o F.

Se reproduce con un triángulo de 17×19 pixels: en CPU se alteran 17 centros con color
constante y 8 con F constante; en cada ruta OpenGL, 151 y 110 respectivamente.
La referencia para color es RGBA exacto; para niebla se compara con un punto de los
mismos atributos, que usa la aplicación de niebla existente sin interpolar F.
El problema incluye alpha 128 convertido en 127, que puede fallar GEQUAL 128 y dejar
huecos aun cuando los vértices tengan alpha suficiente.

Dos regresiones CPU y una OpenGL fallan antes del cambio. Verifican ambos sentidos
de giro, todos los centros interiores, RGBA constante con alpha-test GEQUAL 128 y
F constante frente al control sin interpolación. El shader convierte a float antes
de restar atributos para admitir diferencias negativas sin underflow de enteros.
La suite nativa pasa **531/531**, incluidas siete pruebas con OpenGL real. La sonda
separada y la comparación de gradientes se registran en `ESTADO.md`.

## Coordenadas de textura constantes en triángulos

`ps2recomp-gs-triangle-texcoords.patch` conserva UV 12.4 y S/T/Q constantes mediante
interpolación por diferencias en CPU y OpenGL compute/hardware. Las restas mantienen
el signo para admitir UV descendentes. S, T y Q siguen siendo valores homogéneos:
la división por Q se hace después de interpolar, al muestrear la textura.

La regresión de Q constante de 1,5 detectó además un recíproco inferior en la GPU:
el primer centro ya seleccionaba el texel anterior. El shader refina `1/Q` con el
residuo de una operación `fma` consumida por `precise`; conserva el camino de recíproco
cero. Es un ajuste GS propio, separado de la FPU del EE. GLSL permite un error de
hasta 2,5 ULP en la división y especifica el uso de `precise` con `fma` en las
secciones 4.7.1 y 8.3 de la [especificación de Khronos](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.pdf).

Dos pruebas CPU y una OpenGL fallan antes del cambio. Comparan todos los centros
interiores con un sprite texturizado de 1×1 y valores de referencia explícitos,
en ambos sentidos de giro, con nearest y filtrado lineal. Comprueban también un
gradiente descendente y Q variable en un centro lejos de fronteras de texel.
La suite integrada con el parche IOP de Opus pasa **535/535**, incluidas ocho
pruebas con OpenGL real. La precisión en fronteras exactas con Q variable se sigue
investigando por separado; conservar atributos constantes no resuelve todos esos casos.
La sonda independiente confirma la ruta hardware usando los contadores de batches,
primitivas y tiles compute, después de esperar de forma acotada sus variantes asíncronas.

## Integración con MMI y VU0

La integración posterior con MMI y VU0 de Opus pasa **545/545** pruebas nativas con
OpenGL. En estado 11 desaparece la duplicación Y/Z de las esferas: 30 de las primeras
64 llamadas observadas pasan Clip, frente a cero antes. Las capturas ya muestran
polígonos y texturas distintos, pero siguen deformados y sin Kratos reconocible.
Se investigan las paradas de VU1 en EEXP; ver el control integrado en `ESTADO.md`.

## Referencia Tobiichi-Port

Se revisa [YYOzcan/Tobiichi-Port](https://github.com/YYOzcan/Tobiichi-Port/tree/9f02797f8ab7481fddad4d2daf7afad82d11699f)
en `9f02797f8ab7481fddad4d2daf7afad82d11699f`. Los cinco archivos comparados
(`gs_cpu_backend.cpp`, `ps2_vif1_interpreter.cpp` y núcleo/instrucciones superiores e
inferiores de VU1) son idénticos a los del PS2Recomp fijado en `c5a9d025`.
Su README declara el menú y la partida pendientes; esto describe lo publicado,
no una prueba ejecutada aquí. Los hooks GoW incluyen objetos/tabla virtual de relleno
y atajos de arranque. No se incorporan como arreglo del renderizado ni se ha probado
su ejecutable. Puede servir para contrastar hipótesis de arranque, pero los archivos
revisados no aportan todavía una solución distinta para GS/VIF/VU1.

## Selección del contexto de modelos

`GOW_MODEL_DIAG=1` registra `[gow-model:server]` a la entrada de
`renModelServer::ProcessServer` (`0x159C58`), con un máximo de 64 muestras previas al estado
11 y otras 64 dentro de él. Observa la tabla, grupo y slot seleccionados, el contexto,
su tabla virtual y el destino/ajuste de la llamada, comprobando los límites de RAM.
Los índices se calculan en 64 bits para evitar envolvimientos.

La misma variable observa la entrada real de `renGROBMasterContext::ProcessServer`
(`0x1511F0`): `[gow-model:grob-master]` resume hasta 256 contextos de su lista, el filtro
no nulo en `cliente+0x2C`, ciclos, punteros inválidos y truncamientos. Conserva el límite
de 64 muestras por fase para cada uno de los primeros ocho contextos distintos;
las primeras ocho muestras detallan cada cliente como
`[gow-model:grob-client]`, con su servidor, vista y método virtual. `validRoute` indica
únicamente que los punteros observados caben en RAM, sin certificar el registro del
método en el runtime ni su posterior invocación. Los nodos se comparan por dirección
física para detectar también ciclos entre alias de RAM.

`[gow-model:context]` observa la entrada de `0x159878`, cuya lista y filtros coinciden
con `renModelServerContext::ProcessServer` de la referencia GoW 2, aunque el mapa retail
no le asigna ese nombre. Registra las máscaras de vista, los modelos candidatos a la
llamada directa y los destinados al árbol estático, con los mismos límites por contexto
y por lista. `[gow-model:client]` detalla las ocho primeras muestras.
`[gow-model:process]` registra hasta 64 entradas por fase en `0x157A60`, equivalente
por estructura a `ProcessModel`, con el modelo y su número de grupos. Estos candidatos
se calculan a partir de la memoria observada: tampoco prueban que los filtros internos
o el árbol de esferas permitan dibujar el modelo.

La entrada de procesamiento también observa el esqueleto, su visibilidad raíz y el
primer bloque de bits de visibilidad de grupos. `[gow-model:clip]` registra hasta 64
llamadas por fase a `renView::Clip` (`0x169120`) procedentes de `0x157FA8`. Conserva los
bits de la esfera y de diez coeficientes/límites de la vista. Solo registra un resultado
como válido cuando el original vuelve a su llamador (`completed=1`); `0x80000000`
es el código que esta ruta usa para descartar. Ninguna sonda cambia los cálculos, los
registros FPU ni los filtros. No recoge aquí otras llamadas a Clip ni todos los
descartes internos de partes/modelos estáticos.

Es una observación de entrada: no certifica que el método se haya invocado, que haya modelos
visibles ni que se haya enviado o dibujado geometría. Conserva los registros y la memoria
del juego y llama siempre al original, también en las reanudaciones. El perfil de rendimiento
elimina la variable para evitar mezclar este diagnóstico con la medición.
