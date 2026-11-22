-- Counts what is alive, so two snapshots show which part of the heap grows.
--
-- by_class: live tables per game class (Card, Sprite, UIElement...), found by metatable.
-- by_root:  tables first reached through each key of G, then through other globals.
-- Only reads the heap; it changes nothing the game can see.
local census = {}

local function class_names()
    local names = {}
    for name, value in pairs(_G) do
        -- engine/object.lua: every class is its own __index and inherits `is`.
        if type(value) == 'table' and rawget(value, '__index') == value and type(value.is) == 'function' then
            names[value] = name
        end
    end
    return names
end

local function walk(root, label, seen, by_root, by_class, names)
    local stack = { root }
    while #stack > 0 do
        local value = table.remove(stack)
        local kind = type(value)
        if (kind == 'table' or kind == 'function' or kind == 'thread') and not seen[value] then
            seen[value] = true
            by_root[label] = (by_root[label] or 0) + 1
            if kind == 'table' then
                local meta = getmetatable(value)
                if meta then
                    local class = names[meta] or 'other'
                    by_class[class] = (by_class[class] or 0) + 1
                    stack[#stack + 1] = meta
                end
                for key, item in next, value do
                    stack[#stack + 1] = key
                    stack[#stack + 1] = item
                end
            elseif kind == 'function' then
                for index = 1, math.huge do
                    local name, upvalue = debug.getupvalue(value, index)
                    if not name then break end
                    stack[#stack + 1] = upvalue
                end
                stack[#stack + 1] = getfenv(value)
            end
        end
    end
end

function census.take()
    local seen, by_root, by_class = {}, {}, {}
    local names = class_names()
    seen[census] = true
    -- G first, key by key, so each subtree is charged to the key that holds it.
    seen[G] = true
    for key, value in pairs(G) do walk(value, 'G.' .. tostring(key), seen, by_root, by_class, names) end
    for key, value in pairs(_G) do walk(value, '_G.' .. tostring(key), seen, by_root, by_class, names) end
    walk(debug.getregistry(), 'registry', seen, by_root, by_class, names)

    -- Direct members of each stage list, by class: the objects the stage still holds.
    local by_stage, members = {}, {}
    for stage, objects in pairs(G.STAGE_OBJECTS or {}) do
        for _, object in pairs(objects) do
            local key = 'stage' .. tostring(stage) .. '.' .. (names[getmetatable(object)] or 'other')
            by_stage[key] = (by_stage[key] or 0) + 1
            members[object] = names[getmetatable(object)] or 'other'
            if rawget(object, 'REMOVED') then
                local copies = 0
                for _, other in pairs(objects) do if other == object then copies = copies + 1 end end
                local in_lists = {}
                for list_name, list in pairs(G.I) do
                    for _, other in pairs(list) do if other == object then in_lists[#in_lists + 1] = 'G.I.' .. list_name end end
                end
                for _, other in pairs(G.MOVEABLES) do if other == object then in_lists[#in_lists + 1] = 'G.MOVEABLES' end end
                local key = 'removed copies=' .. copies .. ' also_in=' .. table.concat(in_lists, '+')
                by_stage[key] = (by_stage[key] or 0) + 1
            end
        end
    end
    return { by_root = by_root, by_class = by_class, by_stage = by_stage, members = members }
end

-- What a stage object is: its id, what holds it, and the first button or text inside it.
local function describe(object, class)
    -- REMOVED means remove() ran, yet the object is still in the stage list.
    local parts = { class, rawget(object, 'REMOVED') and 'REMOVED' or 'live' }
    local config = rawget(object, 'config') or {}
    if config.id then parts[#parts + 1] = 'id=' .. tostring(config.id) end
    local parent = config.parent or (rawget(object, 'role') and object.role.major)
    if parent and rawget(parent, 'config') and parent.config.id then parts[#parts + 1] = 'parent=' .. tostring(parent.config.id) end
    if config.text then parts[#parts + 1] = 'text=' .. tostring(config.text) end
    if config.button then parts[#parts + 1] = 'button=' .. tostring(config.button) end
    local found = 0
    local function scan(node, depth)
        if found >= 3 or depth > 12 or type(node) ~= 'table' then return end
        local node_config = rawget(node, 'config')
        if type(node_config) == 'table' and (node_config.button or node_config.text or node_config.id) then
            found = found + 1
            parts[#parts + 1] = string.format('inner{%s %s %s}', tostring(node_config.id), tostring(node_config.button), tostring(node_config.text))
        end
        for _, child in ipairs(rawget(node, 'children') or {}) do scan(child, depth + 1) end
        if rawget(node, 'UIRoot') then scan(node.UIRoot, depth + 1) end
    end
    scan(object, 0)
    if rawget(object, 'T') then parts[#parts + 1] = string.format('at=%.1f,%.1f', object.T.x or 0, object.T.y or 0) end
    return table.concat(parts, ' ')
end

-- Stage objects first seen at least `min_age` snapshots ago, grouped by description. A
-- group that keeps growing from round to round is a leak; long-lived objects that belong
-- to the run (deck, jokers) stay at a fixed count.
local first_seen = setmetatable({}, { __mode = 'k' })
local snapshot_index = 0

function census.old_members(snapshot, min_age, limit)
    snapshot_index = snapshot_index + 1
    local groups = {}
    for object, class in pairs(snapshot.members) do
        first_seen[object] = first_seen[object] or snapshot_index
        if snapshot_index - first_seen[object] >= min_age then
            local key = describe(object, class):gsub(' at=[%-%d.,]+', ''):gsub('%$%d+', '$N')
            groups[key] = (groups[key] or 0) + 1
        end
    end
    local rows = {}
    for key, count in pairs(groups) do rows[#rows + 1] = { key = key, count = count } end
    table.sort(rows, function(a, b) return a.count > b.count end)
    local lines = {}
    for index = 1, math.min(limit, #rows) do lines[index] = rows[index].count .. ' x ' .. rows[index].key end
    return lines
end

-- Stage objects in `after` that were not there in `before`, described.
function census.new_members(before, after, limit)
    local lines = {}
    for object, class in pairs(after.members) do
        if not before.members[object] and class ~= 'UIElement' and #lines < limit then
            lines[#lines + 1] = describe(object, class)
        end
    end
    table.sort(lines)
    return lines
end

-- Lines for the entries that grew the most between two snapshots.
function census.growth(before, after, field, limit)
    local rows = {}
    for key, count in pairs(after[field]) do
        local grown = count - (before[field][key] or 0)
        if grown > 0 then rows[#rows + 1] = { key = key, grown = grown, count = count } end
    end
    table.sort(rows, function(a, b) return a.grown > b.grown end)
    local lines = {}
    for index = 1, math.min(limit, #rows) do
        lines[index] = string.format('%s +%d (now %d)', rows[index].key, rows[index].grown, rows[index].count)
    end
    return lines
end

return census
