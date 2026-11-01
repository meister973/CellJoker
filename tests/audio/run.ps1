# Integration test: known tones in, measured sound out of RPCS3, with no window.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rpcs3Dir = "$env:RPCS3_DIR"
$install = Join-Path $rpcs3Dir 'dev_hdd0\game\BLTR00001\USRDIR'
$sceneFile = Join-Path $install 'test_scene.lua'
$sounds = Join-Path $install 'resources\sounds'

python -B (Join-Path $PSScriptRoot 'make_tones.py') $sounds
if ($LASTEXITCODE -ne 0) { Write-Output 'could not make the test tones'; exit 1 }
Copy-Item (Join-Path $PSScriptRoot 'scene.lua') $sceneFile -Force

$started = Get-Date
$run = & pwsh -NoProfile -File (Join-Path $root 'tools\run_rpcs3.ps1') -Project love-ps3 -Expect 'probe: done' -TimeoutSeconds 60 -DumpAudio
$status = $LASTEXITCODE

Remove-Item $sceneFile, (Join-Path $sounds 'tone_short.ogg'), (Join-Path $sounds 'tone_long.ogg') -ErrorAction SilentlyContinue
$run | Where-Object { $_ -like '*isPlaying*' -or $_ -like '*boot ended*' -or $_ -like '*audio is off*' }

if ($status -ne 0) { Write-Output "scene did not finish: exit=$status"; $run | Select-Object -Last 8; exit 1 }

$wav = Get-ChildItem $rpcs3Dir -Recurse -Filter '*.wav' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $started } | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $wav) { Write-Output 'no dumped WAV found'; exit 1 }

Write-Output "wav: $($wav.FullName)"
python -B (Join-Path $PSScriptRoot 'check.py') $wav.FullName
$checked = $LASTEXITCODE
Remove-Item $wav.FullName -ErrorAction SilentlyContinue
exit $checked
