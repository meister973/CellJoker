-- With no save at all, the Play button opens the run setup and a new run is not the tutorial.
local frame, next_check = 0, 900

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

local function find_play_button()
    return G.MAIN_MENU_UI and G.MAIN_MENU_UI:get_UIE_by_ID('main_menu_play')
end

-- start_run takes effect some frames later, and until then the seed is nil.
local function check_the_new_run()
    local seed = G.GAME.pseudorandom.seed
    if not seed then return end

    log('new run seed', seed)
    if seed == 'TUTORIAL' then error('a new run is still the tutorial') end
    log('driver finished')
    PLAY_DRIVER = function() end
end

function PLAY_DRIVER()
    frame = frame + 1
    if frame < next_check then return end
    next_check = frame + 60

    local button = find_play_button()
    if not button then return end

    log('play button opens', tostring(button.config.button))
    if button.config.button ~= 'setup_run' then error('the Play button still starts the tutorial') end

    G.FUNCS.start_run(nil, { stake = 1 })
    PLAY_DRIVER = check_the_new_run
end
