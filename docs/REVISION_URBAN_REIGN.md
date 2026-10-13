# Revisión de la integración GS de Urban Reign (2026-10-12)

El autor `thiagordsfx` publica el enlace en la
[PR #68](https://github.com/KIexster/god-of-war-recomp/pull/68).
Se revisan archivos escritos a mano de
[urban-reign-recomp en `8fcce75`](https://github.com/thiagordsfx/urban-reign-recomp/tree/8fcce75fcbcca3e558073180becb8654f3c396a8),
sin clonar datos del juego ni ejecutar su port. Licencia GPL-3.0 compatible;
si se incorpora código suyo, conservar licencia y reconocer al autor.
La adaptación propia ya existente se conserva.

Su [documento GS](https://github.com/thiagordsfx/urban-reign-recomp/blob/8fcce75fcbcca3e558073180becb8654f3c396a8/docs/GS_PCSX2.md)
describe renderer DX11, ventana hija de la ventana raylib, GIF crudo,
registros privilegiados, FIFO, guardado de VRAM y opciones de escalado.
Las cifras de 60 FPS y 8x son sus comprobaciones de Urban Reign; no son
mediciones de God of War ni una predicción de rendimiento de EE/VU.
Usa el mismo PCSX2 `2954ebe` que nuestra receta independiente.

## Hallazgo que afecta a nuestro espejo

En [Bridge.cpp](https://github.com/thiagordsfx/urban-reign-recomp/blob/8fcce75fcbcca3e558073180becb8654f3c396a8/thirdparty/pcsx2-gs/compat/Bridge.cpp),
VSync usa `CSR.FIELD ? 0 : 1`. Se confirma independientemente en el
[MTGS original fijado, línea 503](https://github.com/PCSX2/pcsx2/blob/2954ebe6965669219f1bbd256e958fbd8f83a85d/pcsx2/MTGS.cpp#L503).
Nuestro EE establece CSR.FIELD con la paridad del contador, pero el cliente
anterior pasaba esa paridad directamente a GSvsync: convención invertida.

El cliente se ajusta para seleccionar el campo a partir de la **misma copia
de CSR que recibe la DLL**, con la convención de MTGS. No cambia el contador
EE, las IRQ, el renderer habitual o la autoridad de VRAM/FIFO. Se añade un
receptor DLL procedural para comprobar cuatro entregas reales del cliente:
cambio de campo, campo retenido, regreso, bits ajenos y ticks independientes.
Validación al preparar esta entrada pendiente; comprobada posteriormente:
seis CTest sin GPU correctos, incluida la entrega cliente/cola/DLL; negativo
que vuelve al campo anterior compila/falla y restaurado pasa. Header público
intacto. Fixture runtime/DLL/DX11 conserva dos imágenes de 2.048 RGBA exactos,
reset, fragmentos, registro directo y cierre sin fallo. Las capturas anteriores mantienen su identidad y
no certifican paridad de entrelazado. Este fallo del espejo no se presenta
como la causa del espasmo o del encuadre original del port.

Cliente corregido `89ffb57` recompilado sobre la cadena completa 103; CI de
ese código correcto. Controles posteriores con el juego y sus límites se
registran en [ESTADO.md](ESTADO.md); no atribuirle las capturas del cliente previo.

## Partes útiles para la siguiente integración

- La ventana hija evita presentar DX11 y OpenGL en la misma superficie; requiere
  probar entrada, tamaño, foco y recuperación. Nuestro módulo solo ha verificado
  ventana oculta y espejo PCRTC; el backend visible es trabajo posterior.
- `ReadFifo` y `ReadLocal` muestran los puntos originales de PCSX2 necesarios
  para devolver transferencias GS→EE. `SaveVram` fuerza devolución de la cache
  antes de copiar; `LoadVram` reinicia targets. No reemplazar una copia completa
  del estado GS por una copia de VRAM: parser, registros, fragmentos y cursor
  de transferencia también necesitan restauración verificada.
- Mantiene SIGNAL/FINISH/LABEL en el núcleo. Coincide con nuestra separación
  del espejo y evita asumir que los handlers del renderer generan esas IRQ.

Sus correcciones `alignSprite`, `textureInsideRT`, `halfPixelOffset` y
`GSC_UrbanReign` están fijadas para SLUS-21209. No trasladarlas a SCUS-97399
por semejanza de backend. La configuración de God of War debe contrastarse
con su entrada original y con la escena observada.

En [gs_pcsx2_backend.cpp](https://github.com/thiagordsfx/urban-reign-recomp/blob/8fcce75fcbcca3e558073180becb8654f3c396a8/runtime/ps2xRuntime/src/lib/gs/gs_pcsx2_backend.cpp),
el registro de cadencia considera dibujo nuevo si recibió al menos 4 KiB
de GIF. Es una heurística; ese contador no es un hash de píxeles ni demuestra
que una imagen sea distinta. No utilizarla para declarar resuelto un retorno
de contenido antiguo. La revisión no reproduce su transporte completo,
la estabilidad con hilos ni la equivalencia de escena.

## Qué significa la cobertura SDK

El [mapa y sus detalles](https://github.com/thiagordsfx/urban-reign-recomp/blob/8fcce75fcbcca3e558073180becb8654f3c396a8/docs/status/details.en.md)
clasifican 983 funciones de librerías estáticas enlazadas en SLUS-21209.
`functions.csv` contiene dirección/tamaño; el estado se asigna por grupos en
`data.toml`. Ese 100% no es un SDK completo de Sony para PC ni una validación
independiente de todas sus funciones en SCUS-97399.

En [hle_bindings.cpp](https://github.com/thiagordsfx/urban-reign-recomp/blob/8fcce75fcbcca3e558073180becb8654f3c396a8/src/urban/hle_bindings.cpp)
se identifican funciones por nombre y se vinculan servicios del runtime.
GS/DMA/VIF, libpad, libmc y newlib conservan código original recompilado.
Por eso lo reutilizable es la implementación de servicios comunes y el método
de identificación; no sus direcciones, módulos del juego o C++ generado.
SIF/RPC/IOP, archivos, mando y MPEG/IPU requieren comparación por servicio y
pruebas con la edición propia. EE/IOP/VU se coordinarán con Opus antes de cambiar
sus áreas; esta revisión no modifica su checkout.

Un ejemplo de límite: su alias de `sceCdLayerSearchFile` descarta el argumento
de capa, porque Urban Reign usa disco de una capa. No trasladar ese supuesto a
la ISO de 8,5 GB de God of War sin validar búsquedas/lecturas. El mapa también
marca varios límites de librería como aproximados. Mantener nuestro estado
basado en lo comprobado; no importar automáticamente sus etiquetas verdes.

## Créditos

A **thiagordsfx**, autor de [urban-reign-recomp](https://github.com/thiagordsfx/urban-reign-recomp),
por publicar su integración y compartirla con este proyecto. Su Bridge.cpp
permitió detectar la convención de campo incorrecta de nuestro espejo;
la corrección propia se contrastó con MTGS original. El GS utilizado procede
de PCSX2 y conserva sus avisos GPL-3.0+. Esta revisión no importa código del juego.
