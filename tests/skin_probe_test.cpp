// GOW-Port: datos procedurales; ninguna matriz o estructura proviene del juego.
#include "../src/gow_skin_probe.h"
#include <cstdio>
#include <vector>
using namespace gow_skin_probe;
template<class T> void put(std::vector<uint8_t> &ram,size_t at,T value) {
    std::memcpy(ram.data()+at,&value,sizeof(T));
}
bool test() {
    std::vector<uint8_t> ram(0x5000),scratch(0x4000);
    const Memory mem{ram.data(),scratch.data(),ram.size(),scratch.size()};
    // Alias válidos; último byte de cada región; cruces y direcciones no mapeadas.
    for(uint32_t alias:{0u,0x20000000u,0x80000000u,0xA0000000u}) {
        if(mem.at(alias+0x4FFF,0,1)!=ram.data()+0x4FFF || mem.at(alias+0x4FFF,0,2) ||
           mem.at(alias+0x4FFF,1,1) || mem.at(alias+0x4FFF,0xFFFFFFFFFFFFFFFFull,1)) return false;
    }
    for(uint32_t alias:{0x70000000u,0xF0000000u}) {
        if(mem.at(alias+0x3FFF,0,1)!=scratch.data()+0x3FFF || mem.at(alias+0x3FFF,0,2) ||
           mem.at(alias+0x3FFF,1,1) || mem.at(alias+0x4000,0,1)) return false;
    }
    for(uint32_t bad:{0u,0x02000100u,0x22000100u,0x40000100u,0x60000100u,0xC0000100u})
        if(mem.at(bad,0,1)) return false;
    if(Memory{}.at(0x100,0,1) || Memory{ram.data(),nullptr,ram.size(),0x4000}.at(0x70000000,0,1)) return false;
    constexpr uint32_t sk=0x100,def=0x300,records=0x500,visibility=0x900;
    put<uint16_t>(ram,sk+0x5C,3);
    put<uint32_t>(ram,sk+0x50,0x1000); put<uint32_t>(ram,sk+0x54,visibility);
    put<uint32_t>(ram,sk+0x58,records); put<uint32_t>(ram,sk+0x60,def);
    put<uint32_t>(ram,def+0x10,2); put<uint32_t>(ram,def+0xC,64);
    put<uint32_t>(ram,def+32,5); // Slots 0 y 2: selección dispersa, no dos primeros.
    for(uint32_t i=0;i<3;++i) {
        put<uint32_t>(ram,records+i*16,0x81000000u | (i==1 ? 0x6000u : 0u));
        put<int16_t>(ram,records+i*16+4,int16_t(i)-1);
        put<int16_t>(ram,records+i*16+8,int16_t(32+i));
        put<uint32_t>(ram,records+i*16+12,0x3F800000u+i);
        put<uint32_t>(ram,visibility+i*4,i==2 ? 0u : 1u);
    }
    put<uint32_t>(ram,sk+0x68,2); // Máscara usa índice del record, no índice del slot.
    const auto l=layout(mem,sk);
    if(!l.valid || !l.selectionValid || l.count!=3 || l.selectedCount!=2 ||
       !selected(l,0) || selected(l,1) || !selected(l,2) || selected(l,3)) return false;
    const auto s=slot(mem,sk,l,1);
    if(!s.recordValid || s.flags!=0x81006000u || s.indexField!=0 || s.parent!=33 || !s.maskValid || !s.masked ||
       !s.visibilityValid || s.visibility!=1 || s.threshold!=0x3F800001u) return false;
    if(slot(mem,sk,l,3).recordValid || slot(mem,sk,l,2).masked) return false;
    // Los flags no son el índice ni el umbral; no leer un record incompleto.
    if(slot(mem,sk,l,0).flags!=0x81000000u) return false;
    auto truncated=l; truncated.records=0x4FF1u;
    if(slot(mem,sk,truncated,0).recordValid || slot(mem,sk,truncated,0).flags) return false;
    put<int16_t>(ram,records+8,-1); if(slot(mem,sk,l,0).maskValid) return false;
    put<int16_t>(ram,records+8,256); if(slot(mem,sk,l,0).maskValid) return false;
    Matrix packed{},packedRead{}; uint32_t ordinal=1000;
    for(size_t i=0;i<16;++i) { packed[i]=0x40000000u+uint32_t(i); put<uint32_t>(ram,def+128+i*4,packed[i]); }
    if(!bind(mem,l,2,packedRead,ordinal) || ordinal!=1 || packedRead!=packed || bind(mem,l,1,packedRead,ordinal)) return false;
    auto tooFew=l; tooFew.selectedCount=1; if(bind(mem,tooFew,2,packedRead,ordinal)) return false;
    auto badBind=l; badBind.matrixOffset=0xFFFFFFFFu; if(bind(mem,badBind,2,packedRead,ordinal)) return false;
    put<uint16_t>(ram,sk+0x5C,256); if(!layout(mem,sk).valid) return false;
    put<uint16_t>(ram,sk+0x5C,257); if(layout(mem,sk).valid) return false;
    put<uint16_t>(ram,sk+0x5C,3); put<uint32_t>(ram,def+0xC,31);
    if(layout(mem,sk).selectionValid) return false;
    put<uint32_t>(ram,def+0xC,0xFFFFFFFFu); if(layout(mem,sk).selectionValid) return false;
    Matrix expected{},read{};
    for(size_t i=0;i<16;++i) { expected[i]=0x3F000000u+uint32_t(i); put<uint32_t>(scratch,0x3FC0+i*4,expected[i]); }
    const auto beforeRam=ram,beforeScratch=scratch;
    if(!mem.matrix(0x70003FC0,0,read) || read!=expected || mem.matrix(0x70003FC0,1,read) ||
       mem.matrix(0x70003FC0,0xFFFFFFFFu,read) || ram!=beforeRam || scratch!=beforeScratch) return false;
    Timeline t; const auto now=Timeline::Clock::time_point{};
    // No gastar muestras ni ocupar combinaciones antes de la ventana solicitada.
    Timeline delayed;
    for(uint32_t i=1;i<=200;++i) if(delayed.take(i,2,3,now,2999,3000)) return false;
    if(delayed.total || delayed.items[0].skeleton ||
       delayed.take(1,2,3,now,3000,3000)!=1 || delayed.take(1,2,3,now,3001,3000)) return false;
    if(t.take(0,1,2,now) || t.take(1,2,3,now)!=1 || t.take(1,2,3,now)!=0 ||
       t.take(1,2,3,now+std::chrono::milliseconds(99)) ||
       t.take(1,2,3,now+std::chrono::milliseconds(100))!=2) return false;
    for(unsigned i=2;i<32;++i) if(!t.take(1,2,3,now+std::chrono::seconds(i))) return false;
    if(t.take(1,2,3,now+std::chrono::seconds(33))) return false;
    for(uint32_t i=2;i<=16;++i) if(!t.take(i,2,3,now)) return false;
    if(t.take(17,2,3,now)) return false;
    Timeline cap;
    for(unsigned i=0;i<128;++i) if(!cap.take(i/32+1,2,3,now+std::chrono::seconds(i))) return false;
    if(cap.take(10,2,3,now+std::chrono::seconds(200))) return false;
    // Original una vez; before/after solo en entrada/retorno real.
    unsigned originals=0,inputs=0,outputs=0;
    uint32_t pc=0x100;
    const auto in=[&]{++inputs;}; const auto out=[&]{++outputs;};
    observe(pc,0x100,0x200,[&]{++originals; pc=0x200;},in,out);
    if(originals!=1 || inputs!=1 || outputs!=1) return false;
    pc=0x100;
    observe(pc,0x100,0x200,[&]{++originals; pc=0x140;},in,out);
    observe(pc,0x100,0x200,[&]{++originals; pc=0x200;},in,out);
    if(originals!=3 || inputs!=2 || outputs!=1) return false;
    pc=0x100;
    observe(pc,0x100,0x200,[&]{++originals; pc=0x500;},in,out);
    if(originals!=4 || inputs!=3 || outputs!=1) return false;
    return true;
}
int main() {
    if(!test()) { std::fputs("skin_probe_test: FAIL\n",stderr); return 1; }
    std::puts("skin_probe_test: OK"); return 0;
}
