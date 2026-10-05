// GOW-Port: enlaces manuales para God of War (SCUS-97399).
// v3: v2 + diagnostico del cuelgue en pc=0x00176A80 (busqueda en arbol de sub_001769F8).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <ps2_recompiled_functions.h>
#include <cstdio>
#include <cstring>

namespace
{
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
    // Mientras no haya sonido ni smpd, cada comando "termina bien" al instante y devuelve 0, pero
    // registramos lo que pide el juego para poder descifrar el protocolo.
    int g_sndCmdCount = 0;

    void gowSnd989SendCommand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
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
        SET_GPR_U32(ctx, 2, 0u);
        ctx->pc = GPR_U32(ctx, 31);
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

    void applyGowOverrides(PS2Runtime &runtime)
    {
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
