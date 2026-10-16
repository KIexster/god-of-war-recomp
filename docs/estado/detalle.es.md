# Estado del port: detalle

![Estado](mapa.es.svg)

Generado por `tools/estado/generar.py` a partir de `docs/estado/datos.toml` y `config/funcmap.csv`. No lo edites a mano.

## Librerías del SDK de Sony*: 65.4% (423/647)

| Librería | Funciones | Estado | Nota |
|---|---:|---|---|
| `libmpeg` | 103 | 🔧 Parcial | Vídeos con libmpeg2 nativo y sonido (Waffle565 reproduce todos los del disco); aquí se juega con GOW_SKIP_FMV y falta repetirlo |
| `libipu` | 5 | 🔧 Parcial | sceIpuInit corregido; la ruta MPEG por software funciona, sin verificar todas las operaciones de la IPU |
| `libgraph` | 7 | ✅ Funciona |  |
| `libdma` | 8 | ✅ Funciona |  |
| `libcdvd` | 10 | ✅ Funciona | Lee los archivos del disco extraídos junto al ELF (.disc_layout); la ISO solo con GOW_ISO |
| `libdbc` | 11 | 🔧 Parcial | Servicios nativos de mando y tarjeta en lugar del SIO2 emulado |
| `libpad2` | 13 | 🔧 Parcial | Servicio de mando nativo (teclado y gamepad); presiones 0/255 |
| `libvib` | 2 | ⏳ Pendiente | Vibración nativa del runtime de Waffle565 (XInput, DualShock 4/DualSense); sin verificar aquí |
| `libmc2` | 90 | 🔧 Parcial | Servicio de memory card nativo (sin SIO2); Waffle565 verificó cargar y guardar; aquí no repetido tras la migración |
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
| EE | Controlador DMA | 2 | ✅ Funciona | Servicio DMA nativo: las cadenas van directas a VIF1/VU1 y al GS, sin modelo del controlador; búferes reutilizables |
| EE | Temporizadores | 1 | ✅ Funciona | Reloj nativo del EE: el recompilador convierte las lecturas de los temporizadores en llamadas al reloj |
| GS / VU | GS: primitivas y framebuffer | 3 | 🔧 Parcial | Renderizador OpenGL nativo por render targets (Waffle565), resolución interna 2x; menú, barco, combate y HUD correctos aquí, 4 zonas en Waffle565; falta un recorrido completo |
| GS / VU | VIF1 y VU1 | 3 | 🔧 Parcial | VU1 nativa compilada por adelantado (100 % de los bloques en el barco) y UNPACK rápido; barco a ~53-56 flips/s, objetivo 60 |
| GS / VU | GS: texturas, CLUT y fuentes | 2 | 🔧 Parcial | Almacén nativo de texturas y CLUT en lugar de un modelo de VRAM; HUD y fuentes del menú correctos en las capturas |
| GS / VU | GIF (PATH1-3) | 2 | ✅ Funciona |  |
| GS / VU | IPU: vídeo FMV | 2 | 🔧 Parcial | Sin IPU: el decodificador MPEG nativo toma los datos del PSS; Waffle565 verificó todos los vídeos con sonido |
| IOP | CPU R3000A (IRX compilados a C++) | 3 | ✅ Funciona | Los 8 IRX del juego compilados por adelantado (opcional, con un volcado): partida a 21 s en lugar de 34 s; sin volcado, intérprete |
| IOP | SPU2: salida de audio | 3 | 🔧 Parcial | Motor de sonido nativo guiado por las llamadas LIBSD del juego (Waffle565); aquí falta una escucha sostenida tras la migración |
| IOP | Módulos IRX originales | 2 | ✅ Funciona | Los IRX originales, en el intérprete o compilados a C++ |
| IOP | SIF: RPC y DMA EE ↔ IOP | 2 | ✅ Funciona |  |
| IOP | CDVD: archivos del disco extraído | 2 | ✅ Funciona | Cada archivo en su sector original (.disc_layout); la ISO solo con GOW_ISO |
| IOP | Mando DualShock 2 | 2 | 🔧 Parcial | Servicio de mando nativo (teclado y gamepad, sin SIO2); presiones 0/255 |
| IOP | Memory card | 2 | 🔧 Parcial | Servicio nativo (sin SIO2); Waffle565 verificó cargar y guardar; aquí no repetido tras la migración |

* Funciones de las librerías estáticas de Sony enlazadas en SCUS_973.99 (config/funcmap.csv). Hardware: ponderado por componente.
