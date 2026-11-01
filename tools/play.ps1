# Starts the game in RPCS3 with an Xbox pad as Player 1, for playing it by hand.
# The profile applies to this launch only: RPCS3's own input settings stay as they are.
$root = Split-Path -Parent $PSScriptRoot
$rpcs3Dir = "$env:RPCS3_DIR"
$elf = Join-Path $root 'love-ps3\build\love-ps3.elf'

if (-not (Test-Path $elf)) { Write-Output "missing ELF, run tools/build_love.ps1 first: $elf"; exit 2 }
if (Get-Process rpcs3 -ErrorAction SilentlyContinue) { Write-Output 'rpcs3 is already running'; exit 3 }

$profiles = Join-Path $rpcs3Dir 'config\input_configs\global'
New-Item -ItemType Directory -Force $profiles | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'rpcs3_xinput_input.yml') (Join-Path $profiles 'balatro_xinput.yml') -Force

Start-Process -FilePath (Join-Path $rpcs3Dir 'rpcs3.exe') -ArgumentList '--no-gui', '--input-config', 'balatro_xinput', "`"$elf`""
