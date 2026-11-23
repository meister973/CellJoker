-- Host stand-ins for the C modules love_graphics_native and love_audio_native. Each object
-- has the same methods as its PS3 type in runtime/src/love, and no more, so a call that
-- would fail on the console also fails here.
--
-- Draw calls are counted the way the PS3 renderer issues them: one per drawable, and one
-- per printed string.
-- The game seeds math.random with os.time() at boot (globals.lua), so two runs that start in
-- different seconds play different menus. The runner's clock reads HARNESS_TIME instead.
local wall_time = os.time
local START_TIME = tonumber(os.getenv('HARNESS_TIME') or '') or 1700000000
os.time = function(date)
    if date then return wall_time(date) end
    return START_TIME
end

local SCREEN_WIDTH, SCREEN_HEIGHT = 1280, 720

local function read_png_size(path)
    local file = io.open(path, 'rb')
    if not file then return nil end
    local header = file:read(24)
    file:close()
    if not header or #header < 24 then return nil end
    local function be32(at)
        local a, b, c, d = header:byte(at, at + 3)
        return ((a * 256 + b) * 256 + c) * 256 + d
    end
    return be32(17), be32(21)
end

-- A missing key reads as nil, as it does on the PS3 userdata, because the game probes
-- every table in G with `v.is`.
local function make_type(methods)
    local meta = {}
    for name, method in pairs(methods) do
        if name:sub(1, 2) == '__' then meta[name] = method end
    end
    return function(fields)
        local object = setmetatable(fields, meta)
        for name, method in pairs(methods) do
            if name:sub(1, 2) ~= '__' then rawset(object, name, method) end
        end
        return object
    end
end

local function nothing() end

local new_texture = make_type({
    getWidth = function(self) return self.width end,
    getHeight = function(self) return self.height end,
    getDimensions = function(self) return self.width, self.height end,
    getPixelWidth = function(self) return self.pixel_width end,
    getPixelHeight = function(self) return self.pixel_height end,
    setFilter = nothing,
    setWrap = nothing,
})

local new_quad = make_type({ setViewport = nothing })

local new_font = make_type({
    getWidth = function(self, text)
        local _, characters = tostring(text):gsub('[^\128-\191]', '')
        return characters * self.size * 0.55
    end,
    getHeight = function(self) return self.size * 1.2 end,
    setFilter = nothing,
    setLineHeight = nothing,
})

local new_text = make_type({ set = nothing })
local new_shader = make_type({ send = nothing })

local draw_calls, draw_calls_last_frame = 0, 0
local function count_draw() draw_calls = draw_calls + 1 end

local image_cache = {}
local font_cache = {}
local default_font

local function open_font(path, size)
    local key = tostring(path) .. '@' .. tostring(size)
    font_cache[key] = font_cache[key] or new_font({ size = size or 12 })
    return font_cache[key]
end

love_graphics_native = {
    debugDumpScreen = function() return false end,
    push = nothing, pop = nothing, origin = nothing, reset = nothing,
    translate = nothing, scale = nothing, rotate = nothing,
    setColor = nothing, clear = nothing, setLineWidth = nothing, setLineStyle = nothing,
    rectangle = count_draw, polygon = count_draw, draw = count_draw, print = count_draw,
    present = function()
        draw_calls_last_frame, draw_calls = draw_calls, 0
        harness_native.tick(1 / 60)
    end,
    getWidth = function() return SCREEN_WIDTH end,
    getHeight = function() return SCREEN_HEIGHT end,
    getDimensions = function() return SCREEN_WIDTH, SCREEN_HEIGHT end,
    isActive = function() return true end,
    isCreated = function() return true end,
    discard = nothing,
    drawCalls = function() return draw_calls_last_frame end,

    newImage = function(path, options)
        local dpi_scale = (type(options) == 'table' and options.dpiscale) or 1
        local key = path .. '@' .. dpi_scale
        if image_cache[key] then return image_cache[key] end
        local pixel_width, pixel_height = read_png_size(INSTALL_ROOT .. path)
        if not pixel_width then error('love.graphics.newImage: cannot read ' .. path, 2) end
        image_cache[key] = new_texture({ pixel_width = pixel_width, pixel_height = pixel_height,
            width = pixel_width / dpi_scale, height = pixel_height / dpi_scale })
        return image_cache[key]
    end,
    newCanvas = function(width, height)
        width, height = width or SCREEN_WIDTH, height or SCREEN_HEIGHT
        return new_texture({ pixel_width = width, pixel_height = height, width = width, height = height })
    end,
    newQuad = function() return new_quad({}) end,
    newFont = function(path, size)
        if type(path) == 'number' then return open_font('default', path) end
        return open_font(path, size)
    end,
    setNewFont = function(path, size)
        default_font = love_graphics_native.newFont(path, size)
        return default_font
    end,
    setFont = function(font) default_font = font end,
    newText = function() return new_text({}) end,
    newShader = function() return new_shader({}) end,
    setShader = nothing,
    setCanvas = nothing,
    setDefaultFilter = nothing,
}

-- HARNESS_DRAW_LOG=<file> writes every graphics call of one frame in HARNESS_DRAW_EVERY (default
-- 30), with its arguments, and the elements in G.DRAW_HASH at the end of that frame. A draw
-- function in C must give the same file as the game's Lua version (HARNESS_LUA_GAME=1).
local draw_log_path = os.getenv('HARNESS_DRAW_LOG')
if draw_log_path then
    local every = tonumber(os.getenv('HARNESS_DRAW_EVERY') or '') or 30
    local file = assert(io.open(draw_log_path, 'w'))
    local frame, recording = 0, false
    -- Objects are named by the order in which the log first meets them, the same in two runs
    -- that make the same calls, where addresses are not.
    local names, next_name = setmetatable({}, { __mode = 'k' }), 0
    local LOGGED = { 'push', 'pop', 'origin', 'translate', 'scale', 'rotate', 'setColor', 'setLineWidth',
        'setLineStyle', 'rectangle', 'polygon', 'draw', 'print', 'setShader', 'setCanvas', 'clear' }

    local function text(value)
        if type(value) == 'number' then return string.format('%.17g', value) end
        return tostring(value)
    end

    local function describe(value)
        if type(value) ~= 'table' then return text(value) end
        if names[value] then return '#' .. names[value] end
        if type(value[1]) == 'number' then
            local parts = {}
            for i = 1, #value do parts[i] = text(value[i]) end
            return '{' .. table.concat(parts, ',') .. '}'
        end
        next_name = next_name + 1
        names[value] = next_name
        return '#' .. next_name
    end

    local function logged(name, call)
        return function(...)
            if recording then
                local parts = { name }
                for i = 1, select('#', ...) do parts[#parts + 1] = describe((select(i, ...))) end
                file:write(table.concat(parts, ' '), '\n')
            end
            return call(...)
        end
    end

    for _, name in ipairs(LOGGED) do
        love_graphics_native[name] = logged(name, love_graphics_native[name])
    end

    local present = love_graphics_native.present
    love_graphics_native.present = function()
        frame = frame + 1
        if recording then
            local hash = {}
            for i, element in ipairs(G and G.DRAW_HASH or {}) do hash[i] = describe(element) end
            file:write('present ', frame, ' draw_hash ', table.concat(hash, ','), '\n')
            file:flush()
        end
        recording = (frame + 1) % every == 0
        present()
    end
end

---------------------------------------------------------------- audio
-- A static sound ends after STATIC_SECONDS of simulated time, as a real voice ends, because
-- the game's sound worker frees sources only once isPlaying() is false. A stream (music,
-- ambient) loops on the PS3, so it plays until stopped.
local STATIC_SECONDS = 1

local function start(source)
    source.playing = true
    source.ends_at = love_platform.time() + STATIC_SECONDS
end

local new_source = make_type({
    play = function(self) start(self); return true end,
    stop = function(self) self.playing = false end,
    setVolume = nothing,
    setPitch = nothing,
    isPlaying = function(self)
        if self.playing and not self.stream and love_platform.time() >= self.ends_at then self.playing = false end
        return self.playing
    end,
    release = function(self) self.playing = false end,
})

love_audio_native = {
    underruns = function() return 0 end,
    newSource = function(_, kind) return new_source({ playing = false, stream = kind == 'stream' }) end,
    play = function(source) if source then start(source) end end,
    stop = function(source) if source then source.playing = false end end,
}
