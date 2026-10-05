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

**Conclusión:** la configuración que define esas variables no se ha cargado, probablemente porque
viene de un archivo del disco (`GODOFWAR.TOC` / `PART*.PAK`). No aparece en los logs ninguna lectura
del disco: ni `sceCdSearchFile` ni `sceCdRead`, tampoco con error.

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

1. Averiguar de dónde carga el juego la configuración (`HERO_HEAP_SIZE`…): buscar la cadena en
   `GODOFWAR.TOC` / `PART*.PAK` y localizar quién rellena `nodo+0x4C`.
2. Comprobar si el juego llega a leer el disco: poner trazas en `sceCdSearchFile` / `sceCdRead` / `sceCdInit`.
3. Si la configuración se carga bien, revisar si el bucle en `0x0023A978` desaparece.
4. HLE para `mc2_d` (memory card) y `ds2u_d` (DualShock 2).
5. Audio sobre `989snd`.
