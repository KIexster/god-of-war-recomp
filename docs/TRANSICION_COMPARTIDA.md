# Diagnóstico de la transición con presentación compartida

El cuadro viejo alternado con FINISH asíncrono y el encuadre incorrecto ya
presente antes de la copia final del framebuffer requieren controles separados.
Este diagnóstico observa la textura que adquiere el host; no reconstruye una
captura posterior de VRAM como si fuera esa misma imagen.

## Registro opcional

Antes de arrancar el juego, activar `PS2X_GS_PRESENT_DIAG=1` junto con
`PS2X_GS_DIRECT_PRESENT=1`, renderer OpenGL y `PS2X_FRAME_HASH=1`.
El hash existente se calcula en GPU; el diagnóstico no fuerza readback,
`glFinish` ni una adquisición adicional. El hash y el registro tienen coste:
esta ejecución es un control funcional, no una medición de rendimiento.

`[gs-present:shared]` registra únicamente adquisiciones de secuencias nuevas,
hasta 4.096 por instancia del host. Conserva secuencia publicada, secuencia de
renderizado, tick de la imagen y del host, framebuffer/fuente, dimensiones,
hash y validez, y estado blank. El tick del host puede avanzar mientras
retiene la misma textura. Los ticks no acreditan alineación con PCSX2 ni
con una matriz observada en otro momento del juego.

```powershell
python tools/render/inspect_shared_frames.py logs/control_compartido_err.log
```

El inspector informa retrocesos de secuencia, cambios de contenido asociados
a una secuencia repetida y retornos A–B–A de hashes con secuencias crecientes.
Descarta la continuidad del patrón cuando falta el hash, cambian las
dimensiones o cambia el modo del host. Rechaza registros truncados o
intercalados y archivos sin muestras. Un retorno de hash puede corresponder
a animación legítima o a una colisión: no prueba igualdad de píxeles,
corrupción ni FPS. La ausencia de patrones solo cubre las muestras capturadas.

## Control que queda por realizar

Usar el mismo binario, ELF, ISO y secuencia de mando para FINISH síncrono y
asíncrono, con presentación RAM/compartida. Mantener apagados los nuevos hilos
VU1/frente GS al aislar este problema; comprobarlos en otra ejecución posterior.
Relacionar el estado del juego con el registro sin asumir que las capturas
con readback preservan el timing de la ruta compartida. Si las secuencias
avanzan pero vuelve el contenido antiguo, seguir el framebuffer del GS; si
retroceden las secuencias, seguir la selección/sincronización del host.

Pruebas sin juego: retención de textura, cambio bajo la misma secuencia,
retroceso y cambio de modo, retorno de hash, hash desconocido, dimensiones
distintas y rechazo de registros corruptos. Seis pruebas Python correctas.
El parche aplica en comprobación sobre el runtime propio; compilación completa
con la cadena nueva y reproducción del juego pendientes. No se declara
corregida la transición ni se activan opciones de rendimiento por defecto.

Las mediciones de FPS deben esperar a que el PC compartido esté libre de
compilaciones y de otras ejecuciones del juego.
