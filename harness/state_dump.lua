-- HARNESS_STATE_DUMP=<file> writes the transforms of every Moveable every HARNESS_STATE_EVERY
-- frames (default 30). An engine function in C that changes objects cannot run next to its Lua
-- version on one call, so two whole runs are compared instead: the file of a run with
-- HARNESS_LUA_GAME=1 and the file of a run with the C versions must be the same, byte for byte.
local path = os.getenv('HARNESS_STATE_DUMP')
if not path then return function() end end

local every = tonumber(os.getenv('HARNESS_STATE_EVERY') or '') or 30
local file = assert(io.open(path, 'w'))
local TRANSFORM_KEYS = { 'x', 'y', 'w', 'h', 'r', 'scale' }
local FLAG_KEYS = { 'STATIONARY', 'CALCING', 'NEW_ALIGNMENT' }

-- %.17g writes every bit of a double, where tostring keeps 14 digits.
local function text(value)
    if type(value) == 'number' then return string.format('%.17g', value) end
    return tostring(value)
end

local function put_fields(out, tab, keys)
    if type(tab) ~= 'table' then
        out[#out + 1] = tostring(tab)
        return
    end
    for _, key in ipairs(keys) do out[#out + 1] = text(tab[key]) end
end

local function put_moveable(out, index, moveable)
    out[#out + 1] = '\n' .. index
    put_fields(out, moveable.T, TRANSFORM_KEYS)
    put_fields(out, moveable.VT, TRANSFORM_KEYS)
    put_fields(out, moveable.velocity, TRANSFORM_KEYS)
    put_fields(out, moveable.shadow_parrallax, { 'x' })
    put_fields(out, moveable.role and moveable.role.offset, { 'x', 'y' })
    put_fields(out, moveable, FLAG_KEYS)
end

return function(frame)
    if frame % every ~= 0 then return end
    local out = { 'frame', tostring(frame) }
    for index, moveable in ipairs(G.MOVEABLES) do put_moveable(out, index, moveable) end
    out[#out + 1] = '\n'
    file:write(table.concat(out, ' '))
    file:flush()
end
