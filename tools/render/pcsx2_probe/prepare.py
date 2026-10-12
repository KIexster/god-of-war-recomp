#!/usr/bin/env python3
"""GOW-Port: extraer dependencias GS originales para el control Windows aislado.

No descarga archivos, limpia árboles ni lee el juego. Los extractos conservan
la licencia de PCSX2; la revisión y el árbol fuente deben coincidir exactamente.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

REVISION = "2954ebe6965669219f1bbd256e958fbd8f83a85d"
NOTICE = """// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// GOW-Port: extractos de PCSX2 2954ebe para el módulo de prueba aislado.
"""


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end] + "\n"


def prepare(source: Path, output: Path) -> None:
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != REVISION:
        raise ValueError(f"PCSX2 debe estar en {REVISION}; encontrado {actual}")
    subprocess.run(["git", "-C", str(source), "diff", "--exit-code", "--quiet", REVISION, "--"], check=True)
    if output == source or output in source.parents or source in output.parents or output == Path(__file__).resolve().parent:
        raise ValueError("El destino debe ser un directorio separado de las fuentes")
    marker = output / "pcsx2-probe.json"
    if output.exists() and any(output.iterdir()):
        if not marker.exists() or json.loads(marker.read_text(encoding="utf-8")).get("revision") != REVISION:
            raise ValueError("El destino no está vacío ni pertenece a este control")
    output.mkdir(parents=True, exist_ok=True)

    def read(path: str) -> str:
        return (source / path).read_text(encoding="utf-8")

    def write(name: str, text: str) -> None:
        (output / name).write_text(text, encoding="utf-8", newline="\n")

    config = read("pcsx2/Pcsx2Config.cpp")
    result = NOTICE + '''#include "Config.h"
#include "common/Assertions.h"
#include "common/StringUtil.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Pad/Pad.h"
'''
    result += config[config.index("static constexpr FPControlRegister DEFAULT_FPU_FP_CONTROL_REGISTER"):
                     config.index("Pcsx2Config EmuConfig;")]
    constructors = re.findall(r"^Pcsx2Config::(?:\w+::)?\w+\(\)", config, re.M)
    if len(constructors) != 15:
        raise ValueError("La lista de constructores de configuración no coincide")
    for signature in constructors + [
        "bool Pcsx2Config::GSOptions::ShouldDump", "TraceLogFilters::TraceLogFilters()",
        "TraceLogsEE::TraceLogsEE()", "TraceLogsIOP::TraceLogsIOP()", "TraceLogsMISC::TraceLogsMISC()",
        "Pcsx2Config::GamefixOptions& Pcsx2Config::GamefixOptions::DisableAll()",
        "Pcsx2Config::SpeedhackOptions& Pcsx2Config::SpeedhackOptions::DisableAll()",
    ]:
        result += function(config, signature)
    result += 'const char* Pcsx2Config::GSOptions::DEFAULT_CAPTURE_CONTAINER = "mp4";\n'
    card = read("pcsx2/SIO/Memcard/MemoryCardFile.cpp")
    for signature in ["uint FileMcd_GetMtapPort", "uint FileMcd_GetMtapSlot", "bool FileMcd_IsMultitapSlot",
                      "std::string FileMcd_GetDefaultName"]:
        result += function(card, signature)
    result += function(read("pcsx2/SIO/Pad/Pad.cpp"), "Pad::ControllerType Pad::GetDefaultPadType")
    result += "Pcsx2Config EmuConfig;\n"
    write("config_extract.cpp", result)

    gs = read("pcsx2/GS/GS.cpp")
    result = NOTICE + '''#include "GS/GS.h"
#include "GS/GSVector.h"
#include "common/Console.h"
#include "common/RedtapeWindows.h"
#include "common/BitUtils.h"
#include <cstdlib>
static HANDLE s_fh = NULL;
'''
    result += function(gs, "void* GSAllocateWrappedMemory")
    free = function(gs, "void GSFreeWrappedMemory")
    # Al conservar placeholders, MEM_RELEASE sobre ptr solo libera la primera
    # vista. Unirlas primero conserva el rango completo de la reserva original.
    free = free.replace("\tVirtualFreeEx(GetCurrentProcess(), ptr, 0, MEM_RELEASE);", '''\t// GOW-Port: unir todos los placeholders antes de liberar el rango completo.
\tif (repeat > 1 && !VirtualFreeEx(GetCurrentProcess(), ptr, size * repeat, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS))
\t{
\t\tConsole.Error("No se pudieron unir las vistas GS: %u", GetLastError());
\t\tstd::abort();
\t}
\tVirtualFreeEx(GetCurrentProcess(), ptr, 0, MEM_RELEASE);''')
    # GOW-Port: el módulo independiente debe cerrar también el handle del mapa.
    # El original solo borra s_fh; la prueba de ciclos detecta esa fuga.
    free = free.replace("\ts_fh = NULL;", "\t// GOW-Port: cerrar el handle después de desmapear sus vistas.\n\tCloseHandle(s_fh);\n\ts_fh = NULL;")
    result += free
    for signature in ["std::string GetDefaultAdapter()", "u32 GSGetMaxUpscaleMultiplier",
                      "std::pair<u8, u8> GSGetRGBA8AlphaMinMax"]:
        result += function(gs, signature)
    result += '#include "GS/Renderers/Common/GSRenderer.h"\n'
    result += function(gs, "void GSvsync(u32 field, bool registers_written)")
    result += '#include "common/Image.h"\nRGBA8Image::RGBA8Image() = default;\n'
    result += function(read("common/Image.cpp"), "RGBA8Image::RGBA8Image(RGBA8Image&& move)")
    write("gs_extract.cpp", result)

    multi = read("pcsx2/GS/MultiISA.cpp")
    end = "const ProcessorFeatures g_cpu = getProcessorFeatures();"
    result = multi[:multi.index(end) + len(end)] + '\n#include "GS/GSXXH.h"\n'
    for line in multi.splitlines():
        if line.startswith(("u64 (&MultiISAFunctions::GSXXH3", "u32 (&MultiISAFunctions::GSXXH3")):
            result += line + "\n"
    write("cpu_features.cpp", result)

    dump = read("pcsx2/GS/GSDump.cpp")
    write("dump_records.cpp", NOTICE + '#include "GS/GSDump.h"\n' +
          function(dump, "void GSDumpBase::Transfer") + function(dump, "void GSDumpBase::ReadFIFO"))
    write("pcsx2-probe.json", json.dumps({"revision": REVISION, "source": str(source)}, indent=2) + "\n")
    print(f"Extractos preparados en {output}; fuentes originales conservadas")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pcsx2-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.pcsx2_source.resolve(), args.output.resolve())
