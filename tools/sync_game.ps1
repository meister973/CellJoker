# Copies the runtime and the game files it needs into RPCS3's virtual HDD.
# Game files never enter the repository. Only the small fonts and the languages that use them
# go across: game.lua:980 checks each font exists, and the other fonts total 63 MB.
$root = Split-Path -Parent $PSScriptRoot
$target = "$env:RPCS3_DIR\dev_hdd0\game\BLTR00001\USRDIR"
$game = Join-Path $root 'game'

New-Item -ItemType Directory -Force $target | Out-Null

function Copy-Tree([string]$relative) {
    $destination = Join-Path $target $relative
    New-Item -ItemType Directory -Force $destination | Out-Null
    robocopy (Join-Path $game $relative) $destination /E /NFL /NDL /NJH /NJS /NP | Out-Null
}

robocopy $game $target *.lua version.jkr /NFL /NDL /NJH /NJS /NP | Out-Null
Copy-Tree 'engine'
Copy-Tree 'functions'
Copy-Tree 'resources\shaders'
Copy-Tree 'resources\sounds'
Copy-Tree 'resources\textures'

New-Item -ItemType Directory -Force (Join-Path $target 'localization'), (Join-Path $target 'resources\fonts') | Out-Null
Copy-Item (Join-Path $game 'localization\*.lua') (Join-Path $target 'localization') -Exclude 'ja.lua', 'ko.lua', 'zh_CN.lua', 'zh_TW.lua' -Force
Copy-Item (Join-Path $game 'resources\fonts\m6x11plus.ttf') (Join-Path $target 'resources\fonts\m6x11plus.ttf') -Force
Copy-Item (Join-Path $game 'resources\fonts\NotoSans-Bold.ttf') (Join-Path $target 'resources\fonts\NotoSans-Bold.ttf') -Force
Copy-Item (Join-Path $game 'resources\gamecontrollerdb.txt') (Join-Path $target 'resources\gamecontrollerdb.txt') -Force

Copy-Item (Join-Path $root 'runtime\lua\*.lua') $target -Force
robocopy (Join-Path $root 'love-ps3\generated\shaders') (Join-Path $target 'shaders') /E /NFL /NDL /NJH /NJS /NP | Out-Null
$cooked = Join-Path $root 'cooked'
if (Test-Path $cooked) {
    robocopy $cooked (Join-Path $target 'cooked') /E /NFL /NDL /NJH /NJS /NP | Out-Null
}
Write-Output "synced to $target"
exit 0
