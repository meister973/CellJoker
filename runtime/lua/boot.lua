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

local set_language = Game.set_language

-- The language menu draws each name in that language's font, and some fonts do not ship.
Game.set_language = function(...)
    local results = { set_language(...) }
    for _, language in pairs(G.LANGUAGES) do
        if not love.filesystem.getInfo('localization/' .. language.key .. '.lua') then language.omit = true end
    end
    return unpack(results)
end

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
local native_present = love_graphics_native.present

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
    if frame % 300 == 0 then
        write_log(string.format('frame=%d time=%.1f heap_kb=%d draw_calls=%d state=%s pads=%d underruns=%d\n', frame,
            love.timer.getTime(), collectgarbage('count'), love.graphics.drawCalls(), tostring(G and G.STATE),
            love.joystick.getJoystickCount(), love_audio_native.underruns()))
        io.write(string.format('frame=%d heap_kb=%d draw_calls=%d state=%s pads=%d underruns=%d\n', frame,
            collectgarbage('count'), love.graphics.drawCalls(), tostring(G and G.STATE),
            love.joystick.getJoystickCount(), love_audio_native.underruns()))
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
