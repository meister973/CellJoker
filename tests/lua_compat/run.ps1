# Differential test: real LuaJIT in the toolchain image against our Lua 5.1 on the Cell.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$image = 'balatro-ps3dev:2'
$project = Join-Path $root 'lua-compat-test'
$generated = Join-Path $project 'generated'
New-Item -ItemType Directory -Force $generated | Out-Null

# Embed the script as a C string, so the driver needs no file access.
$lines = Get-Content (Join-Path $PSScriptRoot 'cases.lua') | ForEach-Object {
    '"' + $_.Replace('\', '\\').Replace('"', '\"') + '\n"'
}
Set-Content (Join-Path $generated 'cases_lua.h') -Value (@('#define CASES_LUA \') + ($lines | ForEach-Object { "    $_ \" }) + '    ""')

$reference = @'
bit = require 'bit'
for _, line in ipairs(dofile('/work/tests/lua_compat/cases.lua')) do print('case: ' .. line) end
'@
Set-Content (Join-Path $generated 'reference.lua') -Value $reference
$expected = docker run --rm -v "${root}:/work" $image luajit /work/lua-compat-test/generated/reference.lua
docker run --rm -v "${root}:/work" -w /work/lua-compat-test $image sh -c 'make 2>&1 | grep -E "error|warning: impl|Error " || true'

$run = & pwsh -NoProfile -File (Join-Path $root 'tools\run_rpcs3.ps1') -Project 'lua-compat-test' -Expect 'compat: done' -TimeoutSeconds 60
$runLine = $run | Where-Object { $_ -like 'run: *' }
$runDir = ($runLine -replace '^run: ', '') -replace ' exited=.*$', ''
$tty = @(Get-Content (Join-Path $runDir 'TTY.log'))
$tty | Where-Object { $_ -like 'compat: FAIL*' }
$actual = @($tty | Where-Object { $_ -like 'case: *' })

$failures = 0
for ($index = 0; $index -lt [Math]::Max($expected.Count, $actual.Count); $index++) {
    if ($expected[$index] -cne $actual[$index]) {
        $failures++
        Write-Output "DIFF want [$($expected[$index])] got [$($actual[$index])]"
    }
}

Write-Output "cases=$($expected.Count) actual=$($actual.Count) failures=$failures run=$runDir"
if ($failures -gt 0) { exit 1 }
