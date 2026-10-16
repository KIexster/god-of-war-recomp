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
