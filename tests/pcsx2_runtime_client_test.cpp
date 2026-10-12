// GOW-Port: runtime -> observador -> cola -> DLL -> PCRTC, sin datos del juego.
#include "../src/gow_gs_pcsx2_client.h"
#include <array>
#include <fstream>
#include <iterator>

#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const auto output=std::filesystem::absolute(std::filesystem::path(argv[2]));
    if(std::filesystem::exists(output) || !std::filesystem::create_directories(output))return 2;
    const auto captures=output/L"capturas_\u00f1";
    PS2Memory memory;if(!memory.initialize())return 1;
    GS gs;gs.init(memory.getGSVRAM(),PS2_GS_VRAM_SIZE,&memory.gs());
    const auto dll=std::filesystem::absolute(std::filesystem::path(argv[1]));
    _wputenv_s(L"GOW_PCSX2_GS_DIAG",L"1");_wputenv_s(L"GOW_PCSX2_GS_DLL",dll.c_str());
    _wputenv_s(L"GOW_PCSX2_GS_OUTPUT",captures.c_str());
    _wputenv_s(L"GOW_PCSX2_GS_CAPTURE_COUNT",L"2");_wputenv_s(L"GOW_PCSX2_GS_CAPTURE_FROM_TICK",L"0");
    _wputenv_s(L"GOW_PCSX2_GS_CAPTURE_EVERY_TICKS",L"1");
    _wputenv_s(L"PS2X_VU1_HILO",L"0");_wputenv_s(L"PS2X_GS_FRENTE",L"0");
    gow_pcsx2_client::configure(gs);if(!gs.hasExternalObserver())return 1;
    std::vector<uint64_t> words{10|(uint64_t(1)<<15)|(uint64_t(1)<<60),0x0e};
    auto ad=[&](uint64_t data,uint64_t reg){words.push_back(data);words.push_back(reg);};
    ad(1,0x1a);ad(uint64_t(1)<<16,0x4c);ad((uint64_t(1)<<32)|0x100,0x4e);
    ad(0,0x18);ad(0,0x47);ad(1,0x46);ad(6,0);
    constexpr uint32_t color=0x80123456;
    ad(color|(uint64_t(0x3f800000)<<32),1);ad(0,5);ad(64*16|(uint64_t(32*16)<<16),5);
    auto draw=[&]{const auto* bytes=reinterpret_cast<const uint8_t*>(words.data());
        for(size_t offset=0;offset<words.size()*8;){
            const auto size=std::min<size_t>(13,words.size()*8-offset);
            gs.processGIFPacket(bytes+offset,uint32_t(size),GifPathId::Path3);offset+=size;
            if(offset==13)gs.writeRegister(0x40,(uint64_t(63)<<16)|(uint64_t(31)<<48));
        }
    };
    auto& regs=memory.gs();
    regs.pmode=1|(uint64_t(1)<<5)|(uint64_t(1)<<7)|(uint64_t(255)<<8);
    regs.smode1=uint64_t(2)<<13;regs.smode2=0;regs.dispfb1=uint64_t(1)<<9;
    regs.display1=(uint64_t(25)<<12)|(uint64_t(3)<<23)|(uint64_t(255)<<32)|(uint64_t(31)<<44);
    draw();gs.notifyExternalVSync(1,0);
    gs.reset();++words[16];draw();gs.notifyExternalVSync(2,0);
    gs.setExternalObserver({}); // Drenar y cerrar la DLL antes de examinar sus archivos.
    unsigned files=0;
    for(const auto& entry:std::filesystem::directory_iterator(captures)){
        if(entry.path().extension()!=".bmp")continue;
        std::ifstream file(entry.path(),std::ios::binary);
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>{});
        if(bytes.size()!=54+8192)return 1;
        const uint8_t red=entry.path().filename().string().find("tick_2_")!=std::string::npos?0x57:0x56;
        for(size_t p=54;p<bytes.size();p+=4)
            if(bytes[p]!=0x12 || bytes[p+1]!=0x34 || bytes[p+2]!=red || bytes[p+3]!=0xff)return 1;
        ++files;
    }
    if(files!=2)return 1;
    std::puts("Runtime/DLL: GIF fragmentado, registro directo, VSync, reset, drenaje y 2x2048 RGBA exactos correctos");
    return 0;
}
#else
int main(){return 2;}
#endif
