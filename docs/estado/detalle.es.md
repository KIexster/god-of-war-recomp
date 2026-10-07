# Estado del port: detalle

![Estado](mapa.es.svg)

Generado por `tools/estado/generar.py` a partir de `docs/estado/datos.toml` y `config/funcmap.csv`. No lo edites a mano.

## Librerías del SDK de Sony*: 65.4% (423/647)

| Librería | Funciones | Estado | Nota |
|---|---:|---|---|
| `libmpeg` | 103 | 🔧 Parcial | La intro FMV se decodifica por software y se presenta sin GOW_SKIP_FMV; falta verificar el resto de vídeos |
| `libipu` | 5 | 🔧 Parcial | sceIpuInit corregido; la ruta MPEG por software funciona, sin verificar todas las operaciones de la IPU |
| `libgraph` | 7 | ✅ Funciona |  |
| `libdma` | 8 | ✅ Funciona |  |
| `libcdvd` | 10 | ✅ Funciona | Lee la ISO original |
| `libdbc` | 11 | 🔧 Parcial | RPC de dbcman y SIO2 verificados al cargar y guardar en tarjeta; los mandos SIO2 aún se ven desconectados |
| `libpad2` | 13 | 🔧 Parcial | HLE del primer puerto (teclado o gamepad); presiones 0/255 |
| `libvib` | 2 | ⏳ Pendiente | Sin vibración: el HLE no anuncia actuadores |
| `libmc2` | 90 | 🔧 Parcial | Listar, cargar, guardar y recargar verificados con una copia de tarjeta PCSX2 de 8 MB con ECC; faltan formateo y reapertura en PCSX2 |
| `libscf` | 14 | ✅ Funciona |  |
| `libgcc` | 32 | ✅ Funciona |  |
| `C++ EH` | 34 | ✅ Funciona |  |
| `libm` | 14 | ✅ Funciona |  |
| `libc` | 150 | ✅ Funciona |  |
| `libkernel` | 101 | ✅ Funciona |  |
| `sif` | 24 | ✅ Funciona | Comandos y RPC del SIF |
| `fileio` | 15 | ✅ Funciona |  |
| `loadfile` | 14 | ✅ Funciona | Heap del IOP y carga de módulos |

## Hardware de la PS2: 60.5%

| Grupo | Componente | Peso | Estado | Nota |
|---|---|---:|---|---|
| EE | CPU R5900 (recompilada a C++) | 5 | ✅ Funciona |  |
| EE | Kernel: hilos, semáforos y alarmas | 3 | ✅ Funciona |  |
| EE | FPU (COP1) e instrucciones MMI | 2 | ✅ Funciona |  |
| EE | INTC: VSync e interrupción del GS | 2 | ✅ Funciona |  |
| EE | Controlador DMA | 2 | ✅ Funciona | Las cadenas fromSPR/toSPR ya copian la paleta de huesos; formas de Kratos y enemigos verificadas en partida |
| EE | Temporizadores | 1 | ✅ Funciona |  |
| GS / VU | GS: primitivas y framebuffer | 3 | 🔧 Parcial | CPU y OpenGL conservados; barco, Kratos, enemigos y HUD visibles tras el arreglo SPR; persisten diferencias CPU/OpenGL y falta certificar cobertura completa |
| GS / VU | VIF1 y VU1 | 3 | 🔧 Parcial | MMI/EFU y cadenas SPR corregidos: modelos y HUD con formas correctas; cobertura aún parcial. Máscaras VU1: 35 % menos tiempo en una repetición con imagen idéntica, sin medir la mejora de FPS globales |
| GS / VU | GS: texturas, CLUT y fuentes | 2 | 🔧 Parcial | HUD visible; bilineal CPU/OpenGL coincide con PCSX2 software en 256 fracciones RGBA; quedan diferencias de feedback y falta verificar todas las fuentes y CLUT |
| GS / VU | GIF (PATH1-3) | 2 | ✅ Funciona |  |
| GS / VU | IPU: vídeo FMV | 2 | 🔧 Parcial | La intro FMV funciona por decodificación MPEG por software; no demuestra emulación completa del hardware IPU |
| IOP | CPU R3000A (intérprete) | 3 | ✅ Funciona |  |
| IOP | SPU2: salida de audio | 3 | 🔧 Parcial | Bancos cargados y sonido continuo en tiempo emulado verificados; a ~2 fps se oye entrecortado; faltan reverb y ADMA |
| IOP | Módulos IRX originales | 2 | ✅ Funciona |  |
| IOP | SIF: RPC y DMA EE ↔ IOP | 2 | ✅ Funciona |  |
| IOP | CDVD: lectura de la ISO original | 2 | ✅ Funciona |  |
| IOP | Mando DualShock 2 | 2 | 🔧 Parcial | libpad2 por HLE (primer puerto); en el SIO2 emulado los mandos se ven desconectados |
| IOP | SIO2: memory card | 2 | 🔧 Parcial | SIO2 y tarjeta de 8 MB con ECC: cargar, guardar y recargar verificados en el juego sobre una copia PCSX2; faltan formateo y reapertura en PCSX2 |

* Funciones de las librerías estáticas de Sony enlazadas en SCUS_973.99 (config/funcmap.csv). Hardware: ponderado por componente.
