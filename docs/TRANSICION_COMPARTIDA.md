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

La ampliación de 2026-10-11 añade `preferred=0/1`, procedente del
`usedPreferred` de esa misma textura adquirida. En la ruta GPU compartida,
`displayFbp` está en páginas de 8.192 bytes; `sourceFbp` está en esas páginas
con `preferred=0`, o en bloques TEX0 de 256 bytes con `preferred=1`.
Comparar ambos números crudos no acredita igualdad de direcciones.
El indicador no añade readback, adquisición ni sincronización.

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

## Compilación y primera matriz Windows (2026-10-10)

Con el PC liberado por el usuario se completa `scripts\2_compilar.cmd`, código 0,
en el runtime privado `GOW_WORK=E:\gowgpuprof`, conservando sus fuentes previas
en un snapshot y verificando el destino de la junction. Código del port:
`76a25bedac7842e0a35cbd5eb56ca5941ebae429`, 90 parches, Release, VU1 intérprete.
SHA-256 del ejecutable nuevo:
`36e632cd39dfd73e3de3573c38b8c87715f9ef65b1eb6b2bba3ef776ed457c9f`.
No se modifica el checkout de Opus. La suite Windows pasa 591/591 en modo normal
y 625/625 con GPU real; libpad2 también pasa. El ensayo GPU del diagnóstico
registra seis adquisiciones procedurales, sin anomalías detectadas.

Las cuatro pasadas usan ese binario, el mismo ELF/ISO y guion de mando,
tarjetas privadas inicialmente idénticas y las variables de la matriz anterior.
Se eliminan otros diagnósticos opcionales; no hay replay ni capturas periódicas.
`GOW_FAST_BOOT=0` conserva los wrappers instalados por presencia, sin activar
el atajo. El panel de depuración está expandido en las cuatro pasadas.

| FINISH / presentación | Duración real | Primer estado 11 del guion | Adquisiciones | Retornos A–B–A | Mayor distancia host/frame tick |
|---|---|---|---|---|---|
| Síncrono / RAM | 360,06 s | 146,64 s | — | — | — |
| Síncrono / compartida | 360,04 s | 121,16 s | 2.186 | 105 | 3 |
| Asíncrono / RAM | 360,05 s | 116,32 s | — | — | — |
| Asíncrono / compartida | 233,22 s | 111,31 s | 1.247 | 109 | 417 |

Las tres pasadas completas se terminan al límite; el usuario pide recuperar el
PC durante la cuarta y se termina únicamente nuestro proceso identificado.
No es un cierre espontáneo ni un control completo de 360 s. Los últimos informes
de las cuatro mantienen estado 11; no se encuentran las firmas buscadas de
error VU1/GIF/excepción y ninguna tarjeta cambia. Esto no prueba jugabilidad,
equivalencia visual, cierre normal ni estabilidad prolongada.

Los registros compartidos no presentan retrocesos de secuencia/render,
hashes desconocidos ni metadatos inconsistentes. Todos los retornos A–B–A
aparecen antes del primer informe de estado 3. No aparecen tras el primer
informe de estado 11: 1.094 muestras síncronas y 342 asíncronas. Clasificar
por el último informe de estado solo delimita tramos aproximados. Como el
runtime registra únicamente adquisiciones nuevas, cero filas retenidas no
significa cero retenciones en la ejecución.

La secuencia asíncrona 906 tiene `hostTick=1365`, `frameTick=948` y distancia
417; las adquisiciones posteriores vuelven a ticks cercanos. Es una pista para
seguir esperas del host y trabajo pendiente del GS, sin asumir corrupción de
una ranura, equivalencia de píxeles ni retraso sostenido. Una distancia de ticks
no convierte esta ejecución con diagnósticos en una medición de FPS.

La revisión visual permanece incompleta: una captura externa RAM permite ver
HUD/escena/Kratos, pero el panel tapa gran parte de la imagen. No se usa
`copyLatchedHostPresentationFrame` como prueba de la textura compartida.
Reanudar con controles visuales separados y panel contraído, repetir la
combinación asíncrona y después evaluar VU1/frente GS por separado. Conservar
el encuadre dentro del framebuffer como investigación independiente.

El usuario vuelve a reservar el ordenador. #51 permanece en borrador hasta
completar el control funcional y comprobar el CI del HEAD final. Los datos del
juego, registros y capturas permanecen privados; se publica esta descripción
de los resultados y sus límites.

## Actualización de la cadena (2026-10-11)

Se integra `main` con #56/#57 mediante merge, conservando los seis parches de
VU1 nuevos antes del diagnóstico GS. Los 96 parches únicos pasan comprobación
y aplicación en un worktree privado limpio. Configuración: 0 errores,
4 avisos existentes; sintaxis de 21 scripts PowerShell: 0 errores.

El ejecutable y las cuatro pasadas anteriores corresponden a 90 parches,
no a esta integración. Recompilar completamente con el workspace privado
cuando el usuario confirme que el PC está disponible y registrar el nuevo
commit/hash antes de repetir el control. No trasladar los resultados nativos
anteriores al nuevo código VU1 ni a los hilos que no se probaron.

El inspector añade `tick_distance` para obtener directamente la distancia
firmada host/imagen y los metadatos de su máximo. Once pruebas correctas;
las tres nuevas fallan con la versión anterior. Incluye filas retenidas
consistentes y hash desconocido, excluye metadatos contradictorios y admite
distancia negativa cuando el request lee el contador después que el host.
El resumen reproduce los máximos 3/417 en los registros anteriores sin
ejecutar el juego. No demuestra latencia sostenida ni contenido antiguo.

## Lanzador reproducible y ventana del host (2026-10-11)

`tools/render/run_transition_control.py` prepara una carpeta nueva por pasada,
comprueba el ejecutable y ELF contra un manifiesto de compilación y copia
ELF/IOP a esa carpeta privada. La ISO se consume desde su origen. Cada pasada
crea la misma tarjeta procedural sin formatear o copia la indicada con
`--initial-card`; nunca usa el archivo original como tarjeta escribible.
Rechaza carpetas existentes y la salida dentro de los módulos de origen para
evitar una copia recursiva. No copia ni publica el ejecutable o los datos a Git.

La salida debe estar fuera de este checkout o dentro de `logs/`. El lanzador
resuelve la ruta y rechaza otras carpetas del repositorio antes de escribir
la pasada, conservando copias de tarjetas y resultados bajo la zona privada.

El manifiesto JSON requiere `commit` completo, `exe_sha256` y `elf_sha256`.
Registrar estos datos **después de comprobar que la compilación terminó**,
usando el commit de esa compilación, no otro HEAD posterior. Se recomienda
conservar también cadena de parches, modo VU1 y registro privado de build.
El hash del ELF debe coincidir con SCUS_973.99. El manifiesto verifica
identidad del binario, no constituye por sí solo prueba de compilación correcta.

Ejemplo para una **nueva compilación de 96 parches**, cuando el PC esté libre
y se haya creado el manifiesto privado correspondiente:

```powershell
$controlArgs = @(
    '--runner', 'E:\gowgpuprof\PS2Recomp\out\build\ps2xRuntime\ps2EntryRunner.exe',
    '--elf', 'E:\gowgpuprof\SCUS_973.99',
    '--iso', 'E:\gowport\God of War.iso',
    '--iop-mod', 'E:\gowgpuprof\IOP_MOD',
    '--build-manifest', 'E:\gowgpuprof\transition96\build.json',
    '--output-root', 'E:\gowgpuprof\transition96',
    '--seconds', '360',
    '--collapsed-ui'
)
python tools/render/run_transition_control.py sync_ram @controlArgs
python tools/render/run_transition_control.py sync_shared @controlArgs
python tools/render/run_transition_control.py async_ram @controlArgs
python tools/render/run_transition_control.py async_shared @controlArgs
```

No ejecutar esas rutas sobre el binario antiguo como si incorporase la cadena
nueva. Usar `--label` para una repetición en carpeta distinta. Mantener la misma
opción de panel en las cuatro pasadas. El lanzador elimina todas las variables
heredadas `GOW_*`/`PS2X_*` y fija la matriz; activa `GOW_PAD_SOLO_GUION=1`
(añadida en #56) para ignorar entradas físicas. No activa readback, replay ni
capturas. Los hilos están apagados inicialmente; los argumentos
`--vu1-thread 1` / `--gs-front 1` permiten controles posteriores separados.
El frente GS requiere VU1 en hilo.

`window_state.jsonl` registra cambios del estado consultado cada segundo:
ventana visible del PID propio, minimización y si el primer plano pertenece
a ese PID. No lee títulos de otras aplicaciones, activa ventanas ni captura
imágenes. La visibilidad Win32 no garantiza que la ventana esté descubierta,
y perder el foco no implica estar minimizada:
[IsWindowVisible](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindowvisible),
[IsIconic](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-isiconic).
Ventana no encontrada significa estado de minimización desconocido; el
intervalo de muestreo puede omitir cambios de menos de un segundo.

La versión raylib del runtime construido
[`c1ab645`](https://github.com/raysan5/raylib/blob/c1ab645ca298a2801097931d1079b10ff7eb9df8/src/platforms/rcore_desktop_glfw.c#L1258)
espera eventos mientras está minimizada sin `FLAG_WINDOW_ALWAYS_RUN`.
El runtime configura solamente `FLAG_WINDOW_RESIZABLE`, mientras el EE corre
en otro hilo. Esa pausa del host es una explicación posible de una imagen
con tick antiguo al restaurar la ventana. No se registró minimización en las
pasadas anteriores: perder el primer plano no demuestra que ocurriera y no
permite atribuirle los 417 ticks ni dar por resuelta la alternancia.

Crear el archivo `STOP` dentro de la carpeta de la pasada o pulsar Ctrl+C
detiene solamente el proceso iniciado por el lanzador. `result.json`
distingue límite, solicitud de parada, interrupción y salida espontánea;
conserva código de salida, tiempo y hashes de la tarjeta. Un cierre por límite
o por el usuario no es una prueba de crash ni de cierre normal del juego.

Doce pruebas locales Windows pasan, incluida una ejecución de Python como
hijo procedural que termina sin juego ni ventana. Las otras once prueban
entorno, matriz, propiedad de ventana, copia de tarjetas, rechazos antes de
escribir y parada del hijo. Se añade el control a CI; el ensayo completo
Win32 se omite en Linux. La consulta real Win32 también se comprueba sobre
nuestro proceso Python sin ventana. Esto valida la herramienta, no la escena.
Su nueva matriz con el juego y la cadena de 96 parches sigue pendiente.

Los hashes del manifiesto admiten mayúsculas y minúsculas, incluida la salida
habitual de `Get-FileHash`; rechazan valores que no sean 64 cifras hexadecimales.
Una regresión comprueba que un manifiesto equivalente en mayúsculas se acepta
sin relajar la comparación del ejecutable o del ELF.

## Unidades de la fuente adquirida (2026-10-11)

El diagnóstico inicial omitía `usedPreferred`, aunque el renderer y las
ranuras compartidas ya lo conservaban. Se registra ahora como `preferred`.
Esto distingue las unidades de la fuente seleccionada; no cambia qué fuente
elige el renderer, el contenido de las texturas ni el renderer CPU.

El inspector acepta tanto los once campos antiguos como los doce nuevos.
`source_units` cuenta filas consistentes en `unknown`, `pages_8192` y
`blocks_256`, incluyendo retenciones registradas. Una fila antigua no permite
deducir las unidades por igualdad numérica entre framebuffer y fuente.
Los máximos y retornos A–B–A conservan `preferred` cuando está presente.
Contradecir un indicador conocido bajo la misma secuencia es inconsistencia;
omitirlo en una fila antigua no borra el valor previamente conocido.
Rechaza flags fuera de 0/1, campos adicionales desconocidos y duplicados.

Quince pruebas Python pasan. Tres regresiones fallan con el inspector de
`60ce933`: unidades conocidas/desconocidas, conservación de metadatos entre
formatos y evidencia de fuente en un retorno de hash. Los 96 parches se
comprueban y aplican en un worktree privado nuevo sobre el upstream fijado.
La revisión de los dos registros Windows antiguos reproduce los máximos
3/417 y clasifica sus 2.186/1.247 filas como unidades desconocidas.

El CI de `60ce9338f9c27d312a474cab40498f1cf6fdcdd7` terminó correctamente
antes de esta ampliación. No valida su formato nuevo: comprobar el CI del
HEAD final y recompilar completamente en Windows antes de ejecutar el juego.
No hay compilación nativa ni control visual de `preferred` todavía;
#51 continúa en borrador. La selección de fuente y el encuadre siguen pendientes.

## Cambios de dimensiones en el resumen (2026-10-11)

Hasta ahora un cambio de tamaño interrumpía el patrón A–B–A, pero no quedaba
identificado en el JSON. `dimension_changes` informa línea, secuencia/render
y tamaño anterior/nuevo para las filas consistentes, incluso sin hash válido.
Excluye metadatos contradictorios de una misma secuencia y reinicia la
referencia al cambiar de modo del host.

Diecisiete pruebas Python pasan; las dos nuevas fallan con `715e216`.
Los dos registros Windows privados de 90 parches muestran un único cambio,
de 1×1 a 512×448 en la secuencia 2, al inicio. No se observa otro cambio en
las muestras registradas; esto no verifica el aspecto ni el encuadre de la escena.

El CI de `715e216fa72cec85e4112eb20b3fdcbba090a910` pasa ambos controles
([ejecución 38114877209](https://github.com/KIexster/god-of-war-recomp/actions/runs/38114877209)).
Compila la ampliación de `preferred` y valida la cadena integrada sin el juego.
La compilación Windows y la comprobación funcional del código actual siguen
pendientes. Este ajuste del resumen Python no cambia el parche del runtime.

## Directorio privado del control (2026-10-11)

Se aplica al lanzador el mismo criterio de directorio que a la referencia
PCSX2: fuera del checkout o dentro de `logs/`. Trece pruebas Windows pasan;
la nueva comprueba rechazo previo a cualquier escritura y aceptación de
`logs/` o un directorio externo. Falla con el lanzador de `2930666`;
el control negativo intercepta `mkdir` para impedir crear esa salida.
Solo se ejecuta un hijo Python procedural, sin juego ni compilación local.

El CI de `2930666cddd7d907a6576b2410ebed1a8695a61c` había pasado ambos
controles antes de este ajuste. Comprobar el CI del HEAD final. La herramienta
mantiene manifiesto explícito y tarjetas privadas; no altera el runtime.

La investigación de fuente produce la
[PR #58](https://github.com/KIexster/god-of-war-recomp/pull/58), independiente:
normaliza tres comparaciones de páginas FRAME/DISPFB frente a bloques TEX0.
Su control negativo `5f3d7de` compila y falla solo las dos regresiones nuevas;
el arreglo `e52527d` pasa 593/593 y CI completo. Ambos ensayos son remotos,
sin el juego. El control GPU Windows y la escena real siguen pendientes;
no demuestra la causa de la alternancia o cámara. Las dos PR permanecen en borrador.

## Referencia Windows de 96 parches y siguiente integración (2026-10-11)

El usuario libera el PC. Compilación completa terminada con código 0 en 886,33 s:
`de5a89ad437c952a6ae384025ed602e4e62a9c1b`, Release, VU1 intérprete, tres tareas.
SHA-256 del runner: `e5aae46440de86b0eeb97da91f742330ce0e95d22bbf70b368e83c44f082f0bd`.
591/591 pruebas Windows normales y libpad2 pasan. Copia del ejecutable, DLL,
manifiesto y registro en la carpeta privada `E:\gowgpuprof\transition96`.
GPU y matriz nueva todavía pendientes; no se atribuye a este binario el arreglo #58.

La rama integra después #58 por merge y pasa a 97 parches, con corrección de
fuente antes del diagnóstico del host. Requiere una segunda compilación completa
y un manifiesto diferente para comparar contra la referencia anterior. Los
resultados se publicarán con el commit/hash de cada binario, conservando
los dos problemas visuales y las retenciones no registradas como límites abiertos.

## Matriz completa de referencia de 96 parches (2026-10-11)

Binario `de5a89a` y SHA-256 `e5aae46440de86b0eeb97da91f742330ce0e95d22bbf70b368e83c44f082f0bd`,
sin el arreglo #58. Mismas entradas exclusivas, tarjetas procedurales privadas,
panel contraído, hash/diagnóstico activos, VU1/frente GS apagados:

| FINISH / presentación | Duración real | Primer informe 11 | Último estado del mando | Adquisiciones | Retornos A–B–A | Máxima distancia de ticks |
|---|---|---|---|---|---|---|
| Síncrono / RAM | 360,06 s | 149,78 s | 11 | — | — | — |
| Síncrono / compartida | 360,05 s | no registrado | 4 a 25,36 s | 4.096 (cupo) | 3.895 | 3 |
| Asíncrono / RAM | 360,05 s | 116,44 s | 11 | — | — | — |
| Asíncrono / compartida | 360,04 s | 116,39 s | 11 | 2.310 | 113 | 3 |

Las cuatro terminan deliberadamente por límite, con código de proceso 1 y
lanzador 0. Tarjetas intactas, ninguna firma de error buscada. La pasada
síncrona compartida deja de informar el mando en estado 4 y no registra
entrada en partida; host responsive y ticks/acquisiciones continúan hasta
el cupo de ~116 s reales. Su tramo de estado 4 contiene dos hashes alternados,
posiblemente campos de imagen estática: no prueba corrupción. Se muestrea
solo nuestro PID después de varios minutos de espera, 5 s con suspensión
breve de hilos; sin PDB, no identifica una función ni mide rendimiento.

La asíncrona compartida tiene cero retrocesos de secuencia/render, metadatos
contradictorios y hashes desconocidos. Sus 113 retornos ocurren antes del
primer estado 3; ninguno en las 1.205 adquisiciones del tramo del último
informe 11. Clasificación por informes separados ~5 s: no alinea cuadros.
Ambas compartidas usan `preferred=0` en todas las filas y cambian 1×1 → 512×448
solo al inicio. No se observa en ellas la selección corregida por #58 ni se
reproduce la distancia antigua de 417 ticks; eso no prueba solución global.

Ventanas visibles, nunca observadas minimizadas. Solo la síncrona/RAM gana
primer plano y permite dos capturas externas privadas; las otras no tienen
revisión visual válida en este control. Por tanto no se declara equivalencia
de cámara, ausencia de alternancia visible, jugabilidad ni estabilidad prolongada.
El fallo interno de encuadre sigue separado.

Se prepara la compilación completa de 97 parches integrados, con símbolos
privados para localizar la espera si se repite. Mantener Release optimizado:
`CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi` y
`CMAKE_EXE_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF`.
Registrar estos ajustes en el manifiesto; esa compilación no es un benchmark
ni el mismo binario de referencia. No publicar PDB ni código/datos del juego.

## Compilación y controles procedurales integrados (2026-10-11)

Cadena completa de 97, código `f51cc2cb7ee981c1cb6419926d46acc217de1f86`,
terminada con código 0. Runner SHA-256
`ba80364535c22d728058ad98bf8182937f36c2db9ada5a11063f684c1a57add0`.
Release/VU1 intérprete/tres tareas, con los flags optimizados y símbolos
privados indicados arriba. Manifiesto y copia congelada en `transition97`.
Duración aproximada del registro: 902 s; se recupera el manifiesto tras un
error del helper posterior a la compilación, explicado en `ESTADO.md`.

593/593 normal y 627/627 GPU real pasan en Windows. Los controles
`gow_gs_finish_async_test default|async` pasan ambos contratos por separado.
El nuevo `gow_gs_shared_finish_test default|async` adquiere y descarga
16 texturas nuevas en cada modo; compara todas sus filas y metadatos con
CPU, comprueba un píxel conocido independiente, alterna fuente preferida/CRTC
y cambia 64×64 → 64×96 al reutilizar las ocho ranuras. Exige GPU y sharing
reales; no admite fallback ni una secuencia retenida como validación.

El primer ensayo con altura 48 no era comparable con el CPU, que considera
inválidas dimensiones menores de 64. Se conserva su fallo y se corrige
el control, sin tocar la referencia. Este target necesita Windows/WGL y
no se ejecuta en CI Linux; no reemplaza la matriz del juego ni prueba ausencia
de imágenes antiguas, cámara correcta o estabilidad prolongada. Tampoco
añade readback al diagnóstico del host o calcula FPS.

## Runner con COP0.Count por ciclos (2026-10-11)

Cadena completa de 98: código `f36936e067aa283e4567e3b16700cfe4e546b629`,
898,79 s, salida 0, VU1 intérprete/tres tareas y símbolos privados.
SHA-256 del runner congelado:
`4693c62c14919c5bd400aa55b8da50a0ab5f5d281d3d20e7b02624aeaa5c6663`.
El CI pasa 597/597. Se han regenerado las lecturas de Count del bucle EE;
el runner de 97 no contiene esa corrección.

Repetir primero síncrona/compartida, que esperó tras estado 4 en las dos
cadenas anteriores, y después asíncrona/compartida. Conservar guion,
tarjeta inicial, hilos apagados y diagnósticos del protocolo. Solo una
partida a la vez y sin compilación; registrar también minimización y capturas.
La suite nativa y ambos controles FINISH deben completar antes. No trasladar
el éxito de CI/compilación a la transición, cámara, píxeles del juego o FPS.
