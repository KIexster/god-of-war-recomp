// GOW-Port: host procedural sin ventana, fuera del juego.
#include "GS/GS.h"
#include "Host.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "R5900.h"
#include "MemoryTypes.h"
#include "host_probe.h"
#include "common/RedtapeWindows.h"
#include "imgui.h"
#include <cstdio>
#include <cstdlib>
// GOW-Port: estado procedural para referencias estaticas de las cabeceras.
// No se ejecuta ninguna instruccion EE ni se importa memoria del juego.
alignas(16) cpuRegistersPack _cpuRegistersPack{};
alignas(__pagealignsize) u8 eeHw[Ps2MemSize::Hardware]{};
unsigned gsProbeWindowRequests=0;
unsigned gsProbePresentRequests=0;
unsigned gsProbeSkippedPresents=0;
unsigned gsProbeFrameUpdates=0;
unsigned gsProbeGpuPresents=0;
void (*gsProbeBeforePresent)()=nullptr;
static HWND probeWindow=nullptr;
static unsigned probeWidth=0, probeHeight=0;
static bool probeUiFrame=false;
bool gsProbeHasWindow(){return probeWindow!=nullptr;}
bool gsProbeOpenWindow(unsigned width,unsigned height){
    if(probeWindow || !width || !height || width>4096 || height>4096)return false;
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"GOW_PCSX2_Procedural_Probe";
    if(!RegisterClassW(&wc))return false;
    // GOW-Port: ventana propia oculta. No captura ni opera ventanas del usuario.
    probeWindow=CreateWindowExW(0,wc.lpszClassName,L"GS procedural",WS_POPUP,
        0,0,width,height,nullptr,nullptr,wc.hInstance,nullptr);
    if(!probeWindow){UnregisterClassW(wc.lpszClassName,wc.hInstance);return false;}
    probeWidth=width; probeHeight=height;
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(float(width),float(height));
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures; // Backend original ImGui 1.92.
    return true;
}
void gsProbeCloseWindow(){
    if(!probeWindow)return;
    if(probeUiFrame){ImGui::EndFrame();probeUiFrame=false;}
    ImGui::DestroyContext();
    if(!DestroyWindow(probeWindow)){std::fprintf(stderr,"Fallo al cerrar ventana GS: %lu\n",GetLastError());std::abort();}
    probeWindow=nullptr;
    if(!UnregisterClassW(L"GOW_PCSX2_Procedural_Probe",GetModuleHandleW(nullptr))){std::fprintf(stderr,"Fallo al liberar clase GS: %lu\n",GetLastError());std::abort();}
}
void gsProbeBeginUiFrame(){
    if(!probeWindow)return;
    ImGuiIO& io=ImGui::GetIO();io.DeltaTime=1.0f/60.0f; // UI vacia; no reloj del juego.
    ImGui::NewFrame();probeUiFrame=true;
}
void gsProbeSkipUiFrame(){if(probeUiFrame){ImGui::EndFrame();probeUiFrame=false;}}
void gsProbeEndUiFrame(){probeUiFrame=false;} // EndPresent original ya ejecuto ImGui::Render.
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
    info.surface_width=73; info.surface_height=37;
    if(probeWindow){info.type=WindowInfo::Type::Win32;info.window_handle=probeWindow;
        info.surface_width=probeWidth;info.surface_height=probeHeight;}
    return info;
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
