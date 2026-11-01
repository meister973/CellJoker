-- Plays through pad buttons alone, from the main menu to the shop. It replaces only the read of
-- the hardware pad, so everything from love_joystick to the game's own controller code runs.
local STATES = G.STATES
local names = {}
for name, value in pairs(STATES) do names[value] = name end

local frame = 0
local held_until = 0
local held_button = nil
local next_action = 900
local last_state = nil
local hand_step = 0
local finished = false

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

love_platform.read_pad = function(port, buttons)
    for name in pairs(buttons) do buttons[name] = nil end
    if port ~= 0 then return false end
    if held_button and frame < held_until then buttons[held_button] = true end
    return true, 128, 128, 128, 128
end

local function tap(button, gap)
    log('frame', frame, 'tap', button)
    held_button = button
    held_until = frame + 6
    next_action = frame + (gap or 45)
end

local function play_a_hand()
    local focused = G.CONTROLLER.focused.target

    if #G.hand.highlighted > 0 then
        log('highlighted', tostring(#G.hand.highlighted), 'press play')
        G.GAME.blind.chips = 1
        tap('x', 200)
    elseif focused and focused.area == G.hand then
        log('card focused, select it')
        tap('a')
    else
        -- Focus starts in the left column, and the cards are to its right.
        tap('dpright')
    end
end

-- Only presses `a` on the wanted button. Anything else on screen is moved past with the d-pad.
local function press_or_move(button, wanted)
    if button == wanted then
        log('press', wanted)
        tap('a', 90)
    else
        tap('dpdown', 60)
    end
end

function PLAY_DRIVER()
    frame = frame + 1
    if finished then return end

    if G.STATE ~= last_state then
        last_state = G.STATE
        log('frame', frame, 'state', tostring(names[G.STATE]), 'chips', tostring(G.GAME and G.GAME.chips),
            'console', tostring(G.CONTROLLER.GAMEPAD_CONSOLE), 'hid', tostring(G.CONTROLLER.HID.controller))
    end

    if frame < next_action then return end
    if frame == next_action then G.SETTINGS.tutorial_complete = true end

    local focused = G.CONTROLLER.focused.target
    local button = focused and focused.config and focused.config.button

    if G.STATE == STATES.SHOP then
        log('reached the shop with pad input only')
        log('driver finished')
        finished = true
    elseif G.STATE == STATES.SELECTING_HAND then
        play_a_hand()
    elseif G.STATE == STATES.MENU then
        press_or_move(button, G.OVERLAY_MENU and 'start_setup_run' or 'setup_run')
    elseif G.STATE == STATES.BLIND_SELECT then
        press_or_move(button, 'select_blind')
    elseif G.STATE == STATES.ROUND_EVAL then
        press_or_move(button, 'cash_out')
    else
        -- Dealing and scoring take no input, and a stray press here would open Options.
        next_action = frame + 30
    end
end
