# Status do port: detalhes

![Status](mapa.pt-BR.svg)

Gerado por `tools/estado/generar.py` a partir de `docs/estado/datos.toml` e `config/funcmap.csv`. Não edite à mão.

## Bibliotecas do SDK da Sony*: 65.4% (423/647)

| Biblioteca | Funções | Status | Nota |
|---|---:|---|---|
| `libmpeg` | 103 | 🔧 Parcial | Vídeos com libmpeg2 nativo e som: 61 de 67 corretos aqui; falham SEMO08 (deriva), P04MP05 (para) e BONUS20 (sem áudio) |
| `libipu` | 5 | 🔧 Parcial | sceIpuInit corrigido; a rota MPEG por software funciona, mas a cobertura completa das operações da IPU não foi verificada |
| `libgraph` | 7 | ✅ Funciona |  |
| `libdma` | 8 | ✅ Funciona |  |
| `libcdvd` | 10 | ✅ Funciona | Lê os arquivos do disco extraídos ao lado do ELF (.disc_layout); a ISO só com GOW_ISO |
| `libdbc` | 11 | 🔧 Parcial | Serviços nativos de controle e cartão em vez do SIO2 emulado |
| `libpad2` | 13 | 🔧 Parcial | Serviço de controle nativo (teclado e gamepad); pressões 0/255 |
| `libvib` | 2 | ⏳ Pendente | Vibração nativa do runtime do Waffle565 (XInput, DualShock 4/DualSense); não verificada aqui |
| `libmc2` | 90 | 🔧 Parcial | Serviço de memory card nativo (sem SIO2); o Waffle565 verificou carregar e salvar; aqui não repetido após a migração |
| `libscf` | 14 | ✅ Funciona |  |
| `libgcc` | 32 | ✅ Funciona |  |
| `C++ EH` | 34 | ✅ Funciona |  |
| `libm` | 14 | ✅ Funciona |  |
| `libc` | 150 | ✅ Funciona |  |
| `libkernel` | 101 | ✅ Funciona |  |
| `sif` | 24 | ✅ Funciona | Comandos e RPC do SIF |
| `fileio` | 15 | ✅ Funciona |  |
| `loadfile` | 14 | ✅ Funciona | Heap do IOP e carga de módulos |

## Hardware do PS2: 60.5%

| Grupo | Componente | Peso | Status | Nota |
|---|---|---:|---|---|
| EE | CPU R5900 (recompilada para C++) | 5 | ✅ Funciona |  |
| EE | Kernel: threads, semáforos e alarmes | 3 | ✅ Funciona |  |
| EE | FPU (COP1) e instruções MMI | 2 | ✅ Funciona |  |
| EE | INTC: VSync e interrupção do GS | 2 | ✅ Funciona |  |
| EE | Controlador DMA | 2 | ✅ Funciona | Serviço DMA nativo: as cadeias vão direto para VIF1/VU1 e o GS, sem modelo do controlador; buffers reutilizáveis |
| EE | Temporizadores | 1 | ✅ Funciona | Relógio nativo do EE: o recompilador converte as leituras dos temporizadores em chamadas ao relógio |
| GS / VU | GS: primitivas e framebuffer | 3 | 🔧 Parcial | Renderizador OpenGL nativo por render targets (Waffle565), resolução interna 2x; menu, barco, combate e HUD corretos aqui, 4 zonas no Waffle565; falta um percurso completo |
| GS / VU | VIF1 e VU1 | 3 | 🔧 Parcial | VU1 nativa compilada antecipadamente e UNPACK rápido; combate no barco com quadro mediano de 16,7 ms (59,94 Hz) e ~2/3 dos quadros a 60; restam picos quando a VU1 passa de ~15 ms |
| GS / VU | GS: texturas, CLUT e fontes | 2 | 🔧 Parcial | Armazém nativo de texturas e CLUT em vez de um modelo de VRAM; HUD e fontes do menu corretos nas capturas |
| GS / VU | GIF (PATH1-3) | 2 | ✅ Funciona |  |
| GS / VU | IPU: vídeo FMV | 2 | 🔧 Parcial | Sem IPU: o decodificador MPEG nativo pega os dados do PSS; o Waffle565 verificou todos os vídeos com som |
| IOP | CPU R3000A (IRX compilados para C++) | 3 | ✅ Funciona | Os 8 IRX do jogo compilados antecipadamente (opcional, com um dump): partida aos 21 s em vez de 34 s; sem dump, interpretador |
| IOP | SPU2: saída de áudio | 3 | 🔧 Parcial | Motor de som nativo guiado pelas chamadas LIBSD do jogo (Waffle565); aqui falta uma escuta prolongada após a migração |
| IOP | Módulos IRX originais | 2 | ✅ Funciona | Os IRX originais, no interpretador ou compilados para C++ |
| IOP | SIF: RPC e DMA EE ↔ IOP | 2 | ✅ Funciona |  |
| IOP | CDVD: arquivos do disco extraído | 2 | ✅ Funciona | Cada arquivo no seu setor original (.disc_layout); a ISO só com GOW_ISO |
| IOP | Controle DualShock 2 | 2 | 🔧 Parcial | Serviço de controle nativo (teclado e gamepad, sem SIO2); pressões 0/255 |
| IOP | Memory card | 2 | 🔧 Parcial | Serviço nativo (sem SIO2); o Waffle565 verificou carregar e salvar; aqui não repetido após a migração |

* Funções das bibliotecas estáticas da Sony ligadas em SCUS_973.99 (config/funcmap.csv). Hardware: ponderado por componente.
