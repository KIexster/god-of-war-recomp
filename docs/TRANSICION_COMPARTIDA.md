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

Para la secuencia automática, usar `GOW_PAD_TEST=1` y
`GOW_PAD_TEST_NO_CAPTURE=1`: conserva el mando y los registros de estado sin
introducir las capturas periódicas que fuerzan readback. Mantener desactivada
la captura GS replay y las sondas de cámara/modelos durante este control.

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

## Integración de #55 y controles ligeros (2026-10-10)

Se integra `main` en `7800bc27c27ab5ddb0dd19338b834e0e2eff3829`, mediante
merge. Los dos conflictos de inserción en `scripts/compilar.ps1` se resuelven
conservando PATH1 por eventos, decodificación perezosa y búferes GIF, seguidos
por el diagnóstico del host. La cadena contiene 90 parches únicos.

Controles locales de esta integración, sin compilar ni ejecutar el juego:

- Las seis pruebas Python del inspector pasan con Python 3.14.7.
- Configuración: 0 errores y los 4 avisos existentes.
- Sintaxis PowerShell: 20 scripts, 0 errores; incluye los controles privados.
- Los 90 parches pasan `git apply --check --ignore-whitespace` y se aplican
  en orden en un worktree privado nuevo sobre
  `c5a9d02573410a2085a4b4b831b0b68ba3515440`.
- El diff de las fuentes del runtime existente coincide antes y después del
  control. Su ejecutable no se reconstruye ni acredita los cambios integrados.

El PC continúa reservado para las mediciones de Opus. La compilación Windows
completa y el control funcional siguen pendientes; #51 permanece en borrador.
La comprobación de parches no sustituye la compilación, la suite nativa ni la
verificación de las imágenes adquiridas.

## Matriz para el próximo control funcional

Una vez que el usuario confirme que el PC está disponible, compilar la cadena
completa con `GOW_WORK` privado y registrar commit y SHA-256 del ejecutable.
Preparar una carpeta de ejecución privada y una copia independiente de la
misma tarjeta inicial para cada pasada; fijar `GOW_MC0` a esa copia y
`GOW_MC1=0`. No escribir sobre la tarjeta original.

| Pasada | `GOW_GS_FINISH_ASINCRONO` | `PS2X_GS_DIRECT_PRESENT` |
|---|---|---|
| FINISH síncrono, RAM | `0` | `0` |
| FINISH síncrono, compartida | `0` | `1` |
| FINISH asíncrono, RAM | `1` | `0` |
| FINISH asíncrono, compartida | `1` | `1` |

Fijar explícitamente `PS2X_VU1_HILO=0` y `PS2X_GS_FRENTE=0` en las cuatro
pasadas. Mantener el mismo binario, ELF, ISO, escena y guion de mando, con
`PS2X_GS_GPU=1`, `PS2X_GS_PRESENT_DIAG=1`, `PS2X_FRAME_HASH=1`,
`GOW_PAD_TEST=1`, `GOW_PAD_TEST_NO_CAPTURE=1`, `GOW_FAST_BOOT=0` y
`GOW_SKIP_FMV=1`. Desactivar otros diagnósticos, replay GS y capturas automáticas
antes de iniciar. Comprobar la secuencia de estados y los errores VU1/GIF antes
de interpretar las diferencias. Ninguna de estas pasadas mide FPS.

Analizar los registros compartidos con el inspector y conservar por separado
la investigación del encuadre dentro del framebuffer GS. Evaluar después
los hilos VU1 y frente GS, por separado, si el control inicial es estable.

## Metadatos de secuencias que reaparecen (2026-10-10)

Un control sintético `1 → 2 → 1` con metadatos distintos en la segunda
aparición de `1` mostró una omisión: el inspector informaba el retroceso,
pero solo comparaba metadatos de secuencias repetidas consecutivamente.
Ahora conserva los primeros metadatos de cada secuencia hasta un cambio de
modo del host; excluye `hostTick`, que puede avanzar. Una fila inconsistente
rompe la continuidad y no inicia un nuevo patrón de retorno de contenido.

Ocho pruebas Python pasan. La nueva regresión falla con la versión anterior
en cuatro casos: hash, validez de hash, dimensiones y secuencia de renderizado.
También se comprueba que avanzar el tick del host es válido y que un cambio de
modo permite reiniciar la numeración con otros metadatos. Son entradas
procedurales, sin datos del juego. Este cambio al inspector no prueba que el
runtime produzca tales inconsistencias ni corrige la presentación del juego.
