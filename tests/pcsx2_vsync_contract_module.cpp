// GOW-Port: receptor procedural sin GPU para comprobar el VSync entregado por el cliente.
#include "../src/gow_gs_pcsx2_api.h"
#include <thread>

namespace {
bool active=false, mismatch=false;
unsigned frames=0;
std::thread::id owner;
uint32_t access(uint64_t handle){
    if(!active || handle!=1)return GOW_GS_BAD_HANDLE;
    return owner==std::this_thread::get_id()?GOW_GS_OK:GOW_GS_WRONG_THREAD;
}
}
uint32_t gow_gs_abi(){return GOW_GS_ABI;}
uint32_t gow_gs_create(const GowGsCreateInfo* info,uint64_t* handle){
    if(!info || !handle || info->abi!=GOW_GS_ABI || info->size!=sizeof(*info))return GOW_GS_BAD_ARGUMENT;
    if(active)return GOW_GS_BUSY;
    active=true;mismatch=false;frames=0;owner=std::this_thread::get_id();*handle=1;return GOW_GS_OK;
}
uint32_t gow_gs_destroy(uint64_t handle){
    const auto status=access(handle);if(status)return status;active=false;return GOW_GS_OK;
}
uint32_t gow_gs_submit(uint64_t handle,uint32_t,const uint8_t*,uint32_t){return access(handle);}
uint32_t gow_gs_write_register(uint64_t handle,uint32_t,uint64_t){return access(handle);}
uint32_t gow_gs_reset(uint64_t handle){return access(handle);}
uint32_t gow_gs_read_output(uint64_t handle,uint8_t*,uint64_t,GowGsFrameInfo*){
    const auto status=access(handle);return status?status:GOW_GS_NO_FRAME;
}
uint32_t gow_gs_vsync(uint64_t handle,uint32_t field,const uint8_t* regs,uint32_t size){
    const auto status=access(handle);if(status)return status;
    if(!regs || size!=GOW_GS_PRIV_BYTES || frames>=4)return GOW_GS_BAD_ARGUMENT;
    // GOW-Port: valores de referencia del contrato MTGS, no una llamada al helper del cliente.
    constexpr uint32_t expectedFields[]={1,0,0,1};
    constexpr uint8_t expectedCsrFieldBits[]={0,0x20,0x20,0};
    mismatch|=field!=expectedFields[frames] || (regs[0x1001]&0x20u)!=expectedCsrFieldBits[frames];
    ++frames;return mismatch?GOW_GS_FAILED:GOW_GS_OK;
}
extern "C" __declspec(dllexport) uint32_t gow_vsync_contract_result(){
    return !active && !mismatch && frames==4?1u:0u;
}
