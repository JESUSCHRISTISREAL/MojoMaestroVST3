# Renders the Blender pictures into art\out at 4x.  Usage:  .\run_all.ps1 [-Blender "C:\path\to\blender.exe"]
param([string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
Set-Location $PSScriptRoot
$log = Join-Path $PSScriptRoot 'log_all.txt'
if (Test-Path $log) { Remove-Item $log }
foreach ($m in 'plate', 'ptr_big', 'ptr_small') {
    & $Blender -b -P scene.py -- $PSScriptRoot $m 256 *>> $log
    "EXIT $m = $LASTEXITCODE" | Out-File -Append $log
}
& $Blender -b -P scene_cap.py -- $PSScriptRoot cap 256 *>> $log
"EXIT cap = $LASTEXITCODE" | Out-File -Append $log
'ALLDONE' | Out-File -Append $log
