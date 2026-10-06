// GOW-Port: repetir una captura local del mismo ABI sin archivos del juego.
#include "gow_gs_replay.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <thread>

namespace replay=gow_gs_replay;
namespace {
    bool apply(GSRasterBackend &b,const replay::Record &r,PresentationFrame &frame) {
        using replay::Op;
        GSPrimitiveBatch batch{}; replay::Clut clut{}; GSTransferCommand transfer{};
        replay::Clear clear{}; replay::Pixel pixel{}; GSPresentationRequest present{};
        GSSyncReason sync{}; uint32_t csr=0;
        switch(r.op) {
        case Op::Submit: if(!replay::pod(r,batch) || batch.vertexCount>3) return false; b.Submit(batch); break;
        case Op::Clut: if(!replay::pod(r,clut)) return false; b.LoadClut(clut.tex0,clut.texclut); break;
        case Op::Transfer: if(!replay::pod(r,transfer)) return false; b.BeginTransfer(transfer); break;
        case Op::Upload: b.UploadImage(r.data.data(),uint32_t(r.data.size())); break;
        case Op::TextureFlush: if(!r.data.empty()) return false; b.TextureFlush(); break;
        case Op::Flush: if(!r.data.empty()) return false; b.Flush(); break;
        case Op::Reset: if(!r.data.empty()) return false; b.Reset(); break;
        case Op::Sync: if(!replay::pod(r,sync) || sync>GSSyncReason::Reset) return false; b.Sync(sync); break;
        case Op::Present: if(!replay::pod(r,present)) return false; frame=b.Present(present); break;
        case Op::Clear: if(!replay::pod(r,clear)) return false; if(uint32_t(b.ClearFramebuffer(clear.context,clear.rgba))!=clear.result) return false; break;
        case Op::Write: if(!replay::pod(r,pixel)) return false; b.WriteVram(pixel.psm,pixel.base,pixel.bw,pixel.x,pixel.y,pixel.value); break;
        case Op::Read: if(!replay::pod(r,pixel)) return false; if(b.ReadVram(pixel.psm,pixel.base,pixel.bw,pixel.x,pixel.y)!=pixel.value) return false; break;
        case Op::Mxcsr: if(!replay::pod(r,csr) || (csr&0xffff0000u)) return false; _mm_setcsr(csr); break;
        case Op::Consume: {
            if(r.data.size()<4) return false;
            uint32_t n=0; std::memcpy(&n,r.data.data(),4); if(n>replay::kRecordLimit) return false;
            std::vector<uint8_t> data(n); const auto count=b.ConsumeLocalToHostBytes(data.data(),n);
            if(count!=r.data.size()-4 || std::memcmp(data.data(),r.data.data()+4,count)) return false; break;
        }
        default: return false;
        }
        return true;
    }
    size_t differences(const std::vector<uint8_t> &a,const std::vector<uint8_t> &b,size_t &first) {
        first=SIZE_MAX; if(a.size()!=b.size()) return SIZE_MAX;
        if(a==b) return 0; // La biblioteca compara bloques; evita recorrer cada byte idéntico.
        size_t n=0; for(size_t i=0;i<a.size();++i) if(a[i]!=b[i]) { if(first==SIZE_MAX) first=i; ++n; } return n;
    }
    void ppm(const std::filesystem::path &path,const PresentationFrame &f) {
        if(!f || uint64_t(f.width)*f.height*4!=f.pixels.size()) return;
        std::ofstream out(path,std::ios::binary); out<<"P6\n"<<f.width<<' '<<f.height<<"\n255\n";
        for(size_t i=0;i<f.pixels.size();i+=4) out.write(reinterpret_cast<const char*>(f.pixels.data()+i),3);
    }
}

#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {
#else
int main(int argc,char **argv) {
#endif
    if(argc<3) { std::cerr<<"Uso: repetir_gs captura.bin cpu|compute|hardware [--lockstep] [directorio]\n"; return 2; }
    const std::string mode=std::filesystem::path(argv[2]).string(); const bool gpuMode=mode=="compute" || mode=="hardware";
    if(!gpuMode && mode!="cpu") return 2;
    const bool lockstep=argc>3 && std::filesystem::path(argv[3]).string()=="--lockstep";
    const std::filesystem::path output=argc>4?std::filesystem::path(argv[4]):std::filesystem::path(".");
    replay::Reader reader{std::filesystem::path(argv[1])}; replay::Record r;
    if(!reader.next(r) || r.op!=replay::Op::Initial) { std::cerr<<reader.error<<"; falta estado inicial\n"; return 2; }
    replay::Snapshot initial;
    if(!replay::decodeSnapshot(r,initial)) { std::cerr<<"Estado inicial inválido\n"; return 2; }
    if(initial.state.cachePageBase!=UINT32_MAX) {
        const auto base=initial.state.cachePageBase; const auto size=initial.state.cacheBytes.size();
        if(base%size || uint64_t(base)+size>initial.vram.size()) return 2;
        size_t stale=0; for(size_t i=0;i<size;++i) stale+=initial.state.cacheBytes[i]!=initial.vram[base+i];
        std::cout<<"Cache inicial: base="<<base<<" bytesDistintosDeVRAM="<<stale<<'\n';
        // El importador GPU actual no restaura la página CPU. No falsear la entrada
        // ni presentar como paridad una repetición que comienza con estados distintos.
        if(gpuMode && stale) { std::cerr<<"Capturar después de TEXFLUSH: la página CPU inicial contiene texels anteriores\n"; return 2; }
    }
    GSCpuBackend reference; auto referenceVram=initial.vram;
    reference.Initialize(referenceVram.data(),uint32_t(referenceVram.size())); reference.ImportState(initial.state);
    std::unique_ptr<GSRasterBackend> candidate; GSGpuBackend *gpu=nullptr;
    if(gpuMode) {
        auto backend=std::make_unique<GSGpuBackend>(); gpu=backend.get();
        backend->SetHardwareRasterAllowed(mode=="hardware"); candidate=std::make_unique<GSThreadedBackend>(std::move(backend));
    } else candidate=std::make_unique<GSCpuBackend>();
    auto candidateVram=initial.vram; candidate->Initialize(candidateVram.data(),uint32_t(candidateVram.size()));
    if(gpu && (!gpu->IsReady() || dynamic_cast<GSGpuBackend*>(&static_cast<GSThreadedBackend&>(*candidate).Inner())!=gpu)) {
        std::cerr<<"OpenGL no disponible: fallback no cuenta como comparación\n"; return 2;
    }
    auto *access=dynamic_cast<GSBackendStateAccess*>(candidate.get());
    if(!access || !access->ImportState(initial.state)) return 2;
    struct Restore { unsigned csr=_mm_getcsr(); ~Restore(){_mm_setcsr(csr);} } restore;
    PresentationFrame a,b; size_t index=0,draws=0,firstMismatch=SIZE_MAX,frameMismatch=0; bool ended=false;
    std::vector<uint8_t> actualA,actualB;
    while(reader.next(r)) {
        ++index;
        if(r.op==replay::Op::End) {
            replay::Snapshot final; if(!replay::decodeSnapshot(r,final)) return 2;
            reference.Sync(GSSyncReason::DebugReadback); reference.SnapshotVram(actualA);
            size_t first=0; const auto n=differences(final.vram,actualA,first);
            std::cout<<"CPU vs captura final: bytes="<<n<<" first="<<first<<'\n';
            if(n) return 3;
            GSBackendState state; reference.ExportState(state);
            if(state.clut!=final.state.clut || state.clutCbp!=final.state.clutCbp ||
               state.transferState.x!=final.state.transferState.x || state.transferState.y!=final.state.transferState.y ||
               state.transferState.copiedPixels!=final.state.transferState.copiedPixels ||
               state.upload24.size!=final.state.upload24.size || state.upload24.bytes!=final.state.upload24.bytes ||
               state.localToHost!=final.state.localToHost || state.localToHostReadPos!=final.state.localToHostReadPos) {
                std::cerr<<"El estado CPU final no reproduce la captura\n"; return 3;
            }
            candidate->Sync(GSSyncReason::DebugReadback); candidate->SnapshotVram(actualB);
            const auto m=differences(actualA,actualB,first);
            std::cout<<"CPU vs "<<mode<<" final: bytes="<<m<<" first="<<first<<'\n';
            if(m && firstMismatch==SIZE_MAX) firstMismatch=index;
            ended=true; break;
        }
        if(r.op==replay::Op::Initial || !apply(reference,r,a) || !apply(*candidate,r,b)) {
            std::cerr<<"Fallo en registro "<<index<<" op="<<unsigned(r.op)<<'\n'; return 3;
        }
        if(r.op==replay::Op::Submit) ++draws;
        if(r.op==replay::Op::Present) {
            size_t first=0; const auto n=differences(a.pixels,b.pixels,first);
            if(n || a.width!=b.width || a.height!=b.height) {
                ++frameMismatch;
                if(frameMismatch==1) { ppm(output/"replay_cpu.ppm",a); ppm(output/"replay_candidate.ppm",b); }
            }
        }
        if(lockstep && firstMismatch==SIZE_MAX && (r.op==replay::Op::Submit || r.op==replay::Op::Upload || r.op==replay::Op::Transfer ||
                        r.op==replay::Op::Clear || r.op==replay::Op::Write || r.op==replay::Op::Reset)) {
            reference.Sync(GSSyncReason::DebugReadback); reference.SnapshotVram(actualA);
            candidate->Sync(GSSyncReason::DebugReadback); candidate->SnapshotVram(actualB);
            size_t first=0; const auto n=differences(actualA,actualB,first);
            if(n && firstMismatch==SIZE_MAX) {
                firstMismatch=index;
                std::cout<<"Primera divergencia: registro="<<index<<" op="<<unsigned(r.op)<<" draws="<<draws<<" bytes="<<n<<" first="<<first<<std::endl;
                if(r.op==replay::Op::Submit) {
                    GSPrimitiveBatch batch{}; replay::pod(r,batch);
                    const auto &s=batch.state;
                    std::cout<<std::setprecision(17);
                    std::cout<<"prim="<<unsigned(s.prim.type)<<" tme="<<s.prim.tme<<" fst="<<s.prim.fst<<" psm="<<unsigned(s.context.tex0.psm)
                             <<" frame="<<s.context.frame.fbp<<" test="<<std::hex<<s.context.test<<" alpha="<<s.context.alpha<<std::dec<<'\n';
                    for(unsigned i=0;i<batch.vertexCount;++i) { const auto &v=batch.vertices[i];
                        std::cout<<"v"<<i<<" xy="<<v.x<<','<<v.y<<" z="<<v.z<<" stq="<<v.s<<','<<v.t<<','<<v.q<<" uv="<<v.u<<','<<v.v<<'\n'; }
                }
            }
        }
    }
    if(!reader.error.empty() || !ended) { std::cerr<<"Captura incompleta: "<<reader.error<<'\n'; return 2; }
    ppm(output/"replay_last_cpu.ppm",a); ppm(output/"replay_last_candidate.ppm",b);
    std::cout<<"registros="<<index<<" draws="<<draws<<" framesDistintos="<<frameMismatch<<" primeraDivergencia="<<firstMismatch<<'\n';
    if(gpu) { const auto stats=gpu->GetStats(); std::cout<<"OpenGL batches="<<stats.batches<<" prims="<<stats.prims<<" tiles="<<stats.tiles<<'\n'; }
    return firstMismatch!=SIZE_MAX || frameMismatch?1:0;
}
