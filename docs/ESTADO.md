# Estado del proyecto

_Última actualización: 5 de octubre de 2026_

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
| Mando | libpad2 funciona por HLE para el primer puerto; presiones 0/255 y sin vibración. SIO2 sigue pendiente |
| Memory card | `MC2_D.IRX` corre en el IOP, pero el bus SIO2 no está emulado |
| Sin audio | 989snd corre, pero no hay salida de SPU2 |
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
   Próxima comprobación: bit I e interrupciones VIF1. El intérprete actual extrae el opcode
   con `& 0x7F` y no entrega esa interrupción. [SOCOM Unzipped](https://github.com/Scotho/socom-unzipped/blob/main/third_party/ps2recomp/ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp#L15-L20)
   la usa para ordenar uploads de texturas y dibujo. Falta verificar su uso por God of War
   antes de adoptar el mecanismo de pausa/reanudación.
2. Resolver la espera de MPEG (`sceMpegGetPicture`, `0x0018A3D8`) y corregir el renderizado de
   fuentes/3D. `GOW_SKIP_FMV=1` permite investigar la partida mientras el decodificador está pendiente.
3. Memory card (SIO2 / `MC2_D.IRX`).
4. Audio sobre 989snd / SPU2.

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
