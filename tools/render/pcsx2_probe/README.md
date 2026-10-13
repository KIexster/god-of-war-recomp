# Módulo GS aislado para controles procedurales

Este proyecto compila fuentes **originales** de PCSX2 y una capa Host limitada.
No contiene ni requiere el juego, BIOS, ELF o dumps para estos controles.
La DLL también puede recibir el flujo del runtime mediante el espejo opcional
de [PCSX2_GS.md](../../../docs/PCSX2_GS.md); el renderer habitual se conserva.
Windows x64, MSVC, Windows SDK, CMake/Ninja y CPU AVX2.

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

Si se dispone de cabeceras de PS2Recomp con la cadena del port aplicada,
`-DGOW_RUNTIME_HEADERS=C:/trabajo/PS2Recomp/ps2xRuntime/include` añade el contrato
cliente/cola/DLL de VSync y CSR, sin GPU. La CI Windows activa este sexto control.

Controles GPU optativos:

```powershell
& C:/trabajo/gs-build/gs_dx11_probe.exe
& C:/trabajo/gs-build/gs_gif_draw_probe.exe
& C:/trabajo/gs-build/gs_gif_draw_probe.exe --frame
& C:/trabajo/gs-build/gs_gif_draw_probe.exe --window
& C:/trabajo/gs-build/gs_module_gpu_probe.exe C:/trabajo/gs-build/gow_pcsx2_gs_module.dll
python tools/render/probe_fog_reference.py --module C:/trabajo/gs-build/gow_pcsx2_gs_module.dll --resources C:/trabajo/pcsx2/bin/resources
```

El control FOG por ABI compara 14 patrones de RGB medidos, con FGE apagado y
FOG 0/1/77/128/201/255. Cada mitad de un sprite se comprueba en 32×32 píxeles.
Se envían dos sprites con nieblas distintas en el mismo lote: un clear uniforme
FOG=255 de PCSX2 puede omitir el shader, por lo que ese clear no sirve para
decidir su redondeo. Los datos son sintéticos, sin archivos del juego. Alpha
PCRTC es 255; la regresión del runtime comprueba por separado alpha framebuffer
128. No valida interpolación variable, una escena o rendimiento.

DX11 usa shaders SM4/5 originales y modo Surfaceless. DXC se enlaza con carga
retardada; esos controles no necesitan dxcompiler.dll ni dxil.dll. Esto no
implementa DX12 o Vulkan. El dispositivo selecciona hardware: no se introduce
WARP para atribuirle el resultado de una GPU.

La capa Host ofrece configuración por defecto, recursos de shaders, mensajes
a stderr, registros procedurales y memoria de código SW real cuando se solicita.
`--frame` añade tres patrones distintos a GSvsync original y compara cada salida
PCRTC compuesta. El host cuenta los callbacks y la omisión de presentación
propia de Surfaceless; esos contadores no son FPS. `--window` crea una ventana
propia **oculta**, comprueba el backbuffer DXGI antes de Present y repite dos
ciclos de dispositivo/renderer/ventana, con tres patrones por ciclo. Usa un
contexto ImGui vacío para EndPresent original, sin archivos ini/log de interfaz.
No comprueba imagen visible, redimensionado o pérdida del dispositivo.
Reapertura, captura, dumps y reemplazos de texturas no están implementados;
sus operaciones abortan con diagnóstico. Los objetos
SW originales se conservan; los controles GPU no validan esa ruta. Los resultados
y pendientes de integración están en [PCSX2_GS.md](../../../docs/PCSX2_GS.md).

## ABI experimental de la DLL

`module_api.h` define ABI 1 con estructuras de tamaño fijo (Windows x64),
handles numéricos y estados de error. Crea un único GS por DLL, recibe bytes
GIF prestados durante la llamada y una copia de los **8 KiB de registros PS2
privilegiados**, y permite consultar/copiar la textura PCRTC compuesta RGBA.
Ese layout no es el struct GSRegisters de PS2Recomp: el adaptador debe construir
el espejo de registros. VSync no exige terminar los fragmentos GIF pendientes.
La secuencia cuenta VSync, sin certificar contenido nuevo o imagen visible.

Crear, enviar, VSync, leer y cerrar exige el mismo hilo. Un fallo al procesar
entrada se retiene hasta cerrar; los rechazos de argumentos no consumen datos.
La consulta sin buffer devuelve dimensiones/bytes requeridos; un buffer menor
no recibe píxeles. Leer salida realiza readback y tiene coste explícito.

`gs_module_contract_probe` carga la DLL y comprueba exports/ABI/rechazos sin
dispositivo; se añade a CTest/CI. El control GPU optativo usa solo GetProcAddress,
sin enlazar los globals C++ del renderer: creación repetida, singleton ocupado,
handle cerrado, otro hilo, GIF fragmentado, tamaño de registros, capacidad y
2.048 píxeles RGBA exactos por ciclo. La DLL todavía es **solo renderizado**:
no sustituye FIFO/VRAM del runtime, IRQ/FINISH ni soporta carga a mitad de una
partida, pérdida de dispositivo o todos los caminos de fallo de reserva.
