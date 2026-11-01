-- A module table that names any missing member, so a gap in the port surfaces as
-- "love stub: missing love.x.y" and not as an index-nil crash somewhere in the game.
return function(module_name, members)
    return setmetatable(members, {
        __index = function(_, key)
            error('love stub: missing love.' .. module_name .. '.' .. tostring(key), 2)
        end,
    })
end
