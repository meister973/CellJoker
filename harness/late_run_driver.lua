-- Plays one long run by calling what the game's buttons call, and reports what each round
-- cost. Every hand is a forced win, so the run reaches late antes in a predictable time.
-- Each shop buys jokers until the slots are full, so the saved state grows like a real run.
--
-- Lines start with "metric:" (one per round) or "save:" (one per save request).
local MAX_ANTE = tonumber(os.getenv('HARNESS_MAX_ANTE') or '') or 12
local FRAME_LIMIT = tonumber(os.getenv('HARNESS_FRAME_LIMIT') or '') or 400000
local SEED = os.getenv('HARNESS_SEED') or 'PS3TEST1'
-- HARNESS_CALLGRIND_FROM=<frame> collects callgrind costs for HARNESS_CALLGRIND_FRAMES frames,
-- then ends the run. run.sh must run the harness under valgrind --instr-atstart=no.
local CALLGRIND_FROM = tonumber(os.getenv('HARNESS_CALLGRIND_FROM') or '')
local CALLGRIND_FRAMES = tonumber(os.getenv('HARNESS_CALLGRIND_FRAMES') or '') or 600

-- HARNESS_CENSUS=1 walks the heap after each round, which is slow but names what grows.
local census = os.getenv('HARNESS_CENSUS') == '1' and dofile(INSTALL_ROOT .. 'heap_census.lua') or nil
local last_snapshot


local STATES = G.STATES
local state_names = {}
for name, value in pairs(STATES) do state_names[value] = name end

local function log(kind, ...)
    io.write(kind, ': ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

---------------------------------------------------------------- measurement
local instructions, cpu = harness_native.instructions, harness_native.cpu

local round = { frames = 0, instructions = 0, instructions_max = 0, cpu = 0, cpu_max = 0, draw_instructions = 0,
    draws = 0, draws_max = 0, saves = 0, save_instructions_max = 0, save_cpu_max = 0 }

local function reset_round()
    for key in pairs(round) do round[key] = 0 end
end

-- The save worker runs inside push (runtime/lua/love_thread.lua), so this measures the
-- whole save as the console pays for it: STR_PACK, compress, and the file write.
local compressed_bytes = 0
local native_compress = love.data.compress
rawset(love.data, 'compress', function(...)
    local result = native_compress(...)
    compressed_bytes = #result
    return result
end)

local native_get_channel = love.thread.getChannel
rawset(love.thread, 'getChannel', function(name)
    local channel = native_get_channel(name)
    if name ~= 'save_request' or channel.measured then return channel end
    channel.measured = true
    local native_push = channel.push
    channel.push = function(self, request)
        local start_instructions, start_cpu = instructions(), cpu()
        native_push(self, request)
        local spent_instructions, spent_cpu = instructions() - start_instructions, cpu() - start_cpu
        round.saves = round.saves + 1
        round.save_instructions_max = math.max(round.save_instructions_max, spent_instructions)
        round.save_cpu_max = math.max(round.save_cpu_max, spent_cpu)
        log('save', 'type', tostring(request and request.type), 'instructions_k', string.format('%d', spent_instructions / 1000),
            'cpu_ms', string.format('%.2f', spent_cpu * 1000), 'compressed_bytes', tostring(compressed_bytes))
    end
    return channel
end)

local frame_start_instructions, frame_start_cpu = instructions(), cpu()
local retained_history = {}

-- love.run looks up love.draw on every frame, so this wrapper measures the draw half.
local native_draw = love.draw
love.draw = function(...)
    local start = instructions()
    native_draw(...)
    round.draw_instructions = round.draw_instructions + instructions() - start
end

local function measure_frame()
    local now_instructions, now_cpu = instructions(), cpu()
    local spent_instructions, spent_cpu = now_instructions - frame_start_instructions, now_cpu - frame_start_cpu
    frame_start_instructions, frame_start_cpu = now_instructions, now_cpu
    local draws = love.graphics.drawCalls()
    round.frames = round.frames + 1
    round.instructions = round.instructions + spent_instructions
    round.instructions_max = math.max(round.instructions_max, spent_instructions)
    round.cpu = round.cpu + spent_cpu
    round.cpu_max = math.max(round.cpu_max, spent_cpu)
    round.draws = round.draws + draws
    round.draws_max = math.max(round.draws_max, draws)
end

-- Requests that nobody takes pile up in a channel's queue for the rest of the run.
local function queued_requests()
    local counts = {}
    for _, name in ipairs({ 'sound_request', 'load_channel', 'save_request', 'http_request', 'http_response' }) do
        counts[#counts + 1] = name .. '=' .. native_get_channel(name):getCount()
    end
    return table.concat(counts, ',')
end

-- "#t/entries/largest key" per list: a gap shows as #t below the largest key, and then
-- table.remove(t, k) does nothing for any k past #t.
local function list_shape(list)
    local entries, largest = 0, 0
    for key in pairs(list) do
        entries = entries + 1
        if type(key) == 'number' and key > largest then largest = key end
    end
    return #list .. '/' .. entries .. '/' .. largest
end

local function list_shapes()
    return 'moveables=' .. list_shape(G.MOVEABLES) .. ',stage=' .. list_shape(G.STAGE_OBJECTS[G.STAGE])
end

-- HARNESS_PROFILE=1 samples the running Lua function every 1000 VM instructions and prints
-- the top functions per round. It replaces the instruction counter's hook on the main
-- coroutine, so the instr_* fields are not meaningful in that mode.
local profile = os.getenv('HARNESS_PROFILE') == '1' and {} or nil
if profile then
    debug.sethook(function()
        local info = debug.getinfo(2, 'Sl')
        if info then
            local key = info.short_src .. ':' .. tostring(info.linedefined)
            profile[key] = (profile[key] or 0) + 1
        end
    end, '', 1000)
end

local function report_profile()
    local rows = {}
    for key, samples in pairs(profile) do rows[#rows + 1] = { key = key, samples = samples } end
    table.sort(rows, function(a, b) return a.samples > b.samples end)
    local limit = tonumber(os.getenv('HARNESS_PROFILE_ROWS') or '') or 12
    for index = 1, math.min(limit, #rows) do
        log('profile', string.format('%6d', rows[index].samples), rows[index].key)
    end
    for key in pairs(profile) do profile[key] = nil end
end

local function report_round(label)
    if round.frames == 0 then return end
    if profile then report_profile() end
    local heap_kb = collectgarbage('count')
    collectgarbage('collect')
    local retained_kb = collectgarbage('count')
    retained_history[#retained_history + 1] = retained_kb
    log('metric', 'at', label, 'ante', tostring(G.GAME.round_resets.ante), 'round', tostring(G.GAME.round),
        'jokers', tostring(G.jokers and #G.jokers.cards or 0), 'frames', tostring(round.frames),
        'instr_avg_k', string.format('%d', round.instructions / round.frames / 1000),
        'instr_max_k', string.format('%d', round.instructions_max / 1000),
        'draw_instr_avg_k', string.format('%d', round.draw_instructions / round.frames / 1000),
        'cpu_avg_ms', string.format('%.2f', round.cpu / round.frames * 1000),
        'cpu_max_ms', string.format('%.2f', round.cpu_max * 1000),
        'draws_avg', string.format('%d', round.draws / round.frames), 'draws_max', tostring(round.draws_max),
        'saves', tostring(round.saves), 'save_instr_max_k', string.format('%d', round.save_instructions_max / 1000),
        'save_cpu_max_ms', string.format('%.2f', round.save_cpu_max * 1000),
        'heap_kb', string.format('%d', heap_kb), 'retained_kb', string.format('%d', retained_kb),
        'queued', queued_requests(), 'lists', list_shapes())
    if census then
        local snapshot = census.take()
        if last_snapshot then
            for _, line in ipairs(census.growth(last_snapshot, snapshot, 'by_class', 8)) do log('census_class', line) end
            for _, line in ipairs(census.growth(last_snapshot, snapshot, 'by_root', 8)) do log('census_root', line) end
            for _, line in ipairs(census.growth(last_snapshot, snapshot, 'by_stage', 8)) do log('census_stage', line) end
        end
        for _, line in ipairs(census.old_members(snapshot, 2, 12)) do log('census_old', line) end
        last_snapshot = snapshot
    end
    reset_round()
    -- The collection above is not the game's work, so it stays out of the next frame.
    frame_start_instructions, frame_start_cpu = instructions(), cpu()
end

---------------------------------------------------------------- playing
local frame, wait_until, last_state = 0, 900, nil
local started, eval_waits, last_shop_round = false, 0, -1

-- HARNESS_MAX_GROWTH_KB sets the most the retained heap may grow per round, measured from
-- round 3 (after the first jokers are bought) to the end. Over it, the run exits with 1.
local function leak_check()
    local limit = tonumber(os.getenv('HARNESS_MAX_GROWTH_KB') or '')
    local first, last = retained_history[4], retained_history[#retained_history]
    if not limit or not first or #retained_history < 8 then return 0 end
    local per_round = (last - first) / (#retained_history - 4)
    local verdict = per_round <= limit and 'PASS' or 'FAIL'
    log('leak_check', verdict, string.format('growth_kb_per_round %.1f limit %d', per_round, limit))
    return verdict == 'PASS' and 0 or 1
end

local function finish(reason)
    report_round('end')
    log('driver', 'finished', reason, 'frame', tostring(frame))
    os.exit(leak_check())
end

local function buy_jokers()
    G.GAME.dollars = 1000
    if not G.shop_jokers then return false end
    for _, card in ipairs(G.shop_jokers.cards) do
        if #G.jokers.cards < G.jokers.config.card_limit and card.ability.set == 'Joker' then
            log('driver', 'buy', tostring(card.ability.name))
            G.FUNCS.buy_from_shop({ config = { ref_table = card, id = 'buy' }, UIBox = { states = {} } })
            return true
        end
    end
    return false
end

local function act()
    if G.STATE == STATES.BLIND_SELECT and G.blind_select_opts then
        local option = G.blind_select_opts[string.lower(G.GAME.blind_on_deck)]
        local button = option and option:get_UIE_by_ID('select_blind_button')
        if button then
            report_round('blind_select')
            G.FUNCS.select_blind(button)
            return 200
        end
    elseif G.STATE == STATES.SELECTING_HAND and G.hand and G.hand.cards[1] then
        G.GAME.blind.chips = 1
        for i = 1, math.min(5, #G.hand.cards) do G.hand:add_to_highlighted(G.hand.cards[i]) end
        G.FUNCS.play_cards_from_highlighted(nil)
        return 200
    elseif G.STATE == STATES.ROUND_EVAL and G.round_eval then
        eval_waits = eval_waits + 1
        if eval_waits >= 6 then
            eval_waits = 0
            G.FUNCS.cash_out({ config = {} })
            return 200
        end
    elseif G.STATE == STATES.SHOP and G.shop then
        if buy_jokers() then return 120 end
        if G.GAME.round_resets.ante > MAX_ANTE then finish('max_ante') end
        G.FUNCS.toggle_shop({ config = {} })
        return 200
    elseif G.STATE == STATES.GAME_OVER then
        finish('game_over')
    end
    return 60
end

---------------------------------------------------------------- pad input
-- HARNESS_INPUT=pad presses real pad buttons, so the game's own controller code runs, as in
-- tests/gameplay/pad_driver.lua. It buys nothing: shop cards are hard to reach by d-pad.
local held_button, held_until, shop_moves = nil, 0, 0

love_platform.read_pad = function(port, buttons)
    for name in pairs(buttons) do buttons[name] = nil end
    if port ~= 0 then return false end
    if held_button and frame < held_until then buttons[held_button] = true end
    return true, 128, 128, 128, 128
end

local function tap(button, gap)
    held_button, held_until = button, frame + 6
    return gap or 45
end

-- Presses `a` only on the wanted button. Anything else on screen is moved past with the d-pad.
local function press_or_move(wanted)
    local focused = G.CONTROLLER.focused.target
    local button = focused and focused.config and focused.config.button
    if button == wanted then return tap('a', 120) end
    return tap('dpdown', 45)
end

local function act_pad()
    if G.STATE == STATES.SHOP then
        if G.GAME.round_resets.ante > MAX_ANTE then finish('max_ante') end
        shop_moves = shop_moves + 1
        -- The d-pad path to Next Round is not proven; say so rather than stall the run.
        if shop_moves > 40 then
            shop_moves = 0
            log('driver', 'pad could not reach next round, calling it directly')
            G.FUNCS.toggle_shop({ config = {} })
            return 200
        end
        return press_or_move('toggle_shop')
    end
    shop_moves = 0
    if G.STATE == STATES.SELECTING_HAND then
        G.GAME.blind.chips = 1
        local focused = G.CONTROLLER.focused.target
        if #G.hand.highlighted > 0 then return tap('x', 200) end
        if focused and focused.area == G.hand then return tap('a') end
        return tap('dpright')
    elseif G.STATE == STATES.BLIND_SELECT then
        if G.CONTROLLER.focused.target and G.CONTROLLER.focused.target.config.button == 'select_blind' then
            report_round('blind_select')
        end
        return press_or_move('select_blind')
    elseif G.STATE == STATES.ROUND_EVAL then
        return press_or_move('cash_out')
    elseif G.STATE == STATES.GAME_OVER then
        finish('game_over')
    end
    return 30
end

local use_pad = os.getenv('HARNESS_INPUT') == 'pad'
if os.getenv('HARNESS_BUY') == '0' then buy_jokers = function() return false end end

function PLAY_DRIVER()
    frame = frame + 1
    measure_frame()
    if frame > FRAME_LIMIT then finish('frame_limit') end
    if frame == CALLGRIND_FROM then harness_native.callgrind(true) end
    if CALLGRIND_FROM and frame == CALLGRIND_FROM + CALLGRIND_FRAMES then
        harness_native.callgrind(false)
        finish('callgrind_window')
    end

    if G.STATE ~= last_state then
        last_state = G.STATE
        log('state', tostring(state_names[G.STATE]), 'frame', tostring(frame),
            'ante', tostring(G.GAME and G.GAME.round_resets and G.GAME.round_resets.ante))
    end

    -- Beating ante 8 opens the win overlay. Endless mode keeps the run going.
    if G.OVERLAY_MENU and G.GAME and G.GAME.won and not G.harness_endless then
        G.harness_endless = true
        log('driver', 'won, continuing in endless mode')
        G.FUNCS.exit_overlay_menu()
        wait_until = frame + 120
        return
    end

    if frame < wait_until then return end
    wait_until = frame + 60

    if not started then
        started = true
        G.SETTINGS.GAMESPEED = 4
        G.FUNCS.start_run(nil, { stake = 1, seed = SEED })
        wait_until = frame + 200
        return
    end

    if G.CONTROLLER.locked or G.STATE_COMPLETE == false then return end
    wait_until = frame + (use_pad and act_pad() or act())
end
