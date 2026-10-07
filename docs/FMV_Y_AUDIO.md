# FMV y audio (2026-10-07)

## FMV

Sin `GOW_SKIP_FMV`, el juego se quedaba en el estado 4 (`movie=10`) esperando en
`sceMpegGetPicture`: el decodificador recibía datos PSS válidos (`00 00 01 BA`) pero no producía
cuadros. God of War demultiplexa el primer bloque del vídeo (que lleva la cabecera de secuencia
MPEG-2, `00 00 01 B3`) **antes** de llamar a `sceMpegCreate`, y la implementación del runtime
reiniciaba ahí el estado de reproducción. Como el vídeo no repite la cabecera, el decodificador
esperaba una que no llegaba.

`patches/ps2recomp-mpeg-create.patch` conserva la entrada ya demultiplexada del flujo en curso al
crear el decodificador (si aún no se ha servido ninguna imagen y el flujo no ha terminado). La
regresión `sceMpegCreate keeps a sequence header demuxed before it` falla sin el cambio; la suite pasa
**548/548**. En el juego, sin `GOW_SKIP_FMV`, el vídeo de la intro (Kratos en el acantilado) se
decodifica y se presenta.

Aparte: la implementación de `sceMpegCreate`/`sceMpegReset` heredada escribe en direcciones fijas
(`0x1717BC`, `0x171800`…, `0x171904`) que corresponden a otro juego. En SCUS-97399 caen en el código,
así que no parecen afectar al recompilado, pero conviene retirarlas.

## Audio

`patches/ps2recomp-audio-pcm.patch` añade `GOW_AUDIO_PCM=<archivo>`: guarda las muestras emuladas del
SPU2 (s16le, estéreo, 48 kHz) sin los silencios de relleno, para comprobar el audio aunque la
emulación vaya más lenta que el tiempo real.

Resultado: en 23 s de audio emulado (menú, intro y partida) la salida es silencio absoluto. Con una
traza temporal del SPU2: solo hay 48 key-on (inicialización), los volúmenes maestros están a
`0x3FFF` y la RAM de sonido tiene 121 halfwords distintos de cero. Las 27 DMA al SPU2 (27 KB desde
`0x1F9840`–`0x200000` de la RAM del IOP) son de inicialización y llevan ceros. **Ningún banco de
sonido llega a la RAM del SPU2.** Del lado del EE, 989snd envía pocos comandos (inicialización,
`0x4E`, `0x41/0x42/0x4A/0x4B`) y smpd recibe 277 llamadas `SMPD` de tipo `0x13`; no aparece una
carga de banco. Siguiente paso: averiguar qué espera smpd para cargar los bancos (lectura de disco
asíncrona, IRQ del SPU2 o DMA) comparando con la RAM del IOP y `SPU2.bin` de un savestate de PCSX2.
