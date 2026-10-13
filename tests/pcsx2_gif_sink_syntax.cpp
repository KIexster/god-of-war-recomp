// GOW-Port: compilar con las cabeceras reales fijadas; no necesita emulador, ventana o juego.
#include "../tools/render/pcsx2_gif_sink.h"
void checkPcsx2GifBinding()
{
    gow_pcsx2_gs::GifBridge bridge(gow_pcsx2_gs::pcsx2TransferSink());
    alignas(16) uint8_t data[16]{};
    bridge.submit(gow_pcsx2_gs::Path::Path1,data,sizeof(data));
    bridge.submit(gow_pcsx2_gs::Path::Path2,data,sizeof(data));
    bridge.submit(gow_pcsx2_gs::Path::Path3,data,sizeof(data));
}
