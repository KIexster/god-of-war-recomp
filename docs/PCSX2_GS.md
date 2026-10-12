# Integración experimental del GS de PCSX2

El usuario propone adaptar el GS de PCSX2 mientras espera el código del port
de Urban Reign. La captura de ese comentario no contiene repositorio, revisión
o controles reproducibles. Sus cifras de resolución/FPS son del autor del
comentario; no se atribuyen a God of War ni a este trabajo.

Se revisa directamente [PCSX2](https://github.com/PCSX2/pcsx2) en
`2954ebe6965669219f1bbd256e958fbd8f83a85d`, GPL-3.0+ compatible con la licencia
del port. Las fuentes descargadas quedan en el workspace privado. Los dos
headers del puente son código propio: incluyen las interfaces del GS original
al compilar el módulo externo, sin importar fuentes del juego o del otro port.

## Punto de integración

El [GS de PCSX2](https://github.com/PCSX2/pcsx2/blob/2954ebe6965669219f1bbd256e958fbd8f83a85d/pcsx2/GS/GS.h)
recibe flujos GIF y registros privilegiados. Nuestra interfaz `GSRasterBackend`
recibe primitivas ya decodificadas, CLUT y transferencias separadas. Para usar
el frontend/caché originales, el puente debe recibir los tres PATH después
del arbitraje, antes de reducirlos a primitivas. También debe cubrir los
atajos de GIF packed y de IMAGE nativo: enganchar solo `processGIFPacket`
no garantiza cobertura de esos caminos.

`tools/render/pcsx2_gif_bridge.h` conserva hasta 15 bytes pendientes por PATH,
entrega quadwords completas en el orden en que llegan al adaptador y copia
los lotes a memoria alineada a 16 bytes. Cantidad en quadwords, lotes máximos
de 256; no rellena fragmentos ni decodifica/modifica tags. Un rechazo del
destino retiene el fallo: no se repite un prefijo cuyo consumo es desconocido.
El reset exige resetear también el GS. Comprobar final de flujo no equivale
a VSync: los paquetes pueden continuar después de él. El consumidor debe
terminar de usar el puntero antes de retornar; el puente no es una cola de hilos.

`pcsx2_gif_sink.h` enlaza con `GSState::Transfer<3/1/2>` del renderer abierto.
PATH1 utiliza el índice continuo 3 de `GSgifTransfer`; PATH2/3 conservan sus
índices 1/2. No se usa `GSgifTransfer1(span, size)`: esa función espera un
anillo VU de 16 KiB y una dirección dentro de él. Tampoco basta llamar al
índice 0 directamente: corta al terminar EOP, ignoraría otros paquetes del
mismo lote y borra nloop pendiente cuando se agota el span. Son reglas leídas
en el cuerpo fijado de GSState/MTGS, no una prueba de ejecución del renderer.
La conexión rechaza un GS ausente. **No crea un dispositivo, importa VRAM,
presenta un frame ni está conectada al runtime del juego.**

## Comprobado y pendiente

MSVC C++20 /O2 /W4 /WX: pruebas del transporte correctas. Se recorren las 65
fronteras de un bloque de 64 bytes con origen desalineado, fragmentos PATH1/2/3
intercalados, flujo de 32.784 bytes contiguo y byte a byte, alineación, unidades,
entrada inválida, fragmento incompleto, error retenido y reset coordinado.
Sintaxis de la conexión con las cabeceras **reales** fijadas correcta en MSVC,
con las definiciones x86/SSE4.1 del proyecto y AVX2. La CI comprueba además
esa conexión en Linux; no sustituye enlace/ejecución del renderer.

La primera versión del enlace compilaba con índice 0 para PATH1. La revisión
del cuerpo real detectó aquellas reglas incompatibles con fragmentos/lotes;
se corrige al índice 3 antes de usarlo en el port. Los controles de sintaxis
no habrían detectado esa pérdida de datos. Se compila además GSState.cpp real
como objeto privado con MSVC: correcto; su enlace y ejecución siguen pendientes.

### Parser original ejecutado en Windows (2026-10-12)

Se extrae una biblioteca privada de objetos GS/common de las fuentes fijadas
y se enlaza un host explícitamente sin ventana. Configuración, memoria virtual
repetida, detección de CPU y registros de dump usan implementaciones originales;
los servicios Host de este fixture ofrecen ajustes por defecto, errores a
stderr y rechazan solicitudes de ventana. Su contador debe quedar en cero.
La capa de interfaz se compila con fuentes STB de ImGui para este fixture sin UI.
Los codecs privados ya disponibles en PCSX2 usan cabeceras coincidentes:
libpng 1.6.58/zlib 1.3.2. Ninguna de esas DLL o fuentes descargadas se publica.

`tests/pcsx2_gif_parser_probe.cpp` ejecuta **GSState real** con una subclase
que cuenta solicitudes de dibujo sin rasterizarlas. Comprueba tag/payload
PATH1 separados, nloop retenido, dos paquetes EOP en un lote, fragmentos de
PATH2/3 intercalados, sus cursores independientes y RGBAQ exacto. Correcto
en MSVC; dibujo y solicitudes de ventana: cero, como exige la prueba.

Negativo aislado: cambiar solo PATH1 de índice 3 a 0 compila y falla en
`PATH1 conserva nloop al cortar tras tag`. Restaurado pasa. El ejecutable
restaurado tiene SHA-256
`1289d908c425c0f1b138ff7dc4cf619f1a9341c443fe6145eae5c02d9b7e033c`.
La CI comprueba sintaxis del fixture con los headers reales; **no reproduce
todavía su enlace/ejecución nativa**. El contexto de extracción y sus artefactos
siguen privados. Esta prueba confirma la selección de entrada del parser,
no dispositivo DX11, píxeles, caché completa, FIFO/IRQ o renderizado del juego.

### Dispositivo DX11 original ejecutado (2026-10-12)

La biblioteca privada incorpora GSDevice11, GSTexture11, D3D11ShaderCache y
GSTexture originales. `tests/pcsx2_dx11_device_probe.cpp` crea el dispositivo
en modo Surfaceless, con shaders de la revisión fijada y caché desactivada.
No necesita ventana ni ejecuta EE/VU. El host ofrece ajustes por defecto,
traducción identidad y errores a stderr; las referencias estáticas a registros
EE usan almacenamiento procedural, sin ejecutar una CPU de sustitución.

Windows/MSVC Release, AMD Radeon RX 5700 XT: dos ciclos de creación/cierre
correctos. Cada ciclo verifica **todos los 2.701 píxeles RGBA** de una textura
73x37 tras Clear y tras subir un patrón con siete palabras de padding por fila
y dibujarlo con `StretchRectAuto`/Nearest. Esa segunda operación usa el shader
COPY original, no una copia directa DXGI. El readback respeta el pitch de la
textura staging. Canales/alpha y orientación se comparan sin tolerancia.

Negativo privado: omitir solamente la llamada de dibujo compila y falla en
el primer píxel de Shader COPY; restaurado pasa ambos ciclos. SHA-256 del
ejecutable restaurado:
`2be054f554e71ebdafc234998b84d4e7b53542f26a40c2f58403fcbead66db14`.
El fixture se publica; su host/enlace siguen privados y este control GPU no
se ejecuta en CI. No procesa todavía GIF hacia GSRendererHW, importa VRAM,
presenta en una ventana o dibuja el juego. No es una medición de rendimiento.

### GIF hacia GSRendererHW y FIFO comprobados (2026-10-12)

La extracción incorpora GSRenderer, GSRendererHW, GSTextureCache y sus helpers
originales, incluidos los objetos de apoyo SW. No se modifica el dibujo ni
se reemplaza la caché con valores fabricados. El fixture inicializa las tablas
SW originales antes del renderer; su ruta SW no está cubierta por este control.
La configuración usa constructores originales extraídos, sin cargar ajustes,
tarjetas ni una VM. Los servicios opcionales ausentes (captura, dumps, UI,
reapertura y reemplazos de texturas) abortan con diagnóstico si se solicitan;
los ajustes de captura/reemplazos quedan desactivados. No se llama a VSync.

`tests/pcsx2_gif_draw_probe.cpp` envía un mosaico de 16 sprites a FRAME PSMCT32,
sin textura/blending/depth test, por el puente y la conexión originales. Usa
fragmentos de 13 bytes, regenera colores distintos para PATH1/2/3 y lee por
BITBLTBUF/TRXPOS/TRXREG/TRXDIR más InitReadFIFO/ReadFIFO originales. Cada PATH
produce un draw call GPU y **2.048 píxeles RGBA exactos**, incluida alpha.
Se verifica el delta de dibujos de cada envío, no un contador acumulado.

Windows/MSVC Release, RX 5700 XT: tres PATH correctos. Negativo privado que
omite solo el paquete de dibujo compila y falla en el primer píxel de FIFO;
restaurado pasa los tres. SHA-256 restaurado:
`1d271fd05fcbf01cec0b688655c26337b99259a2545274ecccda423740ef01fb`.
Este control verifica dibujo y descarga procedural; no equivale a presentar
un frame del juego, coherencia con su RAM/VRAM, interrupciones o rendimiento.
Su host/enlace todavía siguen privados y la CI no ejecuta este control GPU.

### Receta pública de construcción y control IMAGE (2026-10-12)

`tools/render/pcsx2_probe/` publica el host limitado, servicios, CMake y una
preparación que extrae solo fuentes GS/config originales de la revisión limpia.
No copia DLL privadas ni datos del juego al repositorio. Instrucciones y
dependencias en su [README](../tools/render/pcsx2_probe/README.md). Se añade CI
Windows para enlazar todos los fixtures y ejecutar parser/memoria sin GPU;
los controles de dispositivo/dibujo se ejecutan localmente, de forma explícita.

Esta receta compila y pasa los cuatro controles en Windows/MSVC sobre la
RX 5700 XT. El dibujo amplía su cobertura: IMAGE PSMCT32 host→local, TEXFLUSH,
sprite texturado DECAL/nearest y descarga FIFO. Los 2.048 píxeles completos y
los 91 de una región 13x7 que cruza bloques coinciden exactamente, incluida
alpha. El final parcial de quadword no escribe más allá del QWC solicitado.
Omitir solo IMAGE compila y falla en el primer píxel; restaurado correcto.
SHA-256 del fixture restaurado de dibujo:
`a7991d25961bdddddd8ce3631cf73287f057b8928c735df8724c818e575f3444`.

El cierre del mapeo Windows extraído tenía dos fugas locales: el handle y
tres placeholders retenidos. La preparación añade correcciones GOW-Port sin
cambiar el checkout original: cierra el handle y une las vistas antes de
liberar el rango. Se sigue la semántica documentada de
[VirtualFreeEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfreeex).
Ocho ciclos de cuatro vistas alias, handles estables y MEM_FREE en todo el
rango: correctos. Omitir cada corrección por separado compila y falla (ocho
handles retenidos o vista 1 reservada); restaurado correcto. No valida fallos
de reserva ni transforma este host limitado en el host completo del juego.

Los cuatro ejecutables locales funcionan sin dxcompiler.dll/dxil.dll en su
carpeta: DX11 usa el compilador SM4/5 original, con import DXC retardado para
otras APIs. La carga retardada no acredita soporte de DX12/Vulkan.

### VSync/PCRTC sin ventana comprobados (2026-10-12)

El modo `gs_gif_draw_probe --frame` añade GSvsync **original**: actualización
de PCRTC y Flush antes de GSRendererHW::VSync, caché de salida y Merge DX11.
Tres patrones IMAGE distintos producen tres salidas compuestas 64x32 con
2.048 píxeles RGBA exactos cada una. PMODE MMOD/ALP 255 determina alpha final
255; RGB conserva el patrón. Host cuenta solicitud de frame, FrameSkipped de
Surfaceless y actualización de métricas; no ejecuta interfaz ni mide FPS.

El fixture configura DISPLAY con MAGH 3/DW 255 y DY 25 conforme a NTSC en
PCSX2. La primera configuración procedural usaba DW 63/DY 0: daba salida
16x32 y un desplazamiento vertical de 25 líneas. Se corrigen los registros
de **la prueba** según el cuerpo original; no se modifica el renderer ni se
atribuye este caso al encuadre observado en God of War.

Negativo que omite solo GSvsync: compila y falla por salida 0x0; restaurado
pasa los tres patrones. SHA-256 restaurado:
`cd46ebb2f58aa6bd80c5849e30d7ee5fc0659c644db030f65c8fed1c6701cffb`.
[CI de `08307ac` completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38243100981),
incluido enlace nativo Windows y parser/memoria. Los controles de píxeles siguen
siendo locales. VSync compuesto no valida todavía swapchain, imagen visible,
sincronización con el juego o las combinaciones de FINISH/hilos.

### Swapchain DX11 procedural comprobado (2026-10-12)

`gs_gif_draw_probe --window` crea una ventana Win32 propia oculta y usa el
swapchain/Present originales. Un contexto ImGui vacío acompaña EndPresent,
sin interfaz del emulador ni archivos ini/log. El control lee el recurso
**realmente enlazado como render target DXGI antes de Present**, respetando
RowPitch; la lectura se limita al fixture y no se propone como rendimiento.

Dos ciclos completos de creación/dibujo/cierre de dispositivo, renderer y
ventana, con tres patrones distintos cada uno: los seis backbuffers y las
seis salidas PCRTC contienen sus 2.048 píxeles RGBA exactos. Callbacks de
presentación/métricas consistentes, cierre de HWND/clase comprobado. La ventana
oculta no demuestra una imagen visible o estabilidad prolongada con el juego.

Negativo privado: borrar a magenta el backbuffer real después de dibujar y
antes de compararlo compila y falla, aunque la textura PCRTC coincide. Restaurado
pasa ventana y Surfaceless. Confirma que el control distingue composición y
buffer final, sin atribuir la alteración procedural a un fallo de God of War.
SHA-256 restaurado `74e92ae9a886b66f55079d06f6cbf0ddcbf9af18bfe2064e7d92137f94f3c543`.
[CI de `d1bf750` completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38244581430).
El control GPU nuevo es local; el CI del HEAD que lo publica queda pendiente.

### IMAGE FLG 2/3 y revisión del enganche (2026-10-12)

El fixture alterna ahora IMAGE FLG=2 y FLG=3, conservando el tag en el puente.
Ambos pasan FIFO y, en modo ventana, composición/backbuffer exactos. Omitir
solo el envío FLG=3 compila y falla: recibe el color del patrón anterior en
vez del nuevo. Restaurados, los modos sin VSync, Surfaceless y ventana pasan.
SHA-256 restaurado `77ecd2fa20509b18422d9ade62c8ee35db18f90a0a8ad19e6882be412d311bfd`.
Conserva explícitamente la semántica que necesitaba la corrección de Opus #55;
no es una reproducción de sus medidas de FPS ni un fallo observado del juego.

Revisión del runtime 102: el árbitro entrega bytes en su orden decidido, pero
`tryProcessNativeGifPackedChain` y `tryProcessNativeGifImageUploadChain` pueden
evitarlo. Esta última llama a `uploadImageNative` con cuatro registros, datos
y solo el low word del setup tag: ese punto ya no ofrece el flujo GIF original.
La ruta genérica también puede tomar un atajo IMAGE interno, conservando el
span de entrada. Un único hook dentro de processGIFPacket perdería las dos
rutas DMA externas. El primer control de integración deberá conservar bytes
y orden post-arbitraje, y asegurar un consumidor en un solo hilo; se propone
desactivar aquellos atajos DMA únicamente al activar el puente opcional y
mantenerlos cuando esté ausente. No se implementa aún ese cambio del runtime.
También faltan escrituras directas fuera de GIF, espejo privilegiado, FIFO e IRQ.

### DLL y ABI de renderizado comprobadas (2026-10-12)

El módulo aislado ahora construye `gow_pcsx2_gs_module.dll`, con ABI 1 propia
de estructuras de tamaño fijo, handles, estado y validación de hilo. Recibe
GIF fragmentado y un espejo privilegiado PS2 de 8 KiB, llama GSvsync original
y copia explícitamente la textura compuesta. No intercambia objetos C++ con
el consumidor. Recursos y superficie se configuran al crear; no cambia runtime.

Contrato sin dispositivo: DLL cargada, exports/ABI y argumentos/handles
rechazados correctamente. GPU por GetProcAddress, sin usar los globals del GS:
dos ciclos correctos; singleton ocupado, handles cerrados, otro hilo, registros
incompletos y buffer pequeño rechazados; consulta de tamaño/secuencia correcta,
guardia final intacta y 2.048 píxeles RGBA exactos en cada ciclo.

Negativos aislados: omitir guardia de hilo o de capacidad compila y hace fallar
el control; restaurados correctos. El negativo de capacidad reserva físicamente
más memoria que la capacidad declarada para probar el contrato sin provocar
un acceso fuera de su asignación. La DLL sigue siendo render-only: IRQ/FINISH,
FIFO/VRAM del runtime, activación a mitad de juego y recuperación del dispositivo
pendientes. Detalles en el [README del módulo](../tools/render/pcsx2_probe/README.md).
La CI nueva carga solo el contrato sin dispositivo; GPU comprobada localmente.

### Espejo privilegiado para el adaptador (2026-10-12)

`src/gow_gs_pcsx2_mirror.h` empaqueta los 19 registros reales del runtime en
sus offsets PS2 de 8 KiB, explícitamente little-endian y con huecos en cero.
Excluye vsyncTick: es un contador del runtime que ocupa espacio entre CSR/IMR
en su struct compacto. CSR se lee atómicamente; el llamante debe sincronizar
los demás registros con su escritor. El helper todavía no se instala en el juego.

Prueba independiente con valores distintos y CSR multibyte: correcta en MSVC.
Contraste de los 19 offsets y tamaño con GSPrivRegSet **original** mediante
static_assert: correcto. Se añaden controles portables y de headers reales
a CI, más CTest Windows. No se transforma una copia del struct de 160 bytes
en una copia válida de registros de 8 KiB por rellenar solo su final.

Pendiente para una adaptación funcional:

1. Enlazar la ABI probada con un hook opcional del runtime que preserve bytes,
   orden y hilo, incluidos los atajos DMA. Completar servicios de Host necesarios;
   los servicios ausentes de este fixture siguen limitados.
2. Ampliar presentación a ventana visible, redimensionado y recuperación de
   dispositivo; ampliar formatos, CLUT, blending y depth/alpha test.
3. Añadir espejo de registros privilegiados, CSR/FINISH/SIGNAL, FIFO local→host,
   VRAM inicial y sincronización de escrituras/lecturas. `GSopen` recibe memoria
   de **registros privilegiados**, no directamente los 4 MiB de VRAM.
4. Enlazar todos los caminos de envío y presentación del runtime en un parche
   opcional con regresiones, preservando CPU/OpenGL como referencias.
5. Compilación completa, controles menú/introducción/partida y comparación de
   escena; después medir GS y juego con diagnósticos desactivados. Cambiar GS
   no elimina el coste de EE/VU ni corrige geometría de entrada incorrecta.

Este primer puente es preparación verificable de la integración, sin mejora
de FPS o fidelidad atribuida todavía y sin declarar disponibles DX11/DX12/Vulkan.
