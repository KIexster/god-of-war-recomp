// GOW-Port: contrato DLL sin dispositivo; controles GPU optativos despues.
#include "../tools/render/pcsx2_probe/module_api.h"
#include "common/RedtapeWindows.h"
#include <cstdio>

int main(int argc,char** argv){
    if(argc!=2)return 2;
    HMODULE module=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module){std::fprintf(stderr,"DLL: carga falla %lu\n",GetLastError());return 1;}
    auto abi=reinterpret_cast<decltype(&gow_gs_abi)>(GetProcAddress(module,"gow_gs_abi"));
    auto create=reinterpret_cast<decltype(&gow_gs_create)>(GetProcAddress(module,"gow_gs_create"));
    auto destroy=reinterpret_cast<decltype(&gow_gs_destroy)>(GetProcAddress(module,"gow_gs_destroy"));
    auto submit=reinterpret_cast<decltype(&gow_gs_submit)>(GetProcAddress(module,"gow_gs_submit"));
    auto vsync=reinterpret_cast<decltype(&gow_gs_vsync)>(GetProcAddress(module,"gow_gs_vsync"));
    auto read=reinterpret_cast<decltype(&gow_gs_read_output)>(GetProcAddress(module,"gow_gs_read_output"));
    auto reg=reinterpret_cast<decltype(&gow_gs_write_register)>(GetProcAddress(module,"gow_gs_write_register"));
    auto reset=reinterpret_cast<decltype(&gow_gs_reset)>(GetProcAddress(module,"gow_gs_reset"));
    auto snapshot=reinterpret_cast<decltype(&gow_gs_read_snapshot)>(GetProcAddress(module,"gow_gs_read_snapshot"));
    auto video=reinterpret_cast<decltype(&gow_gs_read_video_info)>(GetProcAddress(module,"gow_gs_read_video_info"));
    auto stream=reinterpret_cast<decltype(&gow_gs_read_stream_info)>(GetProcAddress(module,"gow_gs_read_stream_info"));
    bool ok=abi && create && destroy && submit && vsync && read && reg && reset && snapshot && video && stream;
    if(ok){
        uint64_t handle=0x1234;
        GowGsCreateInfo info{sizeof(info),GOW_GS_ABI,nullptr,64,32,0,0};
        ok=abi()==GOW_GS_ABI && create(nullptr,&handle)==GOW_GS_BAD_ARGUMENT;
        info.abi=2;ok&=create(&info,&handle)==GOW_GS_BAD_ARGUMENT;info.abi=GOW_GS_ABI;
        info.reserved=1;ok&=create(&info,&handle)==GOW_GS_BAD_ARGUMENT;info.reserved=0;
        info.width=0;ok&=create(&info,&handle)==GOW_GS_BAD_ARGUMENT;
        ok&=handle==0x1234 && destroy(handle)==GOW_GS_BAD_HANDLE;
        ok&=submit(handle,1,nullptr,0)==GOW_GS_BAD_HANDLE;
        ok&=reg(handle,0,0)==GOW_GS_BAD_HANDLE && reset(handle)==GOW_GS_BAD_HANDLE;
        ok&=vsync(handle,0,nullptr,0)==GOW_GS_BAD_HANDLE;
        GowGsFrameInfo frame{sizeof(frame),0,0,0,0,0};
        ok&=read(handle,nullptr,0,&frame)==GOW_GS_BAD_HANDLE;
        GowGsVideoInfo settings{};settings.size=sizeof(settings);
        ok&=video(handle,&settings)==GOW_GS_BAD_HANDLE;
        GowGsStreamInfo cursor{};cursor.size=sizeof(cursor);cursor.epoch=0x1234;
        ok&=stream(handle,&cursor)==GOW_GS_BAD_HANDLE && cursor.epoch==0x1234;
        ok&=snapshot(handle,GOW_GS_ASPECT_AUTO,0,0,nullptr,0,&frame)==GOW_GS_BAD_HANDLE;
    }
    FreeLibrary(module);
    std::puts(ok?"DLL: ABI/exportaciones/rechazos sin dispositivo correctos":"DLL: contrato falla");
    return ok?0:1;
}
