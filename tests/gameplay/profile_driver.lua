-- The main profile starts with everything unlocked. Another profile starts fresh.
local frame, next_check = 0, 900

local function log(...)
    io.write('play: ', table.concat({ ... }, ' '), '\n')
    io.flush()
end

local function menu_is_ready()
    return G.MAIN_MENU_UI and G.STATE == G.STATES.MENU
end

local function describe_profile()
    log('profile', G.SETTINGS.profile, 'all_unlocked', tostring(G.PROFILES[G.SETTINGS.profile].all_unlocked),
        'blueprint unlocked', tostring(G.P_CENTERS.j_blueprint.unlocked))
end

local function switch_to(profile, after)
    G.focused_profile = profile
    G.MAIN_MENU_UI:remove()
    G.MAIN_MENU_UI = nil
    G.FUNCS.load_profile()
    PLAY_DRIVER = function()
        frame = frame + 1
        if frame < next_check then return end
        next_check = frame + 60
        if G.SETTINGS.profile == profile and menu_is_ready() then after() end
    end
end

local function check_main_profile_again()
    describe_profile()
    if not G.PROFILES[1].all_unlocked or not G.P_CENTERS.j_blueprint.unlocked then
        error('the main profile lost its unlocks')
    end
    log('driver finished')
    PLAY_DRIVER = function() end
end

local function check_fresh_profile()
    describe_profile()
    if G.PROFILES[2].all_unlocked or G.P_CENTERS.j_blueprint.unlocked then
        error('profile 2 did not start fresh')
    end
    switch_to(1, check_main_profile_again)
end

function PLAY_DRIVER()
    frame = frame + 1
    if frame < next_check then return end
    next_check = frame + 60
    if not menu_is_ready() then return end

    describe_profile()
    if not G.PROFILES[1].all_unlocked or not G.P_CENTERS.j_blueprint.unlocked then
        error('the main profile is not unlocked')
    end
    switch_to(2, check_fresh_profile)
end
