-- Puts the C versions of hot engine functions (runtime/src/lua/game_native.c,
-- moveable_native.c, ui_native.c and text_native.c) in place of the game's Lua ones. On the host
-- runner, HARNESS_LUA_GAME=1 keeps the Lua versions, and HARNESS_COMPARE_GAME=1 runs both on every
-- call and stops the run at the first difference.
local COMPARE_REPORT_EVERY = 100000

local function compared(name, lua_version, native_version)
    local calls = 0
    return function(...)
        local want = lua_version(...)
        local got = native_version(...)
        if want ~= got then
            error(string.format('native %s gave %s where Lua gave %s', name, tostring(got), tostring(want)), 2)
        end
        calls = calls + 1
        if calls % COMPARE_REPORT_EVERY == 0 then
            io.write('native_compare: ', name, ' calls=', calls, '\n')
            io.flush()
        end
        return want
    end
end

local function choose(name, lua_version, native_version)
    if os.getenv('HARNESS_LUA_GAME') == '1' then return lua_version end
    if os.getenv('HARNESS_COMPARE_GAME') == '1' then return compared(name, lua_version, native_version) end
    return native_version
end

return function()
    Node.collides_with_point = choose('collides_with_point', Node.collides_with_point,
        game_native.collides_with_point)
    -- move changes the objects it moves, so its two versions cannot run on one call. The host
    -- runner's HARNESS_STATE_DUMP compares a whole run of each instead (harness/state_dump.lua).
    -- draw_self and DynaText.draw are checked the same way, with HARNESS_DRAW_LOG.
    if os.getenv('HARNESS_LUA_GAME') ~= '1' then
        Moveable.move = game_native.move
        Moveable.glue_to_major = game_native.glue_to_major
        UIElement.draw_self = game_native.ui_draw_self(UIElement.draw_self)
        DynaText.draw = game_native.text_draw(DynaText.draw)
    end
end
