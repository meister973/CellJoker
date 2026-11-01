-- The main profile starts unlocked, as the game's own "Unlock All" profile button does.
-- The other profiles stay as the game made them, for a player who wants to start fresh.
local MAIN_PROFILE = 1

local function unlock_table(items)
    for _, item in pairs(items) do
        if not item.demo and not item.wip then
            item.alerted = true
            item.discovered = true
            item.unlocked = true
        end
    end
end

return function()
    if G.SETTINGS.profile ~= MAIN_PROFILE then return end

    G.PROFILES[G.SETTINGS.profile].all_unlocked = true
    unlock_table(G.P_CENTERS)
    unlock_table(G.P_BLINDS)
    unlock_table(G.P_TAGS)
    -- check_for_unlock walks this list, and would announce each item as newly unlocked.
    G.P_LOCKED = {}
    set_profile_progress()
    set_discover_tallies()
end
