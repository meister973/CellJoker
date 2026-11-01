-- Runs under plain Lua 5.1 in the toolchain image: `lua5.1 tests/joystick/test_love_joystick.lua`.
package.path = 'runtime/lua/?.lua;' .. package.path
local new_joystick_module = require 'love_joystick'

local CENTER = 128
local failures, passed = 0, 0

-- A pad the test controls: buttons held, stick values, and whether it is plugged in.
local function fake_pads()
    local pads = {}
    local function read_pad(port, buttons)
        local pad = pads[port]
        if not pad then return false end
        for name in pairs(buttons) do buttons[name] = nil end
        for name in pairs(pad.held) do buttons[name] = true end
        return true, pad.lx, pad.ly, pad.rx, pad.ry
    end
    local function plug(port)
        pads[port] = { held = {}, lx = CENTER, ly = CENTER, rx = CENTER, ry = CENTER }
        return pads[port]
    end
    local function unplug(port) pads[port] = nil end
    return read_pad, plug, unplug
end

local function setup()
    local read_pad, plug, unplug = fake_pads()
    return new_joystick_module(read_pad), plug, unplug
end

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

test('no pad plugged in lists no joysticks and sends no events', function()
    local module = setup()
    equal(#module.poll(), 0, 'events')
    equal(#module.getJoysticks(), 0, 'joysticks')
end)

test('pad plugged into a later port is found', function()
    local module, plug = setup()
    plug(3)
    module.poll()
    equal(#module.getJoysticks(), 1, 'joysticks')
end)

test('holding cross sends one press, not one per frame', function()
    local module, plug = setup()
    plug(0).held.a = true
    local events = module.poll()
    equal(#events, 1, 'first poll')
    equal(events[1][1], 'gamepadpressed', 'name')
    equal(events[1][3], 'a', 'button')
    equal(#module.poll(), 0, 'second poll')
end)

test('letting go sends a release', function()
    local module, plug = setup()
    local pad = plug(0)
    pad.held.a = true
    module.poll()
    pad.held.a = nil
    local events = module.poll()
    equal(#events, 1, 'events')
    equal(events[1][1], 'gamepadreleased', 'name')
end)

test('the event carries the joystick that the game hands to set_gamepad', function()
    local module, plug = setup()
    plug(0).held.dpup = true
    local events = module.poll()
    equal(events[1][2], module.getJoysticks()[1], 'joystick')
end)

test('mapping string names a Sony pad, so the game shows PlayStation glyphs', function()
    local module, plug = setup()
    plug(0)
    module.poll()
    local mapping = module.getJoysticks()[1]:getGamepadMappingString()
    local name = mapping:match('^%x*,(.-),')
    assert(name and name:match('Sony%f[%W]'), 'name was ' .. tostring(name))
end)

test('stick at center reads zero', function()
    local module, plug = setup()
    plug(0)
    module.poll()
    equal(module.getJoysticks()[1]:getGamepadAxis('leftx'), 0, 'leftx')
end)

test('small drift around center stays inside the deadzone', function()
    local module, plug = setup()
    plug(0).lx = CENTER + 10
    module.poll()
    equal(module.getJoysticks()[1]:getGamepadAxis('leftx'), 0, 'leftx')
end)

test('stick extremes reach minus one and one', function()
    local module, plug = setup()
    local pad = plug(0)
    pad.lx, pad.ly = 0, 255
    module.poll()
    local joystick = module.getJoysticks()[1]
    equal(joystick:getGamepadAxis('leftx'), -1, 'leftx')
    equal(joystick:getGamepadAxis('lefty'), 1, 'lefty')
end)

test('moving a stick sends an axis event, which tells the game a pad is in use', function()
    local module, plug = setup()
    local pad = plug(0)
    module.poll()
    pad.rx = 255
    local events = module.poll()
    equal(#events, 1, 'events')
    equal(events[1][1], 'joystickaxis', 'name')
    equal(events[1][4], 1, 'value')
end)

test('a still stick sends no more axis events', function()
    local module, plug = setup()
    local pad = plug(0)
    pad.rx = 255
    module.poll()
    equal(#module.poll(), 0, 'events')
end)

test('L2 and R2 are trigger axes, because the game reads them that way', function()
    local module, plug = setup()
    plug(0).held.triggerleft = true
    local events = module.poll()
    local joystick = module.getJoysticks()[1]
    equal(joystick:getGamepadAxis('triggerleft'), 1, 'triggerleft')
    equal(joystick:getGamepadAxis('triggerright'), 0, 'triggerright')
    equal(#events, 1, 'events')
    equal(events[1][1], 'joystickaxis', 'name')
end)

test('unplugging releases held buttons and removes the joystick', function()
    local module, plug, unplug = setup()
    plug(0).held.a = true
    module.poll()
    unplug(0)
    local events = module.poll()
    equal(#events, 1, 'events')
    equal(events[1][1], 'gamepadreleased', 'name')
    equal(#module.getJoysticks(), 0, 'joysticks')
end)

test('a pad that returns after a sleep is the same joystick object', function()
    local module, plug, unplug = setup()
    plug(0)
    module.poll()
    local first = module.getJoysticks()[1]
    unplug(0)
    module.poll()
    plug(0)
    module.poll()
    equal(module.getJoysticks()[1], first, 'joystick')
end)

test('vibration calls are accepted', function()
    local module, plug = setup()
    plug(0)
    module.poll()
    equal(module.getJoysticks()[1]:isGamepad(), true, 'isGamepad')
    module.getJoysticks()[1]:setVibration(0.5, 0.5)
end)

print(string.format('joystick tests: passed=%d failures=%d', passed, failures))
os.exit(failures == 0 and 0 or 1)
