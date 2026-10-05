# Ejecuta el port y guarda la salida en logs\.
#   -Segundos N : lo cierra pasados N segundos (0 = sin limite). Por defecto 60.
param([int]$Segundos = 60)
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

# El juego carga sus modulos del IOP como "IOP_MOD/xxx.irx" relativo a la carpeta del ELF, pero en el
# disco estan en la raiz. El emulador del IOP ejecuta los IRX originales (989snd, smpd...), asi que los
# copiamos a IOP_MOD\ la primera vez.
$elfDir = Split-Path -Parent $elf
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
# La imagen de disco se busca en GOW_ISO o como "..\God of War.iso" respecto a la carpeta del ELF
# (ver configureGowCdImage en src\gow_overrides.cpp). Sin ella, el IOP usa una ISO virtual.

$out = Join-Path $LogsDir 'ejecutar.log'
$err = Join-Path $LogsDir 'ejecutar_err.log'
$p = Start-Process -FilePath $exe.FullName -ArgumentList ('"' + $elf + '"') -WorkingDirectory $exe.DirectoryName `
        -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
if ($Segundos -le 0) { $p.WaitForExit(); Write-Host "Terminado (codigo $($p.ExitCode))" }
elseif (-not $p.WaitForExit($Segundos * 1000)) { Stop-Process -Id $p.Id -Force; Write-Host "Detenido a los $Segundos s" }
else { Write-Host "El programa termino solo (codigo $($p.ExitCode))" }
