// GOW-Port: dibujo procedural por GIF, GSRendererHW y FIFO originales.
#include "GS/GS.h"
#include "GS/GSPerfMon.h"
#include "GS/Renderers/DX11/GSDevice11.h"
#include "GS/Renderers/HW/GSRendererHW.h"
#include "../tools/render/pcsx2_gif_sink.h"
#include "../tools/render/pcsx2_probe/host_probe.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

static void ad(std::vector<u64>& words, u64 data, u64 reg)
{
    words.push_back(data); words.push_back(reg);
}

static bool send(gow_pcsx2_gs::GifBridge& bridge, gow_pcsx2_gs::Path path, const std::vector<u64>& words)
{
    const auto* bytes = reinterpret_cast<const u8*>(words.data());
    const size_t length = words.size() * sizeof(u64);
    for (size_t offset = 0; offset < length;)
    {
        const size_t part = std::min<size_t>(13, length - offset);
        if (!bridge.submit(path, bytes + offset, part))
            return false;
        offset += part;
    }
    return bridge.complete();
}

static bool draw(gow_pcsx2_gs::Path path)
{
    constexpr unsigned width = 64, height = 32;
    const double previousCalls = g_perfmon.GetCounter(GSPerfMon::DrawCalls);
    std::vector<u64> packet(2, 0);
    ad(packet, 1, 0x1a); // PRMODECONT: usa PRIM.
    ad(packet, u64(1) << 16, 0x4c); // FRAME_1: base 0, FBW 1, PSMCT32.
    ad(packet, (u64(1) << 32) | 0x100, 0x4e); // ZBUF_1: sin escritura.
    ad(packet, 0, 0x18); // XYOFFSET_1.
    ad(packet, (u64(width - 1) << 16) | (u64(height - 1) << 48), 0x40); // SCISSOR_1.
    ad(packet, 0, 0x47); // TEST_1: sin alpha/depth test.
    ad(packet, 1, 0x46); // COLCLAMP.
    ad(packet, 6, 0x00); // PRIM: sprite sin textura/blending.
    std::vector<u32> expected(width * height);
    for (unsigned ty = 0; ty < 4; ++ty)
        for (unsigned tx = 0; tx < 4; ++tx)
        {
            const u32 color = (17 + tx * 43 + ty * 11 + unsigned(path) * 2) |
                ((31 + ty * 47 + tx * 3) << 8) |
                ((59 + tx * 17 + ty * 23) << 16) | (u32(128 + tx + ty * 4) << 24);
            ad(packet, color | (u64(0x3f800000) << 32), 0x01); // RGBAQ.
            ad(packet, (tx * 16 * 16) | (u64(ty * 8 * 16) << 16), 0x05); // XYZ2.
            ad(packet, ((tx + 1) * 16 * 16) | (u64((ty + 1) * 8 * 16) << 16), 0x05);
            for (unsigned y = ty * 8; y < (ty + 1) * 8; ++y)
                for (unsigned x = tx * 16; x < (tx + 1) * 16; ++x)
                    expected[y * width + x] = color;
        }
    packet[0] = (packet.size() / 2 - 1) | (u64(1) << 15) | (u64(1) << 60);
    packet[1] = 0x0e; // PACKED A+D.
    gow_pcsx2_gs::GifBridge bridge(gow_pcsx2_gs::pcsx2TransferSink());
    if (!send(bridge, path, packet))
        return false;
    g_gs_renderer->Flush(GSState::GSFlushReason::DOWNLOADFIFO);
    std::vector<u64> read(2, 0);
    ad(read, u64(1) << 16, 0x50); // BITBLTBUF: fuente framebuffer, SBW 1.
    ad(read, 0, 0x51); // TRXPOS.
    ad(read, width | (u64(height) << 32), 0x52); // TRXREG.
    ad(read, 1, 0x53); // TRXDIR: local -> host.
    read[0] = 4 | (u64(1) << 15) | (u64(1) << 60); read[1] = 0x0e;
    if (!send(bridge, path, read))
        return false;
    std::vector<u32> actual(expected.size(), 0xdeadbeef);
    g_gs_renderer->InitReadFIFO(reinterpret_cast<u8*>(actual.data()), actual.size() * sizeof(u32));
    g_gs_renderer->ReadFIFO(reinterpret_cast<u8*>(actual.data()), actual.size() * sizeof(u32) / 16);
    for (size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != expected[i])
        {
            std::fprintf(stderr, "GIF/FIFO: pixel %zu esperado %08X recibido %08X\n", i, expected[i], actual[i]);
            return false;
        }
    const double calls = g_perfmon.GetCounter(GSPerfMon::DrawCalls) - previousCalls;
    if (calls < 1)
    {
        std::fprintf(stderr, "GIF/FIFO: no hubo draw calls GPU\n");
        return false;
    }
    std::printf("PATH%u GIF/FIFO: %zu pixeles RGBA exactos, %.0f draw calls GPU\n", unsigned(path), actual.size(), calls);
    return true;
}

static std::vector<u64> packet(const std::vector<u64>& registers)
{
    std::vector<u64> result{(registers.size() / 2) | (u64(1) << 15) | (u64(1) << 60), 0x0e};
    result.insert(result.end(), registers.begin(), registers.end());
    return result;
}

static const std::vector<u32>* expectedBackbuffer=nullptr;
static bool backbufferCorrect=false;
static void checkBackbuffer()
{
    auto* device=static_cast<GSDevice11*>(g_gs_device.get());
    wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
    device->GetD3DContext()->OMGetRenderTargets(1,view.put(),nullptr);
    if(!view || !expectedBackbuffer)return;
    wil::com_ptr_nothrow<ID3D11Resource> resource;
    view->GetResource(resource.put());
    auto source=resource.try_query<ID3D11Texture2D>();
    if(!source)return;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    if(desc.Width!=64 || desc.Height!=32 || desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if(FAILED(device->GetD3DDevice()->CreateTexture2D(&desc,nullptr,staging.put())))return;
    device->GetD3DContext()->CopyResource(staging.get(),source.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(device->GetD3DContext()->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped)))return;
    backbufferCorrect=true;
    for(unsigned y=0;y<32;++y)
        for(unsigned x=0;x<64;++x)
        {
            u32 actual=0;
            std::memcpy(&actual,static_cast<const u8*>(mapped.pData)+y*mapped.RowPitch+x*sizeof(u32),sizeof(actual));
            if(actual!=((*expectedBackbuffer)[y*64+x]|0xff000000))backbufferCorrect=false;
        }
    device->GetD3DContext()->Unmap(staging.get(),0);
}

static bool frame(const std::vector<u32>& expected)
{
    extern unsigned gsProbePresentRequests, gsProbeSkippedPresents, gsProbeFrameUpdates;
    const unsigned presentBefore = gsProbePresentRequests, skipBefore = gsProbeSkippedPresents, updatesBefore = gsProbeFrameUpdates;
    const unsigned gpuBefore=gsProbeGpuPresents;
    auto& regs = *reinterpret_cast<GSPrivRegSet*>(g_gs_renderer->GetRegsMem());
    regs.PMODE.EN1 = 1; regs.PMODE.EN2 = 0;
    regs.PMODE.MMOD = 1; regs.PMODE.ALP = 255; regs.PMODE.SLBG = 1;
    regs.SMODE1.CMOD = 2; regs.SMODE2.INT = 0;
    regs.DISP[0].DISPFB.FBW = 1; regs.DISP[0].DISPFB.PSM = 0;
    // GOW-Port: NTSC usa cuatro ciclos horizontales por pixel en PCRTC.
    regs.DISP[0].DISPLAY.MAGH = 3;
    regs.DISP[0].DISPLAY.DW = 255; regs.DISP[0].DISPLAY.DH = 31;
    regs.DISP[0].DISPLAY.DY = 25; // Inicio activo NTSC de VideoModeOffsets original.
    expectedBackbuffer=&expected;backbufferCorrect=false;
    gsProbeBeforePresent=gsProbeHasWindow()?checkBackbuffer:nullptr;
    GSvsync(0, true); // Wrapper original: actualizar PCRTC y Flush antes de VSync.
    gsProbeBeforePresent=nullptr;expectedBackbuffer=nullptr;
    auto* current = g_gs_device->GetCurrent();
    if (!current || current->GetWidth() != 64 || current->GetHeight() != 32)
    {
        std::fprintf(stderr, "VSync: salida %dx%d; esperado 64x32\n", current ? current->GetWidth() : 0, current ? current->GetHeight() : 0);
        return false;
    }
    const GSVector4i rect(0, 0, 64, 32);
    auto download = g_gs_device->CreateDownloadTexture(64, 32, GSTexture::Format::Color);
    if (!download)
        return false;
    download->CopyFromTexture(rect, current, rect, 0); download->Flush();
    std::vector<u32> actual(expected.size(), 0xdeadbeef);
    if (!download->ReadTexels(rect, actual.data(), 64 * sizeof(u32)))
        return false;
    for (size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != (expected[i] | 0xff000000)) // PMODE.MMOD/ALP: alpha final 255.
        {
            std::fprintf(stderr, "VSync: pixel %zu esperado %08X recibido %08X\n", i, expected[i] | 0xff000000, actual[i]);
            return false;
        }
    const unsigned skipped=gsProbeHasWindow()?0:1;
    if (gsProbePresentRequests != presentBefore + 1 || gsProbeSkippedPresents != skipBefore + skipped || gsProbeFrameUpdates != updatesBefore + 1)
    {
        std::fprintf(stderr, "VSync: callbacks de host Surfaceless inconsistentes\n");
        return false;
    }
    if(gsProbeHasWindow() && (!backbufferCorrect || gsProbeGpuPresents!=gpuBefore+1))
    {
        std::fprintf(stderr,"VSync: backbuffer o callback Present no coincide\n");return false;
    }
    std::puts("VSync/PCRTC: 2048 pixeles RGBA exactos en salida compuesta");
    if(gsProbeHasWindow())std::puts("DXGI: 2048 pixeles RGBA exactos en backbuffer antes de Present, ventana oculta");
    return true;
}

static bool texturedImage(bool checkFrame = false, unsigned seed = 0)
{
    constexpr u32 width = 64, height = 32, textureBase = 0x1000;
    const auto path = gow_pcsx2_gs::Path::Path3;
    const double previousCalls = g_perfmon.GetCounter(GSPerfMon::DrawCalls);
    std::vector<u32> expected(width * height);
    for (u32 y = 0; y < height; ++y)
        for (u32 x = 0; x < width; ++x)
            expected[y * width + x] = ((x * 7 + y * 3 + 9 + seed * 43) & 255) |
                (((x * 5 + y * 11 + 37) & 255) << 8) |
                (((x * 13 + y * 17 + 67) & 255) << 16) |
                (((x * 19 + y * 23 + 127) & 255) << 24);
    gow_pcsx2_gs::GifBridge bridge(gow_pcsx2_gs::pcsx2TransferSink());
    std::vector<u64> upload;
    ad(upload, (u64(textureBase) << 32) | (u64(1) << 48), 0x50); // BITBLTBUF destino CT32.
    ad(upload, 0, 0x51);
    ad(upload, width | (u64(height) << 32), 0x52);
    ad(upload, 0, 0x53); // Host -> local.
    if (!send(bridge, path, packet(upload)))
        return false;
    const size_t qwc = expected.size() * sizeof(u32) / 16;
    std::vector<u64> image(2 + qwc * 2, 0);
    image[0] = qwc | (u64(1) << 15) | (u64(2) << 58); // IMAGE, no A+D.
    std::memcpy(image.data() + 2, expected.data(), expected.size() * sizeof(u32));
    if (!send(bridge, path, image))
        return false;
    std::vector<u64> textured;
    ad(textured, 0, 0x3f); // TEXFLUSH tras IMAGE.
    ad(textured, textureBase | (u64(1) << 14) | (u64(6) << 26) | (u64(5) << 30) |
        (u64(1) << 34) | (u64(1) << 35), 0x06); // TEX0: TCC=RGBA, DECAL.
    ad(textured, 0, 0x14); // TEX1 nearest.
    ad(textured, 6 | (1 << 4) | (1 << 8), 0x00); // Sprite, TME, FST.
    ad(textured, 0x3f80000080808080ull, 0x01);
    ad(textured, 0, 0x03); // UV.
    ad(textured, 0, 0x05); // XYZ2.
    ad(textured, width * 16 | (u64(height * 16) << 16), 0x03);
    ad(textured, width * 16 | (u64(height * 16) << 16), 0x05);
    if (!send(bridge, path, packet(textured)))
        return false;
    g_gs_renderer->Flush(GSState::GSFlushReason::DOWNLOADFIFO);
    // GOW-Port: incluye un rectangulo que cruza bloques, con ultima QW parcial.
    for (const GSVector4i rect : {GSVector4i(0, 0, width, height), GSVector4i(11, 9, 24, 16)})
    {
        std::vector<u64> read;
        ad(read, u64(1) << 16, 0x50);
        ad(read, rect.x | (u64(rect.y) << 16), 0x51);
        ad(read, rect.width() | (u64(rect.height()) << 32), 0x52);
        ad(read, 1, 0x53);
        if (!send(bridge, path, packet(read)))
            return false;
        const size_t pixels = rect.width() * rect.height();
        const size_t qw = (pixels * sizeof(u32) + 15) / 16;
        std::vector<u32> actual(qw * 4 + 4, 0xdeadbeef);
        g_gs_renderer->InitReadFIFO(reinterpret_cast<u8*>(actual.data()), qw * 16);
        g_gs_renderer->ReadFIFO(reinterpret_cast<u8*>(actual.data()), qw);
        for (size_t i = 0; i < pixels; ++i)
        {
            const u32 color = expected[(rect.y + i / rect.width()) * width + rect.x + i % rect.width()];
            if (actual[i] != color)
            {
                std::fprintf(stderr, "IMAGE/textura FIFO: pixel %zu esperado %08X recibido %08X\n", i, color, actual[i]);
                return false;
            }
        }
        for (size_t i = qw * 4; i < actual.size(); ++i)
            if (actual[i] != 0xdeadbeef)
            {
                std::fprintf(stderr, "IMAGE/textura FIFO: escritura fuera de QWC solicitado\n");
                return false;
            }
        std::printf("IMAGE/textura FIFO: %zu pixeles RGBA exactos, rect %d,%d %dx%d\n",
            pixels, rect.x, rect.y, rect.width(), rect.height());
    }
    const double calls = g_perfmon.GetCounter(GSPerfMon::DrawCalls) - previousCalls;
    if (calls < 1)
        return false;
    std::printf("IMAGE/textura: %.0f draw calls GPU\n", calls);
    return !checkFrame || frame(expected);
}

int main(int argc, char** argv)
{
    const bool window = argc == 2 && std::strcmp(argv[1], "--window") == 0;
    const bool checkFrame = window || (argc == 2 && std::strcmp(argv[1], "--frame") == 0);
    if (argc > 1 && !checkFrame)
        return 2;
    GSConfig.Renderer = GSRendererType::DX11;
    GSConfig.UpscaleMultiplier = 1.0f;
    GSConfig.DisableShaderCache = true;
    GSConfig.LinearPresent = GSPostBilinearMode::Off;
    GSConfig.LoadTextureReplacements = false;
    GSConfig.DumpReplaceableTextures = false;
    EmuConfig.CurrentAspectRatio=AspectRatioType::Stretch;
    GSVertexSW::InitStatic(); // Inicializacion original previa al renderer HW.
    bool success=true;
    for(unsigned cycle=0;cycle<(window?2u:1u);++cycle)
    {
        if(window && !gsProbeOpenWindow(64,32))return 1;
        alignas(16) std::array<u8, 0x2000> regs{};
        g_gs_device = std::make_unique<GSDevice11>();
        success = g_gs_device->Create(GSVSyncMode::Disabled, false);
        if (success)
        {
            g_gs_renderer = std::make_unique<GSRendererHW>();
            g_gs_renderer->SetRegsMem(regs.data());
            g_gs_renderer->Reset(true);
            for (auto path : {gow_pcsx2_gs::Path::Path1, gow_pcsx2_gs::Path::Path2, gow_pcsx2_gs::Path::Path3})
                if (!(success = draw(path)))
                    break;
            if (success)
                for (unsigned seed = 0; seed < (checkFrame ? 3u : 1u); ++seed)
                    if (!(success = texturedImage(checkFrame, seed+cycle*3)))
                        break;
            g_gs_renderer->Destroy();
            g_gs_renderer.reset();
        }
        g_gs_device->Destroy();
        g_gs_device.reset();
        gsProbeCloseWindow();
        if(!success)break;
        std::printf("Ciclo GS %u: creacion/dibujo/cierre correctos\n",cycle);
    }
    std::puts(success ? "GS HW procedural: correcto" : "GS HW procedural: fallo");
    return success ? 0 : 1;
}
