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

La CI aplica la misma cadena de parches y ejecuta la suite existente con los
controles procedurales GS/VU1. Su resultado y el de la compilación completa
de Windows deben constar en la PR antes de integrar.

**Pendiente antes de integrar:** compilación oficial de Windows con
`scripts\\2_compilar.cmd`, suite con OpenGL efectivo y prueba con el juego.
El primer intento quedó bloqueado por el arranque del sandbox y por un 403
de la integración GitHub. El segundo intento permite ejecutar comandos fuera
del sandbox y usa GitHub CLI para publicar. La compilación oficial se prepara
en un runtime privado, con la PR en borrador hasta completar los controles.

La modificación afecta al profiler activado por variable de entorno. No se
declara una mejora de FPS con el profiler apagado ni se modifican shaders,
VU1, FPU, EE, IOP o reproducción FMV. Tampoco se habilita por defecto
`PS2X_GS_GPU_FINISH_ASYNC`.
