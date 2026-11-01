-- Second half of the save test: a fresh boot, with the first boot's save folder still there.
local frame, next_action = 0, 900
local loaded = false

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

local function read_expected()
    local expected = {}
    local contents = assert(love.filesystem.read('test_expected.txt'), 'no expected values: run save_write_driver first')
    for key, value in contents:gmatch('(%w+)=([^\n]*)') do expected[key] = value end
    return expected
end

local function check(label, actual, wanted)
    local matches = tostring(actual) == wanted
    log(matches and 'ok  ' or 'FAIL', label, 'want', wanted, 'got', tostring(actual))
    return matches
end

function PLAY_DRIVER()
    frame = frame + 1
    if frame < next_action then return end

    if not loaded then
        loaded = true
        log('tutorial_complete after reboot', tostring(G.SETTINGS.tutorial_complete))
        local packed = get_compressed(G.SETTINGS.profile .. '/save.jkr')
        assert(packed, 'no saved run after reboot')
        G.SAVED_GAME = STR_UNPACK(packed)
        G.FUNCS.start_run(nil, { savetext = G.SAVED_GAME })
        next_action = frame + 300
        return
    end

    local expected = read_expected()
    local game = G.GAME
    local good = true
    good = check('round', game.round, expected.round) and good
    good = check('dollars', game.dollars, expected.dollars) and good
    good = check('seed', game.pseudorandom.seed, expected.seed) and good
    good = check('jokers', #G.jokers.cards, expected.jokers) and good
    good = check('joker1', G.jokers.cards[1] and G.jokers.cards[1].ability.name, expected.joker1) and good
    log('state', tostring(G.STATE))
    if good then log('driver finished') end
    PLAY_DRIVER = function() end
end
