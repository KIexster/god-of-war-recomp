// GOW-Port: espejo privilegiado PS2 para el consumidor GS externo.
// El llamante debe leer registros coherentes en el hilo dueño/sincronizado.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace gow_pcsx2_mirror {
using Bytes=std::array<uint8_t,8192>;
template<class Registers> Bytes pack(const Registers& regs){
    // GSRegisters del runtime es compacto y contiene vsyncTick entre CSR/IMR.
    // Los offsets son de hardware PS2, no del struct anfitrion.
    const uint64_t values[]={regs.pmode,regs.smode1,regs.smode2,regs.srfsh,
        regs.synch1,regs.synch2,regs.syncv,regs.dispfb1,regs.display1,
        regs.dispfb2,regs.display2,regs.extbuf,regs.extdata,regs.extwrite,
        regs.bgcolor,regs.csr.load(std::memory_order_relaxed),regs.imr,
        regs.busdir,regs.siglblid};
    constexpr unsigned offsets[]={0x0000,0x0010,0x0020,0x0030,0x0040,0x0050,
        0x0060,0x0070,0x0080,0x0090,0x00a0,0x00b0,0x00c0,0x00d0,0x00e0,
        0x1000,0x1010,0x1040,0x1080};
    Bytes bytes{};
    for(unsigned i=0;i<19;++i)
        for(unsigned b=0;b<8;++b)
            bytes[offsets[i]+b]=uint8_t(values[i]>>(b*8));
    return bytes;
}
}
