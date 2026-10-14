// GOW-Port: control de la DLL por su ABI, sin acceso a sus globals C++.
#include "../tools/render/pcsx2_probe/module_api.h"
#include "GS/GSRegs.h"
#include "common/RedtapeWindows.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc,char** argv){
    if(argc!=2)return 2;
    HMODULE module=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)return 1;
#define LOAD(name) auto name=reinterpret_cast<decltype(&gow_gs_##name)>(GetProcAddress(module,"gow_gs_" #name)); if(!name)return 1
    LOAD(create);LOAD(destroy);LOAD(submit);LOAD(vsync);LOAD(read_output);LOAD(write_register);LOAD(reset);
    LOAD(read_snapshot);LOAD(read_video_info);
#undef LOAD
    auto require=[](bool value,const char* message){if(!value)std::fprintf(stderr,"DLL GPU: %s\n",message);return value;};
    bool ok=true;
    uint64_t old=0;
    for(unsigned cycle=0;cycle<2 && ok;++cycle){
        GowGsCreateInfo config{sizeof(config),GOW_GS_ABI,nullptr,64,32,0,0};
        uint64_t handle=0;
        if(!(ok=require(create(&config,&handle)==GOW_GS_OK && handle!=old,"crear handle nuevo")))break;
        uint64_t untouched=0x4321;
        ok&=require(create(&config,&untouched)==GOW_GS_BUSY && untouched==0x4321,"singleton ocupado");
        if(old)ok&=require(submit(old,1,nullptr,0)==GOW_GS_BAD_HANDLE,"rechazar handle cerrado");
        uint32_t threadStatus=GOW_GS_OK;
        std::thread other([&]{threadStatus=submit(handle,1,nullptr,0);});other.join();
        ok&=require(threadStatus==GOW_GS_WRONG_THREAD,"rechazar otro hilo sin consumir datos");
        ok&=require(submit(handle,0,nullptr,0)==GOW_GS_BAD_ARGUMENT,"PATH invalido");
        std::vector<uint64_t> words{11 | (uint64_t(1)<<15) | (uint64_t(1)<<60),0x0e};
        auto ad=[&](uint64_t data,uint64_t reg){words.push_back(data);words.push_back(reg);};
        ad(1,0x1a);ad(uint64_t(1)<<16,0x4c);ad((uint64_t(1)<<32)|0x100,0x4e);
        ad(0,0x18);ad((uint64_t(63)<<16)|(uint64_t(31)<<48),0x40);
        ad(0,0x47);ad(1,0x46);ad(6,0);
        const uint32_t color=0x80123456+cycle;
        ad(color|(uint64_t(0x3f800000)<<32),1);ad(0,5);ad(64*16|(uint64_t(32*16)<<16),5);
        const auto* bytes=reinterpret_cast<const uint8_t*>(words.data());
        for(size_t offset=0;offset<words.size()*8 && ok;){
            const size_t size=std::min<size_t>(13,words.size()*8-offset);
            ok&=require(submit(handle,3,bytes+offset,uint32_t(size))==GOW_GS_OK,"GIF fragmentado");offset+=size;
            if(offset==13)ok&=require(write_register(handle,0x3b,0x80)==GOW_GS_OK,"registro directo con tag PATH3 incompleto");
        }
        alignas(16) GSPrivRegSet regs{};
        regs.PMODE.EN1=1;regs.PMODE.MMOD=1;regs.PMODE.ALP=255;regs.PMODE.SLBG=1;
        regs.SMODE1.CMOD=2;regs.DISP[0].DISPFB.FBW=1;
        regs.DISP[0].DISPLAY.MAGH=3;regs.DISP[0].DISPLAY.DW=255;
        regs.DISP[0].DISPLAY.DH=31;regs.DISP[0].DISPLAY.DY=25;
        ok&=require(vsync(handle,0,reinterpret_cast<const uint8_t*>(&regs),sizeof(regs)-1)==GOW_GS_BAD_ARGUMENT,"tamano de registros invalido");
        ok&=require(vsync(handle,0,reinterpret_cast<const uint8_t*>(&regs),sizeof(regs))==GOW_GS_OK,"VSync original");
        GowGsFrameInfo info{sizeof(info),0,0,0,0,0};
        ok&=require(read_output(handle,nullptr,0,&info)==GOW_GS_BUFFER_SMALL && info.width==64 && info.height==32 && info.required_bytes==8192 && info.sequence==1,"consulta de frame/tamano");
        std::vector<uint32_t> pixels(2049,0xdeadbeef);
        ok&=require(read_output(handle,reinterpret_cast<uint8_t*>(pixels.data()),8191,&info)==GOW_GS_BUFFER_SMALL && pixels[0]==0xdeadbeef,"no escribir buffer pequeno");
        ok&=require(read_output(handle,reinterpret_cast<uint8_t*>(pixels.data()),8192,&info)==GOW_GS_OK,"readback explicito");
        for(unsigned i=0;i<2048;++i)ok&=pixels[i]==(color|0xff000000);
        ok&=require(pixels.back()==0xdeadbeef,"no escribir fuera del frame");
        // GOW-Port: API original de snapshot y modo efectivo, separada de la textura cruda.
        GowGsVideoInfo video{};video.size=sizeof(video);
        ok&=require(read_video_info(handle,&video)==GOW_GS_OK && video.sequence==1 &&
            video.smode1==regs.SMODE1.U64 && video.syncv==regs.SYNCV.U64 && video.smode2==regs.SMODE2.U64 &&
            video.really_interlaced==0 && video.interlace_mode==0 && video.pcrtc_anti_blur==1 && video.reserved==0,
            "modo/opciones/registros originales");
        ok&=require(read_video_info(handle,nullptr)==GOW_GS_BAD_ARGUMENT,"rechazar info nula");
        std::thread foreign([&]{threadStatus=read_video_info(handle,&video);});foreign.join();
        ok&=require(threadStatus==GOW_GS_WRONG_THREAD,"consulta de modo respeta hilo propietario");
        for(uint32_t aspect=0;aspect<=GOW_GS_ASPECT_16_9 && ok;++aspect){
            GowGsFrameInfo shot{sizeof(shot),0,0,0,0,0};
            const uint32_t height=aspect==GOW_GS_ASPECT_STRETCH?32:(aspect==GOW_GS_ASPECT_16_9?36:48);
            ok&=require(read_snapshot(handle,aspect,0,0,nullptr,0,&shot)==GOW_GS_BUFFER_SMALL &&
                shot.width==64 && shot.height==height && shot.required_bytes==64u*height*4 && shot.sequence==1,
                "proporciones originales en resolución interna");
            std::vector<uint32_t> rgba(64u*height+1,0xdeadbeef);
            ok&=require(read_snapshot(handle,aspect,0,0,reinterpret_cast<uint8_t*>(rgba.data()),shot.required_bytes-1,&shot)==
                GOW_GS_BUFFER_SMALL && rgba[0]==0xdeadbeef,"snapshot no escribe buffer insuficiente");
            ok&=require(read_snapshot(handle,aspect,0,0,reinterpret_cast<uint8_t*>(rgba.data()),shot.required_bytes,&shot)==GOW_GS_OK,
                "leer snapshot consultado sin recalcular");
            for(unsigned i=0;i<64u*height;++i)ok&=rgba[i]==(color|0xff000000);
            ok&=require(rgba.back()==0xdeadbeef,"snapshot respeta fin del buffer");
        }
        GowGsFrameInfo shot{sizeof(shot),0,0,0,0,0};
        ok&=require(read_snapshot(handle,GOW_GS_ASPECT_AUTO,80,80,nullptr,0,&shot)==GOW_GS_BUFFER_SMALL &&
            shot.width==80 && shot.height==80,"snapshot conserva tamaño de ventana");
        std::vector<uint32_t> padded(80u*80u);
        ok&=require(read_snapshot(handle,GOW_GS_ASPECT_AUTO,80,80,reinterpret_cast<uint8_t*>(padded.data()),padded.size()*4,&shot)==GOW_GS_OK,
            "snapshot con bandas originales");
        for(unsigned y=0;y<80;++y)for(unsigned x=0;x<80;++x)
            ok&=padded[y*80+x]==(y>=10 && y<70?(color|0xff000000):0u);
        ok&=require(read_snapshot(handle,4,0,0,nullptr,0,&shot)==GOW_GS_BAD_ARGUMENT &&
            read_snapshot(handle,0,80,0,nullptr,0,&shot)==GOW_GS_BAD_ARGUMENT &&
            read_snapshot(handle,0,2049,80,nullptr,0,&shot)==GOW_GS_BAD_ARGUMENT,"rechazar parámetros de snapshot");
        ok&=require(read_output(handle,reinterpret_cast<uint8_t*>(pixels.data()),8192,&info)==GOW_GS_OK,
            "snapshots conservan textura original");
        for(unsigned i=0;i<2048;++i)ok&=pixels[i]==(color|0xff000000);
        ok&=require(vsync(handle,1,reinterpret_cast<const uint8_t*>(&regs),sizeof(regs))==GOW_GS_OK &&
            read_snapshot(handle,GOW_GS_ASPECT_AUTO,80,80,reinterpret_cast<uint8_t*>(padded.data()),padded.size()*4,&shot)==GOW_GS_BAD_ARGUMENT,
            "VSync invalida snapshot antiguo");
        ok&=require(read_snapshot(handle,GOW_GS_ASPECT_AUTO,80,80,nullptr,0,&shot)==GOW_GS_BUFFER_SMALL,"consultar segundo snapshot");
        ok&=require(write_register(handle,0x100,0)==GOW_GS_BAD_ARGUMENT,"rechazar direccion de registro");
        ok&=require(reset(handle)==GOW_GS_OK,"reset sin cerrar instancia");
        ok&=require(vsync(handle,0,reinterpret_cast<const uint8_t*>(&regs),sizeof(regs))==GOW_GS_OK &&
            vsync(handle,1,reinterpret_cast<const uint8_t*>(&regs),sizeof(regs))==GOW_GS_OK &&
            read_snapshot(handle,GOW_GS_ASPECT_AUTO,80,80,reinterpret_cast<uint8_t*>(padded.data()),padded.size()*4,&shot)==GOW_GS_BAD_ARGUMENT,
            "reset/VSync no reutilizan caché anterior aunque vuelva la secuencia");
        ok&=require(destroy(handle)==GOW_GS_OK && destroy(handle)==GOW_GS_BAD_HANDLE,"cierre y handle invalidado");
        old=handle;
    }
    FreeLibrary(module);
    std::puts(ok?"DLL GPU: dos ciclos, contratos y 2048 pixeles RGBA exactos correctos":"DLL GPU: fallo");
    return ok?0:1;
}
