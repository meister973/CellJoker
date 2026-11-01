-- love.joystick over PS3 pads. Pads are polled, and the game expects LÖVE's events, so this
-- turns each frame's pad state into presses, releases and axis changes.
--
-- read_pad(port, buttons) fills `buttons` with the names held, and returns connected and the
-- four stick values (0 to 255). Button names are LÖVE's gamepad names.
local PORT_COUNT = 7
local STICK_CENTER = 128
local STICK_RANGE = 127
local STICK_DEADZONE = 16

-- The name matters: controller.lua picks PlayStation button glyphs when it finds "Sony".
local MAPPING_STRING = '00000000000000000000000000000000,Sony DualShock 3,platform:PS3,'

local STICK_AXES = { 'leftx', 'lefty', 'rightx', 'righty' }
local TRIGGER_AXES = { 'triggerleft', 'triggerright' }
local AXIS_NUMBERS = { leftx = 1, lefty = 2, rightx = 3, righty = 4, triggerleft = 5, triggerright = 6 }

-- LÖVE reports these as axes, and controller.lua reads them through getGamepadAxis.
local IS_TRIGGER = { triggerleft = true, triggerright = true }

local function stick_value(raw)
    local offset = raw - STICK_CENTER
    if math.abs(offset) <= STICK_DEADZONE then return 0 end
    return math.max(-1, math.min(1, offset / STICK_RANGE))
end

local Joystick = {}
Joystick.__index = Joystick

function Joystick:getGamepadMappingString() return MAPPING_STRING end
function Joystick:getName() return 'Sony DualShock 3' end
function Joystick:getID() return self.port + 1 end
function Joystick:isGamepad() return true end
function Joystick:isConnected() return self.connected end
function Joystick:isGamepadDown(button) return self.held[button] == true end
function Joystick:getGamepadAxis(axis) return self.axes[axis] or 0 end
function Joystick:isVibrationSupported() return false end
function Joystick:setVibration() return false end

local function new_joystick(port)
    local joystick = { port = port, connected = false, held = {}, axes = {}, frame_buttons = {} }
    for _, axis in ipairs(STICK_AXES) do joystick.axes[axis] = 0 end
    for _, axis in ipairs(TRIGGER_AXES) do joystick.axes[axis] = 0 end
    return setmetatable(joystick, Joystick)
end

local function set_axis(joystick, axis, value, events)
    if joystick.axes[axis] == value then return end
    joystick.axes[axis] = value
    events[#events + 1] = { 'joystickaxis', joystick, AXIS_NUMBERS[axis], value }
end

local function update_buttons(joystick, events)
    for name in pairs(joystick.frame_buttons) do
        if not IS_TRIGGER[name] and not joystick.held[name] then
            joystick.held[name] = true
            events[#events + 1] = { 'gamepadpressed', joystick, name }
        end
    end
    for name in pairs(joystick.held) do
        if not joystick.frame_buttons[name] then
            joystick.held[name] = nil
            events[#events + 1] = { 'gamepadreleased', joystick, name }
        end
    end
end

local function update_joystick(joystick, events, ...)
    local sticks = { ... }
    for index, axis in ipairs(STICK_AXES) do
        set_axis(joystick, axis, stick_value(sticks[index]), events)
    end
    for _, axis in ipairs(TRIGGER_AXES) do
        set_axis(joystick, axis, joystick.frame_buttons[axis] and 1 or 0, events)
    end
    update_buttons(joystick, events)
end

local function release_everything(joystick, events)
    joystick.frame_buttons = {}
    update_buttons(joystick, events)
    for _, axis in ipairs(STICK_AXES) do set_axis(joystick, axis, 0, events) end
    for _, axis in ipairs(TRIGGER_AXES) do set_axis(joystick, axis, 0, events) end
end

return function(read_pad)
    local joysticks = {}
    for port = 0, PORT_COUNT - 1 do joysticks[port] = new_joystick(port) end

    local module = {}

    function module.poll()
        local events = {}
        for port = 0, PORT_COUNT - 1 do
            local joystick = joysticks[port]
            local connected, lx, ly, rx, ry = read_pad(port, joystick.frame_buttons)
            if connected then
                joystick.connected = true
                update_joystick(joystick, events, lx, ly, rx, ry)
            elseif joystick.connected then
                joystick.connected = false
                release_everything(joystick, events)
            end
        end
        return events
    end

    function module.getJoysticks()
        local connected = {}
        for port = 0, PORT_COUNT - 1 do
            if joysticks[port].connected then connected[#connected + 1] = joysticks[port] end
        end
        return connected
    end

    function module.getJoystickCount() return #module.getJoysticks() end
    function module.loadGamepadMappings() end

    return module
end
