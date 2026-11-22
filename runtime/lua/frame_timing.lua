-- Where a frame's time goes, for the log a tester sends back. Each wrapped function adds its
-- own run time to its part. A report gives averages per frame since the last report, with the
-- parts in the order given.
--
-- clock() returns seconds. parts lists the names that wrap() accepts.
return function(clock, parts)
    local module = {}
    local spent, frames, frame_total, worst
    local frame_start = clock()

    local function reset()
        spent = {}
        for _, name in ipairs(parts) do spent[name] = 0 end
        frames, frame_total, worst = 0, 0, 0
    end

    local function finish(name, started, ...)
        spent[name] = spent[name] + (clock() - started)
        return ...
    end

    function module.wrap(name, run)
        assert(spent[name], 'not a timed part: ' .. tostring(name))
        return function(...)
            return finish(name, clock(), run(...))
        end
    end

    function module.end_frame()
        local now = clock()
        local length = now - frame_start
        frame_start = now
        frames = frames + 1
        frame_total = frame_total + length
        if length > worst then worst = length end
    end

    local function milliseconds(seconds)
        return string.format('%.1f', seconds * 1000)
    end

    function module.report()
        if frames == 0 then return 'frame_ms=none' end
        local fields = {
            'frame_ms=' .. milliseconds(frame_total / frames),
            'worst_ms=' .. milliseconds(worst),
        }
        for _, name in ipairs(parts) do
            fields[#fields + 1] = name .. '_ms=' .. milliseconds(spent[name] / frames)
        end
        reset()
        return table.concat(fields, ' ')
    end

    reset()
    return module
end
