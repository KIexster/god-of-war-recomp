// GOW-Port: dibujo procedural por GIF, GSRendererHW y FIFO originales.
#include "GS/GS.h"
#include "GS/GSPerfMon.h"
#include "GS/Renderers/DX11/GSDevice11.h"
#include "GS/Renderers/HW/GSRendererHW.h"
#include "../tools/render/pcsx2_gif_sink.h"
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

int main()
{
    GSConfig.Renderer = GSRendererType::DX11;
    GSConfig.UpscaleMultiplier = 1.0f;
    GSConfig.DisableShaderCache = true;
    GSConfig.LoadTextureReplacements = false;
    GSConfig.DumpReplaceableTextures = false;
    GSVertexSW::InitStatic(); // Inicializacion original previa al renderer HW.
    alignas(16) std::array<u8, 0x2000> regs{};
    g_gs_device = std::make_unique<GSDevice11>();
    bool success = g_gs_device->Create(GSVSyncMode::Disabled, false);
    if (success)
    {
        g_gs_renderer = std::make_unique<GSRendererHW>();
        g_gs_renderer->SetRegsMem(regs.data());
        g_gs_renderer->Reset(true);
        for (auto path : {gow_pcsx2_gs::Path::Path1, gow_pcsx2_gs::Path::Path2, gow_pcsx2_gs::Path::Path3})
            if (!(success = draw(path)))
                break;
        g_gs_renderer->Destroy();
        g_gs_renderer.reset();
    }
    g_gs_device->Destroy();
    g_gs_device.reset();
    std::puts(success ? "GS HW procedural: correcto" : "GS HW procedural: fallo");
    return success ? 0 : 1;
}
