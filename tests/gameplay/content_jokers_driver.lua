-- Runs every joker: five at a time on the board, each group scoring one real hand, with editions
-- cycling so the foil, holo, polychrome and negative shaders draw too. This finds crashes. It does
-- not prove that each effect scores correctly.
local STATES = G.STATES
local GROUP_SIZE = 5
local EDITIONS = { false, { foil = true }, { holo = true }, { polychrome = true }, { negative = true } }

local frame, next_action, started = 0, 900, false
local next_joker = 1
local jokers_run = 0
local groups_in_run = 0

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

local function check_unlocks()
    local locked = 0
    for key, center in pairs(G.P_CENTERS) do
        if (key:find('^j_') or key:find('^b_') or key:find('^v_')) and not center.unlocked then
            locked = locked + 1
        end
    end
    log('locked jokers, decks and vouchers:', tostring(locked), 'all_unlocked',
        tostring(G.PROFILES[G.SETTINGS.profile].all_unlocked))
end

local function deal_next_group()
    for index = #G.jokers.cards, 1, -1 do
        local card = G.jokers.cards[index]
        card:remove_from_deck()
        card:remove()
    end
    G.jokers.config.card_limit = GROUP_SIZE + 5

    local names = {}
    for offset = 0, GROUP_SIZE - 1 do
        local center = G.P_CENTER_POOLS.Joker[next_joker]
        if not center then break end
        local card = create_card('Joker', G.jokers, nil, nil, true, nil, center.key, 'test')
        local edition = EDITIONS[next_joker % #EDITIONS + 1]
        if edition then card:set_edition(edition, true, true) end
        card:add_to_deck()
        G.jokers:emplace(card)
        names[#names + 1] = center.key
        next_joker = next_joker + 1
        jokers_run = jokers_run + 1
    end
    log('joker', tostring(jokers_run), table.concat(names, ' '))
end

local function play_hand()
    -- Keeps the same round going, so every group gets a hand.
    G.GAME.blind.chips = 1e300
    G.GAME.current_round.hands_left = 4
    G.GAME.dollars = 100
    for index = 1, math.min(5, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[index]) end
    G.FUNCS.play_cards_from_highlighted(nil)
    log('score', tostring(G.GAME.chips))
end

function PLAY_DRIVER()
    frame = frame + 1
    if frame < next_action then return end
    next_action = frame + 30

    if not started then
        started = true
        check_unlocks()
        G.SETTINGS.tutorial_complete = true
        G.SETTINGS.GAMESPEED = 8
        G.FUNCS.start_run(nil, { stake = 1, seed = 'PS3TEST1' })
        next_action = frame + 150
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
        if not G.P_CENTER_POOLS.Joker[next_joker] then
            log('all jokers ran:', tostring(jokers_run), 'of', tostring(#G.P_CENTER_POOLS.Joker))
            log('driver finished')
            PLAY_DRIVER = function() end
            return
        end
        -- Each hand spends five cards, so a deck lasts about ten groups.
        if groups_in_run == 8 then
            groups_in_run = 0
            G.FUNCS.start_run(nil, { stake = 1, seed = 'PS3TEST1' })
            next_action = frame + 150
            return
        end
        groups_in_run = groups_in_run + 1
        deal_next_group()
        play_hand()
        next_action = frame + 60
    elseif G.STATE == STATES.GAME_OVER then
        log('game over before the last joker')
        PLAY_DRIVER = function() end
    end
end
