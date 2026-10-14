// GOW-Port: módulo aislado opcional, independiente del runtime del juego.
#include "module_api.h"
#include "host_probe.h"
#include "../pcsx2_gif_sink.h"
#include "../pcsx2_register_adapter.h"
#include "GS/GS.h"
#include "GS/Renderers/DX11/GSDevice11.h"
#include "GS/Renderers/HW/GSRendererHW.h"
#include <cstring>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

// GOW-Port: precisión explícita para no heredar silenciosamente Basic de GSOptions.
#ifndef GOW_PCSX2_BLENDING
#error GOW_PCSX2_BLENDING debe fijarse al compilar el módulo
#endif
#define GOW_STRINGIFY_IMPL(value) #value
#define GOW_STRINGIFY(value) GOW_STRINGIFY_IMPL(value)

namespace {
std::mutex gate;
uint64_t nextHandle=0, activeHandle=0, sequence=0;
std::thread::id owner;
bool failed=false;
alignas(16) GSPrivRegSet regs{};
std::unique_ptr<gow_pcsx2_gs::GifBridge> bridge;
// GOW-Port: descarga optativa independiente de la textura cruda y de la presentación del juego.
std::vector<uint32_t> snapshotPixels;
uint32_t snapshotWidth=0,snapshotHeight=0,snapshotAspect=0,snapshotWindowWidth=0,snapshotWindowHeight=0;
uint64_t snapshotHandle=0,snapshotSequence=0;
bool snapshotValid=false;
// GOW-Port: ambos cursores son de este ABI; una entrada posterior no se atribuye al último VSync.
GowGsStreamInfo streamInfo{};
bool vsyncQwordAligned=false;

struct SnapshotAspectGuard {
    AspectRatioType gsAspect=GSConfig.AspectRatio,emuAspect=EmuConfig.CurrentAspectRatio;
    float customAspect=EmuConfig.CurrentCustomAspectRatio;
    ~SnapshotAspectGuard(){GSConfig.AspectRatio=gsAspect;EmuConfig.CurrentAspectRatio=emuAspect;EmuConfig.CurrentCustomAspectRatio=customAspect;}
};

uint32_t access(uint64_t handle){
    if(!activeHandle || handle!=activeHandle)return GOW_GS_BAD_HANDLE;
    if(owner!=std::this_thread::get_id())return GOW_GS_WRONG_THREAD;
    return failed?GOW_GS_FAILED:GOW_GS_OK;
}
void close(){
    snapshotValid=false;snapshotPixels.clear();
    bridge.reset();
    if(g_gs_renderer){g_gs_renderer->Destroy();g_gs_renderer.reset();}
    else g_texture_cache.reset(); // GOW-Port: constructor HW interrumpido, antes de publicar el renderer.
    if(g_gs_device){g_gs_device->Destroy();g_gs_device.reset();}
    gsProbeCloseWindow();
    activeHandle=0;sequence=0;failed=false;owner={};
}
}

uint32_t gow_gs_abi(){return GOW_GS_ABI;}
uint32_t gow_gs_create(const GowGsCreateInfo* info,uint64_t* handle){
    std::lock_guard lock(gate);
    if(!info || !handle || info->size!=sizeof(*info) || info->abi!=GOW_GS_ABI ||
       info->reserved || info->hidden_window>1 || !info->width || !info->height ||
       info->width>4096 || info->height>4096 || (info->resources && !*info->resources))return GOW_GS_BAD_ARGUMENT;
    if(activeHandle || g_gs_device || g_gs_renderer)return GOW_GS_BUSY;
    try {
        owner=std::this_thread::get_id();
        GSConfig=Pcsx2Config::GSOptions();
        GSConfig.AccurateBlendingUnit=AccBlendLevel::GOW_PCSX2_BLENDING;
        GSConfig.Renderer=GSRendererType::DX11;GSConfig.UpscaleMultiplier=1.0f;
        GSConfig.DisableShaderCache=true;GSConfig.LinearPresent=GSPostBilinearMode::Off;
        GSConfig.LoadTextureReplacements=false;GSConfig.DumpReplaceableTextures=false;
        EmuConfig.CurrentAspectRatio=AspectRatioType::Stretch;
        gsProbeSetSurfaceSize(info->width,info->height);
        static const std::string defaultResources=EmuFolders::Resources;
        EmuFolders::Resources=info->resources?info->resources:defaultResources;
        if(info->hidden_window && !gsProbeOpenWindow(info->width,info->height)){close();return GOW_GS_FAILED;}
        GSVertexSW::InitStatic();
        g_gs_device=std::make_unique<GSDevice11>();
        if(!g_gs_device->Create(GSVSyncMode::Disabled,false)){close();return GOW_GS_FAILED;}
        std::memset(&regs,0,sizeof(regs));
        g_gs_renderer=std::make_unique<gow_pcsx2_gs::RegisterAdapter<GSRendererHW>>();
        g_gs_renderer->SetRegsMem(reinterpret_cast<u8*>(&regs));g_gs_renderer->Reset(true);
        bridge=std::make_unique<gow_pcsx2_gs::GifBridge>(gow_pcsx2_gs::pcsx2TransferSink());
        if(++nextHandle==0)++nextHandle;
        activeHandle=nextHandle;sequence=0;failed=false;*handle=activeHandle;
        streamInfo={};streamInfo.epoch=1;vsyncQwordAligned=false;
        std::fprintf(stderr,"[gow-pcsx2-module] blending=%s; referencia procedural, sin benchmark\n",
                     GOW_STRINGIFY(GOW_PCSX2_BLENDING));
        return GOW_GS_OK;
    } catch(...) {close();return GOW_GS_FAILED;}
}
uint32_t gow_gs_destroy(uint64_t handle){
    std::lock_guard lock(gate);
    const auto status=access(handle);
    if(status!=GOW_GS_OK && status!=GOW_GS_FAILED)return status;
    close();return GOW_GS_OK;
}
uint32_t gow_gs_submit(uint64_t handle,uint32_t path,const uint8_t* bytes,uint32_t size){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    if(path<1 || path>3 || (!bytes && size))return GOW_GS_BAD_ARGUMENT;
    try {
        if(!bridge->submit(static_cast<gow_pcsx2_gs::Path>(path),bytes,size)){failed=true;return GOW_GS_FAILED;}
        ++streamInfo.input_events;streamInfo.input_gif_bytes+=size;
        return GOW_GS_OK;
    } catch(...) {failed=true;return GOW_GS_FAILED;}
}
uint32_t gow_gs_vsync(uint64_t handle,uint32_t field,const uint8_t* privileged,uint32_t size){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    if(!privileged || size!=sizeof(regs) || field>1)return GOW_GS_BAD_ARGUMENT;
    try {
        // GOW-Port: copiar registros PS2 reales, no un struct del runtime con otro layout.
        std::memcpy(&regs,privileged,sizeof(regs));
        // No exigir complete(): VSync puede interrumpir un paquete GIF.
        snapshotValid=false;
        GSvsync(field,true);++sequence;
        ++streamInfo.input_events;
        streamInfo.last_vsync_events=streamInfo.input_events;
        streamInfo.last_vsync_gif_bytes=streamInfo.input_gif_bytes;
        streamInfo.last_vsync_sequence=sequence;streamInfo.last_vsync_field=field;
        vsyncQwordAligned=bridge->complete();
        return GOW_GS_OK;
    } catch(...) {failed=true;return GOW_GS_FAILED;}
}
uint32_t gow_gs_write_register(uint64_t handle,uint32_t reg,uint64_t value){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    if(reg>0x7f)return GOW_GS_BAD_ARGUMENT;
    try {
        static_cast<gow_pcsx2_gs::RegisterAdapter<GSRendererHW>*>(g_gs_renderer.get())->writeDirect(uint8_t(reg),value);
        ++streamInfo.input_events;
        return GOW_GS_OK;
    } catch(...) {failed=true;return GOW_GS_FAILED;}
}
uint32_t gow_gs_reset(uint64_t handle){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    try {
        snapshotValid=false;
        g_gs_renderer->Reset(true);bridge->discardAfterGsReset();sequence=0;
        const uint64_t nextEpoch=streamInfo.epoch+1;
        streamInfo={};streamInfo.epoch=nextEpoch;vsyncQwordAligned=false;
        return GOW_GS_OK;
    } catch(...) {failed=true;return GOW_GS_FAILED;}
}
uint32_t gow_gs_read_output(uint64_t handle,uint8_t* rgba,uint64_t capacity,GowGsFrameInfo* info){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    if(!info || info->size!=sizeof(*info) || (!rgba && capacity))return GOW_GS_BAD_ARGUMENT;
    auto* current=g_gs_device->GetCurrent();
    if(!current)return GOW_GS_NO_FRAME;
    if(current->GetFormat()!=GSTexture::Format::Color)return GOW_GS_FAILED;
    const uint32_t width=current->GetWidth(),height=current->GetHeight();
    const uint64_t row=uint64_t(width)*4,required=row*height;
    *info={sizeof(*info),width,height,uint32_t(row),sequence,required};
    if(!rgba || capacity<required)return GOW_GS_BUFFER_SMALL;
    try {
        auto download=g_gs_device->CreateDownloadTexture(width,height,GSTexture::Format::Color);
        if(!download)return GOW_GS_FAILED;
        const GSVector4i rect(0,0,width,height);
        download->CopyFromTexture(rect,current,rect,0);download->Flush();
        return download->ReadTexels(rect,rgba,uint32_t(row))?GOW_GS_OK:GOW_GS_FAILED;
    } catch(...) {failed=true;return GOW_GS_FAILED;}
}

uint32_t gow_gs_read_stream_info(uint64_t handle,GowGsStreamInfo* info){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    if(!info||info->size!=sizeof(*info))return GOW_GS_BAD_ARGUMENT;
    auto result=streamInfo;
    result.size=sizeof(result);
    result.flags=(result.last_vsync_sequence?GOW_GS_STREAM_HAS_VSYNC:0) |
        (g_gs_device->GetCurrent()?GOW_GS_STREAM_OUTPUT_AVAILABLE:0) |
        (bridge->complete()?GOW_GS_STREAM_INPUT_QWORD_ALIGNED:0) |
        (vsyncQwordAligned?GOW_GS_STREAM_VSYNC_QWORD_ALIGNED:0);
    *info=result;return GOW_GS_OK;
}

uint32_t gow_gs_read_video_info(uint64_t handle,GowGsVideoInfo* info){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    if(!info||info->size!=sizeof(*info))return GOW_GS_BAD_ARGUMENT;
    *info={sizeof(*info),uint32_t(g_gs_renderer->GetVideoMode()),uint32_t(g_gs_renderer->isReallyInterlaced()),
        uint32_t(GSConfig.InterlaceMode),uint32_t(GSConfig.PCRTCAntiBlur),0,sequence,
        regs.SMODE1.U64,regs.SYNCV.U64,regs.SMODE2.U64};
    return GOW_GS_OK;
}

uint32_t gow_gs_read_snapshot(uint64_t handle,uint32_t aspect,uint32_t windowWidth,
    uint32_t windowHeight,uint8_t* rgba,uint64_t capacity,GowGsFrameInfo* info){
    std::lock_guard lock(gate);
    const auto status=access(handle);if(status)return status;
    if(!info||info->size!=sizeof(*info)||aspect>GOW_GS_ASPECT_16_9||windowWidth>2048||windowHeight>2048||
        ((windowWidth==0)!=(windowHeight==0))||(!rgba&&capacity))return GOW_GS_BAD_ARGUMENT;
    try {
        if(!rgba){
            snapshotValid=false;
            SnapshotAspectGuard restore;
            const AspectRatioType selected=aspect==GOW_GS_ASPECT_STRETCH?AspectRatioType::Stretch:
                (aspect==GOW_GS_ASPECT_AUTO?AspectRatioType::RAuto4_3_3_2:
                (aspect==GOW_GS_ASPECT_4_3?AspectRatioType::R4_3:AspectRatioType::R16_9));
            GSConfig.AspectRatio=selected;EmuConfig.CurrentAspectRatio=selected;EmuConfig.CurrentCustomAspectRatio=0.f;
            snapshotPixels.clear();snapshotWidth=snapshotHeight=0;
            if(!g_gs_renderer->SaveSnapshotToMemory(windowWidth,windowHeight,aspect!=GOW_GS_ASPECT_STRETCH,false,
                &snapshotWidth,&snapshotHeight,&snapshotPixels))return GOW_GS_NO_FRAME;
            if(!snapshotWidth||!snapshotHeight||snapshotPixels.size()!=uint64_t(snapshotWidth)*snapshotHeight)return GOW_GS_FAILED;
            snapshotHandle=handle;snapshotSequence=sequence;snapshotAspect=aspect;
            snapshotWindowWidth=windowWidth;snapshotWindowHeight=windowHeight;snapshotValid=true;
        }else if(!snapshotValid||snapshotHandle!=handle||snapshotSequence!=sequence||snapshotAspect!=aspect||
            snapshotWindowWidth!=windowWidth||snapshotWindowHeight!=windowHeight)return GOW_GS_BAD_ARGUMENT;
        const uint64_t required=uint64_t(snapshotWidth)*snapshotHeight*4;
        *info={sizeof(*info),snapshotWidth,snapshotHeight,snapshotWidth*4,sequence,required};
        if(!rgba||capacity<required)return GOW_GS_BUFFER_SMALL;
        std::memcpy(rgba,snapshotPixels.data(),size_t(required));return GOW_GS_OK;
    }catch(...){snapshotValid=false;return GOW_GS_FAILED;}
}
