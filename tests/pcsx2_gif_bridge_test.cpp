// GOW-Port: contratos de transporte de la futura integración GS, sin juego.
#include "../tools/render/pcsx2_gif_bridge.h"
#include <array>
#include <cstdio>
#include <vector>
#include <cstdlib>
using namespace gow_pcsx2_gs;
struct Recorder {
    struct Event { Path path; std::vector<uint8_t> data; };
    std::vector<Event> events;
    bool reject=false,aligned=true,units=true;
    static bool receive(void *opaque,Path path,const uint8_t *data,uint32_t qwc) {
        auto &r=*static_cast<Recorder *>(opaque);
        r.aligned &= (reinterpret_cast<uintptr_t>(data)&15u)==0;
        r.units &= qwc>0 && qwc<=256;
        if(r.reject) return false;
        r.events.push_back({path,{data,data+size_t(qwc)*16}}); return true;
    }
    Sink sink() { return {this,receive}; }
    std::vector<uint8_t> bytes(Path path) const {
        std::vector<uint8_t> result;
        for(const auto &e:events) if(e.path==path) result.insert(result.end(),e.data.begin(),e.data.end());
        return result;
    }
};
static void require(bool value,const char *message) {
    if(!value) { std::fprintf(stderr,"Puente GIF: %s\n",message); std::exit(1); }
}
int main() {
    // Tamaños no múltiplos de QW y origen deliberadamente desalineado.
    alignas(16) std::array<uint8_t,65> seed{};
    for(size_t i=0;i<seed.size();++i) seed[i]=uint8_t(i*37+11);
    const auto *data=seed.data()+1;
    for(unsigned split=0;split<=64;++split) {
        Recorder r; GifBridge bridge(r.sink());
        require(bridge.submit(Path::Path1,data,split),"primera parte");
        require(bridge.submit(Path::Path1,data+split,64-split),"segunda parte");
        require(bridge.complete() && r.bytes(Path::Path1)==std::vector<uint8_t>(data,data+64),"64 bytes intactos para todas las fronteras");
        require(r.aligned && r.units,"alineación y cantidad de quadwords");
    }
    Recorder r; GifBridge bridge(r.sink());
    require(bridge.submit(Path::Path1,data,7) && bridge.pendingBytes(Path::Path1)==7,"fragmento PATH1 retenido");
    require(bridge.submit(Path::Path2,data+16,16),"PATH2 completo");
    require(bridge.submit(Path::Path3,data+32,5),"fragmento PATH3 independiente");
    require(bridge.submit(Path::Path1,data+7,9),"completar PATH1");
    require(bridge.submit(Path::Path3,data+37,11),"completar PATH3");
    require(r.events.size()==3 && r.events[0].path==Path::Path2 && r.events[1].path==Path::Path1 && r.events[2].path==Path::Path3,"orden de quadwords completas, sin mezclar paths");
    require(r.bytes(Path::Path1)==std::vector<uint8_t>(data,data+16) && r.bytes(Path::Path2)==std::vector<uint8_t>(data+16,data+32) && r.bytes(Path::Path3)==std::vector<uint8_t>(data+32,data+48),"cada path conserva sus propios bytes");
    require(bridge.complete(),"todos los fragmentos consumidos");
    require(!bridge.submit(Path(0),data,16) && !bridge.submit(Path(4),data,16) && !bridge.submit(Path::Path1,nullptr,1),"rechazar paths y punteros inválidos");
    require(bridge.submit(Path::Path1,nullptr,0) && bridge.complete(),"vacío válido sin llamada al destino");
    require(bridge.submit(Path::Path1,data,15) && !bridge.complete() && r.events.size()==3,"no rellenar ni emitir un QW incompleto");
    bridge.discardAfterGsReset(); require(bridge.complete(),"reset explícito");
    std::vector<uint8_t> large(32784); for(size_t i=0;i<large.size();++i) large[i]=uint8_t(i*17+3);
    Recorder big; GifBridge bigBridge(big.sink());
    for(size_t i=0;i<large.size();++i) require(bigBridge.submit(Path::Path2,large.data()+i,1),"fragmentación byte a byte");
    require(bigBridge.complete() && big.bytes(Path::Path2)==large && big.units && big.aligned,"flujo grande fragmentado exacto");
    Recorder batched; GifBridge batchedBridge(batched.sink());
    require(batchedBridge.submit(Path::Path3,large.data(),large.size()) && batchedBridge.complete(),"flujo grande contiguo");
    require(batched.bytes(Path::Path3)==large && batched.units && batched.aligned,"lotes acotados sin pérdida o relleno");
    Recorder failed; GifBridge failing(failed.sink()); failed.reject=true;
    require(!failing.submit(Path::Path1,data,16),"rechazo del destino"); failed.reject=false;
    require(!failing.submit(Path::Path1,data,16) && !failing.complete() && failed.events.empty(),"fallo retenido, sin reintentos ambiguos");
    failing.discardAfterGsReset(); require(failing.submit(Path::Path1,data,16) && failing.complete(),"recuperación tras reset coordinado");
    GifBridge absent({}); require(!absent.submit(Path::Path1,data,16) && !absent.complete(),"sin destino no se declara éxito");
    std::puts("Puente GIF: fragmentos, paths, orden, alineación, unidades y errores: OK");
}
