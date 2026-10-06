# Estado del port: detalle

![Estado](mapa.es.svg)

Generado por `tools/estado/generar.py` a partir de `docs/estado/datos.toml` y `config/funcmap.csv`. No lo edites a mano.

## Librerías del SDK de Sony*: 65.4% (423/647)

| Librería | Funciones | Estado | Nota |
|---|---:|---|---|
| `libmpeg` | 103 | ⏳ Pendiente | Vídeo FMV: sceMpeg* son stubs |
| `libipu` | 5 | ⏳ Pendiente | sceIpuInit corregido (gowIpuInit); el FMV aún no se decodifica |
| `libgraph` | 7 | ✅ Funciona |  |
| `libdma` | 8 | ✅ Funciona |  |
| `libcdvd` | 10 | ✅ Funciona | Lee la ISO original |
| `libdbc` | 11 | ⏳ Pendiente | El SIO2 está emulado, pero los mandos se ven desconectados en él |
| `libpad2` | 13 | 🔧 Parcial | HLE del primer puerto (teclado o gamepad); presiones 0/255 |
| `libvib` | 2 | ⏳ Pendiente | Sin vibración: el HLE no anuncia actuadores |
| `libmc2` | 90 | 🔧 Parcial | SIO2 y tarjeta emulados (Mcd001.ps2), sin verificar con el juego |
| `libscf` | 14 | ✅ Funciona |  |
| `libgcc` | 32 | ✅ Funciona |  |
| `C++ EH` | 34 | ✅ Funciona |  |
| `libm` | 14 | ✅ Funciona |  |
| `libc` | 150 | ✅ Funciona |  |
| `libkernel` | 101 | ✅ Funciona |  |
| `sif` | 24 | ✅ Funciona | Comandos y RPC del SIF |
| `fileio` | 15 | ✅ Funciona |  |
| `loadfile` | 14 | ✅ Funciona | Heap del IOP y carga de módulos |

## Hardware de la PS2: 67.4%

| Grupo | Componente | Peso | Estado | Nota |
|---|---|---:|---|---|
| EE | CPU R5900 (recompilada a C++) | 5 | ✅ Funciona |  |
| EE | Kernel: hilos, semáforos y alarmas | 3 | ✅ Funciona |  |
| EE | FPU (COP1) e instrucciones MMI | 2 | ✅ Funciona |  |
| EE | INTC: VSync e interrupción del GS | 2 | ✅ Funciona |  |
| EE | Controlador DMA | 2 | ✅ Funciona |  |
| EE | Temporizadores | 1 | ✅ Funciona |  |
| GS / VU | GS: primitivas y framebuffer | 3 | ✅ Funciona | CPU de referencia conservado; presentación de campos OpenGL verificada, todavía sin escena 3D completa |
| GS / VU | VIF1 y VU1 | 3 | 🔧 Parcial | MMI y EFU corregidos: modelos pasan Clip y VU1 continúa; persisten deformaciones graves en la escena |
| GS / VU | GS: texturas, CLUT y fuentes | 2 | 🔧 Parcial | Faltan letras en algunos textos |
| GS / VU | GIF (PATH1-3) | 2 | ✅ Funciona |  |
| GS / VU | IPU: vídeo FMV | 2 | ⏳ Pendiente | Se alcanza la carga del FMV, pero se queda esperando a MPEG |
| IOP | CPU R3000A (intérprete) | 3 | ✅ Funciona |  |
| IOP | SPU2: salida de audio | 3 | 🔧 Parcial | SPU2 emulado con salida al PC, sin verificar con el juego; faltan reverb y ADMA |
| IOP | Módulos IRX originales | 2 | ✅ Funciona |  |
| IOP | SIF: RPC y DMA EE ↔ IOP | 2 | ✅ Funciona |  |
| IOP | CDVD: lectura de la ISO original | 2 | ✅ Funciona |  |
| IOP | Mando DualShock 2 | 2 | 🔧 Parcial | libpad2 por HLE (primer puerto); en el SIO2 emulado los mandos se ven desconectados |
| IOP | SIO2: memory card | 2 | 🔧 Parcial | SIO2 y protocolo de la tarjeta emulados; archivo Mcd001.ps2 compatible con PCSX2; sin verificar con el juego |

* Funciones de las librerías estáticas de Sony enlazadas en SCUS_973.99 (config/funcmap.csv). Hardware: ponderado por componente.
