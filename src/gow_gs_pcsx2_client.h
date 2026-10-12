// GOW-Port: diagnóstico espejo opcional. El runtime conserva VRAM, FIFO e IRQ.
#pragma once
#include "gow_gs_pcsx2_api.h"
#include "gow_gs_pcsx2_mirror.h"
#include "gow_gs_pcsx2_queue.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include <charconv>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>

#ifdef _WIN32
// GOW-Port: declaraciones pequeñas para no introducir conflictos windows.h/raylib.
#include "ThreadNaming.h"
extern "C" {
__declspec(dllimport) HMODULE WINAPI LoadLibraryExW(const wchar_t*,HANDLE,unsigned long);
__declspec(dllimport) int WINAPI FreeLibrary(HMODULE);
__declspec(dllimport) unsigned long WINAPI GetLastError(void);
}
#endif
namespace gow_pcsx2_client {
inline bool enabled(const char* name){const char* v=std::getenv(name);return v && !std::strcmp(v,"1");}
inline bool number(const char* name,uint64_t fallback,uint64_t maximum,uint64_t& out){
    out=fallback;const char* text=std::getenv(name);if(!text)return true;
    const auto end=text+std::strlen(text);const auto parsed=std::from_chars(text,end,out);
    return parsed.ec==std::errc{} && parsed.ptr==end && out<=maximum;
}
#ifdef _WIN32
struct Event {
    enum Kind {Gif,Register,VSync,Reset} kind=Gif;
    uint32_t path=0,field=0;
    uint64_t tick=0,value=0;
    std::vector<uint8_t> bytes;
};
class Client final:public GSExternalObserver {
    Queue<Event> queue{64u<<20};
    std::thread worker;
    std::filesystem::path dll,output;
    std::string resources;
    uint64_t fromTick=0,captureCount=0;
    std::atomic<bool> failed{false};
    bool ready=false;
    void fail(const char* operation,uint32_t status=GOW_GS_FAILED) noexcept {
        if(!failed.exchange(true))std::fprintf(stderr,"[gow-pcsx2-gs] fallo op=%s status=%u; espejo detenido\n",operation,status);
        queue.close();
    }
    void send(Event event) noexcept {
        if(failed.load())return;
        try {const auto bytes=event.bytes.size()+sizeof(Event);
            if(!queue.push(std::move(event),bytes))fail("cola");
        } catch(...) {fail("reserva-cola");}
    }
    static bool bitmap(const std::filesystem::path& path,const GowGsFrameInfo& info,const std::vector<uint8_t>& rgba){
        if(info.required_bytes!=uint64_t(info.width)*info.height*4 || info.required_bytes>(64u<<20))return false;
        std::array<uint8_t,54> header{};
        auto put=[&](size_t offset,uint32_t value,unsigned size=4){for(unsigned i=0;i<size;++i)header[offset+i]=uint8_t(value>>(8*i));};
        header[0]='B';header[1]='M';put(2,uint32_t(info.required_bytes+54));put(10,54);put(14,40);
        put(18,info.width);put(22,0u-info.height);put(26,1,2);put(28,32,2);
        std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(header.data()),header.size());
        std::vector<uint8_t> row(info.row_bytes);
        for(uint32_t y=0;y<info.height;++y){
            const auto* p=rgba.data()+size_t(y)*info.row_bytes;
            for(uint32_t x=0;x<info.width;++x){row[x*4]=p[x*4+2];row[x*4+1]=p[x*4+1];row[x*4+2]=p[x*4];row[x*4+3]=p[x*4+3];}
            file.write(reinterpret_cast<const char*>(row.data()),row.size());
        }
        return file.good();
    }
    void run(std::promise<bool> startup) noexcept {
        HMODULE module=nullptr;uint64_t handle=0;decltype(&gow_gs_destroy) destroy=nullptr;
        bool signaled=false;
        uint64_t gifs=0,totalBytes=0,frames=0,registers=0,captures=0,lastCapture=0;
        try {
            ThreadNaming::SetCurrentThreadName("GOW PCSX2 GS espejo");
            module=LoadLibraryExW(dll.c_str(),nullptr,0x1100); // DLL_LOAD_DIR | DEFAULT_DIRS.
            if(!module){std::fprintf(stderr,"[gow-pcsx2-gs] LoadLibrary error=%lu\n",GetLastError());throw std::runtime_error("carga");}
#define GOW_LOAD(name) auto name=reinterpret_cast<decltype(&gow_gs_##name)>(GetProcAddress(module,"gow_gs_" #name)); if(!name)throw std::runtime_error("export " #name)
            GOW_LOAD(abi);GOW_LOAD(create);GOW_LOAD(submit);GOW_LOAD(vsync);GOW_LOAD(read_output);GOW_LOAD(write_register);GOW_LOAD(reset);
#undef GOW_LOAD
            destroy=reinterpret_cast<decltype(destroy)>(GetProcAddress(module,"gow_gs_destroy"));
            if(!destroy || abi()!=GOW_GS_ABI)throw std::runtime_error("ABI");
            const GowGsCreateInfo config{sizeof(config),GOW_GS_ABI,resources.empty()?nullptr:resources.c_str(),640,448,0,0};
            const auto created=create(&config,&handle);
            if(created!=GOW_GS_OK){fail("create",created);throw std::runtime_error("dispositivo");}
            std::fprintf(stderr,"[gow-pcsx2-gs] DX11 espejo abierto; IRQ/FIFO/VRAM siguen en runtime; captura limitada, sin benchmark\n");
            startup.set_value(true);signaled=true;
            Event event;
            while(queue.pop(event) && !failed.load()){
                uint32_t status=GOW_GS_OK;
                if(event.kind==Event::Gif){status=submit(handle,event.path,event.bytes.data(),uint32_t(event.bytes.size()));++gifs;totalBytes+=event.bytes.size();}
                else if(event.kind==Event::Register){status=write_register(handle,event.path,event.value);++registers;}
                else if(event.kind==Event::Reset){status=reset(handle);}
                else {
                    status=vsync(handle,event.field,event.bytes.data(),uint32_t(event.bytes.size()));++frames;
                    if(status==GOW_GS_OK && (frames<=4 || (frames%120)==0 || (captures<captureCount && event.tick>=fromTick && (!captures || event.tick-lastCapture>=120)))){
                        GowGsFrameInfo info{sizeof(info),0,0,0,0,0};
                        const auto query=read_output(handle,nullptr,0,&info);
                        if(query!=GOW_GS_NO_FRAME && query!=GOW_GS_BUFFER_SMALL){fail("consulta",query);break;}
                        if(frames<=4 || (frames%120)==0)std::fprintf(stderr,"[gow-pcsx2-gs] tick=%llu frame=%llu field=%u width=%u height=%u gif=%llu bytes=%llu direct=%llu\n",
                            (unsigned long long)event.tick,(unsigned long long)frames,event.field,info.width,info.height,
                            (unsigned long long)gifs,(unsigned long long)totalBytes,(unsigned long long)registers);
                        if(query==GOW_GS_BUFFER_SMALL && captures<captureCount && event.tick>=fromTick && (!captures || event.tick-lastCapture>=120)){
                            if(info.required_bytes>(64u<<20)){fail("tamano-imagen");break;}
                            std::vector<uint8_t> pixels(size_t(info.required_bytes));
                            const auto read=read_output(handle,pixels.data(),pixels.size(),&info);
                            if(read!=GOW_GS_OK){fail("readback",read);break;}
                            const auto path=output/("frame_tick_"+std::to_string(event.tick)+"_seq_"+std::to_string(info.sequence)+".bmp");
                            if(!bitmap(path,info,pixels)){fail("archivo-imagen");break;}
                            ++captures;lastCapture=event.tick;
                            std::fprintf(stderr,"[gow-pcsx2-gs] captura=%llu tick=%llu seq=%llu width=%u height=%u\n",(unsigned long long)captures,(unsigned long long)event.tick,(unsigned long long)info.sequence,info.width,info.height);
                        }
                    }
                }
                if(status!=GOW_GS_OK){fail("evento",status);break;}
            }
        } catch(const std::exception& e){std::fprintf(stderr,"[gow-pcsx2-gs] error=%s\n",e.what());fail("worker");}
          catch(...){fail("worker");}
        if(!signaled)startup.set_value(false);
        if(handle && destroy){const auto status=destroy(handle);if(status)fail("destroy",status);}
        if(module)FreeLibrary(module);
        std::fprintf(stderr,"[gow-pcsx2-gs] cierre failed=%u gif=%llu bytes=%llu frames=%llu direct=%llu capturas=%llu\n",unsigned(failed.load()),(unsigned long long)gifs,(unsigned long long)totalBytes,(unsigned long long)frames,(unsigned long long)registers,(unsigned long long)captures);
    }
public:
    Client(std::filesystem::path path,std::string resourcePath,std::filesystem::path capturePath,uint64_t from,uint64_t count)
        :dll(std::move(path)),output(std::move(capturePath)),resources(std::move(resourcePath)),fromTick(from),captureCount(count){
        std::promise<bool> startup;auto signal=startup.get_future();
        worker=std::thread([this,p=std::move(startup)]() mutable {run(std::move(p));});ready=signal.get();
    }
    ~Client(){queue.close();if(worker.joinable())worker.join();}
    bool initialized() const {return ready;}
    void gif(GifPathId path,const uint8_t* bytes,uint32_t size) noexcept override {
        if(failed.load())return;
        try{Event e;e.kind=Event::Gif;e.path=uint32_t(path);e.bytes.assign(bytes,bytes+size);send(std::move(e));}
        catch(...){fail("copia-GIF");}
    }
    void reg(uint8_t address,uint64_t value) noexcept override {Event e;e.kind=Event::Register;e.path=address;e.value=value;send(std::move(e));}
    void vsync(const GSRegisters& registers,uint64_t tick,uint32_t field) noexcept override {
        if(failed.load())return;
        try{Event e;e.kind=Event::VSync;e.tick=tick;e.field=field;const auto packed=gow_pcsx2_mirror::pack(registers);e.bytes.assign(packed.begin(),packed.end());send(std::move(e));}
        catch(...){fail("copia-registros");}
    }
    void reset() noexcept override {Event e;e.kind=Event::Reset;send(std::move(e));}
};
#endif
inline void configure(GS& gs){
    if(!enabled("GOW_PCSX2_GS_DIAG"))return;
#ifdef _WIN32
    // GOW-Port: registros snapshot en EE; rechazar trabajadores que podrían adelantarse al VSync.
    const auto threaded=[](const char* key){const char* v=std::getenv(key);return v && v[0]=='1';};
    if(threaded("PS2X_VU1_HILO") || threaded("PS2X_GS_FRENTE")){
        std::fprintf(stderr,"[gow-pcsx2-gs] rechazado: desactivar PS2X_VU1_HILO y PS2X_GS_FRENTE\n");return;
    }
    try{
        const char* dll=std::getenv("GOW_PCSX2_GS_DLL");const char* folder=std::getenv("GOW_PCSX2_GS_OUTPUT");
        const char* resources=std::getenv("GOW_PCSX2_GS_RESOURCES");
        uint64_t from=0,count=0;
        if(!dll || !number("GOW_PCSX2_GS_CAPTURE_FROM_TICK",900,UINT64_MAX,from) || !number("GOW_PCSX2_GS_CAPTURE_COUNT",folder?8:0,16,count))throw std::runtime_error("opciones");
        const auto path=std::filesystem::u8path(dll);
        if(!path.is_absolute() || !std::filesystem::is_regular_file(path))throw std::runtime_error("DLL absoluta requerida");
        std::filesystem::path output;
        if(count){if(!folder)throw std::runtime_error("carpeta privada requerida");output=std::filesystem::u8path(folder);
            if(!output.is_absolute() || std::filesystem::exists(output) || !std::filesystem::create_directories(output))throw std::runtime_error("carpeta nueva absoluta requerida");}
        auto client=std::make_shared<Client>(path,resources?resources:"",output,from,count);
        if(client->initialized()){gs.setExternalObserver(std::move(client));std::fprintf(stderr,"[gow-pcsx2-gs] enlace activo desde arranque; atajos DMA desactivados\n");}
    }catch(const std::exception& e){std::fprintf(stderr,"[gow-pcsx2-gs] configuracion rechazada: %s\n",e.what());}
#else
    (void)gs;std::fprintf(stderr,"[gow-pcsx2-gs] modulo DX11 disponible solo en Windows\n");
#endif
}
}
