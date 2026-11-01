# Integration test: plays the real game with no window, then measures the sound that came out.
# The cards driver is the default because it outlasts the music tracks, so the music must restart.
param([string]$Driver = 'content_cards_driver.lua', [int]$TimeoutSeconds = 500)

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rpcs3Dir = "$env:RPCS3_DIR"

$started = Get-Date
pwsh -NoProfile -File (Join-Path $root 'tests\gameplay\run_driver.ps1') -Driver $Driver -TimeoutSeconds $TimeoutSeconds -DumpAudio |
    Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { Write-Output 'the game did not finish its run'; exit 1 }

$tty = Get-ChildItem (Join-Path $root 'runs') -Directory -Filter '*love-ps3' | Sort-Object Name | Select-Object -Last 1
Select-String -Path (Join-Path $tty.FullName 'TTY.log') -Pattern '^frame=' | Select-Object -Last 1 | ForEach-Object { $_.Line }

$wav = Get-ChildItem $rpcs3Dir -Recurse -Filter '*.wav' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $started } | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $wav) { Write-Output 'no dumped WAV found'; exit 1 }

python -B (Join-Path $PSScriptRoot 'check_game.py') $wav.FullName
$checked = $LASTEXITCODE
if ($checked -eq 0) { Remove-Item $wav.FullName -ErrorAction SilentlyContinue }
exit $checked
