# Barrido de sensibilidad: ejecuta escenarios variando un parametro del modelo.
# Uso: .\sensibilidad.ps1 [-Parametro v-pulmon|p-street|p-pre] [-Valores 4,6,8,12,20] [-Escenarios normal,base_baja]
param(
  [string]$Parametro = "v-pulmon",
  [double[]]$Valores = @(4, 6, 8, 12, 20),
  [string[]]$Escenarios = @("normal", "base_baja", "sin_flujo")
)
$exe = "$PSScriptRoot\build\sim.exe"
foreach ($e in $Escenarios) {
  Write-Host "== $e (variando --$Parametro) =="
  foreach ($v in $Valores) {
    $out = & $exe $e --quiet "--$Parametro" $v
    $res = ($out | Select-String '^RESULTADO').Line -replace "RESULTADO $e`: ", ""
    $fallas = ($out | Select-String '\[FALLA\]' | ForEach-Object { $_.Line.Trim() -replace '^\[FALLA\] ', '' }) -join " | "
    "{0,8} -> {1}  {2}" -f $v, $res, $fallas
  }
}
