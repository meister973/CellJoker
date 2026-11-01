-- Sends a distinct value to every interpolator slot, then draws them as screen bands.
local graphics = love_graphics_native
local shader = graphics.newShader('slots.fs')

for slot = 0, 5 do
    local base = (slot + 1) / 10
    shader:send('s' .. slot, { base, base + 0.02, base + 0.04, base + 0.06 })
end

for frame = 1, 40 do
    graphics.clear(0, 0, 0, 1)
    graphics.setColor(1, 1, 1, 1)
    graphics.setShader(shader)
    graphics.rectangle('fill', 0, 0, 1280, 720)
    graphics.setShader()
    if frame == 30 then
        io.write('dump ', tostring(graphics.debugDumpScreen('/dev_hdd0/tmp/frame_30.ppm')), '\n')
        io.flush()
    end
    graphics.present()
end

io.write('probe: done\n')
io.flush()
