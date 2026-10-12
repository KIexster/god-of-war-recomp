// GOW-Port: ABI experimental de renderizado; el runtime conserva IRQ/FINISH.
#pragma once
#include <stdint.h>
#ifdef GOW_PCSX2_MODULE_BUILD
#define GOW_GS_API __declspec(dllexport)
#else
#define GOW_GS_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum { GOW_GS_ABI = 1, GOW_GS_PRIV_BYTES = 8192 };
enum GowGsStatus {
    GOW_GS_OK=0, GOW_GS_BAD_ARGUMENT=1, GOW_GS_BAD_HANDLE=2,
    GOW_GS_WRONG_THREAD=3, GOW_GS_BUSY=4, GOW_GS_FAILED=5,
    GOW_GS_NO_FRAME=6, GOW_GS_BUFFER_SMALL=7
};
typedef struct GowGsCreateInfo {
    uint32_t size, abi;
    const char* resources; // UTF-8; nullptr usa la ruta de desarrollo configurada.
    uint32_t width, height;
    uint32_t hidden_window; // 0 Surfaceless, 1 ventana propia oculta.
    uint32_t reserved;
} GowGsCreateInfo;
typedef struct GowGsFrameInfo {
    uint32_t size, width, height, row_bytes;
    uint64_t sequence, required_bytes;
} GowGsFrameInfo;
GOW_GS_API uint32_t gow_gs_abi(void);
GOW_GS_API uint32_t gow_gs_create(const GowGsCreateInfo* info, uint64_t* handle);
GOW_GS_API uint32_t gow_gs_destroy(uint64_t handle);
GOW_GS_API uint32_t gow_gs_submit(uint64_t handle, uint32_t path, const uint8_t* bytes, uint32_t size);
GOW_GS_API uint32_t gow_gs_vsync(uint64_t handle, uint32_t field, const uint8_t* privileged, uint32_t size);
GOW_GS_API uint32_t gow_gs_read_output(uint64_t handle, uint8_t* rgba, uint64_t capacity, GowGsFrameInfo* info);
#ifdef __cplusplus
}
static_assert(sizeof(GowGsCreateInfo)==32 && sizeof(GowGsFrameInfo)==32, "ABI Windows x64");
#endif
