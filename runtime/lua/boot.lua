-- Stands in for LÖVE's own boot sequence: build `love`, read conf, load main, run.
package.path = INSTALL_ROOT .. '?.lua'
arg = {}

local strict = require 'love_strict'

love = require 'love_modules'

-- misc_functions.lua:109 reads this stray name, which is nil in real LÖVE.
love_graphics_native.dis = false
love.graphics = strict('graphics', love_graphics_native)

-- Test tooling: tests/shader_probe puts a scene here to run in place of the game.
local test_scene = io.open(INSTALL_ROOT .. 'test_scene.lua', 'r')
if test_scene then
    test_scene:close()
    dofile(INSTALL_ROOT .. 'test_scene.lua')
    return
end

require 'conf'
love.conf({ window = {}, modules = {} })

require 'main'

-- After main: the fixes wrap engine classes that main loads.
require 'game_fixes' ()

-- The log shows where a frame's time goes. update_ms includes gc_ms, the game's own GC budget.
local timing = require 'frame_timing' (love_platform.time, { 'update', 'gc', 'draw', 'present' })
love.update = timing.wrap('update', love.update)
love.draw = timing.wrap('draw', love.draw)
nuGC = timing.wrap('gc', nuGC)

-- The game's prep_draw in C (runtime/src/love/lua_prep_draw.c). rawget, because the host
-- runner's graphics table has none, and a missing member raises an error.
local native_prep_draw = rawget(love_graphics_native, 'prepDraw')
if native_prep_draw then prep_draw = native_prep_draw end
require 'native_engine' ()

local unlock_everything = require 'love_unlock'
local show_main_menu = Game.main_menu

-- Not earlier: the unlock tallies need G.GAME, which the main menu creates.
Game.main_menu = function(...)
    -- Before the menu is built: this flag picks what its Play button does.
    G.SETTINGS.tutorial_complete = true
    local results = { show_main_menu(...) }
    unlock_everything()
    return unpack(results)
end

local frame = 0

-- Test tooling: tools/run_rpcs3.ps1 -Frames writes this file to ask for screen dumps.
local function requested_dump_frames()
    local requested = {}
    local file = io.open(INSTALL_ROOT .. 'dump_frames.txt', 'r')
    if not file then return requested end
    for number in file:read('*a'):gmatch('%d+') do requested[tonumber(number)] = true end
    file:close()
    return requested
end

local dump_frames = requested_dump_frames()
local native_present = timing.wrap('present', love_graphics_native.present)

love_graphics_native.present = function()
    if dump_frames[frame] then
        local path = '/dev_hdd0/tmp/frame_' .. frame .. '.ppm'
        io.write('dump ', path, ' ', tostring(love_graphics_native.debugDumpScreen(path)), '\n')
        io.flush()
    end
    native_present()
end

-- A real PS3 has no TTY to read, so a tester sends this file back.
local log_file = io.open('/dev_hdd0/tmp/balatro_log.txt', 'a')

local function write_log(line)
    if not log_file then return end
    log_file:write(line)
    log_file:flush()
end

local function log_progress()
    frame = frame + 1
    timing.end_frame()
    if frame % 300 == 0 then
        local frame_times = timing.report()
        write_log(string.format('frame=%d time=%.1f heap_kb=%d draw_calls=%d state=%s pads=%d underruns=%d %s\n', frame,
            love.timer.getTime(), collectgarbage('count'), love.graphics.drawCalls(), tostring(G and G.STATE),
            love.joystick.getJoystickCount(), love_audio_native.underruns(), frame_times))
        io.write(string.format('frame=%d heap_kb=%d draw_calls=%d state=%s pads=%d underruns=%d %s\n', frame,
            collectgarbage('count'), love.graphics.drawCalls(), tostring(G and G.STATE),
            love.joystick.getJoystickCount(), love_audio_native.underruns(), frame_times))
        io.flush()
    end
end

-- Test tooling: tests/gameplay puts a script here that presses the game's buttons.
local play_driver = io.open(INSTALL_ROOT .. 'play_driver.lua', 'r')
if play_driver then
    play_driver:close()
    dofile(INSTALL_ROOT .. 'play_driver.lua')
end

local frame_step = love.run()
while true do
    local exit_code = frame_step()
    if exit_code then break end
    log_progress()
    if PLAY_DRIVER then PLAY_DRIVER() end
end
