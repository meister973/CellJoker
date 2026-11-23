-- Checks love_graphics_native.prepDraw against the game's own prep_draw, case by case. Both
-- must build the same transform, bit for bit, and push exactly one level on the stack.
local graphics = love_graphics_native
local CASES = 5000
local PROBE_POINTS = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 3.5, -2.25 } }

-- The function from the installed game, so no copy of game code lives in this repository.
local function load_game_prep_draw()
    local file = assert(io.open(INSTALL_ROOT .. 'functions/misc_functions.lua', 'r'))
    local source = file:read('*a')
    file:close()
    local first = assert(source:find('function prep_draw(', 1, true), 'no prep_draw in the game')
    local last = assert(source:find('\nend', first, true), 'no end to prep_draw')
    assert(loadstring(source:sub(first, last + 3)))()
    return prep_draw
end

local function maybe(chance, value)
    if math.random() < chance then return value end
    return nil
end

local function number(low, high)
    return low + (high - low) * math.random()
end

local function random_parallax(chance_per_axis)
    return { x = maybe(chance_per_axis, number(-0.2, 0.2)), y = maybe(chance_per_axis, number(-0.2, 0.2)) }
end

-- Every branch the game's version has: rotation or not, juice, own and parent parallax,
-- offsets with missing axes, and rotate given as nil, false, 0 or an angle.
local function random_case()
    local moveable = {
        VT = { x = number(-5, 30), y = number(-5, 20), w = number(0.1, 6), h = number(0.1, 8),
            r = maybe(0.5, number(-0.3, 0.3)) or 0, scale = number(0.5, 1.5) },
        juice = maybe(0.3, { scale = 0 }),
        layered_parallax = maybe(0.4, random_parallax(0.8)),
        parent = maybe(0.5, { layered_parallax = maybe(0.6, random_parallax(1)) }),
    }
    local scale = ({ 1, 0.97, 0.98, number(0.8, 1.4) })[math.random(4)]
    local rotate = ({ nil, false, 0, number(-0.5, 0.5) })[math.random(4)]
    local offset = maybe(0.3, { x = maybe(0.8, number(-1, 1)), y = maybe(0.8, number(-1, 1)) })
    return moveable, scale, rotate, offset
end

-- The transform that `prep` leaves, read at a few points, and whether one pop restored the origin.
local function transform_after(prep, moveable, scale, rotate, offset)
    graphics.origin()
    prep(moveable, scale, rotate, offset)
    local seen = {}
    for _, point in ipairs(PROBE_POINTS) do
        local x, y = graphics.transformPoint(point[1], point[2])
        seen[#seen + 1] = x
        seen[#seen + 1] = y
    end
    graphics.pop()
    local x, y = graphics.transformPoint(5, 7)
    return seen, x == 5 and y == 7
end

local function seconds_for(prep, moveable)
    local start = love_platform.time()
    for _ = 1, 20000 do
        prep(moveable, 1)
        graphics.pop()
    end
    return love_platform.time() - start
end

G = { TILESCALE = 1.7, TILESIZE = 20 }
math.randomseed(1234)
local game_prep_draw = load_game_prep_draw()

local mismatches, unbalanced = 0, 0
for case = 1, CASES do
    local moveable, scale, rotate, offset = random_case()
    local want = transform_after(game_prep_draw, moveable, scale, rotate, offset)
    local got, balanced = transform_after(graphics.prepDraw, moveable, scale, rotate, offset)
    if not balanced then unbalanced = unbalanced + 1 end
    for index = 1, #want do
        if want[index] ~= got[index] then
            mismatches = mismatches + 1
            if mismatches <= 5 then
                io.write(string.format('prep_draw: case %d value %d want %.9g got %.9g\n', case, index, want[index], got[index]))
            end
            break
        end
    end
end

-- A sprite as most of a frame draws it: no rotation, no juice, no parallax.
local plain = { VT = { x = 3, y = 4, w = 2, h = 3, r = 0, scale = 1 } }
io.write(string.format('prep_draw: lua_ms=%.1f c_ms=%.1f for 20000 calls\n',
    seconds_for(game_prep_draw, plain) * 1000, seconds_for(graphics.prepDraw, plain) * 1000))

-- Last, because a failed call leaves its push behind: both must refuse a moveable with no VT.
graphics.origin()
local errors_match = not pcall(game_prep_draw, {}, 1) and not pcall(graphics.prepDraw, {}, 1)

io.write(string.format('prep_draw: cases=%d mismatches=%d unbalanced=%d errors_match=%s\n',
    CASES, mismatches, unbalanced, tostring(errors_match)))
io.write('probe: done\n')
io.flush()
