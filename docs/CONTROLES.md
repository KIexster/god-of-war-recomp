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

## Medidor de FPS

**F2** muestra u oculta, arriba a la izquierda, los cuadros por segundo del juego (entradas a `vid::Flip`, la función de
cuadro del juego; `GOW_PERF_FRAME_PC` la cambia) y los refrescos de la ventana. Verde a partir de 57, amarillo de 45 a 57,
rojo por debajo. `GOW_FPS=1` lo muestra desde el inicio. Mientras se ve, cada segundo se escribe en el registro
(`logs\ejecutar_err.log`) una línea `[gow-fps] t=<s> juego=<fps> min=<mínimo en ventanas de 0,25 s> pantalla=<Hz>`, para
localizar después dónde hubo tirones.

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

`GOW_PAD_GUION` sustituye esa secuencia por otra: una lista `segundo:acción[:duración]` separada por
comas, con pulsaciones de 0,7 s si se omite la duración. Botones: `start`, `select`, `arriba`, `abajo`, `izquierda`, `derecha`, `x`,
`circulo`, `cuadrado`, `triangulo`, `l1`, `r1`, `l2`, `r2`. Por ejemplo, para abrir Cargar en el menú:

```powershell
$env:GOW_PAD_GUION = '5:start,13:abajo,20:x'
```

`mover_izquierda`, `mover_derecha`, `mover_arriba` y `mover_abajo` llevan el eje
correspondiente del **stick izquierdo** a 0/255, conservando el otro eje.
Las acciones antiguas `izquierda`/`derecha`/`arriba`/`abajo` siguen siendo
cruceta. Ejemplo: `180:mover_izquierda:3,181:cuadrado` mantiene el stick tres
segundos y pulsa ataque a los 181 s. Duración positiva, valores finitos;
entradas inválidas se ignoran. Coincidiendo dos acciones del mismo eje,
prevalece la última activa de la lista. Al terminar, cada lectura vuelve al
estado base del mando, o a neutral con `GOW_PAD_SOLO_GUION=1`.

`GOW_PAD_GUION_DIAG=1`, junto con `GOW_PAD_TEST=1`, registra hasta 128 cambios
del paquete devuelto por libpad2 bajo `[gow-pad2:guion]`: tiempo anfitrión desde
la primera lectura, estado, los dos bytes de botones en orden de paquete y
sticks RX/RY/LX/LY. Permite comprobar que el guion se entrega, **no que el juego
ya acepte control ni que se haya realizado la acción**. Los tiempos siguen
siendo de host; no alinean animaciones entre pasadas. Prueba procedural
`tests/pad_script_test.cpp` comprueba botones, stick separado de cruceta,
presiones, duración, superposición, fin de ventana y entradas inválidas.

El menú principal acepta botones sin pulsar Start, y lo que tarda en aparecer depende de la carga del
equipo (el tiempo emulado avanza con el tiempo real en las esperas). Con un arranque rápido, el Start de
`5:start` cae ya en el menú y elige New Game. Para cargar la primera partida de la tarjeta sea cual sea el
tiempo de arranque: `13:abajo,20:x,62:x` (bajar a Load, abrirlo y elegir la partida). Las mediciones de
rendimiento en los muelles usan ese guion sin más pulsaciones, para que Kratos quede quieto.
`GOW_PAD_SOLO_GUION=1` (lo activa `scripts\probar_rendimiento.ps1`) ignora además el mando y el teclado
reales: tocarlos durante una medición llevaba el menú a otra partida.

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

`GOW_VU1_BUDGET_DIAG=1` (`ps2recomp-vu1-budget-diag.patch`) registra `[gow-vu1-budget]` cuando un programa
VU1 lanzado por MSCAL/MSCNT agota el tope fijo de 65536 ciclos sin llegar a su bit E: muestra la dirección
de entrada, el PC donde se cortó y el contador. Escribe las 32 primeras apariciones y después una de cada 1024.
Si no aparece ninguna línea en la partida, el tope no está cortando programas.

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

## Correcciones de UNPACK VIF

`patches/ps2recomp-vif-unpack.patch` corrige dos formatos del intérprete:

- V2-32/16/8 escribe `X,Y,X,Y` en lugar de conservar las componentes Z/W anteriores.
  La expansión ocurre antes de aplicar las máscaras y las sumas STMOD.
- V4-5 expande RGB a los bits 3..7 y alfa al bit 7. Por ejemplo, `31,17,9,1`
  produce `248,136,72,128`. Este formato sigue ignorando STMOD.

Referencias: [PCSX2 Vif_Unpack.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Vif_Unpack.cpp)
y [SOCOM Unzipped](https://github.com/Scotho/socom-unzipped/blob/main/third_party/ps2recomp/ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp).
Estas correcciones se aplican en la compilación normal.

Tres pruebas sintéticas fallaban antes del arreglo y pasan después: cubren las tres anchuras V2,
extensión con/sin signo, máscaras, protección de escritura, suma por componente y alfa V4-5
encendido/apagado. La suite cambia de 436/441 a 439/441; los dos fallos restantes son los previos.
La ejecución del juego con FMV omitidos sigue alcanzando el estado 11, con defectos del menú
y la imagen de partida negra. No se atribuye una mejora visual a estas correcciones.

## Diagnóstico de VIF y buffers de dibujo

`patches/ps2recomp-vif-diagnostic.patch` añade trazas optativas y acotadas; no altera la ejecución
de VIF ni de VU. Para repetirlas:

```powershell
$env:GOW_SKIP_FMV = '1'
$env:GOW_FAST_BOOT = '1'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar_menu.ps1 -Segundos 140 -DiagnosticoVif
Remove-Item Env:GOW_SKIP_FMV, Env:GOW_FAST_BOOT
```

El script restaura las variables de diagnóstico al terminar. En `logs\ejecutar_err.log`:

- `[gow-vif]` cuenta comandos y solicitudes de interrupción (bit I).
- `[gow-vif-launch]` compara TOP/ITOP y la cabecera en memoria antes de los primeros 24 MSCAL.
- `[gow-xgkick]` muestra las primeras 96 cabeceras GIF y una muestra cada 4096.
- `[gow-xgkick:reject]` identifica los primeros 32 rechazos por formato, longitud o búfer lleno.
  Una cabecera con NLOOP=0 puede ser válida; no debe contarse como un rechazo por sí sola.
- `[gow-gs:buffer]` registra formato, dirección y píxeles con RGB distinto de cero de cada contexto.

Al alcanzar por primera vez el estado 11, `GOW_RENDER_DIAG` guarda también `gow_render_vram.bin`
y `gow_render_context_0.ppm` / `gow_render_context_1.ppm` junto al ejecutable. Las imágenes de
contextos solo se generan para PSMCT32/24 y FBW distinto de cero. Leen la memoria con el direccionamiento
del GS, sin cambiar el framebuffer presentado. Los archivos contienen datos del juego: conservarlos
localmente y no publicarlos.

La prueba de 140 segundos registró 196608 comandos VIF1 sin solicitudes de interrupción y llegó
al estado 11. Los dos contextos dibujaban en FBP=0, FBW=8, PSMCT32, mientras la pantalla presentaba
FBP=208. Sus capturas tenían 212992 píxeles con RGB distinto de cero, pero solo mostraban un fondo
oscuro y puntos dispersos: no apareció una escena 3D oculta en esos buffers. La pantalla seguía negra.
La suite conserva 439/441 pruebas aprobadas y los mismos dos fallos conocidos.

### Corrección de saltos por registro de VU1

`patches/ps2recomp-vu-jump.patch` corrige JR y JALR para leer el valor actual del registro VI.
La copia anterior usada para comparar ramas condicionales no debe determinar estos destinos.
El comportamiento se contrastó con [JR/JALR de PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/x86/microVU_Lower.inl)
y su [análisis de saltos](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/x86/microVU_Analyze.inl).
La regresión también cubre JALR cuando el registro de destino y el de enlace coinciden.

Repetir la prueba anterior de 140 s con este parche registra 212992 comandos VIF1, llega al
estado 11 en aproximadamente 70 s desde la primera lectura del mando y no produce mensajes
`[gow-xgkick:reject]` ni `[VU1 reserved lower]`. Antes se observaban paquetes rechazados por
longitud y bucles de vértices que sobrescribían las cabeceras en 0xF60 y 0x24B0.

La pantalla presentada sigue negra. El contexto 0 conserva 212992 píxeles no negros, pero su
imagen sigue siendo un fondo oscuro con puntos. Esta corrección elimina un fallo de ejecución
VU1; todavía hay que resolver el renderizado de la escena. La suite queda en 440/442, con los
dos fallos previos de heap/DMA. Las trazas temporales de escrituras se han retirado del runtime.

Tras integrar también `ps2recomp-heap.patch`, el conjunto pasa 443/443 pruebas. Los resultados
440/442 de arriba documentan la comparación antes de integrar esa corrección de heap/DMA.

### Captura de comandos GS

`GOW_CAMERA_DIAG=1` registra las matrices de la vista después del retorno completo
de `renView::SetupPipeline` (`0x23B5E0`, SCUS-97399). No sustituye sus cálculos.
En estados 4 (intro) y 11 (partida), observa hasta 16 pares vista/estado, con 32
muestras por par separadas por al menos dos segundos del host. `elapsed` cuenta
desde la primera observación de intro/partida. Registra vista activa, cámara,
articulación, marcas de actualización y los bits de mundo, inversa, proyección
y producto combinado; compara también los buffers del cliente.
`stamp` corresponde a `0x29BDF8`; no es un contador de cuadros ni tiempo simulado.
Las direcciones se validan contra la RAM y los floats se copian como bits, sin normalizarlos.
La ausencia o invalidez de una cámara aparece explícitamente en `[gow-camera]`.
Este diagnóstico no acredita equivalencia con PCSX2 y se limpia/restaura al
ejecutar el perfil de rendimiento. Sus registros quedan en archivos locales.

Para elegir un tramo de comandos del GS, `GOW_GS_REPLAY_TRACE` indica el archivo
local de salida. `GOW_GS_REPLAY_AFTER` fija la espera desde la instalación del
backend (0..3600 segundos, 150 por defecto); `GOW_GS_REPLAY_SECONDS` fija la
duración (0,1..60 segundos, 3 por defecto). Los valores usan punto decimal.
`GOW_GS_REPLAY_STATE` selecciona el estado del juego que permite iniciar la captura
(entero decimal de 32 bits; `11` por defecto, partida; `4` para la intro).
No cambia el estado del juego ni detiene la captura si después cambia de estado.
`GOW_GS_REPLAY_TEXFLUSH=1` exige además un TEXFLUSH del juego en el estado seleccionado y guarda
el estado inicial después de esa invalidación. Por defecto vale `0`. No fuerza
una invalidación ni una transición del juego. Los comandos de presentación no
inician la captura. El límite sigue siendo 64 MiB y puede cerrar el tramo antes
del plazo. Las opciones mal formadas desactivan la captura con un mensaje antes
de abrir el archivo; sin `GOW_GS_REPLAY_TRACE` no se instala el diagnóstico.
El perfil limpia las cinco variables. Esta captura serializa el GS y no sirve
para medir FPS; los volcados permanecen en `logs/`, excluidos de Git.
Ejemplo y repetición: [RENDERIZADO.md](RENDERIZADO.md#repetición-local-de-comandos-gs).

### Control experimental del feedback OpenGL

`PS2X_GS_FEEDBACK_SNAPSHOT=1`, establecido antes de lanzar el ejecutable, protege
la fuente de textura cuando una primitiva escribe en sus mismas páginas. Queda
desactivado por defecto. Congela la fuente por primitiva y sirve para contrastar
la inestabilidad de OpenGL; la caché GS de 8 KiB y los lotes compatibles todavía
requieren verificación. El renderer CPU conserva su comportamiento.

Los controles sin el juego y sus límites están en
[RENDERIZADO.md](RENDERIZADO.md#separación-de-lotes-y-fuente-protegida-opcional).
`repetir_gs` selecciona explícitamente esta política con `--snapshot-feedback`
en modo compute/hardware. Continúa señalando las diferencias con CPU mediante
salida 1, aunque la imagen GPU se mantenga estable entre repeticiones.

`--pausa-ms M` añade una espera de 1..1000 ms entre pasadas de `repetir_gs`;
requiere `--repeticiones N` con N mayor que 1. Permite que el compilador de
shaders termine mientras se conserva el mismo backend. Ocho pasadas sin espera
pueden completar sus imágenes en compute antes de que hardware esté listo:
se deben comprobar `prims` y `tiles`, además del código de salida. La espera
no cambia el estado inicial ni las comparaciones y no mide los FPS del juego.

`--checkpoints-sync`, junto a `--repeticiones N` con N mayor que 1, compara
huellas FNV-1a64 de los 4 MiB de VRAM después de Flush, Sync, Present y End.
Indica el primer control que varía entre pasadas, con registro, operación y
dibujos acumulados. Las comparaciones exactas del End, estado y cuadros se
mantienen. Una huella distinta también produce salida 1, aunque el End coincida.
El historial está limitado a 4096 controles y un exceso se rechaza antes de
repetir dibujos. Añade readbacks y puede cambiar los tiempos: no mide FPS.
Se excluye TEXFLUSH porque no drena un lote GPU en el backend actual; observarlo
con readback introduciría un corte nuevo. Las huellas sirven para localizar
variación y no certifican igualdad byte a byte de los estados intermedios.

`--hasta-registro R` termina la repetición en un Flush, Sync, Present o End
registrado, con `Initial=0`. Permite reducir una diferencia a un tramo corto
sin añadir un corte entre dibujos. Comprueba la frontera antes de crear el
backend; rechaza Submit, TEXFLUSH, índices inexistentes y opciones duplicadas.
En un prefijo compara exactamente los 4 MiB y el estado CPU/candidato y las
presentaciones que ya ocurrieron. El End original y el resto de la captura
quedan sin validar; seleccionar su registro End conserva la comprobación
completa. El límite de 4096 controles se aplica solo al tramo seleccionado.

## Presentación explícita durante el perfil (2026-10-09)

`tests/perf_guard_test.ps1` ejecuta los scripts reales con procesos simulados.
Comprueba que el modo RAM reemplace un `PS2X_GS_DIRECT_PRESENT=1` heredado,
que el modo compartido reemplace un valor 0, que ambos restauren el entorno
y que compartida/CPU falle sin iniciar procesos. Conserva los controles de
carga externa inicial y sobrevenida, marca del registro y cierre exclusivo
del PID propio. La versión anterior del script falla por heredar el modo
compartido en RAM; la versión corregida pasa. No se ejecuta el juego en estos
controles.

La comparación real de cuatro pasadas está documentada en
`docs/RENDIMIENTO_PRESENTACION.md`; sus archivos derivados del juego son
privados y no se utilizan como fixtures de CI.


## Descarte hardware fuera de cobertura (2026-10-09)

El parche 77 añade `OpenGL hardware preserves thin subpixel triangle coverage
and blending`. Compara exactamente los 4 MiB de VRAM con CPU usando triángulos
finos, mezcla, scissor y VRAM inicial no nula. Repite durante la preparación
asíncrona y exige una pasada hardware sin aumentar tiles compute. La suite
Windows pasa 624/624, con 34 controles OpenGL reales, tanto con
`PS2X_GS_HW_DISCARD_UNCOVERED=0` como con el valor 1. El resultado de píxeles
es equivalente en ambos modos; la medida de tiempo se realiza aparte.

Compilación oficial completa y condiciones de las cuatro pasadas del juego:
`docs/RENDER_DESCARTE_FRAGMENTOS.md`. El ensayo sintético y los registros del
juego permanecen privados y no forman parte de los fixtures de CI.

## Capacidad de presentación al grabar GS (2026-10-09)

`tests/gs_replay_test.cpp` comprueba el decorador y `GS::usesSharedPresentation()`
con capacidad RAM/compartida, tanto directamente como dentro de
`GSThreadedBackend`. Se inicializa antes de consultar la capacidad efectiva.
Las consultas no hacen Flush/Sync/Present ni empiezan la captura: el archivo
queda reducido a su cabecera. El control falla con el decorador anterior y
pasa al reenviar `UsesSharedPresentation()` bajo su mutex. La consulta no
requiere OpenGL ni cambia el formato de las capturas.

`scripts/compilar_replay_gs.cmd` pasa completo, incluidos parser, truncamiento,
TEXFLUSH, checkpoints, CLI, feedback y oráculos procedurales. La recompilación
rápida del ejecutable termina con código 0 sobre el runtime privado de 77
parches ya validado; este cambio solo afecta al header del port.

El control funcional de 140 s confirma ISO real, host en textura compartida
con grabador activo, nivel listo y captura privada completa de 27.553.921 bytes.
La captura a 90 s del mando muestra Kratos, cubierta, lluvia y HUD. La repetición
CPU del End difiere en 343.007 bytes: el ensayo no certifica paridad del juego.
Esa divergencia queda como investigación separada de render; el archivo no se
publica. El cierre por límite no prueba el cierre normal ni mide FPS.


## Altura de patrones de feedback (2026-10-09)

`tests/gs_feedback_height_cli_test.py` verifica alturas 1/31/32/33/64/416,
freeze y texels invariantes, dumps por defecto exactos, separaciones TEXFLUSH/
scissor y la equivalencia de los 4 MiB entre GIF y End CPU. Una altura esperada
incorrecta falla; límites y opciones duplicadas fallan antes de crear archivos.
El caso de una fila fija independientemente el RGBA CPU `(141,90,98,128)` del
píxel `(32,0)`. El generador anterior falla al solicitar altura reducida.
Controles locales Windows y reproducción GPU: `docs/FEEDBACK_BILINEAL_MINIMO.md`.

## Persistencia de caché de textura entre Submit (2026-10-09)

`tests/gs_texture_cache_causality_test.py generador replay` comprueba 54 patrones
procedurales CPU, con dos pasadas de la sonda `--invalidar-cache-submit` por
patrón. Referencia y End son estrictos: un End adulterado devuelve 3. Se fijan
los bytes distintos (3/93/96 hasta 32 filas; cero desde 33), las bases y bytes
de caché, el RGB del único píxel, estabilidad entre pasadas y controles de
TEXFLUSH, scissor, textura disjunta y nearest. GPU, opciones incompatibles y
duplicadas se rechazan antes de crear salida. La CLI anterior falla este control.

La sonda solo interviene en el candidato CPU; no altera el renderer del juego.
Comandos, resultados y límites: `docs/FEEDBACK_BILINEAL_MINIMO.md`.

## Muestreo de modelos en intro (2026-10-09)

`tests/model_probe_test.cpp` prueba que agotar el cupo o las 64 plazas del menú
no consume las de intro. Comprueba alias, vistas independientes, intervalos de
dos segundos, 32 muestras por par y el límite de plazas. También prueba
lectura de matrices con NaN sintéticos, índices de articulaciones, memoria
truncada, punteros nulos, direcciones que cruzan el segmento y RAM intacta.
La CI lo ejecuta sin juego, con GCC; el control local MSVC pasa con `/W4 /WX`.

`GOW_MODEL_DIAG=1` registra pose y matrices crudas a la entrada de ProcessModel.
No fuerza su actualización ni cambia la selección de vistas. Las muestras se
identifican por fase, modelo, vista y ordinal; el tiempo transcurrido es del
host y las marcas del objeto/esqueleto son de jerarquía, no números de cuadro.
No usar estas ejecuciones para medir FPS.

Compilación completa Windows con 78 parches correcta. Dos controles de 95 s
sobre el mismo binario (SHA en ESTADO): con modelos activos se obtienen 343
muestras en intro y 1.016 matrices finitas en total; sin modelos se obtienen
128 muestras de cámara en intro. Ninguno registra estado 11. Terminación por
límite controlado; no se verifican aquí partida, equivalencia visual ni FPS.
Una mutación que comparte cupos entre menú e intro hace fallar la prueba.

## Referencia EE local de PCSX2 (2026-10-09)

En un perfil privado de PCSX2, activar PINE en un puerto libre (control local:
28012), arrancar la edición SCUS-97399 y pausar la escena deseada. La herramienta
usa únicamente identidad, estado y Read32 del
[protocolo oficial PINE](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/PINE.cpp).

```powershell
python tools/render/pcsx2_ee_reference.py --port 28012 --output logs/pcsx2_intro_01
python -m unittest discover -s tests -p test_pcsx2_ee_reference.py
```

El directorio de salida debe ser nuevo. Dentro del repositorio solo se acepta
`logs/`; no subir `eeMemory.bin` ni los savestates. Se rechaza el juego sin
pausar, una edición distinta y una marca de jerarquía/identidad que cambie
durante la lectura. La pausa y esa marca estable no equivalen a sincronización
con un cuadro del port. `active_view=0x0` entre cuadros es una observación
válida, no autorización para leer una supuesta vista en dirección cero.

Nueve controles sin juego comprueban fragmentación TCP, Read32 y dirección,
rangos y alineación antes de enviar, respuestas inválidas/truncadas, fallo o
desconexión, pausa, edición, cadenas con longitud/terminador, estabilidad de
captura y protección de directorios rastreados. La CI los ejecuta. El control
real de 32 MiB en la intro pasó y coincidió con una lectura independiente;
identidad, hash, configuración y límites están registrados en ESTADO.

## Articulaciones del modelo y padre de cámara (2026-10-09)

`GOW_MODEL_DIAG=1` añade `[gow-model:joint]` para las primeras ocho matrices
de la paleta, hasta el count de skin y con límites de RAM por matriz. Conserva
fase, modelo, vista, ordinal, índice, dirección y los 16 componentes por bits.
No reemplaza las muestras de raíz ni cambia su cupo/intervalo.

`GOW_CAMERA_DIAG=1` añade `[gow-camera:parent]` y `parentJointWorld` después
de SetupPipeline: padre del cliente, esqueleto, sellos, array y articulación
seleccionada. `validJoint` significa que se pudo leer esa matriz dentro de RAM;
no verifica por sí solo la identidad de la jerarquía. No se aplica el count de
skin a la cámara: el padre de la referencia PCSX2 tiene ese campo en cero y
su articulación 1 es válida. Una articulación `0xFFFF` usa local y no solicita
una matriz de la paleta del padre.

Los controles sintéticos cubren bits NaN sin operar floats, memoria intacta,
alias, count 0/3/20, límite de ocho matrices, punteros nulos, lectura exacta
de 64 bytes y cruce de segmento. El lector C++ sobre la RAM privada de PCSX2
obtiene las ocho matrices de los modelos 125/103 y la misma matriz por bits
en viewWorld, clientWorld y parentJointWorld. Una mutación del paso de la
paleta de 64 a 32 bytes hace fallar el control de modelos.

Compilación completa Windows con 79 parches correcta. El control nativo de
95,612 segundos registra 656 matrices articuladas finitas y no totalmente
nulas. Las 32 muestras válidas de cámara coinciden por bits en viewWorld,
clientWorld y parentJointWorld. En intro hay 32 muestras por cada índice
0..7 de ambos modelos articulados; las articulaciones 3..7 del modelo de 125
cambian mientras su raíz permanece constante. El control acaba por límite
deliberado, sin registros de estado 11; no mide FPS ni valida la paleta completa.
Identidad del binario, comparación y límites: ESTADO.

## Paleta articulada completa (2026-10-09)

`GOW_MODEL_DIAG=1` junto con `GOW_MODEL_PALETTE_DIAG=1` añade
`[gow-model:palette-joint]` y `[gow-model:palette]` en las mismas muestras de
ProcessModel. Incluye la marca de esqueleto, índice, dirección y bits de cada
matriz. Lee hasta el count de skin o 256 articulaciones, el primero que se
alcance. Ese máximo limita la salida del diagnóstico; no modifica la jerarquía
del juego. Se detiene ante la primera matriz fuera de RAM. `requested`, `read`
y `complete` distinguen una lectura completa de una truncada; no acreditan
fidelidad ni que VU1 haya recibido esas matrices. No emite paletas sin count
legible distinto de cero. Los ocho registros anteriores permanecen disponibles.

Control sintético sin juego: últimos índices de paletas 103/125, alias,
bits NaN, 256 matrices, truncamiento al solicitar 257, último byte ausente,
puntero nulo y cruce de segmento; RAM intacta. Mismos cupos, intervalo y
llamada original. No usar estas ejecuciones para medir FPS ni comparar cuadros
distintos como si fueran una secuencia sincronizada.

MSVC `/O2 /W4 /WX` pasa. Volver a limitar el lector completo a ocho matrices
en una copia privada hace fallar la prueba. Recompilación rápida correcta sobre
los mismos 79 parches; control nativo de 95,948 s: 84 lecturas completas,
9.598 matrices finitas y no totalmente nulas. En intro hay 32 muestras por
articulación de las paletas 125/103. Resultados, binario y límites: ESTADO.

## Adquisiciones de presentación compartida (2026-10-10)

`PS2X_GS_PRESENT_DIAG=1` registra `[gs-present:shared]` cuando el host adquiere
una secuencia distinta de la anterior, hasta 4.096 por instancia. Usa los
metadatos de esa textura: secuencia publicada/renderizada, ticks del host y
de la imagen, framebuffer/fuente, dimensiones, hash/validez y blank. No añade
readback, otra adquisición ni glFinish. El hash existente requiere
`PS2X_FRAME_HASH=1`; su coste y el del registro impiden usar este control para FPS.

Desde la ampliación de 2026-10-11 registra también `preferred` del frame
adquirido. En esta ruta GPU, `displayFbp` usa páginas de 8.192 bytes;
`sourceFbp` usa páginas con `preferred=0` o bloques de 256 bytes con
`preferred=1`. El inspector conserva los registros antiguos y cuenta sus
unidades como desconocidas, sin deducirlas por igualdad de valores crudos.
`source_units` incluye solo filas consistentes. No cambia la selección de
fuente ni verifica por sí solo que sea correcta. Quince pruebas Python pasan;
compilación nativa y control del nuevo formato pendientes.

`dimension_changes` informa línea, secuencia/render y tamaño anterior/nuevo
de las filas consistentes. No transforma una contradicción de metadatos en
un cambio de resolución válido ni enlaza tamaños entre modos separados.
El inspector tiene ahora 17 pruebas correctas; las dos nuevas regresiones
fallan con la versión anterior. Las dimensiones no acreditan encuadre ni aspecto.

Usar `GOW_PAD_TEST_NO_CAPTURE=1` con el guion automático mantiene entradas y
registros de estado sin capturas periódicas de VRAM. Eliminar del entorno los
otros diagnósticos: algunos se activan por presencia y siguen activos con
valor `0`. La matriz FINISH síncrono/asíncrono y presentación RAM/compartida,
las copias privadas de tarjetas y los límites de interpretación están en
[TRANSICION_COMPARTIDA.md](TRANSICION_COMPARTIDA.md).

El lanzador resuelve `--output-root` y admite, dentro de este checkout,
únicamente `logs/`; también admite directorios externos. Rechaza una salida
rastreable antes de crear la pasada. Trece pruebas Windows correctas; la
regresión contra la versión anterior intercepta la creación del directorio.

El inspector `tools/render/inspect_shared_frames.py` detecta retrocesos,
metadatos contradictorios para una secuencia, retención y retornos A–B–A.
Un retorno de hash es una pista; no prueba corrupción, igualdad de píxeles
ni contenido viejo. Las capturas posteriores de VRAM tampoco demuestran qué
textura compartida adquirió el host.

Desde el control de 2026-10-11, `tick_distance` informa el mínimo y máximo de
`hostTick - frameTick` y la línea/metadatos del primer máximo. Incluye filas
retenidas consistentes y muestras sin hash; excluye metadatos contradictorios.
La distancia puede ser negativa si el request muestrea el tick después que el
host. No es tiempo, no mide FPS ni prueba latencia sostenida. Cero retenciones
en el resumen no descarta retenciones que el diagnóstico no haya registrado.

`tools/render/run_transition_control.py` automatiza la matriz en carpetas
privadas nuevas, verificando manifiesto/ELF/ejecutable y usando tarjetas
independientes. Registra el estado de nuestra ventana sin capturas, para
distinguir minimización de pérdida de foco. Solo detiene el proceso que
inicia; crear `STOP` en la carpeta de la pasada conserva una parada deliberada
en `result.json`. Variables, argumentos, protocolo y límites:
[TRANSICION_COMPARTIDA.md](TRANSICION_COMPARTIDA.md).

## Bases de la fuente de presentación GS (2026-10-11)

`ps2recomp-gs-source-address.patch` compara las bases físicas de FRAME/DISPFB
y TEX0 en bloques de 256 bytes: convertir la página con un factor de 32.
Los metadatos conservan sus unidades originales. Dos números crudos iguales
pueden ser bases distintas; dos números distintos pueden ser la misma base.

Las regresiones sin juego comprueban 32/32, 32/1.024, 32/1.057 y 0/0
(página/bloque) mediante un backend grabador del frontend. El compositor CPU
usa píxeles procedurales distintos en las direcciones 8.192 y 262.144,
comprueba TBW cero y conserva el criterio existente cuando el ancho es explícito.
La prueba GPU existente añade los casos 32/32 y 32/1.024 con lectura explícita
y comparación CPU; esa lectura solo pertenece al control procedural.

El control negativo se ejecuta en CI sobre las regresiones sin el arreglo;
`5f3d7de` compila y falla únicamente las dos nuevas (591/593 correctas).
Comprobar además el CI del HEAD corregido. Compilación Windows completa,
suite GPU y transición con datos del usuario siguen pendientes. Registrar un
binario nuevo antes del control; no atribuir este cambio al ejecutable anterior.
La identificación de `preferred` del diagnóstico #51 permite observar después
si estos casos ocurren en el juego. No se declara corregido el encuadre ni
la alternancia por normalizar una comparación.

El control GPU compartido comprueba también TBW cero y ambos pares de
direcciones mediante `LoadImageFromTexture` sobre la textura adquirida,
comparando todas las filas con CPU y la selección esperada explícitamente.
No usa `copyLatchedHostPresentationFrame` como sustituto de esa textura.
Este readback existe solo en la regresión procedural; su ejecución Windows
con la cadena nueva sigue pendiente. El CI previo del arreglo `e52527d`
pasa 593/593; no acredita por sí solo esta ampliación GPU exclusiva de Windows.

## Configuración de las pruebas de FINISH (2026-10-11)

La suite completa normal y GPU se ejecuta con entorno de diagnósticos limpio
y FINISH síncrono (`GOW_GS_FINISH_ASINCRONO=0` o ausente). Dos grabadores
exigen que los draws y FINISH estén terminados al retornar. Bajo la opción
asíncrona esas aserciones no representan su contrato y los grabadores pueden
consultarse mientras el worker los modifica. No saltar pruebas ni añadir
excepciones para conseguir una suite verde en esa configuración.

`tests/gs_finish_async_test.cpp` ejecuta procesos separados `default` y
`async`: verifica retorno y CSR según el modo, retiene el backend mediante
un gate procedural y exige orden dibujos → FINISH → lectura y que la lectura
espere siempre. El CI valida ambos. Reproducir ese control específico en
Windows, además de la suite normal/GPU, antes de informar validación asíncrona.
El ensayo Windows adicional de toda la suite GPU asíncrona de 96 parches se
conserva como 623/625, sin declarar éxito ni modificar las expectativas del runtime.

## FINISH y píxeles compartidos en Windows (2026-10-11)

Con la cadena completa de 97 parches (`f51cc2c`), pasan 593/593 normal,
627/627 GPU real y `gow_gs_finish_async_test default|async` en procesos nuevos.
Se añade un control con GPU/contexto WGL reales y referencia CPU:

```text
cmake --build <build-del-runtime> --target gow_gs_shared_finish_test
<build-del-runtime>\gow_gs_shared_finish_test.exe default
<build-del-runtime>\gow_gs_shared_finish_test.exe async
```

El build debe incluir `tools/ci/pruebas_gs.cmake`, como hace CI para los
controles procedurales. Este target se define solo en Windows; necesita
OpenGL 3.3 y compartir texturas, y falla si hay fallback o no llega una
secuencia nueva. Fija FINISH y presentación directa antes de crear el backend;
ejecutar cada modo en un proceso separado, con el entorno GOW/PS2X limpio.

Ambos modos pasan: 16 FINISH/publicaciones, selección preferida/CRTC alterna,
píxel conocido y todas las filas comparadas con CPU, ticks/secuencias y cambio
64×64 → 64×96 al reutilizar las ocho ranuras. Solo este control descarga la
textura adquirida; no cambia el camino normal del host. No solicitar aquí
alturas menores de 64: el CPU las interpreta como configuración inválida.
La comparación inicial con altura 48 se conserva como fallida y no equivalente,
sin modificar el renderer CPU. No afirmar FPS, fidelidad del juego, cámara
correcta o ausencia de alternancia por estas pruebas procedurales.

## Matriz de 97 parches y muestreo de la espera (2026-10-11)

El runner `f51cc2c`, SHA-256
`ba80364535c22d728058ad98bf8182937f36c2db9ada5a11063f684c1a57add0`,
completa controles de 360 s: asíncrona/compartida y ambas RAM informan estado 11;
síncrona/compartida deja de informar después de estado 4 a 25,31 s.
Los detalles, la minimización y el solapamiento accidental de ~30 s al final
de síncrona/RAM constan en ESTADO.md. Se descarta la primera asíncrona/RAM
parada a 34,35 s y se repite aislada. No usar estos tiempos para comparar FPS.

El muestreo por PID con símbolos se realiza después de observar la espera,
solo sobre nuestra instancia: dos ventanas de 5 s. Localiza trabajo EE en
un bucle de COP0.Count; el worker GS espera comandos durante esas muestras.
Conservar las perturbaciones del muestreo y las capturas junto al resultado.
No concluir bloqueo del driver ni eliminar fences a partir de una espera
del juego que todavía no se ha explicado experimentalmente.

Una captura externa PrintWindow de la ventana propia visible y no minimizada
permite revisar la superficie aunque no esté en primer plano. Comprobar
siempre PID, ruta y propiedad de la ventana; rechazar la captura minimizada
o posterior al cierre. Sigue sin identificar por sí sola la secuencia GS
adquirida ni sustituye una comparación alineada con PCSX2. Imágenes privadas.

## Contador COP0.Count (2026-10-11)

`ps2recomp-ee-cop0-count.patch` añade el reloj del contador al runtime y cambia
la emisión MFC0/MTC0. Requiere `scripts\2_compilar.cmd`: el ejecutable generado
anterior sigue leyendo el campo inmóvil aunque se recompilen overrides.
Las cuatro regresiones sin juego comprueban avance/sondeo acotado, estado global
y wrap entre contextos, reset y emisión con destino $zero. El control negativo
de #59 mantiene la semántica anterior mediante APIs compatibles para poder
comprobar fallos de comportamiento; no se usa con el juego ni como HEAD final.

La integración positiva de 98 todavía debe completar compilación, suite,
CI y transición. El hallazgo del bucle muestreado no prueba por sí solo su
efecto funcional. Registrar otro commit/hash del runner y comparar primero
la espera compartida síncrona, manteniendo los hilos opcionales apagados.

Windows integrado de 98 (`f36936e`, runner SHA-256
`4693c62c14919c5bd400aa55b8da50a0ab5f5d281d3d20e7b02624aeaa5c6663`):
597/597 normal y 631/631 GPU real correctas. También pasan FINISH/lectura
y FINISH/textura adquirida en ambos modos, cada uno en proceso nuevo.
La pasada del juego empieza después de terminar compilaciones y controles;
estas pruebas procedurales no prueban solución de la espera o cámara.

Primera pasada síncrona/compartida con ese runner: 360,08 s por límite,
tarjeta intacta, sin firmas de error buscadas; alcanza informe 11 a 145,14 s.
Supera la espera de 96/97 tras 4. Capturas externas muestran progresión de
escena y posteriormente HUD/Kratos/enemigo, sin comparación alineada.
1.910 adquisiciones, 25 A–B–A (ninguno en tramos 4/11), sin regresiones,
inconsistencias ni hash desconocido; distancia máxima 34 antes del primer
informe, 1 en los tramos 4/11. Asíncrona/compartida se repite después, sin
solapar instancias. No presentar esto como benchmark o solución del encuadre.

La matriz aislada de 98 queda completa: las cuatro combinaciones llegan al
informe 11, incluidos ambos controles RAM (126,41/121,31 s) y la compartida
asíncrona (116,40 s). Cada control dura 360 s y termina por límite, tarjetas
intactas y sin firmas buscadas de error; ninguno minimizado o solapado. #59
fusionada con CI final verde. Los tiempos y capturas privadas no prueban FPS,
fidelidad de cámara ni ausencia de alternancia; detalles de cada caso en
`TRANSICION_COMPARTIDA.md` y `ESTADO.md`.

Tras la matriz, evaluar `--vu1-thread 1 --gs-front 0` en una pasada nueva
con etiqueta distinta. El frente GS requiere VU1 en hilo: su control posterior
usa `--vu1-thread 1 --gs-front 1`. No atribuir a GS aislado lo observado en
esa combinación ni usar estos controles con hash/diagnóstico como benchmarks.
Conservar runner/manifiesto/entradas/tarjeta y verificar que la pasada anterior
terminó antes de iniciar otra; no detener procesos de otros colaboradores.

Con hilos se observa que el cupo del diagnóstico acaba antes de partida.
La cadena 99 añade `PS2X_GS_PRESENT_DIAG_FROM_TICK` y el lanzador
`--diag-from-tick <uint64>`: inicio inclusivo según `hostTick`, cupo inalterado
de 4.096 por instancia, sin adquirir/leer/sincronizar más texturas. Usar una
pasada diferente para la ventana tardía y conservar el control de arranque;
los ticks no son segundos ni un mecanismo de alineación con PCSX2. La opción
no existe en el runner de 98: requiere compilación completa de la cadena 99.

VU1 solo completa 360 s; VU1+frente GS sale a 228,17 s con código 0 y causa
desconocida, antes del límite. Registrar `process_exit` y conservarlo como
incompleto, repetir aislado; llegar a estado 11 no acredita estabilidad.

La cadena 99 completa (`3dbac1a`, SHA-256 del runner
`cf063d2f03a1a78f455a9ff57a1378cd4a1ddaf71e6d279d305b34663b96fa07`)
pasa 600/600 normal, 634/634 GPU real y los cuatro contratos independientes
de FINISH/texturas; CI del mismo código verde. Repetición del juego pendiente.

Los controles anteriores con `GOW_FAST_BOOT=0` incluían sondas antiguas de
animación/flash-ready: el registro de overrides comprobaba presencia aunque
el atajo exigía `1`. El ajuste posterior de `src/` exige `1` también para
registrarlas. Recompilar rápidamente sobre el runtime completo de 99 y
anotar otro hash; verificar que no aparezcan `[gow-animation]` ni
`[gow-flash-ready]` cuando no hay diagnóstico explícito de animación/rutas.
Conservar los resultados anteriores con esa limitación, sin usarlos como FPS.

## Paridad de los retornos A–B–A (2026-10-11)

El inspector conserva todos los retornos y añade `frame_ticks` con los tres
ticks de las texturas adquiridas, más `content_return_parity`: `alternating`
si la paridad es 0–1–0 o 1–0–1, `same` si los tres coinciden y `other` para
los demás casos. Utiliza `frameTick`, no el tick del host que adquirió después;
una secuencia retenida no añade otra adquisición a ese historial. Cambios de
modo, dimensiones, hash desconocido o regresiones mantienen las separaciones
anteriores del historial. 20 pruebas Python del inspector pasan.

La presentación de campos puede producir contenido A–B–A legítimo: la suite
nativa GPU incluye un patrón con VRAM invariable y campos par/impar distintos,
comparados contra CPU, también con temporización GoW. Sin embargo, este registro
no contiene SMODE2 ni demuestra que el juego esté en ese modo. La paridad
observada no confirma entrelazado, causa del retorno, igualdad de píxeles o
ausencia de imagen antigua; sirve para seleccionar el siguiente control.

La cadena posterior de 100 conserva SMODE2 del request en la textura publicada
y añade `smode2` opcional al mismo registro adquirido. Requiere compilación
completa; el runner de 99 no tiene ese metadato. `smode2_values` del inspector
conserva los modos del A–B–A y `content_return_modes` separa desconocidos,
tres modos de campos (`(SMODE2 & 3) == 1`) y otros. Se aceptan logs antiguos
sin inventar su modo a partir de la paridad. 24 pruebas Python correctas;
regresión de texturas reales Windows/GPU y control funcional pendientes.

La primera suite nativa de 100 obtiene 600/600 normal y 634/635 GPU: el test
nuevo detecta altura CPU/GPU distinta en modo cuadro (`SMODE2 & 3 == 3`).
Se conserva el resultado inicial en ESTADO/TRANSICION_COMPARTIDA. Su ajuste
valida todos los modos como metadatos de texturas nuevas/retenidas y conserva
comparación de píxeles CPU en campos/progresivo. Para cuadro imprime las
dimensiones mediante `[gs-test:mode-frame]`; **no certifica equivalencia CPU
en ese modo**. El runtime de composición no cambia y su discrepancia sigue
pendiente. La revisión del test exige nueva compilación completa y validación.

La revisión `383c6b6` ya completa compilación Windows de 100, 600/600 normal,
635/635 GPU y cuatro contratos FINISH. El negativo API-compatible fuerza solo
el modo almacenado a cero: 634/635 con fallo único del caso nuevo. Al restaurar
la fuente positiva y recompilar, GPU vuelve a 635/635. La prueba de cuadro
registra CPU 64×128/shared 64×64; conserva explícitamente esa diferencia abierta.
Controles del juego con este runner y modo adquirido aún pendientes; detalles
e identidad del ejecutable en `TRANSICION_COMPARTIDA.md`.

Las tres pasadas de 100 ya completaron 360 s por límite con tarjeta intacta:
sin hilos, VU1 solo y VU1+frente GS. Las ventanas tardías tienen 4.096
adquisiciones cada una, todas con SMODE2=2227921 (`&3=1`). Sus 3.562 y 3.542
A–B–A alternan paridad y conservan modo de campos; sin regresiones de
secuencia/render ni metadatos inconsistentes. Validan que el diagnóstico
acotado funciona durante partida, no la causa visual, estabilidad prolongada,
equivalencia con PCSX2 o FPS. Identidad del runner, tiempos y límites en
`TRANSICION_COMPARTIDA.md`. Para comparar píxeles debe usarse una captura
explícita de la reserva adquirida en otra ejecución; no dar por equivalente
una captura de VRAM latched ni introducir su coste en el registro de metadatos.

La cadena 101 prepara una captura separada, desactivada por defecto:
`--capture-acquired 6` guarda PNG RGBA y JSON en `acquired/` de la carpeta
privada de una pasada compartida. Empieza desde `--diag-from-tick`, máximo
16 intentos; no captura retenciones/vacío ni sobrescribe archivos. Usa la
misma reserva de Acquire, no VRAM latched ni otro Acquire. **Añade readback
y E/S**, por lo que esta pasada no sirve para medir FPS. Sin esa opción no
se instalan variables de captura, incluso si estaban heredadas. Requiere
compilación completa de 101 y validación nativa antes de usarla con el juego;
regresión preparada, ejecución pendiente. Detalles y límites en
`TRANSICION_COMPARTIDA.md`; no publicar PNG/JSON del juego.

La captura de 101 ya completó compilación Windows, 600/600 normal,
636/636 GPU real, cuatro contratos y un negativo compatible que falla
exactamente por píxeles del PNG; fuente restaurada y GPU otra vez 636/636.
Tres pasadas de seis minutos guardan seis PNG/JSON cada una, correlacionados
contra las adquisiciones. La primera tuvo minimización; la repetición sin
hilos no la registra. En VU1 los PNG de ticks 5999/6001 son idénticos y el
de 6000 distinto: A–B–A finito confirmado en bytes, en modo de campos.
No demuestra antigüedad del contenido ni equivalencia de cámara/escena.
El tick de vídeo y el último estado del guion no alinean por sí solos dos
pasadas; consultar identidades y límites en TRANSICION_COMPARTIDA.md.

## Separar poses, cámara y rasterizado (2026-10-11)

#60 ya está fusionada y su CI final/merge es correcto (ver ESTADO.md).
El usuario también observa espasmos/deformaciones de modelos/animaciones de
protagonista y enemigos. Revisar secuencias de movimiento, no solo imágenes
aisladas, y localizar dónde aparecen: matrices finales de skinning, datos que
llegan a VU/GS, rasterizado o presentación. La paleta leída al entrar en
ProcessModel no equivale a la salida posterior de skinning; una matriz finita
no certifica una pose correcta. No identificar personajes solo por número de
articulaciones ni comparar por reloj anfitrión.

Control dedicado de cámara: `GOW_CAMERA_DIAG=1`, `GOW_ANM_DIAG=1`, fast boot 0,
FINISH síncrono/compartida/hilos 0. Muestreo finito/coherente del cliente, sin
referencia de partida PCSX2 alineada; detalles y minimización tardía en ESTADO.
La intro usa cámara articulada; las muestras principales de 11 usan matriz
local. No trasladar automáticamente conclusiones entre ambas cámaras.

Para aislar rasterizado se repite una única traza privada sin EE/IOP/VU ni
lanzar otra partida. Ejemplo: `repetir_gs <traza> hardware <carpeta-nueva>
--hasta-registro 12795 --repeticiones 3 --pausa-ms 250`; comparar también CPU
y compute y registrar primitivas/tiles para distinguir ruta mixta de hardware.
El número 12795 pertenece solo a esta traza; no reutilizarlo con otra.
`--snapshot-feedback` es experimental, añade protección de fuente por Submit,
no emula la caché PS2 y no se activa para declarar un arreglo o medir FPS.
El prefijo no comprueba el End del archivo ni toda la transición. Conservar
los códigos 1 debidos a discrepancia; no contarlos como éxito. Una repetición
exacta GS mantiene fijas las poses que recibió: tampoco prueba que fueran
correctas antes de capturar la traza.

### Diagnóstico posterior del skinning

`GOW_SKIN_DIAG=1` instala dos observadores de las llamadas retail 0x137508 y
0x137AF8 desde 0x1590F0, solo bajo estado 11. El resto y todas las reanudaciones
llaman al original. `[gow-skin]` y `[gow-skin:slot]` se emiten solo al retornar
al caller esperado; una pausa emite `[gow-skin:paused]` y se descarta su salida,
sin reconstruir argumentos al reanudar ni forzar que termine el cálculo.

128 intentos totales, 16 combinaciones skeleton/vista/variante, hasta 32
muestras por combinación y separación mínima de 100 ms del host. No es un
muestreo exhaustivo ni un contador de cuadros simulados. Captura entrada a3,
f12, disposición, selección dispersa, máscara, visibilidad y umbral; por slot,
fuente previa +0x50, paleta mundo a1 y destino real a2 posterior. También
conserva la matriz de definición mediante el ordinal de bits activos,
no mediante el índice del slot; índice de record +4 y padre +8 se distinguen.
El indicador
changed compara destino previo/posterior; no demuestra que no se escribiera
si los bytes siguen iguales. ownerCandidate es s1 del caller, sin identificación
confirmada de personaje. Los datos crudos quedan en logs privados.

El conteo de slots es u16 en skeleton+0x5C, limitado a 256 para el diagnóstico;
definición+0x10 tiene otro significado. Selección/máscara/visibilidad no prueban
que el slot se haya actualizado: intervienen umbral y reutilización. Cada
lectura valida RAM EE de 32 MiB o scratchpad de 16 KiB, incluidos sus alias,
sin normalizar direcciones inválidas mediante RAM_MASK. El observador no
escribe memoria ni modifica cálculos. Añade lectura/E/S y no mide rendimiento.

Preparado con pruebas procedurales de límites, selección no contigua, máscaras
y retorno/pausa, y sintaxis C++20. Requiere recompilación rápida de src/ sobre
la cadena completa 101 y control funcional antes de usar sus registros como
evidencia; esos resultados todavía están pendientes.

Validación posterior: src/ de `e0bf966` recompilado sobre 101 completo,
runner SHA-256 `063ea7299ffb9bb5bc413432df63f555a605225198557fc5cd753983ad2780a0`;
pruebas locales y CI correctos. Una pasada síncrona/compartida, hilos 0,
hash 0, guion de menú hasta 68 s recoge 128 retornos completos de ambas
variantes, cinco grupos y ninguna pausa. Destino/mundo/fuente válidos finitos;
producto mundo por definición coherente para 5.629 slots seleccionados y
cambiados. No identifica personajes ni certifica animaciones correctas.
El primer intento que quedó en menú se conserva separado; ver ESTADO.md.

La captura adquirida de 16 imágenes desde 1500 conserva sus metadatos exactos
y bytes completos. Los hashes del log son desconocidos por configuración;
comparar PNG explícitos. No correlacionar por coincidencia de reloj/tick
con otra ejecución o con las muestras de skinning. El guion actual solo
expresa direcciones digitales: `izquierda`/`derecha` no equivale a mover el
stick. Los controles de desplazamiento requieren soporte analógico explícito
y comprobación del paquete recibido, además de revisión de movimiento.

El soporte analógico descrito al principio ya se prepara para ese control;
su compilación y pasada funcional se registrarán por separado. La sonda de
poses admite `GOW_SKIN_DIAG_FROM_TICK=<uint64 decimal>`: espera el contador de
vídeo del runtime sin gastar el cupo en llamadas anteriores, y añade
`videoTick` a `[gow-skin]`. Ausente/inválido equivale a 0. Reutiliza el parser
del parche de ventana GS; no modifica el reloj. Ese contador no es el stamp
de jerarquía ni identifica automáticamente la textura adquirida o una pose
PCSX2. Es una selección de ventana, no una prueba de alineación.

Validación posterior de `73588d2`: recompilación rápida sobre 101 completa,
runner SHA-256 `0df18c12eb20455c0440fa26f9799f3b85be3babd7e0c0062f62f7cf5708d1be`,
pruebas locales y CI correctos. La pasada prolongada registra entrega por
libpad2 de las cuatro direcciones del stick con ataques combinados; no recoge
imágenes/muestras desde 3000 y no confirma desplazamiento visible. La pasada
de ventana 1500 obtiene 128 retornos completos de ambas variantes y 16
texturas adquiridas, con contador registrado junto a las muestras. Se revisa
una postura semejante al espasmo señalado por el usuario. Producto final
coherente no demuestra pose correcta ni identifica automáticamente el modelo;
la jerarquía previa y VIF/VU/GS requieren más investigación. Resultados y
límites completos en ESTADO.md; no son benchmarks.

### Condiciones de actualización de la jerarquía

La sonda añade `recordFlags` (u32 en record+0), `worldBeforeValid` y
`worldChanged`. Los bits `0x6000` pueden llamar a 0x136870 y transformar la
matriz calculada; un producto simple padre por fuente no basta en esos slots.
`worldChanged` compara bytes antes/después de la misma llamada completa,
solo si ambas lecturas son válidas; falso no demuestra que no se escribiera.
Se conserva la fuente previa, porque el cálculo puede sobrescribir buffers.
No cambia el cupo, el juego, las matemáticas ni las reanudaciones. Añade lectura
solo al diagnóstico solicitado. Registrar la validación de src/ y del control
antes de interpretar estos campos; las muestras anteriores no los contienen.

Validación de `e73e844`: compilación rápida, prueba procedural y CI correctos;
control de cinco minutos completa 128 retornos y 16 capturas adquiridas. En
el grupo de 111 slots, las grandes diferencias del producto indiscriminado
pertenecen a cuatro slots no seleccionados cuyos bytes quedan retenidos.
El producto de los slots cambiados sin transformación especial concuerda en
la muestra. No equivale a referencia de animación PS2. En esta pasada, las
imágenes revisadas ya muestran el brazo bajo: no reproducen la postura elevada
anterior ni se alinean por tener el mismo tick. Véase ESTADO.md para identidad,
cantidades y límites; no modificar matrices a partir de esa comparación parcial.

### Seleccionar la traza GS por vídeo

`GOW_GS_REPLAY_FROM_TICK=<uint64 decimal>` pospone el inicio de una traza
solicitada con `GOW_GS_REPLAY_TRACE`. Se combina con estado, segundos y
TEXFLUSH: todos deben cumplirse. Para una ventana de vídeo sin espera adicional
del host, usar `GOW_GS_REPLAY_AFTER=0`, estado 11 y TEXFLUSH 1. Ausente vale 0;
valores inválidos desactivan la captura con un error, sin opciones parciales.
No se omiten comandos anteriores al umbral: llegan al renderer sin grabarse.
El inicio registra `videoTick` del runtime, no metadatos de una adquisición.
No cambia formato, backend o límites ni alinea poses automáticamente con otra
pasada/PCSX2. La captura serializa/sincroniza y no mide FPS; datos privados.
Prueba procedural y replay CPU correctos; negativo al omitir el umbral falla
y restaurado pasa. Src/ de `d5ab720` recompilado sobre 101 completa, CI correcto.
Control con juego: inicio en tick 1500/estado 11/TEXFLUSH, ocho Present
1500..1507 y End completo. Reproducción hasta el segundo Present con CPU
repetible y hardware con diferencias entre pasadas de ruta constante; no
certifica paridad. Ambas imágenes revisadas muestran la pose elevada de la
introducción del barco. Véase ESTADO.md para identidad, cantidades y límites;
las matrices finitas y esa semejanza no resuelven el espasmo ni miden FPS.
