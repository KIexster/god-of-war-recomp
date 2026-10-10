// GOW-Port: regresión de la transición y lecturas acotadas sin juego.
#include "../src/gow_model_probe.h"
#include <cstdio>
#include <vector>
template<class T> static void put(std::vector<uint8_t> &ram,size_t at,T value) {
    std::memcpy(ram.data()+at,&value,sizeof(value));
}
static bool test() {
    using namespace gow_model_probe;
    PhaseSamples phases;
    for(unsigned i=1;i<=64;++i) if(phases.take(3)!=i) return false;
    if(phases.take(3) || phases.take(0) || phases.take(4)!=1 || phases.take(11)!=1) return false;
    Timeline timeline;
    const auto t=Timeline::Clock::time_point{};
    // Llenar la tabla del menú no impide registrar nuevos modelos en la intro.
    for(uint32_t i=1;i<=64;++i) if(timeline.take(i,1,3,t)!=1) return false;
    if(timeline.take(65,1,3,t) || timeline.take(65,1,4,t)!=1) return false;
    if(timeline.take(65|0x80000000u,1,4,t+std::chrono::seconds(1))) return false;
    if(timeline.take(65,1,4,t+std::chrono::seconds(2))!=2) return false;
    if(timeline.take(65,2,4,t)!=1 || timeline.take(65,1,11,t)!=1 || timeline.take(0,1,4,t)) return false;
    for(unsigned i=3;i<=32;++i) if(timeline.take(65,1,4,t+std::chrono::seconds(i*2))!=i) return false;
    if(timeline.take(65,1,4,t+std::chrono::hours(1))) return false;
    std::vector<uint8_t> ram(0x2000);
    constexpr uint32_t object=0x100,skeleton=0x300,definition=0x500,joints=0x600;
    put(ram,object+0x104,skeleton); put<uint16_t>(ram,object+0x60,0xffff);
    put<uint64_t>(ram,object+0x68,0xfedcba9876543210ull);
    put(ram,skeleton+0x60,definition); put(ram,skeleton+0x8c,joints);
    put<uint16_t>(ram,skeleton+0x86,2); put<uint32_t>(ram,definition+0x10,3);
    put<uint64_t>(ram,skeleton+0x40,0x123456789abcdef0ull);
    for(size_t i=0;i<16;++i) {
        put<uint32_t>(ram,object+0x20+i*4,uint32_t(0x7fc00000u+i));
        put<uint32_t>(ram,object+0x70+i*4,uint32_t(0xabc00000u+i));
        put<uint32_t>(ram,joints+128+i*4,uint32_t(0xfab00000u+i));
    }
    const auto before=ram; const auto p=capture(ram.data(),ram.size(),object);
    if(!p.validObject || !p.validSkeleton || !p.validRoot || p.rootAddress!=joints+128 ||
       p.joint!=0xffff || p.rootId!=2 || p.jointCount!=3 ||
       p.objectTick!=0xfedcba9876543210ull || p.skeletonTick!=0x123456789abcdef0ull) return false;
    for(size_t i=0;i<16;++i) if(p.local[i]!=0x7fc00000u+i || p.world[i]!=0xabc00000u+i || p.rootWorld[i]!=0xfab00000u+i) return false;
    if(ram!=before || capture(ram.data(),ram.size(),object|0x80000000u).rootWorld!=p.rootWorld) return false;
    if(!p.firstJoints[0].valid || !p.firstJoints[1].valid || !p.firstJoints[2].valid ||
       p.firstJoints[3].valid || p.firstJoints[2].address!=joints+128 ||
       p.firstJoints[2].world!=p.rootWorld) return false;
    if(capture(ram.data(),joints+128+63,object).firstJoints[2].valid) return false;
    put<uint32_t>(ram,definition+0x10,20);
    const auto many=capture(ram.data(),ram.size(),object);
    if(many.firstJoints.size()!=8 || !many.firstJoints[7].valid || many.firstJoints[7].address!=joints+7*64) return false;
    put<uint32_t>(ram,definition+0x10,0);
    if(capture(ram.data(),ram.size(),object).firstJoints[0].valid) return false;
    put<uint32_t>(ram,definition+0x10,3);
    if(capture(nullptr,ram.size(),object).validObject || capture(ram.data(),ram.size(),0).validObject ||
       capture(ram.data(),object+0x107,object).validObject) return false;
    if(!capture(ram.data(),joints+128+64,object).validRoot || capture(ram.data(),joints+128+63,object).validRoot) return false;
    put<uint16_t>(ram,skeleton+0x86,3); if(capture(ram.data(),ram.size(),object).validRoot) return false;
    put<uint16_t>(ram,skeleton+0x86,2); put<uint32_t>(ram,skeleton+0x8c,0x1ffffff0u);
    if(capture(ram.data(),ram.size(),object).validRoot) return false;
    put<uint32_t>(ram,skeleton+0x8c,0); if(capture(ram.data(),ram.size(),object).validRoot) return false;
    put<uint32_t>(ram,skeleton+0x60,0); if(capture(ram.data(),ram.size(),object).validRoot) return false;
    put<uint32_t>(ram,object+0x104,0); if(capture(ram.data(),ram.size(),object).validSkeleton) return false;
    // La paleta completa alcanza las articulaciones finales y conserva sus bits crudos.
    ram.resize(joints+256*64);
    for(uint32_t i=0;i<256;++i) for(size_t k=0;k<16;++k)
        put<uint32_t>(ram,joints+i*64+k*4,0x7fc00000u+i*16+uint32_t(k));
    const auto paletteBefore=ram;
    uint32_t visited=0;
    auto check=[&](uint32_t i,const Pose::Joint &j) {
        if(i!=visited || !j.valid || j.address!=joints+i*64) visited=1000;
        else {
            for(size_t k=0;k<16;++k) if(j.world[k]!=0x7fc00000u+i*16+uint32_t(k)) {visited=1000; return;}
            ++visited;
        }
    };
    for(uint32_t count:{103u,125u,256u,257u,0u}) {
        visited=0; const auto r=visitPalette(ram.data(),ram.size(),joints|0x80000000u,count,check);
        if(visited!=(count>256u ? 256u : count) || r.read!=visited || r.complete!=(count<=256u)) return false;
    }
    visited=0;
    const auto shortPalette=visitPalette(ram.data(),joints+125*64-1,joints,125,check);
    if(shortPalette.complete || shortPalette.read!=124 || visited!=124) return false;
    auto unused=[](uint32_t,const Pose::Joint &) {};
    if(visitPalette(nullptr,ram.size(),joints,125,unused).complete ||
       visitPalette(ram.data(),ram.size(),0,125,unused).complete ||
       visitPalette(ram.data(),ram.size(),0x1ffffff0u,125,unused).read || ram!=paletteBefore) return false;
    return true;
}
int main() { if(!test()) { std::fputs("model_probe_test: FAIL\n",stderr); return 1; }
    std::puts("model_probe_test: OK"); return 0;
}
