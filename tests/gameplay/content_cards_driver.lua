-- Runs every tarot, planet and spectral card, every voucher and every booster pack, plus a discard.
-- This finds crashes. It does not prove that each effect is correct.
local STATES = G.STATES

local frame, next_action, started = 0, 900, false
local stage = 'consumables'
local next_consumable, next_voucher, next_booster = 1, 1, 1
local consumables = {}
local held_card
local use_attempts = 0
local used, skipped, discarded, opened = 0, 0, false, 0
local reached_shop, eval_waited = false, false

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

local function restart_run()
    G.FUNCS.start_run(nil, { stake = 1, seed = 'PS3CARD1' })
    next_action = frame + 150
end

local function clear_highlight()
    if G.hand then G.hand:unhighlight_all() end
end

local function give_plain_joker()
    for _, joker in ipairs(G.jokers.cards) do
        if not joker.edition then return end
    end
    local joker = create_card('Joker', G.jokers, nil, nil, nil, nil, 'j_joker', 'test')
    joker:add_to_deck()
    G.jokers:emplace(joker)
end

local function take_next_consumable()
    local center = consumables[next_consumable]
    if not center then
        log('consumables used', tostring(used), 'not usable right then', tostring(skipped), 'of', tostring(#consumables))
        stage = 'vouchers'
        return
    end
    next_consumable = next_consumable + 1
    log('card', center.key)

    clear_highlight()
    G.GAME.consumeable_buffer = 0
    G.consumeables.config.card_limit = 10
    G.jokers.config.card_limit = 10
    G.GAME.dollars = 100
    -- The Fool copies the last card used, and Wheel of Fortune needs a joker with no edition.
    G.GAME.last_tarot_planet = 'c_moon'
    give_plain_joker()

    local count = (center.config and center.config.max_highlighted) or 1
    for index = 1, math.min(count, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[index]) end

    held_card = create_card(center.set, G.consumeables, nil, nil, true, nil, center.key, 'test')
    held_card:add_to_deck()
    G.consumeables:emplace(held_card)
end

local function use_held_card()
    local card = held_card

    -- The card before this one can still hold the use lock while its effect plays.
    if not card:can_use_consumeable() and use_attempts < 5 then
        use_attempts = use_attempts + 1
        next_action = frame + 60
        return
    end
    held_card = nil
    use_attempts = 0

    if card:can_use_consumeable() then
        used = used + 1
        G.consumeables:remove_card(card)
        card:use_consumeable(G.consumeables)
        card:remove()
    else
        skipped = skipped + 1
        log('not usable right then', card.config.center.key)
        card:remove()
    end
    next_action = frame + 45

    -- Some spectrals destroy cards or the hand. A fresh run keeps the rest of the test going.
    if #G.hand.cards < 5 or #G.deck.cards < 5 then restart_run() end
end

-- Card:update works out what a card needs before it can be used, so a card waits one frame in hand.
local function use_next_consumable()
    if held_card then
        use_held_card()
        return
    end
    take_next_consumable()
end

local function apply_next_voucher()
    local center = G.P_CENTER_POOLS.Voucher[next_voucher]
    if not center then
        log('vouchers applied', tostring(next_voucher - 1))
        stage = 'discard'
        return
    end
    next_voucher = next_voucher + 1
    log('voucher', center.key)
    local card = Card(0, 0, G.CARD_W, G.CARD_H, G.P_CARDS.empty, center, { bypass_discovery_center = true })
    card:apply_to_run(center)
    card:remove()
end

local function try_a_discard()
    clear_highlight()
    for index = 1, math.min(5, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[index]) end
    G.GAME.current_round.discards_left = math.max(G.GAME.current_round.discards_left, 1)
    log('discard', tostring(#G.hand.highlighted), 'cards')
    G.FUNCS.discard_cards_from_highlighted(nil)
    discarded = true
    stage = 'to_shop'
    next_action = frame + 150
end

local function play_to_win()
    G.GAME.blind.chips = 1
    clear_highlight()
    for index = 1, math.min(5, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[index]) end
    G.FUNCS.play_cards_from_highlighted(nil)
    next_action = frame + 150
end

local function open_next_booster()
    local center = G.P_CENTER_POOLS.Booster[next_booster]
    if not center then
        log('boosters opened', tostring(opened), 'of', tostring(#G.P_CENTER_POOLS.Booster))
        log('driver finished')
        PLAY_DRIVER = function() end
        return
    end
    next_booster = next_booster + 1
    log('booster', center.key)
    G.GAME.dollars = 100

    local card = Card(G.shop_booster.T.x, G.shop_booster.T.y, G.CARD_W * 1.27, G.CARD_H * 1.27,
        G.P_CARDS.empty, center, { bypass_discovery_center = true, bypass_discovery_ui = true })
    card.cost = 0
    create_shop_card_ui(card)
    G.shop_booster:emplace(card)
    G.FUNCS.use_card({ config = { ref_table = card } })
    opened = opened + 1
    next_action = frame + 150
end

function PLAY_DRIVER()
    frame = frame + 1
    if frame < next_action then return end
    next_action = frame + 20

    if not started then
        started = true
        for _, set in ipairs({ 'Tarot', 'Planet', 'Spectral' }) do
            for _, center in ipairs(G.P_CENTER_POOLS[set]) do consumables[#consumables + 1] = center end
        end
        G.SETTINGS.tutorial_complete = true
        G.SETTINGS.GAMESPEED = 8
        restart_run()
        return
    end

    if G.CONTROLLER.locked or G.STATE_COMPLETE == false then return end

    -- A pack is open: leave it, and carry on in the shop.
    if G.STATE == STATES.TAROT_PACK or G.STATE == STATES.PLANET_PACK or G.STATE == STATES.SPECTRAL_PACK
        or G.STATE == STATES.STANDARD_PACK or G.STATE == STATES.BUFFOON_PACK then
        if G.pack_cards and G.pack_cards.cards[1] then
            G.FUNCS.skip_booster({ config = {} })
            next_action = frame + 90
        end
        return
    end

    if G.STATE == STATES.BLIND_SELECT and G.blind_select_opts then
        local option = G.blind_select_opts[string.lower(G.GAME.blind_on_deck)]
        local button = option and option:get_UIE_by_ID('select_blind_button')
        if button then
            G.FUNCS.select_blind(button)
            next_action = frame + 120
        end
    elseif G.STATE == STATES.SELECTING_HAND and G.hand.cards[1] then
        if stage == 'consumables' then
            use_next_consumable()
        elseif stage == 'vouchers' then
            apply_next_voucher()
        elseif stage == 'discard' then
            try_a_discard()
        elseif stage == 'to_shop' then
            play_to_win()
        end
    elseif G.STATE == STATES.ROUND_EVAL and G.round_eval then
        -- Cashing out while the score rows still arrive crashes the game: they need G.round_eval.
        next_action = frame + 240
        if eval_waited then
            eval_waited = false
            G.FUNCS.cash_out({ config = {} })
            return
        end
        eval_waited = true
    elseif G.STATE == STATES.SHOP and G.shop then
        if not reached_shop then
            reached_shop = true
            log('discard ran', tostring(discarded), 'reached the shop')
        end
        open_next_booster()
    elseif G.STATE == STATES.GAME_OVER then
        log('game over: starting again')
        restart_run()
    end
end
