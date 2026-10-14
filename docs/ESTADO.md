# Estado del proyecto

_Última actualización: 9 de octubre de 2026_

## Qué funciona

- La compilación completa termina sin errores (~11 min de compilación del juego en un equipo de 8+ núcleos).
- `scripts\2_recompilar_rapido.cmd` recompila solo `src/gow_overrides.cpp` en ~1 minuto.
- `ps2EntryRunner.exe` arranca, inicializa raylib 5.5 / OpenGL 3.3 y abre la ventana (640×448).
- **El IOP ejecuta los IRX originales del juego** en el intérprete R3000A de `ps2xIOP`: `sio2man`,
  `dbcman`, `sio2d`, `mc2_d`, `ds2u_d`, `libsd`, `989nomid` (989snd) y **`smpd_iop`** (el cargador de
  datos). `scripts\ejecutar.ps1` los copia a `IOP_MOD\` junto al ELF la primera vez.
- El IOP lee la **ISO original** (`God of War.iso` junto a la carpeta del ELF, o la variable `GOW_ISO`).
- smpd se inicializa: lee el directorio ISO9660 y `GODOFWAR.TOC`, y crea sus hilos.
- **La configuración se carga desde el disco**: la petición `SMPD 0xF "R_Perm"` devuelve el handle 0
  y `HERO_HEAP_SIZE` / `SLOT_HEAP_SIZE` / `UPGRADE_HEAP_SIZE` se encuentran (la guardia provisional del
  diccionario NULL dejó de saltar y se ha retirado).
- **`R_PERM.WAD` se carga entero en streaming** (smpd lee 16 sectores por llamada a `sceCdRead` y los
  envía al EE en bloques de `0x20000` que se alternan entre `0x530640` y `0x550640`).
- 989snd arranca sin errores (antes, 15 × `cause 7` por un `argv` mal construido).
- **El juego entra en su bucle principal (`sys::GameLoop`) y dibuja sus primeras pantallas**: la pantalla
  legal *"Sony Computer Entertainment America presents"*, la de *"Please insert a DUALSHOCK®2…"* y el logo
  de *God of War* de la pantalla de título. A algunos textos les faltan letras.
- El depurador del runtime se muestra/oculta con **F1**.
- El HLE de **libpad2** conecta el primer puerto al backend del runtime: teclado o gamepad, botones,
  ambos sticks y presiones digitales. La navegación llega al **menú principal y la selección de dificultad**.
  Los controles y la prueba reproducible están en [CONTROLES.md](CONTROLES.md).
- El dispatcher conserva los checkpoints que ceden en la entrada de una función. Esto elimina la copia
  parcial de `Animation/goHero` y la llamada virtual a NULL al avanzar desde el menú.
- `gowIpuInit` corrige la inicialización del IPU: el stub genérico saltaba a una dirección de otro
  ejecutable y ejecutaba `FilteredCopyTile` con registros incorrectos. Ahora se alcanza la carga del FMV.
- Con `GOW_SKIP_FMV=1` se alcanza el **estado 11 de partida**, con `pending=0` y el hilo principal activo.
  Tras integrar los arreglos de Claude, el control OpenGL de 330 segundos carga una copia de su tarjeta
  y muestra a Kratos, el escenario, el HUD y la indicación R2 en «Docks of Athens». El rendimiento sigue
  bajo; faltan controles de combate, otros escenarios y rendimiento sostenido.

## Símbolos

Los mapas de nombres de `01-retail-usa-SCUS97399/` (exportación de Ghidra, casados con la demo del E3
y la demo europea) **coinciden con las direcciones de nuestro ELF**. Algunas confirmadas:

| Dirección | Nombre |
|---|---|
| `0x0026BF28` | `snd_SendIOPCommandAndWait` |
| `0x0026C940` | `snd_DoExternCall` |
| `0x00186710` | `wadContext::FindData` |
| `0x00175890` | `stdDynaStringDB::GetDynaStringNode` |
| `0x0017A940` | `sys::Boot2` |
| `0x00239F90` | `stdCList<wadCleanupData, …>` |
| `0x00299D08` | `cbTimerHandler` (alarmas de libkernel) |
| `0x0016AFD8` | `memcpy_asm` |
| `0x0016B120` | `ringBuffer::GetBytes` |
| `0x00183878` | `vid::WaitForDMAComplete` |

`GOW-Port/sym.py <direcciones>` (fuera del repositorio) traduce direcciones con estos mapas.

## Cronología de la investigación del arranque

### 1. El cuelgue en `0x00176A80` no era una espera

`sub_001769F8(raiz, clave)` es la búsqueda en un **árbol binario** de nodos de 16 bytes guardados en un
pool (creado por `sub_00175CD0`, 8000 nodos):

| Campo | Significado |
|---|---|
| `nodo+0x0` | clave (hash) |
| `nodo+0x4` | valor |
| `nodo+0xA` / `nodo+0xC` | hijo izquierdo / derecho (índice u16 en el pool) |
| `*0x29C4BC` | base del pool (`hijo = base + índice*16`) |
| `*0x29C4B4` | nodo centinela (fin de rama) |
| `*0x29C4B8` | handle del pool |

`GetDynaStringNode` (`0x175890`) recibía un diccionario **NULL** desde `wadContext::FindData`
(`0x186710`), que buscaba `HERO_HEAP_SIZE`, `SLOT_HEAP_SIZE` y `UPGRADE_HEAP_SIZE`. La raíz se leía en
la dirección `0x4` y esa basura formaba un ciclo. El diccionario es `nodo+0x4C` del contexto del WAD
`R_Perm`, que no se había cargado.

### 2. La configuración vive en `R_PERM.WAD` y la carga smpd

`R_PERM.WAD` es el primer archivo de `PART1.PAK` (sector 0, `0x378530` bytes). Las variables son
registros de tipo `0x18` del grupo `WAD_R_Perm`:

| Variable | Valor |
|---|---|
| `HERO_HEAP_SIZE` | `0x1EC800` |
| `SLOT_HEAP_SIZE` | `0x100000` |
| `UPGRADE_HEAP_SIZE` | `0x14BC00` |

`GODOFWAR.TOC` es una tabla de entradas de 24 bytes con el formato `nombre[12]`, `u32`, `u32 tamaño`,
`u32 sector`.

El EE no lee el disco: lo hace **`SMPD_IOP.IRX`** ("smpd file streamer"), un plugin de 989snd.

### 3. Protocolo EE ↔ 989snd ↔ smpd

`snd_SendIOPCommandAndWait(cmd, tamaño, datos)` hace `sceSifCallRpc(sid 0x123456, rpc = cmd)`: envía
`tamaño` bytes desde `0x305640` y recibe 12 bytes en `0x305600`. Devuelve la palabra 1 de la respuesta.

El comando **`0x68`** es un mensaje para un plugin: `{u32 plugin, u32 tipo, u32 len, u32 ptr}` más `len`
bytes copiados de `ptr`. Para smpd, `plugin = 0x534D5044` (`'SMPD'`).

| # | cmd | Contenido | Significado | Respuesta |
|---|---|---|---|---|
| 1 | `0x0` | `{0x30A1C0, 0}` | inicialización de 989snd | 0 |
| 2 | `0x68` SMPD `1` | `{modo, estado, 0x20, tabla, 0x280, …}` | inicializar smpd (búferes del EE, ver abajo) | 0 |
| 3–17 | `0xA` | `{0}` … `{14}` | 15 comandos de 989snd | `0x400` |
| 18 | `0x68` SMPD `0xF` | `"R_Perm"` + `0x10000210, 0, 1` | **cargar el WAD `R_Perm`** (flag `0x200` = síncrono) | handle `0` |
| 19… | `0x4C` | 0x1C bytes | `snd_DoExternCall` | `0x20000` |

Búferes de smpd en el EE (`sub_001389D8`, la inicialización): `*0x29BE4C` = estado (0x20 bytes) y
`*0x29BE50` = tabla (0x280 bytes). Se acceden sin caché (`| 0x20000000`) porque smpd los escribe por
DMA (`sceSifSetDma`). La tabla tiene una cabecera de 0x100 bytes y 8 ranuras de 0x30 bytes. En la carga
síncrona, `sub_0017AD70` lee `tabla + 0x12C + handle*0x30` (campo `+0x2C` de la ranura) y
`sub_00185F28` lo guarda en `nodo+0x4C`.

### 4. Ejecutar los IRX originales en lugar de reimplementarlos

`ps2xIOP` está diseñado para ejecutar los IRX del juego ("Game-specific IOP code executes from IRX
modules"). No se cargaban solo porque el juego los pide como `IOP_MOD/xxx.irx`. Una vez disponibles
aparecieron cuatro fallos del emulador, todos corregidos en `patches/ps2recomp-runtime.patch`:

| Fallo | Síntoma | Arreglo |
|---|---|---|
| Heap del IOP de solo 832 KB (`HeapBase = 0x120000`) | smpd: "failed to allocate memory" (pide `0x11ADF8` bytes) | `HeapBase = 0x70000` (los módulos acaban hacia `0x56000`) |
| Tope de 2 M instrucciones por llamada síncrona (`kMaxCallInstructions`) | la inicialización de smpd, ejecutada dentro del RPC de 989snd, se cortaba **en silencio** a mitad del TOC y el EE esperaba para siempre | tope de 400 M y aviso `[IOP] guest call … exceeded its budget` |
| `sprintf`/`vsprintf` de sysclib copiaban el formato literal | smpd hacía `sprintf("%s.wad", nombre)` y buscaba `%S.WAD` → `R_Perm` devolvía -1 | formateador real con argumentos o32 (`iop_format.h`), usado también por `printf` |
| Sin imagen de disco | smpd lee por número de sector | `configureGowCdImage` en `src/gow_overrides.cpp` usa la ISO original |

### 5. Métodos virtuales sin punto de entrada

`sub_0023A000` hace una llamada virtual a `0x239FD0`, un método vacío (`jr ra`) al que solo se llega
por vtable. Ni nuestro mapa de funciones ni Ghidra lo tenían como función: quedaba dentro de
`sub_00239F90` y el runtime lo daba por inexistente (`[guest-branch:missing-target]`). Recorriendo las
vtables del ELF (entradas `{s16 ajuste, s16 índice, u32 función}`) salieron **24 destinos** así. Ahora
están en `entry_points` de `config/recomp.template.toml` (`vfunc_XXXXXXXX`), y el recompilador les crea
un punto de entrada dentro de la función que los contiene.

### 6. El hilo principal moría: una copia de 205 MB

Con lo anterior, el juego avanzaba hasta que **el hilo principal moría** (`Dormant`, `pc=0`) por una
llamada a un callback NULL en `cbTimerHandler`. La lista de alarmas de libkernel (`0x2A5B08`–`0x2A5B14`)
aparecía llena de datos de `R_PERM.WAD`. El culpable era
`memcpy_asm(dst=0x6C190, src=0x530640, n=0xC478000)`, llamado desde `ringBuffer::GetBytes` por el
cargador de WAD: el búfer de streaming estaba **a cero**, así que el cargador leía un tamaño basura.

Los datos sí llegaban por `sceSifSetDma`, pero **al búfer de la petición siguiente**. En el IOP real,
el servidor RPC de 989snd despierta al hilo lector de smpd (de más prioridad), que se ejecuta y apunta
el destino antes de que el hilo RPC conteste. El emulador ejecutaba el servidor RPC como una llamada
síncrona y contestaba en el acto, así que el hilo lector tomaba el destino de la petición siguiente.
**Arreglo:** tras cada RPC, el emulador deja correr a los hilos del IOP que estén listos
(`runReadyThreads`, tope de 4 M ciclos) antes de copiar la respuesta.

### 7. La espera de vídeo: la interrupción del GS

`vid::WaitForDMAComplete` (`0x183878`) espera mientras `*0x29C7D0 == 1`. La bandera la pone a 1 quien lanza
la cadena DMA (`svrEpilogue::FlipAndKick`, `fxCameraFilter`…) y solo la cambia el **manejador de la
interrupción del GS** (`0x182DD8`, código que Ghidra no marcó como función): lee `GS_CSR`, y si `FINISH`
está activo pone la bandera a 2 y borra el bit. El runtime marcaba `CSR.FINISH` pero **nunca lanzaba la
interrupción INTC 0**. Arreglo: `EeScheduler::pollGsInterrupt()` la lanza en el flanco de subida de
`CSR.SIGNAL/FINISH` no enmascarados en `IMR`.

### 8. Las interrupciones pisaban la pila del hilo principal

Con la interrupción del GS activa, el hilo principal moría saltando a `0xF82`. Las pilas de los manejadores
de interrupción se reservan desde lo alto de la RAM (`0x1FFFFF0`) hacia abajo, **justo donde
`SetupThread` coloca la pila del hilo principal**: cada interrupción machacaba sus marcos más antiguos.
Arreglo: `SetupThread` baja el techo de esa zona por debajo de la pila principal
(`PS2Runtime::limitAsyncCallbackStackTop`; ahora empiezan en `0x1FE8000`).

### 9. Otros arreglos del emulador del IOP

- Los módulos arrancaban con `start(tamaño, texto)` en lugar de `start(argc, argv)`; ahora `argv` es
  `[ruta, arg1, …, NULL]`, como en `loadcore`.

Además, el override de `snd_SendIOPCommandAndWait` ya no descarta los comandos: los registra (`[gow-snd]`)
y llama al original. `GOW_SND_STUB=1` recupera el comportamiento antiguo.

## Herramientas de diagnóstico

| Herramienta | Uso |
|---|---|
| `[gow-snd]` | comandos enviados a 989snd/smpd y su respuesta |
| `PS2X_IOP_TRACE=N` (+ `PS2X_IOP_TRACE_FROM=M`) | registra N llamadas a importaciones del IOP a partir de la M |
| `PS2X_IOP_TRACE_NOCLIB=1` | omite `sysclib` en esa traza |
| `PS2X_IOP_TRACE_EVERY=N` | muestreo: una de cada N llamadas |
| `PS2X_IOP_PC_EVERY=N` | PC del IOP cada N instrucciones y aviso cuando no hay hilos listos |
| `PS2X_IOP_TRACE_DMA=1` | cada transferencia `sceSifSetDma` IOP → EE (origen, destino, tamaño, primeros bytes) |
| `[run:thread]` | estado de todos los hilos del EE cada ~10 s (en `ejecutar.log`) |

Los diagnósticos provisionales de las secciones 1, 5 y 6 (`[gow-tree]`, `[gow-dict]`, `[gow-23a000]`,
`[gow-timer]`, `[gow-watch]`) se retiraron de `src/gow_overrides.cpp` una vez resueltos esos problemas;
siguen en el historial de git por si hiciera falta recuperarlos.

## Problemas conocidos

| Problema | Detalle |
|---|---|
| Texto con letras de menos | algunas fuentes/texturas se dibujan incompletas (pantalla del mando, menú) |
| Imagen de partida | se alcanza el estado 11 con los FMV omitidos, pero el framebuffer queda negro |
| Mando | libpad2 funciona por HLE para el primer puerto; presiones 0/255 y sin vibración. En el SIO2 emulado los puertos de mando se ven vacíos |
| Memory card sin verificar | el SIO2 y la tarjeta están emulados (`ps2recomp-sio2.patch`, archivo `Mcd001.ps2`), pero falta probarlo con el juego |
| Audio sin verificar | el SPU2 emulado ya sale por el audio del PC (`ps2recomp-spu2-output.patch`), pero falta probarlo con el juego; sin reverb ni ADMA |
| Sin vídeo FMV | `sceMpeg*` / `sceIpu*` son stubs |
| Dependencias de ninja | Con MSVC en español no se registran las dependencias `/showIncludes`: `2_recompilar_rapido.cmd` toca el archivo unity de los overrides y `compilar.ps1` borra los objetos unity tras regenerar (los cambios en cabeceras del runtime requieren tocar los `.cpp` que las incluyen) |
| Rutas con acentos | `ps2_recomp` no abre rutas no ASCII: `compilar.ps1` copia el ELF y el mapa a la carpeta de trabajo |

## Próximos pasos

1. Investigar VIF/VU→GIF: en el estado 11 los contextos del GS siguen con `FBP=0`, sin primitivas
   en la traza reciente, mientras se presenta `FBP=208`. XGKICK rechaza paquetes por exceder
   su búfer o por el formato de la cabecera. Los avisos `0xFFFFFFFB` y `0xFFFFFFF8` son códigos
   internos del runtime, no instrucciones inválidas del microcódigo. `GOW_RENDER_DIAG=1`
   permite guardar el microcódigo y la RAM para compararlos.
   La recompilación completa reproduce el problema. Una traza temporal de XGKICK confirmó
   paquetes correctos al principio y luego datos de vértices interpretados como cabeceras
   (`source=0x2AB0`, segunda cabecera en `offset=0x10`). Revisar preparación/rotación de buffers
   y UNPACK antes de ampliar el búfer del runtime.
   Se probó por separado la transferencia inmediata usada por SOCOM Unzipped:
   `GOW_XGKICK_IMMEDIATE=1` alcanza el estado 11, pero no resuelve la imagen negra y siguen
   los errores XGKICK. El parche queda opcional; la transferencia por ciclos sigue siendo el valor
   por defecto. Ver la comparación en [CONTROLES.md](CONTROLES.md#prueba-aislada-de-xgkick).
   La comprobación del bit I registró **196608 comandos y cero solicitudes de interrupción VIF1**
   hasta el estado 11 (FMV omitidos, entrada rápida). El mecanismo de pausa/reanudación de
   [SOCOM Unzipped](https://github.com/Scotho/socom-unzipped/blob/main/third_party/ps2recomp/ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp#L15-L20)
   no explica el fallo observado en esa prueba. No se ha habilitado.
   `-DiagnosticoVif` compara las cabeceras antes de MSCAL y durante XGKICK, distingue los paquetes
   vacíos de los rechazos reales y guarda los buffers de dibujo al alcanzar la partida.
   Los contextos FBP=0 tienen datos, pero sus capturas no muestran una escena 3D; FBP=208 sigue
   negro. La traza de escrituras encontró bucles de transformación que recorrían los buffers y
   sobrescribían sus cabeceras. `ps2recomp-vu-jump.patch` corrige la lectura de JR/JALR: usaban
   el valor anterior de VI, aunque ese retraso corresponde a las comparaciones de ramas condicionales.
   Con el arreglo, la prueba de 140 s llegó al estado 11 y registró 212992 comandos VIF sin
   rechazos XGKICK ni instrucciones VU reservadas. La pantalla de partida todavía es negra y los
   contextos de dibujo no muestran una escena 3D. La siguiente comprobación es la transformación
   de coordenadas y la interpretación de los registros/paquetes GIF ahora que sus cabeceras se conservan.
   Ver [CONTROLES.md](CONTROLES.md#diagnóstico-de-vif-y-buffers-de-dibujo).
2. Resolver la espera de MPEG (`sceMpegGetPicture`, `0x0018A3D8`) y corregir el renderizado de
   fuentes/3D. `GOW_SKIP_FMV=1` permite investigar la partida mientras el decodificador está pendiente.
   Hipótesis en curso (`ps2recomp-mpeg-nodata.patch`): el stub de `sceMpegGetPicture` esperaba
   fotogramas sin llamar nunca al callback `sceMpegCbNodata` que el juego registra con
   `sceMpegAddCallback`. En la libmpeg original ese callback es el que lee del anillo y llama a
   `sceMpegDemuxPssRing`, así que nadie alimentaba al decodificador (bloqueo mutuo). El parche lo llama
   en el hilo que pide la imagen; si no aporta datos, reintenta en el siguiente VSync. Dos pruebas de
   regresión lo cubren (sin el parche la suite se cuelga en la primera). **Falta comprobarlo con el
   juego:** ejecutar sin `GOW_SKIP_FMV` y buscar en `ejecutar_err.log` las líneas
   `[MPEG:AddCallback] ... type=1` (el juego registra el callback) y si la espera desaparece.
3. Memory card (SIO2 / `MC2_D.IRX`). **`ps2recomp-sio2.patch`:** antes los registros del SIO2
   (0x1F808200-0x1F8082FF) eran simples latches, `dmacman` no hacía nada y no había interrupción 17, así que
   `sio2man` esperaba para siempre cada transferencia. Ahora el IOP emula el SIO2: cola de comandos (SEND3),
   FIFO de entrada y salida, DMA 11/12 (`sceSetSliceDMA`/`sceStartDMA` de `dmacman` para esos dos canales),
   `RECV1-3`, `ISTAT` e IRQ 17 al poner `CTRL` bit 0. Los comandos de mando y multitap responden como puerto
   vacío (libpad2 sigue por HLE en el EE). La memory card implementa el protocolo de PS2 de PCSX2 (sondeo,
   páginas de 512 + 16 bytes, borrado de bloques de 16 páginas, lectura/escritura, terminador, especificaciones
   y los pasos de autenticación sin cifrado); `SecrAuthCard` de secrman devuelve éxito. Las páginas se guardan
   en bruto en `Mcd001.ps2` junto al ELF, el mismo formato de 8 MB que usa PCSX2 (se pueden intercambiar
   partidas). Si el archivo no existe, la tarjeta empieza sin formatear y se crea en la primera escritura.
   Variables: `GOW_MC0=<ruta>` (otra tarjeta; `GOW_MC0=0` deja el puerto vacío), `GOW_MC1=<ruta>` (segundo
   puerto, vacío por defecto), `GOW_SIO2_DIAG=1` (registra cada comando de tarjeta como `[SIO2] mc0 cmd=0x..`)
   y `GOW_SIO2=0` (vuelve al comportamiento anterior). El protocolo se probó contra `mcman` de ps2sdk; God of
   War usa `MC2_D.IRX` de Sony, que no se ha podido revisar. **Falta comprobarlo con el juego:** si al guardar o
   al arrancar aparece el aviso de tarjeta sin formatear, si el formateo y el guardado terminan, y si
   `Mcd001.ps2` se puede abrir en PCSX2.
4. Audio sobre 989snd / SPU2. **Fase 1 (`ps2recomp-spu2.patch`):** el IOP emula el SPU2: 2 MB de RAM de
   sonido, registros de 16 bits de los dos núcleos, puerto de datos manual, DMA de los canales 4 y 7 que ahora
   copia los datos (antes solo marcaba la transferencia como hecha; el bit `STATX` 0x80 y la interrupción
   diferida no cambian), voces ADPCM con tono, ADSR, volumen fijo y mezcla seca, `ENDX`/`ENVX`/`NAX` legibles
   e IRQ 9 al alcanzar `IRQA`. `GOW_SPU2_IRQ=0` desactiva esa IRQ por si el juego cambia de comportamiento.
   **Fase 2 (`ps2recomp-spu2-output.patch`):** la mezcla de 48 kHz del SPU2 sale por un `AudioStream` de raylib
   (estéreo, 16 bits). El IOP produce las muestras en el hilo del EE y el hilo de audio las consume con un mutex;
   si se acumulan más de 100 ms se descartan las más antiguas, y si faltan se rellena con silencio (si la
   emulación va más lenta que el tiempo real se oirán cortes). `runCycles` avanza el SPU2 hasta el último ciclo
   ejecutado aunque el IOP esté en espera. `GOW_AUDIO=0` desactiva la salida (el SPU2 sigue emulándose). En el
   registro aparece `[SPU2] salida de audio a 48 kHz activa`. Sin verificar con el juego: hay que comprobar si se
   oye la música del menú y si suena a la velocidad correcta.
   Falta: reverb, barrido de volumen, ADMA (PCM en streaming) e interpolación gaussiana.

## Auditoría del intérprete de VU1

Se revisaron, instrucción por instrucción, las unidades superior e inferior de VU1
(`ps2_vu1_upper.cpp`, `ps2_vu1_lower.cpp`) contra la especificación y contra PCSX2 (`VUops.cpp`):
FMAC con broadcast, acumulador y `OPMULA`/`OPMSUB`, `CLIP`, `FTOI`/`ITOF`, `DIV`/`SQRT`/`RSQRT` (con la
latencia de `Q`), flags (`FSAND`, `FMAND`, `FCAND`…), ramas y enlaces, cargas y almacenamientos con
autoincremento, `MTIR`/`MFIR`, el generador `R` y la EFU. La única discrepancia fue una errata en un
coeficiente de la serie de `EATAN` (`-0.1308…` en vez de `-0.1390…`), corregida en
`ps2recomp-vu-efu.patch` con su prueba. La semántica de esas instrucciones no explica la imagen negra
de la partida. Fuera de esta revisión quedan las latencias por instrucción y los riesgos del pipeline.

## Validación del avance al menú

- `scripts\probar_pad2.cmd`: pasa la prueba de bytes de botones, sticks y las doce presiones.
- Compilación de `ps2EntryRunner` y `ps2x_tests`: correcta.
- Suite del runtime desde su directorio raíz: **435/437**. Fallan dos pruebas previas:
  `setup heap and allocator primitives track end-of-heap` y
  `IOP heap DMA uses private backing instead of aliasing EE RDRAM`.
- Al retirar únicamente la corrección del dispatcher, la nueva prueba de checkpoint falla y el total
  baja a **434/437**; los mismos dos fallos de heap/DMA permanecen.
- `ps2recomp-checkpoint.patch` se comprobó sobre la revisión fijada más el parche original del proyecto.
- La prueba de 200 segundos con el IPU corregido llegó a elegir dificultad y después esperó en MPEG.
  Las capturas y registros son evidencia del menú, **no de una partida jugable**.
- La prueba de 600 segundos omitiendo FMV llegó al estado 11 sin omitir la animación de entrada,
  con `pending=0`; las capturas de partida son negras. `GOW_FAST_BOOT=1` repite la transición
  en aproximadamente 70 s desde la primera lectura del mando.
- Con el parche experimental XGKICK, la suite completa da **436/438**: pasa la nueva prueba
  de sobrescritura del buffer y permanecen los mismos dos fallos previos de heap/DMA.
- `ps2recomp-xgkick.patch` aplica correctamente sobre la revisión fijada del runtime.
- `ps2recomp-vif-unpack.patch` corrige la expansión XYXY de V2 y los bits de color/alfa V4-5.
  Sus tres regresiones fallan antes del arreglo (**436/441**) y pasan después (**439/441**),
  manteniendo los dos fallos previos. El parche aplica sobre la revisión fijada.
  La prueba del juego alcanza el estado 11; los defectos del menú y la imagen negra permanecen.

- `ps2recomp-vu-jump.patch` conserva el valor actual de VI para JR/JALR, incluido el caso en que
  JALR reutiliza el registro de destino para su enlace. Mantiene la ranura de retardo y las reglas
  existentes de ramas condicionales. Se sustituyó una expectativa incorrecta del test JR y se añadió
  cobertura JALR: ambas regresiones fallan antes (**438/442**) y pasan después (**440/442**),
  con los mismos dos fallos conocidos. El parche aplica sobre la revisión fijada más los anteriores.
  El ejecutable se compiló y la prueba de 140 s confirmó el avance al estado 11 sin rechazos GIF
  ni instrucciones VU reservadas; las capturas siguen sin acreditar una partida jugable.

- Al integrar los cambios publicados en paralelo de `ps2recomp-heap.patch`, la suite pasa
  **443/443**. El heap privado conserva el rango de God of War y la prueba de DMA usa el límite
  real del heap del IOP. Los resultados 440/442 anteriores corresponden al árbol previo a esa integración.
- Se corrigió la recompilación completa repetida: `checkout -f` no eliminaba `iop_format.h`, creado
  por el parche del runtime, y `git apply` fallaba con `already exists in working directory`.
  `compilar.ps1` retira ahora ese archivo junto a `gow_stub_services.cpp` antes de reaplicar los parches.

- La compilación completa (`scripts\2_compilar.cmd`) regeneró las 6418 unidades del juego,
  reaplicó los siete parches y enlazó `ps2EntryRunner.exe` correctamente. Se verificó por separado
  que los siete parches aplican, en ese orden, sobre la revisión fijada y producen las fuentes usadas
  por la compilación. Tras reconstruir, la suite sigue pasando **443/443**; la configuración tiene
  cero errores (cuatro avisos conocidos), los ocho scripts PowerShell se analizan sin errores y la
  prueba independiente de libpad2 pasa.

- Con el ejecutable completo y el heap integrado, la segunda prueba de 140 s volvió a alcanzar
  el estado 11 en 70,32 s desde la lectura del mando. Registró 217088 comandos VIF1 sin rechazos
  XGKICK ni instrucciones VU reservadas. Las capturas de 90 y 110 s son negras; los contextos de
  dibujo conservan el fondo oscuro. La partida todavía no es jugable.

## Investigación del renderizado 3D: raíces de COP1 (5 de octubre de 2026)

El diagnóstico local de VU1 en el estado 11 encontró una matriz de transformación que ya llegaba
con `NaN` en sus componentes XYZ. Después de transformarse, los paquetes de geometría contenían
coordenadas XYZ nulas y el bit ADC de descarte activado. Los registros del GS mostraban las copias
entre buffers, pero no acreditaban que los triángulos de la partida se dibujaran correctamente.

Al seguir los cálculos de cámara se identificaron dos errores del recompilador COP1:

- `SQRT.S` del R5900 obtiene el radicando de **FT**, pero se emitía una lectura de FS. Con FS=0,
  las funciones de cámara calculaban raíces del registro F0 en lugar de sus sumas de cuadrados.
- `RSQRT.S` calcula **FS / sqrt(FT)**; se emitía **1 / sqrt(FS)**, perdiendo ambos operandos.

`ps2recomp-fpu-roots.patch` corrige la selección de operandos y añade dos regresiones que decodifican
las instrucciones binarias y comprueban el código emitido, incluyendo destinos que coinciden con
las fuentes. Ambas fallan antes del arreglo (**443/445**) y pasan después (**445/445**, antes de
integrar las nuevas pruebas EFU/MPEG). Referencia de contraste:
[implementación COP1 de PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/FPU.cpp#L316-L344).
Este parche no implementa todavía las particularidades de flags y saturación de la FPU del R5900.

Una prueba separada inicializando `VF0.w=1` en los hilos EE nuevos no eliminó los `NaN` de la matriz.
Se retiró ese cambio experimental. Los volcados de memoria, microcódigo y capturas usados para
este diagnóstico permanecen locales en las carpetas ignoradas; no forman parte del parche.

La reconstrucción completa con los diez parches regeneró las 6418 unidades y enlazó correctamente
el ejecutable. La suite integrada, incluidas las pruebas EFU/MPEG publicadas en paralelo, pasa
**448/448**. Los diez parches aplican en orden sobre la revisión fijada; configuración y handlers
sin errores (cuatro avisos conocidos), ocho scripts PowerShell sin errores de sintaxis y prueba
independiente de libpad2 correcta.

Dos ejecuciones de 140 y 165 segundos con `GOW_SKIP_FMV=1` y `GOW_FAST_BOOT=1` alcanzaron el estado 11.
La matriz que antes contenía `NaN` ahora contiene valores finitos y aparecen coordenadas de vértices
distintas de cero. El GS registra miles de primitivas de triángulos durante la partida. La captura
presentada a los 90 segundos muestra un fondo oscuro, sin una escena reconocible ni Kratos: esto
**no acredita una partida jugable**. Los buffers de geometría inspeccionados todavía contienen
vértices con ADC activado; el siguiente paso es correlacionar proyección/recorte VU1 con los paquetes
que realmente recibe el GS. Las trazas temporales añadidas para inspeccionar las matrices se retiraron
y se volvió a enlazar el ejecutable; el parche publicado solo cambia los operandos COP1 y sus pruebas.

Antes de publicar se integró mediante merge `ps2recomp-spu2.patch`, publicado por Opus durante estas
pruebas. Se conservaron ambos parches en `compilar.ps1`; se reconstruyeron el IOP, el ejecutable y las
pruebas afectadas. La suite pasa **456/456** y los once parches aplican en una copia aislada de la revisión
fijada. La nueva ejecución con SPU2 vuelve a alcanzar el estado 11. La traza de vértices de la textura
13264 confirma que llegan coordenadas finitas al GS, pero incluye vértices con `draw=0`; habrá que
correlacionarlos con el recorte y con las primitivas que sí se dibujan, sin asumir que todo descarte sea
un error (pueden estar fuera de la vista).

### Seguimiento de las posiciones antes de la proyección (2026-10-05)

Se integraron las publicaciones de Claude hasta `a401182` (salida SPU2 y SIO2) antes de editar el
proyecto. Los diagnósticos siguientes se ejecutaron con el binario anterior a esas dos integraciones,
con la corrección COP1 y SPU2 fase 1, para conservar una referencia comparable.

Tres ejecuciones de 135/140 segundos, con `GOW_SKIP_FMV=1` y `GOW_FAST_BOOT=1`, alcanzaron el estado 11.
Se instrumentaron temporalmente CLIP/FCGET y la entrada de las transformaciones VU1:

- CLIP recibe coordenadas finitas; en la muestra examinada, los puntos exceden los límites de los
  planos. FCGET observa las actualizaciones con la latencia esperada. Esto no demuestra que todo el
  recorte sea correcto, pero no justifica borrar ADC ni forzar el dibujo de los vértices descartados.
- La prueba de inicializar `VF0.w=1` en hilos EE nuevos se repitió con las raíces COP1 ya corregidas.
  No cambió el resultado visual; se retiró nuevamente.
- Un flujo VIF1 completo capturado durante el estado 11 contiene 141 lanzamientos y usa UNPACK S,
  V2 y V4 en modos 0 y 1. No contiene V3 ni STMOD modo 3. Las diferencias detectadas en esos dos
  caminos del runtime no explican este flujo y no se modificaron como supuesto arreglo del juego.
- En un lote de la rutina situada en `0x3230`, las posiciones XYZ cargadas para `ITOF4` ya están en
  cero. La matriz de cámara es finita y la transformación posterior repite su término de traslación.
  Se conservó un volcado anterior a la entrada de esa rutina para distinguir los datos de entrada de
  los paquetes sobrescritos durante su ejecución. Falta identificar la escritura que produce esos
  ceros; todavía no se atribuyen a un fallo concreto del intérprete ni del juego.

El export de Ghidra de GoW2 Europe Demo proporcionado como referencia sirve para identificar las
funciones de cámara, viewport y recorte; sus tipos y pseudocódigo se contrastan con las instrucciones
de GoW1. No se publica el export, el código generado, los flujos VIF ni los volcados de memoria.
Las trazas temporales se retiraron del runtime. La escena sigue sin ser reconocible: no se acredita
una partida jugable. El siguiente paso es rastrear el productor del buffer de posiciones de ese lote,
incluyendo las escrituras anteriores a la proyección, antes de cambiar su consumidor.

Después se aplicaron SPU2 salida y SIO2 al runtime local, se recompilaron las fuentes afectadas y
se enlazaron el ejecutable y las pruebas. La suite integrada pasa **465/465**. Los **13 parches**
aplican en orden sobre la revisión fijada; las **41 fuentes** `.cpp`/`.h` comparadas coinciden con
el runtime local (se excluye el registro de funciones específico de la compilación del juego).
La ejecución integrada de 150 segundos vuelve a alcanzar el estado 11; la captura presentada
mantiene el fondo oscuro y no muestra una escena 3D reconocible. La configuración pasa sin errores,
con los cuatro avisos ya documentados. El merge de Claude `a401182` tiene sus verificaciones de
GitHub completadas correctamente.

<a id="medicion-de-rendimiento-2026-10-05"></a>

## Medición de rendimiento (2026-10-05)

La compilación Release conservaba activadas las trazas generales, las entradas/salidas de cada
función y las trazas RPC del IOP. El registro de funciones vacía el archivo en cada mensaje; una
ejecución de 150 segundos dejó unos 204 MB. `compilar.ps1` configura ahora las tres opciones en
`OFF` por defecto. `scripts\2_compilar.cmd -Trazas` permite activarlas para investigar.

`ps2recomp-perf.patch` añade un perfil opcional, independiente de esas trazas:

- `GOW_PERF_DIAG=1`: emite una línea `[gow-perf]` cada cinco segundos con tiempos transcurridos
  exclusivos de EE, VU, procesamiento GIF/GS, llamadas al IOP y espera del scheduler. Las llamadas
  al GS anidadas en VU se descuentan de VU; no se suman dos veces.
- Los tiempos de subida de imagen y `EndDrawing` pertenecen al hilo de presentación y se informan
  por separado. `EndDrawing` incluye la limitación de refresco y las esperas del host.
- `GOW_PERF_FRAME_PC=0x001837B8` cuenta entradas a `vid::Flip` de este ELF, tanto desde el scheduler
  como desde llamadas directas. Sus reanudaciones no cuentan. `guest_flip_hz` mide esas entradas;
  `host_hz` mide los refrescos de la ventana. Ninguno acredita por sí mismo cuadros 3D correctos.
- `GOW_PERF_DIAG=frames` conserva ambos contadores sin temporizadores por subsistema, para comprobar
  el coste del perfil; los campos de tiempo quedan en cero porque no se miden. Sin `GOW_PERF_DIAG`,
  no lee relojes ni acumula estos contadores.
- Los tiempos incluyen esperas de mutex y del sistema operativo; **no son porcentajes de uso de
  CPU**. El trabajo de un scope todavía abierto se contabiliza al cambiar de scope, por lo que
  conviene comparar varias ventanas completas, no una sola línea.

Prueba reproducible, sin volcados VIF/RAM/VRAM ni capturas periódicas:

```powershell
powershell -File scripts\probar_rendimiento.ps1 -Segundos 150 -Etiqueta limpio
python tools\rendimiento\resumir.py logs\perf_limpio.log --desde 100 --hasta 145
powershell -File scripts\probar_rendimiento.ps1 -Segundos 150 -SoloCuadros -Etiqueta cuadros
python tools\rendimiento\resumir.py logs\perf_cuadros.log --desde 100 --hasta 145
```

El script conserva las pulsaciones de `GOW_PAD_TEST`, desactiva las capturas con
`GOW_PAD_TEST_NO_CAPTURE=1` y restaura el entorno al terminar. Usa `GOW_SKIP_FMV=1` y `GOW_FAST_BOOT=1`;
estas mediciones no evalúan reproducción FMV ni una partida completa. Los registros quedan locales
en `logs/`. Una línea `[gow-pad2:state]` informa del estado aproximadamente cada cinco segundos
sin escribir imágenes. El resumen separa el hilo del juego del de presentación.

Se compararon tres ejecuciones de 150 s, resumiendo ocho ventanas completas entre los segundos
100 y 145 del reloj del perfil (unos 40 s por muestra). En ese tramo se verificó el estado 11:

| Compilación / medición | Entradas a `vid::Flip` por segundo | Refrescos de ventana por segundo |
|---|---:|---:|
| Trazas activadas, perfil completo | 2,19 | 57,25 |
| Trazas desactivadas, perfil completo | 2,37 | 56,60 |
| Trazas desactivadas, solo contadores | 2,40 | 56,45 |

La diferencia observada respecto a la referencia es de aproximadamente un 8 %. La referencia
conservaba las capturas periódicas del mando; las dos mediciones nuevas las desactivan. Son muestras
individuales y no aíslan toda la variación entre ejecuciones, por lo que no acreditan una mejora
garantizada de FPS. La diferencia entre perfil completo y solo contadores ronda el 1 % en esta
muestra: el coste de los temporizadores no parece explicar el bajo rendimiento observado.

Con trazas desactivadas, el tiempo exclusivo contabilizado en el hilo del juego se reparte en
**IOP 61,26 %, GIF/GS 27,94 %, VU 8,72 % y EE 2,08 %**. La espera del scheduler es nula en esas
ventanas. Es un reparto de tiempo transcurrido de los caminos instrumentados, no uso de CPU ni
una separación interna de CPU/SPU2 dentro del IOP. El próximo perfil del IOP debe distinguir
intérprete, servicios y mezcla SPU2; optimizar únicamente VU tendría un margen pequeño en esta escena.

La reconstrucción completa regeneró las 6418 unidades del juego. Los 14 parches aplican en orden
en una copia aislada y sus 42 fuentes `.cpp`/`.h` comparadas coinciden con el runtime compilado.
La suite integrada pasa **467/467**, incluidas dos comprobaciones de contabilidad exclusiva y
anidamiento del perfil. Configuración y handlers sin errores (cuatro avisos conocidos), scripts
PowerShell sin errores y prueba independiente de libpad2 correcta.

La comprobación visual final, separada de las mediciones y usando el mismo salto de FMV/arranque
rápido, vuelve a alcanzar el estado 11. La captura presentada a los 110 s desde la lectura del mando
muestra el fondo oscuro con ondas, sin una escena 3D reconocible ni Kratos. El perfil y la retirada
de trazas mejoran la observación y reducen costes, pero **no acreditan todavía una partida jugable**.

### Posiciones presentes en el flujo VIF anterior

Se revisó fuera de línea el flujo VIF completo capturado anteriormente en el estado 11. Un UNPACK
`V4_32`, con 120 vectores y `STCYCL=0x0102`, ya contiene en su payload los 120 valores
`(0,0,0,32768)`, antes de que VIF/VU1 los interpreten. El mismo comando seguido del payload aparece
en varios buffers del volcado EE. Otros lotes `V4_16` de 81 vértices contienen posiciones XYZ
distintas de cero. Esto acota los ceros de aquel lote a los datos de entrada EE/DMA; no demuestra
todavía qué productor falla ni que una plantilla vacía sea incorrecta en ese momento.

Los símbolos y el export de referencia ayudan a seguir `renEEPrim::InitUNPACKData`, `InitChunk` y
`GetUpdateAddress`. La primera prepara instrucciones y offsets, reservando espacio para los datos;
hay que identificar y comprobar la rutina que posteriormente rellena las posiciones. No se cambia
el recorte ni se sustituye geometría con datos inventados. Los exports, flujos y volcados permanecen
locales y no se publican.

### Referencia del port de Shadow of the Colossus

El usuario aportó [sotc-vibe-pc](https://github.com/LightVelox/sotc-vibe-pc). Se revisaron su
arquitectura, resultados de rendimiento y el runtime exacto que fija su submódulo:
[`LightVelox/PS2Recomp`, `ac9efa070638ad3b3accd284de6f898d5ab271d1`](https://github.com/LightVelox/PS2Recomp/tree/ac9efa070638ad3b3accd284de6f898d5ab271d1).
La copia de referencia queda en una carpeta local ignorada; el runtime de GoW conserva su revisión
fijada y los parches integrados de este proyecto.

Hallazgos concretos para la siguiente etapa:

- La interfaz `GSRasterBackend` coincide con la nuestra. El fork aporta `GSGpuBackend`, un wrapper
  `GSThreadedBackend`, rasterizado OpenGL y pruebas de referencia/replay. Esto permite estudiar una
  incorporación por módulos conservando el renderer CPU para comparar resultados. La interfaz común
  no basta para garantizar compatibilidad de memoria, sincronización, transferencias o presentación.
- Aporta VU1 recompilada con fallback al intérprete y herramientas de verificación. En nuestra muestra
  VU supone menos del 9 % del tiempo instrumentado, por lo que no es la primera optimización de FPS.
- El IOP y el reloj de audio también tienen cambios. Hay que comparar por separado el intérprete,
  scheduler y SPU2 con los cambios de Claude antes de trasladar cualquier arreglo de ese ámbito.
- Sus [resultados publicados](https://github.com/LightVelox/sotc-vibe-pc/blob/main/Docs/PERFORMANCE_RESULTS.md)
  separan campos emulados, actualizaciones del juego, imágenes distintas y swaps del host. Registran
  mejoras concretas de presentación, pero no acreditan 60 actualizaciones reales por segundo en las
  muestras del santuario. Se usa como referencia de implementación y validación, sin trasladar esa
  cifra de rendimiento a GoW.

Prioridad: contrastar los datos y la salida del renderizado con una referencia correcta; después
adaptar backend GS y mejoras genéricas en parches independientes, con pruebas y comparación visual.

### Adaptación del backend GS de SotC (2026-10-05)

Se incorpora `patches/ps2recomp-gs-opengl.patch` como parche independiente después del perfil.
Adapta `GSGpuBackend`, `GSThreadedBackend`, shaders, contexto WGL y tablas de direccionamiento
de VRAM del commit `ac9efa070638ad3b3accd284de6f898d5ab271d1` de Taylor N. Albarnaz / LightVelox,
bajo GPL-3.0. Los archivos importados llevan el comentario `GOW-Port` y su procedencia.
Selección, créditos, pruebas y límites: [`RENDERIZADO.md`](RENDERIZADO.md).

Se conserva el renderer CPU original como predeterminado y como referencia. El GPU funciona
en el hilo GS; vuelve a CPU si no inicializa el contexto o los shaders. Las lecturas y FINISH
esperan los comandos previos, Flush publica y completa su bloque, y el cierre drena comandos
antes de destruir el contexto en su hilo. La presentación compacta del fork se adapta al
stride de 640 píxeles de nuestro frontend. Las tablas VRAM se inicializan una sola vez.
La presentación GL compartida se deja para una etapa posterior.

La suite nativa pasa **474/474**: cinco nuevas pruebas normales y dos pruebas opcionales con
GPU real. En una AMD Radeon RX 5700 XT se verifica OpenGL 4.6 con compute forzado y con
rasterizado gráfico activo. Se comparan transferencias partidas/alineadas, clear, sprites,
VRAM completa y presentación; también se comprueban interior/exterior de un triángulo plano
y textura CT32. Sin las tres correcciones de la cola, sus regresiones fallan: **469/472**.
Las 45 pruebas separadas de caché/CLUT/memoria GS pasan con la integración nueva.

La reconstrucción completa genera las 6418 unidades del juego. Los 15 parches aplican en un
árbol aislado y sus 61 fuentes `.cpp`/`.h` comparadas coinciden con el runtime local.
Configuración y handlers: cero errores, cuatro avisos conocidos; PowerShell y libpad2 correctos.

Se añade `GOW_EE_PRIM_DIAG=1` para observar `renEEPrim::InitUNPACKData` y `GetUpdateAddress`
sin modificar datos ni su ejecución. Registra los callers y los buffers devueltos; el mando
automático puede releer hasta 16 direcciones cada 5 s. Esas direcciones pueden reutilizarse,
por lo que una lectura tardía necesita comprobar su propiedad antes de atribuir el contenido
a un productor. El diagnóstico de VRAM existente usa ahora el snapshot público para leer
también la memoria del backend GPU actualizada.

El alcance respeta el reparto: no se cambia recompilador, FPU, IOP, VIF ni VU1. El parche
`ps2recomp-fpu-roots.patch` permanece intacto. Se detectó una rama remota adicional de Claude
con arreglos EE/DMA/VIF/VU0 y se mantiene separada; no se incorpora a `main` por esta tarea.
Repetir la prueba de posiciones cuando Opus integre sus cambios EE/FPU precede a decidir
si la recompilación de VU1 aporta una mejora útil.

Comparación local sin capturas, con el mismo binario/ELF/ISO y tres ventanas completas de
5 s en estado 11, dentro de `120–136 s`: CPU directo **2,40 `vid::Flip`/s**, CPU con hilo
**2,39**, OpenGL con hilo **3,26**. Presentaciones del host: **56,33 / 55,68 / 59,12 Hz**.
La mejora de OpenGL es aproximadamente **36 %** en esta muestra corta; no se mide variabilidad
ni se certifican cuadros distintos. La GPU real inicializa sin fallback. Las capturas se
toman en ejecuciones separadas de 155 s: ambos modos muestran agua oscura sin Kratos ni la
escena completa. Las tardías del CPU cambian; las de OpenGL de `56–130 s` son idénticas.
Queda pendiente contrastar la selección de framebuffer/presentación del CPU (`preferredSource`)
con CRTC del GPU, además de la geometría. El CPU conserva su papel de referencia predeterminada.

La traza nueva registra **64 inicializaciones, 298 retornos y 192 lecturas posteriores**.
El caller `0x12E258`, en `LoadClient` (`0x12DE70`), obtiene 40 buffers; las 14 direcciones
de ese caller conservadas por la sonda muestran después `(0,0,0,0x8000)`. Se comprueba en
el MIPS original que el bucle `0x12E270–0x12E288` **escribe deliberadamente esa plantilla**.
La reserva inicial incluye los chunks relacionados con el UNPACK V4-32 de XYZ cero anterior.
Esto corrige la hipótesis de que esos ceros, por sí solos, prueben un productor EE roto:
son una inicialización explícita; queda por seguir su transformación y uso en VU1/GIF.
El caller `0x1FB800` (función `0x1FB4B8`) devuelve dos buffers con XYZ no nulo y cambiante,
también en las lecturas posteriores. No se observan retornos desde los dos productores
`goWater` estudiados en esta muestra. No se altera ADC ni se rellenan posiciones artificiales.

### Arreglos del EE tomados del fork de SotC (2026-10-06)

Con autorización del usuario se incorporan, en `ps2recomp-ee-fixes.patch` (aplicado tras
`ps2recomp-gs-opengl.patch`), los arreglos del EE del fork de SotC (TaylorNAlbarnaz/PS2Recomp, rama
`sotc-port`), marcados con `// GOW-Port:` y el commit de origen:

- **FPU (COP1) con la semántica del PS2** (8b51cb9, 9bb389e, c419f26): la FPU del EE no tiene NaN ni
  infinitos. ADD/SUB/MUL saturan a ±`FLT_MAX`; ADD/SUB alinean los operandos sin bits de guarda (como
  PCSX2); `DIV.S` por cero da ±`FLT_MAX` y pone D o I en FCR31; `SQRT.S`/`RSQRT.S` usan |FT| y redondean
  al más cercano; `CVT.W.S` satura; las comparaciones `C.*.S` comparan el patrón de bits y nunca dan
  "desordenado". Antes una división por cero o un desbordamiento producía infinitos o NaN que se
  propagaban, por ejemplo a matrices de cámara. `ps2recomp-fpu-roots.patch` no cambia: sus dos pruebas
  ahora esperan las llamadas `ps2FpuSqrt`/`ps2FpuRsqrt`, que conservan los mismos operandos (FT para
  `SQRT.S`, FS/sqrt(FT) para `RSQRT.S`).
- **VU0 en modo macro:** VADD/VSUB/VMUL/VMULQ saturan; `VDIV`, `VSQRT` y `VRSQRT` usan las mismas reglas
  con los flags del registro de estado. `VRSQRT` calculaba 1/sqrt(FT) e **ignoraba FS**; ahora es
  FS/sqrt(|FT|).
- **LQ/SQ/LQC2/SQC2 ignoran los 4 bits bajos de la dirección** (9bb389e).
- **BLEZ/BGTZ/BLTZ/BGEZ (y sus variantes) comparan el registro de 64 bits**, no los 32 bits bajos (3c46932).
- **Salto final a la entrada de la función llamada** (c4c20e8): `dispatchGuestBranch` tomaba una llamada
  que volvía con el PC en su propia entrada como un retorno implícito; si dentro hubo un salto (`j` de
  vuelta a la función), el llamador continuaba con la pila de la otra función. Ahora solo se aplica si no
  se despachó ningún salto dentro, además de la condición del checkpoint que ya existía.

No se incorporan todavía la entrega de cada desbordamiento de los temporizadores del EE (d328765), que
depende de otros cambios del fork, ni la propagación de constantes con relocalizaciones (019867b), que
afecta a módulos reubicados que God of War no usa.

Pruebas: 6 pruebas nuevas (comparaciones, suma con alineación, división y raíz con redondeo, LQ/SQ,
salto final, unidad Q de VU0) y la de ramas de 64 bits del fork; la suite pasa **479/479** con los 16
parches. **Falta comprobarlo con el juego:** hace falta `scripts\2_compilar.cmd` (cambia el código que
genera el recompilador y las 6 418 unidades se regeneran). Repetir la prueba de posiciones y de la matriz
de cámara del estado 11 y comparar las capturas.

### Presentación OpenGL y campos entrelazados (2026-10-05)

Se añade `ps2recomp-gs-presentation.patch` después del backend OpenGL, sin modificar el
parche importado ni los ámbitos EE/FPU/IOP/VIF/VU1 de la otra tarea.

La primera diferencia encontrada fue `preferredSource`: el GPU no respetaba la fuente
seleccionada por el frontend. Se corrige conservando sus unidades de **bloques de 256 B**,
distintas de las páginas de **8 KB** de DISPFB, con su formato, stride y origen. La prueba
usa una base no alineada a páginas y los cuatro formatos de color. Esa corrección no bastó
para la muestra del juego: la traza confirmó **dos circuitos activos y ninguna fuente
preferida**.

Un diagnóstico temporal comparó el compositor CPU con OpenGL sobre **la misma VRAM obtenida
del GPU**. El juego usa `PMODE=0x8023`, CT24 y modo de campos (`SMODE2 & 3 == 1`), con un
desplazamiento vertical de una fila entre circuitos. El CPU seleccionaba y duplicaba las
filas del campo par/impar; el shader omitía esa paridad. Así se localiza una diferencia
en presentación sin atribuirla a VU1 ni a ceros de los vértices.

El shader incorpora el campo seleccionado por `vsyncTick & 1`, incluyendo la paridad en
la clave de presentación compartida. En modo progresivo no se fuerza alternancia. También
se corrige la salida de un solo circuito para mostrar su RGB sin mezclar contra el fondo
por el alfa del píxel. El readback diferido conserva la fuente y el destino de cada imagen.
Los diagnósticos temporales se retiraron tras localizar la causa.

Las dos pruebas GPU fallan antes de los arreglos (**472/474**) y pasan después (**474/474**),
tanto con compute como con rasterizado gráfico. Comparan campos par/impar y modo progresivo,
alfa cero, selección de fuente y píxeles completos con el CPU. Incluyen los registros de
temporización de GoW con un patrón sintético **512×448**, sin datos del juego. Los 16 parches
aplican en un árbol aislado y sus 61 fuentes comparadas coinciden con el runtime local.

La reconstrucción completa del arreglo regenera las 6418 unidades y enlaza correctamente.
En una ejecución separada de 155 s, OpenGL se inicializa en la RX 5700 XT sin fallback y
alcanza el estado 11. Las cuatro capturas de `70–130 s` son distintas; siguen mostrando
agua oscura, sin Kratos ni el entorno completo. Son resultados anteriores a la integración
de EE/FPU: no se atribuye ese cambio visual a los arreglos posteriores de Opus.

Antes de publicar se integra `main` actualizado por Opus (`d3fcfa9`, con
`ps2recomp-ee-fixes.patch`). Se conservan ambos parches y se reconstruyen de nuevo las
6418 unidades. La suite nativa combinada pasa **481/481**, incluidas las dos pruebas GPU
reales; las pruebas separadas de caché GS pasan **45/45**. Los **17 parches** aplican en
orden y las **65 fuentes** comparadas coinciden con el runtime local. Configuración y
handlers sin errores (cuatro avisos conocidos); nueve scripts sin errores de sintaxis.

La ejecución combinada de 155 s, con diagnósticos de posiciones y render activados,
inicializa OpenGL sin fallback y alcanza el estado 11. Guarda tres capturas tardías
distintas a `94,85 / 95,19 / 110,10 s`; no llega a guardar la cuarta antes del límite.
Se ve agua y algunos artefactos, sin Kratos ni el escenario completo. Esa ejecución con
diagnósticos no es una medida de rendimiento y no cambia la comparación inicial de FPS.

La sonda registra **64 inicializaciones, 298 retornos y 128 lecturas posteriores**:
las 14 direcciones del caller `0x12E258` conservan la plantilla `(0,0,0,0x8000)` en
112 lecturas; las dos de `0x1FB800` tienen XYZ no nulo y cambiante en las otras 16.
Los arreglos EE/FPU no eliminan esa plantilla deliberada del loader. En el snapshot VU1
del estado 11, los 16 floats de la matriz en `0x1060` son finitos; esta comprobación
de una matriz no certifica todas las transformaciones. Sigue pendiente correlacionar
los buffers escritos por sus productores con VIF/VU1 y los triángulos recibidos por GS.
Los snapshots y las capturas permanecen en `logs/`, ignorados por Git.

### Procedencia de posiciones y paquetes PATH1 (2026-10-05)

Se sigue el recorrido RAM → DMA/VIF → VU1 → GIF sobre `12d5da0`, con los arreglos EE/FPU
de Opus ya integrados. Dos ejecuciones instrumentadas de **165 s y 105 s** llegan al
estado 11 con OpenGL. Las sondas locales conservan los primeros ocho streams VIF1,
48 estados de entrada de VU1 y 256 paquetes PATH1 completos de ese estado. La segunda
ejecución observa además la procedencia de cada tramo DMA, sin cambiar sus datos ni
la ejecución de EE/FPU/IOP. Esas sondas y el test de replay se retiran al terminar;
los volcados permanecen exclusivamente en `logs/`.

En el stream VIF1 número 1, de **481.064 B**, el comando `0x6C788002` en `0x62104`
desempaqueta 120 vectores V4-32. Su payload en `0x62108` ya contiene 120 copias de
`(0,0,0,0x8000)`. La traza DMA lo sitúa en RAM **`0x812390`**: el tramo empieza en
`0x620E8`, procede de `0x812370` y contiene 3.904 B. Esa dirección es exactamente la
devuelta al caller `0x12E258` de `LoadClient`, para el objeto `0x8084C0`, chunk 0,
buffer 1. Se vincula así la plantilla con su origen, sin deducirlo solo de su contenido.

| Payload en el stream 1 | Dirección RAM | Caller observado | Chunk / buffer |
|---|---|---|---|
| `0x62108` | `0x812390` | `0x12E258` | 0 / 1 |
| `0x6BAD0` | `0x7F3220` | `0x12E258` | 0 / 1 |

Hay ocho payloads de ese tamaño y con esa plantilla en el stream 1; reaparecen con
los mismos orígenes en los streams 3, 5 y 7. Las lecturas observadas en VU1, PC byte
`0x3240`, también encuentran la plantilla en memoria de datos antes de transformarla.
Los ceros de estas muestras ya existen antes del UNPACK: no los introduce el renderer
GS. Esto no demuestra que todos esos buffers deban contener geometría visible ni
descarta problemas posteriores de selección o transformación.

En cambio, las direcciones no nulas `0x7FCEB0` y `0x81C020`, actualizadas por `0x1FB800`,
no aparecen como origen de payload en las ocho cadenas DMA muestreadas. La muestra
es limitada; todavía hay que identificar su selección y envío antes de vincularlas
con un paquete de salida. No se fuerza un cambio de buffer ni se escriben posiciones.

El inspector nuevo, `tools/gs/inspeccionar_paquetes.py`, valida límites de etiquetas y
payloads y cuenta XYZ, ADC/XYZ3 y rangos de coordenadas. Ambas capturas PATH1 dan
**2.654 vértices: 337 con kick y 2.317 sin kick**, en 256 paquetes; 220 no contienen XYZ.
Un kick solicita dibujo, pero no equivale por sí solo a un triángulo válido. Los paquetes
252 y 254 tienen 120 XYZ idénticos cada uno, con ADC activo en todos ellos. Otros paquetes
tienen posiciones distintas y kicks habilitados. El descarte de esta muestra ya figura
en la salida de VU1, antes de procesarla el backend GS; no se ha demostrado que todos
esos descartes sean incorrectos ni que expliquen por sí solos la escena ausente.

Una reproducción local desde el estado de entrada VU1 número 5 genera exactamente el
paquete original número 11: SHA-256
`3c6dab3532d876d6b914213fc96cb89af6cfdf92697d6fb0051532b82dff57cb4`.
La suite con ese test temporal pasa **480/480** (479 normales y el replay, sin las dos
pruebas GPU). Esto permite reproducir el resultado del intérprete; no certifica su
equivalencia con hardware PS2. La traza Q muestra divisiones y consumos posteriores;
el valor inicial Q=2 observado antes de DIV no prueba que la perspectiva esté detenida.

El inspector incluye ocho pruebas sintéticas de disposición PACKED/REGLIST/A+D,
ADC frente a fog/XYZ3, PRE, NREG=0, IMAGE, padding y truncamiento; se ejecutan también
en CI. No incluye datos del juego. Uso y alcance: [`RENDERIZADO.md`](RENDERIZADO.md).
El siguiente paso es correlacionar un paquete no degenerado descartado con sus entradas
de transformación/CLIP y seguir el envío de los buffers actualizados. No se publica
un arreglo de comportamiento con esta investigación ni se acredita una escena 3D completa.

Tras retirar las sondas se recompilan sus fuentes y se enlazan de nuevo los ejecutables
del juego y de pruebas; ninguno conserva las opciones temporales de captura/replay.
La suite nativa vuelve a pasar **481/481**, incluidas las dos pruebas GPU reales.
Los 17 parches aplican y las 65 fuentes auditadas coinciden con el runtime local.
El inspector pasa 8/8, configuración y handlers no tienen errores (cuatro avisos
conocidos), los nueve scripts PowerShell y libpad2 pasan sus comprobaciones.

### Ampliación de la procedencia y recorte VU1 (2026-10-06)

Una ejecución de 175 s con `GOW_SKIP_FMV=1`, `GOW_FAST_BOOT=1`, OpenGL y sondas temporales
alcanza el estado 11. Amplía la muestra a **128 cadenas VIF1 y 15.798 tramos DMA**.
Ningún payload observado incluye `0x7FCEB0` o `0x81C020`, los dos buffers con XYZ cambiante.
Sus objetos (`0x7FCCC0`, `0x81BE30`) mantienen `updated=0`, `rendered=1`, `view=1` en
las 128 muestras. Ambos buffers DMA son no nulos. Es una observación de esta muestra,
no una prueba de que nunca se envíen ni de que deban dibujar una parte concreta de la escena.

La inspección del MIPS de `renEEPrimContext::ProcessServer` (`0x141B78`) sitúa la selección
en `0x141C18–0x141C34`: lee el índice actualizado de `objeto+0x146`, selecciona el DMA
y lo copia a `objeto+0x147` antes del filtro de vista del objeto. Falta observar si el
contexto recorre esos objetos y qué filtros aplica; no se fuerza el intercambio de buffers.

La traza del replay VU1 número 5 observa los operandos **después del stall de emisión**.
Conserva el SHA-256 del paquete original número 11 y registra 1.576 pares de instrucciones.
Los 86 CLIP se reparten entre PC byte `0xD28` y `0xD30`; FT.w es negativo en todos
(-427,592 a -75,934). Se ven cambios de CLIP con su latencia. Esto no demuestra un fallo
del recorte: primero hay que comprobar el espacio y el signo esperados de la transformación.
Los enteros empaquetados que se muestran como NaN al leerlos como floats tampoco prueban
un error de posiciones. Las sondas y el replay se retiran; los datos quedan en `logs/`.

### Continuación DIRECT y separación de IMAGE/VIF (2026-10-06)

Se integra en un parche propio la corrección de Claude
[`540defd`](https://github.com/KIexster/god-of-war-recomp/commit/540defd), sin incorporar
los cambios EE/IOP de su rama. Antes, una IMAGE pendiente de GIF PATH2 consumía los comandos
VIF que seguían al DIRECT como si fueran píxeles. Ahora solo toma los payloads de los
siguientes DIRECT/DIRECTHL, mantiene MARK/STCYCL/ITOP y procesa los GIFtags posteriores
a la imagen. La prioridad corresponde al DIRECT actual. Dos pruebas antiguas se corrigen
porque esperaban píxeles sin el siguiente comando DIRECT.

La nueva regresión y esas dos pruebas fallan antes del arreglo (**477/480**) y pasan después
(**480/480**); al añadir la prueba de prioridad, la suite normal pasa **481/481**.

Se detecta además que DIRECT recortaba su tamaño al bloque disponible y perdía el resto.
`ps2recomp-vif-direct-fragments.patch` conserva el payload incompleto y DIRECTHL entre
bloques, sin copiar los payloads completos y con un límite de 1 MiB. Seis regresiones nuevas
fallan sin ese cambio (**481/487**) y pasan con él. Incluyen todos los cortes de byte de
PACKED/REGLIST de 32 B, FIFO, IMAGE con comprobación de VRAM, prioridad, IMMEDIATE=0 y reset.
La suite con OpenGL real pasa **489/489**. Los **19 parches** aplican y sus **67 fuentes**
auditadas coinciden con el runtime local. Configuración y handlers sin errores (cuatro
avisos conocidos), inspector GIF 8/8 y scripts PowerShell sin errores de sintaxis.

Alcance, referencia primaria, crédito y diagnóstico opcional: [`RENDERIZADO.md`](RENDERIZADO.md#transferencias-direct-de-path2).
La reconstrucción completa regenera las **6.418 unidades** y termina correctamente. Tras ella,
la suite nativa vuelve a pasar **489/489**, incluidas las dos pruebas GPU. Libpad2 también pasa.
Una partida de **175 s**, con `GOW_SKIP_FMV=1`, `GOW_FAST_BOOT=1` y `GOW_VIF_DIAG=1`,
inicializa OpenGL en la RX 5700 XT sin fallback y llega al estado 11 con `pending=0`,
`levelReady=1` y `flashReady=1`. Guarda dos capturas tardías distintas a **90,27 y 110,04 s**;
no guarda la de 130 s antes de terminar. Se ve agua oscura, sin Kratos ni el escenario completo.

No aparecen mensajes `[gow-vif-direct]` en esa ejecución: **no se observan DIRECT truncados**
en la muestra. Las regresiones demuestran los defectos del runtime, pero esta prueba no prueba
que hayan causado la escena ausente ni permite atribuirles una mejora visual o de FPS.
Las capturas y registros permanecen en `logs/`; las sondas temporales de geometría ya no están
en el ejecutable reconstruido. El siguiente paso es observar la membresía de los buffers
actualizados en `renEEPrimContext::ProcessServer`.

### Pertenencia al contexto de render (2026-10-06)

Se amplía el diagnóstico opcional `GOW_EE_PRIM_DIAG=1` para observar la lista de
`renEEPrimContext::ProcessServer` a la entrada, sin escribir memoria ni cambiar la
ejecución original. La reconstrucción de `src/` termina correctamente. Se registran
hasta 64 entradas antes del estado 11 y otras 64 en él, con 256 nodos como máximo por
entrada y detección de punteros fuera de RAM, ciclos y truncamiento.

Una ejecución de **175 s** con OpenGL, `GOW_SKIP_FMV=1`, `GOW_FAST_BOOT=1` y mando
automático sin capturas alcanza el estado 11. Las **64 muestras** de ese estado pertenecen
al contexto `0x7B8DC0`: vista `0x75CD70`, ID `0x45`, máscara de contexto `0x3` y cámara
`0x7A3950`. Su lista contiene **dos objetos** y termina sin ciclo, puntero inválido ni
truncamiento. Ambos pasan los primeros filtros observados; eso no acredita dibujo ni
los filtros de material y transformación posteriores.

Los objetos `0x7FCCC0` y `0x81BE30`, actualizados por el caller `0x1FB800`, **no pertenecen
a esa lista en ninguna de las 64 muestras**. Sus enlaces `objeto+8` son cero y conservan
`updated=0`, `rendered=1`. El productor sigue obteniendo ambos buffers con XYZ cambiante.
Esto explica por qué ese contexto no alterna sus índices en la muestra; no demuestra
que deban estar registrados en él ni que su ausencia cause la falta de Kratos.

El siguiente paso es identificar quién crea y registra esos clientes, y correlacionar
los dos objetos que sí recibe el contexto con las entradas VU1. No se fuerza su registro
ni el cambio de buffer. Los resultados permanecen en `logs/`. La CI del commit VIF
`f5711bd` pasa tanto las comprobaciones rápidas como la compilación y suite Linux del runtime:
[ejecución 37457211736](https://github.com/KIexster/god-of-war-recomp/actions/runs/37457211736).

### Orden FIFO y DIRECTHL en el GIF (2026-10-06)

La comparación de `GifArbiter::drain` mezclaba la prioridad numérica del path con una
excepción DIRECTHL/IMAGE dentro de `std::stable_sort`. DIRECT y DIRECTHL del mismo path
resultaban equivalentes, pero tenían relaciones distintas con una IMAGE de PATH3: el
comparador no era un orden débil estricto. En las regresiones, esto permite cambiar el
orden de un mismo canal o servir DIRECTHL antes de la IMAGE pendiente.

`ps2recomp-gif-order.patch` agrupa solo por path, manteniendo la estabilidad, y arbitra
entre las cabeceras de PATH2/PATH3 después de PATH1. La excepción ya no entra en el
comparador de ordenación. Conserva también los paquetes añadidos por un callback para
procesarlos en la siguiente tanda. No cambia los backends CPU/OpenGL ni la semántica EE/IOP.

Las dos regresiones prueban todas las permutaciones de DIRECTHL/DIRECT/IMAGE y de
DIRECTHL/setup/IMAGE. Fallan antes (**487/489**, suite normal) y pasan después. La suite
con las dos pruebas OpenGL reales pasa **491/491**. Los **20 parches** aplican en orden
y sus **68 fuentes** auditadas coinciden con el runtime local. Alcance y referencia:
[`RENDERIZADO.md`](RENDERIZADO.md#orden-de-los-paquetes-gif).

La CI del diagnóstico de contexto `f8a851a` también pasa:
[ejecución 37458210600](https://github.com/KIexster/god-of-war-recomp/actions/runs/37458210600).
La reconstrucción completa regenera las **6.418 unidades** y termina correctamente. Tras
ella, la suite vuelve a pasar **491/491**, incluidas las dos pruebas OpenGL, y una nueva
auditoría confirma las **68 fuentes** de los **20 parches** sin diferencias.

Una partida de **175 s** con OpenGL, `GOW_SKIP_FMV=1`, `GOW_FAST_BOOT=1`,
`GOW_EE_PRIM_DIAG=1` y `GOW_ANM_DIAG=1` inicializa la RX 5700 XT sin fallback y alcanza
el estado 11 con `pending=0`, `levelReady=1` y `flashReady=1`. Las capturas tardías de
**110,19 y 130,27 s** son distintas y muestran agua oscura; siguen ausentes Kratos y el
escenario completo. Las **64 muestras** de contexto repiten la lista válida de dos objetos
y la ausencia de `0x7FCCC0`/`0x81BE30`. No se atribuye al nuevo orden una mejora visual ni
de FPS. Capturas y registros permanecen en `logs/`.

La CI del arreglo GIF `701da8d` pasa:
[ejecución 37461636026](https://github.com/KIexster/god-of-war-recomp/actions/runs/37461636026).

El control de **600 s** sin `GOW_FAST_BOOT`, con el mismo ejecutable, `GOW_SKIP_FMV` y
las mismas sondas, muestra avance de la introducción hasta **11,75 s** de animación en
los mensajes conservados y después llega al estado 11. Las **64 muestras** repiten el
contexto, cámara y lista válida de dos objetos; `0x7FCCC0` y `0x81BE30` siguen ausentes.
Las capturas de **360,28 y 480,20 s** son distintas y muestran agua oscura, sin Kratos.
Este control no aporta evidencia de que el arranque acelerado cause esa ausencia en
el contexto observado. No acredita el registro de todos los demás objetos del nivel
ni permite comparar FPS bajo diagnóstico.

### Efectos de las etiquetas GIF vacías (2026-10-06)

Al revisar el parser se detecta que aplicaba PRE y reiniciaba Q aunque `NLOOP=0`.
Aplicar PRIM también descartaba los vértices pendientes, incluso con la misma topología.
REGLIST e IMAGE aplicaban indebidamente PRE. Se corrigen el parser general y la ruta
PACKED nativa en `ps2recomp-gif-tag-semantics.patch`, conservando el frontend común a CPU
y OpenGL. El atajo de subida IMAGE omitía a su vez PRE del setup PACKED y el reinicio de
Q de las etiquetas no vacías; se corrige sin cambiar los bytes de la imagen.

Las tres primeras regresiones fallan antes (**489/492**, suite normal). Tras esos arreglos,
la nueva regresión de la subida IMAGE todavía falla (**492/493**), antes de corregir ese
atajo. Cubren ambas rutas, etiquetas vacías con y sin PRE entre vértices, Q, modos que
ignoran PRE y controles no vacíos. Alcance y referencia primaria:
[`RENDERIZADO.md`](RENDERIZADO.md#etiquetas-gif-vacías-y-pre).

La revisión de las dos capturas locales anteriores de 256 paquetes PATH1 encuentra en
cada una **205 etiquetas vacías, 90 con PRE=1 y PRIM=0x5C**, sin truncamientos de paquete.
Es el mismo caso que el arreglo convierte en una etiqueta sin emisiones al GS; eso no
demuestra por sí solo que causara la escena ausente. La suite con OpenGL real pasa
**495/495**. Los **21 parches** aplican y sus **68 fuentes** auditadas coinciden con el
runtime local. Configuración/handlers sin errores (cuatro avisos conocidos) y PowerShell
sin errores de sintaxis. La reconstrucción completa regenera las **6.418 unidades** y
termina correctamente. Tras ella, la suite vuelve a pasar **495/495** y la nueva auditoría
confirma las **68 fuentes** de los **21 parches** sin diferencias.

La partida de **175 s** con las mismas opciones OpenGL/SKIP_FMV/FAST_BOOT y sondas de
contexto/animación inicializa el rasterizado de hardware en la RX 5700 XT sin fallback y
llega al estado 11 con `pending=0`, `levelReady=1` y `flashReady=1`. Las capturas de
**110,24 y 130,14 s** muestran agua oscura y son distintas, pero siguen ausentes Kratos
y el escenario completo. Las **64 muestras** conservan la lista válida de dos objetos y
los buffers dinámicos fuera de ella. El arreglo corrige los efectos de las etiquetas;
esta ejecución no acredita una mejora visual ni de FPS. Los datos del juego permanecen
en `logs/`.

### Identificación del productor de los buffers dinámicos (2026-10-06)

El MIPS retail de `0x1FA8A8` llama a `0x1FB4B8` con cinco vectores y después a
`attachment::tChained::Connect` (`0x1FCD48`, nombre del mapa de símbolos) y a otra rutina
con cuatro vectores. La rutina de cinco vectores obtiene tipos 3 y 0 del mismo `renEEPrim`,
elige el índice opuesto al mostrado y escribe siete coordenadas UV con las mismas
constantes que `attachment::tChained::DrawGapFiller` en la referencia GoW 2 con símbolos.

Estas coincidencias vinculan probablemente los buffers de `0x1FB800` con el relleno de
las cadenas de los attachments. No confirman el nombre retail ni que esos objetos deban
registrarse en el contexto observado. Las estructuras difieren entre versiones; no se
cambia `funcmap.csv` ni se sustituye código del juego. El cuerpo del escenario y de Kratos
debe seguirse también por la ruta de modelos. Se localiza `renModelServer::ProcessServer`
en `0x159C58` (nombre del mapa): selecciona un contexto de una tabla y llama a su método
virtual antes de enviar la cadena DMA. El siguiente diagnóstico observará esa selección
y el descarte de modelos, conservando la ejecución original.

### Selección del contexto por el servidor de modelos (2026-10-06)

Se añade `GOW_MODEL_DIAG=1` como observación opcional de `0x159C58`. La recompilación de
`src/` termina correctamente y PowerShell pasa sin errores. La sonda conserva los
checkpoints y llama siempre al original; no escribe registros ni memoria del juego.
El perfil elimina la variable. Alcance: [`RENDERIZADO.md`](RENDERIZADO.md#selección-del-contexto-de-modelos).

Una partida de **175 s** con OpenGL, SKIP_FMV, FAST_BOOT y las sondas de contexto/animación
registra **64 muestras en el estado 3 y 64 en el estado 11**. Todas observan el servidor
`0x59D878`, tabla `0x59DBA8`, grupo/slot `0/0`, array `0x59DBC0`, contexto `0x59DDC8`
y tabla virtual `0x2C2468`, con punteros dentro de RAM. El ajuste de `this` es cero y el
destino seleccionado es `0x1511F0`, identificado por el mapa como
`renGROBMasterContext::ProcessServer`. La referencia GoW 2 también llama a un contexto
maestro GROB desde esta ruta: el destino observado no demuestra una selección incorrecta.

Las capturas de **110,16 y 130,04 s** vuelven a mostrar agua oscura, sin Kratos ni el
escenario completo. Los registros mantienen `pending=0`, `levelReady=1` y `flashReady=1`.
La observación de entrada no certifica las llamadas posteriores ni el culling de los
modelos. El siguiente paso es seguir los contextos e instancias que recorre ese maestro,
y correlacionar sus descartes con los paquetes VU1. Capturas y registros siguen en `logs/`.

La CI del arreglo de etiquetas `f385fcc` pasa:
[ejecución 37465434359](https://github.com/KIexster/god-of-war-recomp/actions/runs/37465434359).

### Recorrido real del maestro GROB y listas de modelos (2026-10-06)

Se amplía `GOW_MODEL_DIAG=1` para observar la entrada del maestro `0x1511F0`, las
listas de `0x159878` y las llamadas a `0x157A60`. El mapa retail deja estas dos últimas
como funciones sin nombre; su estructura coincide respectivamente con
`renModelServerContext::ProcessServer` y `ProcessModel` en la referencia GoW 2.
Los offsets de los filtros se comprueban en el MIPS retail, sin copiar su código
generado. Alcance y límites: [`RENDERIZADO.md`](RENDERIZADO.md#selección-del-contexto-de-modelos).

La primera prueba de **175 s** confirma que el maestro es compartido por varios
servidores. Se limita después cada contexto por separado para que otros servidores
no agoten las muestras de modelos. La segunda prueba, también de **175 s**, registra
en el estado 11 cuatro contextos activos de modelos. `0x1111B98` contiene **14 modelos**:
uno no pasa la máscara de vista, ocho son candidatos al árbol estático y cinco a la
llamada directa. Los otros contextos contienen **1, 1 y 2 modelos**, todos candidatos
a la llamada directa en las muestras observadas. Se verifican **64 entradas reales**
a la rutina de procesamiento en esta fase, repartidas entre nueve modelos distintos,
con un grupo cada uno. Las listas observadas no presentan ciclos, punteros inválidos
ni truncamientos.

Durante las primeras 64 muestras del maestro de modelos, la lista crece de ocho a
trece contextos y después queda en once, con tres activos. El contexto de 14 modelos
se observa en **21 llamadas** antes de esa reducción. Es una transición del estado
observado; no identifica por sí misma un fallo de registro ni cuáles son los modelos
de Kratos. Las capturas de **110,25 y 130,21 s** continúan mostrando agua oscura sin
Kratos ni el escenario completo. No se fuerzan registros, visibilidad ni índices.

La siguiente observación registra la visibilidad raíz del esqueleto y el retorno de
`renView::Clip` procedente del procesamiento de modelos. Una entrada en esa rutina
todavía no prueba que el modelo pase sus filtros internos ni que emita geometría.

### PEXEW duplica Y en la Z de las esferas de visibilidad (2026-10-06)

La tercera partida de **175 s** añade la observación de visibilidad y Clip. Las **64
entradas de procesamiento** en el estado 11 tienen raíz de esqueleto válida y visible,
y el primer bloque de visibilidad de grupos vale `0xFFFFFFFF`. En las **64 llamadas
observadas a Clip**, correspondientes a siete modelos distintos, los cuatro argumentos
son finitos, Y y Z tienen los mismos bits y el retorno real es **`0x80000000`**, descarte.
Antes del estado 11 se registran 49 descartes y 15 retornos `0x20`. Estos límites no
cubren todos los modelos ni todos los descartes de la escena.

Se localiza una causa concreta de los argumentos duplicados: en `0x157F94`, PROT3W
extrae Y para `f13`; en `0x157FA0`, PEXEW debería extraer Z para `f14`. La macro actual
`PS2_PEXEW` usa `_MM_SHUFFLE(2,3,0,1)`, que copia también Y a la palabra baja.
Una reproducción nativa aislada con palabras **[11,22,33,44]**, ajena al juego, confirma
la permutación incorrecta. También detecta que PROT3W rota las cuatro palabras, cuando
debería conservar la cuarta. Se contrasta la semántica con
[PEXEW y PROT3W de PCSX2, commit 32ac6e2](https://github.com/PCSX2/pcsx2/blob/32ac6e23e4aaf8c8c5e74a6c1ed750ee7672120e/pcsx2/MMI.cpp#L1435).

| Instrucción | Resultado actual, de palabra baja a alta | Resultado esperado |
|---|---|---|
| PEXEW | [22,11,44,33] | [33,22,11,44] |
| PROT3W | [22,33,44,11] | [22,33,11,44] |

**Resuelto en `ps2recomp-mmi.patch` (ver "Auditoría de las instrucciones MMI").** Pendiente original: corregir PEXEW en `ps2_runtime_macros.h`
(`_MM_SHUFFLE(3,0,1,2)`) y PROT3W tanto allí como en la emisión de
`mmi_translation_helpers.cpp` (`_MM_SHUFFLE(3,0,2,1)`), con regresiones de las cuatro
palabras, alias origen/destino y registro cero. PROT3W se emite en línea y requiere
regenerar las funciones; cambiar solo la macro no corrige el ejecutable. Este turno
no modifica el recompilador, FPU, IOP ni los parches de EE.

La macro incorrecta explica la Z mal extraída; todavía no se ha probado cuánto cambia
el descarte ni la imagen tras corregirla. Las capturas de **110,04 y 130,22 s** siguen
mostrando agua sin Kratos ni el escenario completo, con OpenGL de hardware y estado 11.
Las sondas compilan en Windows y los datos/reproducciones quedan en `logs/`. La siguiente
prueba de render debe repetir estas sondas tras integrar el arreglo EE y después seguir
los modelos que pasen Clip hasta las partes y los paquetes VU1.

### Continuaciones GIF y equivalencia del atajo DMA (2026-10-06)

Se añaden cuatro parches al final de la lista de compilación, sin modificar los parches
del EE, la FPU, el recompilador o el IOP:

| Parche | Cambio comprobado |
|---|---|
| `ps2recomp-gif-stream.patch` | Cursores PACKED/REGLIST/IMAGE por PATH; conserva registros, Q, padding y pixels entre bloques y DIRECT distintos; VIF entrega bytes originales sin wrappers IMAGE. |
| `ps2recomp-gif-image-order.patch` | DIRECTHL reconoce continuaciones IMAGE y etiquetas tras setup; no confunde registros o relleno con etiquetas ni bloquea por una IMAGE vacía. |
| `ps2recomp-gif-image2.patch` | FLG=3 consume el payload IMAGE2 en las rutas general/nativa y conserva su clasificación en el árbitro. |
| `ps2recomp-gif-native-tag.patch` | El atajo DMA de texturas conserva PRE del setup, reinicia Q y acepta IMAGE2 después de validar toda la cadena; rechazo sin efectos en el GS. |

Los fallos se reproducen antes de corregir cada tema: **4** pruebas de fragmentación,
**4** de clasificación, **3** de IMAGE2 y **1** del estado del atajo DMA. Se añaden
**20 regresiones** en total, con controles de reset, PATH intercalados, NREG=0 y cadena
rechazada. La suite nativa pasa **515/515**, incluidas dos pruebas con OpenGL real.
Se ajusta la expectativa de un wrapper IMAGE antiguo y cuatro fixtures de prioridad
que usaban imágenes vacías; las razones se detallan en
[`RENDERIZADO.md`](RENDERIZADO.md#continuidad-del-flujo-gif-por-path).

La auditoría aplica **25 parches** sobre el commit fijado y compara **71 fuentes**,
sin diferencias respecto al árbol probado, también después de la compilación completa
con **6.418 unidades generadas** y resultado cero. Se repite la suite **515/515** con
OpenGL real. Una partida de **175 s** con SKIP_FMV, FAST_BOOT y las sondas de modelos
llega al estado 11, `pending=0`, `levelReady=1` y `flashReady=1`, sin fallback CPU.
Las capturas de **90,13 y 110,09 s** muestran agua, sin Kratos ni el escenario completo.
Las **64 llamadas observadas a Clip** en esa fase siguen descartando los modelos con
`0x80000000`; Y y Z mantienen los mismos bits y los argumentos son finitos.

No se acredita una mejora de FPS ni la aparición de Kratos. El arbitraje sigue trabajando
con bloques completos, sin reproducir todos los ciclos o preempciones del hardware.
Las posiciones y los descartes de modelos deberán repetirse tras el arreglo MMI pendiente
en el EE. Durante la validación se reproduce otro problema separado en CPU y OpenGL:
48 bytes CT24 juntos producen 16 pixels, pero en tres bloques de 16 B producen solo 15
y dejan la transferencia abierta. Se continúa con la conservación de ese pixel parcial;
los datos sintéticos y las capturas permanecen en `logs/`.

La CI de `1f009b4` pasa:
[`Pruebas`, ejecución 37497401093](https://github.com/KIexster/god-of-war-recomp/actions/runs/37497401093).

### Pixels CT24/Z24 divididos entre cargas IMAGE (2026-10-06)

`ps2recomp-gs-image-fragments.patch` conserva como máximo dos bytes pendientes por
backend. Completa el pixel con la carga siguiente y pasa el resto directamente a la
subida existente, sin retener una copia de la textura. Se limpia el acumulador al
reiniciar o empezar otra transferencia; exportar/importar el estado OpenGL lo conserva.
Los formatos distintos de CT24/Z24 siguen usando la ruta anterior.

La suite reproduce **tres fallos** antes del cambio: cargas CPU, cargas OpenGL y
exportación de un pixel parcial. Con el cambio pasan **519/519 pruebas**, incluidas
cuatro con OpenGL real. Se comparan VRAM completa y contador de pixels en todos los
cortes de una carga de 48 B, además de cargas repetidas de 1 B y 16 B, byte alto preservado
en CT24/Z24, reset/nueva transferencia y exportación/restauración. Las pruebas contienen
datos sintéticos. La auditoría aplica **26 parches** y reproduce exactamente **73 fuentes**.
La compilación completa termina con **6.418 unidades generadas** y código cero;
se repiten las **519/519 pruebas** con OpenGL real y la auditoría exacta de fuentes.
El control de partida de **175 s** llega al estado 11, `pending=0`, `levelReady=1`
y `flashReady=1`, sin fallback CPU. Las capturas de **90,15 y 110,13 s** siguen
mostrando agua sin Kratos ni el escenario completo. Las **64 llamadas observadas a Clip**
en el estado 11 descartan con `0x80000000`, con Y/Z idénticas y argumentos finitos.
Este arreglo corrige las cargas fragmentadas; no acredita mejora de FPS ni resuelve
el descarte de modelos que se investiga por separado en el EE.

La CI de `0a4f66c` pasa:
[`Pruebas`, ejecución 37501053476](https://github.com/KIexster/god-of-war-recomp/actions/runs/37501053476).

### Centros y bordes de triángulos en el renderer CPU (2026-10-06)

`ps2recomp-gs-triangle-sampling.patch` comparte con OpenGL las reglas de triángulos
adaptadas del fork SotC: centro entero, XYOFFSET completo 12.4 y borde compartido
dibujado una sola vez. El CPU anterior desplazaba medio pixel la muestra, truncaba
el offset y aceptaba ambos lados de una arista. Tres regresiones reproducen esos
fallos; OpenGL compute/hardware ya las pasaban. Ahora pasan **523/523 pruebas**,
incluidas cinco con OpenGL real. Se ajustan tres fixtures antiguos por las razones
documentadas en [`RENDERIZADO.md`](RENDERIZADO.md#triangulos-cpu-como-referencia-para-opengl).

La comparación independiente de VRAM completa en **48 casos** de triángulos planos/IIP,
cuatro modos de prueba Z y valores Z32 `7`, `0x80000001` y `0xffffff01`, en las dos
rutas OpenGL, pasa de **12 diferencias** a **cero**. Esto mejora la referencia CPU para
comparar el renderer GPU; no prueba que todos los efectos GS sean equivalentes.
La compilación completa termina con **6.418 unidades generadas** y código cero;
se repiten las **523/523 pruebas** con OpenGL real. La auditoría reproduce **27 parches
y 74 fuentes** exactamente. El control de partida de **175 s** llega al estado 11,
`pending=0`, `levelReady=1` y `flashReady=1`, sin fallback CPU. Las capturas de **90,02
y 110,00 s** siguen mostrando agua sin Kratos ni el escenario completo; las 64 llamadas
observadas a Clip en esa fase mantienen descarte `0x80000000`, Y/Z idénticas y valores
finitos. La comparación sintética mejora; la partida jugable y los FPS quedan sin acreditar.

También se revisa [Tobiichi-Port en `9f02797f`](https://github.com/YYOzcan/Tobiichi-Port/tree/9f02797f8ab7481fddad4d2daf7afad82d11699f).
Los cinco archivos GS/VIF/VU1 comparados coinciden con nuestro runtime base fijado.
Su README mantiene menú y partida pendientes; los atajos GoW de arranque no se
incorporan como solución de renderizado. Se documenta la revisión sin afirmar que
su ejecutable se haya probado aquí.

La CI de `5ada6aa` pasa:
[`Pruebas`, ejecución 37504095195](https://github.com/KIexster/god-of-war-recomp/actions/runs/37504095195).

### Cobertura e interpolación de sprites CPU (2026-10-06)

`ps2recomp-gs-sprite-sampling.patch` conserva las fracciones de XYOFFSET/UV y comparte
con OpenGL los ejes firmados del fork SotC. El CPU deja de dibujar áreas vacías y
mantiene el origen de textura al invertir los ejes o recortar con scissor. Cuatro
regresiones reproducen los fallos antes del arreglo. La comparación sintética de
VRAM completa pasa de **14 diferencias a cero en 16 casos** con compute/hardware;
la suite pasa **528/528**, incluidas seis pruebas con OpenGL real.

Se ajustan ocho fixtures antiguos que dibujaban sprites vacíos: pasan a usar un
rectángulo de 1×1 conservando sus comprobaciones de alias CT32, CLUT, alpha y scissor.
La prueba nueva verifica que ancho/alto cero no cambie ningún byte de VRAM. Los
motivos y la procedencia se detallan en
[`RENDERIZADO.md`](RENDERIZADO.md#sprites-cpu-como-referencia-para-opengl).
La compilación completa termina con **6.418 unidades generadas** y código cero;
se repiten las **528/528 pruebas** con OpenGL real. La auditoría reproduce **28 parches
y 74 fuentes** exactamente. El control de partida de **175 s con el renderer CPU**
llega al estado 11, `pending=0`, `levelReady=1` y `flashReady=1`.
Las capturas de **90,18 y 110,11 s** siguen mostrando agua sin Kratos ni el escenario
completo, y las 64 llamadas observadas a Clip en esa fase descartan con `0x80000000`,
con Y/Z idénticas y argumentos finitos. El mismo bloqueo se observa con CPU y OpenGL;
estos controles no acreditan una partida jugable ni una mejora de FPS.

### Interpolación de atributos constantes y alpha-test (2026-10-06)

`ps2recomp-gs-triangle-constants.patch` calcula RGBA Gouraud y F por diferencias
entre vértices en CPU y OpenGL compute/hardware. La suma redondeada de los pesos
reducía valores constantes; alpha 128 podía quedar en 127 y fallar GEQUAL 128,
dejando huecos en el triángulo. En la sonda de 17×19 pixels fallan 17 centros de
color y 8 de niebla en CPU, y 151/110 en cada ruta OpenGL. Para F se usa como
referencia un punto con los mismos atributos, sin interpolación.

Dos pruebas CPU y una con OpenGL real reproducen el fallo antes del cambio; después
pasan **531/531 pruebas**, incluidas siete con OpenGL. Se comparan los centros
interiores y ambos sentidos de giro; la prueba de color activa GEQUAL 128 para
detectar también los huecos. La auditoría reproduce **29 parches y 74 fuentes**.
La sonda independiente pasa **6/6 casos**: CPU, OpenGL compute y hardware, con color
o F constantes. La comparación de gradientes y profundidad conserva **48/48 casos**
iguales entre CPU y ambas rutas OpenGL. La compilación completa regeneró las 6418
unidades y terminó con código 0; la suite posterior vuelve a pasar **531/531**.

El control de 175 s usa OpenGL real, sin fallback CPU. Las capturas de **90,21 y
110,23 s** muestran agua sin Kratos; se alcanza el estado 11 con `pending=0`,
`levelReady=1` y `flashReady=1`. Las 64 llamadas observadas a Clip en esa fase siguen
descartando con `0x80000000`, argumentos finitos y Y/Z iguales. La corrección de
atributos constantes queda verificada; todavía falta la escena 3D completa.
Este control usa el IOP anterior al parche de aceleración incorporado por Opus;
la integración y su perfil se registrarán por separado.

La CI del parche anterior de sprites también terminó en verde:
[ejecución 37507078237](https://github.com/KIexster/god-of-war-recomp/actions/runs/37507078237).

### Rendimiento del intérprete del IOP (2026-10-06)

El perfil de la medición de rendimiento atribuye al IOP el 61 % del hilo del juego. Un IOP sin hilos
listos cuesta poco (unos 7 ms por segundo emulado, incluido el SPU2), así que ese tiempo es de hilos del
IOP ejecutando instrucciones. El intérprete hacía unos **55 M instrucciones/s** en la máquina de la CI;
el IOP real va a 36,8 MHz, por lo que un IOP ocupado necesita casi dos tercios de un núcleo para ir a
tiempo real. Con callgrind, cada instrucción emulada costaba unas 245 instrucciones del host.

`ps2recomp-iop-fast.patch` (tras `ps2recomp-gs-sprite-sampling.patch`) quita costes por instrucción sin
cambiar el comportamiento:

- La búsqueda de stubs de importación (`IopImportRegistry::decode`) se hacía en **cada** instrucción;
  ahora solo cuando la instrucción es `jr ra`, que es como empiezan los stubs.
- Lectura de instrucciones y cargas/escrituras en RAM con camino rápido en línea (`fetch32`, `load*`,
  `store*`); los registros de hardware, la scratchpad, el SPU2 y el SIO2 siguen por `read*`/`write*`.
  Las escrituras siguen marcando la RAM ocupada como antes.
- La comprobación de DMA pendiente es una marca en línea; `PS2X_IOP_PC_EVERY` se lee al crear el IOP en
  lugar de con un `static` local en cada instrucción; `physicalAddress` pasa a la cabecera.

Medido con callgrind (instrucciones del host para el mismo programa del IOP): bucle aritmético
737 → 452 M (−39 %), bucle con LW/SW 1513 → 907 M (−40 %), es decir, unas **1,65 veces más rápido**. La
nueva prueba "IOP fast loads and stores match the generic memory paths" fija que los accesos rápidos
equivalen a los genéricos (espejos KSEG0/KSEG1, marca de RAM escrita, registros de hardware y FIFO del
SIO2). La suite pasa **523/523** con los 29 parches.

Siguiente paso: averiguar qué hilos del IOP están ocupados en la partida. Si son bucles de espera
activa (sondeo de un registro o de memoria), saltarlos rendiría mucho más que acelerar el intérprete.
Para ello basta una ejecución con `PS2X_IOP_PC_EVERY=1000000`: el registro mostrará `[IOP:pc]` con los
PC más repetidos. **Falta medirlo con el juego:** repetir `scripts\probar_rendimiento.ps1` y comparar el
porcentaje del IOP y las llamadas a `vid::Flip` por segundo.

### Coordenadas constantes e integración del IOP rápido (2026-10-06)

Se integra `origin/main` con el parche IOP de Opus mediante merge, sin modificar su
implementación. Se conservan los dos registros de investigación y se ordena el IOP
rápido después de sprites y antes de los nuevos parches de atributos de triángulos.

`ps2recomp-gs-triangle-texcoords.patch` corrige UV/STQ constantes en CPU y ambas
rutas OpenGL. La regresión incluye Q=1,5: requiere también refinar el recíproco GPU,
porque el texel anterior se seleccionaba incluso en el primer centro. Antes fallan
las tres pruebas nuevas; después pasa la suite integrada **535/535**, incluidas
ocho pruebas con OpenGL real. Las restas de UV son firmadas y se conserva la
interpolación homogénea antes de dividir por Q.

La sonda independiente verifica **24/24 casos de atributos constantes**: UV o STQ
con Q=1,5, nearest/lineal, ambos sentidos y CPU/OpenGL compute/hardware. Conserva
otros 12 casos exploratorios con Q variable y S/T proporcionales a Q sobre fronteras
exactas de texel: todavía difieren del control constante, tanto en CPU como en GPU.
No se consideran resueltos por este parche. El gradiente de la regresión evita esas
fronteras y comprueba que se mantiene la división después de interpolar.

Las variantes hardware se compilan de forma asíncrona y pueden usar compute al
principio. La sonda espera de forma acotada y confirma cada dibujo hardware mediante
los contadores existentes: aumentan primitivas y batches sin aumentar tiles compute.
Así se verifican también los ocho casos constantes por la ruta hardware ejecutada,
no solo con esa ruta habilitada.

La auditoría reproduce **31 parches y 75 fuentes sin diferencias**; los 14 scripts
PowerShell se analizan sin errores. La compilación completa termina con código 0,
regenera las 6418 unidades y la suite posterior vuelve a pasar **535/535**.
El control adicional de RGBA/alpha-test y F pasa **6/6**, confirmando también hardware
con los contadores después de terminar las variantes asíncronas.

El primer control del juego quedó interrumpido al alcanzarse el límite de uso de
la revisión automática de aprobación; solo registró el arranque y no llegó a guardar
el control visual de la escena. Se repetirá tras incorporar las correcciones MMI y
VU0 publicadas por Opus durante esa pausa. El renderer sigue en estado parcial.

### VU0 en modo macro y cadenas DMA largas (2026-10-06)

`ps2recomp-vu0-macro.patch` (tras `ps2recomp-iop-fast.patch`) incorpora, con la autorización ya dada para el
fork de SotC, dos commits más que afectan al código del EE:

- **03d18df — VU0 en modo macro usa su memoria de datos.** `VLQI`/`VSQI`/`VLQD`/`VSQD` y `VILWR`/`VISWR`
  leían y escribían la **RAM del EE desde la dirección 0** (`READ128((vi & 0x3FF) << 4)`) en lugar de la
  memoria de datos de VU0 (`(vi & 0xFF) << 4` dentro de sus 4 KB); además `VSQI`/`VSQD` tenían intercambiados
  los campos Fs e It, y VI0/VF0 podían escribirse. En SotC esto rompía la pila de matrices
  (push/pop) y desaparecían los personajes. Si God of War usa esas instrucciones, estaba leyendo y
  pisando los primeros 16 KB de la RAM del EE.
- **e36fbf1 — cadenas DMA de más de 4096 tags.** El recorrido de cadenas se cortaba a los 4096 tags y
  perdía el final: el GIF quedaba en modo IMAGE y se tragaba la configuración A+D del cuadro siguiente.
  El límite pasa a 2^20 tags y avisa (`[dma] chain on ... stopped after ... tags`) si se alcanza. El mismo
  commit corrige `VRNEXT`/`VRINIT`/`VRXOR`/`VRGET` (LFSR del registro R), `VFTOI` (saturación) y `VABS`
  (denormales), y comparte el registro R de VU0 entre hilos en `EeScheduler`. La herramienta opcional
  `vu0_audit` del fork no se incorpora (depende de su renderer de referencia).

La suite pasa **528/528** con los 30 parches (pruebas del fork que decodifican las instrucciones
`vsqi`/`vlqd`, cadena DMA larga, números aleatorios, `VFTOI`/`VABS`). **Falta comprobarlo con el juego:**
requiere `scripts\2_compilar.cmd` (cambia el código generado). Conviene repetir la captura del estado 11
y, si aparece, buscar `[dma] chain` en el registro.

### Auditoría de las instrucciones MMI (2026-10-06)

A raíz del hallazgo de PEXEW/PROT3W en la esfera de visibilidad (sección "PEXEW duplica Y en la Z de las
esferas de visibilidad"), se comparó el código que emite el recompilador para **60 instrucciones MMI**
(aritméticas, comparaciones, saturación, permutaciones, empaquetado y desplazamientos) con la semántica de
[PCSX2 `MMI.cpp`](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp). Una prueba diferencial local
(se volcó con `CodeGenerator::translateInstruction` el código de cada instrucción y se compiló contra
`ps2_runtime_macros.h`) lo ejecutó con 2000 pares de valores aleatorios y de borde, con destino distinto y
con destino igual a cada fuente, frente a una referencia escrita a partir de PCSX2. **15 instrucciones no coincidían:**

| Instrucción | Fallo |
|---|---|
| PEXEW, PROT3W, PEXCW | palabras mal ordenadas (PEXEW copiaba Y en la Z de las esferas de visibilidad) |
| PEXEH, PREVH, PEXCH | medias palabras mal ordenadas (PREVH invertía las 8 en vez de cada mitad) |
| PINTH, PINTEH | intercalaban la mitad equivocada de rs |
| PABSW, PABSH | leían rs en vez de rt y no saturaban 0x80000000 / 0x8000 |
| PADDUH, PSUBUH | sumaban/restaban sin saturar |
| PSLLVW, PSRLVW, PSRAVW | desplazaban rs (no rt) y en las cuatro palabras, sin extender el signo a 64 bits |

`ps2recomp-mmi.patch` (tras `ps2recomp-vu0-macro.patch`) corrige las macros de `ps2_runtime_macros.h` y la
emisión de `mmi_translation_helpers.cpp`. Con el parche, las 60 instrucciones coinciden con PCSX2 en todas
las variantes. La suite gana `PS2Mmi` (permutaciones con [11,22,33,44], saturación de PABS, desplazamientos
y formas emitidas) y pasa **533/533** con los 31 parches.

**Falta comprobarlo con el juego** (requiere `scripts\2_compilar.cmd`, cambia el código generado): repetir
las sondas de Clip del estado 11. Con PEXEW corregido, Y y Z de la esfera deberían dejar de coincidir y
algunos modelos deberían pasar Clip.

### Control integrado de GS, MMI y VU0 (2026-10-06)

Se integra `origin/main` en `0421ec4` mediante merge, conservando los parches de Opus
y los dos nuevos parches GS de constantes y coordenadas. Los 33 parches reproducen
exactamente las 78 fuentes modificadas del runtime. Se regeneran las 6418 unidades
y se recompila el ejecutable completo. MSVC interrumpe inicialmente `libsd.cpp` sin
emitir diagnóstico; la reanudación compila esa unidad y enlaza correctamente sin
cambiar fuentes. La suite nativa integrada pasa **545/545**, incluidas ocho pruebas
OpenGL en Windows; la configuración y los 14 scripts PowerShell pasan sus controles.

El control OpenGL de 175 s alcanza el estado 11. En sus primeras 64 llamadas a Clip
desde ProcessModel, las esferas son finitas y **ninguna tiene Y y Z con bits iguales**.
**30 devuelven `0` y 34 devuelven `0x80000000`**, frente a 64 rechazos y 64 pares Y/Z
idénticos en el control previo. La muestra abarca once modelos; no es un censo de
toda la escena. El modelo `0xD51C80`, grupo `0xD599C0`, pasa Clip en sus cinco muestras.
Los descartes restantes pueden corresponder a objetos fuera de la vista y no se fuerzan.

Las posiciones observadas también cambian: se actualizan buffers de primitivas que
sí pertenecen a los contextos, desde llamadas como `0x1644E0`, con datos no nulos.
Esto supera el diagnóstico anterior limitado a la plantilla del loader y a los
buffers de `0x1FB800`; no certifica que toda la geometría enviada sea correcta.

La captura a 138,75 s del reloj PAD está en estado 11 (`pending=0`, `levelReady=1`,
`flashReady=0`) y muestra polígonos/texturas deformados. La de 110,02 s aún está en
estado 4 y muestra textura naranja y triángulos grandes. Se inicializa OpenGL real
en la RX 5700 XT, sin fallback. **Kratos y la escena completa siguen pendientes**;
pasar Clip no equivale a una partida jugable. Los registros y capturas quedan solo
en `logs/integrated_mmi_gs_game/`, excluidos del repositorio.

El siguiente control detecta paradas de VU1 en `pc=0x288`, instrucción `0x8040FFFE`.
La tabla LowerOP de [PCSX2 fijada en `32ac6e2`](https://github.com/PCSX2/pcsx2/blob/32ac6e23e4aaf8c8c5e74a6c1ed750ee7672120e/pcsx2/VUops.cpp)
la identifica como EEXP; el runtime la considera reservada. También están cruzados
los códigos de ERSQRT, ESIN y EATAN. Se preparará un parche VU1 separado con pruebas
de instrucciones binarias explícitas, sin modificar las fórmulas EFU existentes.
El perfil previo a ese arreglo incluye los errores emitidos por esas paradas y
no debe describirse como rendimiento de una escena renderizada correctamente.

El script de rendimiento elimina también variables de diagnósticos experimentales
de geometría y replay, y restaura sus valores al terminar.

El perfil OpenGL de 240 s, sin capturas ni diagnósticos opcionales y sin otras pruebas
o compilaciones concurrentes, confirma estado 11 antes del intervalo medido. Sus ocho
ventanas completas de 180,31 a 220,38 s dan **1,00 llamadas a `vid::Flip`/s** y
**59,11 presentaciones/s**. El tiempo exclusivo transcurrido del hilo del juego se
reparte en IOP 53,18 %, VU 40,59 %, EE 3,69 % y envío GS 2,53 %. Incluye los errores
VU1 emitidos por defecto; GS no mide aquí la ejecución del worker OpenGL. No son
porcentajes de utilización de CPU/GPU. El control antiguo de 3,45 Flip/s descartaba
todos los modelos observados: el trabajo ejecutado ahora cambió y no permite atribuir
la diferencia al parche IOP. Se repetirá el perfil después de corregir EFU.

### Decodificación EFU de VU1 (2026-10-06)

`ps2recomp-vu1-efu-opcodes.patch` separa un fallo anterior del intérprete: según la
tabla LowerOP de PCSX2 enlazada arriba, ERSQRT usa `0x79`, ESIN `0x7C`, EATAN `0x7D`
y EEXP `0x7E`; `0x77` está reservado. El runtime utilizaba respectivamente `0x77`,
`0x79`, `0x7C` y `0x7D`. Esto detenía los microprogramas de GoW que ejecutan EEXP en
`pc=0x288` y seleccionaba otra fórmula para las tres instrucciones anteriores.

Se corrigen tanto la ejecución como la decodificación usada por la caché y el
scheduler, con las latencias de las operaciones ya existentes. Se conservan las
fórmulas, el acceso a P y la restricción de EFU en VU0. Tres regresiones nuevas usan
palabras binarias explícitas: comprueban resultados y WAITP/continuación de las cuatro
instrucciones, el rechazo de `0x77` y que EEXP siga reservado en VU0. Dos fixtures
anteriores de EATAN y latencias EFU se ajustan a las codificaciones correctas sin
relajar sus aserciones. Con esos fixtures, cuatro pruebas fallan antes del cambio;
con el arreglo pasan **540/540** sin activar las ocho pruebas OpenGL.

La compilación completa con los 34 parches vuelve a regenerar las 6418 unidades y
termina correctamente. La auditoría posterior compara 78 fuentes sin diferencias;
la suite nativa pasa **548/548**, incluidas las ocho pruebas OpenGL. Los cuatro fallos
previos quedan corregidos y los casos de instrucciones reservadas siguen cubiertos.

El control OpenGL de 300 s llega a estado 11 y registra **cero instrucciones VU1
reservadas** durante toda la ejecución. Conserva los resultados de las 64 muestras
de Clip (30 pasan, 34 descartadas; todas finitas, sin Y/Z duplicadas). Guarda 14
capturas distintas; las de 190,71 y 240,36 s del reloj PAD están en estado 11, con
`pending=0`, `levelReady=1` y `flashReady=0`. Muestran más geometría y texturas que
el control anterior, pero con polígonos enormes, franjas y deformaciones. Kratos
no es reconocible y la escena completa sigue pendiente. Los datos quedan en
`logs/vu1_efu_opcodes_game/`, excluidos de Git.

El perfil posterior de 240 s, sin capturas ni diagnósticos opcionales y sin otras
compilaciones/pruebas concurrentes, mide ocho ventanas completas de 180,06 a 220,07 s
en estado 11: **1,25 Flip/s y 59,99 presentaciones/s**. El tiempo exclusivo transcurrido
del hilo del juego es VU 52,36 %, IOP 41,25 %, GS frontend 3,77 % y EE 2,62 %. Este
control ejecuta más geometría y elimina las paradas/errores VU1 anteriores; el 1,00
Flip/s previo no corresponde a una escena correcta. Se confirma el coste del perfil
con una segunda ejecución de contadores de cuadros y se conserva CPU como siguiente
control para localizar las deformaciones frente a OpenGL.

La repetición contando solo cuadros (`GOW_PERF_DIAG=frames`) también está en estado
11 durante el intervalo: ocho ventanas completas de 180,05 a 220,06 s, con **1,275
Flip/s y 59,99 presentaciones/s**. Son 51 Flip en 40 s frente a 50 con el perfil completo;
esta diferencia pequeña entre ejecuciones no permite medir un overhead exacto, pero
descarta que la instrumentación opcional explique por sí sola la baja frecuencia
observada. Los tiempos en cero de este modo significan contadores desactivados,
no ausencia de trabajo. Los dos registros se conservan en `logs/vu1_efu_opcodes_perf/`
y `logs/vu1_efu_opcodes_frames/`.

El control CPU posterior de 300 s también llega a estado 11 y no registra
instrucciones VU1 reservadas. Conserva las mismas 64 observaciones de Clip (30
pasan, 34 descartadas, todas finitas, sin Y/Z duplicadas). Las capturas tardías
siguen mostrando polígonos desproporcionados y superficies deformadas; Kratos no
es reconocible. Este fallo también ocurre con CPU, aunque las imágenes de dos
ejecuciones independientes no permiten una comparación por pixel ni excluyen un
problema en el frontend GS común. El siguiente control debe repetir los mismos
comandos y el mismo estado inicial en ambos backends. Datos privados:
`logs/vu1_efu_opcodes_cpu_game/`. La CI del commit `94c9389` termina en verde.

### Estado CPU y repetición de comandos GS (2026-10-06)

Para comparar la misma geometría y los mismos comandos se añade
`ps2recomp-gs-cpu-state.patch`, separado de los arreglos anteriores. El CPU original
no implementaba `GSBackendStateAccess`: guardar solo su VRAM perdía paleta, caché
de textura, transferencia y readback. Se adapta la exportación/importación de
Taylor N. Albarnaz / LightVelox, fork SotC `ac9efa0` (GPL-3.0), conservando la caché
original y los bytes CT24 pendientes de este port. La importación valida antes de
modificar el destino; una página fuera de VRAM, desalineada, un pixel CT24 inválido
o un cursor fuera del buffer se rechazan.

Tres regresiones fallan antes del cambio (**540/543**): transferencia CT24 cortada
en dos bytes con CPU directo y con hilo, paleta/caché/readback conservados y rechazo
sin modificar el estado. Tras el arreglo pasan **551/551**, incluidas las ocho
pruebas OpenGL. La sonda de caché usa TBP0 en bloques de 256 B y comprueba un texel
cacheado distinto del contenido de VRAM; el arreglo no invalida esa página para
disimular diferencias. La auditoría previa verifica **35 parches y 79 fuentes**
exactamente. La compilación completa y el control del juego se verifican abajo.

La compilación completa posterior regenera las 6418 unidades y termina correctamente;
la auditoría vuelve a comparar las 79 fuentes sin diferencias. La suite nativa sobre
ese runtime pasa **551/551**. El diagnóstico final inicia la captura únicamente desde
comandos de dibujo/transferencia del EE, para evitar leer sus variables desde el hilo
de presentación. Tras ese ajuste de `src/`, la recompilación rápida y el test sintético
también pasan. Las rutas Unicode absolutas se verifican en Windows.

La captura sintética coincide también con OpenGL real: cero bytes distintos tanto con
compute como con la opción hardware habilitada. Esta muestra pequeña suma un tile
compute en ambas ejecuciones: no demuestra que el segundo control use rasterizado
hardware. Los registros quedan en `logs/gs_replay_synthetic_*`; la captura del tramo
real de GoW se verifica a continuación.

El diagnóstico opcional de `src/gow_gs_replay.h` conserva los comandos, MXCSR,
paleta, transferencia y VRAM inicial/final. La herramienta independiente repite
esa entrada en CPU y OpenGL y puede localizar la primera divergencia. Tiene
límite de tamaño/duración y no se instala sin `GOW_GS_REPLAY_TRACE`; el perfil
elimina esa variable. El test sintético verifica captura completa, parser y rechazo
de truncamiento, y la repetición CPU obtiene **cero bytes distintos**. El estado
completo, uso y límites del formato se describen en `RENDERIZADO.md`.

El control CPU de 200 s llega a estado 11 y guarda una captura completa de comandos
de 23,89 MB. Repite **47.207 envíos de primitivas y dos presentaciones** con cero
bytes distintos respecto a la VRAM final original; también coinciden paleta,
transferencia, bytes CT24 pendientes y readback. Repetirla nuevamente en CPU conserva
las dos imágenes sin diferencias. La página de textura inicial está invalidada,
por lo que no hay una página CPU antigua que impida importar esta entrada en GPU.

OpenGL real en la RX 5700 XT diverge por primera vez en el **registro 577, dibujo
561**, un triángulo texturado con STQ y PSMT8: solo tres bytes de VRAM difieren en
ese punto. Al final difieren **166.763 bytes con compute** y **164.227 con hardware
habilitado**; las dos presentaciones difieren. El segundo recorrido mezcla hardware
y compute (7.749 tiles compute frente a 11.937 del primero), y el log confirma que
hardware se activa. No se interpreta esta ejecución mixta como uso exclusivo de
rasterizado hardware. Estos códigos de salida 1 indican una divergencia localizada,
no un fallo del parser ni pérdida de la captura.

Las 12 capturas del control siguen mostrando polígonos enormes y franjas; Kratos
no es reconocible. Las 64 muestras de Clip siguen finitas, con 30 aceptadas y 34
descartadas, sin Y/Z duplicadas, y no hay instrucciones VU1 reservadas. La repetición
permite aislar precisión/interpolación en GS; no demuestra que esas diferencias
expliquen toda la deformación que también aparece en CPU. El siguiente paso es
comprobar el efecto del modo de redondeo del host y reducir el primer dibujo distinto
a una prueba sintética. Los comandos, coordenadas, VRAM e imágenes originales quedan
únicamente en `logs/gs_cpu_state_game/`, excluidos de Git.

La CI del commit `681c257` pasa ambos jobs. La captura y repetición ya se pueden
reproducir con la herramienta pública, sin publicar los volcados originales.

### Aislamiento del redondeo SSE en el renderer CPU (2026-10-06)

Se confirma una dependencia del backend CPU respecto al modo MXCSR del productor.
VU ejecuta con redondeo hacia cero y envía comandos GS dentro de esa ejecución;
la cola GS también conserva MXCSR. En la captura anterior **46.950 de los 47.207
dibujos** llegan con ese modo, incluido el primero que difiere de OpenGL. Una sonda
sintética de 24 combinaciones de gradiente/UV/STQ, CPU directo/con hilo y cuatro
modos falla en cuatro casos de gradiente: cambian 178 bytes de color por caso.
UV/STQ de esa muestra no cambian; no se generaliza ese resultado a toda textura.

Se aísla además el dibujo real 561 tras repetir todos sus comandos anteriores.
La página CPU cacheada coincide con VRAM. Cambiar únicamente el modo CPU modifica
cuatro bytes entre más cercano y hacia cero; OpenGL compute difiere en un byte
respecto al CPU con más cercano. Esto confirma la influencia de MXCSR en ese
dibujo, pero todavía queda una diferencia de precisión y no se certifica paridad
completa. Las ejecuciones con hardware habilitado de esta sonda pequeña usan
compute (129 tiles); el control mixto completo anterior sigue siendo el que
verifica activación hardware.

`ps2recomp-gs-cpu-rounding.patch` delimita el modo SSE al entrar en `Submit`: usa
el redondeo habitual al más cercano del rasterizador CPU y restaura el del llamador
al salir. Conserva máscaras, FTZ/DAZ y flags; no cambia EE, FPU, IOP ni la ejecución
VU. Es una estabilización del renderer aproximado, no una afirmación de que la
interpolación actual tenga la precisión exacta del GS de PS2. Dos regresiones nuevas
comprueban independencia con CPU directo y con hilo, y conservación del control
MXCSR. Antes del arreglo fallan ambas (**543/545** sin OpenGL); después pasan
**553/553**, incluidas las ocho pruebas OpenGL. La auditoría verifica **36 parches
y 79 fuentes** sin diferencias; los scripts y la configuración pasan, con los
cuatro avisos conocidos.

La compilación completa de los 36 parches termina correctamente y vuelve a generar
las 6418 unidades. La auditoría posterior conserva las 79 fuentes exactas y la suite
nativa pasa **553/553**. La sonda independiente de 24 combinaciones pasa ahora todas,
sin alterar el modo del llamador; la herramienta de captura se reconstruye y su
test sintético también pasa. El primer control de 200 s no llega a iniciar la
captura en estado 11 y queda inconcluso; no sirve para comparar renderers.

Tras el arreglo de libm de Claude (`3f349b2`), la compilación completa conjunta
vuelve a aplicar los 36 parches y ejecuta las funciones originales double de
`sin`, `fabs` y `floor`. La auditoría verifica las 79 fuentes exactas. El control
CPU de 330 s alcanza estado 11 con `pending=0` y guarda una captura completa de
**53.016 dibujos y tres presentaciones**. Repetirla con CPU reproduce exactamente
VRAM final, estado GS y las presentaciones. Compute y el recorrido mixto divergen
primero en un sprite (dibujo 510, registro 520), con 917 bytes distintos; al final
difieren en 387.242 y 386.914 bytes respectivamente y en las tres presentaciones.
El recorrido mixto activa hardware y ejecuta 17.679 tiles compute, frente a 42.001
en compute puro. Son comparaciones de la misma entrada; no son mediciones de FPS.

Las imágenes CPU de 160,48 y 191,27 s muestran cubierta, acantilados, cielo y lluvia
reconocibles, mejorando claramente el control anterior a libm. Una imagen posterior
vuelve a mostrar deformaciones; todavía no se verifican Kratos ni una partida
jugable. Se mantienen los controles originales en `logs/gs_camera_libm_cpu_game/`,
excluidos de Git, y se continúa investigando los desacuerdos GS y los modelos.

El análisis completo de la captura anterior verifica 141.396 vértices finitos. Hay
5.284 primitivas con XYZ completamente en cero, todas con estado PSMT8; los Q de
magnitud mayor de `1e10` usados por STQ aparecen solo en 5.260 de estas primitivas
degeneradas. Esto localiza posiciones cero en la salida final GS, pero no identifica
todavía su contexto de modelo ni demuestra que deban dibujarse. También se observan
210 vértices STQ con Q negativo; no se fuerza su signo. Las observaciones anteriores
de Clip y de miembros no nulos siguen válidas para los contextos que se midieron.

Una sonda privada de 12 triángulos sintéticos grandes reproduce diferencias de Z en
tres casos con el shader actual. Un borrador que conserva el recíproco y los
numeradores enteros en double antes de obtener pesos float elimina esas diferencias
en los 12 casos y en el dibujo real aislado 561. Ese borrador todavía no forma parte
del ejecutable de ese control; se convierte después en el parche separado que se
describe a continuación.

### Precisión de pesos OpenGL y profundidad PACKED Z32 (2026-10-06)

`ps2recomp-gs-triangle-precision.patch` conserva el recíproco del área en double en
dos palabras libres del registro de primitiva. El shader común reconstruye los
numeradores enteros en double dentro del dominio GS y convierte los pesos a float
solo después de multiplicar por ese recíproco, como CPU. `Submit` prepara primitivas
con redondeo SSE al más cercano y restaura los controles del productor. Se conserva
el stride de 40 palabras y el shader hardware generado usa el mismo cálculo.
La regresión compara VRAM completa CPU/OpenGL para dos áreas grandes, dos XYOFFSET,
Z32/Z24 y cuatro modos SSE, con compute y hardware permitido. Falla en el renderer
anterior (**553/554**) y pasa con el arreglo (**554/554**). La sonda privada de doce
casos y el triángulo real aislado anterior también dejan de diferir. Esto mejora
la precisión de GS; todavía no demuestra una ganancia de FPS ni elimina todos los
desacuerdos de la nueva captura.

`ps2recomp-gs-packed-depth.patch` corrige otro fallo antes del backend: PACKED XYZ2
y XYZ3 convertían su Z32 a float antes de almacenarlo en `GSVertex.z` (double).
Por ejemplo, `0x01000001` perdía un bit y `0xFFFFFFFF` acababa escribiéndose como
cero. Ambas rutas convierten directamente a double; XYZF de 24 bits, REGLIST y A+D
conservan su comportamiento. Dos regresiones comprueban cuatro profundidades en
la ruta nativa y en los 33 cortes de bytes del paquete, además de la cola de XYZ3
y XYZ2 con ADC sin dibujo prematuro. Antes del arreglo fallan ambas (**545/547**
sin las pruebas OpenGL); después pasan **556/556**, incluidas las nueve OpenGL.
La auditoría integrada reconstruye **38 parches y 79 fuentes exactas**;
configuración y 14 scripts pasan con los cuatro avisos conocidos. La compilación
completa conjunta termina correctamente, genera las 6418 unidades y conserva las
79 fuentes exactas al auditarlas después de compilar. Sigue el control del juego.

La suite reconstruida después de esa compilación pasa **556/556**. El helper público
compila las tres herramientas y pasa la captura/repetición sintética. CPU reproduce
de nuevo los 53.016 dibujos originales sin diferencias. Compute termina con
386.873 bytes distintos, frente a 387.242 antes; el recorrido mixto termina con
390.713, frente a 386.914. Ambos conservan la primera diferencia de feedback (917
bytes en el sprite 510) y las tres presentaciones distintas. La mezcla de rutas
hardware/compute también cambia (17.783 tiles compute frente a 17.679 antes): no
se atribuye su variación final exclusivamente a precisión ni se declara paridad
hardware. El control OpenGL del juego usa el ejecutable completo de estos cambios.

El control OpenGL de 330 s alcanza estado 11 con `pending=0` y `levelReady=1`,
en RX 5700 XT con hardware activo y sin fallback. La única PPM de partida es la
de 249,35 s: se reconocen cielo, lluvia y escenario al fondo, pero un objeto oscuro
grande tapa el centro y Kratos no es identificable. Las anteriores permanecen en
menú o transición; no se comparan como la misma fase del control CPU de 160/191 s.
Las 64 muestras de Clip conservan los resultados anteriores y los 66 buffers
tardíos observados tienen primeras tripletas no nulas. No aparecen instrucciones
VU reservadas, rutas inválidas, ciclos ni truncamientos. Sigue sin certificar una
partida jugable o una mejora de FPS.

La revisión final protege también los diagnósticos frente al fallback GPU: la cola
puede destruir el backend OpenGL al sustituirlo por CPU, por lo que las herramientas
y la nueva regresión verifican primero `Inner()` antes de consultar `IsReady()`.
No cambia el rasterizado. La segunda compilación completa requerida para publicar
la corrección del test termina correctamente; la suite vuelve a pasar **556/556**
y la auditoría posterior conserva las 79 fuentes exactas.

El control de cámara conjunta muestra 64 Clips finitos y completos: 22 resultados
`0`, seis `20` y 36 descartes `80000000`, sin Y/Z duplicados. Las 544 sondas tardías
de `InitUNPACKData` tienen una primera tripleta no nula. Hay 10.520 primitivas
degeneradas con todos sus XYZ cero; los S enormes aparecen exclusivamente en ellas,
sin XYZ no finitos. Esto no respalda forzar posiciones a otro valor. No aparecen
rutas inválidas ni ciclos en las muestras de modelos/GROB. Los planos oscuros de
240,85 s quedan pendientes de comparar con el mismo cuadro de referencia para
distinguir encuadre, oclusión y geometría defectuosa.

La primera diferencia de la nueva traza se reduce a un sprite bilineal que lee y
escribe el mismo framebuffer CT32. La sonda privada aislada reproduce los 917 bytes
en 476 píxeles de 24 filas, concentrados junto a límites de página; repetirla da el
mismo resultado. Con nearest no difiere, TEXFLUSH previo conserva la diferencia,
y copiar la fuente a una región disjunta la elimina. Esto localiza una diferencia
de feedback/caché CPU frente a OpenGL. La referencia PCSX2 distingue explícitamente
estos casos de lectura del destino, pero no demuestra cuál de nuestras salidas
reproduce el GS; no se fuerza paridad cambiando el filtrado ni anulando la caché.

El caso se reduce además a una textura procedural CT32 de 64×64: CPU y GPU
difieren en 63 píxeles (186 bytes), únicamente en las filas 32 y 33. CPU mutable
difiere igual de su control con fuente disjunta, mientras OpenGL coincide con
ese control en las dos repeticiones. Los 53.248 vecinos bilineales del caso real
también se verifican después de CLAMP, incluidos los negativos remapeados a cero.
La nueva herramienta pública `tools/render/comparar_feedback_gs.cpp` reproduce
el caso sintético y se compila con `scripts\compilar_replay_gs.cmd`; no utiliza
datos del juego. El siguiente paso requiere una referencia GS para este patrón
de lectura y escritura antes de cambiar la política de caché.

### Revisión del trabajo de Claude y comparación visible (2026-10-07)

El checkout `E:\gowclaude\repo` está limpio en `1bdcd72` y coincide con el nuevo
`origin/main`: siete commits posteriores a `1968f03`. Son cambios separados de
getenv en bucles calientes, rendimiento VU1, DMA del scratchpad, entrada MPEG,
diagnóstico PCM, sonido IOP y memory card. La mejora VU1 tiene un banco de pruebas
del mismo cuadro y salida idéntica: 775 a 503 ms; no equivale a una mejora del 35 %
de FPS de la partida. El arreglo de scratchpad explica la paleta de huesos de
Kratos que llegaba cero a VU1. Las capturas locales de Claude muestran a Kratos,
enemigos y HUD con formas reconocibles, y las del guardado llegan a «Save Complete».
Sus resultados y controles están en `COMPARACION_PCSX2.md`, `RENDIMIENTO.md`,
`FMV_Y_AUDIO.md` y `MEMORY_CARD.md`. Falta verificar el ejecutable conjunto con
los dos nuevos arreglos GS; se conserva la referencia anterior como histórica.

La revisión de `repetir_gs` detecta además que CPU reserva filas de 640×512 y GPU
devuelve filas de 640×alto visible. Comparar los vectores brutos podía marcar una
diferencia de tamaño aunque los píxeles visibles fueran iguales, y el exportador
rechazaba la PPM de CPU. `gs_frame_pixels.h` exige el layout explícito del backend,
extrae solo la imagen visible y rechaza buffers truncados o ambiguos. Dieciocho
controles sintéticos, incluido `GSCpuBackend::Present` real de 64×64 y la PPM RGB,
pasan; la captura/repetición CPU sintética vuelve a quedar exacta. Las cifras de
VRAM anteriores no cambian; las comparaciones de presentación brutas se vuelven
a comprobar con esta herramienta antes de considerarlas diferencias de imagen.
La repetición compute completa de la traza histórica confirma ahora tres imágenes
visibles distintas, y exporta tanto CPU como GPU (cuatro PPM válidas). CPU vuelve a
reproducir VRAM final exactamente. Este control usa el modo por lotes: sus 387.310
bytes finales no se comparan como si fuera la ejecución con sincronización por
dibujo anterior. En modo por lotes se etiqueta el primer control de VRAM distinto,
sin presentarlo como la posición de la primera divergencia real.

La integración conserva los siete parches de Claude antes de los dos arreglos GS:
**45 parches y 84 fuentes exactas** al reconstruir y comparar el árbol. La
compilación completa regenera las 6418 unidades y termina correctamente. Después
pasan **565/565 pruebas**, incluidas las nueve OpenGL y las nuevas regresiones de
SPR, MPEG, sonido y tarjeta. Las cuatro herramientas de renderizado se recompilan;
sus 18 controles de imágenes, captura/repetición CPU sintética y sonda de feedback
procedural conservan los resultados esperados.

El control conjunto OpenGL de 225 s carga una partida de «Docks of Athens» desde
una **copia** de la tarjeta ECC de Claude, con el original comprobado por SHA256
sin cambios. RX 5700 XT usa hardware sin fallback. La última captura de 512×448
muestra a Kratos en el punto de guardado, el HUD y la indicación R2; alcanza
estado 11 sin carga pendiente. No prueba todavía combate, rendimiento sostenido
ni todos los escenarios. Las 128 muestras de Clip entre menú y partida son finitas
y completas; las 144 sondas tardías de `InitUNPACKData` tienen una primera tripleta
no nula. Los 64 contextos de modelos de partida no contienen direcciones inválidas,
ciclos ni truncamientos, y no se registran instrucciones VU reservadas.

El treemap de SDK y hardware y los tres README reflejan ahora estos controles:
MPEG/IPU pasan a parcial por la intro decodificada; tarjeta y audio detallan qué se
verificó y qué falta. GS queda parcial por las diferencias CPU/OpenGL todavía
abiertas. El 65,4 % de librerías y 60,5 % de hardware representan las ponderaciones
del mapa, no el porcentaje del juego completado ni su velocidad.

La repetición hardware con tres pasadas en una sola instancia GL certifica la
restauración antes de comparar: VRAM inicial, CLUT/CBP, transferencias y estado
portable exactos en las tres; CPU inicial/final con caché exacta y CPU frente a la
captura final sin diferencias. Las dos pasadas calientes usan ambas 373 lotes,
33.445 primitivas y cero tiles compute, pero difieren entre sí en **4617 bytes de
VRAM** y en **344/298/296 bytes de las tres imágenes visibles**. Ya no se trata de
comparar tamaños distintos de buffers. La pasada fría mezcla compute durante la
compilación de variantes, por lo que no se usa como control de la misma ruta.
Sigue la investigación del orden de acceso a VRAM y de los buffers de subida;
estos tiempos de repetición tampoco son una medición de FPS del juego.

El nuevo `repetir_gs --repeticiones N` lleva ese control al tool público. Reutiliza
las mismas instancias, comprueba la restauración completa y compara VRAM, estado
y todos los cuadros visibles entre pasadas consecutivas. Los deltas de raster
distinguen las pasadas que aún usan compute mientras compilan variantes. Se
corrige también el directorio de salida como tercer argumento y se detectan
opciones inválidas y fallos al exportar, manteniendo los códigos de salida.
El helper completo y sus 18 controles pasan; CPU sintético ×3, compute ×3 y
hardware ×8 conservan paridad y estabilidad. CPU del primer cuadro real ×3 es
exacto y exporta seis PPM válidas; hardware ×8 detecta 545 bytes VRAM y 358 visibles
distintos entre las dos últimas, ambas con cero tiles compute.

La búsqueda con cortes CPU válidos estrecha el caso al segundo sprite de feedback:
519 (antes) y 520 (primer sprite) son repetibles; al incluir 521 aparecen 36 bytes
variables con 3 lotes, 192 primitivas y cero tiles compute. La restauración y el
estado portable final siguen exactos. La diferencia CPU/GPU de 917 bytes del primer
sprite es estable y se distingue de esta variación. Cero reutilizaciones del ring
de subida en la captura descartan ese reciclado como causa; todavía se necesita
una referencia para decidir el tratamiento correcto de lecturas del destino.

Cuatro capturas procedurales comprueban también estados iniciales que el control
simple no cubría: dos bytes CT24 pendientes, lectura local con cursor 7 y nueve
bytes pendientes, página de caché fresca y página con un byte obsoleto. Las cuatro
son exactas en CPU ×3, incluida la imagen visible; las tres compatibles con GPU
son exactas en compute ×3 y la obsoleta se rechaza con código 2 antes de crear GL.
El directorio como argumento 3 exporta las imágenes correctas; un archivo usado
como directorio y nueve variantes de opciones inválidas también devuelven 2.

El nuevo generador `tools/render/generar_feedback_gs.cpp` reduce la inestabilidad
a dos sprites sobre texels procedurales de 64×416, sin archivos del juego. Genera
feedback bilineal, fuente disjunta y feedback nearest, verificando los 27.588
vecinos copiados. El helper recompila ahora cinco herramientas, pasa los 18
controles de imágenes y repite las tres capturas en CPU ×3 con End, VRAM y cuadro
exactos. En RX 5700 XT, hardware ×12 de fuente disjunta y nearest conserva paridad
y estabilidad; feedback bilineal devuelve 1 y varía entre pasadas hardware con
dos lotes, dos primitivas y cero tiles compute (19 bytes VRAM/visibles en 4→5 y
34 en 7→8). Restauración y estado portable final permanecen exactos. Así se puede
reproducir el problema independientemente del juego antes de elegir un arreglo;
no se cambia todavía la política del renderer ni se atribuye una mejora de FPS.

La referencia independiente de PCSX2 v2.8.2 software descubre otro problema:
el filtro bilineal redondeaba al final, mientras la referencia usa fracciones de
cuatro bits y trunca cada etapa. `ps2recomp-gs-bilinear-precision.patch` cambia
solo GS CPU/GLSL y añade tres regresiones con una huella RGBA externa de 256
fracciones UV, también para STQ y cuatro modos SSE. Las tres fallan antes del
arreglo y después pasan **568/568**, incluidas diez OpenGL reales. El control
literal bilineal de una prueba anterior se ajusta al truncado documentado;
nearest no cambia. La compilación completa de las 6418 unidades termina bien
y el árbol reconstruido coincide en sus **46 parches y 84 fuentes**.

El helper recompila seis herramientas y conserva los 18 controles de imágenes
y las tres repeticiones CPU ×3 exactas. `generar_feedback_gs --pcsx2` añade
exportación procedural GS freeze v8/PATH3; la nueva prueba comprueba el freeze
inicial y reproduce los comandos GIF con VRAM final exacta en los tres casos.
Los `.gs` exportados son idénticos por SHA256 a los usados en la referencia
aislada, sin BIOS ni datos del juego. La comparación con PCSX2 software queda
exacta para nearest y, tras el arreglo, para bilinear con fuente disjunta.
Los detalles y fuentes están en [RENDERIZADO.md](RENDERIZADO.md).

En RX 5700 XT, hardware ×12 sigue exacto y estable para fuente disjunta y nearest.
Feedback bilineal aún devuelve 1 y varía: las dos últimas pasadas difieren en
89 bytes de VRAM y de imagen visible, con dos lotes, dos primitivas, cero tiles
compute y estado portable final igual. No se cambia la política de caché ni
se atribuye una mejora de FPS. El treemap incorpora la precisión verificada
y conserva GS/texturas en parcial y sus porcentajes de cobertura anteriores.

El control OpenGL final de 330 s carga «Docks of Athens» desde una copia privada
de la tarjeta ECC, cuyo original sigue idéntico por SHA256. Las 14 capturas son
distintas y válidas; la última muestra a Kratos, el HUD, el punto de guardado y R2,
en estado 11 sin carga pendiente. Las 128 muestras de Clip son finitas y completas;
las 352 sondas tardías de `InitUNPACKData` tienen una primera tripleta no nula,
y las 192 muestras de contextos de partida no contienen punteros inválidos,
ciclos ni truncamientos. No aparecen instrucciones VU reservadas. No prueba
todavía combate ni FPS sostenidos. Se retrasan las pulsaciones del guion privado:
el primer control de 225 s perdió el Start inicial y acabó en la intro de partida
nueva, por lo que no se usa como prueba de carga guardada.

El siguiente control de referencia confirma un fallo distinto con nearest STQ:
el cast directo a entero pierde el signo de las fracciones negativas. El nuevo
`ps2recomp-gs-nearest-stq.patch` convierte primero a 16.16 y extrae la parte entera
con signo, tanto en CPU como en GLSL. Dos matrices de 16×16 procedurales comparan
cuartos de texel negativos y los límites próximos a cero: el original difiere
de PCSX2 software en 624 bytes RGBA/156 píxeles en cada una. Usar solamente
`floor` falla en los valores que pierden la fracción durante la conversión fija.
Las tres regresiones nuevas fallan antes y pasan después para sprites/triángulos,
Q=1/2 y los cuatro modos SSE. Se confirman también las rutas OpenGL por contadores.

La compilación completa vuelve a regenerar las 6418 unidades y termina bien;
**47 parches y 84 fuentes** coinciden, y la suite posterior pasa **571/571** con
once controles OpenGL reales. El helper compila siete herramientas: conserva
los 18 controles de imágenes y añade un generador público de los tres oráculos.
Sus `.gs` son idénticos por SHA256 a los usados en PCSX2 software; sus matrices
CPU coinciden byte por byte con las tres referencias RGBA externas. Los seis
patrones pasan freeze/GIF/End y CPU ×3. Hardware ×8 de los tres oráculos queda
exacto y estable, con 256 primitivas y cero tiles compute en las pasadas calientes.
Los tres dumps anteriores de feedback conservan sus bytes. El treemap refleja
ambos arreglos de muestreo sin cambiar la cobertura parcial ni atribuir FPS.

El control final OpenGL de 330 s con este ejecutable carga de nuevo la copia
privada de «Docks of Athens», con hardware activo y la tarjeta original intacta.
Las 14 capturas son distintas; la última muestra a Kratos, HUD, R2 y punto de
guardado en estado 11 sin carga pendiente. Se conservan 128 muestras de Clip
finitas/completas, 352 primeras tripletas tardías no nulas y 192 contextos de
partida sin punteros inválidos, ciclos ni truncamientos. No se registra VU
reservada. Combate, rendimiento sostenido y feedback bilineal siguen pendientes.

### Feedback GS: controles separados y fuente protegida opcional (7 de octubre)

Se conserva la rama experimental VU1 de Opus sin integrarla ni modificar EE/IOP.
El generador GS añade seis controles a los tres patrones procedurales anteriores:
TEXFLUSH o SCISSOR1 fuera del área dibujada entre dos sprites. Los nueve pasan
freeze/GIF/End y CPU ×3. La validación del dump comprueba ahora también el freeze
completo y los tres bloques privilegiados, para detectar cambios en PMODE,
DISPLAY, contextos, colas GIF y Q que no alterarían la VRAM final.

PCSX2 software v2.8.2 conserva una fuente común en `self`/`self_texflush`; cambiar
SCISSOR1 fuerza dos dibujos y renueva la fuente. Las fuentes de ambos dibujos se
verifican contra el patrón y el primer framebuffer, respectivamente. El resultado
separado difiere en 1238 bytes RGB, demostrando que copiar por primitiva no equivale
a congelar un lote entero. Este control no certifica el pipeline del GS real.

El nuevo parche `ps2recomp-gs-feedback-snapshot.patch` conserva la fuente antes
de cada primitiva con lectura/escritura solapada, usando las páginas sombra del
backend OpenGL y el epoch anterior a la copia. Es opcional, desactivado por
defecto, y se selecciona con `PS2X_GS_FEEDBACK_SNAPSHOT=1` en el ejecutable o
`--snapshot-feedback` en la herramienta de repetición. El renderer CPU conserva
su caché de una página de 8 KiB; la herramienta sigue denunciando sus diferencias.

Antes del cambio, la nueva regresión hardware falla tanto por RGB como por
variación entre pasadas (572/573); compute ya pasa ese patrón. Después pasa
573/573 con trece controles OpenGL reales. Ambas rutas reproducen la huella externa
del caso separado, mantienen estable toda la VRAM restaurada, conservan alpha
y la fuente disjunta y pasan los controles de puntos/triángulos contra la copia
CPU disjunta. El modo experimental todavía no está habilitado por defecto y
no demuestra un aumento de FPS. El treemap conserva cobertura parcial y pesos.

La compilación completa regenera las 6418 unidades y termina con código 0.
La auditoría posterior confirma los 48 parches/84 fuentes, y la suite vuelve
a pasar 573/573. El helper compila siete herramientas y valida los 18 controles
de imagen y los doce patrones freeze/GIF/End con CPU ×3; seis dumps dañados se
rechazan. Los tres dumps originales siguen idénticos por SHA256.

Las 72 pasadas de repetición opcional (compute/hardware × tres patrones ×12)
mantienen VRAM, estado y cuadro visible idénticos entre restauraciones y coinciden
con las tres regiones RGB de referencia. Hardware caliente confirma dos
primitivas y cero tiles compute. El caso separado conserva su salida 1 por los
4557 bytes que difieren de la caché CPU; nearest y fuente disjunta conservan
salida 0. Se rechazan también la opción duplicada y su uso en modo CPU.

El control del juego con la opción activada dura 390 s y carga una copia privada
de la tarjeta de Claude, cuyo original conserva su SHA256. Las quince capturas
son distintas y válidas (512×448); la última muestra a Kratos, el punto de guardado
y R2, en estado 11 sin carga pendiente y con hardware activo. Las 128 muestras
de Clip son finitas y completas, las 416 primeras tripletas tardías de
`InitUNPACKData` son no nulas y los 192 contextos de partida no presentan punteros
inválidos, ciclos ni truncamientos. No se registra VU reservada. La barra de vida
no aparece en esta captura final; combate y FPS sostenidos siguen sin comprobar.
El primer guion de 330 s perdió la selección «Load» y llegó a la intro de una
partida nueva: no se usa como evidencia de carga guardada. Solo se retrasaron
las pulsaciones del control privado para repetirlo.

### STQ bilineal: convertir antes de restar medio texel (7 de octubre)

La siguiente comparación procedural con PCSX2 v2.8.2 software confirma un fallo
de precisión distinto: bilinear restaba medio texel a la coordenada float antes
de cuantizarla, aunque nearest ya usaba 16.16. Una matriz 16×16 con texels RGBA
4×4 disjuntos y valores próximos a los límites signed difiere en 611 bytes
RGBA/156 píxeles. La entrada RGB/alpha de los 16 texels y freeze/GIF/End se
comprueban exactos antes de atribuir la diferencia al filtro.

`ps2recomp-gs-bilinear-stq.patch` realiza la conversión antes del medio texel
en CPU y GLSL. Las tres regresiones nuevas fallan antes (573/576) y pasan
después (576/576, catorce controles OpenGL reales). Usan la huella RGBA externa,
sin repetir la fórmula del filtro en el test, para sprites/triángulos, Q=1/2
y cuatro modos SSE. Hardware tiene que rasterizar al menos una matriz sin
tiles compute. El generador público exporta ahora también el control bilinear
STQ; los detalles y la referencia están en [RENDERIZADO.md](RENDERIZADO.md).

La compilación completa regenera las 6418 unidades y termina con código 0.
La auditoría confirma 49 parches/84 fuentes y la suite posterior pasa 576/576.
El helper compila siete herramientas y valida trece patrones freeze/GIF/End
con CPU ×3, además de los 18 controles de imagen y seis corrupciones rechazadas.
Los cuatro dumps son idénticos a los ejecutados en PCSX2 por SHA256 y sus
matrices CPU coinciden con los 1024 bytes RGBA de cada referencia.

Compute ×8 y hardware ×8 del nuevo patrón conservan VRAM, estado y cuadro
visible entre restauraciones, con RGB externo exacto. Las ocho pasadas rápidas
seleccionando hardware usaban aún compute mientras compilaba su shader.
La nueva opción `--pausa-ms 1000` deja terminar esa compilación entre pasadas
sin cambiar las comparaciones: siete de las ocho repeticiones usan hardware
con 256 primitivas y cero tiles compute. Se rechazan nueve argumentos inválidos;
CPU ×2 con pausa mantiene salida 0 y feedback opcional mantiene su salida 1.
La espera de diagnóstico no interviene en el juego ni mide rendimiento.

El control final de 510 s usa OpenGL, omite FMV y mantiene desactivado el snapshot
experimental. Carga una copia privada de la tarjeta de Claude; el original
conserva su SHA256. Las dieciséis imágenes 512×448 son distintas y válidas.
La última muestra a Kratos durante un ataque, con HUD, estelas de armas, R2 y
punto de guardado, en estado 11 sin carga pendiente. Se reciben las tres
pulsaciones de cuadrado; las 128 muestras de Clip son finitas/completas, las
768 tripletas tardías son no nulas y los 192 contextos de partida no presentan
punteros inválidos, ciclos ni truncamientos. No aparece VU reservada. La imagen
de ataque queda comprobada; combate contra enemigos y rendimiento sostenido
siguen pendientes. La documentación de arquitectura enlaza ahora la lista
de parches de `scripts/compilar.ps1`, para evitar otra lista manual incompleta.

### Páginas regionales: reducir sincronizaciones sin perder feedback (7 de octubre)

`ps2recomp-gs-region-pages.patch` delimita las lecturas de REGION_CLAMP y
REGION_REPEAT y guarda ambos extremos U/V en la clave del bitset. En los
controles procedurales, tres fuentes disjuntas pasan de tres lotes/dos flushes
a uno/cero; la cuarta lectura de una página escrita conserva dos/uno.
Se comparan los 4 MiB completos con CPU en los 13 PSM, con paletas CSM2
coloreadas, nearest/bilinear y caché del bitset activada/desactivada. Cambiar
solo MIN en REGION_CLAMP prueba que la clave se renueva correctamente.

Los controles de bordes incluyen bases TBP desalineadas, cruces de páginas,
vuelta de VRAM y máscaras no nulas, con y sin snapshot. Mantienen las
sincronizaciones reales y exigen hardware efectivo sin tiles compute.
Son 1184 combinaciones entre ambas rutas. Las regresiones nuevas de lotes
fallan antes; los controles aislados con el equipo libre descartan los plazos
de shaders agotados durante una primera ejecución junto a la compilación de
Claude. Los detalles están en [RENDERIZADO.md](RENDERIZADO.md).

La compilación completa termina con código 0; 50 parches/84 fuentes coinciden
y la suite final pasa 580/580, con dieciocho controles OpenGL. Las siete
herramientas pasan 18 controles de imagen y trece freeze/GIF/End con CPU ×3;
los seis dumps dañados siguen rechazándose. La reducción de envíos es un
resultado sintético; faltan medir su efecto en FPS y comprobar la caché GS real.

El control posterior dura 510 s en OpenGL, con FMV omitido y snapshot
experimental desactivado. Carga una copia privada de la tarjeta, cuyo original
conserva su SHA256. Las dieciséis capturas 512×448 son distintas; la última
muestra a Kratos atacando con HUD, estelas de armas, R2 y el punto de guardado,
en estado 11 sin carga pendiente y con hardware activo. Se conservan 128
muestras de Clip finitas/completas, 736 tripletas tardías no nulas y 192 contextos
sin punteros inválidos, ciclos ni truncamientos. No aparece VU reservada.
Esto verifica la imagen de un ataque; combate contra enemigos y FPS sostenidos
siguen pendientes.

### Captura GS tardía desde TEXFLUSH (7 de octubre)

El diagnóstico permite elegir espera y duración con `GOW_GS_REPLAY_AFTER` y
`GOW_GS_REPLAY_SECONDS`. `GOW_GS_REPLAY_TEXFLUSH=1` inicia el tramo después del
primer TEXFLUSH del juego en estado 11 que cumpla el plazo. No añade una
invalidación: conserva los comandos anteriores en el estado inicial y los
posteriores en la traza. Sin archivo de captura no se instala el wrapper;
las opciones inválidas se rechazan antes de abrirlo. El perfil limpia las
cuatro variables. Valores y límites: [CONTROLES.md](CONTROLES.md#captura-de-comandos-gs).

La regresión falla con el comportamiento de inicio anterior y pasa con la
frontera. Comprueba caché antigua sin borrar antes de tiempo, TEXFLUSH del menú
y anterior al plazo, presentación sin inicio, bytes CT24 pendientes, comandos
previos excluidos de la traza y repetición exacta de toda la VRAM final.
La prueba cierra sus lectores antes de borrar los archivos en Windows. El
helper ahora rechaza también errores negativos de proceso: `ERRORLEVEL 1`
por sí solo dejó pasar el cierre anómalo de una primera versión de este test.
La CI compila **y ejecuta** el control CPU sin el juego ni OpenGL.

Tras `2_recompilar_rapido.cmd`, el control CPU de 520 s carga una copia privada
de la tarjeta de Claude, con FMV omitido. El original conserva su SHA256.
Las dieciséis imágenes son distintas; la última muestra a Kratos atacando
junto al punto de guardado, con HUD y estelas. Estado final 11 sin carga
pendiente, 128 Clip finitos/completos, 368 tripletas tardías no nulas y 192
contextos sin punteros inválidos, ciclos ni truncamientos; cero VU reservada.

Con espera 470 s y duración 3 s, el inicio ocurre a 470,287 s desde el backend,
después de TEXFLUSH. El archivo completo ocupa 54.535.037 bytes: 146.015
registros, 145.469 dibujos y dos presentaciones. CPU ×3 reproduce el End, toda
la VRAM, el estado y ambas imágenes exactamente, entre pasadas y frente al
original. La página inicial está invalidada y permite importar el estado en GPU.

Compute sigue divergiendo: el primer dibujo es un sprite UV PSMCT32 de 32×416,
con 1874 bytes distintos desde la dirección 65540. La primera divergencia está
en el registro 3; los finales y las dos presentaciones difieren de CPU. Las
tres pasadas conservan los contadores de raster pero varían algunos bytes
de VRAM, y una transición cambia también el primer cuadro visible. Esta traza
delimita un fallo de GS en una escena tardía; no acredita paridad GPU ni FPS.
Los datos de juego y las imágenes permanecen bajo `logs/`, excluidos de Git.

El sprite se redujo a una traza local de un dibujo: TBP=0/FBP=0, TBW=FBW=8,
CLAMP en ambos ejes, bilinear, textura declarada 1024×1024 y máscara de alpha.
CPU ×3 queda exacto. Compute ×3, con y sin snapshot de feedback, conserva
la misma diferencia de 1874 bytes y permanece estable entre pasadas. La
presentación reducida sigue idéntica porque esa escritura no cambia el buffer
visible actual. Proteger la fuente de toda la primitiva no resuelve este caso;
queda por contrastar el orden de lectura de páginas y la caché CPU de 8 KiB.

### Controles intermedios de variación OpenGL (7 de octubre)

`repetir_gs --checkpoints-sync --repeticiones N` compara huellas de los 4 MiB
en Flush, Sync, Present y End. El informe identifica el primer control que
varía entre pasadas. Los comandos elegidos ya drenan lotes; TEXFLUSH no lo
hace en GPU y queda fuera de esta sonda. Se mantiene la comparación exacta
del End, estado e imágenes. El historial se limita a 4096 controles, con
rechazo previo al replay. Añade readback, no mide FPS ni certifica igualdad
byte a byte intermedia cuando coinciden las huellas.

La CLI pública pasa cinco argumentos inválidos, tres recorridos CPU y el
rechazo del exceso antes de dibujar. Una regresión C++ usa la comparación
real del tool: una diferencia intermedia debe fallar aunque el End sea
idéntico. También verifica cantidad y metadatos. Ambas se ejecutan en CI
sin datos del juego; el helper Windows compila y ejecuta la regresión C++.

El tramo real CPU ×3 pasa sus trece controles y conserva igualdad exacta
final y visible. Compute ×3 con snapshot muestra el primer control variable
en el Flush del registro 73.288, tras 73.003 dibujos; ocho controles varían,
las dos imágenes visibles entre pasadas siguen idénticas y los contadores
son 632 lotes/99.402 primitivas/53.757 tiles en las tres pasadas. Los End GPU
consecutivos difieren en 4296 y 3977 bytes. No acredita estabilidad de toda
la VRAM ni paridad con CPU. El control reducido al primer sprite conserva
1874 bytes de diferencia también en hardware caliente, con cero tiles
compute; ese dibujo aislado permanece estable entre pasadas.

El control privado que elimina la caché de lectura del CPU altera 34.262
bytes del End de ese sprite y tampoco iguala OpenGL: no se incorporó al
runtime. Próximo paso: reducir el lote anterior al primer Flush variable y
contrastarlo con fuentes protegidas y formatos de VRAM, conservando CPU.

### Dependencia de color y profundidad en lotes OpenGL (7 de octubre)

La reducción mediante End derivados de CPU acota la variación observada a
los prefijos 68.897/68.898: el primero queda estable en cinco pasadas y el
segundo cambia 64–128 bytes entre algunas pasadas. El nuevo sprite pertenece
a una pareja que usa FBP=ZBP=320, FBW=1, PSMCT24/PSMZ24 y Z activo. Aunque
los rectángulos XY son disjuntos, color y Z reinterpretan la misma memoria.
Completar el lote antes del segundo sprite elimina la variación en ocho
pasadas. Esta bisección no certifica monotonicidad ni primer fallo global.

El caso procedural independiente del juego difiere de CPU en 6144 bytes
antes del cambio y coincide en los 4 MiB después. El nuevo parche
`ps2recomp-gs-target-alias.patch` separa lotes cuando páginas de color y
profundidad se cruzan entre primitivas. Conserva el renderer CPU y se aplica
después de los cincuenta parches anteriores. La detección es conservadora;
el alias interno de una sola primitiva sigue fuera del arreglo.

Tras la compilación oficial completa (código 0), el backend integrado repite el
tramo real en compute cuatro veces con snapshot y cuatro sin él: trece controles estables, End, estado y ambas imágenes exactamente
iguales entre pasadas. Son 648 lotes frente a los 632 anteriores y se
conservan 99.402 primitivas/53.757 tiles. Todavía difiere de CPU en 739.502
bytes y las dos presentaciones. No acredita paridad ni mejoras de FPS.

Validación integrada: **582/582** pruebas nativas, incluidas veinte pruebas
OpenGL; los controles nuevos cubren CT24/Z24 y CT32/Z32 compartidos, Z de
solo lectura, Z inactivo y buffers disjuntos, en compute y hardware efectivo.
También pasan las herramientas GS y las pruebas de argumentos/controles de la CLI.
El modo por defecto conserva trece controles estables igual que el snapshot.

### Integración de las PR #12, #17 y #18 (8 de octubre)

La #18 (`bfee1bb`) contiene todos los commits de la #17 (`b3d8cd6`). Se
conservan sus dos parches VU1 después de los 51 actuales, incluido el arreglo
GS de alias color/Z. La fusión de la #12 (`927c7b93`) conserva las versiones
actuales de DMA, VIF DIRECT, ramas EE, VU0, FPU y salto de cola: ya estaban
incorporadas en `ps2recomp-vu0-macro.patch`, `ps2recomp-vif-direct.patch` y
`ps2recomp-ee-fixes.patch`; no se añaden copias antiguas.

La pieza nueva de #12 es `ps2recomp-vu1-budget-diag.patch`, adaptada a
`StepContext` de #18. `GOW_VU1_BUDGET_DIAG=1` informa de cortes por presupuesto
con mensajes limitados; no aumenta los 65536 ciclos ni modifica el programa.
La regresión cubre presupuesto corto/cero, reanudación hasta E y reset, con
escrituras directas y con colas. El generador requiere datos locales del
usuario y el C++ derivado del juego nunca se publica.

La revisión detectó un fallo de flags persistentes en #17/#18: MADD con
producto subnormal y suma normal daba `status=0x000` con escrituras directas
y `0x140` con colas. `ps2recomp-vu1-sticky-preserve.patch` conserva los flags
del producto tanto en el intérprete como en los pares/bloques compilados.
La prueba nueva verifica también que un microprograma lector posterior los
observe. Se conserva el análisis de flags MAC temporales que nadie lee.

Resultado integrado: **55 parches** aplican en orden y sus **85 fuentes**
comparadas coinciden con el runtime compilado; `scripts\2_compilar.cmd`
termina con código 0 y actualiza el ejecutable. La suite nativa pasa
**565/565**, sin excluir fallos, y el generador pasa **60 casos procedurales**
con 139 pares compilados y 22 interpretados reales. CI recibe el mismo
control del generador; la validación GS anterior pasó además veinte pruebas
OpenGL opcionales. Dos cadenas VIF históricas del juego (`vif_pcsx2_inicio2`
y `vif_port_480s`) producen imágenes exactamente iguales con colas y
escrituras directas en el runtime integrado. Esta comparación no acredita
paridad con PCSX2 ni los FPS de una partida; las imágenes históricas no
son una referencia equivalente al nuevo modo aritmético.

La compilación oficial usa el intérprete si no se enlaza C++ VU1 derivado
de microprogramas locales. La infraestructura compilada queda verificada
con código procedural; falta integrarla en el flujo de compilación local
y repetir las medidas de rendimiento con los flags corregidos.

### Alias de coordenadas por ancho y vuelta de VRAM (8 de octubre)

El siguiente control procedural detecta un riesgo distinto del cruce color/Z:
con FBW=1, `(0,32)` y `(64,0)` de CT32 apuntan al mismo pixel. También se
repite una dirección al superar las 512 páginas de los 4 MiB. Compute ordena
primitivas por XY dentro de un tile; el interlock hardware también se aplica
al pixel XY. Ninguno ordenaba esos dos XY distintos del mismo destino.

Un lote con 64 sprites sobre el primer XY y otro sobre su alias pierde
6144 bytes en CT32/CT24 o 8192 en CT16/CT16S frente a CPU. Los controles
con páginas disjuntas y coordenadas repetidas dentro del ancho coinciden.
La base FBP=511 permite comprobar además el cruce del final de VRAM.

`ps2recomp-gs-coordinate-alias.patch`, aplicado al final de los 55 anteriores,
conserva por separado el riesgo de coordenadas en color y profundidad.
Completa el lote si el destino actual comparte páginas con uno anterior y
alguno supera el ancho de fila o las 512 páginas lógicas. La base FBP por
sí sola no activa el riesgo. La detección es conservadora por página: dos
escrituras fuera del dominio normal pueden separar lotes aunque sus pixels
sean distintos. No ordena alias internos de una sola primitiva.

Las dos regresiones opcionales cubren cuatro formatos de color, ambos
sentidos del alias, vuelta de VRAM, Z32/Z16 con color disjunto, Z inactivo
y controles que mantienen un único lote. Comparan los 4 MiB y comprueban
los contadores de rasterizado, sin aceptar fallback CPU como OpenGL.
El candidato pasa **587/587** pruebas nativas, incluidas 22 de OpenGL real.
Ambas regresiones fallan, también por los bytes de VRAM, al enlazarlas con
el backend anterior y el mismo resto del runtime.

La compilación oficial completa termina con código 0. Los **56 parches**
aplican en orden y las **85 fuentes** auditadas coinciden. La repetición
CPU ×2 conserva los trece controles, End y ambas imágenes exactos. Compute
×4 sin snapshot y ×4 con él también quedan estables; los 4 MiB del End y
las imágenes coinciden byte a byte con el backend anterior. No añade lotes
a esa traza: mantiene 648, con 99.402 primitivas/53.757 tiles y los mismos
contadores de CLUT y sincronización. Sigue difiriendo de CPU en 739.502
bytes y dos presentaciones; este arreglo no acredita paridad ni más FPS.

El control del ejecutable GS56 dura 510 s, con OpenGL, FMV omitido, VU1
directa por defecto y copia privada de la tarjeta guardada. Las 16 imágenes
512×448 son distintas; se observa a Kratos con forma correcta, ataques y
HUD con barras verde/azul, 87 orbes y R2 visible. El estado final es 11 sin carga
pendiente. Hay 128 muestras Clip finitas/completas, 848 tripletas tardías
no nulas y 192 contextos sin errores; no se registra VU reservada. La
tarjeta original conserva su SHA256. Estas capturas no certifican combate
completo ni FPS sostenidos.

### Ensayo de Z de solo lectura retenido (8 de octubre)

`writePages` incluye las páginas consultadas por el test Z aun con
ZMASK=1. Un ensayo local separó lecturas y escrituras para que textura/CLUT
y profundidad pudieran compartir un lote sin modificar esas páginas.
Sus controles procedurales pasaron en CPU, compute y hardware, incluidas
subidas sobre Z y escrituras antes/después de lectores.

En la traza real, el ensayo reduce 648 a 543 lotes y 53.757 a 46.543 tiles
compute, conservando 99.402 primitivas. Ocho pasadas compute y ocho
hardware con snapshot conservan End e imágenes anteriores. Estos
contadores no miden FPS ni certifican la caché física del PS2.

Sin embargo, el prototipo sin snapshot varió 200 bytes finales y una
presentación en una de 24 pasadas. Limitarlo a snapshot y conservar los
cortes anteriores en el modo habitual tampoco bastó: otra prueba larga
varió 128 bytes finales y una presentación, desde el registro 73.326,
Flush tras 73.036 envíos. **Se retiró la optimización completa de la
cadena publicada**, incluidas sus dos pruebas; código y datos quedan en
`logs/`, excluidos de Git. No se anuncia una mejora de FPS por este ensayo.

El control largo del GS56 real también varía una presentación (190 bytes)
en una de 24 pasadas; su primer control distinto está en el registro 36,
Flush tras 33 envíos. El End y el estado final son idénticos en las 24.
Hay feedback bilineal sobre el framebuffer en los tramos investigados,
pero la causa exacta sigue pendiente. Cuatro pasadas iguales no
certifican estabilidad. La referencia anterior al control largo no
incluía aún el parche de alias de coordenadas; aquí sí se enlaza GS56.

### Control STQ de hardware e integración de VU1 (8 de octubre)

El control STQ bilineal terminaba sus 16 matrices antes de que el
compilador asíncrono de shaders ofreciera hardware. Los píxeles y SSE
eran correctos, pero el control de hardware falló. El nuevo parche
`ps2recomp-gs-stq-hardware-test.patch` repite las matrices durante un
máximo de 12 s, conservando todos los oráculos RGBA, controles SSE y la
exigencia de hardware sin tiles compute. Un resultado correcto solo en
compute sigue fallando; no se modifica el renderer de producción.

Se integraron los siete commits terminados de Opus hasta `e507ac6`,
con sus bloques compilados, helpers, XGKICK y enlace opcional de VU1.
La compilación oficial final utiliza sus 501 micromemorias locales,
sin modificar ni publicar esos datos.

La compilación oficial final termina con código 0: **59 parches** en
orden y **87 fuentes** (`cpp/h/inl`) auditadas sin diferencias con el
runtime. Pasa **587/587** pruebas nativas, con **22 controles OpenGL
efectivos**, **60 casos VU1 exactos** (139 pares compilados y 22
interpretados), PAD2 y los controles de configuración, scripts, GIF y
selección VIF. La medición de FPS de Opus se conserva como resultado
de su tramo, sin extrapolarla al juego completo.

### Integración del planificador y control de partida (8 de octubre)

Se incorpora el `main` de la PR #20, `9f0ebc4`, conservando GS56, la prueba STQ de
hardware y la VU1 compilada. La compilación oficial termina con código 0, con
**60 parches**, las 501 micromemorias locales y un ejecutable de 67.710.976 bytes.
Las **87 fuentes** modificadas (`cpp/h/inl`) coinciden con la aplicación ordenada
de los parches sobre el commit fijado. Pasan **587/587** pruebas nativas, con
**22 controles OpenGL reales**, los **60 casos VU1 exactos** (139 pares compilados
y 22 interpretados), PAD2, configuración, sintaxis PowerShell, inspector GIF
(8 casos) y selección VIF (11 casos).

Un control limpio de 430 s, sin diagnósticos de geometría y con copia privada de
la tarjeta, produce **15 capturas distintas de 512×448**. La última, a 360,10 s
del reloj del mando, muestra a Kratos y varios enemigos en el barco, lluvia,
fondo y HUD; no se observan polígonos estirados en esa captura. El estado es
11, sin carga pendiente y con el nivel listo. OpenGL usa hardware y no aparece
VU reservada. La tarjeta original conserva su SHA256. La prueba anterior del
ejecutable de 59 parches también muestra esa escena con VU1 compilada.

Se descarta una comparación anterior de FPS: coincidió con otra compilación y
sus ventanas correspondían a estado 3 con carga pendiente. El nuevo selector
`resumir.py --partida`, cubierto por siete pruebas, exige informes de partida
cargada a ambos lados de las ventanas sin igualar los relojes del perfil y del
mando. Estos controles verifican integración y la escena observada; todavía
no certifican rendimiento sostenido, combate completo ni feedback GS fiel.

### Validación de 61 parches y reducción del feedback (8 de octubre)

La compilación oficial de `main` en `63c8a02` termina con código 0, con
**61 parches**, 501 micromemorias locales y un ejecutable de 67.710.976 bytes.
Las **87 fuentes** auditadas coinciden con la cadena publicada. FINISH sigue
síncrono por defecto; el test con backend retenido pasa en modo default y en
modo opcional asíncrono. Pasan también PAD2 y los **60 casos VU1 exactos**,
con 139 pares compilados y 22 interpretados.

El primer control local OpenGL pasa 584/587: tres casos coinciden en píxeles
pero no alcanzan su requisito de hardware efectivo. Se conserva ese registro.
Una repetición posterior pasa **587/587**, incluidas las **22 pruebas OpenGL**;
sus variantes se compilan en menos de dos segundos. No se amplían los plazos
ni se cambia el renderer para aceptar el primer fallo. La causa del fallo
intermitente de hardware todavía no se ha demostrado.

`repetir_gs --hasta-registro R` permite detener una traza en un Flush, Sync,
Present o End ya registrado. Compara exactamente los 4 MiB y el estado portable
CPU/candidato al terminar el prefijo, sin validar el End original cuando no se
ejecuta. Rechaza otros comandos e índices fuera del archivo antes de crear el
backend. El límite de 4096 controles se aplica al prefijo. Los controles CLI
cubren once argumentos inválidos, fronteras, un End alterado y límites.

El prefijo hasta el Flush **36**, después de **33 envíos**, reproduce la
referencia CPU y queda idéntico en CPU ×2. Se hacen 64 pasadas por política,
con restauración inicial exacta, los mismos 33 lotes y readback sincronizado:

| Ruta | Comparaciones consecutivas de la misma ruta | Comparaciones con VRAM distinta |
|---|---:|---:|
| Compute habitual | 63 | 4 (59 o 890 bytes) |
| Compute con snapshot | 63 | 0 |
| Hardware habitual, shaders listos | 61 | 61 |
| Hardware con snapshot, shaders listos | 48 | 0 |

Las pasadas compute tienen 544 primitivas/1760 tiles; las de hardware caliente,
544 primitivas/cero tiles. Las primeras 2 pasadas hardware habituales y las
primeras 15 con snapshot usan compute durante la compilación de shaders; se
excluyen de esas dos filas, junto con la transición a hardware. Las políticas
con snapshot conservan **515.877 bytes distintos
de CPU**, por lo que siguen devolviendo 1: estabilidad no implica paridad ni
fidelidad a la caché PS2. En este prefijo no se presenta ningún cuadro y los
readbacks/pausas no miden FPS. El corte reduce la investigación del feedback
de 146.015 registros a 36, sin publicar la traza ni alterar el modo habitual.

### VIF1/temporizadores y VU1 SIMD integrados (8 de octubre)

Se integran los dos commits terminados de la PR #21 (`8841ef0`, `b280247`) y
el primer commit SIMD de la PR #23 (`5e09b12`), conservando sus autores e
historia. Los conflictos de documentación y orden de parches se resuelven
incluyendo los tres cambios. La revisión detecta y corrige en `247a9b3` un
flag de underflow perdido por ADD/SUB SIMD cuando el PC usa FTZ/DAZ: cero
redondeado no implica cancelación exacta. La regresión dirigida falla antes
del arreglo con `status=1c0/c0` y pasa después; los valores de los registros
ya coincidían, por lo que comprobar solo la imagen no detectaba este fallo.

La compilación oficial de `247a9b3` termina con código 0: **64 parches**,
501 micromemorias locales y ejecutable de **70.182.400 bytes**, SHA256
`82ECCA22A4F4600FA60DB255D45176178971D60FF94382CC12D2E08BFC807074`.
Las **87 fuentes** auditadas coinciden con la cadena. Pasan **587/587 pruebas
nativas**, las **22 OpenGL efectivas**, los **60 casos VU1**, **100.800 casos
FMAC aleatorios y 2.016 FTZ/DAZ dirigidos**, PAD2 y ambos controles FINISH.
La prueba FMAC utiliza 359.856 pares compilados sin fallback al intérprete.
Configuración y sintaxis PowerShell también quedan correctas.

Un perfil limpio de 180,55 s, con FINISH síncrono, renderer OpenGL y copia
privada de la tarjeta, no observa compiladores ni otra partida en su vigilancia
cada segundo. Doce ventanas completas de partida cargada (60,04 s entre
103,06 y 163,12 del reloj del perfil) miden **7,58 vid::Flip/s**, con **57,71
presentaciones/s**. Los tiempos exclusivos del hilo del juego son VU **43,24 %**,
GS **38,20 %** (incluidas esperas), EE **12,20 %** e IOP **6,36 %**. La tarjeta
original conserva su SHA256. No se atribuye una ganancia a SSE por comparar
esta única pasada con otra escena; falta una comparación controlada y separar
el trabajo del GS de sus esperas. El treemap refleja SIMD y la reducción del
feedback, manteniendo GS y VIF1/VU1 parciales en los tres idiomas.

Durante estas pruebas Opus añade `754c970` (DIV compilada) a la PR #23 y abre
la rama de flags perezosos. **Esos cambios posteriores todavía no están en
este ejecutable**: requieren una revisión e integración propias. No se altera
el árbol de trabajo de Opus ni se anuncia que toda la PR #23 esté aplicada.

El control limpio del mismo ejecutable de 64 parches dura 430 s y conserva
**15 capturas distintas de 512×448**. La última, a **360,14 s** del reloj del
mando, muestra a Kratos, varios enemigos, barco, lluvia, fondo y HUD, sin
polígonos estirados visibles. Sigue en estado 11, `pending=0`, `levelReady=1`,
con hardware activo y sin instrucciones VU reservadas en el registro.
La tarjeta original conserva su SHA256; se utiliza una copia privada.
Es una comprobación de esa escena, con FMV omitido, y queda pendiente
verificar combate completo, otros escenarios y rendimiento sostenido.

### Atribución del profiler OpenGL (8 de octubre)

El diagnóstico GPU de 180 s sobre el ejecutable de 64 parches descubre que
los lotes hardware imprimen FBP, flags y primitivas como cero. Una consulta
engloba varias variantes de shader sin adjuntar su estado; esas líneas no
permiten identificar el destino que consume tiempo. En compute, el último
lote del umbral de 4096 consultas también pierde datos al resolverse la cola.
Esta ejecución sincroniza timestamps y añade registros: no mide FPS limpios.

`ps2recomp-gs-hardware-profile.patch` atribuye una consulta a cada variante
hardware y adjunta los datos compute antes de resolver las consultas.
El trabajo CPU de preparar esos datos queda fuera del intervalo GPU. Sin
`PS2X_GS_GPU_PROF=1`, las consultas siguen desactivadas. Dos regresiones
OpenGL reales fallan con el backend anterior y pasan con el borrador corregido:
dos variantes dentro del mismo lote y la frontera de 4096 lotes compute.
En ambos casos conservan exactamente los 4 MiB frente al CPU y exigen la ruta
GPU efectiva. La compilación oficial integrada se valida por separado.

La compilación oficial de `23fbce4` termina con código 0: **65 parches**,
501 micromemorias locales y ejecutable de **70.182.912 bytes**, SHA256
`6D7BA3034436D28141D4F429BD3D42ABD7BBA5F79166808CBD8410902F5736A1`.
Las **87 fuentes** auditadas coinciden con la cadena. Pasan **589/589 pruebas
nativas**, incluidas **24 OpenGL efectivas**, los 60 casos VU1, 100.800 FMAC
aleatorios y 2.016 dirigidos FTZ/DAZ, PAD2 y los dos modos FINISH.
El renderer CPU y la política habitual de feedback se conservan.

El diagnóstico corregido de **240,21 s**, con copia privada de la tarjeta y
sin compiladores ni otras partidas observados, alcanza estado 11 con el nivel
listo. La tarjeta original conserva su SHA256. Se seleccionan **280 informes
GPU completos**, encerrados por muestras de ese estado. El promedio de
timestamps raster es **39,26 ms/frame del profiler**; el grupo FBP=0,
flags=`4b` (textura, IIP, mezcla alfa y bilineal) acumula **30,13 ms/frame**.
Los siguientes grupos son `5a` (2,15 ms), `12` (2,08 ms) y `c8` (1,81 ms),
todos en FBP=0. La selección contiene también algunos lotes compute mientras
se preparan variantes. Es una pista para investigar shaders; no equivale a
FPS limpios, tiempo exclusivo de CPU ni atribución por primitiva. No se
compara esta cifra con el profiler anterior, que agrupaba datos vacíos.

Un ensayo privado elimina la llamada previa a `coversPixel` del fragment
hardware, porque las funciones de sombreado ya comprueban cobertura y bordes.
El control procedural alterna original/candidato/original: **8,80 / 9,01 /
8,75 ms** de mediana, 20 lotes por ejecución, 2000 triángulos IIP/STQ/
bilineal/ABE por lote, hardware efectivo y 4 MiB exactos frente al CPU.
Incluye envío y readback sincronizado; no mide FPS de partida. No demuestra
una ganancia, por lo que no se incorpora ni se añade a la cadena de parches.
Las fuentes y registros del ensayo quedan en `logs/` y el ejecutable publicado
conserva los 65 parches verificados.

La herramienta pública de rendimiento amplía el control previo a una
vigilancia cada segundo durante toda la pasada. Ante otra compilación o
partida, cierra solo su instancia, marca el registro como inválido y restaura
el entorno; el selector impide usarlo para calcular FPS o exportar JSON.
Pasan cuatro controles de scripts con procesos simulados y nueve del selector,
sin lanzar ni detener juegos o compiladores ajenos.

El siguiente diagnóstico de CPU utiliza una copia privada enlazada con
símbolos públicos a partir de los objetos optimizados. No sustituye el EXE
normal ni sirve para comparar FPS. El muestreo de 30 s en estado 11 muestra
esperas en el hilo principal y el del GS, pero descubre dos problemas de la
herramienta: inicialización duplicada de DbgHelp (error 87) y marcos de
llamadores recogidos sin imprimirlos. Se corrige el perfilador para mostrar
esa segunda vista y cerrar sus handles de hilos. La espera procedural de
`scripts\probar_perfil.cmd` falla antes de los cambios y pasa después, sin
usar el juego. Falta repetir el muestreo con esa atribución para distinguir
esperas de cola, driver y sincronización; no se asigna todavía una causa.

### Revisión de DIV compilado y PR #23 (8 de octubre)

La parte FMAC SSE de `5e09b12` ya estaba integrada; la novedad de la PR es
`754c970`, que especializa la instrucción inferior DIV. Se resuelven sus conflictos
con `main` conservando el arreglo FTZ/DAZ de `247a9b3` y los cambios posteriores.
No se incluye la optimización separada de flags de `df8c740`.

Los 60 controles anteriores no ejercitaban DIV. Se añaden **38.144 casos**:
16 selectores de componentes, valores límite y aleatorios, Q antes y después de
WAITQ, flags D/I, escrituras directas/con colas y cortes/reanudaciones, con y sin
FTZ/DAZ. Registros, Q y memoria coinciden bit a bit; flags y ciclos también.
Un fallo inyectado solo en una copia privada (latencia 6 en lugar de 7) falla en
el primer caso: Q y flags coinciden, pero los ciclos son `19/18`.

La compilación oficial integrada termina con código 0, **65 parches** y las
501 micromemorias locales. El ejecutable tiene **70.226.432 bytes**, SHA256
`EB8479487D9E572250BE1FFE618E2A8110386B5B1A14423A2AF924C45410068F`.
La auditoría de las **87 fuentes** no encuentra diferencias con los parches.
Pasan **589/589 pruebas nativas**, incluidas **24 OpenGL efectivas**, los 60 casos
VU1 anteriores y los nuevos de DIV, **100.800 FMAC aleatorios**, **2.016 FTZ/DAZ**,
PAD2 y FINISH default/asíncrono. La CI de la integración también pasa en Linux.
Las cifras de mejora de Opus pertenecen a sus capturas; esta revisión valida
paridad y compilación y no certifica nuevos FPS de la partida cargada.

### Perfil GPU diferido en revisión (8 de octubre)

Se prepara como propuesta aislada el parche 67
`ps2recomp-gs-profile-async.patch`: consulta disponibilidad antes de leer
timestamps, conserva ventanas y contadores juntos, limita las muestras y
elimina los objetos de consulta al cerrar. La inspiración procede del
perfilador de ICO PC; la implementación es propia y no altera el render.

Se añaden ocho controles nativos con timestamps simulados. La compilación
oficial Windows, OpenGL efectivo y la prueba del juego quedan pendientes:
las herramientas locales no pudieron iniciar procesos durante este trabajo.
GitHub rechazó crear el blob y la rama con 403 en el primer intento.
No se cambia el estado de los componentes ni se certifican nuevos FPS.
Detalles y límites de atribución temporal:
[Perfil GPU sin esperas durante el render](RENDIMIENTO_PERFIL_GPU.md).

Al reintentar, la ejecución fuera del sandbox funciona. El registro identifica
un bloqueo de `node_repl.exe` al actualizar sus permisos (error Windows 32).
Se actualiza la base a `9e34bff`, conservando la PR #24 de flags de VU1; el
profiler ocupa ahora el lugar **67** de la cadena. La sesión de GitHub CLI es
válida aunque la integración siga rechazando escritura.

Las ocho regresiones aisladas pasan con MSVC, C++20 y `/O2 /W4 /WX`. Un
control negativo invierte la guarda de disponibilidad en una copia temporal:
compila, ejecuta las ocho pruebas y falla en cuatro, incluida la lectura
prematura; después se restaura la copia. La compilación oficial se lanza con
cuatro trabajos simultáneos en `E:\gowgpuprof`, que apunta a una carpeta privada
del workspace. Los controles OpenGL y de juego siguen pendientes de esa build.

### Verificación del profiler y protección de uploads (8 de octubre)

La PR #25 del profiler diferido quedó integrada en `43c3a47`, con CI correcta.
La compilación oficial de `2f6579d`, 67 parches, termina con código 0 tras
reanudar el mismo build después del reinicio. El control funcional de
240,31 s llega al estado 11 con nivel listo y produce ventanas completas
del profiler. Las capturas revisadas muestran a Kratos y enemigos sobre el
barco, lluvia y HUD, sin polígonos estirados visibles en esa escena.
Se utiliza una ISO de solo lectura y tarjetas privadas; FMV está omitido.
Había compilaciones ajenas y no se certifican nuevos FPS.

La revisión confirma que el juego todavía presenta mediante copia a CPU.
Faltan las llamadas del host raylib a la captura de contexto y al consumidor
de texturas compartidas; configurar `PS2X_GS_DIRECT_PRESENT=1` no basta.
Los tests sí ejercitan un share group real de forma explícita. Se conserva
esta diferencia para no confundir la capacidad del backend con su uso real.
Detalles: [verificación del profiler](RENDIMIENTO_PERFIL_GPU.md).

Se prepara un parche separado para impedir sobrescribir chunks del ring
persistente cuya fence no se haya señalado y liberar sus 64 MiB aunque
el host siga vivo. Los controles negativos detectan ambos problemas.
La primera integración pasa 610/610 controles nativos, 25 con OpenGL.
La revisión descubre además que el fallback streaming podía encoger
`dataBuffer` dejando una capacidad obsoleta; se corrige y se prepara su
regresión. La compilación oficial del parche final queda pendiente de
ese control. No se altera EE/FPU/IOP ni el trabajo de Opus.
Detalles: [protección del ring de uploads](RENDER_UPLOAD_RING.md).

### Ring OpenGL validado con la versión conjunta (9 de octubre)

La PR #30 incorpora la base `ca69e9b`, con los nuevos saltos entre bloques
compilados de VU1 de Opus. La compilación oficial de `2c8a8c1` termina con
código 0, 69 parches y 501 micromemorias locales. Pasan 611/611 controles
nativos, incluidos 26 OpenGL reales, y las 97 fuentes auditadas coinciden
con la cadena. Los controles negativos detectan la sobrescritura por timeout,
la fuga del ring con host vivo y la capacidad obsoleta del fallback.

La prueba funcional previa de 240,25 s llega a estado 11 con nivel listo;
sus capturas muestran a Kratos, enemigos, barco, lluvia y HUD sin polígonos
estirados visibles. La versión conjunta dura 241,34 s, llega a ese estado y
muestra también a Kratos, enemigos y HUD sobre el barco. Se omite FMV y se
usan tarjetas privadas. No hay avisos de degradación del ring; un informe
GPU parcial omite 2587 muestras y sus tiempos no se usan. No se certifican
FPS ni una partida completa; la presentación compartida del juego sigue
preparándose en un checkout separado y no forma parte de esta PR.

### 2026-10-09: presentación compartida sobre las PR #31 y #32 de Opus

La PR #33 conecta el consumidor raylib del juego con las texturas del
productor OpenGL, con `PS2X_GS_DIRECT_PRESENT=1` y backend GPU. Conserva CPU
y RAM como referencia/fallback. El latch compartido deja de forzar la
espera del host; las lecturas explícitas y FINISH mantienen sus barreras.
Las capturas de diagnóstico componen VRAM actual con el CRTC del latch,
con caché por generación: no son snapshots históricos del frame adquirido.
Detalles: [presentación compartida](RENDER_PRESENTACION_COMPARTIDA.md).

La base incorpora las PR #31/#32 ya fusionadas; no se modifican sus parches
VU1 ni EE/FPU/IOP/FMV. La suite nativa reconstruida pasa **623/623**, con
33 controles OpenGL reales. Restaurar LINEAR falla en 3/12 controles
específicos; forzar Flush en el latch compartido falla en 2/5 controles de
cola, que terminan con espera acotada también bajo esa regresión. Los
103 archivos parcheados coinciden entre la referencia probada y el runtime
de la compilación oficial. La CI del código actualizado pasa.

Se corrigió además la limpieza incremental de MSVC/Ninja en español:
registraba cero dependencias de headers, por lo que mezclaba objetos de
distintas interfaces. El script descarta objetos runtime/tests y el PCH y
su objeto de creación; el wrapper CMD conserva el código real de salida.
La compilación oficial termina con código 0; la suite enlazada con ese
runtime repite **623/623**. La prueba de **241,18 s** confirma el modo
compartido efectivo y llega repetidamente a estado 11 con nivel listo.
Produce trece capturas diagnósticas; las revisadas muestran a Kratos,
enemigos, barco, lluvia y HUD sin polígonos estirados visibles en esa
escena. No hay errores, degradación del ring ni informes parciales del
profiler. Se usan tarjeta/caché privadas y se omite FMV; el cierre al acabar
el plazo es forzado. No se certifica completar el juego ni mejorar FPS.

### 2026-10-09 — Comparación limpia de presentación

Tras integrar `main` con las PR #33 y #34, el perfil fija explícitamente
presentación RAM o compartida y restaura el entorno. Sus controles pasan;
el script anterior falla al heredar el modo compartido en una pasada RAM.
El analizador PowerShell y los nueve controles de selección de ventanas pasan.

Se completan cuatro pasadas alternadas de 180 s con el mismo ejecutable
de `343404f`, 501 micromemorias, tarjeta inicialmente ausente, caché privada
y perfil solo de cuadros, sin capturas ni diagnósticos GPU. Se confirma
estado 11/nivel listo y el modo efectivo en cada una. El promedio ponderado
es 6,848 FPS RAM y 6,908 compartido (+0,88 %), inferior a la variación entre
las dos pasadas RAM: no se acredita una mejora estable de FPS. Condiciones,
intervalos efectivos y límites en `docs/RENDIMIENTO_PRESENTACION.md`.

La siguiente investigación GS apunta al rasterizado: el diagnóstico previo
muestra 50–56 ms por cuadro presentado y un grupo dominante FBP 0/flags
`0x4b`, con textura, filtrado lineal, interpolación y mezcla. Es una pista
para repetir cadenas GS y preservar su igualdad con CPU; todavía no se
ha añadido una optimización del shader. EE, FPU, IOP y FMV permanecen fuera
de este cambio.

### 2026-10-09 — Ensayo de descarte fuera de cobertura

Se prepara `ps2recomp-gs-discard-uncovered.patch` después de los 76 parches
anteriores. Añade un descarte previo al interlock cuando el fragmento no
cubre el primitivo, con `PS2X_GS_HW_DISCARD_UNCOVERED=0` para conservar el
shader anterior. El control nuevo compara los 4 MiB de VRAM contra CPU con
triángulos finos/subpíxel, mezcla y recorte, y exige observar hardware después
de la compilación asíncrona. El control aislado pasa en ambos modos.

Cinco muestras procedurales con shaders preparados dan 25,04–25,94 ms sin
descarte frente a 16,63–18,35 ms con él para un lote de 4096 triángulos. No
son FPS de partida. Se inicia la suite completa y la compilación oficial;
el detalle y las restricciones del interlock están en
`docs/RENDER_DESCARTE_FRAGMENTOS.md`.


### 2026-10-09 — Descarte hardware: validación completa

La suite nativa termina 624/624 en ambos modos, incluidos 34 controles
OpenGL reales. La compilación oficial termina con código 0 y 77 parches;
los 103 archivos parcheados coinciden con la referencia probada. El mismo
binario se compara en cuatro pasadas 0/1/1/0 de 180 s: el tramo conservador
alineado por informes del mando suma unos 30 s por modo y da 6,958 cuadros/s
sin descarte frente a 7,267 con él (+4,43 %). La muestra es corta y no es un
replay determinista: no acredita un porcentaje estable del juego completo.

El control funcional repetido reconoce la ISO real y llega a partida cargada.
Las capturas del mando a 90/160/190 s muestran a Kratos, enemigos en combate,
cubierta, lluvia y HUD. El ensayo usa diagnósticos y termina por límite;
no valida FPS, todos los efectos ni el cierre normal. La primera tentativa
con ruta mal codificada se descarta. Hash, selección y límites del ensayo:
`docs/RENDER_DESCARTE_FRAGMENTOS.md`.

Se detecta como siguiente tarea que el decorador de captura GS no reenvía
`UsesSharedPresentation()`: al grabar podría hacer que el host eligiese RAM
sobre un backend que presenta texturas compartidas. Se corregirá por separado
con una regresión independiente del juego.

### 2026-10-09 — El grabador GS conserva presentación compartida

El decorador de captura heredaba la capacidad RAM por defecto aunque su
backend presentase texturas compartidas. El host podía elegir un modo distinto
al backend al activar `GOW_GS_REPLAY_TRACE`. Ahora reenvía la capacidad efectiva
bajo el mutex existente; conserva RAM cuando corresponde y no fuerza OpenGL.

La regresión consulta el decorador y el frontend con RAM/compartida y backend
directo/en otro hilo. Falla antes y pasa después; consultar no sincroniza,
presenta ni graba comandos. Suite de herramientas GS completa y recompilación
rápida: código 0. El ensayo de 140 s conserva el host compartido, llega a nivel
listo y cierra una captura privada breve durante la partida. La repetición CPU
no reproduce su VRAM final (343.007 bytes distintos), por lo que no se afirma
paridad de la cadena ni mejora de FPS. Condiciones en `docs/CONTROLES.md`.


### 2026-10-09 — Cadena GS de partida y pista de feedback

La captura privada breve contiene 60.389 Submit. CPU reproduce su propia
salida entre repeticiones, pero difiere en 343.007 bytes del End capturado.
Una sonda privada continúa tras esa divergencia para comparar variantes;
no modifica el verificador público ni certifica la captura contra CPU.

Con shaders calientes, restauración inicial completa y 40.855 primitivas,
289 batches y cero tiles compute por pasada, el shader anterior varía
897 bytes entre las pasadas 2/3 y el descarte varía 1.072. La comparación
entre sus últimas pasadas difiere 1.047 bytes: esta ruta no permite certificar
igualdad entre modos por una sola repetición.

Al repetir con la opción experimental existente `--snapshot-feedback`, las
pasadas 2/3 de cada modo coinciden en los 4 MiB, y la última pasada sin/con
descarte también coincide exactamente. La diferencia frente a CPU persiste
(342.846 bytes en el candidato con descarte); congelar por Submit no emula
la caché PS2 de 8 KiB. Es evidencia que orienta hacia lecturas de textura y
feedback dentro del primitivo, no prueba todavía su causa exacta. No se activa
esta política en el juego ni se atribuye una mejora de FPS a este diagnóstico.

Siguiente tarea: aislar un primitivo procedural con feedback y estudiar la
visibilidad/caché de sus lecturas, conservando el control CPU estricto. No se
publican cadenas, VRAM, capturas ni herramientas derivadas del juego.


### 2026-10-09 — Feedback reducido a un píxel estable

El generador de patrones GS acepta `--altura 1..416`, conservando textura,
freeze y registros iniciales al reducir los dos sprites. Captura y dump PCSX2
coinciden exactamente a través de GIF/End CPU para seis alturas; el default
permanece byte a byte. Se añaden controles de altura esperada, entradas inválidas
y el RGBA CPU independiente del caso mínimo; se integran ambos ejecutables en CI.

Con una fila, el único píxel distinto es `(32,0)`: CPU RGB `(141,90,98)` frente
a hardware `(132,87,86)`, estable entre repeticiones y también con snapshot por
Submit. La media de texels iniciales explica CPU; releer el píxel cambiado por
el primer sprite explica GPU. Queda aislada una diferencia de persistencia de
caché entre primitivas, separada de la variación del caso grande. No se cambia
la política del renderer del juego. Comandos, tabla y límites:
`docs/FEEDBACK_BILINEAL_MINIMO.md`.

### 2026-10-09 — Separar persistencia y reemplazo de caché GS

Sobre main con PR38 integrada, se convierte la intervención privada de caché
en una sonda reproducible del replay, exclusivamente para candidato CPU.
Invalidar su etiqueta antes de cada Submit reproduce el RGB GPU del caso de
una fila, conservando el End y estado de la referencia. Los 54 patrones
procedurales confirman persistencia hasta 32 filas y reemplazo natural desde
33; TEXFLUSH elimina la diferencia pequeña, scissor la conserva. Se verifican
bases/bytes de caché, VRAM, RGB y dos restauraciones iniciales completas.

No se implementa todavía la caché GPU: el caso grande necesita además el
orden de lecturas y reemplazos dentro del sprite. Congelar por Submit o hasta
TEXFLUSH no reproduce ambos contratos. La prueba queda en CI para validar
esa futura corrección sin cambiar EE/FPU/IOP/VU1 ni el ejecutable del juego.
Detalle y límites: `docs/FEEDBACK_BILINEAL_MINIMO.md`.

### 2026-10-09 — Captura de la intro y comprobación del framebuffer

Se añade `GOW_GS_REPLAY_STATE` para elegir el estado que permite iniciar una captura
GS. El valor predeterminado sigue siendo 11 (partida); el estado 4 permite estudiar
la intro previa. Solo los comandos enviados por el EE pueden abrir el tramo; una
presentación no consulta sus variables. El estado limita el inicio, no el final.
El parser rechaza entradas vacías, negativas, fraccionarias, desbordadas o con
sufijos sin aplicar parcialmente las otras opciones. El perfil limpia/restaura
esta variable junto con las demás opciones de captura.

Control local sobre `fa07925`, runtime con 77 parches, ejecutable con este cambio
SHA-256 `750ecf700585103d0c9ecdb3b7388aeb34d2e1d189ba3d8141c61784b4e61c95`:
`GOW_PAD_TEST=1`, `GOW_FAST_BOOT=0`, `GOW_SKIP_FMV=1`, OpenGL con presentación RAM,
TEXFLUSH real y estado 4. Dos ejecuciones propias acotadas a 45 y 70 segundos
capturaron tramos completos desde 24 y 35 segundos del host. El límite de bytes
cerró los tramos antes de los dos segundos solicitados: 141537/145221 registros,
12/9 presentaciones y 54535167/54534992 bytes, respectivamente.

En las 21 presentaciones, PMODE es `0x8023`, los dos DISPFB apuntan a la página
208 y `hasPreferredSource` es falso. Por tanto, la regla que sustituye el destino
por una textura preferida **no explica esos tramos**. La prueba sintética privada
que muestra una mezcla del 75 % presentada como fuente pura demuestra un defecto
separado de esa regla, pero no justifica modificarla como arreglo de esta intro.

Repetir los prefijos hasta la primera/última presentación de cada tramo con CPU
produce el encuadre desplazado: brazo/espalda frente al fuego y cabeza ampliada
frente al cielo. Las dos instancias CPU coinciden byte a byte en esos prefijos;
esto no valida la captura GPU completa. De hecho, repetir el End GPU del tramo
35 s con CPU difiere en 498626 bytes de VRAM. Se conserva la discrepancia como
limitación, sin atribuirla automáticamente a la cámara ni certificar fidelidad.
La grabación PCSX2 aportada muestra otro encuadre; falta comparar los comandos y
el estado de cámara/animación en el mismo tiempo simulado. No se ha corregido
ese fallo visual ni medido una mejora de FPS.

Validación: build rápida del juego correcta; suite de herramientas GS de Windows
(captura, checkpoints, píxeles, feedback y oráculos) correcta; selección de estados
4/11 con memoria sintética, rechazo de valores inválidos, frontera TEXFLUSH y
vigilancia del perfil correctos. Configuración: cero errores y cuatro avisos
preexistentes; sintaxis PowerShell: cero errores. Volcados e imágenes quedan en
carpetas privadas excluidas de Git. Siguiente paso: aislar los dibujos de Kratos
antes de buscar una corrección del framebuffer o de la cámara.
### 2026-10-09 — Observación de las cámaras antes de dibujar la escena

La lectura privada del framebuffer 0 del tramo de fuego, antes del primer sprite
que lo copia al 208 (registro 10720 de la captura 24 s), ya muestra el brazo/espalda
con el encuadre incorrecto. Los 16 sprites siguientes copian 512 columnas en franjas
de 32, sin mezcla alpha. En las 21 presentaciones analizadas, los registros completos
DISPFB/DISPLAY permanecen constantes. La imagen equivocada precede a esa copia final.

Se añade `GOW_CAMERA_DIAG=1`, desactivado por defecto, en el retorno completo de
`renView::SetupPipeline` (`0x23B5E0`). El original siempre se ejecuta y las
reanudaciones de checkpoints no se interpretan como retornos. La sonda observa
matrices mundo/inversa/proyección/combinada del `renView` y las del cliente de
cámara, copiando sus bits sin operar floats en el juego. Los offsets proceden del
MIPS retail de `0x1697F0`: vista+0/0x40/0x100/0x240 y cliente+0x20 o +0x70 / +0xB0.
La lista se toma de vista+0x360, cliente en nodo+8; el sentinel se compara como
la instrucción `beq`, con la dirección completa. Las lecturas validan rango de RAM.

La marca `0x29BDF8` se incrementa al actualizar jerarquías; **no es un contador de
cuadros**. Una primera sonda limitada por esa marca consumía sus muestras demasiado
pronto. La versión final limita cada par vista/estado a 32 observaciones separadas
por dos segundos del host, con un máximo de 16 pares. Registra también vista activa,
identificador de vista y marcas de actualización de mundo/inversa. Las vistas sin
cámara se distinguen de las que contienen nodos o clientes inválidos.

Control funcional final sobre `c035b3b`, runtime 77, SHA-256 del ejecutable
`01046ba112c887041fef06acea9b5784bed7b3ff4c1ba175729955f3e78f64fd`:
95 segundos, OpenGL y presentación RAM, FINISH síncrono explícito, sin fast boot,
FMV omitido y captura GS desactivada. Hay 148 muestras: 86 en intro y 62 en partida,
nueve pares vista/estado. Las 42 muestras con cliente de cámara contienen matrices
finitas; sus matrices mundo/inversa coinciden bit a bit con las de la vista.
La mayor desviación de longitud de sus ejes respecto a uno es `2.22e-7`; el mayor
residuo absoluto del producto mundo×inversa respecto a identidad es `2.58e-4`.
Todas las matrices registradas, incluidas las vistas sin cámara, son finitas.
No se detectan NaN ni la escala estirada anterior en estas observaciones.

Esto no demuestra que se seleccione la cámara correcta ni que las poses, la
jerarquía de huesos o el rasterizado sean equivalentes a PCSX2. Los tres savestates
locales de PCSX2 son de la partida; sus capturas no corresponden a la transición.
La próxima comparación requiere aislar la pose de Kratos y la vista usada en el
mismo instante de la intro. El fallo visual continúa abierto.

Validación: `camera_probe_test.cpp` con MSVC `/O2 /W4 /WX` pasa copia exacta de bits
(incluidos NaN sintéticos), selección local/articulada, alias, sentinel, ausencia,
memoria truncada, límites exactos y memoria intacta. La CI ejecuta el mismo control
con GCC. Build rápida del juego correcta; vigilancia del perfil correcta, incluyendo
limpieza/restauración de `GOW_CAMERA_DIAG`; configuración con cero errores y cuatro
avisos preexistentes; sintaxis PowerShell sin errores. Registros, imágenes y sondas
privadas permanecen excluidos de Git; no se modifica EE, FPU, IOP ni VU1.

Dato de rendimiento transmitido por el usuario: Claude comunica dos rondas
alternadas de `GOW_GS_FINISH_ASINCRONO=1` junto con `PS2X_GS_DIRECT_PRESENT=1`, de
7.3–7.4 a 11.1–11.4 cuadros/s. Esa combinación queda como candidata para una
comparación propia de los cuatro modos tras resolver la transición. No se valida
aquí esa cifra ni se cambia ningún valor predeterminado.
## Modelos durante la transición menú/intro (2026-10-09)

El control anterior de `GOW_MODEL_DIAG=1` agotaba las 64 muestras «early»
en el menú (estado 3); no quedaban observaciones de ProcessModel en la intro
(estado 4). Se separan los cupos de intro, partida (11) y restantes estados
para servidor, contexto, maestro y Clip. ProcessModel tiene una tabla por fase
con hasta 64 pares modelo/vista, 32 muestras por par y dos segundos del host
entre muestras. Los objetos del menú no ocupan la tabla de intro.

La misma variable añade `[gow-model:pose]` y `[gow-model:matrix]`: bits de
matrices local/mundo del objeto y mundo de su articulación raíz. Los offsets
proceden del MIPS retail de CalcWorldMatrix (`0x1303D0`) y CalcSkinHierarchy
(`0x137508`): objeto+0x20/0x70, esqueleto en objeto+0x104, raíz en
esqueleto+0x86 y matrices articuladas desde esqueleto+0x8C con pasos de 64 bytes.
El número de articulaciones se lee de definición+0x10, desde esqueleto+0x60.
Las marcas de actualización se conservan completas (64 bits). Se validan
rangos e índices sin calcular matrices, escribir RAM ni forzar actualizaciones.
Estas son fotografías de entrada a ProcessModel: pueden incluir buffers
anteriores y no demuestran por sí solas qué matriz terminó enviada a VU1.

El control sintético comprueba que un menú agotado deja pasar la intro,
separación por vista y fase, alias físicos, intervalo, límite de muestras,
copia exacta de bits, memoria intacta y matrices truncadas o fuera de rango.
MSVC `/O2 /W4 /WX` pasa; la CI ejecuta el mismo control con GCC.

Se integra `eebf73b` (PR #42 de Opus) antes de la compilación completa del juego.
REA está conectado, pero `open_binary` rechaza este ELF como arquitectura no
soportada: no se atribuye a REA una descompilación ni validación del R5900.
El fallo visual de encuadre continúa abierto.

Control nativo final: `scripts/2_compilar.cmd` completo, 78 parches, retorno 0;
ELF SHA-256 `ec9397d252f14d4412daffaad6be1f1931769e56222b5dbb9b39a947fc8ad71c`.
Ejecutable de `eebf73b + 54c2dd1`, SHA-256
`b67f4e7b9b3377ecdc05b5d8a8033c8acf60f665050ec9ebf8174e51e70a4569`.
En 95 segundos con OpenGL, presentación RAM, FINISH síncrono, fast boot
apagado, FMV omitido y prueba de pad: 123 muestras de modelos en menú y
343 en intro, respectivamente 15 y 11 pares modelo/vista. Los 466 objetos
observados son legibles; 84 lecturas de raíz son válidas. Se registran 1.016
matrices, ninguna no finita ni totalmente nula. La ausencia de una raíz válida
en los demás objetos no se interpreta como corrupción: hay objetos sin
paleta articulada. Los dos modelos articulados observados en intro tienen
125 y 103 articulaciones; sus matrices de raíz son constantes en las 32
muestras de cada uno. No se identifica todavía cuál recurso es Kratos ni se
comprueban aquí las matrices de las otras articulaciones.

El control terminó por su límite temporal deliberado, sin muestras del estado
11. Una segunda ejecución del mismo binario, otros 95 segundos, con
`GOW_MODEL_DIAG=0` y cámara activa, tampoco registra ese estado (128 muestras
de cámara en estado 4, ningún registro de modelos). Esto no establece una
causa ni equivale a verificar visualmente la partida. No se comparan FPS.
Ambos procesos propios finalizaron; no se modificó el checkout de Opus.

La prueba local que vuelve a compartir el cupo de menú/intro falla, como se
espera. Las instrucciones de las dos funciones usadas para recuperar offsets
coinciden con el ELF local: 254 de `0x1303D0` y 380 de `0x137508`, sin huecos ni
bytes distintos. Ghidra 12.1.4 y `ghidra-emotionengine-reloaded` importan ese ELF
con `r5900:LE:32:default` y descompilan ambas funciones en un proyecto privado,
sin análisis global. REA 6.2.0 no admite MIPS en su lector ELF, proveedor Ghidra
y enums de evidencia; además su servidor no tiene `GHIDRA_INSTALL_DIR` definido.
La prueba directa de Ghidra no demuestra soporte del puente REA.

Siguiente comparación: identificar el modelo y las articulaciones del personaje,
y relacionarlas con el cliente de cámara durante el mismo instante de PCSX2.
La sonda no modifica renderizado ni arregla el encuadre; registros y proyecto
Ghidra permanecen privados.

## Referencia pausada de PCSX2 para la intro (2026-10-09)

Referencia nueva con la ISO local `E:\gowport\God of War.iso`, PCSX2
`v2.9.114`, serial `SCUS-97399`, CRC `D6385328`. Se arrancó desde menú,
se seleccionó New Game / Hero y se pausó la intro con Kratos de frente,
ya separado del primerísimo plano. Vulkan, resolución interna 3x,
parches incorporados del emulador activos y cheats/widescreen desactivados.
Este cuadro no está sincronizado con los cuadros anteriores del port:
no permite una comparación de píxeles ni de toda la animación.

Perfil y dos savestates nuevos privados bajo
`E:\gowgpuprof\pcsx2_reference\PCSX2`; no se usaron las ranuras del usuario.
`-datapath` añade `PCSX2` al directorio suministrado en esta instalación.
La instalación original tiene compatibilidad RUNASADMIN y las dos instancias
ocultas iniciales no pudieron cerrarse desde el proceso sin privilegios.
La referencia se obtuvo con una copia local del PCSX2 ya instalado, sin
modificar esa compatibilidad ni los ajustes originales.

`tools/render/pcsx2_ee_reference.py` lee EE por PINE local, comprueba pausa,
serial/CRC, longitudes de respuesta y estabilidad de la marca de jerarquía.
No escribe memoria ni carga estados. Se capturaron 32 MiB en estado 4,
marca 32537, SHA-256
`3219f816196c231b37ff6490f9b477fd7c6184b6a43044c499cb074958aa94c8`.
La lectura repetida coincide byte a byte con el primer volcado privado.
El puntero `0x33104C` vale cero en esta pausa entre cuadros; no interpretar
ese cero como matrices nulas ni como corrupción de cámara.

La vista persistente `0x75CD70`, id `0x45`, enlaza al mismo cliente de cámara
`0x1116990` que el port; su padre es `0x1111F20`. Los objetos articulados
`0x1112470` y `0x753E20` tienen respectivamente 125 y 103 articulaciones.
La raíz de 125 articula la misma traslación que las 32 muestras anteriores
del port: `(1048.109985, 727.026001, 1007.25)`. Frente a una de esas muestras,
7/16 componentes coinciden por bits y la mayor diferencia absoluta es
`3.725290298461914e-08`; los signos de orientación X/Z son negativos en ambos.
Esto acota la hipótesis de un giro de 180 grados de esa raíz, pero no verifica
la paleta completa, el modelo visible seleccionado ni el cálculo VU1/GS.

Al separar la vista principal de las vistas vacías, las 32 muestras nativas
en estado 4 contienen ocho matrices mundo distintas: la posición pasa de
`(1047.542725, 761.248352, 1017.296326)` a
`(1047.589844, 754.348328, 1031.195313)`. La cámara no está totalmente congelada
en ese control. PCSX2 se pausó más adelante en el recorrido, en
`(1047.635132, 749.300110, 1041.421509)`; restar esas posiciones como si fueran
del mismo instante produciría una falsa discrepancia.

Siguiente paso: comparar las articulaciones no raíz y el padre de cámara
con una secuencia alineada. La referencia y su captura permanecen fuera de
Git. No se cambia el renderer ni se afirma una mejora de FPS con este control.

## Primeras articulaciones y padre de cámara (2026-10-09)

Se amplían los diagnósticos opcionales de modelos y cámara, sin escribir RAM,
calcular matrices ni forzar actualizaciones. ProcessModel lee como máximo las
primeras ocho matrices de la paleta, limitadas por el count de skin y los
rangos físicos de RAM. SetupPipeline registra la matriz de la articulación del
padre seleccionada por el cliente de cámara. Los bits se conservan sin operar
floats; `validJoint` indica lectura válida, no identidad semántica comprobada.
La cámara no usa el count de skin: en la referencia PCSX2 vale cero, aunque
su articulación 1 es legible y coincide con la vista.

Controles locales MSVC `/std:c++20 /O2 /W4 /WX`: cámara y modelos pasan,
incluidos alias, memoria intacta, NaN sintéticos, count 0/3/20, límite de ocho,
punteros nulos y matrices truncadas o que cruzan el segmento. Una mutación
privada del paso de paleta de 64 a 32 bytes hace fallar el control de modelos.
El lector C++ sobre la RAM privada de PCSX2 recupera ambas paletas iniciales
y confirma igualdad por bits entre vista, cliente y articulación del padre.

Compilación completa con `scripts/2_compilar.cmd`, 79 parches, retorno 0.
Código de la sonda `1fc7ff7`, sobre `c8c9860`; ELF retail con el SHA-256 ya
registrado arriba. Binario SHA-256
`5b1c81330f499fca55180dce830160bdeafbaa757ef8971e3e1692a95820cdbd`.
Control nativo de 95,612 segundos con cámara/modelos activos, OpenGL,
presentación RAM, FINISH síncrono, fast boot apagado, FMV omitido y prueba
de pad. Se finaliza el proceso propio al alcanzar el límite; el registro del
host no dispone de ExitCode. No se registran muestras de estado 11 ni se
atribuye el límite a un fallo del juego. La captura final muestra un primer
plano frontal de Kratos, pero no está alineada con la referencia PCSX2 y no
demuestra una corrección del encuadre anterior.

Se leen 656 matrices de articulaciones: ninguna no finita ni totalmente nula.
Las 32 muestras válidas de cámara coinciden por bits entre viewWorld,
clientWorld y parentJointWorld. El padre `0x1111F20`, esqueleto `0x1114130`,
paleta `0x112ED00` y articulación 1 (`0x112ED40`) coinciden estructuralmente
con la referencia. En intro, los modelos `0x111B700` (125 articulaciones) y
`0xD51C80` (103) tienen 32 muestras por cada índice 0..7. En el primero, los
índices 3..7 cambian y la raíz permanece constante: una raíz constante no
implica animación congelada. En el segundo las ocho matrices son constantes
durante este tramo observado.

Frente al único cuadro posterior de PCSX2, la diferencia absoluta máxima
de la raíz de 125 es `3,7253e-8`; en índices 1..4, la última muestra queda
entre `6,1035e-5` y `1,8311e-4`. Las articulaciones animadas 5..7 llegan a
`0,048279` en la última muestra. En la paleta de 103 el máximo de los ocho
índices es `1,1444e-5`. Son diferencias descriptivas de instantes distintos,
no tolerancias de equivalencia ni prueba de fidelidad de la animación.

Quedan sin verificar las restantes 117/95 articulaciones, la identidad del
recurso visible y los datos que terminan enviados a VU1. El siguiente paso es
alinear la secuencia y seguir la paleta hasta InitUNPACKData/VIF; no cambiar
la matemática por estas diferencias sin una comparación del mismo instante.
No se mide rendimiento ni cambia el estado del renderer en el mapa. Registros,
capturas y volcados permanecen privados; no se modifica el checkout de Opus.

## Paleta completa antes de ProcessModel (2026-10-09)

`GOW_MODEL_DIAG=1` y `GOW_MODEL_PALETTE_DIAG=1` permiten leer hasta 256
articulaciones por muestra, con los mismos cupos y frecuencia anteriores.
Cada matriz conserva sus bits y la marca de esqueleto; se registra el count
solicitado, las matrices leídas y si se alcanzó ese count. El límite de 256
solo acota el diagnóstico. Se detiene antes de leer una matriz fuera de RAM,
sin cambiar ninguna matriz ni la ejecución original. Cada línea nueva de
matriz se emite en una sola escritura para evitar intercalar componentes con
registros de otros hilos. Una primera prueba con escrituras por componente
produjo líneas intercaladas y no se usa para estas cifras.

Pruebas MSVC `/O2 /W4 /WX`: paletas 103/125/256, petición de 257 truncada,
alias, bits NaN intactos, último byte ausente, NULL, segmento y RAM intacta.
Limitar el lector completo a ocho matrices en una copia privada hace fallar
la prueba. El mismo lector sobre la RAM pausada de PCSX2 lee completas las
125 y 103 articulaciones sin modificar la referencia.

Se integra sobre `98d06c1`; código de la sonda `4ea6204`, runtime de 79
parches sin cambios. `scripts/2_recompilar_rapido.cmd` termina con retorno 0;
se comprueba igualdad de headers/override entre fuente y runtime. SHA-256 del
ejecutable privado utilizado y del binario recompilado:
`80c8cfa9bed8d53e25efdfbbfbf80f9bd1d81f46ff2d20d40d724d1953e1a4e1`.
No se regeneran las fuentes del juego ni se modifica el checkout de Opus.

Control de 95,948 s, OpenGL con presentación RAM y FINISH síncrono,
fast boot apagado, FMV omitido y prueba de pad. El proceso propio se termina
por el límite deliberado; ExitCode no está disponible. Se leen 9.598 matrices
en 84 paletas completas, sin matrices no finitas ni totalmente nulas. Se
observan estados 3/4; no hay registros de estado 11. No se mide rendimiento
ni se valida partida o equivalencia visual.

En intro se obtienen 32 muestras por cada una de las 125/103 articulaciones
de `0x111B700` / `0xD51C80`. Cambian 116 índices de la primera paleta; toda
la segunda permanece constante durante este tramo. Frente al único cuadro
posterior de PCSX2, la última muestra tiene diferencia absoluta máxima
`0,0802001953125` (índice 20) / `0,000011444091796875`. Como los instantes
son distintos, estas cifras no son tolerancias de equivalencia. El encuadre
y la identidad del recurso visible siguen pendientes.

Ghidra 12.1.4 con el lenguaje R5900 recupera en modo de solo lectura las
rutinas retail `0x158DA8`, `0x1590F0`, `0x157100` y `0x166BE8`. La llamada
`0x158B94` actualiza la jerarquía en `0x1590F0` antes de procesar el grupo
en `0x158D48`; las llamadas `0x159570` / `0x15958C` seleccionan
CalcSkinHierarchy / su variante según la visibilidad. La rutina recibe en
a2 un buffer de matrices reservado para el paquete, distinto de la paleta
mundo observada a la entrada de ProcessModel. Por ello la paleta de entrada
no demuestra qué transformaciones consumió VU1. El siguiente control debe
leer ese destino después del skinning y relacionarlo con VIF, respetando las
reanudaciones de la función y alineando la secuencia con PCSX2. Pseudocódigo,
RAM y registros permanecen privados.

## Perfil de CPU por hilo en los muelles: el cuello de botella es VU1 (2026-10-10)

`tools/rendimiento/muestreo.cpp` es un muestreador externo para Windows: suspende cada hilo del proceso
cada 1 ms, lee su RIP y al final resuelve funciones, archivos y líneas con el `.pdb` del runner. Da la
ocupación real de cada hilo (muestras fuera de las funciones de espera del núcleo). La forma de compilarlo
y de usarlo está en la cabecera del archivo. A diferencia de `GOW_PERF_DIAG=1`, no añade código al runner.

**Medido** (main `895bfad`, OpenGL con presentación compartida, `GOW_GS_FINISH_ASINCRONO=1`,
`PS2X_VU1_HILO=1`, `PS2X_GS_FRENTE=1`, guion de mando del A/B, 60 s a partir de t=150 s en los muelles,
20,0 fps):

| Hilo | Ocupado | Qué hace |
|---|---|---|
| VU1/VIF1 (`PS2X_VU1_HILO`) | **91,5 %** | ~66 % en bloques de microcódigo compilado (`programa1..12`), 8,6 % en el despachador `vu1c_gen::run` (el `switch (pc)` solo, ~1,4 %), 11,4 % en `ps2_vu1_core.cpp` (`commitReadyPipelines` 3,8 %, XGKICK, flags, pares interpretados), 3,4 % en `processVIF1Data` |
| GameThread (EE + IOP) | 55 % | el resto espera; el IOP y sus temporizadores ocupan ~25 % de este hilo |
| GSThread | 20 % | |
| frente GS (`PS2X_GS_FRENTE`) | 13,5 % | |

Consecuencias:

- **El camino crítico es el hilo de VU1.** El cuadro dura lo que tarda VU1 (~46 ms), no la suma de EE y VU1:
  ya se solapan.
- **Los fps van en escalones** porque el juego espera VBlank: 60/30/20. En los muelles va clavado a 20,00
  (3 VBlanks). Para pasar a 30 fps el hilo de VU1 debe bajar de ~46 a <33 ms por cuadro (**-28 %**). Una
  mejora menor no cambia los fps: hay que medirla con la ocupación de este muestreador, no con
  `guest_flip_hz`.
- El perfil del hilo de VU1 es plano (ninguna línea pasa del 1,5 %): el coste está en cada par de cada bloque.
  La siguiente mejora grande tendría que reducir el coste por par en `stepPairT`, por ejemplo resolviendo al
  generar las esperas y las latencias de los registros que se escriben dentro del mismo bloque.

**Resultado negativo: agrupar los ciclos del IOP.** `iop_ms` de `GOW_PERF_DIAG=1` atribuía al IOP 2,2–3,2 s
de cada 5 s, y `PS2X_IOP_PC_EVERY` mostró que el IOP solo ejecuta ~0,6 M instrucciones/s: el coste es la
gestión de `runCycles`, que se llama en cada checkpoint del EE (32 ciclos del EE = 4 del IOP) y repasa SPU2,
DMA, callbacks, temporizadores y planificador. Además, la propia medición (dos lecturas del reloj en cada
checkpoint) infla mucho `iop_ms`. Se probó acumular los ciclos del IOP en lotes de 256 ciclos suyos dentro de
`IopEmulator::runEeCycles` (suite 591/591). En la misma escena: **19,92 fps con el lote frente a 19,97 sin
él** (dos rondas alternas). No hay mejora porque el EE no es el camino crítico. Además, el lote provocó
atascos de varios segundos en la carga (host_hz < 1) que desfasan el guion del mando. En una de las rondas
el juego acabó en otra escena (estado 4 → 14) a 60 fps, lo que parecía falsamente una mejora de 20 → 55 fps.
Se descartó y no se publica. Lecciones: comprobar siempre en el registro que las dos variantes siguen la misma
secuencia de estados (`[gow-pad2:state]`), y `scripts\probar_rendimiento.ps1` borra `PS2X_IOP_PC_EVERY`
(para muestrear el IOP hay que llamar a `ejecutar.ps1` directamente).

## VU1 compilada más rápida: despachador optimizado, esperas imposibles y flags por tabla (2026-10-10)

Con el muestreador por hilo (`tools/rendimiento/muestreo.cpp`, que ahora también da un histograma por
instrucción con un quinto argumento) se cruzaron las muestras con el desensamblado (`dumpbin /disasm`).
Cuatro cambios, todos exactos:

- **El despachador `vu1c_gen::run` estaba sin optimizar.** Era un único `switch` con miles de casos y MSVC no
  optimiza funciones tan grandes: lo compilaba como con `/Od` (`pc()` como llamada, variables en la pila).
  Ocupaba el 8–9 % del hilo de VU1. Ahora cada dirección tiene una función pequeña y `run` usa una tabla
  (`tools/vu1/generar_vu1.cpp`): 8,7 → 2,3 % en el banco.
- **Esperas imposibles.** Cada par de un bloque avanza al menos un ciclo. Si el último escritor de un
  registro leído es un par anterior del mismo bloque a una distancia >= su latencia, esa lectura no puede
  detener el par: el generador la quita de `readyCycleT` (struct R, idéntica al par K salvo las lecturas).
  Se quitan 26.090 de 57.305 lecturas.
- **Flags MAC/estado por tabla.** La inversión de bits de las máscaras de carriles (~20 instrucciones por
  FMAC) pasa a ser una consulta en una tabla de 256 entradas. `recordViWriteForBranch` se expande en línea
  (era una llamada en cada par que escribe un VI) y desaparece el store de `m_state.cycles` en cada par
  (`run()` lo fija al terminar y nada lo lee mientras tanto). `patches/ps2recomp-vu1-fast-pairs.patch`.
- **Escrituras de VF de 16 bytes.** `applyDestT`, `applyDest` y el reinicio de VF0 escribían carril a carril.
  Ahora mezclan y escriben 16 bytes, para que el load de 16 bytes del par siguiente pueda reenviarse
  desde el store. Efecto medido pequeño (~1 %).

**Comprobado:** huellas por lanzamiento idénticas entre el intérprete, la VU1 compilada de main y la nueva en
`vif_pcsx2_inicio2` y `vif_port_480s` (incluyen ciclos y flags), suite 590/590, `probar_vu1_compilada` y
`probar_vu1_fmac` (con FTZ/DAZ) correctas.

**Banco** (`repetir_cadena_vif`, VIF1/VU1 sin rasterizar, 40 pasadas, mediana de 5 rondas alternas):
`vif_pcsx2_inicio2` 65,1 → 55,4 ms por cuadro y `vif_port_480s` 71,0 → 60,6 ms (**−15 %**).

**Juego** (muelles de Atenas, muestreador de 60 s desde t=150 s, mismo guion y mismas opciones que el A/B):

| | Hilo de VU1 ocupado | fps | VU1 por cuadro |
|---|---|---|---|
| main | 90,8 % | 20,0 | ~45 ms |
| esta versión | 78,3 % | 20,5 | ~38 ms |

El juego espera VBlank y en los muelles sigue en el escalón de 20 fps: para pasar a 30 la VU1 debe bajar
de ~31 ms por cuadro, otro ~19 %. Medido también: sin normalizar operandos (experimento, resultado
incorrecto) el banco baja ~5 %; sin comprobar esperas, ~3 %.

**Experimentos que engañaban.** Quitar el reinicio de VF0 parecía dar −41 %, pero algún microprograma
escribe en VF0 y el juego hacía otro trabajo; las variantes que cambian el comportamiento (sin flags, sin
VF0) no sirven para medir. Hay que comprobar las huellas también en los experimentos.

**Guion de mando.** Con un arranque rápido, `5:start` elige New Game en el menú principal y el A/B mide la
intro (cámara en movimiento, 16–57 fps, errores XGKICK de formato 3 en main y en la versión nueva) en lugar
de los muelles. El guion `13:abajo,20:x,62:x` carga la partida en ambos casos (ver `CONTROLES.md`).

**Cuelgues esporádicos** con `PS2X_VU1_HILO=1`, `PS2X_GS_FRENTE=1`, `GOW_GS_FINISH_ASINCRONO=1` y presentación
compartida, en 2 de 8 partidas: una vez con main el juego dejó de presentar cuadros (`guest_flip_hz=0`)
mientras la ventana seguía viva, y otra vez con esta versión se detuvo el hilo de presentación
(`[gow-perf]` a los 15 s) mientras el juego seguía a 20 fps. Pendiente de investigar.

Siguientes pasos para VU1: XGKICK perezoso (copiar el paquete cuando la VU escribe en su memoria o al
acabar, en vez de llamar a `progressXgkick` en cada par), confirmar los flags por lotes, operandos sin
normalizar con DAZ/FTZ, y bloques con temporización estática.

## VU1, ronda 2: despacho por última variante, PATH1 por eventos y FLG=3 (2026-10-10)

`muestreo.cpp` admite ahora un sexto argumento `pilas`: guarda la pila de cada muestra (StackWalk64) e imprime
las cadenas de llamadores de las funciones elegidas (varias separadas por `|`). Con eso se localizaron los
costes del hilo de VU1 fuera de los bloques. Todos los cambios son exactos:

- **Despacho por última variante** (`tools/vu1/generar_vu1.cpp`). Una dirección puede tener hasta 13
  variantes de bloque, una por microcódigo, y se probaban en orden: para el bloque más caliente de los
  muelles, cuatro llamadas fallidas antes de la buena. Ahora cada dirección recuerda la última variante que
  coincidió y la prueba primero. Banco: −5 % / −7 %.
- **Decodificación perezosa** (`ps2recomp-vu1-lazy-decode.patch`). En cada cambio de microcódigo se
  decodificaban los 2048 pares (~1 % del hilo de VU1) aunque el código compilado no usa esa caché. Ahora cada
  par se decodifica la primera vez que se pide y `m_statusQuiet` sale de recorrer las palabras.
- **Búferes del árbitro GIF** (`ps2recomp-gif-arbiter-buffers.patch`): cada paquete (XGKICK, DMA) reservaba
  un vector nuevo; ahora se reutilizan los ya procesados.
- **PATH1 por eventos** (`ps2recomp-vu1-path1-events.patch`). Mientras hay un XGKICK activo (casi siempre),
  cada par llamaba a `progressXgkick`. La copia solo depende del número de ciclos y de la memoria de datos, así
  que ahora se cuenta de una vez y se pone al día antes de que la VU escriba en su memoria, antes de mirar si
  el envío sigue activo, al terminar y en cada evento (copia de una GIFtag o fin del paquete), que ocurren en
  el mismo ciclo que antes. Las seis pruebas de XGKICK de la suite pasan, incluidas las de stores durante el
  envío y la espera del segundo XGKICK.
- **FLG=3 en XGKICK.** Una GIFtag con FLG=3 ("disable") funciona como IMAGE en el GS (y así la trata ya el
  frontend), pero XGKICK la rechazaba como instrucción reservada (`0xFFFFFFF8`) y detenía el microprograma.
  Ocurre en la intro y, según lo que haya en pantalla, también en los muelles: en esas partidas faltaba
  geometría y el cuadro salía más barato (29–30 fps falsos). Prueba de regresión nueva en `ps2_vu1_tests.cpp`.

**Comprobado:** huellas por lanzamiento e imágenes de `vif_pcsx2_inicio2` y `vif_port_480s` idénticas a main
(compilado e intérprete), suite 591/591, `probar_vu1_compilada` y `probar_vu1_fmac` (100.800 casos + 2.016 con
FTZ/DAZ) correctas.

**Banco:** `vif_pcsx2_inicio2` 55,4 → 52,1 ms y `vif_port_480s` 60,6 → 55,7 ms por cuadro (desde main, −20 %
y −22 %). **Juego** (muelles, mismas condiciones que en la ronda 1, sin errores de VU1):

| | Hilo de VU1 ocupado | fps | VU1 por cuadro |
|---|---|---|---|
| main | 90,8 % | 20,0 | ~45 ms |
| ronda 1 (#54) | 78,3 % | 20,5 | ~38 ms |
| ronda 2 | 82,3 % | 24,0 | ~34 ms |

Para 30 fps estables faltan ~3 ms por cuadro (~10 %).

**Dependencias de compilación rotas con MSVC en español.** `rules.ninja` tiene el prefijo
`Nota: inclusión del archivo:` en UTF-8, pero `cl.exe` lo escribe en la página de códigos OEM: ninja no
registra ninguna dependencia de cabeceras (`ninja -t deps` da `#deps 0`). `compilar.ps1` ya borraba los
objetos del runtime, de las pruebas y del unity por eso, pero no los de la VU1 compilada (`vu1_generado`, sin
PCH): tras cambiar `ps2_vu1.h`, los bloques quedaban con la disposición anterior de `VU1Interpreter` y el juego
se quedaba en un bucle de espera infinito al arrancar. `compilar.ps1` los borra ahora también. Con
`cmake --build` a mano hay que borrarlos igual tras cambiar cabeceras.

**Descartados tras analizarlos:**
- *Operandos sin normalizar con DAZ.* `normalizeOperand` es por bits, pero el intérprete calcula en float
  productos que pueden ser subnormales y luego los suma; con DAZ, `normalizeFmacExactResult` conservaría otro
  último bit. No es equivalente.
- *Saltarse la normalización de resultados de FMAC.* Solo ADD/SUB/MUL/MAX/MINI/ITOF/ABS dan siempre valores
  normalizados: una MADD/MSUB por la ruta escalar puede dejar un subnormal (dos redondeos con el exacto justo
  por encima de FLT_MIN), así que no se puede suponer en general.
- *Agrupar los ciclos del IOP* (ver la entrada anterior).

Siguiente: bloques con temporización estática (comprobar las esperas una vez a la entrada del bloque en lugar
de en cada par), vida de los flags MAC entre bloques y la copia de PATH1 por tramos.

## Integración del diagnóstico compartido con #55 (2026-10-10)

La rama `codex/shared-frame-diagnostics` integra mediante merge el `main`
`7800bc27c27ab5ddb0dd19338b834e0e2eff3829`. Se conservan todos los cambios
de #55; los dos conflictos de inserción en `scripts/compilar.ps1` se resuelven
añadiendo el diagnóstico del host después de los tres parches nuevos de Opus.
No se modifica su checkout ni se detienen sus procesos.

Control ligero local: seis pruebas del inspector correctas (Python 3.14.7),
configuración con 0 errores y 4 avisos existentes, sintaxis de 20 scripts
PowerShell con 0 errores. Los 90 parches únicos pasan comprobación y aplicación
en orden en un worktree nuevo sobre el commit fijado
`c5a9d02573410a2085a4b4b831b0b68ba3515440`; el diff del runtime existente
coincide antes y después. Registro privado: `logs/transition_review/patch_chain_90.txt`.

Se comprueba que `E:\gowgpuprof` es una junction a nuestro
`E:\Decompilación de God of War\gow-gpu-work`. No se limpia ni reconstruye ese
runtime. El PC continúa reservado para Opus: no se ejecutan compilaciones,
juegos ni benchmarks. La validación Windows completa y la transición con el
HEAD integrado siguen pendientes; #51 conserva el estado de borrador.
El protocolo y la matriz de cuatro controles están en `TRANSICION_COMPARTIDA.md`.
No se declara corregida la alternancia, el encuadre ni los cuelgues intermitentes.

En la revisión posterior del inspector se reproduce una omisión con entradas
sintéticas: una secuencia que reaparece después de otra no se contrastaba con
sus metadatos originales. `inspect_shared_frames.py` conserva ahora esa
referencia hasta el cambio de modo; el tick del host puede variar. Las filas
inconsistentes no alimentan los patrones de retorno. Ocho pruebas pasan; la
nueva regresión falla con la versión anterior para hash, validez, dimensiones
y secuencia de renderizado. No se cambia el parche del runtime ni se ejecuta
el juego: es una mejora de análisis, no un arreglo visual demostrado.

## Validación Windows de la cadena de 90 parches (2026-10-10)

El usuario confirma que el PC está libre. Se vuelve a traer `origin` y la rama
permanece sincronizada con `main` en `7800bc2`, mediante el merge existente.
Se conserva un snapshot privado de los cambios anteriores del runtime y se
comprueban los destinos de limpieza bajo nuestro `gow-gpu-work`; no se toca
el checkout de Opus ni se detienen sus procesos.

`scripts\2_compilar.cmd`, con `GOW_WORK=E:\gowgpuprof`, termina con código 0
para `76a25bedac7842e0a35cbd5eb56ca5941ebae429`, con los 90 parches, Release,
trazas desactivadas, tres tareas paralelas y VU1 en intérprete. El SHA-256
del ELF coincide con el esperado. Ejecutable nuevo:
`36e632cd39dfd73e3de3573c38b8c87715f9ef65b1eb6b2bba3ef776ed457c9f`.
Manifiesto y registro privados: `E:\gowgpuprof\transition90\build.json` y
`full_build.log`. Este binario sí incorpora la cadena actual; no se usa el
ejecutable anterior para atribuirle estos cambios.

La prueba Windows de paquetes libpad2 pasa. La suite nativa pasa **591/591**
en modo normal y **625/625** con `GOW_GS_GPU_TEST=1`, incluyendo GPU real,
compositor compartido y consumidor raylib. La primera ejecución desde una
carpeta de registros falló porque una prueba busca `instructions.h` por rutas
relativas; se repitió desde la raíz del runtime, sin modificar código ni
pruebas. El diagnóstico opcional se activó durante el control GPU: seis
adquisiciones analizadas, hashes conocidos y ningún retroceso, inconsistencia
o retorno A–B–A en esas muestras procedurales. No acreditan imágenes del juego.

La matriz funcional se ejecuta en carpetas privadas, con una copia por pasada
de la misma tarjeta procedural sin formatear (8.650.752 bytes), sin guardados
del usuario; no se escribe sobre su tarjeta original. Se eliminan del entorno
los otros diagnósticos opcionales y las capturas periódicas; se mantienen
`GOW_FAST_BOOT=0`, los wrappers que instala por presencia y el panel de depuración
predeterminado. VU1/frente GS en hilos permanecen apagados. El hash/registro no
se emplea para medir FPS.

| FINISH / presentación | Duración de proceso | Primer estado 11 del guion | Adquisiciones compartidas |
|---|---|---|---|
| Síncrono / RAM | 360,06 s | 146,64 s | — |
| Síncrono / compartida | 360,04 s | 121,16 s | 2.186 |
| Asíncrono / RAM | 360,05 s | 116,32 s | — |
| Asíncrono / compartida | 233,22 s | 111,31 s | 1.247 |

Las tres primeras pasadas se terminan deliberadamente al límite de 360 s.
La cuarta se interrumpe a petición del usuario, que necesita el ordenador:
se valida y termina solo nuestro PID 11320. Su código de salida de terminación
no se atribuye a un fallo espontáneo. Las cuatro llegan a estado 11 y sus
últimos informes continúan en ese estado, sin las firmas buscadas de error
VU1/GIF/excepción. Las tarjetas permanecen idénticas. El guion usa reloj del
host: esos tiempos no alinean cuadros ni comparan rendimiento.

Los dos registros compartidos tienen hashes conocidos, sin retrocesos de
secuencia/render ni metadatos inconsistentes. Registran 105/109 retornos A–B–A,
todos anteriores al primer informe de estado 3. Ninguno aparece en las
1.094/342 adquisiciones posteriores al primer informe de estado 11; estos
grupos siguen el último estado registrado, no una alineación exacta.
El diagnóstico registra solo secuencias nuevas: su contador de retenciones
igual a cero no demuestra que el host nunca retenga una textura.

Hay una pista distinta del hash: en la pasada asíncrona se adquiere la secuencia
906 con `hostTick=1365` y `frameTick=948`, distancia de 417 ticks; el máximo
síncrono fue 3. La adquisición posterior vuelve a ticks cercanos. Investigar
las esperas del host y la cola GS antes de atribuirlo a una ranura corrupta,
a imágenes alternantes o a latencia sostenida. No convertir estos ticks en FPS.

Una captura externa única de la ventana RAM muestra HUD, escena y Kratos,
pero el panel cubre gran parte del encuadre; no acredita fidelidad de cámara.
La captura compartida se rechaza al no estar nuestra ventana en primer plano,
sin capturar otra aplicación. Falta revisión visual con el panel contraído,
repetición de la combinación asíncrona y controles separados de los hilos.
Registros, manifiestos, herramientas de ejecución y captura quedan privados en
`logs/transition_review/` y `E:\gowgpuprof\transition90`.
El PC vuelve a quedar reservado para el usuario; no lanzar nuevas cargas hasta
su confirmación. #51 sigue en borrador y no se declara corregida la transición,
el encuadre ni los cuelgues.

## VU1, ronda 3: escrituras por par fijas, especiales con SSE y más (2026-10-11)

El muestreador tiene un modo `linea` (sexto argumento): línea de tiempo por hilo de las primeras 1500 muestras
(`#` ocupado, `.` esperando, una letra por función elegida). En los muelles muestra que el EE y el hilo de VU1
esperan **a la vez** al VBLANK: el juego va sincronizado con la vertical y los fps salen cuantizados (60/30/20).
Un cuadro cuyo trabajo de VU1 no cabe en dos VBLANK (33,3 ms) se va a tres; la mezcla de cuadros de 2 y 3 da
los 24–25 fps. Un diagnóstico temporal en `syncVu1Worker` confirmó que el EE nunca espera allí a la VU1 en los
muelles: la cadena EE → VU1 se serializa a través del VBLANK, no de esa sincronización.

`GOW_PAD_SOLO_GUION=1` (lo pone `probar_rendimiento.ps1`) ignora el mando y el teclado reales: tocar el ratón o
un mando durante una medición movía a Kratos o cambiaba la partida del menú, y la escena medida no era la misma.

Cambios, todos exactos (huellas por lanzamiento e imágenes idénticas en compilado e intérprete):

- **Escrituras fijas por par** (`ps2recomp-vu1-static-stores.patch` y el generador). Cada par restablecía
  VF0 y VI0 y borraba la copia de VI para saltos. Ahora se decide al compilar: VF0 solo si la instrucción
  superior o la inferior pueden escribirlo (FMAC con fd = 0, ITOF/FTOI/ABS con ft = 0, LQ/MOVE/MR32/LQI/MFIR
  con ft = 0 o una inferior que va a `execLower`), VI0 solo tras `execLower` (las inferiores en línea
  comprueban `it != 0`), y la copia de VI solo en el primer par del bloque o tras un par que la active
  (`clearBranchBackup` en el struct R). Banco: −5 % / −3,4 %.
- **ITOF/FTOI/ABS con SSE y SQ con DEST parcial en registros** (`ps2recomp-vu1-simd-specials.patch`). Los dos
  puntos calientes de los bucles de transformación eran reenvíos store→load fallidos: FTOI se calculaba carril
  a carril en la pila (en doble) y luego se leía en 16 bytes, y SQ.xyz copiaba a la pila, cambiaba carriles y
  volvía a leer. FTOI con `cvttps2dq`: x·2ⁿ en float es exacto salvo desbordamiento y todo valor ≥ 2³¹ satura
  a INT32_MAX (los float cerca de 2³¹ son múltiplos de 128, así que "≥ INT32_MAX" equivale a "≥ 2³¹"); por
  debajo de −2³¹ da INT32_MIN como `floatToInt`. Banco: −2,9 % / −1,9 %.
- **Más inferiores en línea** (`ps2recomp-vu1-lower-specials.patch`): FCAND, FMEQ, FMAND, FCGET, BAL, JR y
  JALR iban a `execLower` (el 85 % de las llamadas que quedaban).
- **`advanceTo` por eventos** (`ps2recomp-vu1-advance-events.patch`): las esperas largas (DIV, XGKICK)
  avanzaban ciclo a ciclo; ahora saltan al siguiente evento (confirmación de una cola o de PATH1).
- **Copia de PATH1 por tramos** (`ps2recomp-vu1-xgkick-bulk.patch`): con crédito de ciclos de sobra, los qwords
  de datos de un paquete se copian con un `memcpy` (respetando la vuelta al principio de la memoria de datos).
- **UNPACK enmascarado de VIF1 con SSE4.1** (`ps2recomp-vif1-unpack-mask-sse.patch`): la máscara, STMOD y
  ROW/COL se aplican a los cuatro carriles a la vez en lugar de palabra a palabra.
- **Despacho**: cada dirección recuerda el último bloque que coincidió (`g_hot`) y `run` lo llama sin pasar
  por la función de despacho.

**Comprobado:** huellas e imágenes de `vif_pcsx2_inicio2` y `vif_port_480s` idénticas (compilado e
intérprete), suite 591/591, `probar_vu1_fmac` (100.800 + 2.016 casos) y `probar_vu1_compilada`, que tiene un
programa procedural nuevo con ITOF/FTOI/ABS, SQ con DEST parcial, FCAND/FCGET/FMAND/FMEQ y BAL/JR/JALR con
valores límite (±0, subnormales, ±2³¹ y vecinos, máximos, Inf/NaN, enteros límite), FTZ/DAZ y cortes por
presupuesto: 936 casos exactos.

**Banco:** ~−15 % por cuadro respecto a la ronda 2 en las dos grabaciones (las cifras absolutas varían entre
sesiones; los A/B se hacen siempre alternando las dos versiones). **Juego** (muelles, mismas condiciones):

| | Hilo de VU1 ocupado | fps | VU1 por cuadro |
|---|---|---|---|
| main | 90,8 % | 20,0 | ~45 ms |
| ronda 1 (#54) | 78,3 % | 20,5 | ~38 ms |
| ronda 2 (#55) | 82,3 % | 24,0 | ~34 ms |
| ronda 3 | 75,4 % | 25,2 | ~30 ms |

Con la configuración por defecto (OpenGL, presentación por RAM, sin hilos opcionales ni FINISH asíncrono), la
misma partida y el mismo guion van a 11,6 fps, sin errores de VU1. Son las dos cifras de los README (~12 y ~25).

Para 30 fps estables el trabajo de VU1 de cada cuadro tiene que caber en dos VBLANK junto con la parte que
espera al EE: faltan ~3 ms (~10 %).

**Descartados:**
- *Tabla para los bits pegajosos de las FMAC con flags muertos*: sin ganancia medible.
- *Agrupar los ciclos del IOP*: sin ganancia y con tirones en las cargas (ver la ronda 2).

**Siguiente (analizado, sin hacer):**
- *Operandos ya normalizados.* Toda FMAC (también MAX/MINI), ITOF y ABS deja un valor normalizado, y para esos
  valores `normalize4` no cambia nada: dentro de un bloque se puede saber qué carriles de cada VF/ACC los
  escribió una de esas instrucciones y saltarse la normalización. Pero los bucles de transformación están
  segmentados (cada iteración usa lo que calculó la anterior, en otro bloque), así que dentro del bloque solo
  se ahorra el ACC de las cadenas MADDA (2 de ~11 normalizaciones en el bucle más caliente).
- *Flags MAC muertos entre bloques.* En el bucle más caliente (dos bloques de 9 pares que saltan uno al otro),
  las 3 FMAC del final de cada bloque guardan flags que la primera FMAC del otro bloque pisa sin que nadie los
  lea. Hace falta comprobar en tiempo de ejecución el código de los sucesores (no está en las palabras del
  bloque) y cuidar los cortes por presupuesto entre los dos bloques, que dejarían visibles otros flags que el
  intérprete.
- *Temporización estática* de los bloques (comprobar las esperas a la entrada en lugar de en cada par).

## Integración del diagnóstico GS con #56 y #57 (2026-10-11)

Se traen referencias remotas y se integra mediante merge `origin/main` en
`34cbe58a9a47e33d1a2ed70b34a9937f72293d43`, con #56 y #57.
Se conservan sin modificaciones los seis parches nuevos, el generador VU1,
sus regresiones, el muestreador y los overrides de Opus. Los conflictos de
inserción en `scripts/compilar.ps1` se resuelven dejando los seis parches en
su orden original y el diagnóstico del host al final. El conflicto de
`ESTADO.md` conserva ambas investigaciones, en orden de fecha.

La cadena integrada contiene 96 parches únicos. Comprobación y aplicación
completas correctas en un worktree privado nuevo sobre el upstream fijado,
sin tocar el runtime construido ni el checkout de Opus. Configuración:
0 errores y los 4 avisos existentes. Sintaxis PowerShell: 21 scripts,
0 errores. Registro privado: `logs/transition_review/patch_chain_96.txt`.

El CI de `ee079d21cecfbc7515f17e34db6490cc7c8a99fc` había terminado en verde
antes de este merge. No valida el nuevo HEAD integrado. Las pruebas nativas
591/625 y la matriz anterior corresponden a **90 parches**; el binario guardado
no incorpora #56. Será necesaria una compilación completa de la cadena nueva
antes de atribuirle imágenes, estabilidad o rendimiento. Los 25,2 FPS de #56
son mediciones de Opus, no una reproducción independiente de GPT.

La disponibilidad del ordenador sigue pendiente de confirmación: se continúa
con revisión y controles ligeros, sin compilaciones, juegos ni benchmarks.
#51 permanece en borrador; se conserva la pista de distancia entre ticks y
el encuadre como problemas que todavía requieren control visual.

## Distancia entre ticks en el inspector compartido (2026-10-11)

El hallazgo de 417 ticks requería hasta ahora un análisis privado adicional.
`inspect_shared_frames.py` informa ahora `tick_distance`: número de muestras
consistentes, mínimo/máximo firmado de `hostTick - frameTick` y línea, secuencia,
render y ticks del primer máximo. Incluye retenciones consistentes y registros
sin hash; descarta metadatos contradictorios. No compara ticks entre imágenes
de épocas distintas ni convierte el resultado en tiempo/FPS. Un valor negativo
puede ser válido porque el request muestrea el contador después que el host.

Once pruebas Python pasan. Las tres nuevas fallan con el inspector de
`cce2d68`: máximo con evidencia, distancia negativa/hash desconocido/cambio de
modo y retención con una fila contradictoria que no debe alterar el máximo.
El inspector nuevo reproduce 0..3 / 0..417 y las líneas originales en los dos
registros Windows conservados de **90 parches**, sin volver a ejecutar el juego.
Se aclara que cero retenciones registradas no descarta las que no emite el
diagnóstico. No se modifica el runtime ni se declara corregido un fallo visual.

También se reubica el párrafo de validación de paletas bajo su sección original
en `CONTROLES.md`; no pertenece al control de presentación compartida.

## Control reproducible de transición y minimización del host (2026-10-11)

Se publica `tools/render/run_transition_control.py`, derivado del control
privado anterior: manifiesto y hashes explícitos, carpetas nuevas, ELF/IOP
copiados, tarjeta procedural o copia privada de la inicial, entorno de
diagnósticos limpio y misma matriz de cuatro modos. Añade selección de hilos,
panel contraído e ignorar entradas físicas con `GOW_PAD_SOLO_GUION=1` de #56.
No publica datos del juego ni modifica el runtime.

La consulta Win32 registra cada cambio observado de visibilidad/minimización
y primer plano, solo para el PID que inicia. No captura imágenes, activa
ventanas ni usa readback GS. En raylib del runtime construido se confirma una
espera de eventos al minimizar sin `FLAG_WINDOW_ALWAYS_RUN`; el EE continúa
en otro hilo. Es una explicación posible de una pausa del host, no la causa
demostrada de los 417 ticks: las pasadas anteriores no registraron minimización.
No se modifica la política de ejecución en segundo plano para esconder el fallo.

Once pruebas Windows de la herramienta pasan en 0,13 s, incluida la ejecución
de un hijo Python procedural, sin juego ni ventana. Se comprueban la copia
sin cambios sobre la tarjeta inicial, rechazo de directorios existentes y
copia recursiva, manifiesto incorrecto, limpieza de variables por presencia,
pérdida de foco distinta de minimización y terminación exclusiva del hijo.
La consulta Win32 real a nuestro proceso Python no encuentra ventana y
conserva minimización desconocida. Son comprobaciones ligeras; no se lanza
el juego, compilación ni benchmark. Protocolo y límites en
`TRANSICION_COMPARTIDA.md`; se incluye la herramienta en CI.

El CI de `e412a1f9b0579ed3dced7083ac3d7aaab140583e` pasa ambos controles,
con los 96 parches y las once pruebas del inspector. La ejecución anterior
del merge `cce2d68` fue cancelada por el nuevo push; no se declara un resultado
propio para ella. El control Windows del juego con esta cadena sigue pendiente.

La revisión del lanzador detecta que un SHA-256 válido escrito en mayúsculas
era rechazado frente al digest Python en minúsculas. Se valida el formato de
64 cifras y se normaliza solo el texto hexadecimal, manteniendo la comparación
de identidad. Doce pruebas Windows pasan; la nueva comprueba aceptación de
mayúsculas y rechazo de valores ausentes, no textuales, truncados o no hexadecimales.

## Fuente preferida de la textura compartida (2026-10-11)

La revisión estática del renderer GPU confirma que `sourceFbp` conserva dos
unidades posibles: páginas DISPFB de 8.192 bytes para la fuente CRTC, o bloques
TEX0 de 256 bytes cuando se usa `preferredSource`. `displayFbp` conserva
páginas. El diagnóstico omitía el indicador que las ranuras ya transportaban:
el mismo número crudo en ambos campos no demostraba igualdad de direcciones.

Se amplía el parche del diagnóstico host con `preferred=%u` a partir del
`frame.usedPreferred` realmente adquirido. Conserva la condición opcional,
una sola adquisición y el límite de 4.096. No añade readback, hash, fence ni
`glFinish`; no modifica selección de fuente, píxeles ni renderer CPU.
El inspector acepta el formato anterior y el nuevo, informa unidades
desconocidas/páginas/bloques y conserva el indicador en evidencia de máximos
y retornos de contenido. Una omisión antigua no contradice ni borra un
indicador conocido; un cambio explícito bajo la misma secuencia sí es conflicto.

Quince pruebas Python correctas. Tres regresiones fallan con el inspector
`60ce933`; flags inválidos, campos desconocidos y duplicados se rechazan.
La cadena completa de 96 parches pasa check/aplicación sobre el upstream fijado
en otro worktree privado nuevo, sin tocar el runtime construido ni a Opus.
Los registros nativos de 90 parches conservan máximos 3/417 y tienen unidades
desconocidas en todas sus 2.186/1.247 filas; no se reconstruye un dato ausente.

CI de `60ce9338f9c27d312a474cab40498f1cf6fdcdd7` correcto en ambos controles.
Ese resultado precede a este cambio y no lo valida. Modificar el parche exige
otra compilación completa Windows y control funcional del HEAD final.
Siguen pendientes la visualización compartida y el encuadre dentro del GS;
#51 permanece borrador. Se realizan solo controles ligeros mientras continúa
la reserva del ordenador por el usuario.

## Evidencia de dimensiones en el inspector (2026-10-11)

`inspect_shared_frames.py` añade `dimension_changes`: línea, secuencia/render
y tamaño anterior/nuevo. Antes solo cortaba el patrón de retornos al cambiar
el tamaño; ahora el dato queda explícito en el resumen. Incluye muestras sin
hash, excluye contradicciones de la misma secuencia y no enlaza épocas
separadas por un cambio de modo.

Diecisiete pruebas correctas, con dos regresiones que fallan contra `715e216`.
Reanalizar los registros Windows conservados de 90 parches muestra en ambos
un único cambio 1×1 → 512×448 en la secuencia 2, al inicio. No hay cambios
posteriores entre las muestras capturadas; no prueba aspecto ni cámara correctos.

El CI de `715e216fa72cec85e4112eb20b3fdcbba090a910` termina en verde,
incluida compilación del nuevo formato `preferred` y suite de los 96 parches.
([CI](https://github.com/KIexster/god-of-war-recomp/actions/runs/38114877209)).
No sustituye el control Windows pendiente. Este ajuste solo cambia Python
y documentación; no se recompila ni ejecuta el juego en el PC reservado.

## Directorio privado del lanzador y seguimiento de fuente (2026-10-11)

El control de transición ahora resuelve la salida y permite, dentro de su
checkout, solo `logs/`; también acepta carpetas externas. Mantiene copias
de tarjetas y resultados en la zona privada, como la herramienta de referencia
PCSX2. Trece pruebas Windows pasan en 0,11 s. La nueva falla contra `2930666`
con `mkdir` interceptado; no se crea la salida rechazada en ese control.
No cambia el runtime ni lanza el juego.

CI de `2930666cddd7d907a6576b2410ebed1a8695a61c` correcto en ambos controles,
antes de este ajuste del lanzador. Comprobar el CI del HEAD final y conservar
pendiente la compilación Windows de la cadena actual.

La revisión de unidades produjo una corrección funcional separada:
[PR #58](https://github.com/KIexster/god-of-war-recomp/pull/58).
`FRAME/DISPFB.FBP` usa páginas de 8.192 bytes; `TEX0.TBP0` bloques de 256.
Se normaliza la comparación en la identificación de copias completas del
frontend y en la selección CPU/GPU de fuente sin TBW. No cambia FINISH ni fences.
Control negativo `5f3d7de`: compila, 591/593, fallan solo las dos nuevas.
Arreglo `e52527d1bf63b513facac380330b94bc2e998937`:
593/593, CI remoto completo correcto
([38115778696](https://github.com/KIexster/god-of-war-recomp/actions/runs/38115778696)).
No se añade ninguna excepción de pruebas.

El arreglo no está integrado en esta rama. Cada PR tiene 96 parches propios;
la aplicación conjunta privada de los 97 pasa, sin compilación conjunta.
Windows GPU y transición real pendientes. Los registros antiguos no indican
`preferred` y no permiten atribuir el fallo visual a estas comparaciones.
Ambas PR siguen en borrador; los problemas de alternancia y encuadre continúan abiertos.

## Comparación de bases de presentación GS (2026-10-11)

La revisión del diagnóstico compartido revela tres comparaciones entre
unidades distintas: `FRAME/DISPFB.FBP` en páginas de 8.192 bytes y
`TEX0.TBP0` en bloques de 256 bytes. Por ejemplo, 32 páginas y 32 bloques
son 262.144 y 8.192 bytes; 32 páginas y 1.024 bloques son la misma dirección.
Comparar los valores crudos podía omitir una fuente distinta o aceptar una
copia sobre su propia base.

Se añade `ps2recomp-gs-source-address.patch`, tema independiente del
diagnóstico #51. Normaliza la página multiplicando por 32 antes de comparar:
en la identificación del sprite de copia completo, en la fuente CPU sin TBW
y en la selección equivalente GPU. Conserva los metadatos en sus unidades
originales, el criterio existente para ancho explícito y el renderer CPU
como referencia. No cambia matemáticas de rasterizado, fences ni FINISH.

Dos regresiones independientes del juego: el frontend usa un backend
grabador para comprobar cuatro pares de bases sin rasterizar; la presentación
CPU comprueba selección, metadatos y un píxel procedural con tres casos de
TBW cero/explícito. El control GPU existente añade las dos colisiones de
unidades y compara píxeles/metadatos contra CPU con readback explícito solo
en la prueba. No se introduce ese readback en la ejecución del juego.

Se publica primero `5f3d7dedeced864133eb1d9887d49f892a0458a9`, con las
regresiones y sin corregir los comparadores, para control negativo en CI
remoto. La [ejecución 38115429132](https://github.com/KIexster/god-of-war-recomp/actions/runs/38115429132)
compila correctamente y pasa 591 de 593 pruebas: fallan exactamente
`display copy compares FRAME pages with TEX0 blocks` y
`presentation source without TBW compares physical addresses`. No se añaden
excepciones a `fallos_conocidos.txt`. El CI del arreglo todavía está pendiente.

La cadena propia contiene 96 parches: todos pasan check/aplicación desde el
upstream fijado en un árbol privado nuevo. El diagnóstico de #51 también
aplica después, como parche 97 en una comprobación privada conjunta.
Configuración: 0 errores y 4 avisos existentes; sintaxis de 10 scripts:
0 errores. No se modifica el checkout de Opus, comprobado limpio en
`34cbe58`, ni sus parches VU1.

Compilación completa Windows y control GPU/juego pendientes de disponibilidad
del PC. No se conoce si estos pares de bases aparecen en la transición real:
los registros de 90 parches no tenían `preferred`. El fallo de unidades es
independiente de probar que explique la alternancia o el encuadre; ambos
siguen sin declararse resueltos. No hay medición nueva de rendimiento.

## Control de unidades en la textura compartida (2026-10-11)

El arreglo `e52527d1bf63b513facac380330b94bc2e998937` pasa ambos controles
del CI ([38115778696](https://github.com/KIexster/god-of-war-recomp/actions/runs/38115778696)):
593/593 pruebas, sin excepciones. Es validación remota sin el juego.

El usuario libera ahora el ordenador y confirma que Claude no trabajará
durante 24 horas. Se inicia primero una compilación completa de la referencia
de #51, todavía sin este arreglo, para comparar después con la integración.
Se amplía el control GPU de este parche a la ruta compartida: mismos pares
32 páginas/32 bloques y 32 páginas/1.024 bloques con TBW cero, lectura de la
textura realmente adquirida y comparación de sus filas y metadatos con CPU.
Las aserciones explícitas de selección evitan que un fallo común CPU/GPU
quede oculto por comparar solo ambos renderers. La descarga pertenece
exclusivamente a la prueba procedural, no al juego ni al diagnóstico del host.

Los 95 parches de main más este parche y el diagnóstico de #51 pasan
comprobación y aplicación en orden en otro árbol privado: 97 en conjunto.
Esta ampliación GPU todavía no se ha ejecutado; comprobar el CI del nuevo HEAD
y completar la compilación Windows antes de atribuirle resultados nativos.

El mismo control añade 16 publicaciones consecutivas con contenido y tick
distintos, alternando ambas selecciones. Reutiliza las ocho ranuras mientras
comprueba los píxeles de cada textura nueva contra CPU; las esperas y lecturas
solo existen en la prueba. La ampliación sigue aplicando antes del diagnóstico
en la cadena privada conjunta de 97. Es cobertura procedural pendiente de
ejecución GPU, no una reproducción del defecto visual ni un benchmark.

## Compilación de referencia actual y merge de fuente (2026-10-11)

Con el PC libre se completa `scripts\2_compilar.cmd` en nuestro runtime privado,
verificando la junction y conservando las fuentes anteriores. Referencia:
`de5a89ad437c952a6ae384025ed602e4e62a9c1b`, 96 parches, Release, VU1 intérprete,
tres tareas paralelas, código final 0 y 886,33 s. El ejecutable de 54.542.336 bytes
tiene SHA-256 `e5aae46440de86b0eeb97da91f742330ce0e95d22bbf70b368e83c44f082f0bd`.
Se congela una copia privada con DLL, manifiesto y registro de compilación.
La suite Windows normal pasa 591/591; libpad2 también pasa. El control GPU y
la nueva matriz con el juego todavía están en curso/pedientes: la compilación
y la suite sin juego no acreditan la transición ni la cámara.

Después se integra por merge #58, `fb8782525ed5b9ee7ac806675f4d58b1dfa5ac0d`,
conservando los documentos de ambas ramas y ordenando fuente de presentación
antes del diagnóstico. La cadena integrada tiene 97 parches. Su comprobación
privada de aplicación es correcta y el CI de #58 pasa ambos controles
([38131689081](https://github.com/KIexster/god-of-war-recomp/actions/runs/38131689081)).
Esta integración aún no está compilada en Windows; el ejecutable de referencia
anterior no incluye el arreglo ni las nuevas comprobaciones de fuente compartida.
No se modifica el checkout de Opus, que continúa limpio. Ambas PR siguen borrador.

## Suite GPU de referencia y contrato de FINISH (2026-10-11)

Sobre el mismo binario/fuentes de referencia `de5a89a`, 96 parches, la suite
Windows con GPU real y FINISH síncrono pasa 625/625. El nuevo diagnóstico
registra `preferred` y hashes válidos en sus adquisiciones procedurales.
Un ensayo adicional de toda la suite con `GOW_GS_FINISH_ASINCRONO=1` pasa
623/625: fallan `FINISH and readback wait for the last compact draw state`
y `shared FINISH and reads retain barriers after held draws`. Se conserva
el registro completo y no se añade ninguna excepción.

Ambas pruebas exigen explícitamente efectos visibles al retorno de FINISH,
una garantía del modo síncrono. El modo opcional encola FINISH y retorna antes,
mientras las lecturas conservan su barrera; el proyecto comprueba ambos
contratos por separado con `tests/gs_finish_async_test.cpp` en CI. Ejecutar
toda la suite bajo la opción asíncrona no hace válidas esas aserciones y
puede leer sus grabadores mientras el worker los modifica. No se declara
verde ese ensayo ni se usa para probar estabilidad del juego. El control
específico todavía debe reproducirse en Windows con la integración nueva.

Primera pasada de referencia síncrona/RAM: 360,06 s, terminación deliberada
por límite (código de proceso 1, código del lanzador 0), estados
0 → 3 → 4 → 9 → 11; primer informe 11 a 149,78 s del guion y último 11.
Tarjeta procedural intacta, sin las firmas de error VU1/GIF/excepción buscadas.
Ventana visible desde ~1 s, nunca observada minimizada; primer plano desde
113,78 s. Dos capturas externas propias con panel contraído muestran escena,
Kratos y, en la posterior, HUD/enemigos. No están alineadas con PCSX2 ni
acreditan fidelidad de cámara, jugabilidad o FPS. Capturas y registros privados.
La matriz compartida/asíncrona está en curso; no se trasladan estos resultados
al ejecutable de 97 parches pendiente de construcción.

## Espera en la referencia compartida síncrona (2026-10-11)

La segunda pasada de la referencia `de5a89a` (96 parches), FINISH síncrono y
textura compartida, completa 360,05 s y termina deliberadamente por límite.
El último informe del mando es estado 4 a 25,36 s del guion, con
`levelReady=0` y `flashReady=0`; no registra estado 9 ni 11 ni nuevos informes
del mando después. Esto acredita una espera sin progreso observado, no el
estado exacto de RAM al terminar ni una causa demostrada en GS/VU1/IOP.
No hay firmas buscadas de error y la tarjeta permanece intacta.

El host continúa respondiendo. La ventana está visible desde ~1 s, nunca se
observa minimizada y no recibe primer plano en esta pasada. Hasta el cupo
de 4.096: cero retrocesos, hashes desconocidos o metadatos contradictorios;
ticks 0..3 de distancia; todas las fuentes son CRTC en páginas
(`preferred=0`). Un único cambio de dimensiones inicial 1×1 → 512×448.
Hay 3.895 retornos A–B–A; 3.792 corresponden al tramo del último estado 4,
con dos hashes alternados. Podrían ser campos par/impar de contenido estático;
no constituyen por sí solos prueba de ranura antigua o corrupción.
El cupo se agota alrededor de 116 s reales: no cubre el resto del control
ni las retenciones que nunca se registran.

Tras observar la espera durante varios minutos se realiza un muestreo externo
de 5 s exclusivamente al PID propio 13840, usando `tools/perfil/muestrear.cpp`,
que selecciona por PID. No se usa el muestreador por nombre de Opus. Las muestras
carecen de símbolos del ejecutable y no identifican una función responsable;
no se publican como medida de rendimiento. El muestreo suspende brevemente
hilos y puede perturbar la ejecución; se conserva ese límite junto al registro.
La siguiente pasada asíncrona/RAM está en curso. La espera debe repetirse con
símbolos antes de modificar sincronización o atribuirla al arreglo de fuentes.

## Cierre de la matriz de referencia actual (2026-10-11)

Las cuatro pasadas de `de5a89a` (96 parches) completan 360 s y terminan por
límite deliberado; las tarjetas privadas quedan intactas y no aparecen las
firmas de error buscadas. Síncrona/RAM llega a 11 a 149,78 s del guion;
asíncrona/RAM a 116,44 s y asíncrona/compartida a 116,39 s. La síncrona
compartida sigue siendo una espera sin nuevos informes tras estado 4 a 25,36 s.

La asíncrona compartida registra 2.310 adquisiciones, ticks de distancia
0..3, cero retrocesos, metadatos contradictorios o hashes desconocidos.
113 retornos A–B–A anteriores al primer informe de estado 3; ninguno en
las 1.205 adquisiciones del tramo del último estado 11. Ambas compartidas
registran únicamente fuentes CRTC (`preferred=0`) y un cambio inicial
1×1 → 512×448. No se reproduce el máximo antiguo de 417 ticks ni se observa
la selección de fuente que corrige #58; no atribuirle todavía un efecto real.

La ventana no se observa minimizada en ningún caso. Solo síncrona/RAM está
en primer plano y tiene capturas externas válidas: el resto conserva pendiente
revisión visual. Estado 11/hash y una captura no acreditan fidelidad ni ausencia
de imágenes antiguas. No hay medición de FPS ni comparación alineada con PCSX2.
Tabla, protocolo y límites completos en `TRANSICION_COMPARTIDA.md`.

Se inicia después otra compilación completa del código integrado (97 parches),
con /O2 y PDB privados para investigar la espera. Debe registrar otro
commit/hash/configuración; las pruebas GPU de fuente adquirida/ranuras y el
control Windows específico de FINISH de ambos contratos siguen pendientes.
#51 conserva borrador; no se declara corregida la transición o el encuadre.

## Cadena integrada Windows y contratos FINISH con GPU (2026-10-11)

Compilación completa de 97 parches terminada con código 0: código
`f51cc2cb7ee981c1cb6419926d46acc217de1f86`, runner de 54.542.336 bytes,
SHA-256 `ba80364535c22d728058ad98bf8182937f36c2db9ada5a11063f684c1a57add0`.
Release optimizado con /Zi y /DEBUG, VU1 intérprete, tres tareas; unos 902 s
entre las marcas 07:58:20 y 08:13:22 del registro. El helper privado falla
después al guardar metadatos por ocultar sus argumentos; se conserva ese
fallo, se corrige el helper y se recupera el manifiesto desde el registro,
sin repetir ni atribuirle un fallo a la compilación. Flags y PDB se verifican
y guardan solo en `E:\gowgpuprof\transition97`; no se publica el PDB.

Suite nativa: **593/593 normal y 627/627 GPU real**, cero fallos conocidos.
Incluye las regresiones nuevas de #58: selección de dirección física y
comparación de píxeles de la textura compartida al reutilizar las ranuras.
El control separado `gow_gs_finish_async_test` pasa `default` y `async`
en procesos Windows nuevos: retorno, CSR, orden y lectura sincronizada.

Se añade `tests/gs_shared_finish_test.cpp`: GPU/contexto compartido real,
sin admitir fallback CPU como éxito; ambos modos pasan 16 publicaciones
con FINISH, secuencias/ticks, fuente esperada, todas las filas comparadas
con CPU y tamaños 64×64 → 64×96 al reutilizar las ocho ranuras. El readback
existe solo en este control sin juego. Primer ensayo conservado: solicitaba
64×48, que el decodificador CPU considera inválido y sustituye por su tamaño
por defecto, mientras GPU usa 64×48. Se ajusta la premisa a alturas válidas;
no se modifica la referencia CPU ni se presenta aquel ensayo como equivalente.
Esa diferencia de tamaño pequeño no explica por sí sola la escena 512×448.

Comienza la matriz del juego con el runner identificado de 97, inicialmente
sin hilo VU1/frente GS. La validación procedural no acredita fidelidad de
la escena, solución de la espera síncrona, alternancia visible ni cámara.
#51 sigue en borrador; su revisión visual y CI del próximo HEAD siguen pendientes.

## Matriz integrada y espera localizada en Count (2026-10-11)

Se completa la matriz de 97 parches con el mismo runner `f51cc2c` y SHA-256
`ba80364535c22d728058ad98bf8182937f36c2db9ada5a11063f684c1a57add0`.
Cada pasada válida dura 360 s; finaliza por límite deliberado (proceso 1,
lanzador 0), sin las firmas de error buscadas y con tarjeta procedural intacta.
VU1 y frente GS permanecen sin sus hilos opcionales. Primer informe del guion:

| FINISH / presentación | Primer estado 11 | Último estado |
|---|---:|---:|
| Asíncrono / compartida | 135,35 s | 11 |
| Síncrono / compartida | No observado | 4, a 25,31 s |
| Síncrono / RAM | 126,78 s | 11 |
| Asíncrono / RAM, repetición aislada | 121,40 s | 11 |

Estos tiempos son informes del guion, no tiempos de carga comparables ni FPS.
La pasada síncrona/RAM estuvo minimizada durante buena parte del control y
sus últimos ~30 s coincidieron por un error de coordinación del lanzador con
otra instancia propia asíncrona/RAM. La entrada en 11 ocurrió antes del
solapamiento, pero no se afirma una pasada completa aislada. Se paró únicamente
la instancia nueva mediante su archivo STOP a 34,35 s y se repitió asíncrona/RAM
sin solapamiento. Se conservan ambos registros; no se presenta la parada como
un cuelgue. No se obtuvo una captura válida de la ventana minimizada.

Asíncrona/compartida: 1.986 adquisiciones, 20 retornos A–B–A, ninguno en los
tramos del último estado 4 o 11; 1.056 adquisiciones bajo el último estado 11.
Distancia de ticks 0..6. Síncrona/compartida vuelve a esperar sin nuevos informes
tras estado 4: agota 4.096 registros, con 3.896 A–B–A (3.800 en el tramo 4),
distancia 0..3. En ambas: cero regresiones, metadatos contradictorios o hashes
desconocidos; un cambio inicial 1×1 → 512×448 y siempre `preferred=0`, fuente
CRTC en páginas. El cupo de la síncrona deja sin observar el resto de la pasada;
la alternancia de dos hashes podría corresponder a campos de una escena quieta.

Capturas externas privadas de la superficie de nuestra ventana mediante
PrintWindow muestran HUD, Kratos y enemigos en asíncrona/compartida; dos
capturas síncronas muestran una escena de llamas/primer plano sin el progreso
esperado. Se verifican PID, ruta, visibilidad y ausencia de minimización antes
de capturar. La petición de captura puede perturbar ese instante; las imágenes
no se alinean con secuencias adquiridas ni con PCSX2. No prueban cámara correcta,
ausencia de imágenes antiguas, equivalencia de píxeles del juego o rendimiento.

Dos muestreos externos de 5 s al PID propio 12184, con PDB privado, encuentran
el ejecutor EE recorriendo `sub_00185A20` y trabajo IOP; el worker GS espera
comandos. El segundo resuelve líneas del bucle 0x185D88..0x185DD8: consulta
COP0.Count para comprobar un plazo. La traducción actual lee `ctx->cop0_count`,
pero la contabilización de ciclos EE no actualiza ese campo. Es un defecto
preexistente de la semántica del contador y una pista concreta para esta espera;
no demuestra todavía que corregirlo resuelva la transición. El muestreo suspende
brevemente hilos, y no se utiliza como benchmark ni como prueba de bloqueo GL.

Se prepara un parche separado de Count, con pruebas sin juego de ciclos, wrap,
contextos, reset y traducción MFC0/MTC0. Debe compilarse, demostrar regresiones
y regenerar completamente el runner antes de atribuirle cualquier efecto.
No modifica archivos ni procesos de Opus; su checkout se comprobó limpio.
El CI del HEAD publicado `10a7bec` está verde. #51 continúa en borrador: siguen
pendientes resolver la espera y comparar la escena/cámara de forma alineada.

## Regresiones de COP0.Count para investigar una espera (2026-10-11)

En la rama de diagnóstico GS, dos controles completos con presentación
compartida síncrona dejan de informar progreso tras estado 4. El muestreo
privado con símbolos encuentra trabajo EE en un bucle que consulta Count.
La traducción actual lee `ctx->cop0_count`, pero los ciclos EE no lo actualizan.
Este problema es anterior a los cambios recientes de VU1: no se atribuye a Opus.

Se añade un parche separado con cuatro regresiones sin juego: avance y sondeo
acotado, wrap/escrituras entre contextos, reset y emisión MFC0/MTC0.
El primer commit es un control negativo deliberado, con interfaces compatibles
y la lectura inmóvil anterior; debe compilar y fallar esas expectativas.
La implementación por ciclos se publicará después conservando este historial.

Todavía no se ha demostrado el efecto sobre la transición. La validación
requiere CI del HEAD corregido, compilación Windows completa para regenerar
las lecturas y repetición con el juego. No se modifica FINISH ni fences, no se
afirma fidelidad de cámara, estabilidad general o FPS, ni se implementa aquí
la interrupción de COP0.Compare. Referencia semántica del contador:
[PCSX2 COP0.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/COP0.cpp).

La rama compartida integra después la implementación positiva de #59:
Count global del runtime actualizado perezosamente por ciclos EE, lecturas
consecutivas con avance mínimo de uno, wrap de 32 bits, escritura que cambia
la base y reset conjunto con el reloj. MFC0 conserva extensión de signo y
ejecuta la lectura incluso con destino $zero, fuera de la macro que descarta
ese registro. No usa reloj de pared ni altera temporizadores, VU1, FINISH o
fences. La cadena de 98 parches aplica en un árbol privado nuevo; configuración
y sintaxis PowerShell pasan (cuatro avisos existentes). Compilación, suite y
control funcional de este código siguen pendientes. Se inicia un build
completo privado, regenerando C++; el runner congelado de 97 permanece aparte.

El CI inicial de la integración `3690bc9` también falla por la cabecera ausente
de la prueba, no por una regresión ejecutada. Se actualiza el parche positivo
con `ps2recomp/types.h`. La compilación privada iniciada con aquel HEAD se
detiene deliberadamente a 333,15 s, verificando el PID del wrapper propio y
sus descendientes; se conservan manifiesto de salida 1 y fuentes parciales.
No hay runner nuevo validado de ese intento. Se reinicia la compilación
completa con el parche corregido, en otra carpeta de artefactos. No se detienen
procesos de otros colaboradores ni se modifican sus archivos.

Primer CI negativo `e7e8914`/38137005221: falla al compilar la nueva prueba
por faltar `ps2recomp/types.h` (Instruction/Symbol/Section incompletos).
Se añade la cabecera y se repite. Ese ensayo no acredita regresiones de
comportamiento; el control negativo debe llegar a ejecutar la suite.

El control negativo corregido `d496189` compila y ejecuta la suite en
[CI 38137285316](https://github.com/KIexster/god-of-war-recomp/actions/runs/38137285316):
591/595 pasan; fallan únicamente las cuatro regresiones nuevas de Count.
No se añaden excepciones a `fallos_conocidos.txt`. Se publica después el
arreglo: Count global actualizado perezosamente por ciclos EE, mínimo de uno
en lecturas consecutivas del mismo bloque, wrap de 32 bits, escritura/reset
de su base y emisión MFC0/MTC0 a través del runtime. MFC0 a $zero ejecuta
explícitamente la lectura, porque SET_GPR_S32 omite evaluar el valor allí.
Se mantiene la extensión de signo de MFC0. No usa reloj host ni añade
interrupciones Compare. El CI positivo y control nativo siguen pendientes.

El positivo `5c64a2248f52ffb09921d439137116901d638012` pasa
[CI 38137693634](https://github.com/KIexster/god-of-war-recomp/actions/runs/38137693634):
595/595, configuración, sintaxis, aplicación de los 96 parches y controles
procedurales completos correctos. La integración de 98 en #51, `f36936e`,
pasa también 597/597 en CI. Se mantiene borrador hasta comprobar Windows
y la transición con el runner recién generado; no se declara resuelta la espera.

## Runner Windows de 98 parches con Count (2026-10-11)

Compilación completa terminada con código 0 a partir de
`f36936e067aa283e4567e3b16700cfe4e546b629`: 898,79 s, 98 parches,
VU1 intérprete y tres tareas. Runner de 54.542.848 bytes, SHA-256
`4693c62c14919c5bd400aa55b8da50a0ab5f5d281d3d20e7b02624aeaa5c6663`.
Se verifican /O2 /Ob2 /DNDEBUG /Zi y enlace /DEBUG /OPT:REF /OPT:ICF;
PDB privado de 177.598.464 bytes. Manifiesto/binario congelado en
`E:\gowgpuprof\transition98_validating`. Las lecturas de Count en el bucle
muestreado se han regenerado con la API del runtime; no se reutiliza el runner 97.

El CI de ese HEAD pasa 597/597. El HEAD documental final de #59, `00d5575`,
pasa también CI completo (38138091541). Se integra mediante merge en #51,
conservando ambos registros en el conflicto de documentación; el parche
positivo es idéntico al usado en la compilación y no cambia la cadena.
La compilación de las pruebas nativas está en curso. La transición todavía
debe repetirse y no se afirma que el defecto de Count explique el encuadre,
alternancia o todos los FPS bajos. Las mediciones de Opus siguen siendo suyas:
documenta 11,6 FPS por defecto y 25,2 con su configuración optimizada; estos
controles con intérprete/hilos apagados/hash/diagnóstico no son comparables.

## Suite nativa de Count y nuevo control del juego (2026-10-11)

Con el runner identificado de 98 y sus fuentes `f36936e`, pasan **597/597
normal y 631/631 GPU real** en Windows, cero fallos conocidos. Hash del
ejecutable de pruebas:
`8741cc50b8a5db8587dd989bd94b143bbda2a9f490bad9764ec84e62f2de4607`.
Pasan los cuatro procesos separados: FINISH/CSR/orden/lectura `default|async`
y FINISH/píxeles de la textura realmente adquirida `default|async`, cada
uno con sus 16 publicaciones y reutilización de ranuras. No se repite la
suite global bajo FINISH asíncrono ni se cambian contratos de los grabadores.

Comienza una pasada nueva de 360 s síncrona/compartida con la misma tarjeta
procedural, guion, variables y hilos apagados, después de terminar las
compilaciones y pruebas. Debe comprobar si persiste la espera observada en
96 y 97; los éxitos anteriores todavía no la dan por resuelta.

## Primera transición compartida síncrona con Count (2026-10-11)

El runner de 98 identificado arriba completa 360,08 s, salida deliberada
por límite (proceso 1/lanzador 0), tarjeta procedural intacta y ninguna
firma de error buscada. Informa 0 → 3 → 4 → 11; primer 11 a 145,14 s del
guion y último 11. En 96/97 el mismo caso dejó de informar tras 4 a ~25 s;
en este ensayo se supera esa espera. No se utiliza el tiempo como benchmark.
La ventana nunca se observa minimizada; primer plano entre ~11 y ~40 s.

1.910 adquisiciones: cero regresiones, metadatos contradictorios o hashes
desconocidos; 25 retornos A–B–A, ninguno en los tramos del último estado 4/11.
Hay 952 adquisiciones en el tramo 11. Solo fuente CRTC (`preferred=0`),
un cambio inicial 1×1 → 512×448. Distancia de ticks 0..34; máximo en secuencia
3 antes del primer informe del mando. Desde estado 0, máximo 3; en los tramos
4 y 11, máximo 1. Esta clasificación no alinea cuadros ni demuestra latencia
sostenida; el diagnóstico solo registra adquisiciones nuevas.

Tres capturas externas privadas de la superficie propia muestran la intro,
enemigos y después HUD/Kratos/enemigo en los muelles. Verificados PID, ruta,
visibilidad y ausencia de minimización, sin activar la ventana. Sus peticiones
pueden perturbar ese instante, y no están alineadas con PCSX2 o la secuencia GS.
Estado 11/capturas no certifican jugabilidad, fidelidad de cámara ni ausencia
de alternancia. Se inicia después una repetición asíncrona/compartida aislada,
con el mismo runner y protocolo. Ambas PR conservan borrador hasta cerrar
validación y CI del HEAD final; los cuelgues con hilos siguen sin probarse.

## Repetición asíncrona compartida con Count (2026-10-11)

El mismo runner de 98 completa 360,04 s sin solapar otra instancia. Límite
deliberado (proceso 1/lanzador 0), tarjeta intacta, sin firmas de error buscadas;
0 → 3 → 4 → 11, primer 11 a 116,40 s y último 11. Ventana nunca observada
minimizada; primer plano solo entre ~344 y ~353 s. Dos capturas externas
propias muestran enemigos y después HUD/Kratos/combate en los muelles;
conservar perturbación de captura y ausencia de alineación PCSX2/secuencia GS.

2.325 adquisiciones, cero regresiones, metadatos contradictorios o hashes
desconocidos, 140 A–B–A: ninguno en los tramos del último estado 4/11.
1.198 adquisiciones bajo el último estado 11. Distancia de ticks 0..3,
máximo 1 en el tramo 11; dimensiones iniciales 1×1 → 512×448 y fuente
CRTC en páginas (`preferred=0`) en todas las filas. Esto no descarta
alternancia visible, retenciones no registradas ni cuelgues más largos/con hilos.

Las dos presentaciones compartidas superan la transición en estas pasadas;
la corrección de Count tiene regresiones negativas/positivas y control nativo,
sin cambiar FINISH/fences. Falta cerrar los dos controles por RAM de 98 y
comparar cámara/escena de forma alineada antes de dar por cerrado #51.
Se inicia síncrona/RAM aislada después de terminar asíncrona/compartida.
## Controles incorporados desde la corrección #59 (2026-10-11)

Se conserva a continuación el registro de validación de la rama Count,
incorporado por merge sin reescribir su historia ni borrar los controles
anteriores. Su HEAD final `ab89804` pasó CI completo y #59 se fusionó en
`cee659a733be0ae9db4764ad99b4e570e9af7849`. Los pendientes siguientes
describen el momento de cada control; #51 conserva la investigación visual.

Control nativo de la integración de 98 en #51, código `f36936e`: build Windows
completo, 898,79 s, salida 0, runner SHA-256
`4693c62c14919c5bd400aa55b8da50a0ab5f5d281d3d20e7b02624aeaa5c6663`.
597/597 normal y 631/631 GPU real correctas, incluidas las cuatro regresiones
de Count. Controles separados de FINISH y de píxeles de texturas adquiridas
pasan ambos modos. Se comprueba la integración con dos parches GS adicionales,
no un ejecutable independiente de esta rama. La transición nueva empieza
después de terminar compilaciones y pruebas; el efecto funcional sigue pendiente.

Primera pasada síncrona/compartida con aquel runner: 360,08 s, terminación
deliberada por límite, tarjeta privada intacta y ninguna firma buscada de error.
Informa estado 11 a 145,14 s; el mismo caso de las cadenas anteriores 96/97
dejaba de informar tras 4 a ~25 s. Se supera la espera en este control.
Capturas externas propias muestran intro, enemigos y finalmente HUD/Kratos
en los muelles, sin alineación PCSX2/secuencia GS y con perturbación de captura.
No se afirma jugabilidad, fidelidad de cámara, ausencia de alternancia o FPS.
Se repite después asíncrona/compartida aislada; falta completar esa pasada y
comprobar CI del HEAD final antes de cerrar la revisión de esta corrección.

La repetición asíncrona/compartida con el mismo runner termina 360,04 s:
primer informe 11 a 116,40 s, último 11, tarjeta intacta y sin firmas buscadas
de error; salida deliberada por límite. Dos capturas propias muestran escena
y después HUD/Kratos/combate, sin comparación alineada ni medida de FPS.
2.325 adquisiciones, cero regresiones/inconsistencias/hash desconocido,
140 A–B–A y ninguno en tramos 4/11. La pasada síncrona también completó 360 s
y superó la espera de las cadenas anteriores. Revisión del cambio: estado
global EE, wrap, escritura/reset de base, extensión de signo y efecto con
destino $zero; sin modificar FINISH/fences ni el checkout de Opus.
El CI del último HEAD documental debe pasar antes de fusionar este tema.
La validación de cámara/alternancia y los hilos permanecen en #51.

## Cierre de la matriz aislada de 98 y publicación Count (2026-10-11)

Se completan las dos RAM con el mismo runner `f36936e`, SHA-256
`4693c62c14919c5bd400aa55b8da50a0ab5f5d281d3d20e7b02624aeaa5c6663`:
síncrona 360,04 s, primer informe 11 a 126,41 s; asíncrona 360,05 s,
primer 11 a 121,31 s. Último estado 11 en ambas, salida deliberada por
límite (proceso 1/lanzador 0), tarjetas intactas y ninguna firma buscada
de error. No minimizadas; primer plano desde ~21/~41 s respectivamente.
Una captura propia de cada RAM muestra escena/enemigos o HUD/Kratos en
los muelles, sin alineación PCSX2/cuadro GS y con perturbación de captura.

La matriz nueva de cuatro casos termina sin solapar juegos/compilación;
las dos compartidas alcanzaron 11 a 145,14/116,40 s. Se supera la espera
síncrona compartida de las cadenas 96/97 con el contador actualizado. Esto
no equivale a declarar cámara correcta, ausencia de alternancia o FPS mejores.
Matriz y límites en `TRANSICION_COMPARTIDA.md`; los hashes no certifican
píxeles iguales y no registran retenciones. Se inicia VU1 en hilo por separado,
todavía con intérprete y hash/diagnóstico: control funcional, no benchmark.

[PR #59](https://github.com/KIexster/god-of-war-recomp/pull/59) revisada y
fusionada por merge en `cee659a733be0ae9db4764ad99b4e570e9af7849`, tras
[CI completo del HEAD final `ab89804`](https://github.com/KIexster/god-of-war-recomp/actions/runs/38139636066).
#51 y #58 integran ese main por merge conservando ambos registros; orden
Count → direcciones GS → diagnóstico. Verificado: los 97 parches de #58
coinciden en orden/contenido con el prefijo compilado de #51, y la cadena 98
de #51 sigue idéntica al código del runner. No se modificó Opus. #51 sigue
borrador por comparación visual y hilos pendientes; comprobar CI de cada HEAD.
## Integración y revisión final de unidades GS (2026-10-11)

La corrección Count #59 se fusionó en `cee659a` tras CI completo del HEAD
`ab89804`. Esta rama integra main por merge, preservando ambas investigaciones
y aplicando Count antes del parche de direcciones GS: 97 parches. La rama
#51 ya compiló esa combinación más el diagnóstico del host (98) en Windows
desde `f36936e`, runner SHA-256
`4693c62c14919c5bd400aa55b8da50a0ab5f5d281d3d20e7b02624aeaa5c6663`.
597/597 normal, 631/631 GPU real y los controles separados de ambas formas
de FINISH/texturas adquiridas pasan. No se atribuye al ejecutable un cambio
posterior de código: los parches de Count y direcciones siguen idénticos.

La espera síncrona compartida se supera con Count; no la corrige este tema
de unidades. La selección del juego continúa `preferred=0`, sin probar
que el arreglo de bases sea causal de cámara/alternancia. La revisión comprueba
las tres conversiones página/bloque, TBW explícito/cero, metadatos y los
controles negativos/positivos CPU y GPU. La semántica física de ambos pares
y la reutilización de ocho ranuras quedan cubiertas sin alterar las
matemáticas del renderer CPU ni FINISH/fences. Falta verificar el CI del
HEAD final tras este merge antes de fusionar #58.

## Publicación GS, controles con hilos y nueva ventana (2026-10-11)

#58 se fusiona por merge en `fac216674e9d763ad418d9c1a9c9ce05088955d1`,
tras [CI completo del HEAD final `957f9da`](https://github.com/KIexster/god-of-war-recomp/actions/runs/38140210658).
#51 integra el main nuevo mediante merge (`a64ed52`), preservando registros
y la cadena de 98 ya compilada. Ambas correcciones (#58/#59) están publicadas
en main; #51 conserva borrador por investigación visual/estabilidad pendiente.

Control 98 asíncrono/compartido con VU1 en hilo, frente GS apagado:
360,04 s por límite, primer informe 11 a 80,90 s, último 11, tarjeta intacta,
sin firmas buscadas de error o minimización; captura privada HUD/Kratos/enemigos
sin alineación. El cupo de 4.096 se agota antes del informe 11 (`hostTick=4136`):
3.064 A–B–A, cero regresiones/inconsistencias/hash desconocido, todos anteriores
a partida. No utilizar ese registro para certificar la presentación de estado 11.

VU1+frente GS alcanza 11 a 80,83 s pero cierra antes: 228,17 s, salida 0,
`process_exit`, sin causa determinada. Último 11, tarjeta intacta, sin firmas
buscadas de error/minimización o captura válida propia; no completa los 360 s
ni prueba estabilidad. Cupo agotado también antes de 11, 3.045 A–B–A. Se conserva
como incompleto y debe repetirse. Los dos controles usan intérprete y hash;
sus tiempos no son FPS ni equivalencia con las mediciones optimizadas de Opus.

Se implementa un parche nuevo de ventana diagnóstica, sin variar Acquire,
FINISH/fences/readback ni el límite 4.096: permite empezar desde un hostTick
explícito para observar partida con hilos. `PS2X_GS_PRESENT_DIAG_FROM_TICK`,
ausente/inválido → 0; lanzador `--diag-from-tick`, rango uint64 validado antes
de escrituras. Tres regresiones MiniTest/MSVC reducidas pasan; negativo con
la semántica previa compatible falla exactamente dos (1/3), sin afirmar
suite completa. 14 pruebas Python del lanzador y 17 del inspector correctas,
configuración/sintaxis correctas, nueva aplicación independiente sobre los
98 anteriores. Cadena 99 pendiente de compilación Windows completa,
suite nativa/CI y repetición del juego con ventana. No se atribuye el nuevo
parche al ejecutable existente ni se declara resuelto encuadre/alternancia.

## Cadena 99 nativa completa y fast boot desactivado (2026-10-11)

Código `3dbac1a442add16518b0504975bfa715f1b32dc4`: compilación Windows
completa, 99 parches, salida 0 en 1.005,59 s; VU1 intérprete, Release
optimizado/símbolos privados/tres tareas. Runner SHA-256
`cf063d2f03a1a78f455a9ff57a1378cd4a1ddaf71e6d279d305b34663b96fa07`.
600/600 normal y 634/634 GPU real correctas; cuatro controles separados
FINISH/lectura y FINISH/textura adquirida `default|async`, todos salida 0.
[CI del mismo HEAD correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38152876084).
La ventana diagnóstica ya está en este ejecutable; falta repetir el juego.

Revisión de overrides: `GOW_FAST_BOOT=0` no activa el atajo, pero instalaba
las sondas por mera presencia y emitía animación/flash-ready en los controles
anteriores. Se corrige solo su condición de instalación para exigir `1`,
manteniendo los diagnósticos explícitos y llamadas originales. No es una
corrección de cámara ni una medición de FPS. El runner anterior no incluye
este cambio de `src/`: necesita recompilación rápida privada, nuevo commit/hash
y control que compruebe la ausencia de aquellas etiquetas. Opus continúa
limpio y no se modifica su checkout; main integrado por merge.

El control de Windows por Computer Use falla al conectar su pipe nativo
(archivo ausente); importación del módulo correcta, dos consultas ligeras
fallidas, ninguna entrada enviada. CLI/compilación/pruebas disponibles.
No se utiliza ese fallo como prueba visual ni se atribuye al juego.

## Primer control 99 con FAST_BOOT=0 aislado (2026-10-11)

Overrides recompilados sobre la cadena 99 completa desde `a37cd85`, salida 0;
runner SHA-256 `a4ec4365273903d525ce19ecade3fb423fb871c5628a84e695b33ac777ca3598`,
[CI del mismo HEAD verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38154314459).
Asíncrona/compartida sin hilos, diagnóstico desde tick 0, completa 360,06 s por
límite; informe 11 desde 154,02 s, último 11, tarjeta intacta, ninguna firma
buscada de error. Se comprueba ausencia de sondas animación/flash-ready.
Ventana visible/nunca minimizada, siempre sin primer plano; sin captura propia.

2.046 adquisiciones, 27 A–B–A solo antes de informe 3, cero regresiones/
inconsistencias/hash desconocido. Bajo último informe 11 hay 983 adquisiciones
y ninguna A–B–A; fuente CRTC en todas. Distancia máxima de ticks 37 en arranque,
5 en tramo 4 y 1 en 11. Detalles en TRANSICION_COMPARTIDA.md; no se comparan
FPS ni se declara solución visual. Se inicia otro control privado VU1 en hilo
con ventana tardía desde 6000; GS frontal se repetirá después, por separado.

## Adquisiciones de partida con VU1 en hilo (2026-10-11)

Mismo runner de 99/overrides `a37cd85`, asíncrona/compartida, VU1 en hilo
y GS frontal apagado, ventana desde 6000: 360,06 s completos por límite,
informe 11 desde 81,05 s, último 11, tarjeta intacta. Ninguna firma buscada
de error ni sondas animación/flash-ready; no minimizada, sin primer plano
o captura propia, sin solapamientos. El primer registro es hostTick=6000,
frameTick=5999, seq/render=5880. 4.096 adquisiciones todas en tramo 11, fuente
CRTC, 512×448/hash válido, cero regresiones/inconsistencias/cambios de dimensión,
distancia de ticks 0..2. El filtro observa ahora partida, a diferencia de 98.

Los 3.009 retornos A–B–A alternan paridad de frameTick. El inspector conserva
los tres ticks y agrega la clasificación de paridad sin ocultar retornos;
20 pruebas pasan. Esto es compatible con campos alternos (la prueba GPU
procedural compara campos distintos con VRAM invariable contra CPU), pero
no identifica SMODE2 ni demuestra causa/corrupción o ausencia de imágenes
antiguas. Mantener límites de hash/cupo y cámara separada. Se inicia aislada
la repetición VU1+GS frontal de 360 s con ventana desde 6000; aún pendiente.

## Control 99 completo con ambos hilos (2026-10-11)

Repetición asíncrona/compartida con VU1+frente GS y ventana desde 6000,
mismo runner `a37cd85`: completa 360,04 s por límite, informe 11 desde 80,97 s,
último 11, tarjeta intacta. No hay firmas buscadas de error ni sondas animación/
flash-ready; no minimizada, sin primer plano/captura propia y sin solapamiento.
No se reproduce en esta pasada el cierre de 98 a 228,17 s. Una repetición
correcta no determina su causa ni prueba estabilidad prolongada.

4.096 adquisiciones en tramo 11, primera hostTick=6000/frameTick=5999,
seq/render=5933. Cero regresiones/inconsistencias/hash desconocido o cambios
de dimensión; 512×448/fuente CRTC, distancia de ticks 0..1. Los 3.506 A–B–A
alternan paridad, compatible con campos pero sin SMODE2 registrado ni prueba
de causa. El CI de `6f30f5c` está verde. Las tres pasadas finitas de 99 ya
terminaron; siguen pendientes comparación visual alineada/encuadre y duración
mayor. #51 conserva borrador. Siguiente control: SMODE2 de la textura adquirida,
con regresión que descarte confundirlo con el CRTC actual. Detalles y límites
en TRANSICION_COMPARTIDA.md. No se publican datos del juego ni FPS.

## Preparación de SMODE2 adquirido, sin cambiar la presentación (2026-10-11)

Los miles de retornos A–B–A de partida en 99 alternan paridad, pero faltaba el
modo del request. Parche nuevo por tema `ps2recomp-gs-acquired-mode-diag.patch`:
guardar SMODE2 completo en la ranura publicada y registrarlo desde el Frame
adquirido. No inferirlo del CRTC actual ni añadir readback/Acquire/hash/barreras.
Una regresión GPU real comprueba campos/cuadro/progresivo, píxeles contra CPU
y metadatos nuevos/retenidos; ejecución pendiente. La rama pasa a 100 parches,
con aplicación independiente del nuevo correcta y sintaxis sin errores.

Inspector 24/24 pruebas: campo opcional decimal uint64, metadatos consistentes,
compatibilidad con logs previos y tres modos por retorno, sin ocultar A–B–A.
Las adquisiciones de 99 no acreditan este runtime; necesitan otra compilación
completa, suite/GPU/negativo y control con el juego. #51 sigue borrador.
Se mantiene separada la investigación de encuadre y no se modifican matemáticas.

## Primer control nativo de 100: diferencia de altura en modo cuadro (2026-10-11)

Compilación Windows completa desde `4a000c48e2d111637a77318eb509dd88f35d653d`,
100 parches, salida 0 en 943,29 s. Runner congelado SHA-256
`fb46a87ac6d80a600f8662ae3ee6b74bbb5b37b84f1cc10fa0eb0b0f3d4367b5`,
Release optimizado con símbolos privados, VU1 intérprete. [CI del mismo HEAD
correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38156407391).
600/600 normal; **634/635 GPU real: falla la nueva prueba de SMODE2**, por
altura lógica distinta y comparación CPU vacía. No se ejecutan los contratos
posteriores ni el juego con este runner; no se presenta como validación completa.

La revisión localiza la diferencia en el caso `SMODE2=0x403`: con DISPLAY de
64×128, GPU divide la altura por dos y CPU conserva la altura completa. Es
código de composición previo, sin cambios en el parche del metadato; todavía
no se determina qué salida debe usarse ni si el juego utiliza ese modo en la
transición. No se atribuye al encuadre ni se cambia el renderer para hacer
pasar una prueba del diagnóstico.

Se precisa el alcance de la nueva regresión: todos los modos conservan
SMODE2 completo en una textura realmente adquirida y retenida; campos y
progresivo mantienen comparación de todas las filas contra CPU. En modo
cuadro se registra la dimensión CPU/GPU y se valida solo textura/metadatos,
conservando explícitamente pendiente su equivalencia. Se mantiene el caso,
sin añadir excepciones a `fallos_conocidos.txt` ni ocultar el resultado inicial.
El parche corregido aplica sobre una base independiente de 99. Requiere otra
compilación completa, suite/negativo significativo y controles del juego;
#51 sigue borrador. La diferencia de altura es una investigación separada.

## Diagnóstico SMODE2 de 100 validado nativamente (2026-10-11)

Compilación completa revisada de `383c6b65986ffe2cddebaed987d9132ea791d100`,
salida 0 en 928,11 s, runner SHA-256
`5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`.
[CI del mismo código verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38157988334).
Windows: 600/600 normal, 635/635 GPU y cuatro contratos independientes
FINISH/lectura/textura adquirida correctos, sin fallos conocidos. Se renuevan
los objetos de los controles privados para el nuevo layout de Frame.

El negativo privado fuerza solo el metadato a cero, conserva la API y compila:
634/635, falla exactamente el caso nuevo de SMODE2. Restauración del archivo
positivo y otra suite GPU: 635/635. El runner permanece positivo y congelado
durante ese control; no se ejecuta el juego sobre la variante negativa.
La prueba distingue modo de textura adquirida/retenida del request posterior.

Se mide además la discrepancia del caso cuadro: CPU 64×128, textura GPU 64×64
con SMODE2=1027. Su equivalencia permanece pendiente; campos/progresivo sí
mantienen comparación de todas las filas. No se cambia el renderer para cerrar
esa diferencia ni se atribuye a la escena del juego sin registrar su modo.
Siguiente paso: controles de 100 sin hilos, con VU1 y con ambos hilos, ventana
tardía y tarjetas privadas. #51 sigue borrador hasta cerrar validación del
nuevo diagnóstico y CI final. Cámara/alternancia no se declaran resueltas.

## Primer control del juego de 100: campos en la textura adquirida (2026-10-11)

Runner `383c6b6`/SHA-256
`5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`,
asíncrona/compartida sin hilos, ventana desde 0: 360,06 s completos por límite,
primer informe 11 a 133,85 s, último 11, tarjeta intacta. Ninguna firma buscada
de error ni sondas de animación/flash-ready; nunca minimizada, primer plano
entre 17,12 y 41,29 s, sin captura propia ni solapamientos. Detalles en
TRANSICION_COMPARTIDA.md; no se afirma cierre normal o equivalencia visual.

1.955 adquisiciones, sin regresiones/inconsistencias/hash desconocido; seis
A–B–A solo antes de los tramos 4/11, todos alternan paridad y tienen modo de
campos. Las 1.063 adquisiciones de tramo 11 registran SMODE2=2227921 (`&3=1`),
512×448/fuente CRTC, ninguna A–B–A y distancia de ticks 0..1. El caso cuadro
con altura distinta (`&3=3`) no corresponde a estas muestras. Esto no prueba
la causa del encuadre ni excluye imágenes antiguas fuera del registro.

[CI `6a53774` verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38159575279).
Continúa otra pasada privada con VU1 solo y ventana desde 6000; después ambos
hilos. Mantener #51 borrador durante la validación pendiente, sin declarar FPS.

## VU1 solo de 100: ventana tardía con modo de campos confirmado (2026-10-11)

Mismo runner `383c6b6`/SHA-256
`5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`,
asíncrona/compartida, VU1 en hilo, frente GS 0 y registro desde 6000. Completa
360,04 s por límite, primer informe 11 a 80,98 s y último 11 a 357,24 s.
Tarjeta intacta, sin firmas buscadas de error ni sondas antiguas; ventana
visible/no minimizada/sin primer plano según monitor. Sin captura propia.

4.096 adquisiciones de tramo 11, todas SMODE2=2227921 (`&3=1`, campos),
512×448/fuente CRTC/hash válido. 3.562 A–B–A: todos alternan paridad y tienen
modo de campos en las tres texturas. Sin regresiones/inconsistencias o cambios
de dimensión; distancia de ticks 0..1. La hipótesis de campos gana apoyo,
pero no demuestra causa visual ni ausencia de imágenes antiguas; el cupo
no cubre toda la pasada. Encuadre/FPS/equivalencia con PCSX2 no acreditados.

[CI `e3678cb` verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38160245742).
Otra pasada aislada con ambos hilos está en curso; no usarla aún como validación.
Se conservan las salidas anticipadas anteriores y sus causas pendientes.

## Ambos hilos de 100 completos; diagnóstico listo para revisión final (2026-10-11)

Mismo runner `383c6b6`/SHA-256
`5f3b9c29dad7797e7edaa17585254eccaadd14238a834d15b69101c28895c74f`,
asíncrona/compartida, VU1+frente GS en hilo, ventana desde 6000: 360,05 s
completos por límite, primer informe 11 a 80,92 s y último 11 a 357,49 s.
Tarjeta intacta, sin firmas buscadas de error ni sondas antiguas; ventana
visible/no minimizada/sin primer plano según monitor, sin captura propia.

4.096 adquisiciones de tramo 11, todas SMODE2=2227921 (`&3=1`, campos),
512×448/fuente CRTC/hash válido. 3.542 A–B–A: todos alternan paridad y tienen
modo de campos en las tres texturas. Sin regresiones/inconsistencias/cambios
de dimensión; distancia de ticks 0..1. La salida anticipada de 98 no se
reproduce aquí, pero su causa y la estabilidad prolongada siguen pendientes.

Las tres pasadas de 100, compilación completa, suite normal/GPU, negativo
significativo restaurado y cuatro contratos cierran la validación funcional
de la instrumentación de #51. [CI `bae5d74` verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38160854972).
Revisión manual del cambio y CI del HEAD final preceden a marcarla lista y
fusionarla. Las referencias anteriores a borrador por comparación visual no
se convierten en una afirmación de fidelidad: #51 solo diagnostica; no corrige
el encuadre ni demuestra ausencia de contenido antiguo. Detalles en
TRANSICION_COMPARTIDA.md. Siguiente tema: captura explícita y acotada de la
textura adquirida para comparar campos; conservar privados los píxeles.

## #51 fusionada; siguiente tema: píxeles de la reserva adquirida (2026-10-11)

#51 fusionada en `05421940617e5ce69889600d9eb727b3c36c0b3b`, revisión manual,
validaciones nativas/funcionales anteriores y [CI de su HEAD final `10edb76`
correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38161351218).
Se cierra la instrumentación; no se declaran resueltos encuadre, causa visual
de A–B–A ni estabilidad prolongada.

Rama siguiente con parche separado `ps2recomp-gs-host-acquired-capture.patch`:
descarga explícita y acotada de la textura de esa misma reserva, PNG RGBA y
metadatos, sin consulta a VRAM ni otro Acquire. Desactivada por defecto,
independiente del diagnóstico de #51; máximo 16 intentos, sin sobrescritura.
Su coste impide usarla como benchmark. `--capture-acquired 6` del lanzador
prepara la carpeta privada y ventana desde `--diag-from-tick`. Protocolo y
variables en TRANSICION_COMPARTIDA.md.

Regresión GPU real preparada para demostrar que cambiar VRAM a B después
de adquirir A no cambia el PNG de A; comparar todos sus píxeles y luego B.
Sintaxis C++20 y aplicación independiente correctas; lanzador 17/17,
configuración 0 errores/4 avisos existentes, PowerShell 0 errores.
Pendientes compilación completa de 101, nativas/negativo/restauración, CI y
capturas del juego. No atribuirlas al runner de 100 ni cambiar matemáticas.

## Captura de reserva validada con GPU real y negativo de píxeles (2026-10-11)

Compilación completa de 101 desde `394779a6dca0fd8dcaf7415e06ad048fdd9ad7f8`,
salida 0 en 916,19 s, runner SHA-256
`63e6a576307675f34b70e4671a5b34de30219627a2ca2a3a362c182effd27040`,
54.552.576 bytes, Release optimizado/símbolos privados/VU1 intérprete.
[CI del mismo código verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38161970774).
Windows 600/600 normal, 636/636 GPU y cuatro contratos FINISH correctos.
El PNG de A conserva todos sus píxeles aunque la VRAM ya contenga B; la prueba
también publica/exporta B y verifica metadatos, ventana/cupo/retenciones y
rechazo de sobrescritura. La discrepancia de altura de modo cuadro sigue abierta.

Negativo API-compatible: imagen magenta del mismo tamaño en vez de leer
la textura; solo recompilar pruebas, sin tocar/ejecutar el runner. 635/636,
fallo único del nuevo caso de captura por píxeles distintos. Archivo positivo
entero restaurado y otra suite GPU: 636/636. Hash de fuente restaurada igual
al respaldo; runner actual y copia congelada idénticos.

Pasada del juego sin hilos, seis capturas desde 1500, en curso. Después VU1
desde 6000; no atribuir todavía resultados ni resolver encuadre/alternancia.
Registrar PNG/JSON privados contra las adquisiciones antes de comparar bytes;
sin equivalencia PCSX2 ni FPS. Identidad y límites en TRANSICION_COMPARTIDA.md.

## Capturas reales: igualdad RGBA A–B–A en modo de campos (2026-10-11)

Dos pasadas completas de 101, mismo runner `394779a`/SHA-256
`63e6a576307675f34b70e4671a5b34de30219627a2ca2a3a362c182effd27040`,
asíncrona/compartida, tarjetas intactas y sin firmas buscadas de error:
sin hilos/desde 1500 (360,07 s, informe 11 desde 131,78 s) y VU1 solo/desde
6000 (360,05 s, informe 11 desde 80,83 s). El primer caso estuvo minimizado
entre 44,22 y 156,01 s; capturas posteriores, condición no constante, repetir.
El segundo no se minimiza ni toma primer plano según el monitor.

Ambos guardan seis PNG RGBA y seis JSON que coinciden con los metadatos de
su adquisición en el log; cada imagen duplica sus 224 pares de filas. En el
primero todos los contenidos son distintos, sin A–B–A en 643 registros de
la ventana. En VU1 hay 4.096 registros/3.204 A–B–A, todos campos/paridad
alterna, sin regresiones/inconsistencias; distancia de ticks 0..2. Los PNG
de ticks 5999/6001 son idénticos byte a byte y el de 6000 es distinto:
confirmación finita del A–B–A en píxeles, además del hash. Otro par impar
6003/6005 es idéntico. Primeras imágenes revisadas muestran Kratos/cubierta
en la misma cámara dentro de cada terna; no un menú alternante allí.

No prueba frescura de VRAM, causa del síntoma original, ausencia fuera de la
ventana, equivalencia CPU/PCSX2 o encuadre correcto. Tampoco son comparables
las poses/cámaras de ventanas 1500 y 6000 entre pasadas. [CI `ce23d73` verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38163566080).
Otra repetición sin hilos está en curso; #60 sigue borrador hasta cerrarla y
comprobar el CI final. Píxeles/JSON privados, sin cambios matemáticos o FPS.

## Repetición sin minimización: control funcional de #60 cerrado (2026-10-11)

Tercera pasada de 101 con el mismo runner `394779a`/hash anterior,
asíncrona/compartida sin hilos, desde 1500: 360,04 s por límite, primer
informe 11 a 116,48 s y último 11; tarjeta intacta, sin firmas buscadas de
error ni sondas antiguas. El monitor no registra minimización. 870
adquisiciones de campos, sin regresiones/inconsistencias/A–B–A/cambios de
dimensión, distancia de ticks 0. Seis PNG/JSON completos correlacionados
contra sus adquisiciones (1462..1467, ticks 1500..1505), contenidos RGBA
distintos y 224/224 pares de filas duplicados en cada imagen.

Revisión visual de las primeras dos: misma vista de Kratos/cubierta dentro
del par. Un tick coincidente entre pasadas no alinea cámara/animación,
porque el guion depende del reloj anfitrión; no inferir causas por comparar
esas poses. La minimización anterior se conserva como limitación separada.

La compilación completa, nativas/negativo restaurado, contratos y tres
pasadas cierran la validación de la captura opcional de #60. Revisión manual
del uso de la reserva Acquire correcta; [CI `9332c1b` verde](https://github.com/KIexster/god-of-war-recomp/actions/runs/38164770068).
Comprobar el CI del HEAD final antes de fusionar. No se declaran resueltos
encuadre, contenido antiguo, modo cuadro o estabilidad prolongada. Próximo
paso: relacionar cámara/animación con la escena realmente dibujada y una
referencia alineada; evitar corregir matrices por observaciones de instantes
distintos. Detalles y hash en TRANSICION_COMPARTIDA.md; píxeles privados.

## #60 fusionada; cámara, repetición GS y síntomas de modelos (2026-10-11)

#60 fusionada en `f41be4eefc31c0ebde3500b4c3b02f3815e9cfd9`: revisión manual,
compilación completa y controles anteriores; [CI del HEAD final `8530914`
correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38175464970)
y [CI del merge correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38175870557).
Se cierra la captura opcional, sin atribuirle la solución del fallo visual.
El runner sigue siendo `394779a`/101, SHA-256
`63e6a576307675f34b70e4671a5b34de30219627a2ca2a3a362c182effd27040`.
Incluye #55 y cambios posteriores; estas pasadas con intérprete, sondas y
hilos desactivados no reproducen la configuración de rendimiento de Opus.

Control síncrono/compartido, sin hilos, con las sondas existentes de cámara
y animación: 360,05 s por límite, primer informe 11 a 126,83 s, tarjeta intacta,
sin firmas buscadas de error. Minimización tardía desde 325,28 s; no afirmar
ventana constante. 268 muestras de cámara completas y finitas. La principal
de 11 usa matriz local (`joint=0xFFFF`), con 32 posiciones distintas y matrices
iguales a las del cliente. Residuo máximo mundo por inversa 0,0002045 y
combinada 0,0001827: descripción numérica, no tolerancia de fidelidad. Esto no
identifica la cámara correcta. Las referencias PCSX2 guardadas corresponden
a menú/intro (estados 3/4), no a una partida 11 alineada.

Otra pasada síncrona/RAM, sin hilos ni hash/sondas/capturas adquiridas, utiliza
solo `5:start,12:x,20:x`; **cambia el guion** respecto a controles anteriores.
240,04 s por límite, primer informe 11 a 126,29 s, tarjeta intacta, sin firmas
buscadas de error. Traza GS desde TEXFLUSH real a 150,028 s, completa por
presupuesto de bytes antes del segundo solicitado: 54.535.090 bytes, seis
Present y 145.452 Submit. Traza/píxeles/datos del juego permanecen privados.

Repetición del **prefijo hasta el primer Present**, registro 12795/12693 draws,
con herramientas nativas enlazadas al runtime 101. CPU: dos pasadas idénticas
entre sí y contra CPU en 4 MiB de VRAM, estado e imagen. Compute solicitado:
dos pasadas idénticas, pero 326.270 bytes VRAM distintos frente a CPU e imagen
distinta. Hardware: dos pasadas con 7472 primitivas/cero tiles difieren entre
sí en 1796 bytes VRAM y 1916 bytes RGBA visibles, con mismo estado. La primera
pasada hardware usa ruta mixta y se excluye de esa comparación.

La protección experimental existente `--snapshot-feedback` congela la fuente
por Submit: tres pasadas hardware, todas con cero tiles, son idénticas en
VRAM/estado/RGBA; su RGB coincide con compute en esta muestra. **No es una
corrección activada por defecto ni emulación de la caché PS2**, no resuelve la
diferencia CPU y no prueba la causa del encuadre o del síntoma original.
Lockstep compute protegido encuentra antes una divergencia CPU de dos bytes
en un triángulo sin textura (registro 76/draw 73); separar esa discrepancia de
la variación hardware. El resto de la traza y su End original no se validan
con este prefijo. Nada de esto mide FPS ni demuestra fidelidad PCSX2.

El usuario amplía el síntoma: movimientos como espasmos y deformaciones de
modelos/animaciones de Kratos y enemigos, además del encuadre. Es una
observación comunicada, todavía sin comparación alineada propia. En la próxima
revisión observar una secuencia de movimiento: extremidades, forma y posición,
no solo capturas aisladas. Priorizar entradas/salidas reales de skinning y sus
índices, luego envío a VU/GS. La paleta leída al entrar en ProcessModel precede
al cálculo posterior: valores finitos allí no descartan este fallo. Leer
RAM/scratchpad con límites separados, respetar checkpoints y observar solo
retornos reales. No corregir matrices ni activar protecciones por intuición.

Se prepara `GOW_SKIN_DIAG=1`, observación posterior de ambas variantes y lectura
del destino a2 separado de la paleta a1. Límites, campos y descarte de pausas
en CONTROLES.md. Pruebas procedurales Windows/MSVC con /W4 /WX y sintaxis
C++20 del override correctas; todavía pendientes recompilación rápida de src/
con nuevo hash y pasada funcional. No se modifica ningún parche o matemática.

## Skinning observado después del retorno real (2026-10-11)

La sonda opcional anterior se recompila desde `e0bf966` mediante
`2_recompilar_rapido.cmd`, solo src/, sobre la compilación completa 101 de
`394779a`. Sin diferencias de parches ni de runtime.lib; termina con código 0
en 19,12 s. Runner 54.559.744 bytes, SHA-256
`063ea7299ffb9bb5bc413432df63f555a605225198557fc5cd753983ad2780a0`.
[CI de ese código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38178208986),
incluidas compilación de src/, pruebas procedurales y suite del runtime.
No confundir esta actualización de src/ con una nueva compilación de parches.

Primer intento síncrono/compartido, hilos 0, no alcanza estado 11: se detiene
con STOP privado a 144,89 s, todavía en menú 3 y sin llamadas de skinning.
Se conserva como intento fallido. El siguiente añade pulsaciones X de menú
a 28/36/44/52/60/68 s: **cambia el guion**, no es repetición alineada. Termina
a 300,05 s por límite, primer informe 11 a 135,49 s y último a 294,36 s.
Tarjeta procedural privada intacta, sin firmas buscadas de fatal/exception/
deadlock/assertion failed/unhandled; el monitor no registra minimización.
Código 1 del juego por cierre externo, controlador 0: no salida natural.

128 muestras completas, ninguna pausa observada, cinco combinaciones de
skeleton/variante/vista/ownerCandidate. Se ejecutan ambas rutas 0x137508 y
0x137AF8. Las 6.173 lecturas de destino, mundo y fuente válidas son finitas.
5.629 slots seleccionados cuyo destino cambió tienen matriz de definición
válida: el producto mundo por definición concuerda con el destino, con
residuo máximo 0,0001361 usando producto de columnas en double. Es una
descripción numérica de esta muestra, **no tolerancia PS2 ni prueba de pose
correcta**. Las 544 matrices de definición no leídas pertenecen a slots no
seleccionados; no son accesos fallidos ni evidencia de corrupción. El cupo
global se agota antes de que todos los grupos lleguen a 32 muestras.

16 PNG/JSON de la reserva adquirida, secuencias 1453..1468 y ticks 1500..1515:
metadatos exactos contra el log, RGBA completos y distintos, 224 pares de
filas duplicados en cada uno. Hash desactivado: se comparan los bytes del PNG,
no hashes desconocidos. La revisión de la secuencia muestra movimiento de
una figura cercana sobre la cubierta, pero **su identidad no está confirmada**
y el encuadre no separa protagonista/enemigos. No se demuestra aún el espasmo
comunicado ni se alinea automáticamente cada PNG con una muestra de skinning.
Las entradas de animación o su envío posterior a VU/GS siguen bajo sospecha;
esta evidencia no descarta esos puntos ni resuelve el encuadre.

Se prepara otro guion con ataques explícitos; las direcciones actuales del
guion son botones digitales, no stick analógico. No afirmar desplazamiento
del protagonista a partir de ellas. Píxeles, matrices, binarios y tarjetas
permanecen privados; controles con lecturas/E/S e intérprete no miden FPS.

## Control de enemigos y preparación de movimiento analógico (2026-10-11)

#61 fusionada en `b29e27e3f4efab6ca4b482392cd4ff56ab174f88`, tras revisión
manual, controles anteriores y [CI del HEAD final `63f32f1` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38179505671).
Un negativo procedural en copia privada cambia el acceso a definición por
índice de slot en lugar de ordinal: compila, falla la prueba (1); restaurado
pasa (0). El header del repositorio permanece intacto. No hubo juego con
esa copia ni cambios en el runtime.

Otra pasada del runner `e0bf966` añade cuadrados/triángulos cada cuatro
segundos entre 180 y 292 s; **cambia el guion**, hash 0, síncrona/compartida,
hilos 0. 300,05 s por límite, informe 11 desde 121,86 s hasta 295,60 s,
tarjeta intacta, sin las firmas buscadas de error ni minimización registrada.
128 retornos completos de ambas variantes, cinco grupos, sin pausas:
6.150 lecturas válidas finitas y 5.606 productos seleccionados/cambiados,
residuo máximo descriptivo 0,0001457. Los 544 bind no leídos siguen siendo
slots no seleccionados. No define una tolerancia PS2.

16 PNG/JSON adquiridos en secuencias 1701..1716, ticks 1750..1765, con
metadatos exactos, contenidos RGBA distintos y todos los pares de filas
duplicados. Se revisan imágenes de varios enemigos con brazos/cuerpo en
movimiento; no se confirma todavía una deformación anómala contra referencia.
La escena parece corresponder a la introducción. Estado 11 no garantiza que
el protagonista ya sea controlable; tampoco la entrega de botones confirma
su efecto. No comparar las poses con la captura anterior por tiempo/tick.

Se añade al guion opcional stick izquierdo y duración explícita, sin cambiar
los botones digitales anteriores, más registro acotado del paquete devuelto.
Se prepara también una ventana tardía de la sonda de poses para no gastar su
cupo durante la introducción. Pruebas y siguiente pasada prolongada pendientes
de documentar al completarlas; no se modifica ningún parche o matemática.

Pruebas procedurales de guion/paquetes y ventana de skinning correctas en
Windows/MSVC /W4 /WX; sintaxis C++20 del override correcta y configuración
con 0 errores/4 avisos existentes. Compilación rápida y control analógico
con el juego todavía pendientes. Se revisa también la referencia propuesta
de Persona 4: [alcance y material útil](REFERENCIA_PERSONA4.md), sin importar
su código ni atribuirle un arreglo.

La actualización de src/ desde `73588d2` termina con código 0 en 19,97 s
sobre el mismo runtime 101 (runtime.lib intacta). Runner 54.561.792 bytes,
SHA-256 `0df18c12eb20455c0440fa26f9799f3b85be3babd7e0c0062f62f7cf5708d1be`.
[CI de ese código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38180275266);
[CI del merge de #61 correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38179912408).
La primera ejecución se cierra con STOP a 65,45 s por una ruta privada de
salida mal escrita; no cuenta como control completado. Se relanza en el
directorio privado verificado con tarjeta procedural nueva.

El usuario identifica como espasmo la pose del brazo elevado de Kratos en
una captura aportada. Hay una pose visualmente semejante en la secuencia
adquirida anterior; se conserva como ejemplo señalado, no como pose normal
verificada. El cruce por orden del log muestra una limitación concreta:
las 32 muestras iniciales del skeleton candidato de 111 slots terminan cerca
del informe de host 140,56 s; la adquisición de tick 1500 queda entre los
informes 186,02 y 191,06 s. **No corresponden a la misma ventana**. Son límites
aproximados por orden de registros, no timestamps exactos de ejecución.
No usar aquellos productos correctos para descartar el cálculo de la pose
señalada. Además del control tardío en marcha, recoger otra ventana de sonda
y captura desde 1500; todavía falta identificar el modelo y una referencia
PCSX2 alineada para localizar la alteración.

El control analógico prolongado termina a 540,05 s por límite, estado 11
desde 132,06 s hasta 536,28 s, tarjeta procedural intacta y sin firmas
buscadas de error. 78 registros de paquete, 43 con stick no neutral: las
cuatro direcciones se devuelven por libpad2, con botones de ataque combinados.
No hay minimización; el primer plano cambia seis veces y las entradas reales
están excluidas por el modo solo guion. **No se obtienen muestras de skinning,
adquisiciones registradas ni PNG desde 3000**: no prueba movimiento visible,
una pose tardía correcta ni la causa de la ausencia de registros. Se conserva
ese resultado y no se cuenta como validación visual de la ventana.

Nueva pasada desde 1500 en curso, con el mismo runner y el guion de nueve
pulsaciones de menú, sin movimientos/ataques añadidos, para observar matrices
en la ventana de la pose señalada. Sigue sin referencia PCSX2 alineada.

## Ventana de matrices junto a la postura señalada (2026-10-11)

La pasada anterior desde 1500 termina a 300,04 s por límite, runner `73588d2`
y SHA-256 `0df18c12eb20455c0440fa26f9799f3b85be3babd7e0c0062f62f7cf5708d1be`.
FINISH síncrono, compartida, hilos 0, hash 0; tarjeta procedural intacta,
sin firmas buscadas de error ni minimización registrada. No es una salida
natural del juego ni un control de FPS.

128 retornos completos, ambas variantes, cinco grupos y ninguna pausa.
6.150 lecturas válidas de destino/mundo/fuente finitas; 5.606 productos finales
seleccionados y cambiados, residuo máximo descriptivo 0,0001419. El grupo
candidato de 111 slots aporta 32 muestras en videoTick 1500..1531 y 3.296
productos, residuo máximo 0,000005588. Ahora la sonda sí alcanza la misma
ventana de contador que las imágenes; no se demuestra todavía la identidad
del skeleton ni una correspondencia uno a uno entre su cálculo y cada textura.

16 PNG/JSON adquiridos, secuencias 1456..1471, frameTick 1500..1515, metadatos
exactos contra su registro y RGBA completos distintos. Cada imagen duplica
los 224 pares adyacentes de filas. Se revisan posiciones inicial, intermedia
y final: Kratos en cubierta pasa del brazo/hoja elevado a una posición baja;
la inicial se parece al ejemplo que el usuario identifica como espasmo.
No se afirma equivalencia de píxeles con su captura ni pose correcta PS2.

El producto final coherente acota esa operación observada, pero no certifica
la animación de entrada, la jerarquía previa ni el envío posterior VIF/VU/GS.
Una comprobación privada exploratoria de mundo padre por fuente local da
diferencias en algunos slots. **No es todavía un fallo demostrado**: el
record tiene flags `0x6000` que pueden transformar el resultado, y selección,
umbral y reutilización deciden qué se actualiza. Registrar esos flags y el
cambio de mundo antes de evaluar la jerarquía; no corregir matrices por ese
producto incompleto. La referencia Persona 4 ayuda a revisar semántica, sin
fuente externa incorporada. Encuadre y espasmos siguen abiertos.

La compilación rápida, controles de paquete y ventana, pruebas procedurales,
revisión manual y [CI de `7df7894` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38181222772)
validan el alcance de #63: soporte de guion y diagnóstico opcional. El control
desde 3000 sin muestras se conserva como limitación; no se presenta como
movimiento visible validado. Comprobar también CI del HEAD documental final
antes de fusionar. No hay cambios de parches o matemática del juego.

## Condiciones de la jerarquía: diagnóstico preparado (2026-10-11)

La comprobación exploratoria anterior motiva registrar flags del record y
cambio de la matriz mundo. `recordFlags` conserva todos los bits de record+0;
`worldBeforeValid`/`worldChanged` separan lecturas inválidas de bytes retenidos,
sin afirmar ejecución por slot. Se añade lectura previa de mundo a la sonda
opcional, sin modificar cálculo, argumentos, buffers, checkpoints o parches.
La prueba procedural distingue flags/índice/umbral y rechaza records truncados;
Windows/MSVC /W4 /WX y sintaxis C++20 del override correctos. Pendientes
recompilación rápida, control y CI del nuevo código. No se declara que las
diferencias exploratorias sean corrupción.

## Condiciones de jerarquía observadas (2026-10-11)

Src/ de `e73e844` se recompila sobre 101 completa con código 0 en 19,56 s,
sin cambios de parches/runtime.lib. Runner 54.562.304 bytes, SHA-256
`29a43df0d67b092b4ab67a02adf6bb24d39244d1a87b2ada1a15d7402982314e`.
[CI del código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38181839302).
#63 ya fusionada en `bf6768b4e6af6fa834d5f555c92131c85c35fa2c`, tras revisión
y [CI del HEAD final `4857fdb` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38181681700).
Se integra ese main mediante merge; #64 continúa la observación.

Control síncrono/compartido, hilos 0, guion de menú anterior, hash 0 y ventana
1500: 300,05 s por límite; tarjeta procedural intacta, sin firmas buscadas de
error o minimización registrada. 128 retornos completos de ambas variantes,
cinco grupos, ninguna pausa; 6.173 lecturas válidas finitas y 5.629 productos
finales, residuo máximo descriptivo 0,0001494. El grupo de 111 slots vuelve a
aportar 32 muestras en 1500..1531 y 3.296 productos finales, máximo 0,000006007.

En ese grupo, los cuatro slots 107..110 conservan los bytes de mundo y de
destino, no están seleccionados; 107/109 tienen visibilidad 0. Son los slots
que daban las diferencias grandes al aplicar indiscriminadamente el producto
padre por fuente. No tienen flags `0x6000` en estas muestras. Excluidos los
bytes retenidos y las transformaciones especiales, se evalúan 3.392 productos
de jerarquía cambiados: máximo 0,000004453. **Son magnitudes de esta muestra,
no tolerancias PS2 ni prueba de animación correcta**; tampoco ausencia de
cambio demuestra que no se escribiera. No se justifica corregir aquel
producto incompleto como si revelara corrupción.

16 PNG/JSON adquiridos, secuencias 1458..1473/ticks 1500..1515, metadatos
exactos y RGBA distintos, 224 pares de filas duplicados. Las imágenes 1/8/16
revisadas muestran el brazo/hoja ya en posición baja: no reproducen la pose
elevada exacta de la pasada anterior. Incluso compartiendo ventana de vídeo,
las dos ejecuciones no están alineadas por estado de animación. No usar este
control para descartar el espasmo anterior.

Como comprobación de identidad de recurso, las 103 matrices de definición
de la muestra anterior coinciden en sus 6.592 bytes con un bloque del estado
PCSX2 histórico .02, que sigue en estado 4. Se localiza una definición y
skeleton compatibles. Esto relaciona los datos estáticos, **no alinea poses,
confirma identidad de personaje ni convierte esa intro en referencia de
partida**. Todos esos datos y análisis siguen privados. El siguiente control
debe seguir la geometría recibida por GS durante la postura señalada y separar
esa entrada del rasterizado/presentación; la animación fuente sigue abierta.

## Traza fija antes de la postura y ventana de vídeo preparada (2026-10-11)

#64 fusionada en `76f60218eabe2584ffe454de7bd986d349f49ff8`, tras revisión,
control anterior y [CI del HEAD final `17cd878` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38182260148).
Una nueva pasada del mismo runner `e73e844` usa FINISH síncrono/presentación
RAM/GPU/hilos 0, sin sonda de skinning o hash, y el mismo guion de menú.
Captura GS tras TEXFLUSH desde 175 s: inicio 175,023 s, End completo,
54.535.097 bytes, 145.442 Submit y siete Present en ticks 1434..1440.
Se cierra con STOP privado a 188,28 s después de terminar la traza, para
liberar CPU antes de repetirla; tarjeta intacta, sin firmas buscadas de error
ni minimización. No es un control de cinco minutos completado ni salida natural.

Se repite el prefijo hasta **su segundo Present**, registro 22360, 22.207
Submit/tick 1435; no se reutiliza el índice 12795 de la traza antigua. Dos
pasadas CPU: VRAM de 4 MiB, estado y dos frames iguales. Hardware, tres
pasadas: la primera mezcla tiles compute y no cuenta como comparación de
ruta constante. La segunda/tercera tienen 17.362 primitivas y cero tiles,
pero difieren en 1.081 bytes de VRAM y 1.288 bytes RGBA del segundo frame;
estado semántico igual. Código 1 por diferencia, **no resultado correcto**.
El End original y resto de comandos no se comprueban con este prefijo.
La revisión de imágenes muestra Kratos aún más distante en cubierta; no
es la postura exacta señalada, ni demuestra que esta variación cause espasmos.

El plazo en segundos del host eligió un tramo anterior al 1500 buscado.
Se añade `GOW_GS_REPLAY_FROM_TICK`, umbral opcional del contador de vídeo del
runtime, junto a estado/plazo/TEXFLUSH ya existentes. Ausente equivale a 0;
un valor decimal inválido desactiva la captura sin aplicar opciones parciales.
Antes del umbral se siguen enviando comandos al backend, sin comenzar snapshot
o registro; Present no abre captura por anunciar un tick diferente. Mantiene
el formato de traza, límites, backend y reglas de sincronización anteriores.
Ese umbral **no alinea poses entre ejecuciones o con PCSX2**.

Control procedural Windows correcto: comandos previos llegan a CPU sin
grabarse, inicio en TEXFLUSH al alcanzar 1500, snapshot preservado, una sola
primitiva posterior registrada, End válido; parser de uint64 y opciones
inválidas sin cambios parciales. Replay CPU del fixture y layout de píxeles
correctos. Pendientes recompilación de src/, control de ventana con el juego
y CI del nuevo código. No se modifica ningún parche o matemática del juego.

## Ventana de la introducción y reproducción de la pose (2026-10-11)

El usuario precisa que el ejemplo de espasmo corresponde a la introducción
del barco. Se centra esta comprobación en ese tramo; los avisos anteriores
sobre NPCs y los controles con HUD siguen siendo casos separados.

Src/ de `d5ab720` recompilado sobre la cadena completa 101: código 0 en
19,47 s, runtime.lib y parches intactos. Runner 54.562.816 bytes, SHA-256
`6c7a3119476351602683e81f8ad0a890059b83c4cf1f0d068c3f83c9e0b7f436`.
[CI del código correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38182940840).
Negativo procedural en copia privada: omitir solo el umbral compila y hace
fallar la prueba de ventana; restaurarlo pasa. No se altera el header público.

Control FINISH síncrono/RAM/GPU/hilos 0, mismo guion de menú hasta 68 s,
sin hash o diagnóstico de presentación: 300,06 s, cierre por límite del
control, tarjeta procedural intacta, sin firmas buscadas de error o
minimización registrada. Captura desde tick 1500, estado 11 y TEXFLUSH:
inicio 177,069 s/tick 1500, End completo, 54.535.181 bytes, 145.436 Submit y
ocho Present en 1500..1507. El formato y límites anteriores se conservan.

Se repite hasta **el segundo Present de esta traza**, registro 22797,
22.619 Submit/tick 1501. Dos pasadas CPU: VRAM de 4 MiB, estado y dos frames
idénticos. Hardware, tres pasadas: primera con ruta mixta, excluida de la
comparación constante; segunda/tercera con 17.605 primitivas y cero tiles,
estado semántico igual, pero 1.408 bytes de VRAM y 1.512 bytes RGBA del segundo
frame distintos. Código 1 por diferencia: no paridad ni estabilidad validada.
El prefijo no comprueba el End original ni el resto de la traza.

Se revisa **el mismo segundo Present** exportado por CPU y hardware: ambos
muestran la pose de brazo elevado y hoja cruzando la cabeza, muy semejante a
la señalada por el usuario. Es evidencia para seguir la geometría de entrada
al GS y su producción EE/VU; no una equivalencia exacta con la captura del
usuario, una referencia PAL/PCSX2 alineada, ni una prueba de que la variación
de raster cause el espasmo. Tampoco demuestra animación correcta una imagen
fija. Los PPM y su conversión PNG con RGB intacto siguen privados.

La sonda completa 128 retornos, cinco grupos, ambas variantes, ninguna pausa:
6.173 lecturas válidas finitas y 5.629 productos finales, residuo descriptivo
máximo 0,0001441. El candidato de 111 slots aporta 32 muestras en 1490..1520,
3.296 productos finales y máximo 0,000005563. Sus bases fuente seleccionadas
tienen determinantes 0,6558639..1,0000008 y longitudes de eje desde 0,8098543;
sin determinantes cero/negativos en esa muestra. Son magnitudes observadas,
no tolerancias de fidelidad: escala o mezcla de animación pueden producir
bases no unitarias. No normalizar ni corregir matrices sin identificar su
productor y contrastar una pose de referencia. La causa del espasmo sigue abierta.

## Pista de PMULTH del port de Waffle565 (2026-10-11)

El usuario aporta [God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port),
revisado en `811313597514940a4d6020fefc1ce5b83f5b4fe4`. Su
[entrada de corrección `50b591d`](https://github.com/Waffle565/God-Of-War-Native-Port/commit/50b591d2bc679cc705b55764588716410df10d92)
identifica PMULTH mal traducida en decodificadores de animación; relata
comparación diferencial y mejora visual en su port. Son comprobaciones de
ese colaborador, no una reproducción independiente nuestra. El commit público
documenta el arreglo, pero no incluye su implementación del runtime.

Se comprueba independientemente en nuestro código regenerado NTSC que cuatro
PMULTH, en tres decodificadores, siguen emitiendo `_mm_madd_epi16` y sumando
sus resultados en un escalar. Es un fallo de semántica: PMULTH produce ocho
productos signed de 16 por 16 bits, LO={p0,p1,p4,p5}, HI={p2,p3,p6,p7} y
rd={p0,p2,p4,p6}. Se contrasta el orden con
[PCSX2 MMI.cpp fijado](https://github.com/PCSX2/pcsx2/blob/2954ebe6965669219f1bbd256e958fbd8f83a85d/pcsx2/MMI.cpp#L1292).

Se prepara `ps2recomp-ee-pmulth.patch`, tema separado al final de la cadena
(102), con implementación propia de esa semántica en el header del runtime
y emisión por el recompilador. Conserva los ocho resultados en ambos pares
HI/LO, el registro cero y los operandos cuando rd coincide con rs/rt. No cambia
la estructura del contexto, direcciones o funciones del juego. Regresiones con
valores distintos por lane, negativos/extremos, ambos pares HI/LO, alias y cero;
la prueba de emisión cubre rd=0 con sus efectos secundarios. No se incorpora
código generado o del otro port, ni se modifica el checkout de Opus.

Pendientes compilación completa que regenere las cuatro instrucciones,
suite/negativo, CI y control de la introducción con el nuevo ejecutable. La
evidencia confirma el error de instrucción, **todavía no que corrija todos los
espasmos, el encuadre o la presentación asíncrona/compartida**.

## Cierre y controles pendientes de PMULTH (2026-10-11)

#65 fusionada en `dbfbe719eea0483b768aa4eef3894db1221cf28c`, tras revisión,
control y [CI del HEAD final `396f594` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38183579985).
La [PR #67](https://github.com/KIexster/god-of-war-recomp/pull/67) contiene
PMULTH y permanece en borrador. La compilación completa privada de `2fd0fc9`
ya regeneró los cuatro sitios con el helper correcto y sigue compilando el
runner. Todavía no hay ejecutable final ni control visual de esta corrección.

El primer CI de #67 falla compilando las regresiones: `GPR_VEC(&ctx, ...)`
expande el puntero sin paréntesis. Se corrige el argumento de esas pruebas a
`GPR_VEC((&ctx), ...)`; no cambia el helper, la emisión o la lógica del juego.
Pendiente CI del nuevo HEAD, suite nativa y negativo significativo. No tratar
el primer CI fallido como validación del runtime.

También se revisa la [issue #66](https://github.com/KIexster/god-of-war-recomp/issues/66),
aportada por otro colaborador: separa casos medidos en Transformers de sus
hipótesis para God of War. En nuestro runtime se confirman los presupuestos
65.536/4.096 de VU1/VU0 y que el constructor de contexto deja vf0 a cero;
StartThread crea ese contexto. **No se ha demostrado que causen este espasmo**.
El próximo control activará `GOW_VU1_BUDGET_DIAG=1`, sin aumentar presupuestos,
y examinará magnitudes además de finitud. Primero comprobar PMULTH; las demás
hipótesis requieren evidencia por separado y no se incorporan arreglos en bloque.

## PMULTH: compilación completa y regresiones nativas (2026-10-11)

La cadena 102 termina con código 0 en 959,94 s, regenerando los cuatro sitios
PMULTH con `ps2_pmulth`. Código funcional de `2fd0fc9`, 54.565.376 bytes,
runner SHA-256 `3092e98bc53ff8e1109a71ee596d7bfa0a15263ac6a6f1a0bfc3fc91ee8cfc4f`.
Se conserva esa identidad de compilación: el HEAD posterior corrige argumentos
de tests, documenta el cierre e integra main, sin cambiar helper/emisión.
VU1 interpretada, tres tareas, /O2 /Ob2 /DNDEBUG /Zi; datos y símbolos privados.
No atribuir a esta build los FPS medidos con VU1 compilada.

Regresiones finales de la PR aplicadas al árbol de pruebas nativo: 602 casos
normales y 638 GPU, cero fallos; cuatro contratos FINISH/presentación correctos.
Negativo aislado con las aserciones procedurales públicas: el helper correcto
pasa, reemplazarlo por la suma escalar anterior **compila y falla**, restaurarlo
pasa. Header del runtime de trabajo intacto. No es un control del juego.
[CI final `94cacf8` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38184150710).

Se inicia control privado de la introducción: FINISH síncrono/compartido,
hilos 0, mismo guion de menú, ventana de matrices 1490 y 16 adquisiciones desde
1500, hash 0; diagnóstico de cortes VU1 activado sin aumentar el presupuesto.
Pendiente terminar, revisar poses/registro y publicar el resultado antes de
marcar #67 lista. No se ha declarado el espasmo resuelto.

## PMULTH: primer control visual posterior (2026-10-11)

La pasada anterior termina a 300,06 s por límite; tarjeta procedural intacta,
sin firmas buscadas de error ni minimización. Se registra foco durante unos
dos segundos, sin probar su causa. Con `GOW_VU1_BUDGET_DIAG=1` no aparecen
cortes de presupuesto en esta configuración síncrona/compartida, hilos 0;
esto no descarta cortes en otros tramos o con hilos/rendimiento activados.

128 retornos completos, cinco grupos, ambas variantes, ninguna pausa;
6.173 lecturas válidas finitas y 5.629 productos finales, residuo descriptivo
máximo 0,0001364. Candidato de 111 slots: 32 muestras 1490..1520, 3.296 productos
y máximo 0,000005823. Conserva bases fuente no unitarias (determinante mínimo
0,6596880, eje mínimo 0,8122119), sin ceros/negativos en esta muestra: tampoco
ahora justifican normalizar matrices por intuición.

16 texturas adquiridas 512x448, secuencias 1455..1470/ticks 1500..1515, metadatos
exactos y RGBA diferentes; 224 pares de filas duplicadas por imagen. Las
imágenes 1/8/16 revisadas muestran cabeza erguida y hojas delante del cuerpo,
sin la pose elevada del ejemplo. Es un resultado favorable de esta ventana,
**no una validación completa de animaciones/NPCs, cámara o estabilidad**.

Comparación descriptiva con la ventana de la traza anterior: los 32 stamps
coinciden, pero ninguna matriz de entrada ni scalar coincide exactamente.
No son poses alineadas. El mayor paso fuente del slot 14 pasa de 1,13344 a
0,0007890 y el mayor paso de posición observado en el grupo pasa de 18,71 a
0,84; no se usan como tolerancia PS2, porcentaje de mejora o prueba causal
automática. PMULTH sí queda corregida por su semántica y regresiones. Falta
ampliar el control a otras animaciones y contrastar referencia alineada.

## GS de PCSX2: comienzo de adaptación propia (2026-10-11)

#67 fusionada en `96853bad688305c1d62d9dad6faeb1f1e58e1de4` tras controles,
revisión y [CI del HEAD final `3ba1000` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38196063455).
Se integra main sin reescribir historia. El usuario pide adaptar por nuestra
cuenta el GS de PCSX2 mientras el autor del port de Urban Reign entrega su enlace.
No se dispone aún de su código ni se reproducen sus cifras de FPS/resolución.

Se prepara transporte propio de GIF para los tres PATH, con fragmentos,
quadwords alineadas, límites y errores; conexión a `GSState::Transfer` de
PCSX2 `2954ebe`. Pruebas procedurales Windows y sintaxis con sus cabeceras
reales correctas; CI añadida para ambas. No cambia runtime/parches o backend
predeterminado. Todavía no crea un GS real ni dibuja el juego: extracción del
módulo DX11, registros, FIFO/VRAM y cobertura de caminos nativos pendientes.
Arquitectura, condiciones y límites en [PCSX2_GS.md](PCSX2_GS.md).

Revisión posterior: la primera conexión compilaba pero PATH1 usaba el índice
0 legado. GSState corta ese índice en EOP y borra nloop incompleto al terminar
el span, incompatible con nuestros lotes/fragmentos. Se corrige al índice
continuo 3 usado por GSgifTransfer/MTGS; los otros dos mantienen sus cursores.
Sintaxis correcta no prueba ese comportamiento. GSState.cpp real compila
como objeto privado en MSVC; aún falta enlazar/ejecutar el módulo. No se había
conectado el puente al juego ni modificado su renderer.

## GS de PCSX2: parser original probado (2026-10-12)

La extracción privada de GS/common ya enlaza y ejecuta GSState original sin
ventana/dispositivo. Se publica el fixture procedural, sin datos del juego:
retención de PATH1 al separar tag/payload, dos EOP por lote, fragmentos PATH2/3
y cursores independientes, RGBAQ exacto. La subclase cuenta dibujo sin
rasterizar; la prueba exige cero dibujos y cero solicitudes de ventana.
No es un renderer de sustitución. Bibliotecas/servicios originales y límites
de reproducción del host privado detallados en [PCSX2_GS.md](PCSX2_GS.md).

Negativo de índice PATH1 0: compila y falla en el nloop pendiente; índice 3
restaurado correcto. SHA-256 del ejecutable procedural restaurado
`1289d908c425c0f1b138ff7dc4cf619f1a9341c443fe6145eae5c02d9b7e033c`.
El CI anterior `d4aab8e` completo correcto, incluido enlace sintáctico de
headers reales; pendiente CI del HEAD que añade fixture/resultados.
Todavía falta alojar DX11, píxeles, FIFO/VRAM/IRQ y conectar el runtime.

## GS de PCSX2: dispositivo DX11 con píxeles comprobados (2026-10-12)

La extracción privada ya crea GSDevice11 original en modo sin ventana sobre
la AMD Radeon RX 5700 XT. Se publica `tests/pcsx2_dx11_device_probe.cpp`:
dos ciclos de creación/cierre; Clear y patrón subido con padding por fila,
dibujado por shader COPY original. Los 2.701 píxeles RGBA de cada operación
coinciden exactamente, incluida alpha y orientación. Negativo aislado que
omite el dibujo: compila y falla en el primer píxel; restaurado pasa.
SHA-256 restaurado `2be054f554e71ebdafc234998b84d4e7b53542f26a40c2f58403fcbead66db14`.

[CI completo de `6139073` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38200847766).
El nuevo control DX11 fue nativo privado, no una ejecución GPU de CI. Host,
dependencias, alcance y pendientes en [PCSX2_GS.md](PCSX2_GS.md). Falta enviar
GIF al rasterizador/cache completos, VRAM/FIFO/IRQ y conectar el juego.
No cambia el renderer actual ni acredita una mejora de FPS.

## GS de PCSX2: primer dibujo GIF en hardware comprobado (2026-10-12)

La extracción privada de GSRenderer/GSRendererHW/GSTextureCache originales
enlaza y dibuja en DX11. Fixture publicado `tests/pcsx2_gif_draw_probe.cpp`:
mosaico de 16 sprites, fragmentos de 13 bytes, colores distintos por PATH1/2/3
y descarga FIFO original. Los tres envíos producen un draw call GPU cada uno
y 2.048 píxeles RGBA exactos. Negativo que omite solo el paquete de dibujo:
compila y falla en el primer píxel; restaurado pasa los tres PATH. SHA-256
`1d271fd05fcbf01cec0b688655c26337b99259a2545274ecccda423740ef01fb`.

[CI completo de `72e4f55` correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38238566117).
Este control GPU sigue siendo nativo privado. Servicios opcionales ausentes
abortan si se usan; no se solicita VSync, ventana o reapertura. Los objetos
SW se conservan, sin validación de esa ruta. Límites en [PCSX2_GS.md](PCSX2_GS.md).
Faltan reproducción pública del módulo, IMAGE/texturas, presentación y conexión
al runtime. No se atribuye renderizado de God of War ni una mejora de FPS.

## GS de PCSX2: módulo reproducible, IMAGE y cierre de memoria (2026-10-12)

Se publica host/CMake/preparación del módulo aislado y se añade CI Windows:
enlace de cuatro fixtures, parser y memoria ejecutados sin GPU. Compilación
local de la receta y cuatro controles correctos. Se verifica además IMAGE
CT32, textura DECAL/nearest y FIFO completo/parcial: 2.048 + 91 píxeles RGBA
exactos, dibujo GPU confirmado. Omitir IMAGE compila y falla; restaurado
correcto. SHA-256 del fixture de dibujo
`a7991d25961bdddddd8ce3631cf73287f057b8928c735df8724c818e575f3444`.

La extracción Windows conservaba un handle y placeholders al cerrar. Se
corrige solo el host aislado, con comentarios GOW-Port y regresión: ocho
ciclos alias de 4x4 MiB, handles estables y todo el rango libre. Negativos
separados para handle y unión de placeholders compilan y fallan; restaurado
correcto. Sin validación del camino de fallo de reserva. Las fuentes originales
y el runtime del juego permanecen intactos.

La receta local ejecuta los cuatro fixtures sin DLL DXC; no acredita otras
APIs o rendimiento. [Instrucciones](../tools/render/pcsx2_probe/README.md),
límites y siguientes pasos en [PCSX2_GS.md](PCSX2_GS.md). El CI de `4addf7b`
completo correcto; el nuevo job nativo está pendiente del HEAD que lo publica.

## GS de PCSX2: salida VSync/PCRTC procedural (2026-10-12)

`gs_gif_draw_probe --frame` ejecuta GSvsync original, su actualización CRT/Flush
y Merge DX11. Tres patrones distintos, tres salidas 64x32: los 2.048 píxeles
RGBA de cada una coinciden exactamente con RGB de entrada y alpha 255 impuesta
por PMODE. Surfaceless omite el Present de ventana y el host comprueba sus
callbacks. Negativo al omitir solo GSvsync compila y falla; restaurado pasa.
SHA-256 `cd46ebb2f58aa6bd80c5849e30d7ee5fc0659c644db030f65c8fed1c6701cffb`.

DISPLAY se configura conforme a las unidades NTSC del cuerpo original. El
primer desfase de 25 líneas pertenecía al fixture con DY 0, no a una ejecución
de God of War. No cambia el renderer del juego ni resuelve por sí solo su
encuadre. [CI de `08307ac` completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38243100981),
incluido enlace Windows; esta nueva prueba GPU es local. Ventana/swapchain e
integración del runtime siguen pendientes; no hay medición de FPS.

## GS de PCSX2: backbuffer DXGI y dos ciclos de ventana (2026-10-12)

El modo `--window` usa una ventana procedural propia oculta y Present original.
Dos ciclos completos de dispositivo/renderer/ventana, tres patrones por ciclo:
los seis backbuffers reales leídos antes de Present y las seis texturas PCRTC
coinciden exactamente en sus 2.048 píxeles RGBA. Contexto ImGui vacío, sin
archivos de interfaz; callbacks y cierre de HWND/clase comprobados.

Negativo al borrar el backbuffer a magenta tras dibujar: compila y falla aun
con salida PCRTC correcta; restaurado pasa ventana y Surfaceless. SHA-256
`74e92ae9a886b66f55079d06f6cbf0ddcbf9af18bfe2064e7d92137f94f3c543`.
[CI de `d1bf750` completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38244581430).
El nuevo control GPU sigue siendo local. No se ejecuta God of War, no se
comprueba imagen visible, resize/pérdida de dispositivo ni estabilidad del
FINISH asíncrono del runtime. No se mide FPS. API/hook de integración pendientes.

## GS de PCSX2: IMAGE FLG=3 y cobertura del futuro hook (2026-10-12)

Se comprueban IMAGE FLG=2/3 con el parser/rasterizador original, sin alterar
los tags en el puente. FIFO, salida PCRTC y backbuffer de ventana exactos.
Negativo que omite solo FLG=3 compila y devuelve el patrón anterior; la prueba
falla. Restaurados, los tres modos pasan. SHA-256
`77ecd2fa20509b18422d9ade62c8ee35db18f90a0a8ad19e6882be412d311bfd`.
Conserva la semántica relevante para #55, sin atribuir FPS o un fallo del juego.

Revisión confirma dos atajos DMA que evitan el árbitro/frontend genérico.
La subida nativa normaliza datos/registros y pierde parte de los tags originales;
el futuro hook no puede limitarse a processGIFPacket. Propuesta y pendientes
documentados en [PCSX2_GS.md](PCSX2_GS.md), sin modificar el runtime por ahora.
[CI de `ada5c5e` completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38246271325).

## GS de PCSX2: ABI de DLL y consumidor independiente (2026-10-12)

Se construye una DLL opcional con ABI 1 (tamaños/handles/errores/hilo). Contrato
de carga/exports/argumentos sin dispositivo correcto, añadido a CTest/CI. Cliente
GPU mediante GetProcAddress, sin globals del GS: dos ciclos, GIF fragmentado,
registros y consulta de salida correctos; 2.048 píxeles RGBA exactos por ciclo.
Singleton ocupado, handle cerrado, otro hilo y buffers insuficientes rechazados;
guardia final intacta. Negativos de hilo/capacidad compilan y fallan; restaurados
correctos. No se modifica aún PS2Recomp ni se enlaza el juego. El espejo de 8 KiB
debe construirse desde registros PS2 reales; no copiar su struct GSRegisters.

[CI de `1f87036` completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38248274550).
Nuevo contrato DLL pendiente del HEAD que lo publica. GPU local, sin medición
de FPS; IRQ/FINISH, FIFO/VRAM, recuperación y hook pendientes. Alcance en
[PCSX2_GS.md](PCSX2_GS.md) y [README del módulo](../tools/render/pcsx2_probe/README.md).

## GS de PCSX2: espejo privilegiado explícito (2026-10-12)

Helper sin activar en el juego: empaqueta 19 registros a offsets PS2, en
little-endian, huecos cero y CSR atómico; omite vsyncTick del struct compacto.
Prueba de valores distintos/CSR multibyte y cambios de tick correcta en MSVC.
Los 19 offsets y 8 KiB se contrastan con GSPrivRegSet original mediante
static_assert. CI/CTest incorporados. El llamante debe sincronizar los campos
no atómicos; el helper no corrige por sí solo acceso concurrente o presentación.

## GS de PCSX2: enlace espejo opcional preparado (2026-10-12)

Parche 103: observador de GIF original, escrituras directas/reset y VSync del
EE. Se rechazan los dos atajos DMA y PACKED mientras está ligado, sin consumir
su entrada; comportamiento habitual conservado al desenlazar. Cliente Windows
por DLL, hilo dueño y cola acotada; captura PCRTC BMP privada limitada. Rechaza
hilos VU1/frente GS. IRQ/FIFO/VRAM siguen en runtime; backend habitual intacto.

Compilación del módulo/cliente Windows correcta. Parser conserva cursor y Q
con registro directo; DLL GPU dos ciclos/2.048 RGBA exactos, reset y rechazo
de dirección inválida correctos. Config 0 errores/4 avisos existentes; 24 PS1
sin errores. La suite del nuevo runtime y la compilación completa de 103 aún
pendientes. Variables/protocolo y límites en [PCSX2_GS.md](PCSX2_GS.md).

## Pausa solicitada tras compilación completa 103 (2026-10-12)

HEAD de código `477e01372d3f4a58c460b126b8e459873e9b9286`: los cuatro trabajos
de CI correctos en run `38298178064`. Compilación Windows completa mediante
`2_compilar.cmd` en workspace privado, 103 parches, salida 0, 1.072,57 s.
Runner SHA-256 `e8e18d005dce8674de8df92be4522ec8f038b6a6aeedc3769915c5679988a471`.
El ejecutable y su manifiesto quedan congelados; no se ejecuta aún el juego.

Se publica preparación del control completo runtime/cola/DLL, con dos patrones
distintos separados por reset, y opciones del control de transición que registran
hash de DLL y capturas PCRTC privadas. Las 19 pruebas Python del control pasan.
El fixture nativo completo aún no se ha compilado/ejecutado. Sigue pendiente
corregir la lectura Unicode del entorno Windows en el cliente: el controlador
puede resolver la junction a una ruta con acentos y `u8path(getenv(...))` no
garantiza interpretar correctamente los bytes ACP. No se activa el espejo en
una partida antes de corregir y validar ese camino.

Pausa expresa del usuario al terminar la compilación. Pendientes al retomar:
Unicode y regresión con carpeta no ASCII, recompilación de src/ sobre esta
compilación completa, suites nativas/contratos, control procedural runtime/DLL
y después menú/introducción/partida. PR #68 permanece en borrador; no se afirma
mejora de FPS ni resolución de encuadre, espasmos o FINISH asíncrono.

## GS de PCSX2: recorrido runtime/DLL y rutas Unicode verificados (2026-10-12)

Se retoma por petición del usuario. El control nativo del enlace reproduce el
rechazo de configuración con la DLL en una ruta con acentos y una carpeta
`capturas_ñ`: los bytes ACP de `getenv` no son UTF-8 válido para `u8path`.
Se leen esas rutas con `_wgetenv`/UTF-16 y se convierten únicamente los recursos
a UTF-8 para el ABI. El negativo anterior compila y termina con código 1;
con la corrección termina con 0, sin cambiar el runtime ni el módulo DX11.

Control completo con el runtime 103: 28 spans GIF/352 bytes, dos escrituras
directas durante tags incompletos, dos VSync y un reset; ambas imágenes de
64x32 tienen sus 2.048 RGBA exactos, con colores distintos antes/después del
reset. Cola drenada y DLL cerrada con `failed=0`; carpeta no ASCII correcta.
DLL SHA-256 `cbbc2d060df61e479355479fc3ce5b054e5ce27302bf19d5accda735d21ecd62`.
Todavía no es un control del juego o de FPS. La actualización de src/ del
runner sobre la compilación completa 103 y las suites nativas están pendientes.

Las suites nativas de esa compilación completa terminan correctamente:
605/605 normal y 641/641 con los controles OpenGL GPU activados, sin fallos
conocidos. Los cuatro contratos FINISH normal/asíncrono, incluida presentación
compartida real y comparación CPU de 16 texturas, pasan. Estos patrones no
sustituyen la transición con el juego ni validan todas las combinaciones de hilos.

## GS de PCSX2: primera transición y revisión de Tobiichi (2026-10-12)

Src/ `e4cf8c14732771112bb87054b53edaae66ea6f35` actualizado sobre runtime
completo `477e013`/103; runner SHA-256
`089b1cea9cd14bb3cf3e2fa387b3a6f7f303801dfbd24b67b3be3b2fa8916ca5`.
CI de e4cf8c1 completo correcto. Negativo privado que omite escrituras directas
y permite atajos con observador: compila y provoca cuatro fallos. Fuentes
restauradas byte por byte, 605 normales y 641 GPU correctas de nuevo.

Primer control del juego, FINISH síncrono/presentación compartida, sin hilos
VU1/frente GS: 360,08 s hasta el límite, sin salida anticipada. Tarjeta privada
copiada, mismo ELF verificado, guion sin capturas periódicas. Espejo DX11
procesa GIF/VSync del juego y guarda 10 imágenes PCRTC 512x448, ticks 900..1980,
además de ocho texturas OpenGL adquiridas 1400..1407. Se revisan muestras de
la introducción y enemigos; no se certifican poses/animación o paridad con PS2.
Estado 11 incluye la introducción: no demuestra control jugable/HUD.

Inspector compartido: 610 adquisiciones desde tick 1400, sin regresiones,
metadatos inconsistentes, cambios de dimensiones o retornos A-B-A registrados.
Cubre esa muestra; no demuestra ausencia de retenciones ni resuelve todos
los fallos. Tick EE del espejo y tick host/adquisición no alinean píxeles por
sí solos. Salida 1 corresponde a la terminación del proceso propio al límite;
no se verifica cierre normal de la partida. No mide FPS.

Se revisan cambios recientes de Tobiichi-Port por petición del usuario:
[comparación, candidatos y límites](REVISION_TOBIICHI.md). No se copian el
buzón global o el control de fences sin sus regresiones. Control asíncrono
compartido equivalente en curso; no se cambia la compilación durante la partida.

Control FINISH asíncrono/compartido sobre el mismo runner e4cf8c1: 360,06 s,
final al límite sin salida anticipada, 12 capturas PCRTC 900..2220 y ocho
texturas adquiridas. Se observa HUD, Kratos y enemigos en la salida DX11 del
tick 2100; no se afirma fidelidad de animación o control manual por esa imagen.
Inspector: 918 adquisiciones, sin regresiones, metadatos inconsistentes,
cambios de dimensiones o A-B-A registrados. Mismos ELF/tarjeta inicial y
opciones de aislamiento. Ambos controles del espejo usan rutas DMA genéricas:
no extrapolar estabilidad a los atajos habituales, los hilos o una partida larga.

Se prepara captura por intervalo configurable para estudiar poses consecutivas:
`GOW_PCSX2_GS_CAPTURE_EVERY_TICKS=1..600`, predeterminado 120, cupo máximo 16
conservado. Controlador actualizado y 19 pruebas Python correctas. Fixture
runtime/DLL captura dos colores exactos en ticks 1 y 2 con reset; negativo que
ignora el intervalo y fuerza 120 compila/falla, restaurado correcto. Cambio
solo de src/ y control, sin nuevo parche del runtime. Recompilación del runner
y ráfaga con el juego pendientes; el ejecutable e4cf8c1 no contiene esta opción.

## GS de PCSX2: primeras ráfagas comparadas dentro de una ejecución (2026-10-12)

Src/ `e5e040e5bd4ea56d13499bd827f9276fa4400ce7` recompilado sobre la misma
cadena completa 103 de `477e013`, sin cambiar la biblioteca del runtime.
Runner SHA-256 `4af9bd9bca230265db2bb4c9f8a3122bad0643e83a95c477d11eeae5c4641d09`;
VU1 interpretada, hilos VU1/frente GS apagados. [CI del HEAD e5e040e correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38314554618).
Mismos ELF, ISO, guion y copia de tarjeta inicial; espejo DX11 desde arranque,
16 capturas PCRTC consecutivas desde tick 1400, sin capturas periódicas del mando.

Dos controles terminados: síncrono/RAM, 420,08 s, y asíncrono/compartido,
420,07 s. Ambos alcanzan el límite, sin salida anticipada ni fallo registrado
del espejo; terminación forzada del proceso propio, no cierre normal del juego.
La tarjeta copiada conserva su hash. El asíncrono registra 1.142 adquisiciones
compartidas sin regresiones, metadatos inconsistentes, hashes desconocidos,
cambios de dimensiones o A-B-A en esa muestra. Cero filas retenidas no prueba
ausencia de retenciones: el diagnóstico solo registra adquisiciones nuevas.

Se revisan las 16 imágenes PCRTC del síncrono/RAM y los 16 pares PCRTC/textura
adquirida del asíncrono/compartido, 512x448, ticks 1400..1415. En esos pares
de una sola ejecución se observa encuadre y silueta de Kratos semejantes, con
movimiento gradual, sin el salto grande o la deformación brusca buscados en
esta ventana. OpenGL muestra bordes más dentados y diferencias entre líneas.
Es una observación visual: no certifica igualdad de píxeles, fidelidad PS2,
poses de todas las animaciones/NPCs ni la causa de esas diferencias.

Entre el control RAM y las pasadas compartidas anteriores, un mismo tick
corresponde a momentos distintos de la introducción. No atribuir al renderer
una diferencia de cámara o postura obtenida así. Incluso dentro de la misma
ejecución, tick EE y tick de adquisición no garantizan idéntico prefijo GIF;
el contraste visual no sustituye una referencia alineada por estado/comandos.
Síncrono/compartido y asíncrono/RAM equivalentes siguen en curso para completar
la matriz con este mismo ejecutable. Diagnósticos, hash, dos renderers y readbacks
impiden usar estos controles para comparar FPS con las mediciones de Opus.
El espejo fuerza rutas DMA genéricas; estabilidad de atajos e hilos pendiente.

## GS de PCSX2: convención de campo revisada con Urban Reign (2026-10-12)

El autor publica su integración; [revisión y referencias](REVISION_URBAN_REIGN.md).
Se confirma en MTGS original que GSvsync recibe `CSR.FIELD ? 0 : 1`. El espejo
anterior pasaba la paridad del contador EE directamente, invertida respecto a
esa convención. El cliente se ajusta a la misma instantánea CSR enviada a la
DLL. Se prepara regresión de productor/cola/receptor DLL sin GPU, con cuatro
campos incluyendo retención y ticks independientes. Validación nativa,
negativo y juego posteriores pendientes. No cambia runtime, IRQ, contador o
renderer habitual, ni acredita la causa del espasmo/encuadre del juego.

Antes de ese ajuste, el síncrono/compartido e5e040e también termina a 420,06 s;
961 adquisiciones sin anomalías del inspector, 16 pares de capturas revisados
con encuadre/silueta semejantes en la ventana 1400..1415. Ambos controles
compartidos presentan 224 pares de filas RGB duplicadas en cada imagen OpenGL,
y cero pares duplicados en sus salidas DX11. OpenGL duplica el campo seleccionado;
PCSX2 usa desentrelazado automático. Conteo descriptivo, no igualdad de píxeles
ni prueba de que la diferencia sea la causa de deformación del modelo. Las
capturas previas conservan el campo invertido del espejo: no certificar paridad
de entrelazado con ellas. El asíncrono/RAM e5 equivalente sigue en curso.

Validación posterior del cliente `89ffb57ab40577ea6bcb342d151f2217e14b0165`:
seis CTest Windows sin dispositivo correctos, incluida entrega de cuatro
VSync/CSR por el cliente y la cola. Negativo en copia privada, pasando de
nuevo el campo del runtime directamente: compila y falla con código 1;
restaurado código 0, header público intacto. Fixture real runtime/DLL/DX11
recompilado y correcto, 2x2.048 RGBA exactos, GIF fragmentado, registro directo,
reset y cierre sin fallo. No es aún comparación de campos con la escena PS2.

La matriz e5e040e completa sus cuatro controles de 420 s: asíncrono/RAM,
420,08 s, alcanza el límite sin fallo registrado del espejo; 16 capturas
PCRTC revisadas, movimiento gradual en la ventana, tarjeta copiada intacta.
Cada variante conserva 16 PCRTC 1400..1415; compartidas también 16 texturas
adquiridas. Conservar el límite del campo invertido en esas capturas del espejo.
No extrapolar a hilos, rutas DMA optimizadas o todas las animaciones/NPCs.

Src/ del cliente corregido recompilado sobre full 477e013/103, código 0 en
20,67 s, biblioteca del runtime intacta. Runner SHA-256
`0c2d81f875bd15032cd030be8a068cc08670a2185c2cfc428b36e6342e4299e7`.
Se inician controles compartidos síncrono/asíncrono con este mismo ejecutable:
ráfaga de introducción desde 1400 y de Kratos/NPCs desde 2100, respectivamente.
Distintas ventanas deliberadas; no comparar poses entre pasadas por tick.
Capturas y registros aún pendientes de revisión. El nuevo CI está en curso.

Revisión intermedia del síncrono/compartido `89ffb57`, todavía ejecutándose:
16 PCRTC consecutivas 1400..1415 y 16 texturas adquiridas 1400..1414/1416,
todas 512x448. Se revisan los 15 pares que tienen ambos archivos de ese tick;
el adquirido 1416 no se empareja con el PCRTC 1415. Encuadre y silueta semejantes,
movimiento gradual de Kratos en esta ventana, sin deformación brusca observada.
La diferencia entre líneas/filtrado permanece. Es inspección visual de la misma
ejecución, sin prueba de igualdad de comandos o píxeles, fidelidad PS2 o todas
las animaciones. Falta final al límite e inspector del registro completo,
y control asíncrono posterior de Kratos/NPCs. [CI de código 89ffb57 correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38317718747).

## GS de PCSX2: introducción y NPCs con campo corregido (2026-10-12)

Ambos controles de `89ffb57`, mismo runner `0c2d81f...`, terminan al límite
de 420 s: síncrono/compartido 420,08 s; asíncrono/compartido 420,06 s. Sin
salida anticipada ni fallo registrado del espejo; tarjetas copiadas intactas.
Se conserva la terminación forzada del proceso propio, sin verificar cierre
normal de partida. [CI documental anterior f7ad52a correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38318710304).

Inspector final: 714 adquisiciones síncronas desde 1400 y 417 asíncronas desde
2100, sin regresiones de secuencia/render, metadatos inconsistentes, A-B-A,
cambios de dimensiones o hashes desconocidos en esas muestras. Distancia de
ticks 0 en ambas; no es una medida de latencia, FPS o ausencia de retenciones.
El espejo conserva DMA genérico e hilos VU1/frente GS apagados.

Introducción: se revisan las 16 PCRTC y 16 texturas adquiridas, con 15 pares
de ticks coincidentes y el adquirido 1416 separado. Kratos mantiene silueta
coherente y movimiento gradual en la ventana. No acredita todas sus poses.

Asíncrono: se revisan las 16 parejas PCRTC/adquirido 2100..2115, 512x448;
las salidas muestran HUD, Kratos y NPCs. Hay diferencias visibles mayores
que el filtrado de líneas: la imagen OpenGL tiene mucha más niebla y oculta
detalles de algunos NPCs; DX11 los muestra más claros. El encuadre general y
los objetos de primer plano son semejantes. No se observa la deformación
brusca buscada en estas imágenes, pero **no hay equivalencia visual validada**.
El tick por sí solo tampoco garantiza el mismo prefijo GIF o pose; esta
observación no identifica aún un fallo concreto de fog, profundidad o mezcla.

Siguiente control: seguir FOG/FOGCOL/FGE y raster, con comandos/estado comparables
y patrones procedurales, antes de cambiar la cámara o normalizar matrices.
El diagnóstico opcional de #68 ya recibe y dibuja el flujo del juego; no es
un backend principal, un benchmark o resolución general de espasmos/encuadre.
La revisión de Urban Reign incluye crédito explícito a thiagordsfx, por petición
del usuario; implementación del enlace y ajuste de campo propios, contrastados
con PCSX2 original. No se han publicado capturas, volcados o datos del juego.

## Urban Reign: SDK y referencia de FOG (2026-10-12)

Se revisa su mapa SDK de 983 funciones enlazadas, metadatos y bindings HLE:
[alcance, servicios candidatos y créditos](REVISION_URBAN_REIGN.md).
Muchas librerías conservan código original recompilado; reutilizar servicios
comunes, sin trasladar direcciones ni supuestos del juego. Su alias de búsqueda
CD/DVD ignora la capa en un disco de una capa: requiere control propio para GoW.
Se añade crédito a thiagordsfx y PCSX2 en los tres README, con el mismo alcance.

El parser original fijado conserva FOG al alternar XYZF/XYZ: PACKED XYZF3→XYZ3,
XYZF2 con ADC→XYZ2 y A+D XYZF3→XYZ3. Control sin dispositivo con una primitiva
válida, valores distintos 13/201/233/77 y ninguna llamada a Draw: correcto.
Se añade a su fixture público. En nuestro frontend, XYZF asigna el fog al vértice
pero no actualiza `m_curFog`, que usa el XYZ posterior. Se prepara comprobación
de regresión propia antes de corregirlo en un tema separado. No se atribuye
todavía a esa omisión la diferencia de niebla observada ni el espasmo original.

## GS: retener FOG al escribir XYZF, preparación (2026-10-12)

#68 fusionada en `578d4ec582da3b5c05f521b9aa33102046b40499`, tras revisión,
controles y [CI final fe13f32 correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38331548876).
Se integra main por avance directo y se abre tema independiente para FOG.

Control procedural nativo del frontend anterior con renderer CPU explícito,
POINT/FGE, profundidad ALWAYS y colores distintos: seis variantes PACKED,
A+D y REGLIST, XYZF2/3→XYZ2, dibujan el control `805c2ec6`, pero el XYZ posterior
produce `80c05f3b`. La referencia se obtiene escribiendo FOG explícitamente,
con las mismas entradas; los valores son sintéticos, sin datos del juego.
La primera prueba sin configurar profundidad ALWAYS no dibujaba: se descarta
como prueba del fallo. El control corregido sí dibuja y distingue las nieblas.

Se prepara `ps2recomp-gs-fog-latch.patch`, al final de la cadena (104), sobre
base 103 íntegra en worktree propio. XYZF2 PACKED, XYZF3 PACKED y la ruta común
A+D/REGLIST actualizan `m_curFog` antes de lanzar o descartar el vértice. XYZ2/3
siguen conservando el atributo. No cambia cámara, matrices, CRTC, fences o
opciones de rendimiento. El atajo PACKED nativo comparte ese decodificador.

Seis regresiones de grupo cubren los tres PATH, PACKED/A+D/REGLIST/directo/nativo,
ADC, cambio de contexto y reset, comprobando píxeles dibujados y actualización
FOG posterior. Configuración 0 errores/4 avisos existentes; sintaxis 24 PS1,
0 errores. El parche pasa aplicación en comprobación sobre 103. La suite con
regresiones y código anterior se está construyendo para comprobar el negativo.
Positivo, compilación completa de 104, CI y control funcional todavía pendientes.
No se declara corregida la diferencia de niebla del juego, los espasmos o el encuadre.

Validación nativa de `1bfa0358f23a0c25604a7e69c7ab50890a2bfcbf`: las seis
regresiones nuevas con el código anterior fallan y las otras 605 pasan; biblioteca
103 SHA-256 `bf984c75fd78a3dc78502460e22390dfc16ea50b9653a850719724f7f517b2df`
intacta. Aplicando solo la corrección, 611/611 normales y 647/647 con controles
OpenGL GPU correctas. El control privado de seis variantes también pasa a
seis colores iguales al control explícito. No hay fallos conocidos añadidos.
[CI del código 1bfa035 correcto](https://github.com/KIexster/god-of-war-recomp/pull/69/checks).

Son pruebas del runtime, no una compilación nueva del juego. Se inicia la
compilación completa 104 con `GOW_WORK` privado y fuentes anteriores conservadas;
se registrará su commit/hash al terminar. La [PR #69](https://github.com/KIexster/god-of-war-recomp/pull/69)
sigue en borrador hasta completar compilación, escena y CI del HEAD final.

Compilación completa terminada de `9b80b05b7bd23a63d24af32a61495a3501b19c21`:
104 parches aplicados en orden, código final 0 en 1039,50 s, con `GOW_WORK`
privado y VU1 intérprete. Runner de 54.589.952 bytes, SHA-256
`0e65c95d134daccfe3f6f295e299e6def27a77b46402c6e18f4cb66cd2ee8643`.
ELF SCUS_973.99 con hash esperado; fuentes anteriores conservadas y ejecutable,
símbolos y manifiesto congelados antes del control. Flags `/O2 /Ob2 /DNDEBUG /Zi`
y `/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF`.
[CI de 9b80b05 completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38333982967).

Se inicia el control síncrono/compartido de este ejecutable, hilos VU1/frente GS
apagados, tarjeta privada copiada, diagnóstico y ráfaga de 16 imágenes desde
2100. Espejo PCSX2 DX11 con campo corregido y módulo anterior identificado.
Después se repetirá con FINISH asíncrono. Resultado de escena todavía pendiente;
los dos renderers, hashes y readbacks excluyen una comparación de FPS con Opus.

Durante esta revisión, el usuario confirma que han desaparecido los espasmos
de Kratos y los errores de animación de los NPCs en su prueba. Se conserva como
validación visual aportada por el usuario; el mensaje no identifica hash del
ejecutable ni duración. Es un resultado favorable del conjunto de mejoras,
incluida la corrección PMULTH de #67. No permite atribuir por sí solo esa mejora
al nuevo FOG ni cerrar el encuadre o la estabilidad con todas las opciones.

Primer control 104 síncrono/compartido terminado: 420,08 s por límite del
controlador, proceso 1/lanzador 0; tarjeta copiada intacta. Primer informe de
estado 11 a 158,36 s y último 11. El monitor registra minimización entre
358,44 y 372,53 s, restaurada antes de la ráfaga: no afirmar ventana constante.
No se registran fallos del enlace PCSX2 en la muestra revisada.

16 PCRTC y 16 texturas adquiridas forman 16 parejas 2100..2115, 512×448.
Cada JSON adquirido coincide con su registro; los 16 RGBA son distintos.
18 adquisiciones totales desde el umbral hasta el final: sin regresiones,
metadatos inconsistentes, A-B-A, cambios de tamaño o hashes desconocidos;
distancia de ticks 0, sin interpretar ese dato como latencia o FPS.
Revisadas las 16 parejas y las salidas completas iniciales: HUD, Kratos y
enemigos, con movimientos graduales y sin deformación brusca observada en
esta ventana. Encuadre general semejante entre salidas de la misma ejecución,
sin certificar el encuadre de PS2 ni equivalencia de poses/píxeles.

**La neblina más intensa de OpenGL frente a DX11 persiste con el FOG corregido.**
La regresión semántica es real, pero su reparación no resuelve esa diferencia
visual en esta escena. Seguir profundidad, mezcla y pases posteriores, además
del atributo de niebla. Se inicia a continuación el control asíncrono/compartido
con el mismo runner, copia inicial de tarjeta, hilos apagados y ventana 2100.
Sin datos del juego publicados; estos controles no son benchmarks.

Control 104 asíncrono/compartido terminado con el mismo runner: 420,06 s por
límite (proceso 1/lanzador 0), tarjeta copiada intacta, primer informe 11 a
126,98 s y último 11. Minimización 49,30..241,88 s, restaurada antes de la
ráfaga. No hay firmas buscadas de fallo fatal, aserción, acceso inválido,
opcode no implementado/desconocido o fallo del enlace en los dos registros
revisados; no sustituye comprobar todos los caminos del juego.

16 PCRTC y 16 adquiridos, 16 parejas 2100..2115, metadatos correlacionados y
16 RGBA distintos. 347 adquisiciones desde el umbral, sin anomalías del
inspector y distancia de ticks 0. Se revisan todas las parejas y las imágenes
completas iniciales: HUD, Kratos y cuatro enemigos, movimiento gradual sin
espasmo brusco observado en esa ventana. Encuadre general semejante entre
los dos renderers de la misma ejecución. La neblina excesiva de OpenGL sigue
presente. Las poses de las dos pasadas difieren: el mismo tick entre pasadas
no alinea la escena; no comparar sus píxeles como efecto aislado de FINISH.

Revisión local de #69: el atributo es global, se actualiza antes de ADC/kick,
todos los decodificadores implicados conservan FOG y los casos negativos
fallan con código anterior. Cadena 104 íntegra, compilación completa y controles
síncrono/asíncrono completados. Ningún proceso propio del juego permanece
abierto. Comprobar CI del HEAD documental final antes de sacar de borrador y
fusionar. Esta validación cierra la corrección de retención FOG; siguen abiertos
la neblina restante, referencia alineada de encuadre, modos con hilos, estabilidad
prolongada y rendimiento habitual sin diagnósticos.

## GS: precisión de FOG contra el shader original (2026-10-12)

#69 fusionada en `595fe5a03c80f36199ec727c1ff8672311001fae` tras revisión,
compilación, controles funcionales y [CI final 3359e3c correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38336793501).
Se integra main por avance directo y se abre tema independiente de aritmética.

CPU y GLSL usan pesos `F` y `255-F` y truncan ambos productos por separado.
Con F=0 y FOGCOL=(50,100,200), producen (49,99,199), aunque la referencia DX11
produce FOGCOL exacto. También oscurecen un canal cuyo color y FOGCOL son iguales.
El control público `tools/render/probe_fog_reference.py`, por ABI de la DLL
original identificada, confirma 14 patrones RGB: FGE apagado y FOG
0/1/77/128/201/255, dos colores y ambas direcciones de diferencia.

La primera referencia privada de sprite uniforme no permite decidir F=255:
PCSX2 lo trata como clear sin efecto de niebla y omite el shader. Se conserva
esa limitación; el control final envía dos sprites con nieblas distintas en el
mismo lote, verifica la mitad uniforme y reproduce el shader, incluidos los
dos casos F=255. Los 14 patrones finales coinciden con una suma de productos
de pesos `F` y `256-F`, truncada una sola vez. Referencia DX11/PCRTC alpha255;
las regresiones del runtime conservan alpha framebuffer128.

Se prepara parche 105, `ps2recomp-gs-fog-math.patch`, sobre base 104 íntegra:
CPU, compute y hardware comparten la corrección, sin tocar mezcla posterior,
matrices, CRTC o opciones de rendimiento. Una prueba antigua esperaba
FOGCOL=(15,31,47) al programar (16,32,48) con F=0; se corrige ese esperado
porque contradice la retención exacta comprobada, manteniendo la prueba.

Negativo nativo antes de corregir runtime: 612 casos normales, falla solo la
nueva regresión CPU; 650 GPU, fallan exactamente las tres nuevas de CPU,
compute y hardware. Los otros 611/647 pasan; biblioteca 104 intacta. Tras
corregir código, las nuevas pasan, pero la expectativa antigua citada falla
en ambas suites; se ajusta antes de repetir el positivo. Configuración
0 errores/4 avisos existentes; 24 PowerShell, 0 errores. Positivo normal
definitivo: 612/612 correcto tras ajustar ese esperado. GPU definitivo:
650/650 correcto, incluidos compute y hardware efectivos. No se añaden fallos conocidos.
Compilación completa 105, CI y control funcional pendientes.
Esta diferencia de uno o dos niveles por canal no explica por sí sola la
neblina fuerte restante ni acredita mejoras de animación o FPS.

Por petición del usuario de reunir las opciones de rendimiento de ambos
colaboradores, la próxima compilación completa 105 incorporará VU1 compilada.
Se verifica el checkout limpio de Opus y se copian a carpeta privada propia
las 501 micromemorias de 16 KiB que indica su CMake; identidad original/copia
comprobada antes y después. No se modifica su checkout, runtime o capturas.
Los controles anteriores incorporan sus parches, pero usan VU1 intérprete y
hilos apagados: no representan su modo de 24–25 FPS. El C++ VU generado y las
micromemorias se conservarán únicamente en el workspace privado. Registrar
runner/hash, generación/enlace VU y control funcional antes de atribuir esa
configuración al nuevo ejecutable. #70 sigue en borrador hasta completar esos pasos.

Compilación completa 105 con VU1 compilada terminada (2026-10-13): fuente
`3828e9ec499482e039c6d73a0eeb399aa6c42295`, código final 0 en 1169,19 s,
501 micromemorias de la copia privada, 13 archivos generados compilados y
enlazados. El generador informa 5.991 funciones de par y 1.294 bloques.
Runner de 66.302.976 bytes, SHA-256
`747c4224ee0ff19a15ac73dbb8df471a4ea5ce755fc6088e2547a8b9f26a3340`;
ELF con hash esperado, símbolos/manifiesto/ejecutable congelados. Flags
`/O2 /Ob2 /DNDEBUG /Zi` y `/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF`.
[CI del código/documentación 3828e9e correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38338896408).

Se prepara el lanzador privado solicitado por el usuario: verifica manifiesto,
hash del ejecutable y ELF, usa una tarjeta copiada y activa VU1 compilada,
hilos VU1/frente GS, FINISH asíncrono y presentación compartida. Desactiva
espejo DX11, hashes, sondas y capturas; permite solo contador de cuadros
`GOW_PERF_DIAG=frames`/`GOW_PERF_FRAME_PC=0x001837B8` para mostrar
`guest_flip_hz`. Comprobación `-PrepararSolo` en Windows PowerShell correcta,
sin abrir el juego. No quedan compiladores, suites ni partidas propios activos.
Se deja el PC disponible para su prueba. Control funcional del nuevo ejecutable,
modo combinado y FPS todavía pendientes: #70 conserva el borrador. No atribuir
a este binario las pruebas de escena realizadas con el intérprete 104.

## Runner 105: revisión de la prueba manual y significado de los contadores (2026-10-13)

Se vuelven a traer las referencias remotas y a integrar `origin/main`, ya
contenido en la rama, sin reescribir historia. Los cuatro checks del HEAD
documental `3b1715e` terminaron en SUCCESS, [run 38340666366](https://github.com/KIexster/god-of-war-recomp/actions/runs/38340666366).
Manifiesto, resultado de compilación y SHA-256 del runner 105 congelado
coinciden con la entrada anterior; `-PrepararSolo` correcto. No se recompila.

Se revisan `err.log` y `out.log` de la prueba manual privada
`prueba_usuario_0843bf95ada5435895db0bc966a91559`. Acreditan renderer hardware,
presentación compartida e hilos VU1/frente GS activos; contienen 166 lecturas
pad2, con botones y sticks reales. Hay 20 ventanas de perfil, hasta t=113,80
del reloj del contador. Las seis últimas informan 26,25..37,07 entradas a
Flip/s y 9,06..14,80 iteraciones host/s. Otras tres ventanas registran host
0,91/0,65/0,59 por segundo; las dos primeras duran 7,71/9,17 s. No omitir
esos tramos lentos ni presentar el máximo como rendimiento sostenido.

`ps2_perf::guestEntry` cuenta entradas a `vid::Flip` (`0x001837B8`) desde el
dispatcher o llamadas directas. El host incrementa `hostFrames` después de
`renderFrame`/`EndDrawing`; puede dibujar de nuevo la misma textura. Acquire
escoge la publicación más reciente cuyo fence terminó y puede conservar la
anterior. Por tanto, **ninguno de los dos contadores mide cuadros únicos
mostrados**, ni existe una correspondencia obligatoria uno a uno. El código
conserva fences y no introduce `glFinish` global para ocultar esta diferencia.

Todos los ámbitos de tiempo del registro manual están a cero: el modo
`GOW_PERF_DIAG=frames` desactiva sus temporizadores. Cero no acredita ausencia
de trabajo o esperas. La prueba carece de informes `pad2:state`, monitor de
ventana y resultado con código/causa de salida. out contiene liberación de
texturas, pero eso solo acredita parte del desmontaje. No permite atribuir
las ventanas a una escena precisa, minimización, espera GS o cierre normal;
tampoco ofrece referencia visual alineada ni estabilidad prolongada.

El lanzador de controles admite `--perf off|frames|timing` y
`--presentation-diagnostics hash|sequence|off`, preservando hash/off como
opciones independientes del perfil y desactivando diagnósticos heredados.
`timing` activa tiempos transcurridos exclusivos por hilo, no uso de CPU;
los ámbitos se contabilizan al cambiar/cerrar, no al instante del informe.
La captura sigue requiriendo opción explícita. Se registran opciones y
significado de las métricas en el resultado privado. 21 pruebas Python
correctas; configuración 0 errores/4 avisos existentes; 25 scripts PowerShell,
0 errores. Estos checks prueban la herramienta, no validan todavía #70.

Se añade `--pad-script` para registrar un guion de mando explícito en el
control, conservando `GOW_PAD_SOLO_GUION=1` y descartando guiones heredados.
Motivo: el guion predeterminado solo pulsa Start/Cross al principio; las
pasadas nuevas alcanzan después el estado 14, que no se usa como muestra de
partida sostenida. 22 pruebas de preparación/aislamiento correctas tras
ampliarlo. El guion explícito no convierte el control en replay determinista.

## GS: control funcional de FOG preciso con runner 105 (2026-10-13)

Tres controles secuenciales completados sobre el mismo runner 105 congelado
de `3828e9e`/SHA-256 `747c4224...a3340`, VU1 compilada y tarjetas privadas
copiadas de la misma inicial. El usuario confirmó el PC libre; no se ejecutaron
otras partidas, compilaciones o suites locales simultáneamente. Se conserva
CPU y el espejo DX11. Las pasadas con espejo tienen ambos hilos apagados;
el combinado tiene VU1/frente GS activados y no usa espejo.

| Control compartido | Duración | Primer estado 11 del mando | Estado final | Adquisiciones | A–B–A | Distancia máxima host/imagen |
|---|---:|---:|---:|---:|---:|---:|
| FINISH síncrono, espejo DX11 | 420,07 s | 50,15 s | 11 | 3.313 | 0 | 0 |
| FINISH asíncrono, espejo DX11 | 420,07 s | 45,14 s | 14 | 4.096 | 108 | 19 |
| FINISH asíncrono, ambos hilos | 420,04 s | 35,04 s | 14 | 4.096 | 6 | 32 |

Todas terminan por límite (proceso 1/controlador 0), con tarjeta intacta.
Ningún monitor registra minimización; el foco varía en el síncrono y no
pertenece al juego en los otros dos. Visible no demuestra ventana descubierta.
No aparecen las firmas buscadas de excepción, aserción, acceso inválido,
opcode desconocido/no implementado ni fallo del espejo. No es prueba de
cierre normal ni estabilidad prolongada. Los dos cupos de 4.096 se agotan;
no cubren todas las adquisiciones hasta el cierre. No se encuentran regresiones
de secuencia, metadatos inconsistentes, cambios de tamaño o hashes desconocidos.

Los 108 retornos del asíncrono aparecen después del último informe de estado
14; 107 tienen igual paridad y uno alterna, todos en modo de campos. Un hash
que vuelve no acredita igualdad de píxeles ni contenido antiguo. El estado 14
no se usa como partida sostenida. Ninguna de estas pasadas mide FPS.

Síncrono: 16 pares adquiridos/PCRTC 2100..2115, cámara de introducción/NPCs
sin HUD. Asíncrono: adquiridos 3999..4014 y PCRTC 4000..4015, 15 parejas por
tick y un adquirido 3999 sin pareja dentro del cupo; HUD, Kratos y enemigos.
Se revisan todas las parejas y las primeras salidas completas: movimiento
gradual sin deformación brusca observada en esas ventanas. **La neblina
OpenGL más intensa que DX11 persiste**. El tick coincidente dentro de la
ejecución no garantiza igual prefijo de comandos ni equivalencia de píxeles.

Combinado: 16 adquiridos 3989..4071 con saltos, HUD/Kratos/NPCs y movimiento
gradual en las imágenes revisadas. El primer adquirido tiene
hostTick4001/frameTick3989: el umbral elige cuándo capturar en el host, no
obliga a adquirir un tick posterior. Las 16 imágenes de cada pasada son
distintas en RGBA y sus JSON coinciden con los registros. No comparar poses
entre ejecuciones por compartir tick.

Revisión local de #70: pesos y suma coinciden con los 14 patrones externos;
CPU y el shader común compute/hardware conservan alpha y mezcla posterior.
Negativos, positivos 612/612 normal y 650/650 GPU, compilación completa 105
y estos controles cierran la validación local de la corrección numérica.
Antes de ready/merge comprobar CI del HEAD final. Siguen abiertos neblina
restante, encuadre PS2, frescura de presentación, jugabilidad completa,
estabilidad prolongada y cuadros únicos mostrados.

## Runner 105: espera del host y nuevo control de profundidad (2026-10-13)

Perfil separado sin espejo, hash ni capturas, con ambos hilos, temporizadores
y guion explícito de Start/Cross y ataques cada dos segundos. Primer intento:
116,92 s hasta STOP propio, sin llegar a IRX, lecturas de mando o estados;
Flip=0/host≈60. No sirve como perfil de partida. El segundo, con las mismas
opciones, sí arranca y completa 240,06 s por límite. El atasco no tiene causa
identificada; no atribuirlo al perfil o al guion por esa única pareja.

Repetición: tarjeta intacta, ventana visible/no minimizada sin primer plano,
sin firmas buscadas de error; último estado 14. El selector por orden de líneas
conserva 14 ventanas completas de estado 11 entre t=50,07 y 120,54 del perfil,
70,46 s. Flip ponderado 29,19 entradas/s (15,80..35,55 por ventana), host
13,69 iteraciones/s (10,37..20,52). Upload acumula 4.014,37 ms y HostWait
66.320,16 ms. El último ámbito contiene `EndDrawing`, que incluye dibujo,
swap y limitador; la preparación contiene selección/cola/acquisición.
Es una localización del tiempo transcurrido del host, no uso de CPU ni causa
GPU aislada. Separar driver, limitador y GPU/cola sin retirar fences o añadir
`glFinish`. Los tiempos VU/GS pueden proceder de otros hilos y no forman un
reparto del hilo EE. La pasada no ofrece imágenes alineadas, benchmark con
Opus ni cuadros únicos mostrados; estados muestreados no fijan cámara/poses.

Fixture procedural privado de 24 casos por modo, mismo runtime 105 y DLL
DX11: CPU/compute/hardware efectivos ofrecen las mismas 11 diferencias.
Siete proceden de ZTE apagado: se descarta color con ZTST 0/2/3 y se escribe
profundidad con ZTST 1, mientras DX11 dibuja y conserva Z. La lectura del
GS original también condiciona prueba y escritura Z por ZTE. Otras cuatro
difieren un nivel durante mezcla, dos con FGE apagado; seguirlas por separado
antes de cambiar blending. No se modificó runtime ni runner, y no se ha
acreditado todavía que el pase de niebla del juego use esos casos ZTE.

## GS: habilitación de prueba y escritura de profundidad (2026-10-13)

#70 fusionada en `4792988b83c26b318ec455c8666e945194a01323`, tras revisión
local, controles y los cuatro checks del [HEAD final b6d1559 correctos](https://github.com/KIexster/god-of-war-recomp/actions/runs/38390456236).
Se integra main por merge y se abre tema independiente en
`codex/gs-depth-enable`, comprobando primero el árbol limpio.

El parche 106, `ps2recomp-gs-zte.patch`, condiciona la prueba y escritura de Z
por TEST.ZTE. Con ZTE=0, CPU y el shader común compute/hardware ignoran ZTST
y conservan el valor Z existente, aunque ZMSK=0. No cambia alpha, FOG, mezcla,
fences, FIFO, opciones de rendimiento o el renderer de referencia.

El fixture procedural original DX11/corpus de 24 casos daba 11 diferencias
con 105: siete de ZTE y cuatro de mezcla de un nivel. Tras corregir ZTE,
CPU/compute/hardware coinciden en los 16 casos de profundidad; permanecen
las cuatro diferencias menores de mezcla, incluidas dos con FGE apagado.
Se conservan para investigación separada. No acreditan todavía causa de la
neblina en el juego ni autorizan cambiar blending sin otro contraste.

Nueva regresión: 16 combinaciones ZTE/ZTST/ZMSK, color/alpha, profundidad y
píxel vecino; GPU exige compute/hardware efectivos, sin CPU como oráculo.
Negativo contra biblioteca 105 intacta: fallan los tres grupos CPU, compute
y hardware. Primer positivo normal: 612/613; GPU: 652/653. Falla solo el
fixture antiguo PACKED XYZ2 que pretendía escribir todos los bits Z32, pero
programaba TEST=1<<17 sin ZTE. Se activa explícitamente ZTE=1, manteniendo
todos sus esperados y rutas nativa/fragmentada; no se retira la prueba.

Positivos definitivos: **613/613 normal y 653/653 GPU**, códigos 0. GPU
reutiliza únicamente la caché privada 106 de la primera pasada, con claves
que incluyen el shader completo; estos tiempos no son un benchmark.
Test SHA-256 `bd67ab7f3e9225767dc9c96f15d5c09297b008f8e0be66a961f439e90faef8a8`;
biblioteca `57022e3a794851038bc141600cf00069daa939c01073e03ee15ac3235c5dba69`.
Originales de fuentes/fixture/biblioteca/test 105 e identidades conservados
en snapshot privado antes de editar. El runner 105 congelado permanece intacto.

106/106 parches comprobados/aplicados en orden desde el pin en un worktree
privado nuevo, sin reset/clean de los directorios de Opus. Configuración
0 errores/4 avisos existentes; 25 PowerShell, 0 errores. Sin fallos conocidos
añadidos ni datos del juego publicados. **Compilación completa del runner
106, CI final y control funcional todavía pendientes**; no trasladar los
resultados de escena 105 al nuevo comportamiento. Seguir el pase de niebla
con estado de profundidad/mezcla y una referencia de comandos coherente.

Compilación completa 106 terminada: fuente
`32be6ec31da2decf7b49e6ee4c190a39070e0526`, código 0 en 1175,59 s,
VU1 compilada con la misma copia privada de 501 micromemorias y 13 archivos
generados compilados/enlazados. Runner de 66.302.464 bytes, SHA-256
`fc2436ee121feaa15e14e7f2dd0998ee6eb6b0c251a5e292af4397cb82e46f72`.
Manifiesto y ELF comprobados, binario/símbolos congelados, flags
`/O2 /Ob2 /DNDEBUG /Zi` y `/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF`.
Biblioteca del full `5c3787e7ae5a47c427efd0cdd0c4de469bed33c132e34e0a4ea370d7a5ec67bc`;
las suites anteriores usaron otro artefacto de las mismas fuentes/corrección.
El primer intento falló antes del compilador al leer salida Python CP1252
como UTF-8; se conserva y se repite con codificación explícita en carpeta
nueva. No se interpreta el registro de una compilación previa como ese intento.

[CI del código 32be6ec completo correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38393297586).
Después del full, se integra main con #72 por merge en
`834f6ec15529f497de08c11370615540f3aefb66`; la fuente del runner sigue siendo
32be6ec. #72 corrige el resumen de métricas, con 13 pruebas y
[CI final ed05d9f correcto](https://github.com/KIexster/god-of-war-recomp/actions/runs/38394545326),
merge `08103d113ebb57edbc2b8b2e810e4d08d7e24371`.

Controles 106 síncrono y asíncrono compartidos, espejo DX11 original,
ambos hilos apagados, tarjetas copiadas de la misma inicial:

| Control | Duración | Primer informe 11 | Estado final | Adquisiciones desde 4000 | A–B–A | Distancia máxima |
|---|---:|---:|---:|---:|---:|---:|
| Síncrono | 420,08 s | 91,19 s | 11 | 1.026 | 0 | 0 |
| Asíncrono | 420,08 s | 45,13 s | 14 | 4.096 | 110 | 10 |

Ambos terminan por límite (proceso 1/controlador 0), con tarjeta intacta,
sin minimización ni firmas buscadas de fallo. Ventanas visibles sin primer
plano; eso no demuestra que estuvieran descubiertas. No hay regresiones de
secuencia, metadatos inconsistentes, cambios de tamaño o hashes desconocidos.
El asíncrono agota el cupo; sus 110 retornos aparecen tras el último informe
14 y no prueban contenido viejo. No usar estado 14 como partida sostenida.

16 adquiridos y 16 PCRTC de cada pasada, 16 parejas 4000..4015, todas
correlacionadas y con RGBA distintos. Se revisan las parejas y las primeras
salidas completas: HUD, Kratos y enemigos, movimiento gradual sin deformación
brusca observada en la muestra. **La neblina más intensa de OpenGL persiste
con ZTE corregido**. Comparar dentro de una ejecución no garantiza igual
prefijo GIF; no comparar píxeles con otra pasada por compartir tick. No son
benchmarks, cierre normal, encuadre PS2 ni estabilidad prolongada.
Control combinado 106 en curso; #71 conserva el borrador hasta completar
su validación y comprobar CI del HEAD final.

Combinado 106 terminado: 420,05 s por límite, proceso 1/controlador 0,
tarjeta intacta, sin minimización ni firmas buscadas de error. Primer
informe 11 a 35,07 s del mando; último 14 a 415,92 s. 4.096 adquisiciones
(cupo agotado), 18 retornos A–B–A tras el último informe de estado 14,
distancia máxima 27; cero regresiones, metadatos inconsistentes, tamaños
cambiantes o hashes desconocidos. No convertir esos retornos en prueba
de contenido viejo ni el estado 14 en partida sostenida.

Sus 16 adquiridos 3989..4045, con saltos, tienen RGBA distintos y JSON
correlacionados. Revisadas las 16 imágenes: HUD, Kratos y enemigos,
movimiento gradual en la muestra, sin deformación brusca observada; neblina
intensa permanece. Este control no usa espejo porque ambos hilos están
activos. Los tres controles 106 completados, junto a negativos, positivos
nativos y compilación completa, cierran la validación local de **ZTE**.
No cierran frescura del host, encuadre PS2, jugabilidad completa o estabilidad
prolongada. Comprobar CI del HEAD documental final antes de ready/merge #71.

La referencia actual del módulo reinicializa GSOptions, heredando exactitud
de mezcla Basic de Config.h en 2954ebe; no la fija a Maximum. El HLSL original
tiene caminos de mezcla hardware/software y documenta aproximaciones de
hardware dependientes de GPU. Por ello las cuatro diferencias menores del
corpus Basic no bastan para cambiar CPU/GLSL. Se prepara una DLL Maximum
separada para ese contraste, conservando módulo original y su hash; todavía
no se construye ni prueba. Seguir prefijo de comandos, profundidad, mezcla
y pases posteriores antes de atribuir la neblina restante.

### Referencia DX11 Maximum reproducible (2026-10-13)

La preparación anterior se completó. DLL Maximum privada separada, SHA-256
`aa98175972b0c3c654861465cba83934ec5edffa9e74f5128458182492f93546`,
ABI/exportaciones/rechazos sin dispositivo y dos ciclos GPU de 2.048 RGBA
correctos. Con el corpus de 24 casos, CPU/compute/hardware coinciden en 24/24;
las cuatro diferencias menores Basic desaparecen sin cambiar el runtime.
La DLL Basic original permanece intacta, SHA-256
`cbbc2d060df61e479355479fc3ce5b054e5ce27302bf19d5accda735d21ecd62`.

Control con esa DLL privada y runner 106 congelado: 420,386 s por límite,
proceso 1/controlador 0, tarjeta intacta. Ambos hilos apagados, presentación
compartida síncrona; no otras cargas simultáneas detectadas. Primer informe
de estado 11 a 50,17 s; último 11 a 415,47 s. Ventana visible y sin minimizar
en las dos transiciones registradas; sin firmas buscadas de fallo.
16 adquiridos y 16 PCRTC, parejas 4000..4015 revisadas más primera pareja
completa. HUD, Kratos y enemigos; RGBA distintos y correlación con metadatos.
672 adquisiciones, sin regresiones/inconsistencias/retornos A–B–A/tamaños
cambiantes/hashes desconocidos; distancia máxima 0. No supervisión exhaustiva
del foco u oclusión, cierre normal, estabilidad prolongada ni FPS únicos.

OpenGL conserva neblina más intensa y diferencias de líneas/poses frente
a Maximum. No comparar con otro run por tick ni afirmar prefijo alineado:
VSync DX11 se observa antes de callbacks/IRQ EE; la presentación OpenGL se
solicita desde el bucle host, que puede encontrar comandos posteriores.
Es una posibilidad establecida por el orden del código, aún sin cuantificar
en esta escena. Seguir composición/profundidad/pases posteriores además de FOG.

Se añade `GOW_PCSX2_BLENDING=Maximum|Basic` a CMake del módulo aislado:
Maximum explícito por defecto, Basic disponible para reproducir lo anterior,
otros valores rechazados; la DLL declara la precisión al crear GS. ABI 1
sin cambios. Nuevo control procedural público de 24 celdas, todos sus píxeles,
con esperados RGB contrastados con los cuatro renderers; Basic histórico
20/24, Maximum privado 24/24. No prueba alpha framebuffer ni el juego.
La compilación de esta herramienta no sustituye la del runner 106; no se
cambian parches, mezcla del runtime ni referencias CPU.

Build aislado de la herramienta completo: 107 unidades compiladas/enlazadas.
DLL Maximum final `9f0130e26d34ce26c0ee3423d56a77358fd0fec2351661cd194a879328179bb7`:
24/24 pipeline, 14/14 FOG, dos ciclos GPU de 2.048 RGBA y seis CTest correctos.
Basic configurado por CMake
`b6f43f1778fa450db764cda512f5526cc8de1a3ef8e95e2e697e00ba42136cb7`
reproduce 20/24 con los mismos cuatro casos de un nivel, y pasa el contrato
sin dispositivo. Configuración inválida rechazada, Maximum restaurado y
artefacto final vuelto a verificar; Basic histórico intacto. Configuración
del port sin errores/cuatro avisos existentes y 25 scripts PowerShell sin
errores. El control de escena anterior corresponde a aa9817, no a esta DLL.
CI del HEAD publicado pendiente antes de fusionar esta etapa.

### Neblina con frontera común y límites del espejo hardware (2026-10-13)

[#73 fusionada](https://github.com/KIexster/god-of-war-recomp/pull/73),
merge `b41467f8b063c34ba553aa914a9d7f662042d9e8`, tras revisión local y
[cuatro controles CI correctos del HEAD 80a782f](https://github.com/KIexster/god-of-war-recomp/actions/runs/38449849007).

Diagnóstico privado separado, sin cambiar runtime/runner 106: la DLL recibe
cada span GIF, escritura directa, reset y VSync bajo el mismo mutex de la ABI.
Entrega el flujo a DX11 Maximum y a otro frontend GS con OpenGL 106; solicita
su captura en esa misma frontera antes de aceptar nuevos eventos. Se conservan
los 19 registros privilegiados de entrada, estado inicial vacío y orden de PATH.
No guarda el flujo completo de aproximadamente 20 GB. La presentación del
host sigue siendo independiente; este diagnóstico añade un renderer y cortes
de cola/readbacks, por lo que no mide rendimiento.

DLL privada `6c7e1ed41b75f6f0411c86f139684a5c7c9c94631f535ae3a767d09232a59be8`,
biblioteca runtime del full 106. Patrones: DX11 y shadow OpenGL 24/24 RGB
y uniformidad de celdas; dos ciclos ABI/GPU con GIF fragmentado, escritura
directa, reset/cierre y 2.048 RGB por ciclo correctos. El primer patrón mezcla
hardware/compute al iniciar; no atribuir todo ese patrón a hardware.

Control síncrono compartido, hilos apagados, tarjeta privada: 600,405 s por
límite, proceso 1/controlador 0, tarjeta intacta. Primer informe 11 a 55,34 s,
último 11 a 594,38 s. Sin firmas buscadas de fallo o minimización en los
cambios de ventana registrados; el foco cambió. 1.106 adquisiciones, distancia
máxima 0, sin regresiones/inconsistencias/retornos A–B–A/tamaños cambiantes o
hashes desconocidos en la muestra. No acredita oclusión, cierre normal,
combate completo, modo combinado durante diez minutos o estabilidad prolongada.

16 parejas DX11/shadow de frontera 4000..4015 y 16 adquiridos del host,
512×448, distintos y con metadatos adquiridos correlacionados. Revisados los
16 recortes y cuatro triples completos. En 4000 el prefijo shadow contiene
14.984.206 spans/17.393.025.344 bytes y cero escrituras directas; GPU efectivo,
151.982.196 primitivas/0 tiles. En los 15 intervalos siguientes, 64.494..65.782
primitivas y cero tiles: hardware en la ráfaga. **La neblina más intensa
persiste con ese flujo y frontera comunes**. En la región de fondo de la
primera pareja, shadow supera DX11 en promedio +31,50/+37,63/+32,92 RGB;
en el suelo la diferencia media está por debajo de un nivel por canal.
Son descripciones de regiones, sin tolerancia de equivalencia ni causa única.
DX11 Automatic conserva historia de campos; OpenGL duplica filas. El desfase
de solicitud del host no explica por sí solo este contraste del diagnóstico.

Corpus privado de composición, 16 casos EN1/EN2/MMOD/SLBG/ALP/alpha: CPU y
OpenGL del runtime coinciden entre sí en 16/16. Frente a DX11 hardware coinciden
en 10/16; frente al software original de PCSX2 con composición DX11, en 11/16.
Hay diferencias grandes al usar RC1 solo con alpha cero o ALP reducido, una
diferencia menor de redondeo y discrepancias hardware/software con alpha 64.
Ejemplo dual/MMOD=0/alpha=64: hardware (240,64,128), software original y runtime
(128,112,80). RC1 solo/MMOD=1/ALP=64: ambos renderers originales (210,54,100),
runtime (240,64,128). No adjudicar esos resultados a la neblina de la escena
sin sus registros/pases; Maximum no certifica todos los caminos de alpha.

Referencia software privada `3ad27ad874ec7e8bcba35396bb2134f158156261d392965978cc43085587db55`:
usa la fábrica original GSRendererSW, cero hilos extra y dispositivo/composición
DX11. Pasa 24/24 pipeline y 14/14 FOG. Solo probada con entradas procedurales;
su fixture rechaza escrituras directas, sin simular retención de Q. No es un
renderer integrado del juego ni una validación de toda la ABI. CPU y espejo
hardware se conservan. Próximo contraste: salida de merge antes del
desentrelazado y ráfaga acotada con estado inicial verificable para el software
original; seguir profundidad, alpha, textura/FOG y pases posteriores.

El contraste completo de esa ráfaga procedural revela otro límite de la
referencia hardware: los 14 controles FOG históricos solo verificaban la
mitad izquierda. En ambas mitades hay 28 controles; CPU/OpenGL del runtime
y software original coinciden en 28/28, hardware Maximum en 16/28. Sus 12
mitades derechas FGE usan el FOG del primer sprite. Por ejemplo, F=0 a la
izquierda y 17 a la derecha: hardware produce (50,100,200) en ambas,
software original y runtime (50,100,200)/(62,95,190). Los 14 esperados
izquierdos, incluidos los de la corrección matemática #70, permanecen válidos;
el método no excluye toda optimización hardware de un lote con nieblas distintas.
No prueba que ocurra en el fondo de la escena ni justifica reducir FOG globalmente.

`probe_fog_reference.py` comprueba ahora ambas mitades por defecto, color y
uniformidad de todos los píxeles; `--left-only` reproduce los 14 anteriores.
Negativo real hardware 16/28/código 1; positivo software original 28/28/código 0;
CPU/OpenGL privados 28/28 RGB y uniforme. El modo histórico conserva 14/14.
No se modifica renderer/alpha del runtime. Se mantiene explícito que PCRTC
no valida por sí solo alpha framebuffer o interpolación dentro de una primitiva.

Herramienta privada de merge/Freeze/GIF/VSync ya compilada: limita la ráfaga
a 16 campos/160 MiB, exige fragmentos exteriores completos en la frontera y
un End válido. Freeze original incluye lectura de destinos de la caché para
capturar VRAM; no se publica. Una ráfaga sintética de cuatro campos, 11 registros,
4.237.045 bytes, se restaura en GSRendererSW original: sus cuatro imágenes
64×32 coinciden enteramente en RGB con una ejecución fresca de ese software.
El desacuerdo con hardware se reproduce también sin Freeze y queda localizado
en la mitad derecha; no se adjudica a restauración. End truncado se rechaza
antes de crear dispositivo/directorio. La historia inicial de desentrelazado
no está serializada. Control de esta herramienta con el juego pendiente.

### Ráfaga de escena repetida con el software original (2026-10-13)

[#74 fusionada](https://github.com/KIexster/god-of-war-recomp/pull/74),
merge `6aaa1ed0f595f7f91a9e4e27d72481fd0fa92060`, tras revisión local y
[cuatro controles CI correctos del HEAD 622af4b](https://github.com/KIexster/god-of-war-recomp/actions/runs/38456073463).

Control nuevo con el runner 106 congelado, presentación compartida síncrona,
hilos apagados y tarjeta privada. DLL de diagnóstico separada
`eeef73a7162c129f67f03da037765d20071682492b0c1e7b6bf9df3bf54786de`:
conserva el renderer DX11 Maximum original y el segundo frontend/OpenGL del
contraste de frontera común. Lee además su salida de merge **antes del
desentrelazado**, guarda Freeze original en 3999 con lectura de destinos de
caché y registra los eventos posteriores hasta End 4015. No modifica IRQ/FIFO,
runner, parches ni renderer principal. El readback y la referencia adicional
pueden alterar tiempos/cachés; son diagnósticos, no medición de velocidad.

Termina a los 600,391 s por límite, proceso 1/controlador 0; tarjeta intacta.
Primer informe de estado 11 a 55,84 s, último 11 a 596,70 s; sin firmas buscadas
de fallo ni minimización en los cambios de ventana registrados.
1.167 adquisiciones, sin regresiones/inconsistencias/retornos A–B–A/tamaños
cambiantes/hashes desconocidos; distancia máxima 0. Se obtienen 16 adquiridos,
16 PCRTC, 16 merges y 16 shadow, 512×448, en 4000..4015; los adquiridos están
correlacionados y hay 16 RGBA distintos. No certifica oclusión, cierre normal,
partida completa, modo combinado prolongado, FPS únicos ni estabilidad larga.

La ráfaga privada tiene 100.082 registros/114.804.229 bytes, bajo el límite
de 160 MiB, con Freeze inicial, GIF, 17 VSync 3999..4015 y End válido. No
contiene escrituras directas: el fixture software sigue rechazándolas.
Referencia privada de restauración
`894057dfb542ca43abd9d1b934301151c9c740cf6d67f8a5c212e860dd486a1c`,
GSRendererSW original y dispositivo/composición DX11, termina con código 0 y
17 imágenes. El formato exige continuidad y End antes de crear el dispositivo;
su control sintético de restauración y rechazo de truncado consta arriba.
El Freeze incluye VRAM nativa y estado GS; **no conserva la historia inicial
de desentrelazado**. No demuestra equivalencia de cachés/optimizaciones entre
hardware y software ni integra ese software como renderer del juego.

Se revisan los 16 tríos en contactos reducidos y cuatro completos de
4004/4007/4011/4015, tras varios campos de calentamiento de la referencia.
El software original mantiene un fondo cercano al merge hardware;
**el exceso de brillo/neblina permanece en OpenGL**. En la región de fondo
x=210..439, y=35..149 de 4004, OpenGL−software tiene media
+31,03/+37,26/+32,57 RGB; software−hardware tiene media
−0,44/−0,45/−0,41 y media absoluta 0,49/0,50/0,45. En las 16 imágenes,
esa media absoluta software/hardware está por debajo de 0,68 por canal.
Son medidas descriptivas de esa región, sin tolerancia de equivalencia:
las imágenes completas también tienen diferencias y los campos iniciales
no restauran historia anterior. En el fondo de 4000, PCRTC actual y merge
hardware coinciden en RGB; allí el desentrelazado posterior no explica el
contraste de brillo con OpenGL.

Los registros de composición son constantes en 3999..4015: PMODE `0x8023`
(EN1=EN2=1, MMOD=1, AMOD=0, SLBG=0, ALP=128), SMODE2 `0x21fed1`
(INT=1, FFMD=0), DISPFB1 `0x800000090d0`, DISPFB2 `0x90d0` y BGCOLOR=0.
Esto acota el contraste a la configuración real; los desacuerdos procedurales
de alpha y FOG variable del espejo no demuestran por sí solos la causa del
fondo. Como inferencia, el desacuerdo persistente frente a ambas referencias
orienta el siguiente control hacia el runtime. Falta repetir el mismo campo
de comandos de backend, con su estado/VRAM inicial, en CPU y GPU para separar
interpretación del frontend y rasterización; seguir profundidad, mezcla,
textura/FOG y pases posteriores. **La neblina no está corregida**.

### Destino recién dibujado, alpha y referencia CPU (2026-10-13)

[#75 fusionada](https://github.com/KIexster/god-of-war-recomp/pull/75),
merge `9ac5585f1a8400922223e41719e6bc01c7e4e35d`, tras revisión local y
[cuatro controles CI correctos del HEAD 0f42d7f](https://github.com/KIexster/god-of-war-recomp/actions/runs/38460157286).

Nuevo diagnóstico privado, runner 106 congelado: registra un campo de backend
después de VSync 4002 y hasta Present/End 4003, con estado/VRAM propios.
DLL `7bb35c83b832fa1b055e8fc269f1640d1bcd94952b80d67701f28d33d261edb6`;
patrón 64×64 validado primero, dos repeticiones CPU/compute/hardware con estado
final e imágenes correctos. Se conserva la DLL anterior y no se cambia el juego.
Control compartido síncrono, hilos apagados: 450,224 s, detenido mediante STOP
propio después de completar la captura y 16 muestras; proceso 1/controlador 0,
tarjeta intacta, primer informe 11 a 55,21 s, último 11 a 442,00 s. No es un
control completo de 600 s ni un cierre normal. 283 adquisiciones sin anomalías
buscadas, distancia máxima 0; 16 adquiridos/PCRTC 4003..4018 correlacionados y
distintos. Sin FPS, estabilidad prolongada o validación del modo combinado.

Registro de 33.391.712 bytes: Initial, 78.181 Submit, 23 TEXFLUSH, 139 CLUT,
tres transferencias/tres uploads, Present y End. Ningún Submit activa FGE.
La referencia CPU y el candidato se restauran desde ese mismo estado inicial.
Dos repeticiones hardware hasta Present tienen diferencias de RGB/VRAM/estado;
la primera mezcla compute mientras calientan shaders, la segunda registra cero
tiles. Un replay completo CPU devuelve código 3 al no reproducir el End GPU.
**No se declara paridad ni se convierte ese código en éxito**.

La imagen PCRTC de 4003 copia FBP0 heredado a FBP208 al comienzo del campo;
después se dibuja la escena nueva en FBP0. Que CPU repita la bruma al presentar
FBP208 no excluye un problema previo de GPU. Una sonda cambia únicamente los
dos DISPFB de Present para leer el destino FBP0 recién dibujado como CT32:
es una petición de diagnóstico, **no la imagen presentada del juego**.
CPU/GPU tienen 122.960 píxeles RGB distintos, media absoluta
9,92/11,71/10,34; dos pasadas hardware calientes tienen el mismo RGB aunque
su VRAM final difiere en 1.151 bytes. Compute solicitado produce el mismo RGB
final que hardware en esta sonda. No acredita estabilidad global.

Prefijos independientes con la misma entrada anterior y readback al final
acotan el contraste de fondo: después del clear 54, RGBA y Z son cero en
las regiones inspeccionadas; el RGB del fondo permanece igual hasta 76158.
La diferencia grande aparece en los 64 triángulos 76160..76223, con FGE=0,
T8 y ALPHA `0x54`, cuya ecuación usa el alpha del destino. Antes de ese pase,
el alpha del fondo ya difiere: tras 76158, medias CPU 63,43 y GPU 102,24;
después de 76223, GPU supera CPU en +26,77/+31,78/+27,88 RGB.
La Z24 leída en las regiones permanece idéntica en los cuatro prefijos
76076/76142/76158/76223. Son descripciones locales, sin equivalencia global.
Snapshot experimental por Submit no cambia el RGB de esta sonda; invalidar
la página de textura CPU por Submit tampoco cambia su resultado.

La divergencia de alpha se introduce en el bloque 75983..76076: preparación
CT24 en FBP320 y lectura Z16 hacia un destino CT16 en FBP0 con FBMSK `0x3fff`.
Un primer patrón reducido Z16/CT16/máscara no reproduce todo el desacuerdo de
la escena y también muestra diferencias con el software original. Por tanto,
no se adjudica aún a una conversión, máscara o sincronización concretas.

La auditoría identifica entretanto un error de la referencia CPU: aplica
siempre saturación al resultado de ALPHA, aunque COLCLAMP=0. Cuatro patrones
procedurales completos 64×64 contrastados con GSRendererSW original y GPU
coinciden en los esperados wrap/clamp. Por ejemplo, −(32,64,128) envuelto da
(224,192,128), no cero; (200,220,240)+(80,64,32) envuelto da (24,28,16), no 255.
CPU 106 falla esos dos controles y pasa los dos de saturación. El espejo
hardware Maximum falla los dos negativos incluso con saturación, por una
ruta que conserva el color fuente; no usarlo como autoridad única aquí.

Parche 107 separado `ps2recomp-gs-cpu-colclamp.patch`: CPU selecciona wrap o
saturación del RGB de ALPHA y conserva alpha/Z y el resto del pipeline.
Regresión sin juego: CPU 106 falla 2/4, código 2; nuevo objeto CPU con el
resto de la biblioteca 106 pasa 4/4, código 0. No modifica GLSL. COLCLAMP=0
aparece en la preparación de la máscara de esta escena: la CPU más oscura
no certificaba que GPU estuviera equivocada en ese bloque. Se conserva el
runner/runtime 106 mientras se valida la corrección de la referencia.
Suite completa incremental reenlazada con CPU/main/regresión/tabla 107 y
bibliotecas restantes 106: 617/617 normal y 657/657 con controles GPU,
códigos 0, sin fallos conocidos añadidos. No sustituye el full del juego.
`scripts/2_compilar.cmd`, CI del HEAD final y control funcional de 107
siguen pendientes antes de fusionar. **La neblina GPU y
su fidelidad frente al software original continúan abiertas**.

### 2026-10-14 — Compilación completa de la referencia CPU 107

El full obligatorio `scripts/2_compilar.cmd` terminó con código 0 en
1.110,174 s, fuente `f76bc46327ca646a1eb5fc6103b4b484d1d9be8b`, 107 parches,
VU1 compilada y 501 micromemorias privadas. Los 13 archivos `programa0..12`
se compilaron y enlazaron. Ejecutable de 66.302.464 bytes, SHA-256
`c19f1d31ab52d26d3cdd09ca68310c74d62595435d912e9ba30f122aff1af250`;
biblioteca runtime
`49c3458c2acb8ff45f0c6d5126403b1c4d19944a2dd098e779987ba49b88acb9`.
Manifiesto y binarios se conservaron en una carpeta privada; runner/runtime
106 y su biblioteca `5c3787…67bc` siguen intactos.

El primer intento acabó con código 1 al interrumpir exclusivamente su
descarga de FFmpeg tras 9.412,85 s, antes de compilar el runner. Sus registros
y archivo parcial se conservaron. CMake había retirado la copia local porque
el proyecto no define URL_HASH. El reintento verificó el archivo privado
existente por SHA-256 y lo extrajo normalmente mediante un guard de su
script CMake generado; no se cambió la dependencia ni las fuentes C++.

Los cuatro controles de CI del HEAD fuente pasaron en `38487502682`.
Esto acredita compilación y pruebas; **el control funcional 107 todavía está
en curso** y la PR 76 sigue en borrador. No se declara corregida la neblina
OpenGL, cierre normal ni estabilidad prolongada.

### 2026-10-14 — Controles funcionales cerrados del runner 107

Los tres controles siguientes usan el ejecutable completo 107 anterior,
con tarjeta privada copiada, cuyo SHA-256 permaneció idéntico antes/después
y al original. Se revisaron registros y capturas. No se encontraron las
firmas buscadas de excepción, assertion u opcode desconocido; esa búsqueda
no descarta otros fallos. Los procesos se cerraron por el límite del control,
**sin acreditar salida normal ni estabilidad prolongada**.

`scripts/probar_menu.ps1 -Segundos 115` terminó en 115,343 s, controlador 0.
Nueve capturas muestran título, nueva partida, dificultad, introducción de
Kratos e inicio del barco. El informe alcanza estado 11 a 91,68 s. Este
script no registró el estado de minimización y no mide FPS presentados.

Control compartido síncrono con VU1 compilada, hilos VU1/frente GS apagados
y espejo DX11 original Maximum `9f0130…79bb7`: 420,390 s, proceso 1 por
límite/controlador 0. Primer estado 11 a 50,24 s y último a 410,74 s;
ventana sin minimización registrada. Los 16 adquiridos y 16 cuadros DX11
4000..4015 se correlacionan con sus registros, son distintos y tienen
512×448. Se revisaron los contactos y dos pares completos: aparecen HUD,
Kratos y enemigos; OpenGL conserva la bruma excesiva y también hay
diferencias de líneas/pose. El mismo tick no demuestra el mismo prefijo
de comandos. 520 adquisiciones registradas sin regresiones de secuencia,
repeticiones inconsistentes, retornos A-B-A o cambios de tamaño; distancia
hostTick−frameTick máxima 0 en este registro, sin equivalencia con FPS.

Control compartido combinado, FINISH asíncrono e hilos VU1/frente GS
realmente activados según el registro: 420,046 s, proceso 1 por
límite/controlador 0, sin minimización registrada. Primer estado 11 a
35,06 s; el último informe es estado 14 a 416,41 s. **No se acreditan
420 s de partida sostenida**. Los 16 adquiridos distintos y correlacionados
512×448, frameTicks 3990..4039, muestran HUD, Kratos y enemigos en la escena
muestreada. Las 4.096 adquisiciones no tienen regresiones de secuencia,
repeticiones inconsistentes o cambios de tamaño. Hay 32 retornos A-B-A:
30 con la misma paridad y dos alternantes, todos con modo de campos
registrado. No se atribuyen a corrupción o imagen antigua sin investigar
estado y píxeles. La distancia entre ticks está entre 0 y 22; no son
milisegundos, cuadros únicos ni una medición de latencia.

Se cierra la validación funcional acotada del cambio CPU COLCLAMP. La
documentación final y su CI todavía deben incorporarse a la PR 76 antes
de fusionarla. Estos controles no resuelven la neblina, el encuadre,
el rendimiento real presentado o una sesión combinada prolongada.

### 2026-10-14 — TEXA en texturas de profundidad: negativo externo y candidato 108

Se revisó `GSLocalMemory` del software original: `ReadTexel24Z`,
`ReadTexel16Z` y `ReadTexel16SZ` llaman a las mismas expansiones TEXA que sus
formatos de color. CPU y GLSL del runtime solo aplicaban TA0/TA1/AEM a
CT24/CT16/CT16S y omitían Z24/Z16/Z16S. En el bloque real 75983..76076 de
la captura de backend anterior, TEXA tiene **TA0=0, TA1=0, AEM=false**;
32 sprites muestrean Z16 con DECAL/TCC=1 hacia CT16 y FBMSK `0x3fff`.
El supuesto inicial TA1=128 no corresponde a esa entrada y queda descartado.

Control procedural sin juego, 48 celdas completas 8×8 en una imagen 64×64:
seis formatos color/profundidad y ocho patrones TA0/TA1/AEM, cero RGB y bit
alto de 16 bits. Se escribe alpha y se observa mediante una mezcla posterior
que usa Ad. GSRendererSW original y DX11 Maximum pasan 48/48. CPU 106 y
GPU 106 pasan 29/48: los 19 fallos son de los alias de profundidad, mientras
los 24 casos de color pasan. La GPU está realmente activada; esta fixture
mezcla hardware/compute y no certifica exclusivamente hardware. El caso
Z16 de bit alto y TA0=TA1=0 devuelve 128 en el runtime anterior y 0 en ambas
referencias originales.

El candidato 108, separado en `ps2recomp-gs-texa-depth.patch`, añade solamente
los tres alias a `applyTexa` CPU/GLSL. Conserva RGB, mezcla, FOG, profundidad,
fences y presentación. Regresión independiente: CPU 107 sin el cambio falla
las tres pruebas, código 3, con 19 patrones erróneos; CPU 108 pasa 3/3 y
OpenGL 108 pasa 5/5, incluidos compute y hardware observados sin fallback
CPU. También comprueba el RGBA y el vecino no cubierto. Suite completa
incremental con objetos CPU/GLSL/main/regresión 108, biblioteca runtime full
107 y bibliotecas de pruebas anteriores: 620/620 normal en 11,821 s y
662/662 GPU en 209,116 s, códigos 0, sin fallos conocidos añadidos.
**Todavía no sustituye el full obligatorio ni el control del runner 108**.

Se repitieron los mismos prefijos 76076/76158/76223 del campo registrado con
106, cambiando únicamente los objetos CPU/GLSL a 108. Primero se validó un
registro procedural completo, código 0 y estado/VRAM End/RGB exactos.
En las regiones de cielo y actores, CPU y GPU 108 producen RGBA idéntico y
la Z24 permanece igual. Después de 76076, el alpha medio del cielo cambia
de 206,7415 con CPU 107 corregida a 85,9133; después de 76158 y 76223,
de 102,2364 a 43,1870. El RGB después de la mezcla tardía 76223 baja de
85,2845/95,5252/82,4820 a 44,6819/47,2167/40,0719. Esto atribuye una parte
del brillo a la expansión TEXA de esa misma entrada, sin comparar ticks
de distintas ejecuciones. Los replays parciales siguen devolviendo 1 por
otras diferencias de VRAM/progreso de transferencia: no hay paridad global.

La lectura anterior usa una **sonda sintética de FBP0 recién dibujado**;
el PCRTC real de ese registro copia FBP208 heredado al comienzo del campo.
No acredita la imagen presentada del runner 108 ni su fidelidad final.
Próximo: full aislado con 108 parches y VU1 compilada, menú obligatorio,
nueva captura con CPU/GPU 108 y espejo DX11 dentro de la misma ejecución,
repetición software original en la misma frontera, y control combinado.
Neblina restante, encuadre, cuadros únicos presentados y estabilidad
prolongada siguen abiertos.

### 2026-10-14 — Full 108 terminado; control funcional todavía pendiente

`scripts/2_compilar.cmd` terminó con código 0 en 1.171,305 s, fuente
`084908442f9640535cbd94bb9dcc216c9e46738d`, 108 parches y VU1 compilada con
501 micromemorias privadas. Los 13 archivos `programa0..12` se compilaron
y enlazaron. Ejecutable de 66.302.976 bytes, SHA-256
`a147193bcf80eb5cdf1c5c68a3cefb201f9f1dcc9ba80eb5f3f509c849e3fd3a`;
biblioteca runtime
`116b54c428b5f9d9c85fd7f8c04a3ff4d6dbfdee09634340f0160ea44eff6645`.
Manifiesto/binarios en carpeta privada separada, sin modificar las
bibliotecas ni ejecutables 106 y 107. Caché FFmpeg original verificada por
el mismo SHA-256 del full 107; extracción normal, sin cambiar fuentes.

Los cuatro controles CI de la fuente 0849084 pasaron en `38493899926`.
Se integró después main mediante merge, conservando la identidad separada
de la fuente compilada. La PR 76 de COLCLAMP CPU quedó fusionada en
`4caab4e630b4089e60b884cc3ea782a074249506`, tras revisión local, full,
controles funcionales y cuatro CI SUCCESS del HEAD documental final
`60aa5249ad345145a8283d01b197683fda816a0e`, run `38493430447`.

Ampliación del límite del control combinado 107: el **primer** informe de
estado 14 aparece ya a 100,75 s, además del último a 416,41 s. No se
adjudica la causa sin captura de esa transición. No se debe tratar el
intervalo completo como partida sostenida. El futuro control 108 podrá
entregar ataques con un guion explícito, pero entregar botones por sí
solo no prueba aceptación del juego ni supervivencia.

La PR 77 de TEXA permanece en borrador. Menú 115, referencia privada
reenlazada con runtime 108, nueva escena frente a DX11/software en la misma
frontera y configuración combinada siguen pendientes. El full y su CI no
acreditan por sí solos fidelidad visual o estabilidad prolongada.

### 2026-10-14 — Menú 108 y referencia reenlazada: controles procedurales cerrados

El menú obligatorio 108 terminó en 115,294 s, controlador 0 por límite.
Se revisaron las nueve capturas: primera negra durante el arranque,
título/nueva partida, dificultad, introducción de Kratos e inicio del barco
con el personaje visible. El informe de 90,87 s muestra estado 11.
Tarjeta privada intacta antes/después/al original, sin las firmas buscadas
de excepción/assertion/opcode desconocido. No hay monitoreo de
minimización en este script, salida normal o prueba de partida prolongada.
Estas imágenes de otra ejecución no se usan para comparar ticks con 107.

Se construyó una DLL privada nueva con el mismo nativeCore/API Maximum y
trazado originales, pero frontend/shadow y biblioteca runtime **full 108**
`116b54…6645`, sin sobrescribir la referencia 106. SHA-256 del módulo:
`1879c86e9c05189e8d8bc0d9e91c718bd17151a00d1a063def4fde3390d88488`.
El primer control no pudo crear la referencia porque omitía la carpeta
obligatoria del trazado; se conservaron los registros y se corrigió la
configuración del helper, sin cambiar renderer/DLL ni ocultar una diferencia
de píxeles. El reintento usa carpetas nuevas.

Los 48 patrones TEXA completos pasan en CPU, hardware solicitado y compute
solicitado del runtime completo 108, y 48/48 en la referencia DX11 original.
El modo hardware de esta fixture registra 96 primitivas y 12 tiles: es una
ruta mixta, conservando el alcance de los tests efectivos separados.
Tres campos FOG 64×64, seis mitades completas, del shadow GPU coinciden
byte a byte en RGB con software original recién inicializado. Dos campos
restaurados desde Freeze y el mismo GIF/VSync coinciden exactamente con
esa referencia software fresca. Un registro sin End se rechaza antes de
crear dispositivo o directorio de salida. Se conserva el límite conocido
de FOG de la segunda mitad del hardware original.

Estas comprobaciones validan la herramienta antes de usarla con el juego;
no son todavía el contraste de escena 108. La PR 77 sigue en borrador:
faltan nueva escena con las mismas fronteras, control combinado y CI del
HEAD documental final antes de ready/merge.

### 2026-10-14 — Escena 108 con fronteras comunes y separación del host

Control compartido síncrono 108, VU1 compilada, hilos VU1/frente GS apagados
y DLL privada 1879: terminó en 464,471 s mediante STOP propio después de
completar 16 adquiridos, 16 cuadros DX11, 16 shadow GPU y End4015. Proceso 1,
controlador 0; **no es un control completo de 600 s ni una salida normal**.
Tarjeta intacta, sin minimización registrada ni firmas de fallo buscadas;
primer estado 11 a 55,17 s y último a 457,72 s. Las 251 adquisiciones
registradas no muestran regresiones, repeticiones inconsistentes, retornos
A-B-A o cambios de tamaño; distancia máxima entre ticks 0. Las 16 capturas
acquiridas 512×448, 4000..4015, son distintas y se correlacionan con el registro.

Ráfaga nativa con Freeze3999 y el mismo GIF/VSync hasta End4015:
113.538.293 bytes, 94.829 registros, SHA-256
`995df20376de22b04678ed4156c55449b48fd8ece1e8d1a1677364c69df13b51`.
Software original restaura esa misma entrada y produce 17 campos
3999..4015, código 0. Se revisaron los 16 tríos de merge DX11/software
restaurado/shadow108 y los cuadros completos 4004/4015. Aparecen HUD,
Kratos y enemigos; el fondo del shadow 108 queda cercano a las referencias
originales. En 4015, región fija de fondo x=200..339, y=0..79, medias RGB
shadow 35,6946/35,7850/31,2411 y software 35,7638/35,8478/31,3343.
La región incluye mástil/cuerdas y no acredita igualdad global.

Persisten diferencias de líneas, bordes y pose. En todo 4015, diferencia
absoluta media shadow−SW 3,8922/3,7271/3,2203 RGB; media firmada
−0,2818/−0,2830/−0,2386, máximo absoluto 157 y 195.092 píxeles distintos.
DX11−SW tiene media absoluta 0,6009/0,5978/0,5453. Son estadísticas
descriptivas, sin tolerancia global aceptada ni equivalencia de estado/VRAM.
La historia de campos anterior al Freeze no se conserva; los 16 campos
posteriores se inspeccionan sin presentar esa limitación como resuelta.

La textura adquirida por el host **no es idéntica al shadow en ninguno de
los 16 ticks**. En 4015 difieren 118.710 píxeles RGB, media absoluta
3,9683/3,5600/3,0705 frente al shadow. También se revisó el cuarteto completo
DX11/SW/shadow/adquirido. El fondo adquirido sigue cercano, pero ese tick
no acredita su misma frontera de comandos: el EE ejecuta en otro hilo,
notifica VSync antes de despertar callbacks y el host toma/enfila Present
por separado. Esto ofrece una explicación posible de la diferencia,
**sin atribuirla todavía a orden, slots, raster o presentación**.
La comparación común de shadow/DX11/SW no debe convertirse en paridad
acreditada de la textura del host.

La expansión TEXA tiene negativos/positivos externos, full y control visual
acotado; quedan abiertos la separación anterior y los detalles de imagen.
El control combinado 108 con entradas de ataque está en curso. PR 77
continúa en borrador hasta su cierre/revisión y CI del HEAD documental final.

### 2026-10-14 — Combinado 108 con ataques: control de 420 s cerrado

Control compartido con FINISH asíncrono, VU1/frente GS en sus propios hilos
realmente activados: 420,059 s, proceso 1 por límite/controlador 0. Tarjeta
privada intacta, sin minimización registrada ni firmas de fallo buscadas.
Primer estado 11 a 35,06 s y último a 416,35 s; no aparece estado 14 en
los informes. Se conservó el arranque Start/X y se añadieron pulsaciones
de cuadrado desde 40 s, cada 0,75 s durante 0,35 s. Los 128 cambios de
paquete registrados incluyen `ff7f`/neutral en estado 11. Las 16 capturas
tardías 11998..12032, distintas y correlacionadas, muestran HUD y Kratos
ejecutando ataques con los arcos de las hojas. Eso acredita entrega y
acción visible en esa porción, sin afirmar completar una sección del juego.

Las 4.096 adquisiciones registradas no tienen regresiones de secuencia,
repeticiones inconsistentes, retornos A-B-A o cambios de tamaño; imágenes
512×448 y modo de campos registrado. Distancia entre ticks 0..4, sin
convertirla en milisegundos, latencia o FPS únicos. No se compara esa cifra
con el combinado 107 como mejora de rendimiento: los guiones/escenas son
distintos. El arranque forma parte de los 420 s; la permanencia observada
en estado 11 comprende los informes 35,06..416,35. **No hay salida normal,
estabilidad prolongada ni cobertura del juego completo**.

Quedan cerrados full, menú, negativos/positivos externos, referencia
procedural y controles funcionales acotados de TEXA 108. La revisión final
de código/alcance es local, sin aprobación externa. La PR 77 requiere todavía
CI verde del HEAD documental final antes de ready/merge. Permanecen abiertas
la diferencia adquirido/shadow, los detalles frente al software original,
encuadre, presentación única, desglose de HostWait y estabilidad larga.

### 2026-10-14 — TEXA fusionado; composición PCRTC 109 en validación

La PR 77 quedó fusionada en `80f42145d2e9373d02c23685294de40236deed97`.
Los cuatro checks del HEAD final `14a70b3` terminaron en SUCCESS,
run [38498133684](https://github.com/KIexster/god-of-war-recomp/actions/runs/38498133684).
La revisión final es local y acotada; full, menú y controles108 anteriores
están cerrados dentro del alcance descrito. Persisten las diferencias de
detalle y entre la imagen adquirida y el shadow, encuadre y estabilidad larga.

La siguiente sonda usa fuentes CT32 uniformes independientes, páginas0/32,
salida64×64 progresiva y32 combinaciones EN/MMOD/SLBG/ALP/alpha. CPU y
OpenGL108 coinciden con el software original en16/32 resultados RGB.
Cuando solo queda RC1, ambos omiten su mezcla con BGCOLOR; también hay
errores de un nivel en la aritmética de mezcla. La fuente primaria
PCSX2 `2954ebe`, `GSDevice11::DoMerge` y `merge.fx`, conserva la mezcla de
RC1 incluso con RC2 apagado: MMOD selecciona ALP/255 o alpha de RC1×2/255.
Esto pertenece al compositor de salida, separado del ALPHA del raster.

El candidato109 mezcla también RC1 con BGCOLOR y suma los productos antes
del redondeo UNORM al entero más cercano, tanto en CPU como en GLSL.
Los tres modos privados CPU/compute/hardware producen31/32 RGB exactos
frente al software original. En ALP201, el rojo es232 frente a231; verde61
y azul120 coinciden. Se mantiene esa diferencia registrada: el modelo
UNORM ideal da232 y la [especificación D3D11, sección17.5](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
admite mezcla con precisión igual a la del formato de salida. Es compatible
con un límite numérico de la referencia, sin demostrar su causa particular
ni declarar paridad exacta32/32. El espejo hardware original difiere del
software en cuatro patrones con alpha de framebuffer y tampoco es autoridad
única. La prueba nueva usa32 esperados del modelo, con el caso201 explicado.

Negativos contra la LIB full108: CPU0/2, exit2, GPU0/4, exit4. Candidato
CPU2/2 y GPU4/4, exit0, OpenGL efectivo comprobado sin fallback. La suite
incremental normal termina622/622, exit0,13,871s. La primera suite encontró
seis fallos en controles que pretendían copiar RGB con alpha bajo y MMOD0;
siete fixtures de copia/stride/latch ahora solicitan MMOD1/ALP255 de forma
explícita, conservando sus expectativas. El control GPU de copia opaca
también requiere esa configuración: modificar un píxel negro para darle
color con alpha0 lo deja negro al mezclar con el fondo. La nueva matriz
cubre la mezcla por separado. No se añaden fallos conocidos. Suite GPU,
full109 obligatorio, menú y contraste funcional del ejecutable pendientes.

Otra sonda de450 filas con PCRTC observado108 coincide HW/SW6/6 y difiere
en CPU/OpenGL con ambas paridades. Los17 registros del mismo burst108 tienen
SMODE1=0 y SYNCV=0; la referencia `isReallyInterlaced()` requiere CMOD y
VFP impar, mientras el port duplica filas a partir de SMODE2. `GsSetCrt`
del runtime configura SMODE2 y todavía no modela esos registros de
temporización. No se atribuye la diferencia a una paridad equivocada ni
se cambia el entrelazado en109: falta establecer la configuración efectiva
de la referencia y la temporización adecuada del port. Estas sondas no
prueban una causa única de la imagen del juego, FPS ni estabilidad.

El cierre posterior de las suites del candidato 109 registra **622/622
normal** (13,523 s) y **666/666 GPU** (46,452 s), exit 0, sin fallos conocidos
ni fallos nuevos. Exe incremental SHA-256
`17e8efbd9a101d171da0f5c55d3792c8e4424286de8528921d1db509e868f0cc`:
objetos CPU/GLSL y pruebas 109, pruebas 107/108, LIB runtime full108 y LIB
anterior de pruebas. No acredita todavía la compilación completa 109.
El primer GPU terminó 664/666: sus dos fallos eran el control de copia
mencionado; el ajuste explícito a copia opaca conserva todos los controles
previos de VRAM, fuentes y campos. Los resultados fallidos se conservan.

La sonda pública nueva reprodujo los 32 RGBA del software original de las
sondas privadas, módulo SHA-256
`3ad27ad874ec7e8bcba35396bb2134f158156261d392965978cc43085587db55`, exit 0.
Config válida (cuatro avisos históricos, cero errores), sintaxis de 25 scripts
PowerShell, prueba pad2 y revisión de espacios correctas. Continúan pendientes
full109, menú y contraste funcional del nuevo runner; la PR permanecerá en
borrador hasta cerrar esa validación y el CI del HEAD final.

### 2026-10-14 — Full109 cerrado y título contrastado con la imagen adquirida

La compilación obligatoria mediante `scripts/2_compilar.cmd` terminó con
código 0 en 1.050,312 s. Fuente `3fb71484935a1b6f373eb4f6fa62912250627915`,
109 parches, VU1 compilada, las 501 micromemorias privadas identificadas y
los 13 archivos `programa0..12` compilados y enlazados. Ejecutable de
66.302.976 bytes, SHA-256
`6af0e4c12f4cf5d8061ed478b41ea48c28d93aa52a560456aa499e0cdd02c552`;
LIB del runtime
`4b46443ce6d1f93e006d216b75ebcd554e93437aedc764042063b84e348f361d`.
Manifiesto y resultados privados en `transition109_pcrtc_blend_vu1c_full`.
Los cuatro checks de esa fuente finalizaron en SUCCESS,
[run38552301551](https://github.com/KIexster/god-of-war-recomp/actions/runs/38552301551).
Esto sustituye el estado pendiente de compilación anterior, sin acreditar
todavía el CI del HEAD documental final ni todo el comportamiento del juego.

El menú obligatorio terminó por límite en 115,314 s, controlador PowerShell
0. Tarjeta privada intacta, nueve capturas 512×448 y ninguna firma buscada
de fallo. Dificultad, introducción y barco/Kratos aparecen en las muestras;
primer informe 11 a 70,19 s y último a 90,19 s. Las capturas 1..3 mostraban
fuego/rostro sin opciones, y las 0/7 eran negras. No se atribuye una causa
a esas imágenes. Este script no registró el estado de la ventana y su
cierre por límite no demuestra salida normal ni estabilidad prolongada.

Para revisar el título se ejecutó un control separado con solo Start a 5 s,
pero **las cuatro referencias de ese control reciben la misma ejecución**:
merge DX11 original, shadow OpenGL ligado a la LIB full109, software original
restaurado desde su Freeze/GIF/VSync y textura adquirida por el host.
Duración 180,172 s por límite, proceso 1/controlador 0, tarjeta intacta,
ventana visible sin minimización registrada y estado 3 hasta 170,81 s.
Se revisaron los cuartetos completos 900/904/908/912 y el adquirido 900:
todos muestran el título, «New Game» y «Select». La falta de opciones en
las capturas del script de menú no demuestra una regresión del compositor.

La captura `GowPadTest` llama a `copyLatchedHostPresentationFrame`: en modo
compartido descarga VRAM **actual** con la solicitud CRTC latched. No copia
los píxeles de la textura adquirida por la ventana. Esto limita su uso como
evidencia de lo presentado, sin demostrar por sí solo la causa del fondo
sin opciones en las muestras anteriores. No se ha modificado esa ruta.

Ráfaga de título Freeze899..End915, 41.187.493 bytes, 23.084 registros,
SHA-256 `5a83dee1de5422f044dcca690167ab1b690985f005fbb5a40cd2f4bc0b94e88a`.
Software original restaurado: 17 campos 899..915, código 0, sin historia
inicial de desentrelazado serializada. Los 16 adquiridos son distintos,
se correlacionan exactamente con sus registros, tamaño 512×448 y SMODE2=1.
En 3.305 adquisiciones registradas no hay regresiones de secuencia,
repeticiones inconsistentes ni cambios de tamaño; distancia de ticks 0..1.
Esas cifras no miden latencia ni cuadros únicos por segundo.

Adquirido y shadow siguen sin ser idénticos al mismo tick: la ejecución
compartida no acredita su misma frontera GIF. Tampoco hay igualdad global
shadow/software: en 915 la diferencia absoluta media RGB es
2,7159/1,5433/0,6734, con 194.045 píxeles distintos; DX11/software da
0,7117/0,5857/0,3457. Son estadísticas descriptivas, sin tolerancia global.
El módulo de captura full109 se validó primero con TEXA48, FOG64, restauración
SW frente a referencia fresca y rechazo de End truncado antes de crear GS.
CPU y referencias originales se conservan; no hay `glFinish` global ni
fences retiradas. La PR80 sigue en borrador hasta cerrar escena, combinado
con hilos, revisión local final y CI del HEAD final.

### 2026-10-14 — Escena109: mismo prefijo y comparación separada del host

Control compartido síncrono109, VU1 compilada y ambos hilos apagados:
433,235 s, STOP propio después de End4015 y los 16 HW/shadow/adquiridos.
Proceso1/controlador0, tarjeta intacta, ventana sin minimización registrada
y ninguna firma buscada de fallo. Primer estado11 a55,22 s y último a426,94 s.
No son600 s completos, salida normal ni prueba de estabilidad prolongada.

Freeze3999..End4015:113.832.837 bytes,94.405 registros, SHA-256
`c04ee2bd36a0a907942c8728ba6f79bbf0c75e9e7572ea793562affb1828190f`.
Software original restaura esa entrada y repite17 campos3999..4015, código0.
La historia inicial de desentrelazado no se conserva. Se revisaron los16
tríos completos de merge DX11/software/shadow109 en contactos, más los
cuartetos completos4004/4015 con adquirido. Aparecen HUD, Kratos y enemigos.
El fondo queda cercano a las referencias originales; persisten diferencias
de líneas y pose. PMODE constante0x8023, SMODE2=0x21fed1 y fuentes208,
sin cambiar el entrelazado en109.

En4015, región fija x200..339,y0..79: medias RGB shadow
35,4516/35,3573/30,6859 y software35,2632/35,1795/30,5835. Incluye mástil
y cuerdas; no acredita igualdad global. En todo el campo, diferencia
absoluta media shadow−SW4,0995/3,9323/3,4149 RGB, media firmada
−0,2999/−0,2906/−0,2419, máximo171 y208.847 píxeles distintos.
DX11−SW tiene media absoluta0,6197/0,6157/0,5630. No hay tolerancia global
aceptada ni comparación causal con otra pasada por compartir el tick.

Los16 adquiridos4000..4015,512×448, son distintos y se correlacionan
exactamente con sus registros. En90 adquisiciones registradas no hay
regresiones, repeticiones inconsistentes, retornos A-B-A ni cambios de
tamaño; distancia de ticks0. Ninguno de los16 adquiridos es idéntico al
shadow del mismo tick. En4015 hay216.932 píxeles distintos y media absoluta
11,8596/11,3732/9,6050 RGB. **La frontera GIF del host sigue sin acreditarse**:
ese desacuerdo no demuestra por sí solo un fallo de rasterización o de slots.
Estas pasadas con espejo y readbacks no miden FPS únicos.

La escena cierra un contraste funcional acotado del ejecutable109. El modo
combinado con entradas de ataque sigue en validación; PR80 permanece en
borrador hasta su cierre, revisión final y CI del HEAD documental final.

### 2026-10-14 — Combinado109 con ataques cerrado y revisión local

Presentación compartida, FINISH asíncrono y VU1/frente GS en hilos propios,
confirmados en el registro de arranque:420,056 s por límite, proceso1 y
controlador0. Tarjeta privada intacta, sin minimización registrada ni firmas
buscadas de fallo. Primer informe de estado11 a35,09 s y último a416,33 s;
no aparece estado14 en los informes. El arranque forma parte de los420 s,
por lo que no son420 s íntegros de partida ni estabilidad prolongada.

Guion Start/X y cuadrado desde40 s cada0,75 s durante0,35 s. Los128 cambios
de paquete registrados incluyen ataque/neutral en estado11; su cupo limita
las lecturas conservadas. Se revisaron los16 adquiridos tardíos11999..12040
en contacto y los completos primero/último: HUD y Kratos ejecutando ataques
con arcos de las hojas. Son16 RGBA distintos,512×448, SMODE2=0x21fed1,
correlacionados exactamente con el registro. No se afirma completar una
sección del juego ni equivalencia frente a otra ejecución por compartir tick.

Las4.096 adquisiciones registradas no muestran regresiones, repeticiones
inconsistentes, retornos A-B-A ni cambios de tamaño. Distancia entre ticks
0..4, sin convertirla en tiempo, latencia o FPS únicos. El logger omite
iteraciones que reutilizan la misma secuencia; cero filas repetidas no
descarta reutilización por el host. El proceso se detuvo al alcanzar el
límite: **no se acredita todavía cierre normal**.

Quedan cerrados el full109 obligatorio, menú, negativos/positivos,
referencia procedural, título/escena en fronteras comunes y este control
combinado acotado. La revisión final de fuente y alcance es local, sin
aprobación externa: factor0..255, suma ponderada acotada antes de dividir255,
alpha conservado durante mezcla y normalizado después. Las pruebas de
copia solicitan opacidad explícita y conservan sus expectativas; no se
añaden fallos conocidos. Referencias CPU/DX11/SW y fences preservadas.
La PR80 necesita el CI del HEAD documental final antes de ready/merge.
Siguen abiertos frontera GIF del adquirido, líneas/pose, temporización,
encuadre, desglose de HostWait, cuadros distintos y estabilidad prolongada.

### 2026-10-14 — PCRTC109 fusionado y control de tiempos sin capturas

PR80 fusionada en `ca6bace6eec3f33432229d357d8f6b2bbffe38ae` después de
los cuatro SUCCESS del HEAD final `286267264027b181997e1438a7fd48874d8996ea`,
[run38559090776](https://github.com/KIexster/god-of-war-recomp/actions/runs/38559090776).
La revisión de fuente y evidencia es local, sin aprobación externa.

Otro control usa el mismo runner109 identificado, presentación compartida,
FINISH asíncrono y ambos hilos, pero sin espejo, hash, readback ni diagnóstico
de adquisiciones. `GOW_PERF_DIAG=1`, guion Start/X y ataques, tarjeta copiada.
Termina por límite en 600,164 s, proceso/controlador0. El controlador envía
WM_CLOSE únicamente a la ventana del PID hijo: el proceso sale con código0
en 0,147 s, sin recurrir a terminación forzada. Se acredita **cierre solicitado
de esa ventana**, no final del juego ni salida espontánea. Tarjeta intacta,
sin minimización registrada ni firmas buscadas; estado11 en los informes
35,06..596,22 s. El primer plano cambia durante el control; no fue constante.

119 informes de tiempos; 111 tienen estado11 observado en dos reportes
consecutivos, sin demostrar estado continuo entre muestras. En ellos,
Flip25,32..49,07/s y host9,11..60/s. Diez ventanas t50,17..95,67 registran
host9,11..19,64/s y Flip25,32..41,68/s; cien posteriores t100,69..595,73
registran host55,66..60/s y Flip41..46,6/s. **Son contadores de operaciones,
no cuadros únicos mostrados**. No hay capturas de esta pasada para alinear
su cámara/escena con otro control ni declarar una mejora de rendimiento.

HostWait tiene mediana4.979,41 ms en ventanas de unos5 s, mientras Upload
tiene mediana5,20 ms. El código mide HostWait alrededor de `EndDrawing`
entero: loteGL, swap, limitador y eventos. Ese ámbito también domina las
ventanas de host60/s; no prueba por sí solo espera de GPU, fences o swap.
Los ámbitos de EE/VU/GS/IOP abarcan hilos distintos y no se suman como tiempo
exclusivo global o porcentaje de CPU. Falta separar los componentes de
EndDrawing y contar envíos nuevos/reutilizados sin hash/readback. Este
control de diez minutos no acredita estabilidad prolongada ni todo el juego.

### 2026-10-14 — Referencia de proporción y límites del procesamiento original

Wrapper privado nuevo con `SaveSnapshotToMemory` original y consulta de modo:
compilación0, 16 patrones de aspecto/ventana y tres rechazos correctos.
Repite el mismo Freeze/GIF/VSync de la escena109: los17 campos crudos SW
son idénticos al módulo SW anterior. En4015, Stretch512×448 conserva RGB
exacto; Auto/4:3 producen597×448 y16:9 produce796×448. Se revisaron el
Stretch completo y Auto con ventana640×448. Esto separa proporción de
textura y configuración de presentación del calculador original, sin
establecer todavía el encuadre físico PS2 ni cambiar la cámara del port.

La configuración efectiva del original en esa entrada es VESA, Automatic,
PCRTCAntiBlur activado y `isReallyInterlaced=false`: SMODE1=0, SYNCV=0,
SMODE2=0x21fed1. Otra matriz sintética usa cuatro condiciones CMOD/VFP y
cuatro INT/FFMD, seis campos por instancia nueva. Sus96 campos HW/SW son
RGB idénticos. CPU/SW tienen igual tamaño84/96 y RGB exacto0/96; los dos
modos GPU tienen igual tamaño60/96 y RGB exacto0/96. Las dimensiones cambian
según configuración; no se impone una igualdad512×448 a todos los casos.
Con CMOD2 se reconoce NTSC; solo CMOD2+VFP1 activa reallyInterlaced.
Las diferencias de INT1/FFMD0 persisten con esos registros sintéticos.

También se contrastó PCRTCAntiBlur ON/OFF en instancias SW independientes
del mismo patrón de filas. ON conserva sus colores originales; OFF mezcla
filas adyacentes. GPU mezcla y duplica pares de filas. En el control observado,
los cinco merge originales antes del desentrelazado son RGB idénticos a
GetCurrent después; ambos difieren del shadow. En el campo6, media absoluta
GPU−ON2,6161/6,7411/10,2009 RGB y GPU−OFF2,9174/6,5938/10,0112. No son
tolerancias de escena ni prueba de una causa única. La primera petición de
trace desde1 fue rechazada por su límite start>=2; corregida a Freeze1,
merge2..6/End6, terminó0. Ese rechazo no era un fallo del renderer del juego.
No se modifica bob, paridad, temporización o fences para igualar el oráculo.

### 2026-10-14 — Snapshot y modo efectivo como extensión optativa del módulo

El módulo aislado permite ahora `gow_gs_read_video_info` y
`gow_gs_read_snapshot`. Mantiene ABI1 y los structs/calls anteriores; una DLL
antigua puede carecer de los nuevos exports. Snapshot llama al calculador
original con Stretch/Auto/4:3/16:9, restaura la configuración de aspecto y
conserva el output crudo. Consulta y lectura usan la misma captura; VSync,
reset y cierre invalidan la caché. No cambia el renderer del juego ni requiere
otra compilación de sus archivos generados: es una herramienta aislada.

Compilación local sobre Core original fijado:0; contratos sin dispositivo y
control GPU de dos ciclos:0. Proporciones, bandas, buffers insuficientes,
canarios, hilo propietario, conservación de output y reset con secuencia
repetida comprobados. Al omitir solo invalidación VSync/reset, falla exactamente
el control de caché antigua después de reset, código1; positivo intacto0.
La sonda pública `probe_snapshot_reference.py` obtiene16/16 RGBA idénticos
a la referencia SW privada de aspectos/ventanas. DLL antigua: error explícito
de función ausente antes de crear GS. Resultados/binarios privados, sin assets
publicados. Faltan CI del HEAD final y revisión final de esta extensión antes
de fusionar; no se presenta como corrección ya validada de encuadre del juego.

### 2026-10-14 — Referencia fusionada; opciones públicas de perfil y cierre

PR81 fusionada en `ec7cd43882d8f58c1208ad5828b1e744bc674a8c` después de
los cuatro SUCCESS del HEAD `e71ad9c32ac5634ee1062b98247488e3368944ba`,
[run38562439264](https://github.com/KIexster/god-of-war-recomp/actions/runs/38562439264).
Compilación aislada, contratos, GPU, negativo de caché y sonda anteriores
cierran esa extensión de referencia; revisión local, sin aprobación externa.

El lanzador `run_transition_control.py` expone ahora `--gpu-profile`,
`--gpu-stats` y `--request-window-close`, registradas en measurement/result.
Las dos primeras activan flags ya existentes; no añaden temporizadores al
runtime. La tercera envía WM_CLOSE solo a una ventana visible del PID del
hijo creado por Popen, sin activar ventanas. Si no hay ventana, falla el
envío o el hijo no termina en15s, registra el fallback y usa el handle de
ese mismo hijo para detenerlo. Un hijo ya cerrado conserva su código y no
consulta ventanas. Se mantienen los valores predeterminados anteriores.

26 pruebas propias Windows y91 Python completas pasan0: incluyen selección
del HWND propio, exclusión de ventanas ajenas, conservación del error17,
timeout/sin ventana, saneado de flags heredadas y metadatos. Control real de
75,323s con las tres opciones, runner109 sin recompilar: proceso/controlador0,
WM_CLOSE0en0,303s sin fallback, tarjeta intacta, sin minimización ni firmas
buscadas. Estado11 en35,08..70,23; no hay imágenes de esa pasada ni prueba de
estabilidad prolongada o FPS mostrados. Falta CI del HEAD final del lanzador.

### 2026-10-14 — Timestamps GPU109: raster y denominador comprobados

Control privado anterior de240,172s, mismo ejecutable109, sin espejo/hash/
readbacks, con perfil CPU y GPU y entradas de ataque: proceso/controlador0,
cierre solicitado de ventana0en0,159s sin forzar. Tarjeta intacta, sin
minimización registrada ni firmas buscadas; estado11 en35,09..235,62.
39 ventanas CPU tienen estado11 observado en informes consecutivos, con
Flip27,44..46,97/s y host10,72..60/s. No comparar su velocidad con otra
ejecución por coincidir el guion o tick; no hay referencia visual alineada.

El perfil GPU genera2.613 ventanas:2.612 de cuatro intentos y una al cierre.
Hay ocho avisos de muestras omitidas. En2.083 ventanas de cuatro sin esos
avisos, sin línea de tiempos intercalada y con último estado observado11,
raster registra12,93..103,88ms, mediana19,56; present0,02..0,24ms, mediana0,02.
CLUT tiene mediana0,03ms. Son tiempos de los ámbitos instrumentados por
timestamps, sin cubrir toda la GPU ni representar utilización o tiempo de CPU.

Se comprobó el denominador en fuente: `profFrame/endFrame` se llama después
del DispatchCompute de Present y **antes** de PublishShared. No cuenta Flip,
VSync, adquisiciones o EndDrawing; las ventanas pueden contener trabajo
acumulado entre intentos. El cierre puede recoger queries pendientes con
menos intentos. Las ventanas GPU y CPU carecen de una frontera temporal común
registrada: raster destaca entre los ámbitos medidos, pero no se atribuye
todavía a él toda la espera de EndDrawing ni se derivan FPS físicos.
Los resultados apoyan continuar con el desglose de loteGL/swap/limitador/
eventos y con contadores de secuencias enviadas/reutilizadas. No se modifican
fences, sincronización, bob, paridad o raster para ocultar la diferencia.
