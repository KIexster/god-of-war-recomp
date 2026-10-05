// GOW-Port: enlaces manuales para God of War (SCUS-97399).
// v3: v2 + diagnostico del cuelgue en pc=0x00176A80 (busqueda en arbol de sub_001769F8).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_host_backend.h"
#include "gow_pad2_packet.h"
#include <ps2_recompiled_functions.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <array>
#include <chrono>
#include <fstream>

namespace
{
    uint32_t readGuest32(const uint8_t *rdram, uint32_t addr);
    // libpad2 uses socket handles and an 18-byte payload, unlike libpad's
    // port/slot API and 32-byte status packet. God of War expects state 1 and
    // a button profile beginning with 0xff to identify a DualShock 2.
    struct GowPadSocket { bool open = false; uint32_t port = 0; };
    std::array<GowPadSocket, 2> g_padSockets{};

    void padReturn(R5900Context *ctx, uint32_t result)
    {
        SET_GPR_S32(ctx, 2, static_cast<int32_t>(result));
        ctx->pc = GPR_U32(ctx, 31);
    }

    GowPadSocket *padSocket(R5900Context *ctx)
    {
        const uint32_t handle = GPR_U32(ctx, 4);
        return handle < g_padSockets.size() && g_padSockets[handle].open ? &g_padSockets[handle] : nullptr;
    }

    uint8_t *padBuffer(uint8_t *rdram, uint32_t address, size_t size)
    {
        const uint32_t physical = address & 0x1fffffffu;
        return address && physical < 0x02000000u && size <= 0x02000000u - physical ? rdram + physical : nullptr;
    }

    void gowPad2Init(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        g_padSockets = {};
        padReturn(ctx, 1u);
    }

    void gowPad2CreateSocket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        const uint8_t *params = padBuffer(rdram, GPR_U32(ctx, 4), 12);
        uint32_t port = 0, slot = 0;
        if (params) { std::memcpy(&port, params + 4, 4); std::memcpy(&slot, params + 8, 4); }
        if (!params || port >= 2 || slot != 0 || (GPR_U32(ctx, 5) & 63u)) { padReturn(ctx, 0xffffffffu); return; }
        for (uint32_t handle = 0; handle < g_padSockets.size(); ++handle)
        {
            if (g_padSockets[handle].open) continue;
            g_padSockets[handle] = {true, port};
            std::fprintf(stderr, "[gow-pad2] socket=%u port=%u slot=%u\n", handle, port, slot);
            padReturn(ctx, handle);
            return;
        }
        padReturn(ctx, 0xffffffffu);
    }

    void gowPad2DeleteSocket(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        auto *socket = padSocket(ctx);
        if (socket) socket->open = false;
        padReturn(ctx, socket ? 1u : 0xffffffffu);
    }

    void gowPad2GetState(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        const auto *socket = padSocket(ctx);
        // Only the first port has a host input backend. Keep port 2 disconnected.
        padReturn(ctx, socket && socket->port == 0 ? 1u : 0u);
    }

    void gowPad2GetButtonProfile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        auto *buffer = padBuffer(rdram, GPR_U32(ctx, 5), 4);
        const auto *socket = padSocket(ctx);
        if (!buffer || !socket || socket->port != 0) { padReturn(ctx, 0xffffffffu); return; }
        std::memset(buffer, 0xff, 4);
        padReturn(ctx, 4u);
    }

    void gowPad2Read(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto *buffer = padBuffer(rdram, GPR_U32(ctx, 5), 18);
        const auto *socket = padSocket(ctx);
        uint8_t state[32]{};
        if (!buffer || !socket || socket->port != 0 || !runtime ||
            !runtime->padBackend().readState(0, 0, state, sizeof(state))) { padReturn(ctx, 0xffffffffu); return; }
        if (!IsGamepadAvailable(0))
        {
            state[6] = IsKeyDown(KEY_A) ? 0 : IsKeyDown(KEY_D) ? 255 : 128;
            state[7] = IsKeyDown(KEY_W) ? 0 : IsKeyDown(KEY_S) ? 255 : 128;
            state[4] = IsKeyDown(KEY_J) ? 0 : IsKeyDown(KEY_L) ? 255 : 128;
            state[5] = IsKeyDown(KEY_I) ? 0 : IsKeyDown(KEY_K) ? 255 : 128;
        }
        // Opt-in smoke test: advance title/new-game prompts without host UI input.
        // Capture the GS's own presented pixels; normal play never injects input.
        static const bool smokeTest = [] {
            const char *value = std::getenv("GOW_PAD_TEST");
            return value && std::strcmp(value, "1") == 0;
        }();
        static const auto testStart = std::chrono::steady_clock::now();
        if (smokeTest)
        {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - testStart).count();
            constexpr double presses[] = {5, 12, 20, 28, 36, 52, 60, 68, 76, 84, 100, 116, 132};
            for (size_t i = 0; i < std::size(presses); ++i)
                if (seconds >= presses[i] && seconds < presses[i] + 0.7)
                {
                    if (i == 0) state[2] &= ~0x08u; // Start
                    else state[3] &= ~0x40u; // Cross
                }
            static size_t capture = 0;
            constexpr double captures[] = {4, 10, 18, 26, 34, 44, 56, 70, 90, 110, 130, 160, 190, 240, 360, 480, 580};
            if (capture < std::size(captures) && seconds >= captures[capture])
            {
                std::vector<uint8_t> pixels;
                uint32_t width = 0, height = 0, display = 0, source = 0;
                bool preferred = false;
                if (runtime->gs().copyLatchedHostPresentationFrame(pixels, width, height, &display, &source, &preferred))
                {
                    const std::string name = "gow_pad_test_" + std::to_string(capture) + ".ppm";
                    std::ofstream file(name, std::ios::binary);
                    file << "P6\n" << width << ' ' << height << "\n255\n";
                    for (size_t i = 0; i + 3 < pixels.size(); i += 4)
                        file.write(reinterpret_cast<const char *>(pixels.data() + i), 3);
                    std::fprintf(stderr, "[gow-pad2:test] frame=%s seconds=%.2f\n", name.c_str(), seconds);
                    uint32_t gameState = 0, pending = 0;
                    std::memcpy(&gameState, rdram + 0x0029E560u, sizeof(gameState));
                    std::memcpy(&pending, rdram + 0x0029E574u, sizeof(pending));
                    std::fprintf(stderr, "[gow-pad2:test] state=%u pending=%u stage=%u levelReady=%u flashReady=%u movie=%u\n",
                                 gameState, pending, readGuest32(rdram, 0x29E5A0u), readGuest32(rdram, 0x29E584u),
                                 readGuest32(rdram, 0x29CAB4u), readGuest32(rdram, 0x29C838u));
                    if (std::getenv("GOW_RENDER_DIAG"))
                    {
                        static bool renderDumped = false;
                        if (gameState == 11u && !renderDumped)
                        {
                            renderDumped = true;
                            std::ofstream code("gow_vu1_code.bin", std::ios::binary);
                            code.write(reinterpret_cast<const char *>(runtime->memory().getVU1Code()), 0x4000);
                            std::ofstream data("gow_vu1_data.bin", std::ios::binary);
                            data.write(reinterpret_cast<const char *>(runtime->memory().getVU1Data()), 0x4000);
                            std::ofstream ram("gow_render_ram.bin", std::ios::binary);
                            ram.write(reinterpret_cast<const char *>(rdram), 0x02000000u);
                        }
                        const auto snapshot = runtime->gs().getDebugSnapshot();
                        std::fprintf(stderr, "[gow-gs] ctxFbp=%u,%u display=%u source=%u\n", snapshot.ctx[0].frame.fbp,
                                     snapshot.ctx[1].frame.fbp, display, source);
                        unsigned count = 0;
                        const auto history = runtime->gs().getDebugHistory();
                        for (auto event = history.rbegin(); event != history.rend() && count < 16; ++event)
                            if (event->kind == GSDebugEventKind::Draw)
                            {
                                ++count;
                                std::fprintf(stderr, "[gow-gs:draw] prim=%u tex=%u fbp=%u xy=%g,%g:%g,%g z=%g:%g a=%u:%u test=%llx alpha=%llx\n",
                                             unsigned(event->prim.type), unsigned(event->prim.tme), event->frame.fbp,
                                             event->xMin, event->yMin, event->xMax, event->yMax, event->zMin, event->zMax,
                                             event->aMin, event->aMax, static_cast<unsigned long long>(event->test),
                                             static_cast<unsigned long long>(event->alpha));
                            }
                    }
                }
                ++capture;
            }
        }
        const auto packet = gow_pad2::makePacket(state);
        std::memcpy(buffer, packet.data(), packet.size());
        const uint16_t buttons = static_cast<uint16_t>(state[2] | (state[3] << 8));
        static unsigned reads = 0;
        static uint16_t lastButtons = 0xffff;
        if (++reads <= 3 || buttons != lastButtons)
            std::fprintf(stderr, "[gow-pad2] read buttons=%04x sticks=%u,%u,%u,%u\n", buttons, buffer[2], buffer[3], buffer[4], buffer[5]);
        lastButtons = buttons;
        if (std::getenv("GOW_ANM_DIAG") && readGuest32(rdram, 0x29E560u) == 4u && reads % 100 == 0)
        {
            const uint32_t card = readGuest32(rdram, 0x29BE50u);
            std::fprintf(stderr, "[gow-transition] padType=%u cardState=%u pause=%u,%u,%u,%u speed=%x\n",
                         readGuest32(rdram, 0x2FD9C8u), readGuest32(rdram, card + 0x278u),
                         readGuest32(rdram, 0x29E564u), readGuest32(rdram, 0x29E568u),
                         readGuest32(rdram, 0x29E56Cu), readGuest32(rdram, 0x29E570u), readGuest32(rdram, 0x32F1F0u));
        }
        padReturn(ctx, 18u);
    }

    void gowVibGetProfile(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        // No rumble capability yet; the game skips actuator updates for count 0.
        padReturn(ctx, 0u);
    }

    // Sectores de la capa 0 del DVD (donde empieza la capa 1). Medido en la ISO del usuario.
    constexpr uint32_t kLayer1StartLbn = 2080544u;

    void writeGuest32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        if (addr == 0u) return;
        std::memcpy(rdram + (addr & 0x01FFFFFFu), &value, sizeof(value));
    }

    uint32_t readGuest32(const uint8_t *rdram, uint32_t addr)
    {
        uint32_t v;
        std::memcpy(&v, rdram + (addr & 0x01FFFFFFu), sizeof(v));
        return v;
    }

    uint16_t readGuest16(const uint8_t *rdram, uint32_t addr)
    {
        uint16_t v;
        std::memcpy(&v, rdram + (addr & 0x01FFFFFFu), sizeof(v));
        return v;
    }

    void gowDiagPathSelect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == 0x00180E50u && std::getenv("GOW_PATH_DIAG"))
        {
            const uint32_t object = GPR_U32(ctx, 4);
            const uint32_t address = GPR_U32(ctx, 5);
            const auto *text = padBuffer(rdram, address, 256);
            std::fprintf(stderr, "[gow-path] iterator=0x%x parent=0x%x child=%.255s\n", object,
                         readGuest32(rdram, object + 4), text ? reinterpret_cast<const char *>(text) : "<invalid>");
        }
        sub_00180E50_0x180e50(rdram, ctx, runtime);
    }

    void gowDiagAttachNode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == 0x00180D08u && GPR_U32(ctx, 5) == 0 && std::getenv("GOW_PATH_DIAG"))
        {
            std::ofstream file("gow_path_failure.bin", std::ios::binary);
            file.write(reinterpret_cast<const char *>(rdram), 0x02000000u);
            std::fprintf(stderr, "[gow-path] NULL node iterator=0x%x ra=0x%x; RAM saved\n", GPR_U32(ctx, 4), GPR_U32(ctx, 31));
        }
        sub_00180D08_0x180d08(rdram, ctx, runtime);
    }

    // SCUS-97399 sceIpuInit uses SetD4_CHCR at 0x279588 and tables at
    // 0x2a1610/0x2a1660. The generic stub's hard-coded 0x126428 is a
    // FilteredCopyTile continuation in this ELF, so initialize MMIO directly.
    void gowIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto &memory = runtime->memory();
        memory.write32(0x1000B400u, 1u);
        memory.write32(0x10002010u, 0x40000000u);
        memory.write32(0x10002000u, 0u);
        for (uint32_t offset : {0u, 16u, 32u, 48u, 64u, 64u, 64u, 64u})
            memory.write128(0x10007010u, runtime->Load128(rdram, ctx, 0x002A1610u + offset));
        memory.write32(0x10002000u, 0x50000000u);
        memory.write32(0x10002000u, 0x58000000u);
        for (uint32_t offset : {0u, 16u})
            memory.write128(0x10007010u, runtime->Load128(rdram, ctx, 0x002A1660u + offset));
        memory.write32(0x10002000u, 0x60000000u);
        memory.write32(0x10002000u, 0x90000000u);
        memory.write32(0x10002010u, 0x40000000u);
        memory.write32(0x10002000u, 0u);
        std::fprintf(stderr, "[gow-ipu] initialized using SCUS-97399 tables\n");
        padReturn(ctx, 0u);
    }

    // Optional FMV bypass at the movie API, before buffers/RPCs are allocated.
    // Keep the normal MPEG path available for decoder investigation.
    void gowSkipMovieLoad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        const auto *name = padBuffer(rdram, GPR_U32(ctx, 4), 128);
        std::fprintf(stderr, "[gow-fmv] skipped %.127s\n", name ? reinterpret_cast<const char *>(name) : "<invalid>");
        writeGuest32(rdram, 0x0029C838u, 0u);
        writeGuest32(rdram, 0x0029C870u, 0u);
        writeGuest32(rdram, 0x0029C840u, 0u);
        padReturn(ctx, 0u);
    }

    void gowSkipMovieReady(uint8_t *, R5900Context *ctx, PS2Runtime *) { padReturn(ctx, 1u); }
    void gowSkipMovieNoop(uint8_t *, R5900Context *ctx, PS2Runtime *) { padReturn(ctx, 0u); }

    void gowDiagFlashReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static unsigned calls = 0;
        if (++calls <= 30)
            std::fprintf(stderr, "[gow-flash-ready] a0=%x event=%x ra=%x\n", GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 31));
        sub_001B2390_0x1b2390(rdram, ctx, runtime);
    }

    void gowDiagAnimationTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31), object = GPR_U32(ctx, 4);
        const char *fastBoot = std::getenv("GOW_FAST_BOOT");
        if (fastBoot && std::strcmp(fastBoot, "1") == 0 && ra == 0x0021E714u &&
            readGuest32(rdram, 0x29E560u) == 4u && readGuest32(rdram, 0x29E584u) == 1u)
        {
            // Only the intro-completion query is bypassed, after the level load.
            // No animation state or game-state flags are changed.
            sub_00100BF0_0x100bf0(rdram, ctx, runtime);
            return;
        }
        sub_00100C50_0x100c50(rdram, ctx, runtime);
        static unsigned samples = 0;
        if (readGuest32(rdram, 0x29E560u) == 4u && ctx->pc == ra && samples++ % 100 == 0)
        {
            float delta = 0;
            std::memcpy(&delta, rdram + 0x29C64Cu, sizeof(delta));
            std::fprintf(stderr, "[gow-animation] object=%x time=%g delta=%g\n", object, ctx->f[0], delta);
        }
    }

    void gowDiagAnimationDuration(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        sub_00100BF0_0x100bf0(rdram, ctx, runtime);
        static unsigned samples = 0;
        if (readGuest32(rdram, 0x29E560u) == 4u && ctx->pc == ra && samples++ % 100 == 0)
            std::fprintf(stderr, "[gow-animation] duration=%g\n", ctx->f[0]);
    }

    void gowDiagFilteredCopy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static unsigned entries = 0, resumes = 0;
        if (ctx->pc == 0x001262C8u && ++entries <= 30)
            std::fprintf(stderr, "[gow-filter] entry a0=%x a1=%x a2=%x a3=%x t0=%x t1=%x t2=%x t3=%x ra=%x sp=%x\n",
                         GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7),
                         GPR_U32(ctx, 8), GPR_U32(ctx, 9), GPR_U32(ctx, 10), GPR_U32(ctx, 11),
                         GPR_U32(ctx, 31), GPR_U32(ctx, 29));
        if (ctx->pc == 0x00126528u && ++resumes <= 40)
            std::fprintf(stderr, "[gow-filter] resume index=%x limit=%x rows=%x ra=%x sp=%x\n",
                         GPR_U32(ctx, 10), GPR_U32(ctx, 12), GPR_U32(ctx, 14), GPR_U32(ctx, 31), GPR_U32(ctx, 29));
        sub_001262C8_0x1262c8(rdram, ctx, runtime);
    }

    // int sceCdReadDvdDualInfo(int *on_dual, unsigned int *layer1_start)
    void gowCdReadDvdDualInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        writeGuest32(rdram, GPR_U32(ctx, 4), 1u);
        writeGuest32(rdram, GPR_U32(ctx, 5), kLayer1StartLbn);
        SET_GPR_U32(ctx, 2, 1u);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Volcado hex + ASCII de memoria del EE para el registro de comandos.
    void dumpGuestBytes(const uint8_t *rdram, uint32_t addr, uint32_t len)
    {
        for (uint32_t off = 0; off < len; off += 16u)
        {
            char hex[16 * 3 + 1] = {};
            char asc[17] = {};
            const uint32_t n = (len - off < 16u) ? (len - off) : 16u;
            for (uint32_t i = 0; i < n; ++i)
            {
                const uint8_t c = rdram[(addr + off + i) & 0x01FFFFFFu];
                std::snprintf(hex + i * 3, 4, "%02x ", c);
                asc[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
            }
            std::fprintf(stderr, "[gow-snd]     +%03x: %-48s %s\n", off, hex, asc);
        }
    }

    // sub_0026BF28(cmd, tamano, datos): envio de comandos al driver de sonido 989snd (version EE).
    // El original hace sceSifCallRpc(sid 0x123456, rpc = cmd, envio = 0x305640 [tamano bytes],
    // respuesta = 0x305600 [12 bytes]) y devuelve la palabra 1 de la respuesta.
    // cmd 0x68 = mensaje para un plugin de 989snd (smpd, el cargador de datos): 'datos' apunta a
    // {u32 a, u32 b, u32 len, u32 ptr} y se envian los 12 primeros bytes + len bytes copiados de ptr.
    // Desde que el emulador del IOP ejecuta los IRX originales (989NOMID.IRX + SMPD_IOP.IRX, copiados en
    // IOP_MOD/ junto al ELF), el comando se pasa al original; aqui solo registramos lo que pide el juego.
    // Con GOW_SND_STUB=1 se vuelve al comportamiento antiguo (responder 0 sin pasar por el IOP).
    int g_sndCmdCount = 0;
    const bool g_sndStub = []
    {
        const char *v = std::getenv("GOW_SND_STUB");
        return v != nullptr && v[0] == '1';
    }();

    void gowSnd989SendCommand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != 0x0026BF28u) // reanudacion tras un checkpoint dentro del original
        {
            sub_0026BF28_0x26bf28(rdram, ctx, runtime);
            return;
        }
        const uint32_t cmd = GPR_U32(ctx, 4);
        const uint32_t size = GPR_U32(ctx, 5);
        const uint32_t data = GPR_U32(ctx, 6);
        if (++g_sndCmdCount <= 300)
        {
            if (cmd == 0x68u)
            {
                const uint32_t a = readGuest32(rdram, data);
                const uint32_t b = readGuest32(rdram, data + 4u);
                const uint32_t len = readGuest32(rdram, data + 8u);
                const uint32_t ptr = readGuest32(rdram, data + 12u);
                std::fprintf(stderr, "[gow-snd] #%d cmd=0x68 (plugin) tamano=0x%x a=0x%x b=0x%x len=0x%x ptr=0x%x ra=0x%x\n",
                             g_sndCmdCount, size, a, b, len, ptr, GPR_U32(ctx, 31));
                dumpGuestBytes(rdram, ptr, (len < 0x80u) ? len : 0x80u);
            }
            else
            {
                std::fprintf(stderr, "[gow-snd] #%d cmd=0x%x tamano=0x%x datos=0x%x ra=0x%x\n",
                             g_sndCmdCount, cmd, size, data, GPR_U32(ctx, 31));
                dumpGuestBytes(rdram, data, (size < 0x40u) ? size : 0x40u);
            }
        }
        if (g_sndStub)
        {
            SET_GPR_U32(ctx, 2, 0u);
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        const uint32_t ra = GPR_U32(ctx, 31);
        sub_0026BF28_0x26bf28(rdram, ctx, runtime);
        if (g_sndCmdCount <= 300 && ctx->pc == ra)
        {
            std::fprintf(stderr, "[gow-snd]   -> v0=0x%x\n", GPR_U32(ctx, 2));
            // Lectura de smpd (snd_DoExternCall 'SMPD' tipo 6: {.., destino@+0xC, tamano@+0x10, ..}): primeros bytes leidos
            if (cmd == 0x4Cu && readGuest32(rdram, data) == 0x534D5044u && readGuest32(rdram, data + 4u) == 6u)
            {
                const uint32_t dst = readGuest32(rdram, data + 12u);
                std::fprintf(stderr, "[gow-snd]   datos leidos en 0x%x:\n", dst);
                dumpGuestBytes(rdram, dst, 0x30u);
            }
        }
    }

    // sceSifLoadStartModuleBuffer(iopAddr, argLen, args, int *result) usado por el juego para cargar un
    // modulo IOP minimo embebido en el ELF ("ck01"). El runtime aun no puede ejecutar modulos cargados
    // desde memoria del IOP, asi que respondemos como una consola retail: el modulo se carga (id valido)
    // y termina con NO_RESIDENT_END (1), que es el valor con el que el juego continua normalmente.
    void gowLoadStartModuleBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        writeGuest32(rdram, GPR_U32(ctx, 7), 1u);
        SET_GPR_U32(ctx, 2, 0x40001000u);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // ---- Diagnostico del cuelgue en pc=0x00176A80 ----
    // sub_001769F8(raiz, clave) busca en un arbol binario cuyos nodos (16 bytes) viven en un pool
    // creado por sub_00175CD0 (8000 nodos):
    //   nodo+0 = clave, nodo+4 = valor, nodo+0xA = hijo izq (indice u16), nodo+0xC = hijo der (indice u16)
    //   direccion de un hijo = *0x29C4BC + indice*16; *0x29C4B4 = nodo centinela (fin de rama);
    //   *0x29C4B8 = handle del pool.
    // El bucle solo es infinito si el arbol tiene un ciclo. Este envoltorio recorre el arbol en solo
    // lectura antes de llamar al original y vuelca los datos la primera vez que detecta un ciclo.
    int g_treeLookupCalls = 0;
    bool g_treeCycleReported = false;

    void gowTreeLookupDiag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == 0x001769F8u && !g_treeCycleReported) // llamada nueva (no reanudacion tras checkpoint)
        {
            const uint32_t rootPtr = GPR_U32(ctx, 4);
            const uint32_t key = GPR_U32(ctx, 5);
            const uint32_t sentinel = readGuest32(rdram, 0x0029C4B4u);
            const uint32_t pool = readGuest32(rdram, 0x0029C4B8u);
            const uint32_t base = readGuest32(rdram, 0x0029C4BCu);
            if (++g_treeLookupCalls <= 5)
                std::fprintf(stderr, "[gow-tree] call#%d root=0x%x *root=0x%x key=0x%x sentinel=0x%x pool=0x%x base=0x%x ra=0x%x\n",
                             g_treeLookupCalls, rootPtr, readGuest32(rdram, rootPtr), key, sentinel, pool, base, GPR_U32(ctx, 31));

            uint32_t node = readGuest32(rdram, rootPtr);
            if (node != 0u && sentinel != 0u)
            {
                int steps = 0;
                while (node != sentinel && steps < 10000)
                {
                    const uint32_t k = readGuest32(rdram, node);
                    if (k == key) break;
                    node = base + ((key < k) ? readGuest16(rdram, node + 0xAu) : readGuest16(rdram, node + 0xCu)) * 16u;
                    ++steps;
                }
                if (steps >= 10000)
                {
                    g_treeCycleReported = true;
                    std::fprintf(stderr, "[gow-tree] CICLO en la llamada #%d: root=0x%x key=0x%x sentinel=0x%x(idx %u) pool=0x%x base=0x%x ra=0x%x\n",
                                 g_treeLookupCalls, rootPtr, key, sentinel, (sentinel - base) >> 4, pool, base, GPR_U32(ctx, 31));
                    node = readGuest32(rdram, rootPtr);
                    for (int i = 0; i < 24 && node != sentinel; ++i)
                    {
                        const uint32_t k = readGuest32(rdram, node);
                        std::fprintf(stderr, "[gow-tree]   nodo 0x%x idx=%u clave=0x%x valor=0x%x h8=%u izq=%u der=%u\n",
                                     node, (node - base) >> 4, k, readGuest32(rdram, node + 4u),
                                     readGuest16(rdram, node + 8u), readGuest16(rdram, node + 0xAu), readGuest16(rdram, node + 0xCu));
                        node = base + ((key < k) ? readGuest16(rdram, node + 0xAu) : readGuest16(rdram, node + 0xCu)) * 16u;
                    }
                }
            }
        }
        sub_001769F8_0x1769f8(rdram, ctx, runtime);
    }

    // Resultado del diagnostico (run del 2026-10-05): el arbol esta bien; el cuelgue viene de una llamada a
    // sub_00175890(dicc, clave) con dicc = NULL (raiz leida en 0x4 => basura con ciclo). sub_00175890 es el
    // "buscar en diccionario" generico (via sub_00175A70 / sub_00175AB0, 15 llamadores) y devuelve el valor
    // (& 0x7FFFFFFF) o 0 si no existe, guardando ademas "no encontrado" en dicc+0.
    // Guardia provisional: con dicc NULL devolvemos 0 ("no encontrado") sin tocar memoria baja, y volcamos
    // la pila para localizar quien pasa el NULL.
    int g_nullDictCalls = 0;

    void gowDictFindGuard(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dict = GPR_U32(ctx, 4);
        if (ctx->pc == 0x00175890u && dict < 0x00100000u)
        {
            if (++g_nullDictCalls <= 4)
            {
                const uint32_t sp = GPR_U32(ctx, 29);
                std::fprintf(stderr, "[gow-dict] NULL #%d dicc=0x%x clave=0x%x ra=0x%x sp=0x%x ra_envoltorio=0x%x\n",
                             g_nullDictCalls, dict, GPR_U32(ctx, 5), GPR_U32(ctx, 31), sp, readGuest32(rdram, sp));
                // Pila heuristica: palabras de la pila que parecen direcciones de retorno en el codigo del ELF
                std::fprintf(stderr, "[gow-dict]   pila:");
                for (uint32_t off = 0; off < 0x200u; off += 4u)
                {
                    const uint32_t w = readGuest32(rdram, sp + off);
                    if (w >= 0x00100008u && w < 0x002A0000u && (w & 3u) == 0u)
                        std::fprintf(stderr, " +%x:0x%x", off, w);
                }
                std::fprintf(stderr, "\n");
            }
            SET_GPR_U32(ctx, 2, 0u);
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        sub_00175890_0x175890(rdram, ctx, runtime);
    }

    // smpd (SMPD_IOP.IRX) lee GODOFWAR.TOC / PART*.PAK por numero de sector con sceCdRead. Sin imagen de
    // disco, el IOP monta una ISO virtual con los archivos extraidos; con la ISO original los sectores son
    // exactamente los del DVD. Orden: variable GOW_ISO, "../God of War.iso" respecto al ELF, o el primer
    // .iso de la carpeta del ELF.
    void configureGowCdImage()
    {
        namespace fs = std::filesystem;
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        if (!paths.cdImage.empty())
            return;
        std::error_code ec;
        fs::path image;
#ifdef _WIN32
        // Windows environment variables are UTF-16; getenv's ANSI bytes are not UTF-8.
        if (const wchar_t *env = _wgetenv(L"GOW_ISO"); env != nullptr && env[0] != L'\0')
            image = fs::path(env);
#else
        if (const char *env = std::getenv("GOW_ISO"); env != nullptr && env[0] != '\0')
            image = fs::u8path(env);
#endif
        else if (!paths.elfDirectory.empty())
        {
            const fs::path sibling = paths.elfDirectory.parent_path() / "God of War.iso";
            if (fs::is_regular_file(sibling, ec))
                image = sibling;
            else
                for (const auto &entry : fs::directory_iterator(paths.elfDirectory, ec))
                    if (entry.is_regular_file(ec) && entry.path().extension() == ".iso")
                    {
                        image = entry.path();
                        break;
                    }
        }
        if (image.empty() || !fs::is_regular_file(image, ec))
        {
            std::fprintf(stderr, "[gow-cd] sin imagen de disco: el IOP usara la ISO virtual de la carpeta extraida\n");
            return;
        }
        paths.cdImage = image;
        PS2Runtime::setIoPaths(paths);
        const auto imageUtf8 = image.u8string();
        std::fprintf(stderr, "[gow-cd] imagen de disco: %s\n", reinterpret_cast<const char *>(imageUtf8.c_str()));
    }

    // Diagnostico: sub_0023A000(ctx, obj, nodo) empieza con una llamada virtual
    // (*(obj+0x20))->fn@+0x14(obj + ajuste@+0x10). Registramos el destino de las primeras llamadas.
    int g_vcall23A000 = 0;

    void gowDiag23A000(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == 0x0023A000u && g_vcall23A000 < 20)
        {
            ++g_vcall23A000;
            const uint32_t obj = GPR_U32(ctx, 5);
            const uint32_t vtbl = readGuest32(rdram, obj + 0x20u);
            const uint32_t fn = readGuest32(rdram, vtbl + 0x14u);
            const int16_t adj = static_cast<int16_t>(readGuest16(rdram, vtbl + 0x10u));
            std::fprintf(stderr, "[gow-23a000] #%d a0=0x%x obj=0x%x a2=0x%x vtbl=0x%x fn=0x%x ajuste=%d tipo=%u ra=0x%x\n",
                         g_vcall23A000, GPR_U32(ctx, 4), obj, GPR_U32(ctx, 6), vtbl, fn, adj,
                         readGuest16(rdram, obj), GPR_U32(ctx, 31));
        }
        sub_0023A000_0x23a000(rdram, ctx, runtime);
    }

    // Diagnostico de las alarmas de temporizador de libkernel (Timer 2). cbTimerHandler (0x299D08) recorre la
    // lista *0x2A5B10 y llama al callback de cada nodo (nodo+0x28); se observo un nodo 0xFFF1F1E1 con callback 0.
    int g_timerDiag = 0;

    void dumpAlarmList(const uint8_t *rdram, const char *tag)
    {
        const uint32_t head = readGuest32(rdram, 0x002A5B10u);
        std::fprintf(stderr, "[gow-timer] %s cabeza=0x%x libres=0x%x n=%d actual=0x%x\n", tag, head,
                     readGuest32(rdram, 0x002A5B0Cu), static_cast<int>(readGuest32(rdram, 0x002A5B08u)),
                     readGuest32(rdram, 0x002A5B14u));
        uint32_t node = head;
        for (int i = 0; i < 4 && node != 0u && (node & 3u) == 0u && node < 0x02000000u; ++i)
        {
            std::fprintf(stderr, "[gow-timer]   nodo 0x%x: sig=0x%x w1=0x%x id=0x%x flags=0x%x objetivo=0x%x%08x cb=0x%x arg=0x%x\n",
                         node, readGuest32(rdram, node), readGuest32(rdram, node + 4u), readGuest32(rdram, node + 8u),
                         readGuest32(rdram, node + 12u), readGuest32(rdram, node + 0x24u), readGuest32(rdram, node + 0x20u),
                         readGuest32(rdram, node + 0x28u), readGuest32(rdram, node + 0x30u));
            node = readGuest32(rdram, node);
        }
    }

    void gowDiagSetTimerAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool fresh = ctx->pc == 0x0029A4A8u;
        const uint32_t ra = GPR_U32(ctx, 31);
        if (fresh && g_timerDiag < 40)
            std::fprintf(stderr, "[gow-timer] SetTimerAlarm(a0=0x%x a1=0x%x a2=0x%x a3=0x%x) ra=0x%x\n",
                         GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7), ra);
        sub_0029A4A8_0x29a4a8(rdram, ctx, runtime);
        if (fresh && g_timerDiag < 40 && ctx->pc == ra)
        {
            ++g_timerDiag;
            std::fprintf(stderr, "[gow-timer]   -> 0x%x\n", GPR_U32(ctx, 2));
            dumpAlarmList(rdram, "tras SetTimerAlarm");
        }
    }

    void gowDiagTimerHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == 0x00299D08u)
        {
            const uint32_t head = readGuest32(rdram, 0x002A5B10u);
            static int calls = 0;
            if (++calls <= 3 || (head != 0u && ((head & 3u) != 0u || head >= 0x02000000u)))
            {
                static int bad = 0;
                if (bad < 5)
                {
                    if ((head & 3u) != 0u || head >= 0x02000000u)
                        ++bad;
                    std::fprintf(stderr, "[gow-timer] cbTimerHandler #%d ra=0x%x sp=0x%x\n", calls, GPR_U32(ctx, 31), GPR_U32(ctx, 29));
                    dumpAlarmList(rdram, "en cbTimerHandler");
                }
            }
        }
        sub_00299D08_0x299d08(rdram, ctx, runtime);
    }

    // Vigilancia: memcpy_asm(dst, src, n) (0x16AFD8) que escriba sobre las variables de alarmas de libkernel.
    int g_memcpyWatch = 0;

    void gowWatchMemcpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == 0x0016AFD8u && g_memcpyWatch < 10)
        {
            const uint32_t dst = GPR_U32(ctx, 4) & 0x01FFFFFFu;
            const uint32_t src = GPR_U32(ctx, 5);
            const uint32_t n = GPR_U32(ctx, 6);
            static int fromRing = 0;
            if ((src & 0x01FFFFFFu) >= 0x00530640u && (src & 0x01FFFFFFu) < 0x00570640u && fromRing < 6)
            {
                ++fromRing;
                std::fprintf(stderr, "[gow-watch] memcpy desde el bufer de streaming: dst=0x%x src=0x%x n=0x%x\n", GPR_U32(ctx, 4), src, n);
                dumpGuestBytes(rdram, src, 0x20u);
            }
            if (dst < 0x002A5B20u && dst + n > 0x002A5B00u)
            {
                ++g_memcpyWatch;
                const uint32_t sp = GPR_U32(ctx, 29);
                std::fprintf(stderr, "[gow-watch] memcpy_asm dst=0x%x src=0x%x n=0x%x ra=0x%x  pila:", GPR_U32(ctx, 4), src, n, GPR_U32(ctx, 31));
                for (uint32_t off = 0; off < 0x100u; off += 4u)
                {
                    const uint32_t w = readGuest32(rdram, sp + off);
                    if (w >= 0x00100008u && w < 0x002A0000u && (w & 3u) == 0u)
                        std::fprintf(stderr, " 0x%x", w);
                }
                std::fprintf(stderr, "\n");
            }
        }
        sub_0016AFD8_0x16afd8(rdram, ctx, runtime);
    }

    void applyGowOverrides(PS2Runtime &runtime)
    {
        configureGowCdImage();
        runtime.replaceFunction(0x00279600u, gowIpuInit);
        if (std::getenv("GOW_PATH_DIAG") || std::getenv("GOW_ANM_DIAG") || std::getenv("GOW_FAST_BOOT"))
        {
            runtime.replaceFunction(0x001B2390u, gowDiagFlashReady);
            runtime.replaceFunction(0x00100C50u, gowDiagAnimationTime);
            runtime.replaceFunction(0x00100BF0u, gowDiagAnimationDuration);
        }
        if (const char *skip = std::getenv("GOW_SKIP_FMV"); skip && std::strcmp(skip, "1") == 0)
        {
            runtime.replaceFunction(0x00188CA0u, gowSkipMovieLoad);
            runtime.replaceFunction(0x00188E80u, gowSkipMovieNoop);
            runtime.replaceFunction(0x00188ED8u, gowSkipMovieReady);
            runtime.replaceFunction(0x00188EF0u, gowSkipMovieReady);
            runtime.replaceFunction(0x00188F20u, gowSkipMovieNoop);
        }
        if (std::getenv("GOW_RENDER_DIAG"))
            for (const uint32_t address : {0x001262C8u, 0x00126310u, 0x0012633Cu, 0x00126368u,
                                           0x00126428u, 0x00126478u, 0x00126528u})
                runtime.replaceFunction(address, gowDiagFilteredCopy);
        runtime.replaceFunction(0x00180E50u, gowDiagPathSelect);
        runtime.replaceFunction(0x00180D08u, gowDiagAttachNode);
        runtime.replaceFunction(0x0027B7E8u, gowPad2Init);
        runtime.replaceFunction(0x0027B828u, gowPad2Init);
        runtime.replaceFunction(0x0027B890u, gowPad2CreateSocket);
        runtime.replaceFunction(0x0027B998u, gowPad2DeleteSocket);
        runtime.replaceFunction(0x0027B9F0u, gowPad2Read);
        runtime.replaceFunction(0x0027BAC8u, gowPad2GetButtonProfile);
        runtime.replaceFunction(0x0027BB98u, gowPad2GetState);
        runtime.replaceFunction(0x0027BF90u, gowVibGetProfile);
        runtime.replaceFunction(0x0016AFD8u, gowWatchMemcpy);
        runtime.replaceFunction(0x0023A000u, gowDiag23A000);
        runtime.replaceFunction(0x0029A4A8u, gowDiagSetTimerAlarm);
        runtime.replaceFunction(0x00299D08u, gowDiagTimerHandler);
        // El recompilador descarta estas dos funciones porque empiezan en el delay slot
        // de un "jr ra" suelto de la funcion anterior.
        const bool a = ps2_game_overrides::bindAddressHandler(runtime, 0x00296C48u, "sceSifInitRpc");
        const bool b = ps2_game_overrides::bindAddressHandler(runtime, 0x00294990u, "iWakeupThread");
        // Sin handler en el runtime: la version original espera al CDVD del IOP para siempre.
        const bool c = runtime.replaceFunction(0x0027AB00u, gowCdReadDvdDualInfo);
        const bool d = runtime.replaceFunction(0x0026BF28u, gowSnd989SendCommand);
        const bool e = runtime.replaceFunction(0x00298CE8u, gowLoadStartModuleBuffer);
        const bool f = runtime.replaceFunction(0x001769F8u, gowTreeLookupDiag);
        const bool g = runtime.replaceFunction(0x00175890u, gowDictFindGuard);
        std::fprintf(stderr, "[gow-override] sceSifInitRpc=%d iWakeupThread=%d sceCdReadDvdDualInfo=%d snd989=%d modbuf=%d treeDiag=%d dictGuard=%d\n",
                     a, b, c, d, e, f, g);
    }
}

PS2_REGISTER_GAME_OVERRIDE("God of War (SCUS-97399)", "SCUS_973.99", 0x00100008u, 0u, applyGowOverrides)
