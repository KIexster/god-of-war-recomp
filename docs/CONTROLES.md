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

`GOW_PAD_GUION` sustituye esa secuencia por otra: una lista `segundo:botón` separada por comas, con
pulsaciones de 0,7 s. Botones: `start`, `select`, `arriba`, `abajo`, `izquierda`, `derecha`, `x`,
`circulo`, `cuadrado`, `triangulo`, `l1`, `r1`, `l2`, `r2`. Por ejemplo, para abrir Cargar en el menú:

```powershell
$env:GOW_PAD_GUION = '5:start,13:abajo,20:x'
```

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
