# Diagnóstico de la transición con presentación compartida

Estado actual, 2026-10-12: #51 fusionada en
`05421940617e5ce69889600d9eb727b3c36c0b3b` tras su validación y CI del HEAD final.
La cadena posterior 102 incluye la corrección EE PMULTH de #67: compilación
completa nativa, 602/602 normal, 638/638 GPU y cuatro contratos correctos.
El primer control síncrono/compartido sin hilos completa cinco minutos; sus
16 imágenes adquiridas de la introducción son favorables, con los límites
descritos en ESTADO.md. La combinación asíncrona con hilos y el control amplio
de animaciones/NPCs siguen pendientes; no se declaran todos los fallos resueltos.

La adaptación independiente de PCSX2 de #68 está conectada como espejo opcional
en la cadena 103. Dos controles compartidos de seis minutos verifican recepción
de GIF/VSync y salidas de introducción/NPCs; el asíncrono muestra HUD. Conserva
el renderer habitual y usa rutas DMA genéricas, sin hilos VU1/frente GS. No
certifica animación, paridad de escena, rendimiento o estabilidad prolongada.
La captura por ráfaga y los controles RAM siguen en curso. Véase PCSX2_GS.md.
Los resultados históricos siguientes conservan la identidad de sus cadenas.

Count #59 y unidades de framebuffer #58 están fusionadas en main. Se conservan
la matriz aislada de cuatro controles de 98 y las tres pasadas de seis minutos
de 99; no acreditan el nuevo campo SMODE2 de 100. La comparación visual
alineada, el encuadre y la estabilidad prolongada siguen abiertos. Los apartados
siguientes conservan los resultados y las limitaciones de cada ejecutable.

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

## Matriz aislada completa con Count (2026-10-11)

Mismo runner de 98, código `f36936e067aa283e4567e3b16700cfe4e546b629`,
SHA-256 `4693c62c14919c5bd400aa55b8da50a0ab5f5d281d3d20e7b02624aeaa5c6663`.
Guion exclusivo, tarjetas procedurales privadas iguales, VU1 intérprete,
hilos VU1/frente GS apagados y hash/diagnóstico del protocolo. Ninguna
compilación, otra partida o emulador simultáneos en estas cuatro pasadas.

| FINISH / presentación | Duración real | Primer informe 11 | Último estado | Adquisiciones | A–B–A |
|---|---|---|---|---|---|
| Síncrono / compartida | 360,08 s | 145,14 s | 11 | 1.910 | 25 |
| Asíncrono / compartida | 360,04 s | 116,40 s | 11 | 2.325 | 140 |
| Síncrono / RAM | 360,04 s | 126,41 s | 11 | — | — |
| Asíncrono / RAM | 360,05 s | 121,31 s | 11 | — | — |

Todas terminan deliberadamente por límite (proceso 1/lanzador 0), tarjetas
intactas, sin firmas buscadas de error. Ventanas visibles y nunca observadas
minimizadas. RAM síncrona gana primer plano desde ~21 s; RAM asíncrona desde
~41 s. Una captura propia de cada RAM permite revisar enemigos o HUD/Kratos;
no identifica por sí sola un cuadro GS ni lo alinea con PCSX2, y perturba
el instante capturado. No utilizar los tiempos como FPS.

En las dos compartidas, cero regresiones de secuencia/render, metadatos
contradictorios o hashes desconocidos; ninguno de los A–B–A en los tramos
del último estado 4/11. Todas las fuentes son CRTC, `preferred=0`, y solo
cambia la dimensión inicial 1×1 → 512×448. Máxima distancia de ticks 34
(síncrona, antes del primer informe del mando) y 3 (asíncrona); máximo 1
en sus tramos 11. El registro omite retenciones y tiene cupo; ausencia de
patrones no certifica ausencia de imagen antigua o igualdad de píxeles.

#59 se fusiona en `cee659a` tras [CI del HEAD final `ab89804` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38139636066).
#51 integra main mediante merge, conservando el código del runner: comprobado
diff vacío de parches/script/src/config frente a `f36936e`. La espera concreta
de 96/97 se supera; cámara, alternancia visible y estabilidad con hilos siguen
abiertas. Se inicia después, aislada, asíncrona/compartida con solo VU1 en hilo;
el frente GS requiere VU1 en hilo y se evalúa en otra pasada posterior.

## Cupo con hilos y ventana diferida (2026-10-11)

Con el runner validado de 98, asíncrona/compartida y VU1 en hilo solo:
360,04 s por límite, informe 11 desde 80,90 s y último 11, tarjeta intacta,
sin firmas buscadas de error ni minimización. Captura propia privada muestra
HUD/Kratos/enemigos, sin alineación de cuadro. El diagnóstico agota 4.096
adquisiciones en estado 4, última en `hostTick=4136`; no cubre ninguna bajo
el último informe 11. Sus 3.064 A–B–A y cero regresiones/inconsistencias/hash
desconocido solo describen aquel tramo inicial.

La combinación posterior VU1+frente GS informa 11 desde 80,83 s, pero termina
a 228,17 s con `process_exit`, salida 0, antes del límite 360 s. El registro
no determina la causa del cierre. Tarjeta intacta, ninguna firma buscada
de error ni minimización; sin captura propia de esa pasada. No es un control
completo ni prueba estabilidad. También agota el cupo en estado 4, 3.045
A–B–A y ningún registro de adquisición bajo el último informe 11. Repetir.

Nuevo parche por tema `ps2recomp-gs-host-diag-window.patch`, aplicado después
del diagnóstico: `PS2X_GS_PRESENT_DIAG_FROM_TICK=<uint64 decimal>` permite
registrar desde ese `hostTick`, inclusivo. Ausente/inválido conserva inicio
0. Mantiene máximo 4.096 registros por instancia, solo adquisiciones nuevas,
los mismos metadatos y hash existente; no cambia Acquire, render, FINISH,
fences o readback. Los ticks son los del registro, no segundos ni alineación.
El lanzador acepta `--diag-from-tick 6000`, registra la variable en el
manifiesto y rechaza rangos inválidos antes de crear la pasada. Desplazar
la ventana omite explícitamente el arranque; conservar ambas ejecuciones.

Regresiones nuevas sin juego: no gastar cupo antes del tick inclusivo,
mismo límite 4.096 desde cero o diferido, parse decimal/límites de uint64.
Las tres pasan con MiniTest/MSVC en un ejecutable reducido. El control
negativo usa APIs compatibles con la semántica previa (sin ventana):
1/3 pasa y fallan exactamente las dos de aplazamiento/parser; no es la suite
nativa completa. 14 pruebas del lanzador y 17 del inspector pasan; configuración
0 errores/4 avisos, sintaxis 23 scripts/0 errores incluyendo los privados,
cadena 98 aplicada en orden y nuevo parche aplicado en árbol independiente.
La cadena 99 requiere compilación completa, suite/CI y repetición funcional.

## Validación nativa de 99 y aislamiento de las sondas (2026-10-11)

Compilación completa desde `3dbac1a442add16518b0504975bfa715f1b32dc4`,
99 parches, salida 0, 1.005,59 s. VU1 intérprete, Release optimizado con
símbolos privados y tres tareas. Runner congelado SHA-256:
`cf063d2f03a1a78f455a9ff57a1378cd4a1ddaf71e6d279d305b34663b96fa07`.
[CI del mismo código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38152876084).
Windows: 600/600 normal y 634/634 GPU real; comparador sin fallos conocidos.
FINISH/lectura y FINISH/textura adquirida pasan `default` y `async`, cada
modo en proceso nuevo. El último compara 16 texturas por modo, todas sus
filas y metadatos contra CPU, incluyendo reutilización de las ocho ranuras.
Estas pruebas no acreditan los píxeles ni la estabilidad del juego.

Se detecta además que la condición de instalación de los overrides de
animación comprobaba presencia de `GOW_FAST_BOOT`, aunque el atajo propiamente
dicho ya exigía valor `1`. Por ello las pasadas anteriores con valor `0`
incluían `[gow-animation]` y `[gow-flash-ready]`, aun sin activar el atajo.
Se conserva esa limitación en sus resultados. El cambio en `src/` exige
valor exacto `1` para instalar esas sondas por fast boot; los diagnósticos
explícitos `GOW_PATH_DIAG`/`GOW_ANM_DIAG` conservan su activación existente.
No cambia las funciones originales cuando las sondas están desactivadas.

El runner congelado anterior no contiene este último ajuste de overrides.
Recompilar solo `src/` sobre la cadena 99 completa, registrar otra identidad,
y verificar ausencia de esas etiquetas con `GOW_FAST_BOOT=0` durante la
repetición funcional. Mantener #51 borrador hasta completar los controles
requeridos y comprobar CI del HEAD final; cámara y alternancia siguen abiertas.

## Primer control funcional de 99 sin sondas de animación (2026-10-11)

Sobre el runtime completo anterior se recompilan solo overrides desde
`a37cd856ccc884c03b7a8260957d56d9d7ac49cd`, salida 0 en 19,05 s, sin
regenerar el C++ ni cambiar parches. Nuevo runner SHA-256:
`a4ec4365273903d525ce19ecade3fb423fb871c5628a84e695b33ac777ca3598`.
[CI del mismo HEAD correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38154314459).

Asíncrona/compartida, hilos apagados, ventana por defecto (tick 0), guion
exclusivo/hash y tarjeta procedural privada iguales: 360,06 s por límite
(proceso 1/lanzador 0), primer informe 11 a 154,02 s, último 11. Tarjeta
intacta, ninguna firma buscada de error ni etiquetas de animación/flash-ready.
Ventana visible, nunca minimizada ni en primer plano durante esta pasada.
Sin captura propia; el control de ventanas por Computer Use no está disponible.

2.046 adquisiciones, 27 A–B–A solo antes de informe 3; cero regresiones,
metadatos contradictorios o hashes desconocidos. Hay 983 adquisiciones bajo
el último informe 11, ninguna A–B–A allí; fuente CRTC/`preferred=0` en todas.
Solo cambia la dimensión inicial 1×1 → 512×448. Distancia máxima de ticks 37
en arranque bajo informe 0; máximo 5 en tramo 4 y 1 en 11. Los informes del
mando son muestras espaciadas, no una alineación de cuadro. No se diagnostica
imagen antigua ni rendimiento a partir de esas distancias o tiempos.

Se repite después VU1 en hilo solo con `--diag-from-tick 6000` en otra carpeta
privada; luego VU1+frente GS. No solapar partidas, emulador o compilaciones.
La ausencia de patrones en estas muestras no certifica presentación visual
correcta, equivalencia de píxeles, cámara o estabilidad prolongada.

## Ventana de partida con VU1 en hilo (2026-10-11)

Mismo runner `a37cd85`/SHA-256
`a4ec4365273903d525ce19ecade3fb423fb871c5628a84e695b33ac777ca3598`,
asíncrona/compartida, VU1 en hilo y frente GS apagado, ventana desde 6000.
Completa 360,06 s por límite; informe 11 desde 81,05 s, último 11. Tarjeta
intacta, ninguna firma buscada de error ni sonda de animación/flash-ready,
ventana visible/nunca minimizada, sin primer plano ni captura propia. No se
solapa con otra partida, emulador o compilación.

El primer registro tiene `hostTick=6000`, `frameTick=5999`, secuencia/render
5880. Se registran exactamente 4.096 adquisiciones, todas bajo último informe
11: el filtro conserva el cupo durante el arranque. Fuente CRTC/`preferred=0`,
512×448, hash válido en todas; cero regresiones de secuencia/render, metadatos
contradictorios o cambios de dimensión. Distancia de ticks 0..2.

Hay 3.009 A–B–A en este tramo. El inspector ahora conserva los tres ticks
por retorno y cuantifica su paridad sin suprimir ninguno: los 3.009 son
0–1–0 o 1–0–1 en `frameTick`; cero con paridad constante o distinta.
Sus 20 pruebas incluyen ticks del host iguales con paridades de textura
distintas, retenciones y separaciones por modo/hash desconocido.
El control sin hilos anterior también tiene sus 27 retornos de arranque
con paridad alterna, ninguno durante partida.

La suite GPU procedural ya comprueba dos campos distintos con VRAM invariable
y temporización GoW contra CPU. Este resultado es compatible con alternancia
de campos, pero el registro del juego no incluye SMODE2: no prueba ese modo,
su causa ni ausencia de contenido antiguo. No se declaran píxeles iguales,
cámara correcta o FPS. El cupo sigue siendo finito y deja sin registro el
resto de la pasada. Se inicia después la repetición VU1+frente GS desde 6000.

## Repetición completa con VU1 y frente GS (2026-10-11)

Mismo runner `a37cd85`/SHA-256
`a4ec4365273903d525ce19ecade3fb423fb871c5628a84e695b33ac777ca3598`,
asíncrona/compartida, VU1+frente GS en hilos, ventana desde 6000: completa
360,04 s por límite (proceso 1/lanzador 0), informe 11 desde 80,97 s, último
11. Tarjeta intacta, ninguna firma buscada de error ni sondas de animación/
flash-ready; ventana visible/nunca minimizada, sin primer plano ni captura
propia, sin solapamientos. Esta pasada no reproduce el cierre anticipado de
98 a 228,17 s: su causa sigue desconocida, no se declara corregida por ello.

Primer registro hostTick=6000, frameTick=5999, seq/render=5933. Las 4.096
adquisiciones están bajo el último informe 11, 512×448/fuente CRTC/hash válido,
sin regresiones de secuencia/render ni metadatos contradictorios o cambios
de dimensión. Distancia de ticks 0..1. 3.506 A–B–A, todos con paridad alterna
de frameTick; no demuestran imágenes antiguas ni entrelazado por sí solos.

La cadena 99 supera así tres controles finitos: sin hilos/ventana 0, VU1
solo/ventana 6000 y VU1+frente GS/ventana 6000. No se repite aquí la matriz
RAM/síncrona completa de 98 ni se trasladan sus resultados al nuevo runner.
[CI del inspector y documentación `6f30f5c` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38155362566);
comprobar el nuevo HEAD documental después de publicar este resultado.
Mantener #51 borrador por comparación visual pendiente. Para distinguir
campos de otros retornos, el siguiente diagnóstico debe conservar SMODE2
del request usado para producir la textura, no leer el CRTC actual después.
Las pruebas y adquisiciones nuevas no resuelven el encuadre dentro del GS.

## Modo del request realmente compuesto: cadena 100 en preparación (2026-10-11)

Nuevo parche separado `ps2recomp-gs-acquired-mode-diag.patch`, después de la
ventana: conserva `request.smode2` completo en la ranura al publicar, junto
con su tick. El host registra `smode2=<uint64 decimal>` del mismo Frame
devuelto por Acquire. No lee el CRTC actual, adquiere otra textura, descarga
píxeles, calcula otro hash o cambia FINISH/fences/filtrado/composición.

Regresión Windows/WGL con textura real: VRAM fija, campos par/impar, modo de
cuadro y progresivo, comparar filas contra CPU y comprobar SMODE2 completo
en cada secuencia nueva y retenida. Cambiar después el request del llamador
no altera el modo de la textura anterior. La altura del patrón es 128 para
mantener al menos 64 filas lógicas incluso en el modo que divide el buffer.
No dar por correcta esta nueva regresión antes de ejecutarla; requiere GPU
real en Windows, no se ejecuta como éxito vacío en CI Linux.

El inspector acepta 11..13 campos, incluidos los dos opcionales `preferred`
y `smode2`; rango uint64, coherencia por secuencia y registros antiguos.
Conserva todos los A–B–A: `smode2_values` guarda los tres modos cuando alguno
es conocido; `content_return_modes` cuenta `unknown` si falta alguno,
`all_field` si en los tres `(SMODE2 & 3) == 1`, y `other` para el resto.
Modo registrado y paridad compatibles no prueban la causa del retorno,
píxeles iguales ni ausencia de imagen antigua. Los logs de 99 siguen sin
SMODE2 y deben clasificarse como desconocidos, sin inventarlo por la paridad.

24 pruebas Python del inspector correctas, sintaxis 23 scripts/0 errores,
aplicación independiente del nuevo parche sobre una base de 99 correcta.
Pendientes CI del HEAD final, compilación completa Windows de 100, prueba
nativa nueva/negativo significativo y otra ventana de partida. El runner
de 99 no contiene el campo. Conservar su identidad y resultados anteriores.

## Control inicial de 100 y alcance de la regresión de modo (2026-10-11)

La compilación completa de `4a000c48e2d111637a77318eb509dd88f35d653d` termina
con salida 0 en 943,29 s; 100 parches, Release `/O2 /Ob2 /DNDEBUG /Zi`, link
`/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF`, VU1 intérprete y símbolos privados.
Runner de 54.542.848 bytes, SHA-256
`fb46a87ac6d80a600f8662ae3ee6b74bbb5b37b84f1cc10fa0eb0b0f3d4367b5`.
[CI de ese código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38156407391).
Después de congelarlo se recompilan también los objetos de los controles
procedurales privados: el campo nuevo cambia el layout de Frame.

Suite Windows: 600/600 normal en 13,42 s; GPU real 634/635 en 31,35 s. Falla
solo «SMODE2 de textura adquirida y retenida», con razones «alto lógico» y
«Composición real comparada contra CPU». Los contratos siguientes quedan sin
ejecutar por esa salida; ninguna partida usa este runner. Mantener el fallo
como resultado, sin confundir compilación correcta con validación funcional.

Para `SMODE2=0x403` y DISPLAY de 64×128, la fórmula GPU divide la altura por
dos y CPU no la divide. El diagnóstico no modificó esas fórmulas. Investigar
por separado qué interpretación corresponde y si ese modo aparece realmente
en el juego; la diferencia no prueba la causa del encuadre ni de la alternancia.

La nueva prueba se ajusta al propósito de conservar el metadato: mantiene
campos par/impar, cuadro y progresivo con bits altos, todas las adquisiciones
nuevas y retenidas. Campos/progresivo siguen comparando todas las filas contra
CPU. En cuadro se adquiere una textura real no vacía y se imprimen las
dimensiones de ambos renderers mediante `[gs-test:mode-frame]`, sin afirmar
equivalencia allí. No se suprime el modo ni se cambia composición, FINISH o
fences. Esta decisión reduce expresamente el alcance de la comparación CPU
del test nuevo; no cambia las pruebas anteriores ni tapa el primer fallo.

Pendientes otra compilación completa, suite normal/GPU, control negativo
compatible que fuerce el metadato a cero y restauración positiva, contratos
independientes y ventana del juego. No usar el runner anterior para acreditar
la prueba ajustada. PR #51 permanece borrador y el encuadre sigue separado.

## Validación nativa de 100 revisada y negativo del metadato (2026-10-11)

Nueva compilación completa desde `383c6b65986ffe2cddebaed987d9132ea791d100`,
100 parches, salida 0 en 928,11 s. Release optimizado con símbolos privados,
VU1 intérprete y flags iguales a la anterior. Runner de 54.542.848 bytes,
SHA-256 `5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`.
[CI de ese código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38157988334).
Se congelan runner/símbolos antes de incluir los controles privados y se
recompilan sus objetos por el cambio de layout de Frame.

Windows: 600/600 normal en 13,39 s y 635/635 GPU real en 30,87 s; ambos
comparadores devuelven 0, sin fallos conocidos. Los cuatro controles pasan en
procesos nuevos: FINISH/CSR/orden/lectura y FINISH/textura adquirida, cada uno
con `default` y `async`. El segundo compara 16 texturas por modo, todas las
filas contra CPU, fuentes, dimensiones y reutilización de ocho ranuras.

El caso nuevo confirma SMODE2 completo en texturas nuevas y retenidas para
campos/cuadro/progresivo; campos/progresivo comparan píxeles contra CPU. En
cuadro registra realmente `smode2=1027 cpu=64x128 shared=64x64`. Ese caso
solo valida textura/metadatos: la equivalencia de composición sigue pendiente
y no se declara corregida por el éxito de la suite ajustada.

Negativo privado compatible con la API: cambiar únicamente el almacenamiento
de `slot.frame.smode2` por cero y recompilar **solo las pruebas**, sin alterar
ni ejecutar el runner. Compila y ejecuta 635 casos: 634 pasan, falla exactamente
«SMODE2 de textura adquirida y retenida». El comparador denuncia esa regresión;
no es un fallo de compilación ni una excepción en `fallos_conocidos.txt`.
Después se restaura el archivo positivo completo, se recompilan las pruebas
y GPU vuelve a 635/635 en 31,33 s. Fuente restaurada y runner congelado verificados.

Pendientes controles funcionales aislados de este runner: primero hilos 0 y
ventana desde 0, luego VU1 y VU1+frente GS por separado desde 6000. El diagnóstico
no introduce readback, Acquire adicional, cálculo adicional de hash ni glFinish.
Los patrones del juego y la comparación visual aún no están validados para 100.

## Primer control funcional de 100: modo adquirido durante partida (2026-10-11)

Runner completo `383c6b6`, SHA-256
`5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`,
asíncrona/compartida, hilos 0, registro desde 0 y guion/tarjeta procedural
privada iguales. Completa 360,06 s por límite (proceso 1/lanzador 0), primer
informe 11 a 133,85 s, último 11. Tarjeta intacta; ninguna firma buscada de
error ni sondas de animación/flash-ready. Ventana visible/nunca minimizada,
en primer plano solo entre 17,12 y 41,29 s según el monitor; sin captura propia.
El cambio de primer plano no fue una entrada automatizada de Computer Use.

1.955 adquisiciones, sin regresiones de secuencia/render, metadatos contradictorios
o hashes desconocidos; fuente CRTC/`preferred=0`. Dimensión inicial 1×1 →
512×448. Seis A–B–A, todos antes de los tramos 4/11, con paridad alterna y
tres modos de campos registrados. Distancia de ticks máxima 37 en arranque,
6 en tramo 4 y 1 en 11; no mide tiempo ni demuestra latencia sostenida.

Hay 1.063 adquisiciones bajo último informe 11: `smode2=2227921`, cuyo valor
bajo es `(SMODE2 & 3) == 1`, modo de campos. Ninguna A–B–A allí. El caso de
cuadro con altura CPU/GPU distinta (`&3 == 3`) no corresponde a estas muestras;
no descarta otros problemas de composición ni demuestra la causa de la cámara.
Los informes del mando son espaciados, no una alineación de cuadro con PCSX2.

[CI de la validación nativa/documentación `6a53774` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38159575279).
Se inicia después VU1 en hilo solo, ventana desde 6000, en otra carpeta;
VU1+frente GS se repetirá tras cerrar esa pasada. No solapar partidas, emulador
o compilación, ni usar estos tiempos/hash/diagnóstico/intérprete como FPS.
Sin comparación visual, no se declaran resueltos alternancia, encuadre o cuelgues.

## Control de 100 con VU1 solo: retornos en modo de campos (2026-10-11)

Mismo runner completo `383c6b6`/SHA-256
`5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`,
asíncrona/compartida, VU1 en hilo y frente GS desactivado, ventana desde 6000.
Completa 360,04 s por límite (proceso 1/lanzador 0), primer informe 11 a
80,98 s y último 11 a 357,24 s. Tarjeta procedural intacta, ninguna firma
buscada de error ni sondas de animación/flash-ready. Ventana visible, nunca
minimizada y sin primer plano según el monitor; sin captura propia ni solapamientos.

4.096 adquisiciones bajo último informe 11, todas `smode2=2227921` (`&3=1`),
512×448, fuente CRTC/`preferred=0`, hash válido. Primera secuencia/render
5877, hostTick=6000/frameTick=5999; última 9973, hostTick=10153/frameTick=10152.
Distancia de ticks 0..1, sin regresiones de secuencia/render, inconsistencias
de metadatos ni cambios de dimensión. Los 3.562 retornos A–B–A alternan
paridad y conservan modo de campos en las tres muestras; no hay retornos
de paridad igual, modos desconocidos u otros modos en esta ventana.

Esto refuerza la compatibilidad con composición por campos, pero no prueba
qué produjo el síntoma visual, igualdad de píxeles o ausencia de contenido
antiguo. El cupo termina antes del final de la partida; no cubre todo el
intervalo de seis minutos. No se usa como benchmark ni comparación con Opus.
[CI del HEAD documental `e3678cb` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38160245742).
Se inicia después otra pasada aislada VU1+frente GS, ventana desde 6000;
su resultado queda pendiente. Encuadre y comparación visual siguen abiertos.

## Control de 100 con ambos hilos y cierre del diagnóstico (2026-10-11)

Mismo runner `383c6b6`/SHA-256
`5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`,
asíncrona/compartida, VU1 y frente GS en hilo, ventana desde 6000. Completa
360,05 s por límite (proceso 1/lanzador 0), primer informe 11 a 80,92 s y
último 11 a 357,49 s. Tarjeta intacta; ninguna firma buscada de error ni
sondas de animación/flash-ready. Ventana visible, nunca minimizada/sin primer
plano según el monitor. No hay captura propia ni solapamientos.

4.096 adquisiciones bajo último informe 11, todas SMODE2=2227921 (`&3=1`),
512×448, fuente CRTC/hash válido. Primera secuencia/render 5958,
hostTick=6000/frameTick=5999; última 10053, hostTick=10119/frameTick=10118.
Distancia de ticks 0..1; cero regresiones/inconsistencias/cambios de dimensión.
3.542 A–B–A: todos alternan paridad y registran campos en las tres muestras.
No se reproduce la salida anticipada de 98, pero su causa permanece abierta;
dos repeticiones de seis minutos no prueban estabilidad prolongada.

Las tres pasadas de 100 completan el control funcional **del diagnóstico**:
sin hilos/ventana 0, VU1 solo/ventana 6000 y ambos/ventana 6000. El registro
queda acotado, conserva metadatos de la textura adquirida y funciona en partida.
La compilación completa y suite nativa/negativo/contratos constan arriba.
[CI del resultado VU1 `bae5d74` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38160854972).
Revisar también el CI del HEAD final antes de publicar #51 como lista/fusionarla.

Se precisa el criterio de cierre: #51 incorpora instrumentación opcional,
no una corrección visual. Las entradas anteriores que mantenían borrador por
comparación visual describían el trabajo pendiente entonces; la ausencia de
esa comparación sigue limitando las conclusiones sobre el juego y no invalida
el metadato probado con GPU real. Encuadre, contenido antiguo y equivalencia
con PCSX2 permanecen pendientes, sin declararlos resueltos por este cierre.
La siguiente comprobación requiere unos pocos píxeles de la textura realmente
adquirida, con metadatos de esa reserva, en una captura explícita separada:
la VRAM latched no basta y añadirla no debe cambiar silenciosamente el coste
del diagnóstico de #51. No convertir estas pasadas en mediciones de FPS.

## Captura explícita de la reserva adquirida: preparación de 101 (2026-10-11)

#51 se fusionó en `05421940617e5ce69889600d9eb727b3c36c0b3b` después de revisar
el cambio, cerrar las validaciones anteriores y comprobar [CI del HEAD final
`10edb76` verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38161351218).
Quedan abiertos encuadre, causa visual de los retornos y estabilidad prolongada.

Nuevo tema separado `ps2recomp-gs-host-acquired-capture.patch`, aplicado tras
los 100 anteriores. `GSHostPresentation` puede guardar unas pocas texturas
de la misma reserva que acaba de adquirir, con PNG RGBA y JSON de sus metadatos.
La opción está desactivada por defecto y es independiente de PRESENT_DIAG.
No consulta VRAM, vuelve a adquirir ni introduce glFinish global o cambios
en FINISH/fences/composición. **Activarla sí descarga píxeles y escribe archivos**:
usar otra pasada diagnóstica, nunca un benchmark o prueba de ausencia de cuelgues.

Variables leídas al construir el host: `PS2X_GS_ACQUIRED_CAPTURE_DIR` apunta
a una carpeta privada existente, `PS2X_GS_ACQUIRED_CAPTURE_FROM_TICK` fija
el primer hostTick inclusivo (ausente/inválido: 0), y
`PS2X_GS_ACQUIRED_CAPTURE_COUNT` limita intentos (por defecto 6, rango 1..16;
inválido desactiva). No captura secuencias retenidas, retrocesos, texturas vacías
o PMODE apagado; los fallos también consumen el cupo. Rechaza archivos existentes
y conserva la ruta Unicode en Windows. Un JSON completo acompaña a cada PNG
correcto; fallos se registran como `[gs-present:capture] status=failed`.

El lanzador añade `--capture-acquired 6` solo para casos compartidos, crea
`acquired/` dentro de la carpeta nueva de la pasada y utiliza `--diag-from-tick`
también para esa ventana. Con valor 0 no instala las variables de captura,
incluidas las heredadas. Conservar manifiesto/hash/guion/tarjetas y cerrar
una pasada antes de iniciar otra. Los PNG y JSON del juego permanecen privados.

Regresión procedural Windows/WGL pendiente de ejecución: publicar A,
adquirirla, modificar VRAM a B y exportar la reserva A. Comparar todos los
píxeles del PNG con A y luego publicar/exportar B; verificar metadatos, ventana,
retención, cupo y rechazo de sobrescritura. No aceptar fallback CPU ni un éxito
vacío en Linux. Sintaxis C++20 correcta, aplicación sobre base independiente
de 100 correcta, 17 pruebas del lanzador correctas, configuración 0 errores/
4 avisos existentes y sintaxis PowerShell 0 errores. Pendientes compilación
completa de 101, suite normal/GPU, negativo significativo/restauración, CI
y control del juego antes de atribuirle capturas o conclusiones visuales.

## Validación nativa de la captura adquirida de 101 (2026-10-11)

Compilación Windows completa de `394779a6dca0fd8dcaf7415e06ad048fdd9ad7f8`,
101 parches, salida 0 en 916,19 s; Release optimizado con símbolos privados,
VU1 intérprete y tres procesos de compilación. Runner de 54.552.576 bytes,
SHA-256 `63e6a576307675f34b70e4671a5b34de30219627a2ca2a3a362c182effd27040`.
Flags `/O2 /Ob2 /DNDEBUG /Zi` y `/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF`;
runner/símbolos congelados antes de incluir los controles privados.
[CI del mismo código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38161970774).
[CI del merge de #51 en main también correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38161755251).

Windows: 600/600 normal en 12,41 s, 636/636 GPU real en 30,98 s, sin fallos
conocidos. Los cuatro contratos FINISH/lectura/textura adquirida pasan en
procesos nuevos `default`/`async`; el segundo compara 16 texturas por modo,
todas las filas y reutilización de ranuras. La nueva regresión verifica PNG
RGBA completo de A después de cambiar VRAM a B, metadatos de esa reserva y
posterior PNG de B, además de ventana/cupo/retenciones/no sobrescritura.
La diferencia CPU/GPU en modo cuadro descrita en 100 permanece abierta.

Negativo privado compatible: sustituir únicamente LoadImageFromTexture por
una imagen sintética magenta del mismo tamaño en el exportador. Recompilar
solo las pruebas; runner sin cambios y ninguna partida. Compila y ejecuta
636 casos: **635 pasan y falla exactamente «Captura de reserva adquirida
tras cambiar VRAM»**, por píxeles distintos. Restaurar el archivo positivo
completo y recompilar: GPU vuelve a 636/636 en 31,84 s. Fuente restaurada
idéntica al respaldo; runner actual y copia congelada conservan el mismo hash.

Se inicia después una pasada aislada del juego asíncrona/compartida sin hilos,
ventana desde 1500 y seis capturas; todavía no hay resultado funcional ni
conclusión visual de 101. Esa ventana difiere de la anterior desde 0 para
reservar las capturas para partida. Después comparar otra ventana con VU1
en hilo desde 6000, sin solapar partidas/compilación/emulador. PNG/JSON y
análisis de píxeles permanecen privados; no usar los tiempos como FPS.

## Primeras capturas de 101: procedencia y limitación de ventana (2026-10-11)

Runner `394779a`/SHA-256
`63e6a576307675f34b70e4671a5b34de30219627a2ca2a3a362c182effd27040`,
asíncrona/compartida sin hilos, seis capturas desde hostTick=1500. Completa
360,07 s por límite (proceso 1/lanzador 0), primer informe 11 a 131,78 s,
último 11; tarjeta intacta, ninguna firma buscada de error ni sondas antiguas.
**El monitor registra minimización entre 44,22 y 156,01 s**, con primer plano
antes/después; no se conoce su causa y no hubo entrada automatizada de Computer
Use. No presentar la pasada como condición de ventana constante. Las capturas
son posteriores a la restauración, bajo último informe 11; se repetirá el caso.

643 adquisiciones desde 1500, todas SMODE2=2227921 (`&3=1`), 512×448/fuente
CRTC/hash válido, sin regresiones/inconsistencias/A–B–A/cambios de dimensión,
distancia de ticks 0. Se guardan seis PNG RGBA y seis JSON de secuencias
486..491/ticks 1500..1505; cada campo coincide con su adquisición en el log.
Los seis contenidos RGBA son distintos entre sí; los 224 pares adyacentes
de filas son idénticos dentro de cada imagen, conforme al compositor de campos.
La revisión de las primeras dos muestra Kratos/cubierta/lluvia, sin alternar
menú y partida en esas muestras. No demuestra ausencia de contenido antiguo
en otras ventanas, escena correcta, equivalencia PCSX2 o encuadre resuelto.

## VU1 de 101: A–B–A confirmado en píxeles adquiridos (2026-10-11)

Mismo runner, asíncrona/compartida, VU1 en hilo y frente GS 0, seis capturas
desde hostTick=6000. Completa 360,05 s por límite, primer informe 11 a 80,83 s
y último 11 a 358,22 s; tarjeta intacta, sin firmas buscadas de error ni sondas
antiguas. Ventana visible/no minimizada/sin primer plano según el monitor.
4.096 adquisiciones bajo último informe 11, todas SMODE2=2227921 (`&3=1`),
512×448/fuente CRTC/hash válido; sin regresiones/inconsistencias/cambios de
dimensión, distancia de ticks 0..2. Los 3.204 A–B–A alternan paridad y tienen
modo de campos en las tres muestras. El cupo no cubre todo el intervalo.

Seis PNG/JSON completos de secuencias 5806..5811, ticks de textura
5999, 6000, 6001, 6003, 6005, 6006. Metadatos correlacionados exactamente
contra los registros de esas adquisiciones; 224/224 pares de filas duplicados
en cada PNG. Las texturas 5806 y 5808, de paridad impar, son **idénticas en
todos sus bytes RGBA**, y 5807 (par) es distinta: A–B–A real en esta terna,
no solo coincidencia del hash GPU. También 5809 y 5810 son idénticas entre sí,
ambas impares. Las demás parejas son distintas; no extrapolar esa igualdad
a otras texturas o al renderer CPU del juego.

La revisión visual de 5806/5807 muestra la misma escena/cámara de Kratos en
cubierta, con campos distintos. Esto sostiene la compatibilidad del retorno
con composición por campos, **sin demostrar la frescura de VRAM ni la causa
del síntoma original**. No se observa un menú alternante en estas imágenes;
no se descarta fuera de las seis capturas. El encuadre no se compara con una
referencia alineada. Las ventanas 1500 y 6000 de las dos pasadas tampoco
representan automáticamente la misma pose/instante: no comparar sus cámaras
como evidencia de diferencia causada por los hilos.

[CI del resultado nativo `ce23d73` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38163566080).
Se inicia otra pasada sin hilos para repetir la condición de ventana. #60
permanece borrador durante ese control y revisión del CI del HEAD final.
Todos los píxeles, JSON y análisis permanecen privados; no se modifican las
matemáticas de cámara ni se publican como benchmark o solución del fallo.

## Repetición sin hilos y cierre del control de captura (2026-10-11)

Mismo runner `394779a` y hash anterior, asíncrona/compartida, VU1/frente GS
desactivados, seis capturas desde 1500. Completa 360,04 s por límite (proceso
1/lanzador 0), primer informe 11 a 116,48 s y último 11. Tarjeta intacta,
ninguna firma buscada de error ni sondas antiguas. El monitor no registra
minimización; pasa a primer plano al final. Esto no borra la minimización
de la pasada anterior ni identifica su causa.

870 adquisiciones bajo último informe 11, todas SMODE2=2227921 (`&3=1`),
512×448/fuente CRTC/hash válido, distancia de ticks 0. Cero regresiones,
inconsistencias, A–B–A o cambios de dimensión en esa ventana. Seis PNG RGBA
y seis JSON de secuencias 1462..1467/ticks 1500..1505, todos sus metadatos
coinciden con el registro adquirido. Los seis contenidos son distintos entre
sí y cada imagen duplica 224/224 pares adyacentes de filas. Las primeras dos
imágenes revisadas muestran Kratos en cubierta y la misma vista dentro del par.
No compararlas con poses/cámaras de otra pasada por compartir el tick 1500:
el guion usa tiempo anfitrión y ese tick no alinea el estado del juego.

Las tres pasadas de 101, compilación completa, suite normal/GPU, negativo
restaurado y contratos cierran la validación funcional de esta **captura
opcional**. Revisión manual: la llamada usa el Frame de Acquire mientras
su reserva sigue vigente; sin opción no hay readback ni E/S de captura.
[CI de `9332c1b` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38164770068).
Antes de fusionar #60, comprobar también el CI del HEAD que incorpore este
resultado. Encuadre, frescura del contenido, discrepancia en modo cuadro y
estabilidad prolongada siguen abiertos; no se cambian matemáticas ni se
declara solucionado el síntoma original. PNG/JSON/análisis quedan privados.
