// GOW-Port: host procedural sin ventana, fuera del juego.
#include "GS/GS.h"
#include "Host.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "R5900.h"
#include "MemoryTypes.h"
#include <cstdio>
#include <cstdlib>
// GOW-Port: estado procedural para referencias estaticas de las cabeceras.
// No se ejecuta ninguna instruccion EE ni se importa memoria del juego.
alignas(16) cpuRegistersPack _cpuRegistersPack{};
alignas(__pagealignsize) u8 eeHw[Ps2MemSize::Hardware]{};
unsigned gsProbeWindowRequests=0;
Pcsx2Config::GSOptions GSConfig;
std::unique_ptr<GSDevice> g_gs_device;
bool GSIsHardwareRenderer(){return GSGetCurrentRenderer()!=GSRendererType::SW;}
GSRendererType GSGetCurrentRenderer(){return g_gs_device?GSConfig.Renderer:GSRendererType::SW;}
namespace Host {
std::string_view TranslateToStringView(std::string_view, std::string_view message){return message;}
std::optional<WindowInfo> AcquireRenderWindow(bool) {
    ++gsProbeWindowRequests;
    if(!g_gs_device) return std::nullopt;
    WindowInfo info; info.type=WindowInfo::Type::Surfaceless;
    info.surface_width=73; info.surface_height=37; return info;
}
void ReportErrorAsync(std::string_view title,std::string_view message) {
    std::fprintf(stderr,"%.*s: %.*s\n",int(title.size()),title.data(),int(message.size()),message.data());
}
std::string GetStringSettingValue(const char*,const char*,const char* fallback){return fallback?fallback:"";}
void AddIconOSDMessage(std::string,const char*,std::string_view message,float){
    std::fprintf(stderr,"OSD: %.*s\n",int(message.size()),message.data());
}
bool IsFullscreen(){return false;}
void SetFullscreen(bool enabled){if(enabled){std::fprintf(stderr,"Fullscreen no admitido en el fixture\n");std::abort();}}
void RunOnCPUThread(std::function<void()> function,bool){function();}
}
namespace EmuFolders {
std::string Logs=".";
std::string Resources=GOW_PROBE_RESOURCES;
std::string Cache=".";
std::string Snapshots=".";
std::string Videos=".";
}
