// GOW-Port: genera C++ para un microprograma de VU1 (16 KB de micromemoria).
// Uso: generar_vu1 <vu1MicroMem.bin> <salida.cpp> [id]
// Cada par de instrucciones se escribe ya decodificado (como lo decodifica el intérprete) y se ejecuta con
// VU1Interpreter::stepPair, así que el resultado y los ciclos son los mismos que al interpretar. El
// programa se registra con el hash del microcódigo; si el juego carga otro código, se interpreta.
// La micromemoria y el C++ generado salen de los datos del juego: no se publican.
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstring>

struct VU1CompiledGenerator
{
    using Pair = VU1Interpreter::DecodedInstructionPair;
    using Usage = VU1Interpreter::InstructionUsage;

    static Pair decode(const VU1Interpreter &vu, const uint8_t *code, uint32_t pc)
    {
        return vu.decodeInstructionPair(code, pc);
    }

    static void usage(std::ostringstream &out, const char *name, const Usage &u)
    {
        const Usage d{};
        for (int i = 0; i < 2; ++i)
            if (u.vfRead[i].reg != 0 || u.vfRead[i].lanes != 0)
                out << " p." << name << ".vfRead[" << i << "] = {" << int(u.vfRead[i].reg) << ", " << int(u.vfRead[i].lanes) << "};";
        if (u.vfWrite.reg != 0 || u.vfWrite.lanes != 0)
            out << " p." << name << ".vfWrite = {" << int(u.vfWrite.reg) << ", " << int(u.vfWrite.lanes) << "};";
#define FIELD(f)          \
    if (u.f != d.f)       \
        out << " p." << name << "." #f " = " << int(u.f) << ";";
        FIELD(vfReadCount)
        FIELD(viRead)
        FIELD(viWrite)
        FIELD(accRead)
        FIELD(accWrite)
        FIELD(latency)
        FIELD(vfLatency)
        FIELD(viLatency)
        FIELD(waitQ)
        FIELD(waitP)
        FIELD(readsClip)
        FIELD(writesClip)
        FIELD(delaysNextBranchRead)
        FIELD(reserved)
#undef FIELD
        if (u.pipeline != d.pipeline)
            out << " p." << name << ".pipeline = static_cast<VU1Interpreter::Pipeline>(" << int(u.pipeline) << ");";
    }

    static std::string pairInitializer(const Pair &p)
    {
        std::ostringstream out;
        out << "[] { P p{}; p.lower = 0x" << std::hex << p.lower << "u; p.upper = 0x" << p.upper << "u;" << std::dec;
        usage(out, "lowerUsage", p.lowerUsage);
        usage(out, "upperUsage", p.upperUsage);
#define FLAG(f)   \
    if (p.f)      \
        out << " p." #f " = true;";
        FLAG(iBit)
        FLAG(eBit)
        FLAG(mBit)
        FLAG(dBit)
        FLAG(tBit)
        FLAG(upperNop)
#undef FLAG
        if (p.upperVfShadowReg)
            out << " p.upperVfShadowReg = " << int(p.upperVfShadowReg) << ";";
        if (p.suppressedLowerVf)
            out << " p.suppressedLowerVf = " << int(p.suppressedLowerVf) << ";";
        out << " return p; }()";
        return out.str();
    }
};

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "Uso: generar_vu1 <salida.cpp> <micromemoria.bin>...\n";
        return 2;
    }
    // Pares distintos que aparecen en cada dirección (el juego carga varios microprogramas en las mismas
    // direcciones con MPG). Las palabras a cero (memoria sin programa) no se compilan.
    std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> variants;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> images; // pc -> imagen de la que sale
    std::vector<std::vector<uint8_t>> codes;
    for (int a = 2; a < argc; ++a)
    {
        std::ifstream file(argv[a], std::ios::binary);
        std::vector<uint8_t> code((std::istreambuf_iterator<char>(file)), {});
        if (code.size() != PS2_VU1_CODE_SIZE)
        {
            std::cerr << argv[a] << ": la micromemoria de VU1 debe tener 16384 bytes\n";
            return 1;
        }
        codes.push_back(std::move(code));
    }
    VU1Interpreter vu;
    std::ostringstream functions, dispatch;
    size_t total = 0;
    std::map<uint32_t, std::vector<std::string>> cases;
    std::set<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> seen;
    for (const auto &code : codes)
    {
        for (uint32_t pc = 0; pc + 8u <= code.size(); pc += 8u)
        {
            uint32_t lower = 0, upper = 0;
            std::memcpy(&lower, code.data() + pc, 4);
            std::memcpy(&upper, code.data() + pc + 4, 4);
            if ((lower == 0u && upper == 0u) || !seen.insert({pc, {lower, upper}}).second)
                continue;
            const auto pair = VU1CompiledGenerator::decode(vu, code.data(), pc);
            if (pair.upperUsage.reserved || pair.lowerUsage.reserved)
                continue;
            char name[32];
            std::snprintf(name, sizeof(name), "p%04X_%zu", pc, cases[pc].size());
            functions << "    struct K" << name << "\n    {\n        static constexpr P value = "
                      << VU1CompiledGenerator::pairInitializer(pair) << ";\n    };\n"
                      << "    static VU1C_NOINLINE bool " << name << "(VU1Interpreter &vu, C &c)\n    {\n"
                      << "        return vu.stepPairT<K" << name << ">(c);\n    }\n";
            std::ostringstream test;
            test << "                if (lower == 0x" << std::hex << lower << "u && upper == 0x" << upper << "u && inRange(pc))\n" << std::dec
                 << "                {\n                    ++g_stats.compiled;\n                    if (!" << name << "(vu, c))\n"
                 << "                        return;\n                    continue;\n                }\n";
            cases[pc].push_back(test.str());
            ++total;
        }
    }
    std::ostringstream out;
    out << "// Generado por tools/vu1/generar_vu1.cpp a partir del microcódigo del juego. No publicar.\n"
        << "#include \"runtime/ps2_vu1.h\"\n#include \"ps2_vu1_compiled.inl\"\n#include <cstring>\n\n"
        << "#if defined(_MSC_VER)\n#define VU1C_NOINLINE __declspec(noinline)\n#else\n#define VU1C_NOINLINE __attribute__((noinline))\n#endif\n\n"
        << "#include <cstdio>\n#include <cstdlib>\n\n"
        << "namespace\n{\n"
        << "    // GOW_VU1C_DIAG: pares compilados e interpretados al salir. GOW_VU1C_RANGO=inicio-fin (hex): solo usa el\n"
        << "    // código compilado en ese rango de direcciones (para acotar una diferencia con el intérprete).\n"
        << "    struct Stats\n    {\n        unsigned long long compiled = 0, interpreted = 0;\n"
        << "        ~Stats()\n        {\n            if (std::getenv(\"GOW_VU1C_DIAG\"))\n"
        << "                std::fprintf(stderr, \"[vu1c] compilados=%llu interpretados=%llu\\n\", compiled, interpreted);\n        }\n    } g_stats;\n"
        << "    struct Range\n    {\n        uint32_t lo = 0, hi = 0xFFFFFFFFu;\n        Range()\n        {\n"
        << "            if (const char *v = std::getenv(\"GOW_VU1C_RANGO\"))\n            {\n"
        << "                char *end = nullptr;\n                lo = static_cast<uint32_t>(std::strtoul(v, &end, 16));\n"
        << "                hi = end && *end == '-' ? static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 16)) : lo;\n            }\n        }\n    } g_range;\n"
        << "    inline bool inRange(uint32_t pc) { return pc >= g_range.lo && pc <= g_range.hi; }\n}\n\n"
        << "template <int Id>\nstruct VU1CompiledProgram;\n\n"
        << "template <>\nstruct VU1CompiledProgram<0>\n{\n"
        << "    using P = VU1Interpreter::DecodedInstructionPair;\n"
        << "    using C = VU1Interpreter::StepContext;\n"
        << functions.str()
        << "    static void run(VU1Interpreter &vu, C &c)\n    {\n        for (;;)\n        {\n"
        << "            const uint32_t pc = vu.m_state.pc;\n"
        << "            if (pc + 8u <= c.codeSize)\n            {\n"
        << "                uint32_t lower, upper;\n"
        << "                std::memcpy(&lower, c.vuCode + pc, 4);\n                std::memcpy(&upper, c.vuCode + pc + 4, 4);\n"
        << "                switch (pc)\n                {\n";
    for (const auto &[pc, tests] : cases)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "0x%04X", pc);
        out << "                case " << name << ":\n";
        for (const auto &t : tests)
            out << t;
        out << "                break;\n";
    }
    out << "                default:\n                    break;\n                }\n            }\n"
        << "            ++g_stats.interpreted;\n"
        << "            if (!vu.stepInterpreted(c))\n                return;\n        }\n    }\n};\n\n"
        << "static const bool g_registered = (VU1Interpreter::registerCompiledProgram(&VU1CompiledProgram<0>::run), true);\n";
    std::ofstream(argv[1], std::ios::binary) << out.str();
    std::printf("[generar_vu1] %zu pares compilados de %zu imagenes\n", total, codes.size());
    return 0;
}
