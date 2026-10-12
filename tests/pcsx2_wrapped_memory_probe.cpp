// GOW-Port: vistas VRAM repetidas y cierre del handle del módulo aislado.
#include "GS/GS.h"
#include "GS/GSExtra.h"
#include "common/RedtapeWindows.h"
#include <cstdio>

int main()
{
    constexpr size_t bytes = 4 * 1024 * 1024, repeat = 4;
    DWORD before = 0, after = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &before))
        return 1;
    for (unsigned cycle = 0; cycle < 8; ++cycle)
    {
        auto* memory = static_cast<u8*>(GSAllocateWrappedMemory(bytes, repeat));
        if (!memory)
            return 1;
        memory[bytes - 1] = static_cast<u8>(0x91 + cycle);
        memory[(repeat - 1) * bytes] = static_cast<u8>(0x31 + cycle);
        bool alias = true;
        for (size_t view = 0; view < repeat; ++view)
            alias &= memory[view * bytes] == 0x31 + cycle && memory[(view + 1) * bytes - 1] == 0x91 + cycle;
        GSFreeWrappedMemory(memory, bytes, repeat);
        if (!alias)
        {
            std::fprintf(stderr, "VRAM: las vistas no comparten los mismos bytes\n");
            return 1;
        }
        for (size_t view = 0; view < repeat; ++view)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(memory + view * bytes, &info, sizeof(info)) || info.State != MEM_FREE)
            {
                std::fprintf(stderr, "VRAM: vista %zu sigue reservada tras cerrar (estado %lx)\n", view, info.State);
                return 1;
            }
        }
    }
    if (!GetProcessHandleCount(GetCurrentProcess(), &after) || before != after)
    {
        std::fprintf(stderr, "VRAM: handles antes %lu, despues %lu\n", before, after);
        return 1;
    }
    std::puts("VRAM: ocho ciclos, cuatro vistas alias y handles sin fuga");
    return 0;
}
