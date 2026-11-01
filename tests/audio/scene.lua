-- Plays known tones through love_audio_native, in a fixed order that check.py measures. Each section
-- lasts a set time, and two markers of silence let the checker find the sections.
local audio = love_audio_native
local platform = love_platform

local function wait(seconds)
    platform.sleep(seconds)
end

local short = audio.newSource('resources/sounds/tone_short.ogg', 'static')
local long = audio.newSource('resources/sounds/tone_long.ogg', 'stream')

io.write('audio: start\n'); io.flush()
wait(1.0)

-- 1. A decoded clip at its own pitch. It lasts one second.
audio.play(short)
wait(1.5)

-- 2. A decoded clip an octave up. Twice the speed, so half a second.
short:setPitch(2.0)
audio.play(short)
wait(1.5)
io.write('short finished, isPlaying=', tostring(short:isPlaying()), '\n'); io.flush()

-- 3. A stream at its own pitch.
long:setVolume(1.0)
audio.play(long)
wait(1.5)

-- 4. The same stream silent, as the game's quiet music tracks are. Nothing may be heard.
long:setVolume(0.0)
wait(1.5)

-- 5. Back up, with an octave up. It must seek, then play the right pitch without a glitch.
long:setPitch(2.0)
long:setVolume(1.0)
wait(1.5)

-- 6. Half volume.
long:setPitch(1.0)
long:setVolume(0.5)
wait(1.5)

long:stop()
wait(0.5)
io.write('stopped, isPlaying=', tostring(long:isPlaying()), '\n')
io.write('probe: done\n'); io.flush()
