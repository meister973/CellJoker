-- Runs under plain Lua 5.1 in the toolchain image: `lua5.1 tests/joystick/test_love_thread.lua`.
package.path = 'runtime/lua/?.lua;' .. package.path
local new_thread_module = require 'love_thread'

local failures, passed = 0, 0

local function equal(actual, expected, label)
    if actual ~= expected then
        error(string.format('%s: want %s got %s', label, tostring(expected), tostring(actual)), 2)
    end
end

local function test(name, body)
    local ok, message = pcall(body)
    if ok then
        passed = passed + 1
    else
        failures = failures + 1
        print('FAIL ' .. name .. ': ' .. tostring(message))
    end
end

-- Scripts by name, as the worker files would be on disk.
local function module_with(scripts)
    return new_thread_module(function(path)
        return assert(loadstring(assert(scripts[path], 'no script ' .. path)))
    end)
end

local WORKER = [[
    CHANNEL = love_thread_under_test.getChannel('requests')
    while true do
        local request = CHANNEL:demand()
        results[#results + 1] = request.value
    end
]]

test('a channel pops what was pushed, in order', function()
    local module = module_with({})
    local channel = module.getChannel('x')
    channel:push(1)
    channel:push(2)
    equal(channel:pop(), 1, 'first')
    equal(channel:pop(), 2, 'second')
    equal(channel:pop(), nil, 'empty')
end)

test('the same name gives the same channel', function()
    local module = module_with({})
    equal(module.getChannel('x'), module.getChannel('x'), 'channel')
end)

test('a request is served before push returns, so a snapshot is not needed', function()
    local module = module_with({ worker = WORKER })
    love_thread_under_test = module
    results = {}
    module.newThread('worker'):start()
    local request = { value = 'first' }
    module.getChannel('requests'):push(request)
    equal(results[1], 'first', 'served')
    request.value = 'changed later'
    equal(results[1], 'first', 'not affected')
end)

test('requests pushed before start are served once it runs', function()
    local module = module_with({ worker = WORKER })
    love_thread_under_test = module
    results = {}
    module.getChannel('requests'):push({ value = 'early' })
    module.newThread('worker'):start()
    equal(results[1], 'early', 'served')
end)

test('two workers keep separate globals', function()
    local module = module_with({
        a = "NAME = 'a'; seen_a = function() return NAME end; shared.a = seen_a",
        b = "NAME = 'b'; seen_b = function() return NAME end; shared.b = seen_b",
    })
    shared = {}
    module.newThread('a'):start()
    module.newThread('b'):start()
    equal(shared.a(), 'a', 'a')
    equal(shared.b(), 'b', 'b')
    equal(rawget(_G, 'NAME'), nil, 'global')
end)

test('a worker sees the real globals', function()
    local module = module_with({ reader = "shared.value = outside_value" })
    shared, outside_value = {}, 42
    module.newThread('reader'):start()
    equal(shared.value, 42, 'value')
end)

test('a worker error reaches the caller, instead of vanishing', function()
    local module = module_with({ broken = "error('worker broke')" })
    local ok, message = pcall(function() module.newThread('broken'):start() end)
    equal(ok, false, 'ok')
    assert(tostring(message):find('worker broke'), tostring(message))
end)

test('a worker that pushes to its own channel does not recurse', function()
    local module = module_with({ worker = [[
        local channel = love_thread_under_test.getChannel('requests')
        while true do
            local request = channel:demand()
            shared.count = shared.count + 1
            if request == 'first' then channel:push('done') end
        end
    ]] })
    love_thread_under_test = module
    shared = { count = 0 }
    module.newThread('worker'):start()
    module.getChannel('requests'):push('first')
    equal(shared.count, 2, 'count')
end)

test('demand outside a worker returns what is queued, and never blocks', function()
    local module = module_with({})
    local channel = module.getChannel('x')
    equal(channel:demand(), nil, 'empty')
    channel:push(5)
    equal(channel:demand(), 5, 'value')
end)

print(string.format('thread tests: passed=%d failures=%d', passed, failures))
os.exit(failures == 0 and 0 or 1)
