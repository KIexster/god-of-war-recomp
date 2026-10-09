// GOW-Port: lectura de matrices con memoria sintética, sin juego ni runtime.
#include "../src/gow_camera_probe.h"
#include <cstdio>
#include <vector>

template<class T> static void put(std::vector<uint8_t> &ram,size_t at,T value) {
    std::memcpy(ram.data()+at,&value,sizeof(value));
}
static bool test() {
    using namespace gow_camera_probe;
    std::vector<uint8_t> ram(0x2000);
    constexpr uint32_t view=0x100,node=0x600,client=0x700;
    put(ram,view+0x360,node); put(ram,node+8,client);
    put<uint16_t>(ram,client+0x60,0xFFFF);
    put<uint64_t>(ram,client+0x68,0x123456789abcdef0ull);
    put<uint64_t>(ram,client+0xF0,0xfedcba9876543210ull);
    const std::array<size_t,6> offsets={view,view+0x40,view+0x100,view+0x240,client+0x20,client+0xB0};
    for(size_t j=0;j<offsets.size();++j) for(size_t i=0;i<16;++i)
        put<uint32_t>(ram,offsets[j]+i*4,uint32_t(0x7fc00000u+j*32+i));
    const auto before=ram;
    const auto s=capture(ram.data(),ram.size(),view);
    if(!s.validView || !s.validNode || !s.validCamera || s.empty || s.joint!=0xFFFF ||
       s.camera!=client || s.matrixTick!=0x123456789abcdef0ull || s.inverseTick!=0xfedcba9876543210ull) return false;
    const std::array<std::array<uint32_t,16>,6> matrices={s.inverse,s.combined,s.projection,s.world,s.clientWorld,s.clientInverse};
    for(size_t j=0;j<matrices.size();++j) for(size_t i=0;i<16;++i)
        if(matrices[j][i]!=uint32_t(0x7fc00000u+j*32+i)) return false;
    if(ram!=before) return false;
    // Alias KSEG conservan los mismos bits, incluidos NaN: nunca se operan floats.
    if(capture(ram.data(),ram.size(),view|0x80000000u).world!=s.world) return false;
    put<uint16_t>(ram,client+0x60,2);
    for(size_t i=0;i<16;++i) put<uint32_t>(ram,client+0x70+i*4,uint32_t(0xabc00000u+i));
    const auto joint=capture(ram.data(),ram.size(),view);
    if(joint.joint!=2) return false;
    for(size_t i=0;i<16;++i) if(joint.clientWorld[i]!=0xabc00000u+i) return false;
    put(ram,view+0x360,view+0x360);
    const auto empty=capture(ram.data(),ram.size(),view);
    if(!empty.validView || !empty.empty || empty.validCamera || empty.validNode) return false;
    const auto aliasSentinel=capture(ram.data(),ram.size(),view|0x80000000u);
    if(aliasSentinel.empty || !aliasSentinel.validNode) return false;
    put(ram,view+0x360,uint32_t(0));
    if(capture(ram.data(),ram.size(),view).validNode) return false;
    put(ram,view+0x360,uint32_t(ram.size()-11));
    if(capture(ram.data(),ram.size(),view).validNode) return false;
    put(ram,view+0x360,node); put(ram,node+8,uint32_t(ram.size()-0x107));
    const auto truncated=capture(ram.data(),ram.size(),view);
    if(!truncated.validNode || truncated.validCamera) return false;
    for(uint32_t bad:{0u,0x1c51u,0xfffffff0u})
        if(capture(ram.data(),ram.size(),bad).validView) return false;
    if(capture(nullptr,ram.size(),view).validView || capture(ram.data(),0,view).validView) return false;
    if(!capture(ram.data(),ram.size(),0x1c50).validView) return false; // límite exacto
    put(ram,node+8,uint32_t(0x1ef8));
    if(!capture(ram.data(),ram.size(),view).validCamera) return false;
    return true;
}
int main() {
    if(!test()) { std::fprintf(stderr,"Lectura de cámara: fallo\n"); return 1; }
    std::puts("Lectura de cámara: matrices, alias, ausencia, límites y memoria intacta OK");
}
