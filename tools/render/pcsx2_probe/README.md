# Módulo GS aislado para controles procedurales

Este proyecto compila fuentes **originales** de PCSX2 y una capa Host limitada.
No contiene ni requiere el juego, BIOS, ELF o dumps. Tampoco está conectado al
runtime del port. Windows x64, MSVC, Windows SDK, CMake/Ninja y CPU AVX2.

Entradas fijadas:

- PCSX2 `2954ebe6965669219f1bbd256e958fbd8f83a85d`, árbol limpio; `common/`,
  `pcsx2/`, `3rdparty/` y `bin/resources/shaders/` disponibles.
- Microsoft DirectX-Headers `64c650b0c6642ab0f3cdf674e38401e02ccd2460`.
- libpng/zlib instaladas para MSVC. La CI usa vcpkg
  `e456309491fd875487bf02b8a0dca76c1b1b00cc`, `libpng:x64-windows-static-md`.
  Una instalación dinámica necesita sus DLL junto a los ejecutables.

Desde Developer PowerShell, con Python 3 en PATH, elegir directorios propios
para las fuentes, extractos y build. No reutilizar el checkout de un colaborador:

```powershell
python tools/render/pcsx2_probe/prepare.py --pcsx2-source C:/trabajo/pcsx2 --output C:/trabajo/gs-extractos
cmake -S tools/render/pcsx2_probe -B C:/trabajo/gs-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPCSX2_SOURCE=C:/trabajo/pcsx2 -DPROBE_GENERATED=C:/trabajo/gs-extractos -DDIRECTX_HEADERS=C:/trabajo/DirectX-Headers -DCMAKE_TOOLCHAIN_FILE=C:/trabajo/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build C:/trabajo/gs-build --parallel 3
ctest --test-dir C:/trabajo/gs-build --output-on-failure
```

La preparación comprueba revisión/árbol, conserva los avisos GPL-3.0+ y genera
solo extractos de configuración, utilidades, detección CPU y registros de dump.
Rechaza destinos ajenos no vacíos. No cambia las fuentes descargadas. El cierre
de memoria repetida añade dos correcciones locales señaladas GOW-Port: cerrar
el handle y unir los placeholders antes de liberar todo el rango. La prueba
verifica alias, handles y MEM_FREE tras ocho ciclos; no cubre fallos de reserva.

CTest ejecuta parser y memoria **sin dispositivo**. Se compilan también los
controles GPU; ejecutarlos expresamente, uno después del otro, en un PC libre:

```powershell
& C:/trabajo/gs-build/gs_dx11_probe.exe
& C:/trabajo/gs-build/gs_gif_draw_probe.exe
& C:/trabajo/gs-build/gs_gif_draw_probe.exe --frame
```

DX11 usa shaders SM4/5 originales y modo Surfaceless. DXC se enlaza con carga
retardada; esos controles no necesitan dxcompiler.dll ni dxil.dll. Esto no
implementa DX12 o Vulkan. El dispositivo selecciona hardware: no se introduce
WARP para atribuirle el resultado de una GPU.

La capa Host ofrece configuración por defecto, recursos de shaders, mensajes
a stderr, registros procedurales y memoria de código SW real cuando se solicita.
`--frame` añade tres patrones distintos a GSvsync original y compara cada salida
PCRTC compuesta. El host cuenta los callbacks y la omisión de presentación
propia de Surfaceless; esos contadores no son FPS. No hay ventana/presentación.
UI, reapertura, captura, dumps y reemplazos de texturas no están implementados;
sus operaciones abortan con diagnóstico. Los objetos
SW originales se conservan; los controles GPU no validan esa ruta. Los resultados
y pendientes de integración están en [PCSX2_GS.md](../../../docs/PCSX2_GS.md).
