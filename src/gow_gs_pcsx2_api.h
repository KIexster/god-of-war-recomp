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
// GOW-Port: extensión optativa; no cambia los structs ni las llamadas de ABI1 anteriores.
typedef struct GowGsVideoInfo {
    uint32_t size, video_mode, really_interlaced, interlace_mode;
    uint32_t pcrtc_anti_blur, reserved;
    uint64_t sequence, smode1, syncv, smode2;
} GowGsVideoInfo;
// GOW-Port: prefijos de llamadas aceptadas en esta instancia/epoch, no posiciones de VRAM.
// complete() del transporte sólo comprueba fragmentos de 16 bytes, no el final del tag GIF.
enum GowGsStreamFlags {
    GOW_GS_STREAM_HAS_VSYNC=1, GOW_GS_STREAM_OUTPUT_AVAILABLE=2,
    GOW_GS_STREAM_INPUT_QWORD_ALIGNED=4, GOW_GS_STREAM_VSYNC_QWORD_ALIGNED=8
};
typedef struct GowGsStreamInfo {
    uint32_t size, flags;
    uint64_t epoch, input_events, input_gif_bytes;
    uint64_t last_vsync_events, last_vsync_gif_bytes, last_vsync_sequence;
    uint32_t last_vsync_field, reserved;
} GowGsStreamInfo;

enum GowGsSnapshotAspect {
    GOW_GS_ASPECT_STRETCH=0, GOW_GS_ASPECT_AUTO=1,
    GOW_GS_ASPECT_4_3=2, GOW_GS_ASPECT_16_9=3
};
GOW_GS_API uint32_t gow_gs_abi(void);
GOW_GS_API uint32_t gow_gs_create(const GowGsCreateInfo* info, uint64_t* handle);
GOW_GS_API uint32_t gow_gs_destroy(uint64_t handle);
GOW_GS_API uint32_t gow_gs_submit(uint64_t handle, uint32_t path, const uint8_t* bytes, uint32_t size);
GOW_GS_API uint32_t gow_gs_write_register(uint64_t handle, uint32_t reg, uint64_t value);
GOW_GS_API uint32_t gow_gs_reset(uint64_t handle);
// GOW-Port: field usa la convención GSvsync de PCSX2 (CSR.FIELD ? 0 : 1).
GOW_GS_API uint32_t gow_gs_vsync(uint64_t handle, uint32_t field, const uint8_t* privileged, uint32_t size);
GOW_GS_API uint32_t gow_gs_read_output(uint64_t handle, uint8_t* rgba, uint64_t capacity, GowGsFrameInfo* info);
// El modo y las opciones proceden del GS original; video_mode usa GSVideoMode de la fuente fijada.
GOW_GS_API uint32_t gow_gs_read_video_info(uint64_t handle, GowGsVideoInfo* info);
// No consume entrada ni fuerza render/readback. La identidad incluye handle + epoch.
// input_events cuenta submit (incluso vacío), write_register y VSync correctos; reset abre epoch.
// last_vsync_* describe la frontera de la llamada VSync, sin acreditar RGB nuevo/composición nueva.
// OUTPUT_AVAILABLE sólo observa GetCurrent; puede conservar textura tras reset o posteriores GIF.
// Export optativo: DLL antiguas pueden no ofrecerlo. Los structs y calls anteriores no cambian.
GOW_GS_API uint32_t gow_gs_read_stream_info(uint64_t handle, GowGsStreamInfo* info);
// nullptr/0 calcula y guarda el snapshot original; lectura posterior usa esa captura sin recalcular.
// Ventana0/0 conserva resolución interna; máximo2048 por dimensión. Original normaliza alpha y reescala.
// La caché se invalida en VSync/reset/cierre. Las DLL antiguas pueden no exportar estas dos funciones.
GOW_GS_API uint32_t gow_gs_read_snapshot(uint64_t handle, uint32_t aspect, uint32_t window_width,
    uint32_t window_height, uint8_t* rgba, uint64_t capacity, GowGsFrameInfo* info);
#ifdef __cplusplus
}
static_assert(sizeof(GowGsCreateInfo)==32 && sizeof(GowGsFrameInfo)==32, "ABI Windows x64");
static_assert(sizeof(GowGsVideoInfo)==56, "Extensión optativa de ABI Windows x64");
static_assert(sizeof(GowGsStreamInfo)==64, "Frontera optativa de ABI Windows x64");
#endif
