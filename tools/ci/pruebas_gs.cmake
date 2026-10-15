# Controles del GS usando el runtime ya parcheado, sin datos del juego ni OpenGL.
# Incluir desde el CMakeLists.txt raíz de PS2Recomp después de definir ps2_runtime.
# GOW-Port: las herramientas de replay del GS usaban el rasterizador CPU (gs_cpu_backend.h), que el runtime de
# Waffle565 sustituye por el renderizador nativo; se retiraron con la migración (ver docs/ESTADO.md).
add_executable(gow_gs_finish_async_test "${CMAKE_CURRENT_LIST_DIR}/../../tests/gs_finish_async_test.cpp")
target_link_libraries(gow_gs_finish_async_test PRIVATE ps2_runtime)

# GOW-Port: GPU/contextos WGL reales; no sustituir por un éxito vacío en Linux.
if(WIN32)
    add_executable(gow_gs_pcsx2_client_test "${CMAKE_CURRENT_LIST_DIR}/../../tests/pcsx2_runtime_client_test.cpp")
    target_link_libraries(gow_gs_pcsx2_client_test PRIVATE ps2_runtime)
endif()
