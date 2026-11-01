# Integration test: known uniforms in, pixels out, on the real renderer. Needs a Vulkan
# RPCS3 window, which tools/run_rpcs3.ps1 hides as soon as it appears.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$install = "$env:RPCS3_DIR\dev_hdd0\game\BLTR00001\USRDIR"
$sceneFile = Join-Path $install 'test_scene.lua'
Copy-Item (Join-Path $PSScriptRoot 'scene.lua') $sceneFile -Force

# The scene's half-transparent image is not part of the game, so it is made here each time.
$makeImage = "import os, sys; sys.path.insert(0, r'$root\tools'); import texture_cook as c; from PIL import Image; os.makedirs(r'$install\cooked\tests', exist_ok=True); open(r'$install\cooked\tests\half_white.rtex', 'wb').write(c.cook_from_image(Image.new('RGBA', (4, 4), (255, 255, 255, 128))).to_bytes())"
python -B -c $makeImage

$run = & pwsh -NoProfile -Command "& '$root\tools\run_rpcs3.ps1' -Project love-ps3 -Expect 'probe: done' -TimeoutSeconds 90 -Frames 30"
Remove-Item $sceneFile -ErrorAction SilentlyContinue
$run | Select-Object -First 12

$runDir = (($run | Where-Object { $_ -like 'run: *' }) -replace '^run: ', '') -replace ' exited=.*$', ''
python -B (Join-Path $PSScriptRoot 'check.py') (Join-Path $runDir 'frame_30.ppm')
exit $LASTEXITCODE
