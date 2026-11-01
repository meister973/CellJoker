-- Draws known values through the probe shader, then alpha blending cases over red.
local graphics = love_graphics_native
local shader = graphics.newShader('probe.fs')
local half_white = graphics.newImage('tests/half_white.png')
local half_white_quad = graphics.newQuad(0, 0, 4, 4, 4, 4)
local canvas = graphics.newCanvas(1280, 720)

shader:send('a', { 0.1, 0.2, 0.3, 0.4 })
shader:send('b', { 0.5, 0.6 })
shader:send('c', 0.7)
shader:send('d', 0.8)

local function draw_scene()
    graphics.clear(0, 0, 0, 1)
    graphics.setShader(shader)
    graphics.setColor(0.25, 0.5, 0.75, 1)
    graphics.rectangle('fill', 0, 0, 1280, 500)
    graphics.setShader()

    graphics.setColor(1, 0, 0, 1)
    graphics.rectangle('fill', 0, 500, 1280, 220)

    -- Vertex alpha through the default shader.
    graphics.setColor(0, 0, 1, 0.5)
    graphics.rectangle('fill', 0, 500, 300, 100)

    -- Alpha returned by a custom shader.
    graphics.setColor(1, 1, 1, 1)
    graphics.setShader(shader)
    graphics.rectangle('fill', 300, 500, 300, 100)
    graphics.setShader()

    -- Alpha from a texture, drawn whole and through a quad.
    graphics.draw(half_white, 600, 500, 0, 75, 25)
    graphics.draw(half_white, half_white_quad, 900, 500, 0, 75, 25)
end

for frame = 1, 40 do
    -- Lower half: the same scene through a canvas, as the game draws everything.
    graphics.setCanvas(canvas)
    draw_scene()
    graphics.setCanvas()
    draw_scene()
    graphics.setColor(1, 1, 1, 1)
    graphics.draw(canvas, half_white_quad, 0, 620)
    graphics.setColor(0, 1, 0, 1)
    graphics.rectangle('fill', 0, 620, 40, 40)

    if frame == 30 then
        io.write('dump ', tostring(graphics.debugDumpScreen('/dev_hdd0/tmp/frame_30.ppm')), '\n')
        io.flush()
    end
    graphics.present()
end

io.write('probe: done\n')
io.flush()
