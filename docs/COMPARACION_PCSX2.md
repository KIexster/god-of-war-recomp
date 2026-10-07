# Comparación con PCSX2: cadena VIF1 de un cuadro

_6 de octubre de 2026_

## Objetivo

Separar los fallos de VIF1/VU1/GS de los datos que prepara el EE. La herramienta toma la cadena DMA
de VIF1 de un cuadro ya construido por el juego y la ejecuta sobre el VIF1, VU1 y GS del runtime,
sin ejecutar el EE:

- con una cadena de **PCSX2** (savestate), una imagen correcta indica que VIF1/VU1/GS dibujan bien
  esos paquetes;
- con una cadena **del port** (volcado de `GOW_RENDER_DIAG`), la repetición debe reproducir la imagen
  del juego. Si lo hace, los defectos vienen de los datos del EE.

## Uso

```powershell
scripts\compilar_repetir_vif.cmd            # requiere scripts\2_compilar.cmd
python tools\render\extraer_cadena_vif.py --pcsx2 "<sstates>\SCUS-97399 (XXXXXXXX).01.p2s" logs\vif_pcsx2
logs\repetir_cadena_vif.exe logs\vif_pcsx2 logs\vif_pcsx2\imagen
```

Para un volcado del port: ejecutar el juego con `GOW_RENDER_DIAG=1` y, opcionalmente,
`GOW_RENDER_DIAG_DESDE=<segundos>` (vuelca la primera captura del mando en estado 11 a partir de ese
momento; sin la variable, la primera en estado 11). Los volcados quedan junto al ejecutable:

```powershell
python tools\render\extraer_cadena_vif.py --volcado E:\gowport\PS2Recomp\out\build\ps2xRuntime logs\vif_port
logs\repetir_cadena_vif.exe logs\vif_port logs\vif_port\imagen
```

`repetir_cadena_vif` escribe `imagen_antes.ppm` e `imagen_despues.ppm` del framebuffer indicado
(por defecto `FBP=0`, `FBW=8`, PSMCT32, 512×448, el que usa la partida). Todo lo extraído contiene
datos del juego y queda en `logs/`.

### Detalles del formato

- La cadena del cuadro empieza en `0x450C00` (un DIRECT de 15 QW con la configuración del GS y un
  NEXT a la lista) y termina en el END que apunta `D1_TADR`. Se reproduce lo que recibe VIF1 con
  `CHCR.TTE`: los 64 bits altos de cada etiqueta seguidos de su carga, siguiendo CALL/RET.
- En `GS.bin` del savestate (PCSX2 v2.x), la VRAM son los 4 MB anteriores a los últimos `0x54` bytes
  (cuatro `GIFPath` de 20 bytes y `Q`).
- Los registros de VIF1 se toman de `eeHwRegs.bin` (`0x3C00`). VU1 empieza con registros a cero; la
  cadena del cuadro sube el microcódigo y las constantes que usa.
- No se repite la cadena del canal GIF (PATH3): las texturas que el juego sube durante el cuadro no
  se actualizan, así que algunos colores y texturas pueden diferir de la captura de PCSX2.

## Resultados

| Entrada | Resultado |
|---|---|
| PCSX2, Egeo (Kratos en el mástil, savestate `6C2355D5`) | Geometría correcta: barco, mástil, rocas, agua y Kratos en su sitio. Difieren colores (Kratos y el agua), atribuibles a texturas/CLUT no actualizadas por PATH3. |
| PCSX2, Desierto de las Almas Perdidas (`C1CFCB84`) | Escena reconocible y en su sitio, con una neblina más intensa y partículas de fuego con otra textura. |
| PCSX2, Hades (`CBE116C1`) | Kratos y el HUD correctos; faltan las paredes. Sin investigar. |
| Port, partida a los 190 s | La repetición reproduce la imagen del juego (fondo de tablas oscuro y una silueta negra). |

El microcódigo de VU1 que sube el port coincide con el de los savestates en los primeros `0x24D8`
bytes; el resto es una zona que el juego reemplaza durante la partida.

**Conclusión:** con paquetes correctos, VIF1, VU1 y el GS del runtime dibujan la geometría del Egeo
correctamente. Las deformaciones y la oscuridad de la partida vienen de los datos que prepara el EE
(paquetes, matrices o vértices), no de la interpretación de VIF1/VU1. No se descarta un fallo de VU1
que solo aparezca con otros datos.

## Colores de vértice que no se recalculan

Las mallas estáticas de la cadena de PCSX2 están también en la RAM del port, con las mismas
posiciones. En varias, los colores de vértice difieren: en PCSX2 valen `0x80606060` (gris uniforme)
y en el port conservan los valores originales del disco, muy oscuros (`0x80020305`, `0x80030609`...).
Se comprobó en la ISO: el bloque del port coincide byte a byte con los datos del disco, así que el
juego reescribe esos colores durante la partida y en el port esa escritura no ocurre (o escribe lo
mismo). Es una explicación candidata de la escena oscura y de la silueta negra de Kratos; falta
identificar la función del EE que los recalcula.
