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
  y `HERO_HEAP_SIZE` / `SLOT_HEAP_SIZE` / `UPGRADE_HEAP_SIZE` se encuentran (la guardia del diccionario
  NULL ya no salta).
- El juego avanza hasta `stdCList<wadCleanupData>` (`0x239FD0`), tras varias llamadas
  `snd_DoExternCall` (cmd `0x4C`).

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

Además, el override de `snd_SendIOPCommandAndWait` ya no descarta los comandos: los registra (`[gow-snd]`)
y llama al original. `GOW_SND_STUB=1` recupera el comportamiento antiguo.

## Herramientas de diagnóstico

| Herramienta | Uso |
|---|---|
| `[gow-snd]` | comandos enviados a 989snd/smpd y su respuesta |
| `[gow-dict]` | llamadas a `GetDynaStringNode` con diccionario NULL (guardia provisional) |
| `[gow-tree]` | ciclos en el árbol de `sub_001769F8` |
| `PS2X_IOP_TRACE=N` (+ `PS2X_IOP_TRACE_FROM=M`) | registra N llamadas a importaciones del IOP a partir de la M |
| `PS2X_IOP_TRACE_NOCLIB=1` | omite `sysclib` en esa traza |
| `PS2X_IOP_TRACE_EVERY=N` | muestreo: una de cada N llamadas |
| `PS2X_IOP_PC_EVERY=N` | PC del IOP cada N instrucciones y aviso cuando no hay hilos listos |

## Problemas conocidos

| Problema | Detalle |
|---|---|
| Bucle en `0x00239FD0` | dentro de `stdCList<wadCleanupData>` (`ra=0x23A044`), tras los `snd_DoExternCall` |
| Errores de 989snd | 15 × `989snd Error: cause 7 -> …` al arrancar (sin investigar) |
| Sin audio | 989snd corre, pero no hay salida de SPU2 |
| Sin vídeo FMV | `sceMpeg*` / `sceIpu*` son stubs |
| Dependencias de ninja | Con MSVC en español no se registran las dependencias `/showIncludes`; `2_recompilar_rapido.cmd` fuerza la recompilación de los overrides (los cambios en cabeceras del runtime requieren tocar los `.cpp` que las incluyen) |

## Próximos pasos

1. Investigar el bucle en `stdCList<wadCleanupData>` (`0x239FD0`) y qué piden los comandos `0x4C`.
2. Comprobar si la guardia `gowDictFindGuard` y el diagnóstico del árbol ya pueden retirarse.
3. Revisar los errores `cause 7` de 989snd.
4. Audio sobre 989snd / SPU2.
