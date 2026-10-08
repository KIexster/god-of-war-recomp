# Controles del wrapper GS usando el runtime ya parcheado, sin datos del juego ni OpenGL.
# Incluir desde el CMakeLists.txt raíz de PS2Recomp después de definir ps2_runtime.
add_executable(gow_gs_replay_test "${CMAKE_CURRENT_LIST_DIR}/../../tests/gs_replay_test.cpp")
target_link_libraries(gow_gs_replay_test PRIVATE ps2_runtime)
