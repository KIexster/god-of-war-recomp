# Revisión de Tobiichi-Port (2026-10-12)

Se revisa por petición del usuario [YYOzcan/Tobiichi-Port](https://github.com/YYOzcan/Tobiichi-Port),
HEAD `ac6a8ada90d5ee6cf1417d77cf122a31c6734c9a`, licencia GPL-3.0. Lectura
selectiva de código/diffs mediante GitHub; no se clona su árbol ni se descargan
datos del juego. Su `.kiexster-sync` registra `a42d77f7052fcf016dd829dd9c932f12f55f9388`:
no sustituye nuestro main posterior ni autoriza a sobrescribir sus correcciones.

## Cambios revisados

El [commit `27ab4c0`](https://github.com/YYOzcan/Tobiichi-Port/commit/27ab4c04702f07c34acf653fd109e50659ad3824)
añade launcher Qt, fullscreen/aspecto, FSR1 EASU+RCAS/FXAA, modo panorámico,
buzón de presentación, tiles 8x8 configurables y optimizaciones VIF/VU1/audio.
El [commit `017cbaf`](https://github.com/YYOzcan/Tobiichi-Port/commit/017cbafb0a52f675c7ba2976424d76b28297f47b)
introdujo VBlank en tiempo real, lotes IOP y ajustes de VU1 compilada/audio.
Sus cifras de rendimiento son mediciones de ese autor en Linux; no se han
reproducido aquí ni acreditan nuestro ejecutable Windows con VU1 intérprete.

| Tema | Comparación y utilidad |
| --- | --- |
| FRAME/TEX0: páginas frente a bloques | Nuestro runtime ya normaliza esas direcciones; no añadir otra vez la misma corrección. |
| Búferes reutilizados del árbitro GIF | Ya presentes en nuestro runtime integrado con Opus. |
| VF0/VI0 y habilitar VU1 compilada | Ya se restauran tras pares y están habilitadas las APIs; nuestros controles actuales no contienen programas compilados del juego. |
| Tiles 8x8 y dispatch partido | Cambio distinto al nuestro, que mantiene 16x16. Candidato para prueba independiente de píxeles y rendimiento; preservar una opción 16x16 y cubrir el límite de 65.535 grupos. |
| FSR1/FXAA/aspecto/launcher | Posible mejora de presentación posterior. No corrige geometría o animación de entrada ni reemplaza el rasterizador GS por DX11. Mantener el aspecto original en controles comparativos. |
| VBlank/IOP/VIF/VU1 optimizados | Revisar por tema con regresiones y coordinación EE/IOP/VU1; fuera del cambio de presentación actual. |

## Límites observados en su presentación

En [gs_threaded_backend.cpp](https://github.com/YYOzcan/Tobiichi-Port/blob/ac6a8ada90d5ee6cf1417d77cf122a31c6734c9a/PS2Recomp/ps2xRuntime/src/lib/gs/gs_threaded_backend.cpp),
el buzón y su flag son globales. El constructor activa el flag, pero el
destructor no lo desactiva ni vacía el último cuadro; no hay reset del buzón
en el archivo. Por inspección, una nueva instancia puede heredar estado o
contenido anterior. No se ha ejecutado ese caso: requiere regresión de
creación/reset/cierre antes de adaptar la idea. Además, es una ruta RAM para
Linux sin contexto compartido, no la selección de textura compartida WGL que
investigamos en Windows.

En [gs_gpu_backend.cpp](https://github.com/YYOzcan/Tobiichi-Port/blob/ac6a8ada90d5ee6cf1417d77cf122a31c6734c9a/PS2Recomp/ps2xRuntime/src/lib/gs/gpu/gs_gpu_backend.cpp),
`throttleGpuFrames` ignora el resultado de `ClientWaitSync` y borra la fence
tras esperar como máximo 100 ms. Si expira, la cantidad de trabajo pendiente
no queda estrictamente limitada como afirma su comentario. Las nuevas
`frameFences` tampoco se eliminan explícitamente en el destructor observado.
Son hallazgos de revisión, no reproducciones de fallos del juego. No copiar
ese control tal cual ni quitar fences/glFinish global para esconder problemas.

No se modifica Opus ni se incorpora código de ese fork durante esta revisión.
Prioridad: terminar primero los controles funcionales del espejo PCSX2 de #68;
después valorar tiles o filtros en cambios separados y con evidencia propia.
