// GOW-Port: control procedural del dispositivo DX11 original de PCSX2.
// No ejecuta el juego ni sustituye el parser/rasterizador del GS.
#include "GS/GS.h"
#include "GS/Renderers/DX11/GSDevice11.h"
#include <cstdio>
#include <memory>
#include <vector>

static bool compare(GSTexture* texture, const std::vector<u32>& expected, const char* label)
{
    const GSVector4i rect(0, 0, texture->GetWidth(), texture->GetHeight());
    auto download = g_gs_device->CreateDownloadTexture(rect.width(), rect.height(), GSTexture::Format::Color);
    if (!download)
        return false;
    download->CopyFromTexture(rect, texture, rect, 0);
    download->Flush();
    std::vector<u32> actual(expected.size(), 0);
    if (!download->ReadTexels(rect, actual.data(), rect.width() * sizeof(u32)))
        return false;
    for (size_t i = 0; i < actual.size(); ++i)
    {
        if (actual[i] != expected[i])
        {
            std::fprintf(stderr, "%s: pixel %zu, esperado %08X, recibido %08X\n",
                label, i, expected[i], actual[i]);
            return false;
        }
    }
    std::printf("%s: %zu pixeles RGBA exactos\n", label, actual.size());
    return true;
}

static bool checkDevice(unsigned cycle)
{
    constexpr unsigned width = 73, height = 37, padding = 7;
    constexpr u32 clear = 0xc7915329;
    g_gs_device = std::make_unique<GSDevice11>();
    if (!g_gs_device->Create(GSVSyncMode::Disabled, false))
        return false;
    std::printf("DX11 ciclo %u: %s\n", cycle, g_gs_device->GetName().c_str());
    std::unique_ptr<GSTexture> source(g_gs_device->CreateTexture(width, height, 1, GSTexture::Format::Color, false));
    std::unique_ptr<GSTexture> target(g_gs_device->CreateRenderTarget(width, height, GSTexture::Format::Color, false, false));
    if (!source || !target)
        return false;
    g_gs_device->ClearRenderTarget(target.get(), clear);
    if (!compare(target.get(), std::vector<u32>(width * height, clear), "Clear"))
        return false;
    std::vector<u32> padded((width + padding) * height, 0xdeadbeef);
    std::vector<u32> expected(width * height);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const u32 color = ((x * 3 + y * 11 + cycle) & 255) |
                (((x * 7 + y * 5 + 29) & 255) << 8) |
                (((x * 17 + y * 13 + 83) & 255) << 16) |
                (((x * 19 + y * 23 + 41) & 255) << 24);
            padded[y * (width + padding) + x] = expected[y * width + x] = color;
        }
    if (!source->Update(GSVector4i(0, 0, width, height), padded.data(), (width + padding) * sizeof(u32)))
        return false;
    // GOW-Port: pasa por los shaders originales; copiar con DXGI directamente
    // no comprobaria el estado ni la conversion del dispositivo de PCSX2.
    g_gs_device->StretchRectAuto(source.get(), target.get(), Nearest);
    return compare(target.get(), expected, "Shader COPY");
}

int main()
{
    GSConfig.DisableShaderCache = true;
    GSConfig.Renderer = GSRendererType::DX11;
    bool success = true;
    for (unsigned cycle = 0; cycle < 2; ++cycle)
    {
        success = checkDevice(cycle);
        // GOW-Port: las texturas locales ya estan destruidas al cerrar device.
        if (g_gs_device)
        {
            g_gs_device->Destroy();
            g_gs_device.reset();
        }
        if (!success)
            break;
    }
    std::puts(success ? "DX11 procedural: correcto" : "DX11 procedural: fallo");
    return success ? 0 : 1;
}
