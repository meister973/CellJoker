# Copies the runtime and the game files it needs into RPCS3's virtual HDD.
# Game files never enter the repository.
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

Copy-Tree 'localization'
Copy-Tree 'resources\fonts'
Copy-Item (Join-Path $game 'resources\gamecontrollerdb.txt') (Join-Path $target 'resources\gamecontrollerdb.txt') -Force

Copy-Item (Join-Path $root 'runtime\lua\*.lua') $target -Force
Copy-Item (Join-Path $root 'love-ps3\ICON0.PNG') (Split-Path -Parent $target) -Force
robocopy (Join-Path $root 'love-ps3\generated\shaders') (Join-Path $target 'shaders') /E /NFL /NDL /NJH /NJS /NP | Out-Null
$cooked = Join-Path $root 'cooked'
if (Test-Path $cooked) {
    robocopy $cooked (Join-Path $target 'cooked') /E /NFL /NDL /NJH /NJS /NP | Out-Null
}
Write-Output "synced to $target"
exit 0
