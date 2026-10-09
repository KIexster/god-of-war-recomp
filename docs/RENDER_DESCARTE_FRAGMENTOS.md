# Descarte de fragmentos fuera de cobertura

El renderer hardware genera geometría conservadora y comprueba la cobertura
GS en el fragment shader. En triángulos finos, buena parte del rectángulo no
cubre píxeles del triángulo, pero esos fragmentos entraban igualmente en la
sección ordenada del interlock aunque no escribiesen color o profundidad.

`patches/ps2recomp-gs-discard-uncovered.patch`, después del generador por
carpetas, añade `discard` cuando `coversPixel`/`shadePrim` rechazan el fragmento.
Conserva la comprobación de cobertura, el sombreado y la mezcla existentes.
`beginInvocationInterlockARB` y `endInvocationInterlockARB` permanecen fuera
de ramas; se conserva también la condición de escritura para invocaciones
auxiliares. La [especificación ARB](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_fragment_shader_interlock.txt)
permite descartes anteriores, pero prohíbe envolver esas llamadas en un `if`
o colocar un `return` anterior.

`PS2X_GS_HW_DISCARD_UNCOVERED=0` conserva el shader anterior para comparar.
El valor por defecto activa el descarte. La selección se lee al preparar el
renderer; forma parte de la fuente GLSL y, por tanto, de la clave de caché de
shaders. No cambia el renderer CPU ni la ruta compute.

## Control procedural

El control nuevo dibuja ocho triángulos finos con coordenadas subpíxel,
ambos sentidos de recorrido, interpolación de color, mezcla, recorte y una
primitiva degenerada sobre VRAM inicial no nula. Compara exactamente los
4 MiB contra CPU, incluidos los píxeles ajenos a la cobertura. Exige observar
una pasada hardware real, sin aumentar los tiles compute.

Las variantes frías empiezan en compute mientras el shader se compila en
otro hilo. El control restablece la VRAM y repite con una espera acotada de
20 segundos hasta observar hardware; comprueba los píxeles de cada pasada.
No acepta igualdad obtenida únicamente mediante fallback compute. El descarte
es una optimización que debe conservar las salidas: el control de imagen
también tiene que pasar con el shader anterior.

## Primer ensayo de tiempo

Un ensayo privado sobre RX 5700 XT prepara la variante antes del cronómetro,
envía 4096 triángulos finos de unos 124 píxeles de lado y termina con FINISH.
Se comprueba que las muestras medidas no vuelvan a compute. Cinco muestras
con el shader anterior dan 25,04–25,94 ms; con descarte, 16,63–18,35 ms.
Ambas variantes pasan el control de VRAM del CPU. Son tiempos transcurridos
de ese lote sintético, no FPS del juego ni una estimación para toda la escena.

La adaptación OpenGL existente conserva su licencia GPL-3.0 y el crédito a
Taylor N. Albarnaz / LightVelox, `sotc-port` en
`ac9efa070638ad3b3accd284de6f898d5ab271d1`. El descarte y su control son cambios
propios marcados con `GOW-Port`.

## Verificación integrada

La suite Windows con OpenGL real pasa **624/624** con descarte desactivado
y **624/624** activado; incluye 34 controles OpenGL. La CI de `54b7107` pasa
sus dos trabajos. La compilación oficial con `scripts/2_compilar.cmd` termina
con código 0 y 77 parches, conservando las 501 micromemorias privadas de VU1.
Los 103 archivos parcheados coinciden con la referencia probada, normalizando
finales de línea y el include del runner; la desactivación habitual de LTCG
del script queda fuera de esa comparación de fuentes.

El ejecutable de `54b7107` mide 70.469.632 bytes; su SHA-256 es
`706ACF0D64CD601E935C2050ACF184730B5698DBF2BE3D82A57C5BD68427D3F6`.
Este mismo binario se utiliza con la opción 0/1 en la comparación del juego.

## Selección del tramo del juego

Las cuatro pasadas usan presentación compartida, perfil solo de cuadros,
tarjeta inicialmente ausente y la misma carpeta privada de caché. Alternan
descarte 0, 1, 1, 0 durante 180 segundos por proceso, sin capturas ni
diagnósticos GPU, con vigilancia de otras partidas y compilaciones.

La precarga separa el reloj del perfil y el del mando unos 38 segundos en
las primeras pasadas y unos 5 después de preparar la caché. El intervalo
inicial previsto de 100–170 segundos del perfil mezcla momentos distintos
de la secuencia de entrada; no se usa para atribuir una mejora al shader.

Se delimitan en cambio informes del mando entre sus segundos 100 y 125,
con estado 11, `pending=0` y `levelReady=1`. El primer informe del perfil
posterior al primer estado limita el inicio, y el último anterior al estado
final limita el fin. `resumir.py --partida` selecciona las ventanas completas
entre esos límites por orden de líneas, sin igualar ambos relojes. Esto deja
unos 15 segundos medidos por pasada; se conserva el intervalo efectivo.

El mando usa tiempo real y no constituye un replay determinista del juego.
Los estados muestreados no prueban igualdad de todos los enemigos o animaciones.
La muestra corta y las dos repeticiones por modo limitan la conclusión a este
tramo observado; no certifican un porcentaje estable para el juego completo.

| Pasada | Descarte | Límites del perfil (s) | Ventanas | Tiempo medido (s) | `guest_flip_hz` |
|---|---|---|---|---|---|
| 1 | 0 | 143,55–158,58 | 3 | 15,03 | 6,857 |
| 2 | 1 | 143,01–158,01 | 3 | 15,00 | 7,200 |
| 3 | 1 | 105,02–120,03 | 3 | 15,00 | 7,333 |
| 4 | 0 | 105,02–120,04 | 3 | 15,01 | 7,060 |

El promedio ponderado por tiempo es **6,958 cuadros/s sin descarte** y
**7,267 con descarte** (+4,43 %). Cuenta llamadas de `vid::Flip` en
`0x001837B8`, no los refrescos del host de 60 Hz. Ambos modos confirman
rasterizado hardware y partida cargada. La vigilancia no detecta compiladores
ni otra partida durante las cuatro ejecuciones.

Los límites de la tabla permiten repetir la selección con
`python tools/rendimiento/resumir.py <registro-privado> --partida --desde <inicio> --hasta <fin>`.
La alineación se eligió al detectar la separación de relojes, antes de completar
la cuarta pasada. Los registros y tarjetas son privados. La comparación usa el
mismo ejecutable nuevo y no atribuye al descarte diferencias con binarios anteriores.


## Control funcional del ejecutable

Una ejecución de 240 segundos con descarte 1, presentación compartida,
diagnósticos GS y capturas usa el binario del SHA indicado. La ISO real se
confirma en el registro; el mando de prueba atraviesa el menú y alcanza
estado 11, `pending=0`, `levelReady=1`. Las capturas a los segundos del mando
90, 160 y 190 muestran a Kratos, la cubierta, lluvia, HUD y enemigos en combate.
Son capturas diagnósticas de la VRAM actual con el CRTC latched, no una
comparación exacta con CPU del juego ni una certificación de todos sus efectos.
El proceso se cierra por el límite del ensayo: no prueba el cierre normal.
Los diagnósticos invalidan usar esta ejecución como medida de FPS.

La primera tentativa privada no cargó la ISO por una ruta con acentos leída
con codificación incorrecta en su lanzador PowerShell. Se excluye del control
funcional y se conserva separadamente; la repetición corrige la codificación.
Las cuatro pasadas de rendimiento sí confirman ISO real y partida cargada.
