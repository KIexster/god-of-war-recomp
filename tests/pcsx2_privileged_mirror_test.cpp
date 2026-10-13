// GOW-Port: registros con valores distintos, offsets PS2 y huecos en cero.
#include "../src/gow_gs_pcsx2_mirror.h"
#include <cstdio>
struct Compact {
    uint64_t pmode=1,smode1=2,smode2=3,srfsh=4,synch1=5,synch2=6,syncv=7;
    uint64_t dispfb1=8,display1=9,dispfb2=10,display2=11,extbuf=12,extdata=13,extwrite=14,bgcolor=15;
    std::atomic<uint64_t> csr{0x1122334455667788ull},vsyncTick{0xffffffffffffffffull};
    uint64_t imr=17,busdir=18,siglblid=19;
};
int main(){
    Compact regs;
    const auto bytes=gow_pcsx2_mirror::pack(regs);
    std::array<uint8_t,8192> expected{};
    for(unsigned i=0;i<15;++i)expected[i*16]=uint8_t(i+1);
    const uint8_t csr[]={0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11};
    for(unsigned i=0;i<8;++i)expected[0x1000+i]=csr[i];
    expected[0x1010]=17;expected[0x1040]=18;expected[0x1080]=19;
    if(bytes!=expected){std::fprintf(stderr,"Espejo: offsets/endian/huecos incorrectos\n");return 1;}
    regs.vsyncTick=0;
    if(gow_pcsx2_mirror::pack(regs)!=expected){std::fprintf(stderr,"Espejo: vsyncTick no es registro PS2\n");return 1;}
    std::puts("Espejo: 19 registros PS2, little-endian, huecos en cero, sin vsyncTick");
    return 0;
}
