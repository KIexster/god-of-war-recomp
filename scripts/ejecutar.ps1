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

$out = Join-Path $LogsDir 'ejecutar.log'
$err = Join-Path $LogsDir 'ejecutar_err.log'
$p = Start-Process -FilePath $exe.FullName -ArgumentList ('"' + $elf + '"') -WorkingDirectory $exe.DirectoryName `
        -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
if ($Segundos -le 0) { $p.WaitForExit(); Write-Host "Terminado (codigo $($p.ExitCode))" }
elseif (-not $p.WaitForExit($Segundos * 1000)) { Stop-Process -Id $p.Id -Force; Write-Host "Detenido a los $Segundos s" }
else { Write-Host "El programa termino solo (codigo $($p.ExitCode))" }
