// GOW-Port: FINISH real en ambos modos y píxeles de la textura adquirida, sin juego.
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include "runtime/gs/gs_shared_present.h"
#include "runtime/gs/gs_swizzle.h"
#include "runtime/gs/gs_threaded_backend.h"
#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Host
    {
        bool ownsWindow = false;
        bool captured = false;
        ~Host()
        {
            if (captured)
            {
                GSSharedPresent::RestoreHostContext();
                GSSharedPresent::ShutdownHost();
            }
            if (ownsWindow) CloseWindow();
        }
        void initialize()
        {
            require(!IsWindowReady() && !GSSharedPresent::Active(), "Se requiere un host nuevo");
            SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_WINDOW_UNFOCUSED);
            InitWindow(160, 120, "GOW control procedural FINISH y shared");
            ownsWindow = IsWindowReady();
            require(ownsWindow && rlGetVersion() == RL_OPENGL_33, "Se requiere host OpenGL 3.3");
            GSSharedPresent::CaptureHostContext();
            captured = true;
            GSSharedPresent::RestoreHostContext();
        }
    };

    struct ImageOwner
    {
        Image image{};
        explicit ImageOwner(Texture2D texture) : image(LoadImageFromTexture(texture)) {}
        ~ImageOwner() { if (image.data) UnloadImage(image); }
        ImageOwner(const ImageOwner &) = delete;
        ImageOwner &operator=(const ImageOwner &) = delete;
    };

    GSSharedPresent::Frame acquireAfter(uint64_t sequence)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            GSSharedPresent::Frame frame{};
            if (GSSharedPresent::Acquire(frame) && frame.sequence > sequence) return frame;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("No llega una textura nueva; no se admite una imagen retenida");
    }

    void checkPixels(const GSSharedPresent::Frame &frame, const PresentationFrame &reference,
                     uint8_t expectedRed)
    {
        require(bool(reference) && !frame.blank, "Referencia e imagen adquirida activas");
        require(frame.width == reference.width && frame.height == reference.height,
                "Dimensiones de la textura adquirida");
        require(frame.displayFbp == reference.displayFbp && frame.sourceFbp == reference.sourceFbp &&
                frame.usedPreferred == reference.usedPreferred, "Metadatos de la fuente adquirida");
        const Texture2D texture{frame.texture, int(frame.width), int(frame.height), 1,
                                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ImageOwner readback(texture); // GOW-Port: descarga solo dentro de este control procedural.
        require(readback.image.data && readback.image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 &&
                readback.image.width == int(frame.width) && readback.image.height == int(frame.height),
                "Se requiere lectura real de la textura compartida");
        const auto *pixels = static_cast<const uint8_t *>(readback.image.data);
        require(pixels[0] == expectedRed, "Píxel conocido de la dirección física esperada");
        require(reference.pixels.size() >= size_t(640) * frame.height * 4, "Stride CPU completo");
        const size_t rowBytes = size_t(frame.width) * 4;
        for (uint32_t y = 0; y < frame.height; ++y)
            require(std::equal(pixels + y * rowBytes, pixels + (y + 1) * rowBytes,
                               reference.pixels.begin() + size_t(y) * 640 * 4),
                    "Filas de la textura adquirida distintas de CPU");
    }
}
#endif

int main(int argc, char **argv)
{
    if (argc != 2 || (std::string_view(argv[1]) != "default" && std::string_view(argv[1]) != "async"))
    {
        std::cerr << "Uso: gow_gs_shared_finish_test default|async\n";
        return 2;
    }
#ifndef _WIN32
    std::cerr << "Este control requiere Windows y GPU compartida real\n";
    return 77;
#else
    try
    {
        // GOW-Port: procesos separados; la opción de FINISH se lee una sola vez.
        require(_putenv_s("GOW_GS_FINISH_ASINCRONO", std::string_view(argv[1]) == "async" ? "1" : "0") == 0 &&
                _putenv_s("PS2X_GS_DIRECT_PRESENT", "1") == 0, "No se pudo fijar el modo del control");
        Host host;
        host.initialize();
        std::vector<uint8_t> cpuRam(GSSwizzle::kMemorySize), gpuRam(GSSwizzle::kMemorySize);
        GSCpuBackend cpu;
        cpu.Initialize(cpuRam.data(), uint32_t(cpuRam.size()));
        auto inner = std::make_unique<GSGpuBackend>();
        auto *raw = inner.get();
        inner->SetHardwareRasterAllowed(false);
        GSThreadedBackend gpu(std::move(inner));
        gpu.Initialize(gpuRam.data(), uint32_t(gpuRam.size()));
        GSSharedPresent::RestoreHostContext();
        require(&gpu.Inner() == raw && raw->IsReady() && gpu.UsesSharedPresentation(),
                "GPU real y sharing requeridos; CPU fallback no valida el control");
        GSSharedPresent::Frame startup{};
        require(!GSSharedPresent::Acquire(startup), "El control no hereda una textura anterior");
        GSPresentationRequest request{};
        request.pmode = 1;
        request.dispfb1 = 32ull | (1ull << 9);
        request.hasPreferredSource = true;
        request.preferredDestFbp = 32;
        uint64_t lastSequence = 0, lastRender = 0;
        for (uint32_t step = 0; step < 16; ++step)
        {
            const bool preferred = (step & 1u) == 0;
            const uint8_t sourceRed = uint8_t(31 + step), displayRed = uint8_t(97 + step);
            for (auto *backend : {static_cast<GSRasterBackend *>(&cpu), static_cast<GSRasterBackend *>(&gpu)})
            {
                backend->WriteVram(GS_PSM_CT32, 32, 10, 0, 0, 0x80332200u | sourceRed);
                backend->WriteVram(GS_PSM_CT32, 1024, 1, 0, 0, 0x80665500u | displayRed);
            }
            request.preferredSource = {preferred ? 32u : 1024u, 0, GS_PSM_CT32, 0};
            // GOW-Port: ambas alturas son válidas para el decodificador CRTC de referencia (mínimo 64).
            request.display1 = (63ull << 32) | (uint64_t(step < 8 ? 63 : 95) << 44);
            request.vsyncTick = 100 + step;
            // GOW-Port: ejercitar FINISH del modo elegido antes del Present en la misma cola.
            gpu.Sync(GSSyncReason::Finish);
            const auto reference = cpu.Present(request);
            require(!gpu.Present(request), "La publicación normal no devuelve píxeles RAM");
            const auto frame = acquireAfter(lastSequence);
            require(frame.renderSequence > lastRender && frame.vsyncTick == request.vsyncTick,
                    "Secuencia y tick de la composición nueva");
            require(frame.usedPreferred == preferred, "Selección esperada independiente de CPU/GPU");
            std::cout << "paso=" << step << " seq=" << frame.sequence
                      << " adquirido=" << frame.width << 'x' << frame.height
                      << " CPU=" << reference.width << 'x' << reference.height << '\n';
            checkPixels(frame, reference, preferred ? sourceRed : displayRed);
            lastSequence = frame.sequence;
            lastRender = frame.renderSequence;
        }
        std::cout << "FINISH " << argv[1] << ": 16 texturas nuevas, fuentes, dimensiones y píxeles CPU: OK\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Control compartido: " << error.what() << '\n';
        return 1;
    }
#endif
}
