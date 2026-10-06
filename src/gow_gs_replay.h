// GOW-Port: diagnóstico opcional para repetir exactamente los comandos del backend GS.
// La idea de capturar estado + comandos procede de Taylor N. Albarnaz / LightVelox,
// PS2Recomp sotc-port ac9efa070638ad3b3accd284de6f898d5ab271d1 (GPL-3.0).
// Implementación propia; formato local del mismo ABI. Los volcados nunca se publican.
#pragma once

#include "runtime/gs/gs_backend_state.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_threaded_backend.h"
#include "runtime/ps2_memory.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <type_traits>
#include <xmmintrin.h>

namespace gow_gs_replay
{
    constexpr uint64_t kLimit = 64ull << 20;
    constexpr uint32_t kRecordLimit = 16u << 20;
    enum class Op : uint8_t { Initial=1, Submit, Clut, Transfer, Upload, TextureFlush,
                             Clear, Consume, Write, Reset, Read, Present, Flush, Sync, Mxcsr, End };
    struct Header { char magic[8]={'G','O','W','G','S','R','1',0};
        uint32_t batchSize=sizeof(GSPrimitiveBatch), requestSize=sizeof(GSPresentationRequest);
        uint32_t stateSize=sizeof(GSBackendState), pointerSize=sizeof(void*); };
    struct FixedState {
        std::array<uint16_t,512> clut{}; std::array<uint32_t,2> clutCbp{};
        uint32_t cachePageBase=UINT32_MAX; std::array<uint8_t,8192> cacheBytes{};
        GSTransferCommand transfer{}; GSTransferSnapshot transferState{};
        GSUpload24State upload24{}; uint64_t localToHostReadPos=0;
        uint32_t localBytes=0, vramBytes=0;
    };
    struct Clut { GSTex0Reg tex0{}; GSTexClutReg texclut{}; };
    struct Clear { GSContext context{}; uint32_t rgba=0, result=0; };
    struct Pixel { uint32_t psm=0,base=0,bw=0,x=0,y=0,value=0; };
    struct Record { Op op{}; std::vector<uint8_t> data; };
    struct Snapshot { GSBackendState state; std::vector<uint8_t> vram; };

    template<class T> void append(std::vector<uint8_t> &out,const T &v) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto *p=reinterpret_cast<const uint8_t*>(&v); out.insert(out.end(),p,p+sizeof(v));
    }
    template<class T> bool pod(const Record &r,T &v) {
        if(r.data.size()!=sizeof(v)) return false;
        std::memcpy(&v,r.data.data(),sizeof(v)); return true;
    }
    inline bool decodeSnapshot(const Record &r,Snapshot &out) {
        FixedState f{};
        if(r.data.size()<sizeof(f)) return false;
        std::memcpy(&f,r.data.data(),sizeof(f));
        if(f.vramBytes!=PS2_GS_VRAM_SIZE || f.localBytes>kRecordLimit ||
           uint64_t(sizeof(f))+f.localBytes+f.vramBytes!=r.data.size() ||
           f.localToHostReadPos>f.localBytes || f.upload24.size>2u) return false;
        auto &s=out.state; s.clut=f.clut; s.clutCbp=f.clutCbp;
        s.cachePageBase=f.cachePageBase; s.cacheBytes=f.cacheBytes;
        s.transfer=f.transfer; s.transferState=f.transferState; s.upload24=f.upload24;
        s.localToHostReadPos=f.localToHostReadPos;
        s.localToHost.assign(r.data.begin()+sizeof(f),r.data.begin()+sizeof(f)+f.localBytes);
        out.vram.assign(r.data.begin()+sizeof(f)+f.localBytes,r.data.end()); return true;
    }
    class Reader {
        std::ifstream file; uint64_t bytes=sizeof(Header); bool ended=false;
    public:
        std::string error;
        explicit Reader(const std::filesystem::path &path):file(path,std::ios::binary) {
            Header expected{},actual{};
            if(!file.read(reinterpret_cast<char*>(&actual),sizeof(actual)) ||
               std::memcmp(&actual,&expected,sizeof(actual))) error="Cabecera o ABI incompatible";
        }
        bool next(Record &out) {
            if(!error.empty() || ended) return false;
            uint8_t op=0; uint32_t size=0;
            if(!file.read(reinterpret_cast<char*>(&op),1) ||
               !file.read(reinterpret_cast<char*>(&size),4)) { error="Volcado incompleto"; return false; }
            if(op<uint8_t(Op::Initial) || op>uint8_t(Op::End) || size>kRecordLimit ||
               bytes+5+size>kLimit) { error="Registro o límite inválido"; return false; }
            bytes+=5+size; out.op=Op(op); out.data.resize(size);
            if(size && !file.read(reinterpret_cast<char*>(out.data.data()),size)) {
                error="Payload incompleto"; return false;
            }
            ended=out.op==Op::End; return true;
        }
    };

    // Se instala únicamente al solicitar GOW_GS_REPLAY_TRACE. Serializa también
    // presentación/readback para que la captura no mezcle comandos de dos hilos.
    class Backend final: public GSRasterBackend, public GSBackendStateAccess {
        std::unique_ptr<GSRasterBackend> inner; PS2Memory *memory;
        std::ofstream file; mutable std::recursive_mutex mutex;
        std::chrono::steady_clock::time_point created=std::chrono::steady_clock::now(),started;
        double after, duration; uint64_t bytes=sizeof(Header); uint32_t mxcsr=UINT32_MAX;
        bool active=false, finished=false;
        bool write(Op op,const void *p,uint32_t n) {
            const auto tag=uint8_t(op); file.write(reinterpret_cast<const char*>(&tag),1);
            file.write(reinterpret_cast<const char*>(&n),4);
            if(n) file.write(reinterpret_cast<const char*>(p),n);
            bytes+=5+n; if(!file) { finished=true; active=false; return false; } return true;
        }
        template<class T> void write(Op op,const T &v) { write(op,&v,sizeof(v)); }
        bool snapshot(Op op) {
            inner->Flush(); inner->Sync(GSSyncReason::DebugReadback);
            Snapshot s; inner->SnapshotVram(s.vram);
            auto *access=dynamic_cast<GSBackendStateAccess*>(inner.get());
            if(!access || !access->ExportState(s.state) || s.vram.size()!=PS2_GS_VRAM_SIZE ||
               s.state.localToHost.size()>kRecordLimit-PS2_GS_VRAM_SIZE-sizeof(FixedState)) return false;
            FixedState f{}; f.clut=s.state.clut; f.clutCbp=s.state.clutCbp;
            f.cachePageBase=s.state.cachePageBase; f.cacheBytes=s.state.cacheBytes;
            f.transfer=s.state.transfer; f.transferState=s.state.transferState;
            f.upload24=s.state.upload24; f.localToHostReadPos=s.state.localToHostReadPos;
            f.localBytes=uint32_t(s.state.localToHost.size()); f.vramBytes=uint32_t(s.vram.size());
            std::vector<uint8_t> payload; append(payload,f);
            payload.insert(payload.end(),s.state.localToHost.begin(),s.state.localToHost.end());
            payload.insert(payload.end(),s.vram.begin(),s.vram.end());
            if(bytes+5+payload.size()>kLimit) return false;
            const bool ok=write(op,payload.data(),uint32_t(payload.size())); file.flush(); return ok;
        }
        bool recording(uint32_t nextSize=0,bool allowStart=false) {
            if(finished) return false;
            const auto now=std::chrono::steady_clock::now();
            if(!active) {
                // El hilo de presentación no debe leer variables que escribe el EE.
                // Solo empezar al recibir sus comandos de dibujo/transferencia.
                if(memory && !allowStart) return false;
                uint32_t state=0;
                if(memory && memory->getRDRAM()) std::memcpy(&state,memory->getRDRAM()+0x29E560u,4);
                if((memory && state!=11) || std::chrono::duration<double>(now-created).count()<after) return false;
                started=now; active=snapshot(Op::Initial);
                if(!active) { finished=true; file.close(); std::fprintf(stderr,"[gow-gs:replay] error de estado inicial\n"); return false; }
                std::fprintf(stderr,"[gow-gs:replay] inicio state=%u\n",state);
            }
            // Reserva espacio para el estado final incluso con readback grande.
            if(std::chrono::duration<double>(now-started).count()>=duration ||
               nextSize>kRecordLimit || bytes+nextSize+32+(kRecordLimit+5)>kLimit) {
                const bool ok=snapshot(Op::End); active=false; finished=true; file.close();
                std::fprintf(stderr,"[gow-gs:replay] fin completo=%u bytes=%llu\n",unsigned(ok),(unsigned long long)bytes);
                return false;
            }
            const uint32_t csr=_mm_getcsr();
            if(csr!=mxcsr) { mxcsr=csr; write(Op::Mxcsr,csr); }
            return !finished;
        }
    public:
        Backend(std::unique_ptr<GSRasterBackend> backend,PS2Memory *mem,
                const std::filesystem::path &path,double delay=150,double seconds=3)
            :inner(std::move(backend)),memory(mem),file(path,std::ios::binary),after(delay),duration(seconds) {
            Header h{}; file.write(reinterpret_cast<const char*>(&h),sizeof(h)); finished=!file;
            if(finished) std::fprintf(stderr,"[gow-gs:replay] no se pudo abrir el archivo\n");
        }
        // Cierre explícito para sondas deterministas, sin esperar al reloj del host.
        bool finish() {
            std::lock_guard lock(mutex); if(!active || finished) return false;
            const bool ok=snapshot(Op::End); active=false; finished=true; file.close(); return ok;
        }
        void Initialize(uint8_t *p,uint32_t n) override { std::lock_guard lock(mutex); inner->Initialize(p,n); }
        void Reset() override { std::lock_guard lock(mutex); if(recording()) write(Op::Reset,nullptr,0); inner->Reset(); }
        void Submit(const GSPrimitiveBatch &v) override { std::lock_guard lock(mutex); if(recording(sizeof(v),true)) write(Op::Submit,v); inner->Submit(v); }
        void LoadClut(const GSTex0Reg &a,const GSTexClutReg &b) override { std::lock_guard lock(mutex); Clut v{a,b}; if(recording(sizeof(v))) write(Op::Clut,v); inner->LoadClut(a,b); }
        void BeginTransfer(const GSTransferCommand &v) override { std::lock_guard lock(mutex); if(recording(sizeof(v),true)) write(Op::Transfer,v); inner->BeginTransfer(v); }
        void UploadImage(const uint8_t *p,uint32_t n) override { std::lock_guard lock(mutex); if(recording(n)) write(Op::Upload,p,n); inner->UploadImage(p,n); }
        void TextureFlush() override { std::lock_guard lock(mutex); if(recording()) write(Op::TextureFlush,nullptr,0); inner->TextureFlush(); }
        void Flush() override { std::lock_guard lock(mutex); if(recording()) write(Op::Flush,nullptr,0); inner->Flush(); }
        void Sync(GSSyncReason v) override { std::lock_guard lock(mutex); if(recording(sizeof(v))) write(Op::Sync,v); inner->Sync(v); }
        PresentationFrame Present(const GSPresentationRequest &v) override { std::lock_guard lock(mutex); if(recording(sizeof(v))) write(Op::Present,v); return inner->Present(v); }
        bool ClearFramebuffer(const GSContext &c,uint32_t rgba) override {
            std::lock_guard lock(mutex); const bool enabled=recording(sizeof(Clear));
            const bool result=inner->ClearFramebuffer(c,rgba); if(enabled) write(Op::Clear,Clear{c,rgba,uint32_t(result)}); return result;
        }
        uint32_t ConsumeLocalToHostBytes(uint8_t *p,uint32_t n) override {
            std::lock_guard lock(mutex); const bool enabled=recording(n<=kRecordLimit-4?n+4:UINT32_MAX);
            const uint32_t count=inner->ConsumeLocalToHostBytes(p,n);
            if(enabled) { std::vector<uint8_t> data; append(data,n); if(p) data.insert(data.end(),p,p+count); write(Op::Consume,data.data(),uint32_t(data.size())); } return count;
        }
        uint32_t ReadVram(uint32_t p,uint32_t b,uint32_t w,uint32_t x,uint32_t y) const override {
            std::lock_guard lock(mutex); auto *self=const_cast<Backend*>(this); const bool enabled=self->recording(sizeof(Pixel));
            const uint32_t value=inner->ReadVram(p,b,w,x,y); if(enabled) self->write(Op::Read,Pixel{p,b,w,x,y,value}); return value;
        }
        void WriteVram(uint32_t p,uint32_t b,uint32_t w,uint32_t x,uint32_t y,uint32_t v) override { std::lock_guard lock(mutex); if(recording(sizeof(Pixel))) write(Op::Write,Pixel{p,b,w,x,y,v}); inner->WriteVram(p,b,w,x,y,v); }
        void SnapshotVram(std::vector<uint8_t> &v) const override { std::lock_guard lock(mutex); inner->SnapshotVram(v); }
        GSTransferSnapshot GetTransferSnapshot() const override { std::lock_guard lock(mutex); return inner->GetTransferSnapshot(); }
        bool ExportState(GSBackendState &v) override { std::lock_guard lock(mutex); auto *a=dynamic_cast<GSBackendStateAccess*>(inner.get()); return a && a->ExportState(v); }
        bool ImportState(const GSBackendState &v) override { std::lock_guard lock(mutex); auto *a=dynamic_cast<GSBackendStateAccess*>(inner.get()); return a && a->ImportState(v); }
    };

    inline void configure(PS2Memory &memory,GS &gs) {
        std::filesystem::path path;
#ifdef _WIN32
        if(const auto *p=_wgetenv(L"GOW_GS_REPLAY_TRACE");p && *p) path=p;
#else
        if(const auto *p=std::getenv("GOW_GS_REPLAY_TRACE");p && *p) path=p;
#endif
        if(path.empty()) return;
        gs.setRasterBackend(std::make_unique<Backend>(GSThreadedBackend::MakeDefault(),&memory,path));
    }
}
