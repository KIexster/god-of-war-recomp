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
            // PATH1 directo a Transfer<0>: GSgifTransfer1 espera un anillo de
            // 16 KiB y una dirección, no un span arbitrario de paquetes del port.
            switch(path) {
            case Path::Path1: g_gs_renderer->Transfer<0>(bytes,qwc); break;
            case Path::Path2: g_gs_renderer->Transfer<1>(bytes,qwc); break;
            case Path::Path3: g_gs_renderer->Transfer<2>(bytes,qwc); break;
            default: return false;
            }
            return true;
        }};
    }
}
