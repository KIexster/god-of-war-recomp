# Perfil reproducible: cuenta vid::Flip y presentaciones, sin volcados ni capturas del juego.
param(
    [int]$Segundos = 150,
    [switch]$SoloCuadros,
    [ValidateSet('cpu', 'cpu-hilo', 'opengl')][string]$Renderer = 'cpu',
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Etiqueta = 'limpio'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$clear = @('GOW_VIF_DIAG', 'GOW_RENDER_DIAG', 'GOW_EE_PRIM_DIAG', 'GOW_MODEL_DIAG', 'GOW_PATH_DIAG', 'GOW_ANM_DIAG', 'GOW_CLIP_TRACE',
           'GOW_GEOM_DIAG', 'GOW_GEOMETRY_DIAG', 'GOW_SIO2_DIAG', 'GOW_GS_GPU_TEST',
           'GOW_REPLAY_TRACE', 'GOW_REPLAY_STEM', 'PS2X_GS_DIAG', 'PS2X_GS_TRACE_TBP', 'PS2X_GS_TRACE_AFTER',
           'PS2X_GS_DUMP_SECONDS', 'PS2X_IOP_PC_EVERY', 'PS2X_IOP_TRACE', 'PS2X_IOP_TRACE_EVERY',
           'PS2X_IOP_TRACE_NOCLIB', 'PS2X_IOP_TRACE_FROM', 'PS2X_IOP_TRACE_DMA')
$set = @{
    GOW_PERF_DIAG = $(if ($SoloCuadros) { 'frames' } else { '1' })
    GOW_PERF_FRAME_PC = '0x001837B8' # vid::Flip de SCUS-97399; sus reanudaciones no cuentan.
    GOW_PAD_TEST = '1'
    GOW_PAD_TEST_NO_CAPTURE = '1'
    GOW_SKIP_FMV = '1'
    GOW_FAST_BOOT = '1'
    PS2X_GS_GPU = $(if ($Renderer -eq 'opengl') { '1' } else { '0' })
    PS2X_GS_THREAD = $(if ($Renderer -eq 'cpu-hilo') { '1' } else { '0' })
}
$previous = @{}
foreach ($name in @($clear) + @($set.Keys)) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    # En PowerShell 7.5/.NET 9, pasar $null a SetEnvironmentVariable deja un valor vacío.
    # getenv() aún lo detecta: eliminar la entrada evita activar diagnósticos por presencia.
    foreach ($name in $clear) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
    foreach ($name in $set.Keys) { [Environment]::SetEnvironmentVariable($name, $set[$name], 'Process') }
    Write-Host "Renderer: $Renderer"
    & (Join-Path $PSScriptRoot 'ejecutar.ps1') -Segundos $Segundos
    $destination = Join-Path $LogsDir "perf_$Etiqueta.log"
    Copy-Item -LiteralPath (Join-Path $LogsDir 'ejecutar_err.log') -Destination $destination -Force
    Write-Host "Perfil guardado en $destination"
}
finally {
    foreach ($name in $previous.Keys) {
        if ($null -eq $previous[$name]) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
        else { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
    }
}
