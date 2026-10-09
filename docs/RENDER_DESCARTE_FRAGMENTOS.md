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
