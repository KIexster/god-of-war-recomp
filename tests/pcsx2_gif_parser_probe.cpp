// GOW-Port: solo estado/transferencias del GS original, no es un renderer.
#include "GS/GSState.h"
#include "../tools/render/pcsx2_gif_bridge.h"
#include "../tools/render/pcsx2_register_adapter.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
class Decoder final: public gow_pcsx2_gs::RegisterAdapter<GSState> {
public:
    unsigned draws=0;
    void Draw() override { ++draws; }
    uint64_t rgbaq() const { uint64_t bits=0; std::memcpy(&bits,&m_v.RGBAQ,8); return bits; }
    float q() const {return m_q;}
    void q(float value){m_q=value;}
};
static void require(bool value,const char *label) {
    if(!value){ std::fprintf(stderr,"Parser GS real: %s\n",label); std::exit(1); }
}
static bool receive(void *opaque,gow_pcsx2_gs::Path path,const uint8_t *bytes,uint32_t qwc){
    auto &gs=*static_cast<Decoder *>(opaque);
    switch(path){
    case gow_pcsx2_gs::Path::Path1: gs.Transfer<3>(bytes,qwc); break;
    case gow_pcsx2_gs::Path::Path2: gs.Transfer<1>(bytes,qwc); break;
    case gow_pcsx2_gs::Path::Path3: gs.Transfer<2>(bytes,qwc); break;
    default:return false;
    }
    return true;
}
extern unsigned gsProbeWindowRequests;
int main(){
    alignas(16) std::array<uint8_t,0x2000> privileged{};
    Decoder gs; gs.SetRegsMem(privileged.data());
    gow_pcsx2_gs::GifBridge bridge({&gs,receive});
    const std::array<uint64_t,2> tag{1|(1ull<<15)|(1ull<<60),0xE};
    const std::array<uint64_t,2> a{0x3f80000080706050ull,1};
    const std::array<uint64_t,2> b{0x3f80000080102030ull,1};
    require(bridge.submit(gow_pcsx2_gs::Path::Path1,reinterpret_cast<const uint8_t*>(tag.data()),16),"tag PATH1");
    require(gs.m_path[3].nloop==1,"PATH1 conserva nloop al cortar tras tag");
    gs.q(2.5f);gs.writeDirect(0x3b,0x80);
    require(gs.m_path[3].nloop==1 && gs.q()==2.5f,"registro directo conserva cursor PATH1 y Q");
    require(bridge.submit(gow_pcsx2_gs::Path::Path1,reinterpret_cast<const uint8_t*>(a.data()),16),"payload PATH1");
    require(gs.m_path[3].nloop==0 && gs.rgbaq()==a[0],"registro PATH1 correcto");
    std::array<uint64_t,8> both{tag[0],tag[1],a[0],a[1],tag[0],tag[1],b[0],b[1]};
    require(bridge.submit(gow_pcsx2_gs::Path::Path1,reinterpret_cast<const uint8_t*>(both.data()),sizeof(both)),"dos paquetes con EOP");
    require(gs.rgbaq()==b[0],"EOP no pierde el segundo paquete de un lote");
    require(bridge.submit(gow_pcsx2_gs::Path::Path2,reinterpret_cast<const uint8_t*>(tag.data()),7),"fragmento tag PATH2");
    require(bridge.submit(gow_pcsx2_gs::Path::Path3,reinterpret_cast<const uint8_t*>(tag.data()),16),"tag PATH3 intercalado");
    require(bridge.submit(gow_pcsx2_gs::Path::Path2,reinterpret_cast<const uint8_t*>(tag.data())+7,9),"completar tag PATH2");
    require(gs.m_path[1].nloop==1 && gs.m_path[2].nloop==1,"cursores separados PATH2/3");
    require(bridge.submit(gow_pcsx2_gs::Path::Path2,reinterpret_cast<const uint8_t*>(a.data()),16),"payload PATH2");
    require(gs.rgbaq()==a[0] && gs.m_path[2].nloop==1,"PATH2 no consume PATH3");
    require(bridge.submit(gow_pcsx2_gs::Path::Path3,reinterpret_cast<const uint8_t*>(b.data()),16),"payload PATH3");
    require(gs.rgbaq()==b[0] && bridge.complete(),"PATH3 y transporte completos");
    require(gsProbeWindowRequests==0,"no se abrió dispositivo o ventana");
    require(gs.draws==0,"no se declara dibujo de píxeles en esta prueba");
    std::puts("Parser PCSX2 real: PATH1 fragmentado, EOP múltiples, PATH2/3 independientes y RGBAQ: OK");
}
