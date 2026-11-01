-- Every LÖVE module except graphics. Files are real; audio, threads and input are not
-- connected yet, and accept what the game sends them.
local strict = require 'love_strict'
local platform = love_platform

-- Whatever the console's display accepted, which is usually 720p but need not be.
local SCREEN_WIDTH, SCREEN_HEIGHT = love_graphics_native.getDimensions()
local SAVE_ROOT = INSTALL_ROOT .. 'save/'
platform.mkdir(SAVE_ROOT)

local love = {}

---------------------------------------------------------------- filesystem
local function resolve(path)
    if platform.stat(SAVE_ROOT .. path) then return SAVE_ROOT .. path end
    return INSTALL_ROOT .. path
end

local function read_whole_file(full_path)
    local file = io.open(full_path, 'rb')
    if not file then return nil end
    local contents = file:read('*a')
    file:close()
    return contents
end

local function write_whole_file(path, contents, mode)
    local file = io.open(SAVE_ROOT .. path, mode)
    if not file then return false, 'cannot open ' .. path end
    file:write(contents)
    file:close()
    return true
end

love.filesystem = strict('filesystem', {
    getInfo = function(path)
        local kind, size = platform.stat(resolve(path))
        if not kind then return nil end
        return { type = kind, size = size, modtime = 0 }
    end,
    read = function(path)
        local contents = read_whole_file(resolve(path))
        if not contents then return nil, 'not found: ' .. path end
        return contents, #contents
    end,
    write = function(path, contents) return write_whole_file(path, contents, 'wb') end,
    append = function(path, contents) return write_whole_file(path, contents, 'ab') end,
    remove = function(path) return platform.remove(SAVE_ROOT .. path) end,
    createDirectory = function(path) return platform.mkdir(SAVE_ROOT .. path) end,
    getDirectoryItems = function(path) return platform.list(resolve(path)) end,
    getSaveDirectory = function() return SAVE_ROOT end,
    getSourceBaseDirectory = function() return INSTALL_ROOT end,
})

---------------------------------------------------------------- timer
local timer_start = platform.time()
local last_step = 0
local frames_this_second, fps, fps_window_start = 0, 0, 0

love.timer = strict('timer', {
    getTime = function() return platform.time() - timer_start end,
    sleep = platform.sleep,
    step = function()
        local now = platform.time() - timer_start
        local delta = now - last_step
        last_step = now
        frames_this_second = frames_this_second + 1
        if now - fps_window_start >= 1 then
            fps = frames_this_second
            frames_this_second, fps_window_start = 0, now
        end
        return delta
    end,
    getFPS = function() return fps end,
    getDelta = function() return 1 / 60 end,
})

---------------------------------------------------------------- system and window
love.system = strict('system', {
    -- The game's own console flags (globals.lua) then apply with no game edit.
    getOS = function() return 'ps4' end,
    openURL = function() return false end,
    getClipboardText = function() return '' end,
    setClipboardText = function() end,
})

local WINDOW_FLAGS = { fullscreen = true, fullscreentype = 'exclusive', vsync = 1, display = 1,
    minwidth = 1, minheight = 1, resizable = false, borderless = false, centered = true, x = 0, y = 0 }

love.window = strict('window', {
    getDesktopDimensions = function() return SCREEN_WIDTH, SCREEN_HEIGHT end,
    getDisplayCount = function() return 1 end,
    getFullscreenModes = function() return { { width = SCREEN_WIDTH, height = SCREEN_HEIGHT } } end,
    getMode = function() return SCREEN_WIDTH, SCREEN_HEIGHT, WINDOW_FLAGS end,
    setMode = function() return true end,
    updateMode = function() return true end,
    getTitle = function() return 'Balatro' end,
    setTitle = function() end,
    setIcon = function() end,
    isOpen = function() return true end,
    toPixels = function(value) return value end,
    fromPixels = function(value) return value end,
    getDPIScale = function() return 1 end,
    showMessageBox = function(title, message) print('messagebox: ' .. tostring(title) .. ': ' .. tostring(message)) end,
})

---------------------------------------------------------------- threads
local function load_worker(path)
    return assert(loadfile(INSTALL_ROOT .. path))
end

local threads = require 'love_thread' (load_worker)
love.thread = strict('thread', {
    newThread = threads.newThread,
    getChannel = threads.getChannel,
})

---------------------------------------------------------------- input
love.mouse = strict('mouse', {
    getPosition = function() return SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 end,
    setPosition = function() end,
    isVisible = function() return false end,
    setVisible = function() end,
    setGrabbed = function() end,
    setRelativeMode = function() end,
    isDown = function() return false end,
})

love.keyboard = strict('keyboard', {
    isDown = function() return false end,
    setKeyRepeat = function() end,
    setTextInput = function() end,
})

love.touch = strict('touch', { getTouches = function() return {} end })

-- Read through the table each time, so tests/gameplay can feed in a scripted pad.
local joystick = require 'love_joystick' (function(port, buttons)
    return platform.read_pad(port, buttons)
end)
love.joystick = strict('joystick', {
    getJoysticks = joystick.getJoysticks,
    getJoystickCount = joystick.getJoystickCount,
    loadGamepadMappings = joystick.loadGamepadMappings,
})

local pending_events = {}
local next_event = 1

love.event = strict('event', {
    pump = function()
        pending_events = joystick.poll()
        next_event = 1
        -- The XMB's Quit Game item arrives through here.
        if platform.exit_requested() then
            pending_events[#pending_events + 1] = { 'quit' }
        end
    end,
    poll = function()
        return function()
            local event = pending_events[next_event]
            if not event then return nil end
            next_event = next_event + 1
            return unpack(event)
        end
    end,
    quit = function() end,
})

---------------------------------------------------------------- audio and data
love.audio = strict('audio', love_audio_native)
love.sound = strict('sound', {})
love.data = strict('data', love_data_native)

-- The workers open with lines such as require "love.timer", which are already here.
for _, name in ipairs({ 'audio', 'sound', 'system', 'timer', 'thread', 'filesystem' }) do
    package.loaded['love.' .. name] = love[name]
end

love.arg = { parseGameArguments = function(arguments) return arguments end }
love.handlers = {
    gamepadpressed = function(pad, button) love.gamepadpressed(pad, button) end,
    gamepadreleased = function(pad, button) love.gamepadreleased(pad, button) end,
    joystickaxis = function(pad, axis, value) love.joystickaxis(pad, axis, value) end,
}

return love
