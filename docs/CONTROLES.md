# Entrada del mando y pruebas del menú

God of War usa **libpad2** (sockets), mientras que el backend de entrada de PS2Recomp entrega paquetes
de libpad (puerto/slot). Los overrides de `src/gow_overrides.cpp` conectan ambas interfaces para el
primer puerto. El segundo puerto permanece desconectado; todavía no hay vibración.

## Teclado

Cuando no hay un gamepad conectado al backend:

| Control de PS2 | Tecla |
|---|---|
| Stick izquierdo | WASD |
| Stick derecho | IJKL |
| Cruceta | Flechas (WASD también activa la cruceta del backend) |
| X | X o Espacio |
| Círculo | C |
| Cuadrado | Z o numérico 0 |
| Triángulo | V o numérico 1 |
| L1 / R1 | Q / E |
| L2 / R2 | Shift izquierdo / derecho |
| Start / Select | Enter / Tab |

El backend usa el gamepad de índice 0 cuando está disponible. Sus botones, sticks y gatillos se
adaptan al paquete libpad2. Las presiones se simulan como 0 o 255 según el botón esté suelto o pulsado.

## Validación

`scripts\probar_pad2.cmd` compila y ejecuta una prueba independiente del juego. Comprueba el orden
de bytes de X/Start, ambos sticks y las doce presiones contra la tabla que usa el ejecutable de
SCUS-97399. Requiere las mismas herramientas de C++ que la compilación normal.

Después de compilar el port, se puede repetir la prueba de navegación:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar_menu.ps1 -Segundos 115
```

La prueba activa temporalmente `GOW_PAD_TEST=1`: pulsa Start a los 5 segundos desde la primera lectura
del mando y X a los 12, 20, 28, 36, 52, 60, 68, 76, 84, 100, 116 y 132 segundos. Guarda hasta diecisiete imágenes PPM del framebuffer del GS en
`logs\`, junto con `ejecutar.log` y `ejecutar_err.log`. La secuencia es un diagnóstico; no detecta qué
pantalla está activa y **no certifica que se haya iniciado una partida jugable**. La ejecución normal
no inyecta botones. Hay que revisar las imágenes y los registros.

Para investigar búsquedas de nodos durante las transiciones:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar_menu.ps1 -DiagnosticoRutas
```

`GOW_PATH_DIAG=1` registra `[gow-path]` y guarda `gow_path_failure.bin` junto al ejecutable si se intenta
adjuntar un nodo NULL. El volcado contiene la RAM del juego y no debe publicarse.

## Dispatcher y checkpoints

`patches/ps2recomp-checkpoint.patch` se aplica después del parche principal del runtime. Evita tratar
una cesión del EE en el punto de entrada de una función como un retorno implícito. El fallo se
reprodujo al navegar desde el menú: una copia parcial dejó `goimation` en lugar de `goHero` y la
búsqueda del nodo terminó en una llamada virtual a NULL (`0x00180D30`). El parche incluye una prueba
de regresión en `ps2_runtime_expansion_tests.cpp`.

## Inicialización del IPU

El stub genérico de `sceIpuInit` intentaba llamar a `0x00126428` y leer tablas de otro ejecutable.
En SCUS-97399 esa dirección es una continuación de `FilteredCopyTile`: al elegir dificultad, la
llamada ejecutaba el bucle de dibujo con registros de inicialización y bloqueaba el EE.
`gowIpuInit` reproduce las escrituras de inicialización en MMIO y usa las tablas del juego en
`0x002A1610` y `0x002A1660`, sin esa llamada a código ajeno. Esto no implementa la decodificación de FMV.

Para investigar la carga del nivel sin esperar al decodificador, `GOW_SKIP_FMV=1` omite las películas
en la API de carga del juego, antes de reservar buffers y abrir el flujo. Sus consultas de
buffer listo/fin devuelven verdadero. La opción está desactivada por defecto y no implementa vídeos.

```powershell
$env:GOW_SKIP_FMV = '1'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar_menu.ps1 -Segundos 230
Remove-Item Env:GOW_SKIP_FMV
```

`GOW_FAST_BOOT=1` permite repetir diagnósticos sin esperar la animación de entrada: solo su consulta
de finalización (`ra=0x0021E714`) devuelve la duración total cuando el nivel ya está cargado.
La opción está desactivada por defecto. La prueba sin ella llegó al estado de partida después de
varios minutos; no es necesario usarla para completar esa transición.

`GOW_ANM_DIAG=1` registra tiempos de animación y condiciones de pausa. `GOW_RENDER_DIAG=1` registra
los contextos y las primitivas recientes del GS, y al entrar en el estado de partida guarda
`gow_vu1_code.bin`, `gow_vu1_data.bin` y `gow_render_ram.bin` junto al ejecutable. Esos volcados
contienen datos del juego y no deben publicarse.

## Prueba aislada de XGKICK

`patches/ps2recomp-xgkick.patch` añade `GOW_XGKICK_IMMEDIATE=1`: copia y envía el paquete GIF
al emitir XGKICK, antes de que instrucciones posteriores sobrescriban el buffer VU.
Con la variable ausente o con `0` se conserva la transferencia por ciclos.
La idea procede del [runtime de SOCOM Unzipped](https://github.com/Scotho/socom-unzipped/blob/main/third_party/ps2recomp/ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp#L1074-L1083).
La adaptación usa el parser acotado y el manejo de memoria circular existentes; no importa
programas VU nativos ni direcciones específicas de SOCOM.

```powershell
$env:GOW_SKIP_FMV = '1'
$env:GOW_FAST_BOOT = '1'
$env:GOW_RENDER_DIAG = '1'
$env:GOW_XGKICK_IMMEDIATE = '1'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar_menu.ps1 -Segundos 140
# Guardar los logs/capturas antes de repetir: la siguiente ejecución los reemplaza.
$env:GOW_XGKICK_IMMEDIATE = '0'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar_menu.ps1 -Segundos 140
Remove-Item Env:GOW_XGKICK_IMMEDIATE, Env:GOW_SKIP_FMV, Env:GOW_FAST_BOOT, Env:GOW_RENDER_DIAG
```

Las ejecuciones de 140 segundos con el mismo ejecutable, opción `1` y `0`, llegan al estado 11.
Las capturas de partida a los 90 segundos desde la primera lectura del mando son idénticas:
todos sus píxeles son negros. Persisten errores de paquetes XGKICK y los defectos del menú.
La opción queda como experimento desactivado por defecto.
Una regresión sintética comprueba que un SQ posterior sobrescribe la memoria VU, mientras el paquete
ya enviado conserva sus bytes originales. La prueba existente del modo por ciclos sigue pasando.
