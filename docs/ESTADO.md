# Estado del proyecto

_Última actualización: 5 de octubre de 2026_

## Qué funciona

- La compilación completa termina sin errores (~11 min de compilación del juego en un equipo de 8+ núcleos).
- `scripts\2_recompilar_rapido.cmd` recompila solo `src/gow_overrides.cpp` en ~1 minuto.
- `ps2EntryRunner.exe` arranca, inicializa raylib 5.5 / OpenGL 3.3 y abre la ventana (640×448).
- Se aplican los overrides:
  `[gow-override] sceSifInitRpc=1 iWakeupThread=1 sceCdReadDvdDualInfo=1 snd989=1 modbuf=1 treeDiag=1 dictGuard=1`
- El IOP carga por HLE `sio2man`, `dbcman`, `sio2d`, `libsd` y `989nomid`.
- `dbcman` responde `check-version` (`0x310`) y los RPC de `989snd` se contestan en silencio.
- El juego supera el antiguo cuelgue en `0x00176A80` (ver abajo) y avanza hasta `0x0023A978`.

## Investigación: el cuelgue en `0x00176A80`

No era un bucle de espera. `sub_001769F8(raiz, clave)` es la búsqueda en un **árbol binario**
de nodos de 16 bytes guardados en un pool (creado por `sub_00175CD0`, 8000 nodos):

| Campo | Significado |
|---|---|
| `nodo+0x0` | clave (hash) |
| `nodo+0x4` | valor |
| `nodo+0xA` / `nodo+0xC` | hijo izquierdo / derecho (índice u16 en el pool) |
| `*0x29C4BC` | base del pool (`hijo = base + índice*16`) |
| `*0x29C4B4` | nodo centinela (fin de rama) |
| `*0x29C4B8` | handle del pool |

Encima hay un diccionario genérico: `sub_00175890(dicc, clave)`, que se usa a través de
`sub_00175A70` / `sub_00175AB0` y tiene 15 llamadores. Devuelve `valor & 0x7FFFFFFF`, o 0 si la
clave no existe.

Las 36 primeras búsquedas funcionan. La 37.ª llega con **`dicc = NULL`**: la raíz se lee en la
dirección `0x4`, que contiene basura, y esa basura forma un ciclo (`idx 0 ↔ idx 14356`).

El `NULL` sale de `sub_00186710(nodo, clave, &encontrado)`, que busca en una jerarquía de nodos con
el diccionario en `nodo+0x4C` y hasta 6 hijos en `nodo+0x50`. Las claves que se buscan son
variables de configuración:

```
HERO_HEAP_SIZE, SLOT_HEAP_SIZE, UPGRADE_HEAP_SIZE
pila: 0x1BAED8 → 0x1BAC1C → 0x186744 → 0x175AC0   (desde 0x21CA68 ← 0x17A99C ← 0x138DC0 ← main 0x1001C8)
```

**Conclusión:** la configuración que define esas variables no se ha cargado.

## Investigación: de dónde sale la configuración

Las variables están en **`R_PERM.WAD`**, el primer archivo de `PART1.PAK` (sector 0, `0x378530` bytes).
Son registros de tipo `0x18` (constante entera) dentro del grupo `WAD_R_Perm`:

| Variable | Valor |
|---|---|
| `HERO_HEAP_SIZE` | `0x1EC800` |
| `SLOT_HEAP_SIZE` | `0x100000` |
| `UPGRADE_HEAP_SIZE` | `0x14BC00` |

`GODOFWAR.TOC` es una tabla de entradas de 24 bytes con el formato `nombre[12]`, `u32`, `u32 tamaño`,
`u32 sector`.

El juego no lee el disco directamente: en su libcdvd no hay `sceCdRead`. Los nombres `.PAK` / `.TOC`
solo aparecen en **`SMPD_IOP.IRX`** ("smpd file streamer"). Es un módulo del IOP que se registra como
**plugin de 989snd**: importa `snd989`, tiene `HandleInitialisePlugin` y atiende `FindResource`,
`ReadFile`, `ReadResource`, `HandleFrameTick`, streaming de VAG/MPEG… Es uno de los módulos que el
runtime no carga (no tiene proveedor HLE).

Recorrido de la petición de carga (en `sub_001BACC8`, el arranque de la configuración):

```
sub_00185F28(ctx, "R_Perm", 0x10000210)
  → sub_0017AD70 → sub_0026CA18 → sub_0026BF28(cmd 0x68, tamaño, &{a0,a1,a2,a3})   ; envío a 989snd
  → sub_0026B918 → sub_0026C4B8 → sceSifCallRpc(sid 0x123456, rpc 0x4D)            ; lote de comandos
sub_001BE550 / sub_0017A8B8                                                        ; ¿espera/procesa?
sub_001BABE8("HERO_HEAP_SIZE", …)                                                  ; lee la variable
```

### Protocolo con 989snd / smpd (registro del 5 de octubre de 2026)

`sub_0026BF28(cmd, tamaño, datos)` hace `sceSifCallRpc(sid 0x123456, rpc = cmd)`: envía `tamaño`
bytes desde el búfer `0x305640` y recibe 12 bytes en `0x305600`. Devuelve la palabra 1 de la
respuesta.

El comando **`0x68`** es un mensaje para un plugin de 989snd. `datos` apunta a
`{u32 plugin, u32 tipo, u32 len, u32 ptr}` y se envían los 12 primeros bytes más `len` bytes
copiados de `ptr` (máximo 0x200 en total). Para smpd, `plugin = 0x534D5044` (`'SMPD'`).

Comandos que envía el juego al arrancar (override `gowSnd989SendCommand`, etiqueta `[gow-snd]`):

| # | cmd | Contenido | Interpretación |
|---|---|---|---|
| 1 | `0x0` | `{0x30A1C0, 0}` | inicialización de 989snd |
| 2 | `0x68` SMPD tipo `1` | `{0, 0x4533C0, 0x20, 0x453440, 0x280, 1, 0x1FBF20}` | inicializar smpd: direcciones de búferes del EE (¿estado de 0x20 bytes y cola/resultados de 0x280?) |
| 3–17 | `0xA` | `{0}` … `{14}` | 15 comandos de 989snd (¿reservar canales/bancos?) |
| 18 | `0x68` SMPD tipo `0xF` | `"R_Perm"` (16 bytes) + `0x10000210, 0, 1` | **cargar el WAD `R_Perm`** |

smpd escribe los resultados en la memoria del EE con `SIFCopy`, previsiblemente en los búferes que
recibe en el mensaje de tipo 1.

**Causa raíz:** el override `gowSnd989SendCommand` (`0x26BF28`) responde OK al instante y tira todos
los comandos, entre ellos el `0x68` que pide cargar `R_Perm`. Y aunque llegaran al IOP, tampoco hay
nadie que implemente smpd.

**Arreglo provisional** (`gowDictFindGuard` en `src/gow_overrides.cpp`): si `dicc` es NULL,
`sub_00175890` devuelve 0 ("no encontrado") y vuelca la pila. Así se evita el cuelgue, pero los heaps
quedan con tamaño 0.

## Problemas conocidos

| Problema | Detalle |
|---|---|
| Configuración sin cargar | `HERO/SLOT/UPGRADE_HEAP_SIZE` no existen: el nodo de configuración tiene el diccionario a NULL |
| Bucle en `0x0023A978` | `sub_0023A920` recorre una lista circular de objetos llamando a métodos virtuales (`ra=0x1E6004`); probablemente es consecuencia de lo anterior |
| Rutas de IRX | El juego pide `IOP_MOD/xxx.irx`, pero en el disco están en la raíz y en mayúsculas (`SIO2MAN.IRX`) |
| Módulos sin HLE | `mc2_d.irx`, `ds2u_d.irx` y `smpd_iop.irx` (memory card, mando, `smpd`) |
| Sin audio | `989snd` está silenciado |
| Sin vídeo FMV | `sceMpeg*` / `sceIpu*` son stubs |
| Dependencias de ninja | Con MSVC en español no se registran las dependencias `/showIncludes`; `2_recompilar_rapido.cmd` fuerza la recompilación |

## Próximos pasos

1. **HLE de smpd** (el cargador de datos):
   - ~~Registrar en `gowSnd989SendCommand` los comandos y argumentos que envía el juego.~~ ✅
   - Descifrar cómo espera el EE el resultado de `SMPD` tipo `0xF`: qué lee de `0x4533C0` /
     `0x453440`, dónde espera recibir los datos del WAD y cómo los procesa `sub_00185F28` /
     `sub_0017A8B8`.
   - Implementarlo leyendo `GODOFWAR.TOC` + `PART*.PAK` del disco extraído.
2. Con la configuración cargada, quitar la guardia `gowDictFindGuard` y comprobar si el bucle en
   `0x0023A978` desaparece.
4. HLE para `mc2_d` (memory card) y `ds2u_d` (DualShock 2).
5. Audio sobre `989snd`.
