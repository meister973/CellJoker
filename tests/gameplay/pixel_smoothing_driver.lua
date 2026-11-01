-- Toggles Pixel Smoothing as the Options menu does. Each toggle reloads every atlas, which
-- must not use up RSX memory, however many times a player flips it.
local frame = 0
local toggles = 0

function PLAY_DRIVER()
    frame = frame + 1
    if frame < 900 or frame % 30 ~= 0 then return end

    toggles = toggles + 1
    if toggles > 8 then
        io.write('play: 8 toggles survived, atlas width ', tostring(G.ASSET_ATLAS.Joker.image:getWidth()), '\n')
        io.write('play: driver finished\n')
        io.flush()
        PLAY_DRIVER = function() end
        return
    end

    local scaling = toggles % 2 == 1 and 1 or 2
    io.write('play: toggle ', toggles, ' to scaling ', scaling, '\n')
    io.flush()
    G.FUNCS.change_pixel_smoothing({ to_key = scaling })
end
