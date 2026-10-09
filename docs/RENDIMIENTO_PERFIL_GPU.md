# Perfil GPU sin esperas durante el render

La opción `PS2X_GS_GPU_PROF=1` emitía timestamps para cada operación y los
recogía con `GL_QUERY_RESULT` en cada presentación. Si la GPU no había terminado,
esa lectura podía esperar. El límite de 4096 muestras provocaba también una
recogida forzada dentro del envío de comandos. El profiler podía modificar
la sincronización del trabajo que intentaba medir.

El parche `ps2recomp-gs-profile-async.patch` se añade después de
`ps2recomp-gs-hardware-profile.patch`, como parche 67. Es una implementación
propia. La idea de conservar medidas pendientes con sus datos de cuadro procede
del [perfilador de ICO PC de nathanialf](https://github.com/nathanialf/ico-pc/blob/46de16d393b817290e1bbf5de63a6de10cf9575c/port/render/rd_perf.c).
No se copia código de ICO ni se trasladan sus shaders o algoritmos de juego.

## Recogida y límites

Se congelan ventanas de cuatro presentaciones con sus timestamps, metadatos de
raster, primitivas, tiles, pares, máximos, razones de flush y hazards de upload.
Se consulta `GL_QUERY_RESULT_AVAILABLE` del último timestamp de cada ventana
antes de leer resultados. Todos los timestamps se emiten en el mismo contexto.
La [especificación ARB_timer_query](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt)
garantiza que la disponibilidad de una consulta implica la de las consultas
anteriores del mismo tipo.

La recogida consume ventanas listas en orden, sin esperar por la primera
pendiente. Como máximo hay tres ventanas cerradas esperando y una activa.
Cada ventana acepta hasta 4096 pares de timestamps. Si falta capacidad, sigue
el render y se omiten muestras; los contadores de trabajo y de cuadros siguen
contando. Cuando se libera espacio, se cierra la ventana activa, que puede
tener más de cuatro cuadros, y se usa su divisor real.

Un informe con `partial profile` tiene tiempos GPU incompletos: no debe usarse
para comparar rendimiento con un informe completo. La línea `window` muestra
el número real de cuadros. Se conserva el formato de `per frame`, los grupos
por FBP/flags y los detalles de los lotes.

La hora de impresión corresponde a la **recogida**, que puede ocurrir después
de la ejecución medida. No debe correlacionarse directamente con muestras
externas de estado del juego usando únicamente la proximidad temporal del log.
Para volver a seleccionar ventanas por estado habrá que validar esa atribución
o capturar un identificador de cuadro compartido. Los contadores internos del
informe sí pertenecen a la ventana original.

Solo la destrucción del backend permite recoger resultados pendientes de forma
forzada, incluido el último intervalo corto. Después se eliminan todos los
objetos de consulta del profiler antes de destruir el contexto OpenGL.

## Verificación

Se añaden ocho regresiones nativas en `ps2xTest/src/ps2_gpu_profile_tests.cpp`,
con timestamps simulados y sin assets del juego:

- GPU ocupada: una consulta de disponibilidad, ninguna lectura de resultados
  ni reutilización de handles pendientes.
- Ventanas listas y pendientes: orden, tiempos correctos y recogida única.
- Metadatos y contadores originales, aunque ya exista trabajo de otra ventana.
- Cola llena: no se pierde trabajo; divisor real de una ventana prolongada.
- Límite de muestras y contabilidad explícita de omisiones; handles únicos.
- Cierre con una ventana corta, reciclado completo y sin duplicación.
- Cierre vacío sin inventar cuadros ni consultas.
- Ventana sin timestamps con sus cuadros y razones de flush.

La comprobación aislada de Windows compila con MSVC, C++20, `/O2 /W4 /WX`
y termina con **8/8 pruebas correctas**. No necesita OpenGL ni assets del juego.
Un control negativo invierte la guarda de disponibilidad solo en una copia
temporal: compila y ejecuta las ocho pruebas, pero falla en cuatro, incluida
la lectura prematura. La copia se restaura después. Esto comprueba que las
regresiones detectan el fallo que se quiere evitar.

La [PR #25](https://github.com/KIexster/god-of-war-recomp/pull/25) quedó integrada
en `43c3a47`. La CI de la propuesta pasó sus dos jobs. La compilación oficial
de Windows con `scripts\2_compilar.cmd`, sobre `2f6579d` y **67 parches**, terminó
con código 0 después de reanudar el mismo build tras reiniciar el PC. El
ejecutable tiene 70.229.504 bytes y SHA256
`6C035DDFFBE4FC706CAA8171AD3A6FFA642911605A3F86CE8AD3CEAC2634A470`.

Al integrar después los cambios de flags de VU1, una ejecución sobre
**68 parches** pasó 596 de 597 pruebas: el directorio de trabajo impedía
encontrar `instructions.h` en el control de mappings VU0. Los **24 controles
OpenGL efectivos** pasaron. La siguiente suite, ya con las trece regresiones
del ring y ejecutada desde la raíz del runtime, pasó **610/610** sin cambiar
el código de VU0. La validación final del ring se registra por separado en
[Protección del ring de uploads](RENDER_UPLOAD_RING.md).

La prueba funcional del ejecutable de 67 parches duró **240,31 s**, con ISO
de solo lectura, tarjetas privadas, FMV omitido y el profiler activado.
Llegó a estado 11, `pending=0`, `levelReady=1`, y produjo ventanas de cuatro
cuadros. Las capturas revisadas muestran a Kratos, enemigos, barco, lluvia y
HUD sin polígonos estirados visibles en esa escena. Se cerró únicamente esa
instancia al agotar el plazo; no fue un cierre natural del juego.

El raster hardware estuvo activo, pero **la presentación del juego aún copia
a CPU**: el runtime no captura el contexto de raylib ni llama al consumidor
de texturas compartidas. `PS2X_GS_DIRECT_PRESENT=1` por sí sola no conecta esa
ruta. Los controles OpenGL comparten contextos de forma explícita; su resultado
no prueba que el juego ya use presentación compartida. Esto se investigará
en un parche separado. Había compilaciones ajenas durante la prueba, por lo
que estos registros no se usan para comparar FPS.

La modificación afecta al profiler activado por variable de entorno. No se
declara una mejora de FPS con el profiler apagado ni se modifican shaders,
VU1, FPU, EE, IOP o reproducción FMV. Tampoco se habilita por defecto
`PS2X_GS_GPU_FINISH_ASYNC`.

### 2026-10-09: consumidor compartido conectado en la PR #33

La limitación de presentación por RAM descrita arriba corresponde a la
prueba anterior del profiler. La PR #33 conecta el host raylib con las
texturas compartidas al seleccionar backend GPU y
`PS2X_GS_DIRECT_PRESENT=1`. La pasada funcional de 241,18 s confirma el
modo efectivo en el registro y llega a la escena del barco; las capturas
diagnósticas siguen disponibles bajo demanda. La ruta CPU/RAM permanece
como referencia. Validación y límites en
[presentación compartida](RENDER_PRESENTACION_COMPARTIDA.md). No se usan
los tiempos de esa pasada con diagnósticos para atribuir una mejora de FPS.
