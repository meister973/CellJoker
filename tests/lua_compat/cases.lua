-- Behaviour the shipped game expects from LuaJIT. Run under real LuaJIT to make the
-- expected output, and on the Cell to make the actual output; the two must match.
local out = {}

local function record(label, value)
    if type(value) == 'number' then
        out[#out + 1] = label .. ' ' .. string.format('%.17g', value)
    else
        out[#out + 1] = label .. ' ' .. tostring(value)
    end
end

for _, seed in ipairs({0.1, 0.5, 0.9, 0.123456, 1.5, 1.9, 12345.678}) do
    math.randomseed(seed)
    for draw = 1, 5 do record('random ' .. seed .. ' #' .. draw, math.random()) end
    for draw = 1, 5 do record('random10 ' .. seed .. ' #' .. draw, math.random(10)) end
    for draw = 1, 5 do record('random52 ' .. seed .. ' #' .. draw, math.random(1, 52)) end
end

record('log 8 2', math.log(8, 2))
record('log 100 10', math.log(100, 10))
record('log 5', math.log(5))
record('log 0.001 5', math.log(0.001, 5))
for power = 1, 15 do
    record('floorlog10 ' .. power, math.floor(math.log(10 ^ power, 10)))
end

record('tobit 2^31+5', bit.tobit(2 ^ 31 + 5))
record('tobit ffffffff', bit.tobit(0xFFFFFFFF))
record('tobit 3.5', bit.tobit(3.5))
record('tobit 2.5', bit.tobit(2.5))
record('tobit -1', bit.tobit(-1))
record('band', bit.band(0xFFFFFFFF, 0xFF))
record('band high', bit.band(0xF0000000, 0xFF000000))
record('bor high', bit.bor(0x80000000, 1))
record('bxor high', bit.bxor(0, 3000000000))
record('bnot high', bit.bnot(0xFFFFFFFF))
record('lshift', bit.lshift(3000000000, 1))
record('rshift', bit.rshift(3000000000, 1))
record('arshift', bit.arshift(3000000000, 1))
record('rol', bit.rol(0x80000001, 1))
record('ror', bit.ror(0x80000001, 1))
record('bswap', bit.bswap(0x80000001))
record('tohex upper', bit.tohex(-1, -4))
record('tohex lower', bit.tohex(0xABCDEF, 8))

record('format nil', string.format('%s', nil))
record('format true', string.format('%s', true))
record('format mixed', string.format('%s %d %s', 1, 2, nil))
record('format percent', string.format('%%%s', false))
record('format table ok', (pcall(string.format, '%s', {})))
record('format padded', string.format('%-5s|', 'ab'))
record('format huge', string.format('%d', 3000000000))

return out
