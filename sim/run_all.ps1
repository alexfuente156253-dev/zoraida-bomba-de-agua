# Ejecuta todos los escenarios y muestra un resumen (PASA/FALLA). Parametros extra se pasan a sim.exe.
$exe = "$PSScriptRoot\build\sim.exe"
$nombres = (& $exe | Select-String '^\s{2}(\S+)\s' | ForEach-Object { $_.Matches[0].Groups[1].Value }) | Where-Object { $_ -ne 'todos' }
$fallos = 0
foreach ($n in $nombres) {
  $out = & $exe $n --quiet @args
  $out | Where-Object { $_ -match '^\s+\[(FALLA|INFO)' -or $_ -match '^RESULTADO' } | ForEach-Object { "  $_" }
  if ($LASTEXITCODE -ne 0) { $fallos++ }
}
Write-Host ""
Write-Host "Escenarios con fallas: $fallos de $($nombres.Count)"
exit $fallos
