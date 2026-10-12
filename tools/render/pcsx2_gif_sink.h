// GOW-Port: enlace opcional con GSState de PCSX2 2954ebe6965669219f1bbd256e958fbd8f83a85d.
// Compilar dentro del módulo GS aislado, con sus includes y bibliotecas. El GS
// real debe estar abierto; este archivo no crea un dispositivo ni una ventana.
#pragma once
#include "pcsx2_gif_bridge.h"
#include "GS/Renderers/Common/GSRenderer.h"

namespace gow_pcsx2_gs
{
    inline Sink pcsx2TransferSink() {
        return {nullptr,[](void *,Path path,const uint8_t *bytes,uint32_t qwc)->bool {
            if(!g_gs_renderer) return false;
            // PATH1 usa la entrada continua de GSgifTransfer (índice 3). El
            // índice 0 legado corta en EOP y descarta nloop si acaba el span:
            // rompería tanto fragmentos como varios paquetes en un mismo lote.
            // Los índices 1/2 conservan cursores propios para PATH2/3.
            switch(path) {
            case Path::Path1: g_gs_renderer->Transfer<3>(bytes,qwc); break;
            case Path::Path2: g_gs_renderer->Transfer<1>(bytes,qwc); break;
            case Path::Path3: g_gs_renderer->Transfer<2>(bytes,qwc); break;
            default: return false;
            }
            return true;
        }};
    }
}
