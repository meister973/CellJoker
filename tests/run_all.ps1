# Runs every test with no window: unit tests, then RPCS3 with the built game. Takes about 40 minutes.
# Run tools/build_love.ps1 first. Prints one PASS or FAIL line per test.
$root = Split-Path -Parent $PSScriptRoot
$failed = @()

function Invoke-Test([string]$Name, [scriptblock]$Test) {
    $output = & $Test 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Output "PASS $Name"
        return
    }
    Write-Output "FAIL $Name"
    $output | Select-Object -Last 12 | ForEach-Object { "  $_" }
    $script:failed += $Name
}

function Invoke-ToolchainLua([string]$File) {
    docker run --rm -v "${root}:/work" -w /work balatro-ps3dev:2 lua5.1 $File
}

function Invoke-Driver([string]$Driver, [int]$TimeoutSeconds = 280) {
    pwsh -NoProfile -File (Join-Path $root 'tests\gameplay\run_driver.ps1') -Driver $Driver -TimeoutSeconds $TimeoutSeconds
}

Invoke-Test 'tool unit tests' {
    Push-Location (Join-Path $root 'tools\tests')
    python -B -m unittest test_cgcomp_full_precision test_texture_cook test_make_game_data
    Pop-Location
}
Invoke-Test 'love_joystick' { Invoke-ToolchainLua 'tests/joystick/test_love_joystick.lua' }
Invoke-Test 'love_thread' { Invoke-ToolchainLua 'tests/joystick/test_love_thread.lua' }

Invoke-Test 'lua behavior' { pwsh -NoProfile -File (Join-Path $root 'tests\lua_compat\run.ps1') }
Invoke-Test 'shader probe' { pwsh -NoProfile -File (Join-Path $root 'tests\shader_probe\run.ps1') }
Invoke-Test 'shader uniform slots' { pwsh -NoProfile -File (Join-Path $root 'tests\shader_probe\run_slots.ps1') }
Invoke-Test 'audio tones' { pwsh -NoProfile -File (Join-Path $root 'tests\audio\run.ps1') }

Invoke-Test 'play to the shop' { Invoke-Driver 'play_driver.lua' }
Invoke-Test 'no tutorial' { Invoke-Driver 'skip_tutorial_driver.lua' }
Invoke-Test 'fresh profile' { Invoke-Driver 'profile_driver.lua' }
Invoke-Test 'pad buttons' { Invoke-Driver 'pad_driver.lua' }
Invoke-Test 'pixel smoothing' { Invoke-Driver 'pixel_smoothing_driver.lua' }
Invoke-Test 'every deck' { Invoke-Driver 'content_decks_driver.lua' 590 }
Invoke-Test 'every joker' { Invoke-Driver 'content_jokers_driver.lua' 900 }
Invoke-Test 'save a game' { Invoke-Driver 'save_write_driver.lua' }
Invoke-Test 'load the saved game' {
    pwsh -NoProfile -File (Join-Path $root 'tests\gameplay\run_driver.ps1') -Driver save_read_driver.lua -KeepSave
}
Invoke-Test 'every card and pack, with sound' { pwsh -NoProfile -File (Join-Path $root 'tests\audio\run_game.ps1') }

Write-Output "failed: $($failed.Count) $($failed -join ', ')"
exit $failed.Count
