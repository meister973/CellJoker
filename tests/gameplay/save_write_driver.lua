-- First half of the save test: plays into the shop so the game saves the run, then records what
-- the second boot must find. save_read_driver.lua runs next, with the save folder kept.
local STATES = G.STATES
local frame, next_action, started = 0, 900, false
local bought = false

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

-- Bytes of `return {a=1}`, raw deflate level 1, made by Python's zlib. LÖVE's "deflate" is raw, so
-- this also proves that a save copied from a PC would open.
local KNOWN_RAW_DEFLATE = '\043\074\045\041\045\202\083\168\078\180\053\172\005\000'

local function check_data_module()
    local text = string.rep('balatro saves ', 100)
    local packed = love.data.compress('string', 'deflate', text, 1)
    assert(#packed < #text, 'compress did not shrink')
    assert(love.data.decompress('string', 'deflate', packed) == text, 'round trip differs')
    assert(love.data.decompress('string', 'deflate', KNOWN_RAW_DEFLATE) == 'return {a=1}', 'known data differs')
    assert(not pcall(love.data.decompress, 'string', 'deflate', 'not deflate data'), 'bad data accepted')
    log('love.data ok')
end

local function write_expected()
    local game = G.GAME
    local lines = {
        'round=' .. tostring(game.round),
        'dollars=' .. tostring(game.dollars),
        'seed=' .. tostring(game.pseudorandom.seed),
        'jokers=' .. tostring(#G.jokers.cards),
        'joker1=' .. tostring(G.jokers.cards[1] and G.jokers.cards[1].ability.name),
    }
    love.filesystem.write('test_expected.txt', table.concat(lines, '\n'))
    log('expected', table.concat(lines, ' '))
end

function PLAY_DRIVER()
    frame = frame + 1
    if frame < next_action then return end
    next_action = frame + 30

    if not started then
        started = true
        check_data_module()
        G.SETTINGS.tutorial_complete = true
        G.SETTINGS.GAMESPEED = 8
        G.FUNCS.start_run(nil, { stake = 1, seed = 'PS3SAVE1' })
        next_action = frame + 150
        return
    end

    if G.CONTROLLER.locked or G.STATE_COMPLETE == false then return end

    if G.STATE == STATES.BLIND_SELECT and G.blind_select_opts then
        local option = G.blind_select_opts[string.lower(G.GAME.blind_on_deck)]
        local button = option and option:get_UIE_by_ID('select_blind_button')
        if button then G.FUNCS.select_blind(button); next_action = frame + 120 end
    elseif G.STATE == STATES.SELECTING_HAND and G.hand.cards[1] then
        G.GAME.blind.chips = 1
        for index = 1, math.min(5, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[index]) end
        G.FUNCS.play_cards_from_highlighted(nil)
        next_action = frame + 120
    elseif G.STATE == STATES.ROUND_EVAL and G.round_eval then
        next_action = frame + 240
        if G.round_eval_waited then G.FUNCS.cash_out({ config = {} }) end
        G.round_eval_waited = true
    elseif G.STATE == STATES.SHOP and G.shop then
        if not bought then
            bought = true
            G.GAME.dollars = 50
            local card = G.shop_jokers.cards[1]
            G.FUNCS.buy_from_shop({ config = { ref_table = card, id = 'buy' }, UIBox = { states = {} } })
            next_action = frame + 120
            return
        end
        -- The game saves on its own timer: asking now keeps the test short, not the code path.
        save_run()
        G.FILE_HANDLER.force = true
        write_expected()
        PLAY_DRIVER = function()
            frame = frame + 1
            if frame > next_action + 300 then
                log('save.jkr', tostring(love.filesystem.getInfo('1/save.jkr') ~= nil),
                    'settings.jkr', tostring(love.filesystem.getInfo('settings.jkr') ~= nil))
                log('driver finished')
                PLAY_DRIVER = function() end
            end
        end
    end
end
