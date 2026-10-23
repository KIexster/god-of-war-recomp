# Clona PS2Recomp, recompila SCUS_973.99 a C++ y compila el ejecutable para PC.
# Lo llama scripts\2_compilar.cmd, que antes carga el entorno de Visual Studio (vcvars64).
param([switch]$Trazas)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$elf  = Get-GowElf
$work = Get-GowWorkDir
New-Item -ItemType Directory -Force -Path $work | Out-Null
$rec  = Join-Path $work 'PS2Recomp'
$gen  = Join-Path $work 'generado'
$bld  = Join-Path $rec 'out\build'

$template  = Join-Path $RepoRoot 'config\recomp.template.toml'
$funcmap   = Join-Path $RepoRoot 'config\funcmap.csv'
# GOW-Port: runtime de Waffle565/God-Of-War-Native-Port (rama runtime),
# que ya incluye la pila de parches de este repositorio al 2026-10-09. Los parches anteriores
# siguen en patches\anteriores\ como referencia y no se aplican.
$waffleRuntimePatch = Join-Path $RepoRoot 'patches\ps2recomp-waffle-runtime.patch'
# GOW-Port: retira de ps2xTest las pruebas de piezas que el runtime de Waffle565 sustituye por diseño.
$waffleTestsPatch = Join-Path $RepoRoot 'patches\ps2recomp-waffle-tests.patch'
# GOW-Port: el runtime de Waffle565 se compila con MSVC; esto lo deja compilar y enlazar con GCC (CI de Linux).
$waffleGccPatch = Join-Path $RepoRoot 'patches\ps2recomp-waffle-gcc.patch'
# GOW-Port: el NativeState de la VU1 nativa se perdía con cada VU1Interpreter (std::shared_ptr).
$vu1nLeakPatch = Join-Path $RepoRoot 'patches\ps2recomp-vu1n-leak.patch'
# GOW-Port: rutas rápidas del UNPACK de VIF1 (de la base anterior), con STMOD 3 como la ruta genérica de Waffle565.
$vif1UnpackRapidoPatch = Join-Path $RepoRoot 'patches\ps2recomp-vif1-unpack-rapido.patch'
# GOW-Port: búferes de las cadenas DMA reutilizables (el EE aplanaba ~2,6 MB por cuadro en un vector nuevo).
$dmaBuffersPatch = Join-Path $RepoRoot 'patches\ps2recomp-dma-buffers.patch'
# GOW-Port: la vibración busca mandos Sony (HID) en un hilo aparte, no en el hilo del juego.
$padRumbleScanPatch = Join-Path $RepoRoot 'patches\ps2recomp-pad-rumble-scan.patch'
# GOW-Port: VBlank al campo NTSC real (59,94 Hz): los vídeos iban a 30,00 imágenes/s y el audio se quedaba atrás.
$vblankNtscPatch = Join-Path $RepoRoot 'patches\ps2recomp-vblank-ntsc.patch'
# GOW-Port: diagnóstico opcional de la traza por cuadro (PS2X_GS_WORKER_TRACE_SECS/_PCS, VBlank, IRQ del GS, hilos).
$gsWorkerTracePatch = Join-Path $RepoRoot 'patches\ps2recomp-gs-worker-trace.patch'
# GOW-Port: medidor de FPS en pantalla (F2 o GOW_FPS=1) y línea [gow-fps] por segundo en el registro.
$fpsOverlayPatch = Join-Path $RepoRoot 'patches\ps2recomp-fps-overlay.patch'
# GOW-Port: el medidor de FPS cuenta también las imágenes que llegan a la ventana.
$fpsImagenesPatch = Join-Path $RepoRoot 'patches\ps2recomp-fps-imagenes.patch'
# GOW-Port: la ventana muestra los cuadros en orden (no salta al más reciente si hay menos de 3 pendientes).
$presentOrdenPatch = Join-Path $RepoRoot 'patches\ps2recomp-present-orden.patch'
# GOW-Port: el trabajador pasa los paquetes GIF a la etapa GS en lotes (PS2X_GS_STAGE_BATCH).
$stageLotesPatch = Join-Path $RepoRoot 'patches\ps2recomp-gs-stage-lotes.patch'
# GOW-Port: pantalla completa sin bordes por defecto (F11) y depurador oculto al empezar (F1).
$pantallaPatch = Join-Path $RepoRoot 'patches\ps2recomp-pantalla-completa.patch'
# GOW-Port: los ciclos de los puntos de control del EE se pasan al IOP solo cuando vence su lote.
$iopAcumuladoPatch = Join-Path $RepoRoot 'patches\ps2recomp-iop-acumulado.patch'
# GOW-Port: 64 puntos de ritmo por campo (antes 16): menos adelanto del reloj del EE que dormir al terminar la GS.
$ritmoFinoPatch = Join-Path $RepoRoot 'patches\ps2recomp-ritmo-fino.patch'
# GOW-Port: 16 ciclos del EE por salto hacia atrás del código generado y 4 por despacho (antes 32 y 8).
$ciclosMitadPatch = Join-Path $RepoRoot 'patches\ps2recomp-ciclos-mitad.patch'
# GOW-Port: ranura de despacho de la VU1 nativa en una línea de caché (memo en BlockTiming, idleLast).
$vu1Ranura64Patch = Join-Path $RepoRoot 'patches\ps2recomp-vu1-ranura64.patch'
# GOW-Port: el redondeo de cada ejecución de la VU se cambia con el MXCSR en x86-64 (no fegetround/fesetround).
$vuRedondeoPatch = Join-Path $RepoRoot 'patches\ps2recomp-vu-redondeo.patch'
# GOW-Port: un MPG solo invalida los bloques de la VU1 nativa que leen el rango escrito.
$vu1InvalidacionRangoPatch = Join-Path $RepoRoot 'patches\ps2recomp-vu1-invalidacion-rango.patch'
# GOW-Port: el reloj del EE se pone al día con el del host si se queda atrás (cada 2048 puntos de control).
$eePonerseAlDiaPatch = Join-Path $RepoRoot 'patches\ps2recomp-ee-ponerse-al-dia.patch'
# GOW-Port: con GOW_PAD_TEST=1 se ignoran los mandos físicos (solo el guion de la prueba controla el juego).
$pruebaSinMandoPatch = Join-Path $RepoRoot 'patches\ps2recomp-prueba-sin-mando.patch'
# GOW-Port: la presentación usa los registros de pantalla capturados al empezar cada VBlank (sin parpadeos del menú en los vídeos).
$pantallaLatchPatch = Join-Path $RepoRoot 'patches\ps2recomp-pantalla-latch.patch'
# GOW-Port: sceCdStop (cdvdman:15) del IOP nativo: termina al momento (antes, sin implementar, colgaba el IOP).
$cdStopPatch = Join-Path $RepoRoot 'patches\ps2recomp-cd-stop.patch'
# GOW-Port: DelayThread fuera de un hilo deja correr el IOP durante el retraso (antes devolvía al momento y 989snd
# se colgaba al parar el sistema de sonido).
$iopDelayFueraPatch = Join-Path $RepoRoot 'patches\ps2recomp-iop-delay-fuera.patch'
# GOW-Port: la demultiplexación del PSS se detiene en el fin de programa (0x1B9), como libmpeg (sin esto el juego
# se quedaba al final de P04MP05.PSW pidiendo datos para siempre).
$mpegFinProgramaPatch = Join-Path $RepoRoot 'patches\ps2recomp-mpeg-fin-programa.patch'
# GOW-Port: los puntos de control del EE solo suman ciclos hasta el siguiente suceso (temporizador, lote del IOP,
# plazo, fin de porción); antes cada uno hacía toda la contabilidad (más de un tercio del hilo del EE).
$eeViaRapidaPatch = Join-Path $RepoRoot 'patches\ps2recomp-ee-via-rapida.patch'
# GOW-Port: la etapa GS cede el lock del GS entre paquetes cuando la presentación lo espera (la ventana llegaba a
# mostrar 2-8 imágenes por segundo con el juego a 60-120).
$gsPrioridadPatch = Join-Path $RepoRoot 'patches\ps2recomp-gs-prioridad-presentacion.patch'
# GOW-Port: las pasadas de niebla/DOF que leen el buffer Z como PSMCT24 lo leen en la GPU con el formato de Z (antes
# se decodificaba en la CPU una VRAM sin la profundidad: sin niebla y 1024x1024 téxeles por cuadro).
$gsZComoColorPatch = Join-Path $RepoRoot 'patches\ps2recomp-gs-z-como-color.patch'
# GOW-Port: agrupa triángulos consecutivos sin píxeles comunes en las pasadas de volúmenes de sombra, reduciendo
# llamadas y barreras de textura sin alterar el orden de los solapamientos.
$gsVolumenesAgrupadosPatch = Join-Path $RepoRoot 'patches\ps2recomp-gs-volumenes-agrupados.patch'
# GOW-Port: GOW_HZ=120: el VBlank del EE el doble de frecuente (la lógica del juego hasta 120 cuadros/s); el reloj de
# los vídeos sigue en campos NTSC y la presentación del host sube a 120.
$ee120hzPatch = Join-Path $RepoRoot 'patches\ps2recomp-ee-120hz.patch'
# GOW-Port: contexto OpenGL del GS en Linux con EGL (antes solo WGL: en Linux la ventana quedaba magenta).
$gsEglLinuxPatch = Join-Path $RepoRoot 'patches\ps2recomp-gs-egl-linux.patch'
# GOW-Port: GOW_VU1_CAPTURA también con la VU1 nativa activa (antes solo guardaba con GOW_VU1_NATIVE_OFF=1, a ~6 FPS).
$vu1CapturaNativaPatch = Join-Path $RepoRoot 'patches\ps2recomp-vu1-captura-nativa.patch'
# GOW-Port: VF0 y VI0 de VU0 son constantes (PR #79), no incluido en el runtime de Waffle565.
$vu0Vf0Patch = Join-Path $RepoRoot 'patches\ps2recomp-vu0-vf0.patch'
$vu0Vi0Patch = Join-Path $RepoRoot 'patches\ps2recomp-vu0-vi0.patch'
# GOW-Port: permite pasar tools\vu1\vu1n_heads.txt (entradas extra de la VU1 nativa) desde este repositorio.
$vu1nHeadsPatch = Join-Path $RepoRoot 'patches\ps2recomp-vu1n-heads.patch'
$overrides = Join-Path $RepoRoot 'src\gow_overrides.cpp'

$git = 'git'
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $git = Join-Path $env:ProgramFiles 'Git\cmd\git.exe' }

function Run([string]$exe, [string[]]$a) {
    Write-Host ">> $exe $($a -join ' ')"
    & $exe @a
    if ($LASTEXITCODE -ne 0) { throw "Fallo ($LASTEXITCODE): $exe" }
}
function Paso([string]$t) { Write-Host ''; Write-Host "==== [$(Get-Date -Format HH:mm:ss)] $t ====" }

Paso 'Herramientas'
Write-Host "ELF:     $elf"
Write-Host "Trabajo: $work"
Run $git @('--version'); Run 'cmake' @('--version'); Run 'ninja' @('--version')

if (-not (Test-Path -LiteralPath (Join-Path $rec '.git'))) {
    Paso 'Clonando PS2Recomp'
    Run $git @('clone', $PS2RecompUrl, $rec)
}
Push-Location $rec
Run $git @('fetch', 'origin', $PS2RecompCommit)
Run $git @('checkout', '-f', $PS2RecompCommit)
Run $git @('submodule', 'update', '--init', '--recursive')
# checkout -f no retira los archivos nuevos que crearon los parches en una compilacion anterior, y git apply
# fallaria al volver a crearlos. Se borran todos los que crea algun parche de patches\ ("new file mode").
foreach ($patchFile in Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'patches') -Filter '*.patch' -File) {
    $created = $null
    foreach ($line in Get-Content -LiteralPath $patchFile.FullName) {
        if ($line -match '^diff --git a/(\S+) b/') { $created = $Matches[1] }
        elseif ($created -and $line -like 'new file mode*') {
            $extra = Join-Path $rec ($created -replace '/', '\')
            if (Test-Path -LiteralPath $extra) { Remove-Item -LiteralPath $extra -Force }
        }
    }
}
Run $git @('apply', '--ignore-whitespace', '--verbose', $waffleRuntimePatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $waffleTestsPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vu0Vf0Patch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vu0Vi0Patch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vu1nHeadsPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $waffleGccPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vu1nLeakPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vif1UnpackRapidoPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $dmaBuffersPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $padRumbleScanPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vblankNtscPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $gsWorkerTracePatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $fpsOverlayPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $fpsImagenesPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $presentOrdenPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $stageLotesPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $pantallaPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $iopAcumuladoPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $ritmoFinoPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $ciclosMitadPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vu1Ranura64Patch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vuRedondeoPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vu1InvalidacionRangoPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $eePonerseAlDiaPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $pruebaSinMandoPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $pantallaLatchPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $cdStopPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $iopDelayFueraPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $mpegFinProgramaPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $eeViaRapidaPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $gsPrioridadPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $gsEglLinuxPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $vu1CapturaNativaPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $gsZComoColorPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $gsVolumenesAgrupadosPatch)
Run $git @('apply', '--ignore-whitespace', '--verbose', $ee120hzPatch)
Pop-Location

# El codigo generado incluye <ps2_recompiled_functions.h> desde src/runner
$cm = Join-Path $rec 'ps2xRuntime\CMakeLists.txt'
if (-not (Select-String -LiteralPath $cm -Pattern 'GOW-Port include' -Quiet)) {
    Add-Content -LiteralPath $cm "`n# GOW-Port include`ntarget_include_directories(ps2EntryRunner PRIVATE `${CMAKE_CURRENT_SOURCE_DIR}/src/runner)"
}

# Optimizado en paralelo: /O2 en cada archivo, sin optimizacion global del enlazador (/GL, /LTCG)
$rm = Join-Path $rec 'ps2xRuntime\cmake\ReleaseMode.cmake'
$t = Get-Content -LiteralPath $rm -Raw
$t2 = $t -replace '(?m)^\s*/GL\b.*\r?\n', '' -replace '(?m)^\s*/LTCG\b.*\r?\n', '' `
         -replace 'set_property\(TARGET \$\{TargetName\} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE\)', '# IPO desactivado (GOW-Port)'
if ($t2 -ne $t) { Set-Content -LiteralPath $rm -Value $t2 -NoNewline; Write-Host 'ReleaseMode.cmake: LTCG desactivado' }

# GOW-Port: IOP nativo (tools\iop\iopn_gen.py, Waffle565). GOW_IOP_MODULOS es la carpeta que deja una ejecucion
# con PS2X_IOP_DUMP_MODULES=<carpeta>. Sin ella se enlaza el registro vacio y los IRX corren en el interprete.
if ($env:GOW_IOP_MODULOS -and (Test-Path -LiteralPath (Join-Path $env:GOW_IOP_MODULOS 'modules.txt'))) {
    Paso 'Generando el IOP nativo'
    Run 'python' @('-I', (Join-Path $RepoRoot 'tools\iop\iopn_gen.py'), $env:GOW_IOP_MODULOS, (Join-Path $rec 'ps2xIOP\src\native'))
}

Paso 'Configurando CMake'
# Las trazas por función vacían el archivo en cada entrada/salida. Activarlas solo para investigar.
$logsEnabled = if ($Trazas) { 'ON' } else { 'OFF' }
Run 'cmake' @('-S', $rec, '-B', $bld, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF',
              "-DPS2X_ENABLE_RUNTIME_LOGS=$logsEnabled", "-DPS2X_ENABLE_AGRESSIVE_LOGS=$logsEnabled", "-DPS2X_ENABLE_IOP_RPC_TRACE=$logsEnabled",
              '-DPS2X_ENABLE_RUNNER_UNITY_BUILD=ON', '-DPS2X_ENABLE_RUNNER_PCH=ON')
# VU1 compilada (opcional): GOW_VU1_MICROCODIGO es una carpeta local con micromemorias capturadas con
# GOW_VU1_CAPTURA. Sin ella (o vacía) el juego usa el intérprete. Ver docs\RENDIMIENTO.md.
$vu1Micro = if ($env:GOW_VU1_MICROCODIGO) { (Resolve-Path -LiteralPath $env:GOW_VU1_MICROCODIGO).Path -replace '\\', '/' } else { '' }
$vu1Gen = if ($vu1Micro) { (Join-Path $RepoRoot 'tools\vu1\generar_vu1.cpp') -replace '\\', '/' } else { '' }
# GOW-Port: generador de la VU1 nativa (adaptado de Waffle565/God-Of-War-Native-Port); se activa con GOW_VU1_NATIVE=1.
$vu1nGen = if ($vu1Micro) { (Join-Path $RepoRoot 'tools\vu1\vu1n_gen.py') -replace '\\', '/' } else { '' }
$vu1nHeads = if ($vu1Micro) { (Join-Path $RepoRoot 'tools\vu1\vu1n_heads.txt') -replace '\\', '/' } else { '' }
Run 'cmake' @('-S', $rec, '-B', $bld, "-DPS2X_VU1_MICROCODE_DIR=$vu1Micro", "-DPS2X_VU1_GENERATOR_SOURCE=$vu1Gen", "-DPS2X_VU1N_GEN_OVERRIDE=$vu1nGen", "-DPS2X_VU1N_HEADS_FILE=$vu1nHeads")

Paso 'Compilando el recompilador'
Run 'cmake' @('--build', $bld, '--target', 'ps2_recomp')

Paso 'Generando C++ desde SCUS_973.99'
if (Test-Path $gen) { Remove-Item $gen -Recurse -Force }
New-Item -ItemType Directory $gen | Out-Null
$fw = { param($p) $p -replace '\\', '/' }
# ps2_recomp no abre rutas con caracteres no ASCII (p. ej. acentos en la carpeta del repositorio):
# copiamos el ELF y el mapa de funciones a la carpeta de trabajo, que tiene una ruta corta.
$elfWork = Join-Path $work 'SCUS_973.99'
$mapWork = Join-Path $work 'funcmap.csv'
Copy-Item -LiteralPath $elf -Destination $elfWork -Force
Copy-Item -LiteralPath $funcmap -Destination $mapWork -Force
(Get-Content -LiteralPath $template -Raw) `
    -replace '@ELF@', (& $fw $elfWork) `
    -replace '@MAP@', (& $fw $mapWork) `
    -replace '@OUT@', ((& $fw $gen) + '/') |
    Set-Content -LiteralPath (Join-Path $work 'config.toml') -Encoding ASCII
$recompExe = Get-ChildItem $bld -Recurse -Filter 'ps2_recomp.exe' | Select-Object -First 1
Run $recompExe.FullName @((Join-Path $work 'config.toml')) | Select-Object -Last 5
Write-Host ("Archivos generados: {0}" -f (Get-ChildItem $gen).Count)

Paso 'Copiando codigo generado al runtime'
$runner = Join-Path $rec 'ps2xRuntime\src\runner'
Get-ChildItem $runner -File | Remove-Item -Force
Get-ChildItem -LiteralPath $gen -File | Copy-Item -Destination $runner
Copy-Item -LiteralPath $overrides -Destination (Join-Path $runner 'gow_overrides.cpp')
Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'src') -Filter '*.h' -File | Copy-Item -Destination $runner
# Con MSVC en espanol ninja no registra las dependencias /showIncludes: los .cpp incluidos desde los
# archivos unity (codigo generado, register_functions.cpp, overrides) no fuerzan recompilacion. Borramos
# los objetos unity para que se recompilen siempre con el codigo recien generado.
# GOW-Port: un cambio de headers tambien altera el ABI del runtime y de los tests.
# Sin dependencias registradas, conservar esos objetos o el PCH puede mezclar
# interfaces anteriores con el runner nuevo (C3668 o una violacion de acceso).
foreach ($objectRelative in @('ps2xRuntime\CMakeFiles\ps2_runtime.dir',
                             'ps2xTest\CMakeFiles\ps2_test_lib.dir',
                             'ps2xTest\CMakeFiles\ps2_test_function_table.dir')) {
    $objectDir = Join-Path $bld $objectRelative
    if (Test-Path -LiteralPath $objectDir) {
        Get-ChildItem -LiteralPath $objectDir -Recurse -Filter '*.obj' -File | Remove-Item -Force
    }
}
$runnerObjectDir = Join-Path $bld 'ps2xRuntime\CMakeFiles\ps2EntryRunner.dir'
if (Test-Path -LiteralPath $runnerObjectDir) {
    Get-ChildItem -LiteralPath $runnerObjectDir -Filter '*.pch' -File | Remove-Item -Force
    Get-ChildItem -LiteralPath $runnerObjectDir -Filter 'cmake_pch*.obj' -File | Remove-Item -Force
}
$unityDir = Join-Path $bld 'ps2xRuntime\CMakeFiles\ps2EntryRunner.dir\Unity'
if (Test-Path -LiteralPath $unityDir) { Get-ChildItem -LiteralPath $unityDir -Filter '*.obj' | Remove-Item -Force }
# GOW-Port: la VU1 compilada (vu1_generado\programa*.cpp) no usa el PCH. Si el generador produce el mismo
# texto, ninja no la recompila aunque cambien ps2_vu1.h o ps2_vu1_compiled.inl, y sus bloques acceden a
# VU1Interpreter con la disposición anterior (bucles de espera infinitos al arrancar).
$vu1ObjectDir = Join-Path $bld 'ps2xRuntime\CMakeFiles\ps2EntryRunner.dir\vu1_generado'
if (Test-Path -LiteralPath $vu1ObjectDir) { Get-ChildItem -LiteralPath $vu1ObjectDir -Filter '*.obj' | Remove-Item -Force }
Run 'cmake' @('-S', $rec, '-B', $bld)

Paso 'Compilando el juego optimizado en paralelo'
Run 'cmake' @('--build', $bld, '--target', 'ps2EntryRunner')

$exe = Get-ChildItem $bld -Recurse -Filter 'ps2EntryRunner.exe' | Select-Object -First 1
Paso "OK: $($exe.FullName)"
