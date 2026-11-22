-- Runs under plain Lua 5.1 in the toolchain image: `lua5.1 tests/joystick/test_frame_timing.lua`.
package.path = 'runtime/lua/?.lua;' .. package.path
local new_frame_timing = require 'frame_timing'

local failures, passed = 0, 0

local function equal(actual, expected, label)
    if actual ~= expected then
        error(string.format('%s: want %s got %s', label, tostring(expected), tostring(actual)), 2)
    end
end

local function test(name, body)
    local ok, message = pcall(body)
    if ok then
        passed = passed + 1
    else
        failures = failures + 1
        print('FAIL ' .. name .. ': ' .. tostring(message))
    end
end

-- A clock the test moves by hand, in seconds.
local function fake_clock()
    local now = 0
    return function() return now end, function(seconds) now = now + seconds end
end

-- A stand-in for work that takes `seconds` on the fake clock.
local function work(advance, seconds, result)
    return function()
        advance(seconds)
        return result
    end
end

test('when two frames take 10 ms and 30 ms, reports a 20 ms average and a 30 ms worst', function()
    local clock, advance = fake_clock()
    local timing = new_frame_timing(clock, { 'update' })
    advance(0.010); timing.end_frame()
    advance(0.030); timing.end_frame()
    equal(timing.report(), 'frame_ms=20.0 worst_ms=30.0 update_ms=0.0', 'report')
end)

test('a wrapped function adds its own time to its name and returns its results', function()
    local clock, advance = fake_clock()
    local timing = new_frame_timing(clock, { 'update', 'draw' })
    local update = timing.wrap('update', work(advance, 0.004, 'updated'))
    local draw = timing.wrap('draw', work(advance, 0.002))
    equal(update(), 'updated', 'result')
    draw()
    timing.end_frame()
    equal(timing.report(), 'frame_ms=6.0 worst_ms=6.0 update_ms=4.0 draw_ms=2.0', 'report')
end)

test('a report starts the next window from zero', function()
    local clock, advance = fake_clock()
    local timing = new_frame_timing(clock, { 'update' })
    local update = timing.wrap('update', work(advance, 0.050))
    update(); timing.end_frame()
    timing.report()
    advance(0.010); timing.end_frame()
    equal(timing.report(), 'frame_ms=10.0 worst_ms=10.0 update_ms=0.0', 'report')
end)

test('a report before any frame ends says so instead of dividing by zero', function()
    local clock = fake_clock()
    local timing = new_frame_timing(clock, { 'update' })
    equal(timing.report(), 'frame_ms=none', 'report')
end)

print(string.format('frame_timing: %d passed, %d failed', passed, failures))
os.exit(failures == 0 and 0 or 1)
