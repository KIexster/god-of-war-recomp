# Ejecuta el port y guarda la salida en logs\.
#   -Segundos N : lo cierra pasados N segundos (0 = sin limite). Por defecto 60.
param([int]$Segundos = 60, [switch]$VigilarRendimiento)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$work = Get-GowWorkDir
$bld  = Join-Path $work 'PS2Recomp\out\build'
$exe  = $null
if (Test-Path -LiteralPath $bld) {
    $exe = Get-ChildItem -LiteralPath $bld -Recurse -Filter 'ps2EntryRunner.exe' |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
if (-not $exe) { throw 'No existe ps2EntryRunner.exe. Corre antes scripts\2_compilar.cmd.' }
$elf = Get-GowElf
Write-Host "Ejecutable: $($exe.FullName)"
Write-Host "ELF:        $elf"

$elfDir = Split-Path -Parent $elf

# GOW-Port: como en el port de Waffle565, el juego lee sus datos de los archivos del disco extraidos junto al ELF,
# cada uno en su sector original (.disc_layout de tools\iso_extract.py --layout; ver configureGowCdImage en
# src\gow_overrides.cpp). La primera vez se extraen de "..\God of War.iso". Con GOW_ISO se lee esa imagen.
if (-not $env:GOW_ISO -and -not (Test-Path -LiteralPath (Join-Path $elfDir '.disc_layout'))) {
    $iso = Join-Path (Split-Path -Parent $elfDir) 'God of War.iso'
    if (Test-Path -LiteralPath $iso) {
        Write-Host "Extrayendo el disco en $elfDir (solo la primera vez)..."
        & python -I (Join-Path $RepoRoot 'tools\iso_extract.py') $iso $elfDir --layout
        if ($LASTEXITCODE -ne 0) { throw 'Fallo la extraccion del disco (tools\iso_extract.py)' }
    }
    else {
        Write-Warning "No hay .disc_layout junto al ELF ni ISO en $iso. Define GOW_ISO o extrae el disco con tools\iso_extract.py --layout"
    }
}

# El juego carga sus modulos del IOP como "IOP_MOD/xxx.irx" relativo a la carpeta del ELF, pero en el
# disco estan en la raiz. El emulador del IOP ejecuta los IRX originales (989snd, smpd...), asi que los
# copiamos a IOP_MOD\ la primera vez.
$iopMod = Join-Path $elfDir 'IOP_MOD'
$irx = @(Get-ChildItem -LiteralPath $elfDir -Filter '*.IRX' -File)
if ($irx.Count -gt 0 -and -not (Test-Path -LiteralPath (Join-Path $iopMod 'SMPD_IOP.IRX'))) {
    New-Item -ItemType Directory -Force -Path $iopMod | Out-Null
    $irx | Copy-Item -Destination $iopMod
    Write-Host "Copiados $($irx.Count) modulos IRX a $iopMod"
}
elseif ($irx.Count -eq 0 -and -not (Test-Path -LiteralPath $iopMod)) {
    Write-Warning "No hay modulos .IRX junto al ELF: copia los IRX del disco a $iopMod"
}
# GOW-Port: valores de juego de play.cmd (Waffle565). Se respetan los que ya esten definidos en el entorno.
foreach ($par in @(@('PS2X_GS_WORKER', '1'), @('GOW_GS_FINISH_ASINCRONO', '1'), @('GOW_AUDIO_DEVICE', 'default'))) {
    if (-not [Environment]::GetEnvironmentVariable($par[0])) { Set-Item -Path ('env:' + $par[0]) -Value $par[1] }
}

$out = Join-Path $LogsDir 'ejecutar.log'
$err = Join-Path $LogsDir 'ejecutar_err.log'
if ($env:GOW_PAD_TEST -eq '1' -and $env:GOW_PAD_TEST_NO_CAPTURE -ne '1') {
    # No mezclar capturas antiguas con una prueba que se detenga antes de producirlas todas.
    Get-ChildItem -LiteralPath $exe.DirectoryName -Filter 'gow_pad_test_*.ppm' -File |
        Remove-Item -Force
}
$launch = @{
    FilePath = $exe.FullName; ArgumentList = ('"' + $elf + '"'); WorkingDirectory = $exe.DirectoryName
    RedirectStandardOutput = $out; RedirectStandardError = $err; PassThru = $true
}
if ($VigilarRendimiento) { $launch.WindowStyle = 'Hidden' }
$p = Start-Process @launch
if ($VigilarRendimiento) {
    # GOW-Port: vigilar toda la pasada, excluyendo solo el PID que acabamos de lanzar.
    # No detener compiladores ni otras partidas: cerrar unicamente nuestra instancia.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $invalid = $null
    try {
        while ($true) {
            $busy = @(Get-Process -Name cl,link,clang,clang-cl,cc1plus,ninja,cmake,ps2EntryRunner -ErrorAction SilentlyContinue |
                      Where-Object { $_.Id -ne $p.Id })
            if ($busy.Count -gt 0) {
                throw ('Perfil invalidado por carga externa: ' +
                       (($busy.ProcessName | Sort-Object -Unique) -join ', ') + '.')
            }
            if ($Segundos -gt 0 -and $watch.Elapsed.TotalSeconds -ge $Segundos) { break }
            $waitMs = 1000
            if ($Segundos -gt 0) {
                $waitMs = [Math]::Max(1, [Math]::Min(1000, [int](($Segundos - $watch.Elapsed.TotalSeconds) * 1000)))
            }
            if ($p.WaitForExit($waitMs)) { break }
        }
    }
    catch { $invalid = $_ }
    finally {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
        $p.WaitForExit() # Vaciar las redirecciones antes de marcar o copiar el registro.
    }
    if ($null -ne $invalid) {
        Add-Content -LiteralPath $err -Value ('[gow-perf:invalid] ' + $invalid.Exception.Message) -Encoding utf8
        throw $invalid
    }
    Write-Host ('Perfil vigilado terminado a los {0:N2} s' -f $watch.Elapsed.TotalSeconds)
}
elseif ($Segundos -le 0) { $p.WaitForExit(); Write-Host "Terminado (codigo $($p.ExitCode))" }
elseif (-not $p.WaitForExit($Segundos * 1000)) { Stop-Process -Id $p.Id -Force; Write-Host "Detenido a los $Segundos s" }
else { Write-Host "El programa termino solo (codigo $($p.ExitCode))" }
