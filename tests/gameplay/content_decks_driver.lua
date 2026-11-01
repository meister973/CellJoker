-- Starts a run with every deck and scores one hand with it. This finds crashes. It does not prove
-- that each deck's rule is correct.
local STATES = G.STATES

local frame, next_action = 0, 900
local decks = {}
local deck_index = 0
local hand_played = false
local started_deck = false

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

local function start_next_deck()
    deck_index = deck_index + 1
    local center = decks[deck_index]
    if not center then
        log('all decks ran:', tostring(deck_index - 1))
        log('driver finished')
        PLAY_DRIVER = function() end
        return
    end
    G.GAME.viewed_back = { name = center.name, effect = { center = center } }
    G.FUNCS.start_run(nil, { stake = 1, seed = 'PS3TEST1' })
    hand_played = false
    started_deck = true
    next_action = frame + 150
end

function PLAY_DRIVER()
    frame = frame + 1
    if frame < next_action then return end
    next_action = frame + 30

    if #decks == 0 then
        for _, center in ipairs(G.P_CENTER_POOLS.Back) do decks[#decks + 1] = center end
        G.SETTINGS.tutorial_complete = true
        G.SETTINGS.GAMESPEED = 8
        start_next_deck()
        return
    end

    if G.CONTROLLER.locked or G.STATE_COMPLETE == false then return end

    if G.STATE == STATES.BLIND_SELECT and G.blind_select_opts then
        local option = G.blind_select_opts[string.lower(G.GAME.blind_on_deck)]
        local button = option and option:get_UIE_by_ID('select_blind_button')
        if button then
            G.FUNCS.select_blind(button)
            next_action = frame + 120
        end
    elseif G.STATE == STATES.SELECTING_HAND and G.hand.cards[1] then
        if hand_played then
            start_next_deck()
            return
        end
        hand_played = true
        log('deck', G.GAME.selected_back.name, 'hand_size', tostring(#G.hand.cards),
            'hands', tostring(G.GAME.current_round.hands_left), 'dollars', tostring(G.GAME.dollars),
            'deck_cards', tostring(#G.deck.cards))
        G.GAME.blind.chips = 1e300
        for index = 1, math.min(5, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[index]) end
        G.FUNCS.play_cards_from_highlighted(nil)
        next_action = frame + 60
    elseif G.STATE == STATES.GAME_OVER or G.STATE == STATES.ROUND_EVAL or G.STATE == STATES.SHOP then
        start_next_deck()
    end
end
