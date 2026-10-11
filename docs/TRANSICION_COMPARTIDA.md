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
