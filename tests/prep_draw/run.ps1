# Integration test: the C prepDraw builds the same transform as the game's prep_draw.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$install = "$env:RPCS3_DIR\dev_hdd0\game\BLTR00001\USRDIR"
$sceneFile = Join-Path $install 'test_scene.lua'
Copy-Item (Join-Path $PSScriptRoot 'scene.lua') $sceneFile -Force

$run = & pwsh -NoProfile -Command "& '$root\tools\run_rpcs3.ps1' -Project love-ps3 -Expect 'probe: done' -TimeoutSeconds 90"
Remove-Item $sceneFile -ErrorAction SilentlyContinue

$result = $run | Where-Object { $_ -like 'prep_draw: *' }
$result | Write-Output
if ($result -match 'cases=\d+ mismatches=0 unbalanced=0 errors_match=true') { exit 0 }
exit 1
