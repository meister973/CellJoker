-- Throwaway: plays the game by calling what its buttons call, and logs the outcome.
local frame, wait_until, last_state, actions = 0, 900, nil, 0
local STATES = G.STATES
local names = {}
for k, v in pairs(STATES) do names[v] = k end

local function log(...)
  io.write('play: ', table.concat({...}, ' '), '\n'); io.flush()
end

local function hand_text()
  local t = {}
  for _, c in ipairs(G.hand.cards) do t[#t + 1] = tostring(c.base and c.base.value):sub(1,2) .. tostring(c.base and c.base.suit):sub(1,1) end
  return table.concat(t, ' ')
end

local started = false
local eval_waits = 0
local bought = false
local shops = 0

function PLAY_DRIVER()
  frame = frame + 1
  if G.STATE ~= last_state then
    last_state = G.STATE
    log('frame', frame, 'state', tostring(names[G.STATE]), 'ante', tostring(G.GAME and G.GAME.round_resets and G.GAME.round_resets.ante),
        'round', tostring(G.GAME and G.GAME.round), 'chips', tostring(G.GAME and G.GAME.chips), 'dollars', tostring(G.GAME and G.GAME.dollars), 'heap_kb', tostring(math.floor(collectgarbage('count'))))
  end
  if frame < wait_until then return end
  wait_until = frame + 60

  if not started then
    started = true
    log('start_run')
    G.SETTINGS.GAMESPEED = 4
    G.FUNCS.start_run(nil, { stake = 1, seed = 'PS3TEST1' })
    wait_until = frame + 200
    return
  end

  if G.CONTROLLER.locked or (G.STATE_COMPLETE == false) then return end

  if G.STATE == STATES.BLIND_SELECT and G.blind_select_opts then
    local option = G.blind_select_opts[string.lower(G.GAME.blind_on_deck)]
    local button = option and option:get_UIE_by_ID('select_blind_button')
    if button then
      log('select_blind', tostring(G.GAME.blind_on_deck))
      G.FUNCS.select_blind(button)
      wait_until = frame + 200
    end
  elseif G.STATE == STATES.SELECTING_HAND and G.hand and G.hand.cards[1] then
    log('hand', hand_text(), 'hands_left', tostring(G.GAME.current_round.hands_left), 'blind_chips', tostring(G.GAME.blind and G.GAME.blind.chips))
    -- Forced win: this run tests what comes after a blind, not scoring.
    G.GAME.blind.chips = 1
    G.GAME.dollars = 50
    for i = 1, math.min(5, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[i]) end
    actions = actions + 1
    G.FUNCS.play_cards_from_highlighted(nil)
    wait_until = frame + 200
  elseif G.STATE == STATES.ROUND_EVAL and G.round_eval then
    -- The button is in a nested box, so call what it calls once the rewards have counted up.
    eval_waits = eval_waits + 1
    if eval_waits >= 6 then
      eval_waits = 0
      log('cash_out dollars', tostring(G.GAME.dollars))
      G.FUNCS.cash_out({ config = {} })
      wait_until = frame + 200
    end
  elseif G.STATE == STATES.SHOP and G.shop then
    local jokers = G.shop_jokers and #G.shop_jokers.cards or 0
    log('shop jokers', tostring(jokers), 'first', tostring(jokers > 0 and G.shop_jokers.cards[1].ability.name))
    if jokers > 0 and not bought then
      bought = true
      local card = G.shop_jokers.cards[1]
      log('buy', tostring(card.ability.name), 'owned_before', tostring(#G.jokers.cards))
      G.FUNCS.buy_from_shop({ config = { ref_table = card, id = 'buy' }, UIBox = { states = {} } })
      wait_until = frame + 120
      return
    end
    log('owned_jokers', tostring(#G.jokers.cards), 'dollars', tostring(G.GAME.dollars))
    bought = false
    shops = shops + 1
    if shops >= 4 then log('driver finished'); PLAY_DRIVER = function() end; return end
    G.FUNCS.toggle_shop({ config = {} })
    wait_until = frame + 200
  elseif G.STATE == STATES.GAME_OVER then
    log('GAME_OVER reached, actions', tostring(actions), 'done')
    PLAY_DRIVER = function() end
  end
end
