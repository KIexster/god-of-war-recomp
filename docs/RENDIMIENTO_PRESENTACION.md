# Comparación de presentación RAM y compartida

Medición del 9 de octubre de 2026, después de fusionar las PR #33 y #34.
Se compara únicamente la presentación; ambos modos usan el mismo renderer
OpenGL, las mismas 501 micromemorias de VU1 y el mismo ejecutable.

## Condiciones

- CPU: AMD Ryzen 7 5800XT, 8 núcleos y 16 hilos.
- GPU: AMD Radeon RX 5700 XT; driver Windows `32.0.21045.5002`.
- Ejecutable de la compilación oficial de `343404f`, con 75 parches:
  70.469.120 bytes, SHA-256
  `33C012EF1BB0DC01C325027480893CDB427F406745C686FC4D8296F1BFFFE5A5`.
- Script basado en `main` `c8ce1d5`, con selección explícita de presentación.
  La PR #34 modifica cómo se pasan las carpetas al generador; esta comparación
  conserva el binario anterior para aislar la presentación.
- Cuatro ejecuciones de 180 segundos: RAM, compartida, compartida, RAM.
  Tarjeta privada ausente al inicio de cada pasada y la misma caché privada
  de shaders, ya utilizada en el control funcional anterior.
- `GOW_FAST_BOOT=1`, `GOW_SKIP_FMV=1`, mando de prueba con secuencia fija,
  `GOW_PAD_TEST_NO_CAPTURE=1` y perfil `-SoloCuadros`.
  Sin capturas, volcados, profiler GPU ni estadísticas detalladas GS.
- Vigilancia de compiladores y otras instancias durante toda la ejecución.
  Los registros deben confirmar el modo efectivo RAM o compartido; solicitar
  el modo compartido no basta si OpenGL vuelve a RAM.

## Selección y límites

Se seleccionan ventanas completas entre los segundos 100 y 170 del reloj
del perfil, encerradas por informes `state=11`, `pending=0`, `levelReady=1`.
El reloj comienza después del arranque del proceso; por eso una ejecución
de 180 segundos puede tener menos ventanas válidas hasta 170. El resumen
conserva el intervalo efectivo y pondera `guest_flip_hz` por su duración.

`guest_flip_hz` cuenta entradas en `vid::Flip` de SCUS-97399. Las presentaciones
del host son otro contador y no equivalen a FPS de partida. Con
`-SoloCuadros`, los tiempos por subsistema quedan a cero deliberadamente:
no sirven para atribuir el cuello de botella.

La secuencia de botones usa tiempo real, no un replay determinista del estado
del juego. Los estados muestreados confirman partida cargada, pero no identidad
de todos los enemigos, animaciones ni cuadros entre pasadas. Dos repeticiones
por modo tampoco permiten generalizar un porcentaje a todo el juego. Los
registros, tarjetas, shaders y demás archivos derivados del juego permanecen
privados.

## Resultado

| Orden | Presentación | Ventanas | Intervalo efectivo (s) | Duración seleccionada (s) | FPS del juego |
|---|---|---:|---:|---:|---:|
| 1 | RAM | 12 | 100,09–160,19 | 60,12 | 6,756 |
| 2 | Compartida | 13 | 100,02–165,08 | 65,04 | 6,918 |
| 3 | Compartida | 13 | 100,01–165,11 | 65,08 | 6,898 |
| 4 | RAM | 13 | 100,07–165,14 | 65,06 | 6,932 |

Los promedios ponderados son **6,848 FPS con RAM** y **6,908 FPS compartidos**:
diferencia observada de **+0,88 %**. Las dos pasadas RAM difieren entre sí
aproximadamente un 2,6 %; esta muestra no demuestra una mejora estable de FPS.
El host presenta cerca de 59,9 veces por segundo en ambos modos, conservando
cuadros cuando el juego avanza más despacio.

Las cuatro pasadas confirman su modo efectivo, conservan el hash antes y
después, no generan capturas y no contienen marcas de carga externa ni
errores fatales. Una partida de Opus comienza después de terminar la cuarta
pasada, siete segundos más tarde; no se detiene ni se incorpora a la medición.
El cierre de nuestras instancias al vencer el plazo es forzado y no constituye
una nueva prueba del cierre normal del backend.

La presentación compartida conserva valor como ruta sin lectura de píxeles
por cuadro y como base para futuras mejoras, pero no resuelve por sí sola
el rendimiento de esta escena. La siguiente investigación del render se
dirige al [rasterizado y al grupo de texturas dominante](RENDER_PRESENTACION_COMPARTIDA.md#siguiente-investigación-rasterizado).

## Repetir

Usar `scripts/probar_rendimiento.ps1 -Renderer opengl -Presentacion ram` o
`-Presentacion compartida`, ambos con `-SoloCuadros -Segundos 180` y etiquetas
distintas. Restaurar el mismo estado inicial de tarjeta antes de cada pasada
y verificar el hash del ejecutable antes y después. Después ejecutar:

```powershell
python tools/rendimiento/resumir.py logs/perf_ram_1.log --partida --desde 100 --hasta 170
python tools/rendimiento/resumir.py logs/perf_compartida_1.log --partida --desde 100 --hasta 170
```
