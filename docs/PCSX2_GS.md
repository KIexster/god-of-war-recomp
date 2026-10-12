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

Pendiente para una adaptación funcional:

1. Construir un módulo GS aislado con las fuentes/licencias fijadas, recursos
   de shaders y dependencias de Windows. `GS.cpp` usa servicios Host, ajustes,
   recursos, métricas y estado del juego; incluir un header no los resuelve.
2. Ampliar los patrones a transferencias IMAGE, texturas y lectura parcial;
   añadir ventana/presentación, VSync y controles del cierre completo.
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
