# Builds love-ps3 from clean, cooks the textures if needed, and syncs to RPCS3's HDD.
$root = Split-Path -Parent $PSScriptRoot
$image = 'balatro-ps3dev:2'

docker run --rm -v "${root}:/work" -w /work/love-ps3 $image sh -c 'rm -rf build generated; python3 -B ../tools/shader_build.py generated/shaders ../game/resources/shaders ../tests/shader_probe && make 2>&1 | grep -E " error |Error " ; test -f build/love-ps3.elf'
if ($LASTEXITCODE -ne 0) { Write-Output 'build failed'; exit 1 }

if (-not (Test-Path (Join-Path $root 'cooked'))) {
    python -B (Join-Path $root 'tools\texture_cook.py') (Join-Path $root 'game\resources\textures') (Join-Path $root 'cooked\resources\textures')
}

& (Join-Path $PSScriptRoot 'sync_game.ps1')
Write-Output 'build ok'
exit 0
