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

Pendiente para una adaptación funcional:

1. Construir un módulo GS aislado con las fuentes/licencias fijadas, recursos
   de shaders y dependencias de Windows. `GS.cpp` usa servicios Host, ajustes,
   recursos, métricas y estado del juego; incluir un header no los resuelve.
2. Alojar DX11, configuración, ventana y cierre correctos; comprobar un patrón
   procedural completo con el GS real, transferencias y salida de píxeles.
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
