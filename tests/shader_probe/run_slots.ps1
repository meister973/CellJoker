# Integration test: all six interpolator slots carry their values to the fragment program.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$install = "$env:RPCS3_DIR\dev_hdd0\game\BLTR00001\USRDIR"
$sceneFile = Join-Path $install 'test_scene.lua'
Copy-Item (Join-Path $PSScriptRoot 'slots_scene.lua') $sceneFile -Force

$run = & pwsh -NoProfile -Command "& '$root\tools\run_rpcs3.ps1' -Project love-ps3 -Expect 'probe: done' -TimeoutSeconds 90 -Frames 30"
Remove-Item $sceneFile -ErrorAction SilentlyContinue

$runDir = (($run | Where-Object { $_ -like 'run: *' }) -replace '^run: ', '') -replace ' exited=.*$', ''
python -B (Join-Path $PSScriptRoot 'check_slots.py') (Join-Path $runDir 'frame_30.ppm')
exit $LASTEXITCODE
