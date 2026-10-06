# Estado del proyecto

_Última actualización: 6 de octubre de 2026_

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
- Con `GOW_SKIP_FMV=1`, la prueba de 600 segundos alcanzó el **estado 11 de partida**, con
  `pending=0` y el hilo principal activo. La animación previa de 13,33 s de juego tardó varios minutos
  reales. La imagen de partida permanece negra: todavía no hay gameplay visible verificado.

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
