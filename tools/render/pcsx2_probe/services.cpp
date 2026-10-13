// GOW-Port: servicios opcionales ausentes del fixture; nunca simulan dibujo.
#include "GS/GS.h"
#include "GS/GSCapture.h"
#include "GS/GSXXH.h"
#include "GS/Renderers/HW/GSTextureReplacements.h"
#include <cstdio>
#include <cstdlib>
#include "host_probe.h"
static void unavailable(const char* name){std::fprintf(stderr,"Servicio no disponible: %s\n",name);std::abort();}
extern unsigned gsProbePresentRequests, gsProbeSkippedPresents, gsProbeFrameUpdates;
namespace GSTextureReplacements {
void Initialize(){if(GSConfig.LoadTextureReplacements || GSConfig.DumpReplaceableTextures)unavailable("TextureReplacements");}
bool HasAnyReplacementTextures(){Initialize();return false;}
bool HasReplacementTextureWithOtherPalette(const GSTextureCache::HashCacheKey&){Initialize();return false;}
void ProcessAsyncLoadedTextures(){unavailable("TextureReplacements async");}
GSTexture* LookupReplacementTexture(const GSTextureCache::HashCacheKey&,bool,bool*,std::pair<u8,u8>*){unavailable("TextureReplacements lookup");return nullptr;}
void DumpTexture(const GSTextureCache::HashCacheKey&,const GIFRegTEX0&,const GIFRegTEXA&,GSTextureCache::SourceRegion,GSLocalMemory&,u32){unavailable("TextureReplacements dump");}
}
namespace GSCapture {
void EndCapture(){} // Captura nunca iniciada; servicio deshabilitado.
bool IsCapturingVideo(){return false;}
GSVector2i GetSize(){unavailable("Capture size");return {};}
bool DeliverVideoFrame(GSTexture*){unavailable("Capture frame");return false;}
bool BeginCapture(float,GSVector2i,float,std::string){unavailable("Capture begin");return false;}
}
#include "Host.h"
#include "ImGui/FullscreenUI.h"
#include "ImGui/ImGuiManager.h"
#include "PerformanceMetrics.h"
#include "VMManager.h"
#include "GSDumpReplayer.h"
#include "GS/GSDump.h"
#include "common/Image.h"
#include "Memory.h"
#include "common/RedtapeWindows.h"

bool GSreopen(bool,bool,GSRendererType,std::optional<const Pcsx2Config::GSOptions*>){unavailable("GS reopen");return false;}
bool FMVstarted=false; // No hay ejecutable ni reproduccion de FMV en este control.
namespace Host {
void AddKeyedOSDMessage(std::string,std::string message,float){std::fprintf(stderr,"OSD: %s\n",message.c_str());}
bool GetBaseBoolSettingValue(const char*,const char*,bool fallback){return fallback;}
void BeginPresentFrame(){++gsProbePresentRequests;gsProbeBeginUiFrame();}
}
namespace FullscreenUI {void Render(){if(!gsProbeHasWindow())unavailable("UI");}} // Sin interfaz propia que dibujar.
namespace ImGuiManager {
void NewFrame(){if(!gsProbeHasWindow())unavailable("UI NewFrame");gsProbeEndUiFrame();} // El host abre UI en BeginPresentFrame.
void SkipFrame(){++gsProbeSkippedPresents;gsProbeSkipUiFrame();}
void RenderOSD(){if(!gsProbeHasWindow())unavailable("UI OSD");if(gsProbeBeforePresent)gsProbeBeforePresent();}
}
namespace GSDumpReplayer {
bool IsReplayingDump(){return false;} bool IsRunner(){return false;}
void RenderUI(){unavailable("dump UI");} void UpdateGSStats(){unavailable("dump stats");}
}
namespace PerformanceMetrics {
void Update(bool,bool,bool){++gsProbeFrameUpdates;} // Solo cuenta callbacks; no mide FPS.
void OnGPUPresent(float,u64,u64){++gsProbeGpuPresents;} // Contador de callbacks, no rendimiento.
InternalFPSMethod GetInternalFPSMethod(){return InternalFPSMethod::None;}
void StartSavingMetrics(u32){unavailable("metrics save");}
bool IsSavingMetrics(){return false;} void DumpSavedMetrics(){unavailable("metrics dump");}
void SetGSSWThreadCount(u32){} // Estadisticas deshabilitadas; no altera rasterizacion.
void SetGSSWThread(u32,Threading::ThreadHandle){}
}
namespace VMManager {
std::string GetDiscSerial(){return {};}
std::string GetTitle(bool){return "GS procedural";}
u32 GetDiscCRC(){return 0;}
namespace Internal {
const std::vector<u32>& GetSoftwareRendererProcessorList(){static const std::vector<u32> processors;return processors;}
}
}
#define REJECT_DUMP_FACTORY(name) std::unique_ptr<GSDumpBase> GSDumpBase::name(const std::string&,const std::string&,u32,u32,u32,const u32*,const freezeData&,const GSPrivRegSet*){unavailable("GS dump factory");return {};}
REJECT_DUMP_FACTORY(CreateUncompressedDump)
REJECT_DUMP_FACTORY(CreateXzDump)
REJECT_DUMP_FACTORY(CreateZstDump)
bool GSDumpBase::VSync(int,bool,const GSPrivRegSet*){unavailable("GS dump VSync");return false;}
bool RGBA8Image::SaveToFile(const char*,u8) const {unavailable("Image file save");return false;}
// GOW-Port: reserva real de codigo para el rasterizador SW original.
// Se limita a esa region; no representa memoria EE/VU.
namespace SysMemory {
u8* GetCodePtr(size_t offset){
    struct CodeMemory {
        u8* memory=static_cast<u8*>(VirtualAlloc(nullptr,HostMemoryMap::SWrecSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
        ~CodeMemory(){if(memory)VirtualFree(memory,0,MEM_RELEASE);}
    };
    static CodeMemory code;
    u8* base=code.memory;
    if(!base || offset<HostMemoryMap::SWrecOffset || offset>HostMemoryMap::SWrecOffset+HostMemoryMap::SWrecSize)unavailable("SW code allocation");
    return base+(offset-HostMemoryMap::SWrecOffset);
}
}
