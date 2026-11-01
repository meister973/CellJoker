# Runs the game with no window, with one of this folder's scripts in control of it.
# Usage: run_driver.ps1 -Driver play_driver.lua
# Each run starts with no saves, unless -KeepSave says this one follows an earlier one.
param([Parameter(Mandatory)][string]$Driver, [int]$TimeoutSeconds = 280, [switch]$KeepSave, [switch]$DumpAudio)

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$install = "$env:RPCS3_DIR\dev_hdd0\game\BLTR00001\USRDIR"
$installed = Join-Path $install 'play_driver.lua'
Copy-Item (Join-Path $PSScriptRoot $Driver) $installed -Force
if (-not $KeepSave) { Remove-Item (Join-Path $install 'save') -Recurse -Force -ErrorAction SilentlyContinue }

$run = & pwsh -NoProfile -File (Join-Path $root 'tools\run_rpcs3.ps1') -Project love-ps3 -Expect 'driver finished' -TimeoutSeconds $TimeoutSeconds -DumpAudio:$DumpAudio
$status = $LASTEXITCODE
Remove-Item $installed -ErrorAction SilentlyContinue

$run | Where-Object { $_ -like 'play: *' -or $_ -like '*boot ended*' -or $_ -like '*.lua:*' -or $_ -like 'rpcs3 is already*' }
Write-Output "$Driver exit=$status"
exit $status
