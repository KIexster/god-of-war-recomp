# GOW-Port: módulo GS procedural Windows. No compila ni necesita el juego.
cmake_minimum_required(VERSION 3.24)
project(gow_pcsx2_probe LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Este control requiere Windows x64 y MSVC")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PCSX2_SOURCE "" CACHE PATH "Fuentes limpias PCSX2 2954ebe")
set(PROBE_GENERATED "" CACHE PATH "Extractos preparados por prepare.py")
set(DIRECTX_HEADERS "" CACHE PATH "Microsoft DirectX-Headers 64c650b")
if(NOT EXISTS "${PROBE_GENERATED}/pcsx2-probe.json" OR NOT EXISTS "${DIRECTX_HEADERS}/include/directx/dxcore.h")
  message(FATAL_ERROR "Preparar extractos y aportar DirectX-Headers antes de configurar")
endif()
execute_process(COMMAND git -C "${PCSX2_SOURCE}" rev-parse HEAD
  OUTPUT_VARIABLE source_revision RESULT_VARIABLE git_result OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT git_result EQUAL 0 OR NOT source_revision STREQUAL "2954ebe6965669219f1bbd256e958fbd8f83a85d")
  message(FATAL_ERROR "PCSX2 no coincide con la revisión fijada")
endif()
execute_process(COMMAND git -C "${PCSX2_SOURCE}" diff --exit-code --quiet "${source_revision}" --
  RESULT_VARIABLE git_result)
if(NOT git_result EQUAL 0)
  message(FATAL_ERROR "Las fuentes fijadas de PCSX2 tienen modificaciones")
endif()
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
add_subdirectory("${PCSX2_SOURCE}/3rdparty/fmt" fmt EXCLUDE_FROM_ALL)
add_subdirectory("${PCSX2_SOURCE}/3rdparty/cpuinfo" cpuinfo EXCLUDE_FROM_ALL)

set(gs_sources GSState GSClut GSLocalMemory GSLocalMemoryMultiISA GSBlock GSTables
  GSVector GSDrawingContext GSDrawingEnvironment GSPerfMon GSUtil GSPng GSXXH GSRingHeap)
set(gs_common GSDevice GSTexture GSVertexTrace GSVertexTraceFMM GSRenderer GSDirtyRect GSFunctionMap)
set(gs_hw GSRendererHW GSRendererHWMultiISA GSTextureCache GSHwHack)
set(gs_sw GSRasterizer GSDrawScanline GSDrawScanlineCodeGenerator.all
  GSSetupPrimCodeGenerator.all GSTextureCacheSW GSRendererSW)
set(gs_dx11 GSDevice11 GSTexture11 D3D11ShaderCache D3D)
set(common_sources AlignedMalloc Assertions Console FileSystem StringUtil Timer SmallString
  Error CrashHandler DynamicLibrary StackWalker Perf MD5Digest HostSys Semaphore
  Windows/WinMisc Windows/WinThreads Windows/WinHostSys)
set(all_sources host.cpp services.cpp
  "${PROBE_GENERATED}/config_extract.cpp" "${PROBE_GENERATED}/gs_extract.cpp"
  "${PROBE_GENERATED}/cpu_features.cpp" "${PROBE_GENERATED}/dump_records.cpp")
foreach(file IN LISTS gs_sources)
  list(APPEND all_sources "${PCSX2_SOURCE}/pcsx2/GS/${file}.cpp")
endforeach()
foreach(group common hw sw dx11)
  string(TOUPPER "${group}" dir)
  if(group STREQUAL "common")
    set(dir Common)
  endif()
  foreach(file IN LISTS gs_${group})
    list(APPEND all_sources "${PCSX2_SOURCE}/pcsx2/GS/Renderers/${dir}/${file}.cpp")
  endforeach()
endforeach()
foreach(file IN LISTS common_sources)
  list(APPEND all_sources "${PCSX2_SOURCE}/common/${file}.cpp")
endforeach()
foreach(file imgui imgui_draw imgui_tables imgui_widgets)
  list(APPEND all_sources "${PCSX2_SOURCE}/3rdparty/imgui/src/${file}.cpp")
endforeach()
add_library(gow_pcsx2_probe STATIC ${all_sources})
configure_file(imconfig_probe.h.in "${CMAKE_CURRENT_BINARY_DIR}/imconfig_probe.h" @ONLY)
target_include_directories(gow_pcsx2_probe PUBLIC "${PCSX2_SOURCE}" "${PCSX2_SOURCE}/pcsx2"
  "${PCSX2_SOURCE}/pcsx2/GS"
  "${PCSX2_SOURCE}/3rdparty/fmt/include" "${PCSX2_SOURCE}/3rdparty/fast_float/include"
  "${PCSX2_SOURCE}/3rdparty/winwil/include" "${PCSX2_SOURCE}/3rdparty/imgui/include"
  "${PCSX2_SOURCE}/3rdparty/include" "${PCSX2_SOURCE}/3rdparty/cpuinfo/include"
  "${PCSX2_SOURCE}/3rdparty/xbyak" "${DIRECTX_HEADERS}/include")
target_compile_definitions(gow_pcsx2_probe PUBLIC _M_X86=1 __SSE4_1__=1 UNICODE _UNICODE
  _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WIN32_WINNT=0x0A00 WINVER=0x0A00
  IMGUI_USER_CONFIG="${CMAKE_CURRENT_BINARY_DIR}/imconfig_probe.h")
target_compile_definitions(gow_pcsx2_probe PRIVATE GOW_PROBE_RESOURCES="${PCSX2_SOURCE}/bin/resources")
target_compile_options(gow_pcsx2_probe PUBLIC /O2 /Gy /Gw /arch:AVX2 /EHsc /utf-8 /Zc:preprocessor /Zc:__cplusplus)

# Solo SM4/5 para DX11: DXC se retrasa y no se necesita al ejecutar estos tests.
# Se genera la import library a partir del ABI público, sin copiar una DLL.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dxc.def" "LIBRARY dxcompiler.dll\nEXPORTS\n DxcCreateInstance\n")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dxc.lib"
  COMMAND "${CMAKE_AR}" /nologo "/def:${CMAKE_CURRENT_BINARY_DIR}/dxc.def"
    /machine:x64 "/out:${CMAKE_CURRENT_BINARY_DIR}/dxc.lib"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/dxc.def" VERBATIM)
add_custom_target(probe_dxc_import DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/dxc.lib")
add_dependencies(gow_pcsx2_probe probe_dxc_import)
target_link_libraries(gow_pcsx2_probe PUBLIC fmt::fmt cpuinfo PNG::PNG ZLIB::ZLIB
  OneCore d3d11 dxgi dxguid dbghelp psapi advapi32 d3dcompiler delayimp
  "${CMAKE_CURRENT_BINARY_DIR}/dxc.lib")
target_link_options(gow_pcsx2_probe INTERFACE /OPT:REF /DELAYLOAD:dxcompiler.dll)

get_filename_component(GOW_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
foreach(pair "gs_parser_probe;pcsx2_gif_parser_probe" "gs_dx11_probe;pcsx2_dx11_device_probe"
  "gs_gif_draw_probe;pcsx2_gif_draw_probe" "gs_wrapped_memory_probe;pcsx2_wrapped_memory_probe")
  list(GET pair 0 target)
  list(GET pair 1 file)
  add_executable(${target} "${GOW_REPO}/tests/${file}.cpp")
  target_link_libraries(${target} PRIVATE gow_pcsx2_probe)
endforeach()
enable_testing()
add_test(NAME parser_original COMMAND gs_parser_probe)
add_test(NAME memoria_alias_y_cierre COMMAND gs_wrapped_memory_probe)
# Los dos controles GPU se ejecutan expresamente en un equipo con hardware.
