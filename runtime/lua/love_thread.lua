-- love.thread on one Lua state. Each worker is a coroutine with its own globals, which runs when a
-- request arrives. The game's save and sound workers are loops around Channel:demand(), so this
-- runs them as shipped. A request is served before push returns, which also gives what LÖVE gets
-- by copying a pushed table: the worker sees the values as they were at the push.
--
-- load_chunk(path) returns the worker's code as a function.
return function(load_chunk)
    local module = {}
    local channels = {}

    local function resume_worker(worker)
        worker.waiting_on = nil
        local ok, message = coroutine.resume(worker.routine)
        if not ok then
            error(debug.traceback(worker.routine, tostring(message)), 0)
        end
    end

    local function new_channel()
        local channel = { queue = {}, waiter = nil }

        function channel:push(value)
            self.queue[#self.queue + 1] = value
            local waiter = self.waiter
            if waiter and coroutine.running() ~= waiter.routine then
                self.waiter = nil
                resume_worker(waiter)
            end
        end

        function channel:pop()
            return table.remove(self.queue, 1)
        end

        function channel:peek()
            return self.queue[1]
        end

        function channel:getCount()
            return #self.queue
        end

        function channel:clear()
            self.queue = {}
        end

        -- Inside a worker, this hands control back until a request arrives. The main thread has
        -- nothing to wait for, because every worker has already served what it was sent.
        function channel:demand()
            local routine = coroutine.running()
            if not routine then return self:pop() end

            while #self.queue == 0 do
                self.waiter = module.worker_of(routine)
                coroutine.yield()
            end
            return self:pop()
        end

        return channel
    end

    local workers = {}

    function module.worker_of(routine)
        return workers[routine]
    end

    function module.getChannel(name)
        channels[name] = channels[name] or new_channel()
        return channels[name]
    end

    function module.newThread(path)
        local thread = { running = false }

        function thread:start(...)
            local chunk = load_chunk(path)
            -- Both workers set globals such as CHANNEL, so each needs its own.
            setfenv(chunk, setmetatable({}, { __index = _G }))

            local routine = coroutine.create(chunk)
            local worker = { routine = routine }
            workers[routine] = worker
            self.running = true

            local ok, message = coroutine.resume(routine, ...)
            if not ok then
                error(debug.traceback(routine, tostring(message)), 0)
            end
        end

        function thread:isRunning() return self.running end
        function thread:getError() return nil end
        function thread:wait() end

        return thread
    end

    return module
end
