# Handoff: Balatro on PS3

Updated 2026-11-01. The game plays on RPCS3 from the unmodified shipped Lua, with sound and saves.
A real PS3 ran the game on 2 runs with no problems.

## State

| Area | State | Evidence |
|---|---|---|
| Boot to main menu (`G.STATE` 11) | Works on RPCS3 | `tools/run_rpcs3.ps1`, frame dumps |
| Text, sprites, canvas, CRT effect, dissolve, background swirl | Correct in frame dumps | `tests/shader_probe`, frame dumps |
| Game logic | Works in scripted play: scoring, Small, Big and Boss blinds, cash out, shop, buying, ante 2 | `tests/gameplay/run_driver.ps1`, frame dumps |
| Controller input | Connected. Scripted button presses play from the menu to the shop. Physical presses on an Xbox pad are not yet confirmed. | `tests/gameplay/run_driver.ps1`, `tests/joystick` |
| Audio | Works on RPCS3. Known tones measure correctly. In the game, music plays and restarts, with no clipping and 0 underruns. CPU cost on a PS3 is not known. | `tests/audio/run.ps1`, `tests/audio/run_game.ps1` |
| Content | All 150 jokers, 15 decks, 52 tarot, planet and spectral cards, 32 vouchers and 32 packs ran with no crash. Effects are not proven correct. Jokers and decks last ran before sound and saves, and the owner stopped their rerun. Everything starts unlocked, and the tutorial is skipped. | `tests/gameplay/content_*_driver.lua` |
| PKG | `tools/make_pkg.py` makes it on the user's PC from `Balatro.exe`. An earlier PKG ran on the owner's PS3. The tool ran under Wine, but not yet on real Windows. | `dist/make_pkg` |
| Saves | Work on RPCS3: a run and settings come back on the next boot. A stall while saving on a PS3 is not measured. | `tests/gameplay/save_write_driver.lua`, `save_read_driver.lua` |
| `love.thread` | Workers run as coroutines. Each has its own globals, and a request is served before `push` returns. | `tests/joystick/test_love_thread.lua` |
| `love.data` compress and decompress | Raw deflate through zlib | `save_read_driver.lua` |
| Languages | 11 of 15 ship. Japanese, Korean and Chinese are left out, and `boot.lua` hides them from the menu, because their fonts are too large. | `tools/make_game_data.py` |
| Real PS3 | Ran fine on 2 runs. It reports through `/dev_hdd0/tmp/balatro_log.txt`. | |

## Commands

Run all commands from the repository root in PowerShell. Docker needs full access in a sandbox.

Build from clean and copy to RPCS3's HDD. This also cooks the textures on the first run:

```powershell
pwsh -NoProfile -File tools/build_love.ps1
```

Run the game with no window. The Null renderer draws nothing, so this tests logic only:

```powershell
pwsh -NoProfile -File tools/run_rpcs3.ps1 -Project love-ps3 -Expect 'frame=3000' -TimeoutSeconds 240
```

Get pictures with the real renderer. RPCS3's window is hidden as soon as it appears, and the
game writes `frame_<n>.ppm` to the run folder under `runs/`:

```powershell
pwsh -NoProfile -Command "& ./tools/run_rpcs3.ps1 -Project love-ps3 -Expect 'dump /dev_hdd0/tmp/frame_2400' -TimeoutSeconds 120 -Frames 2400"
```

Play it by hand. RPCS3 opens a window, with the first Xbox pad as Player 1 for this launch only:

```powershell
pwsh -NoProfile -File tools/play.ps1
```

Run all the tests, with no window. Run `tools/build_love.ps1` first. `tests/run_all.ps1` prints one
line per test, and its list shows how to run one test alone:

```powershell
pwsh -NoProfile -File tests/run_all.ps1
```

Build the Windows tool after a build. It goes to `dist/make_pkg`, and it holds no game content:

```powershell
docker run --rm -v "${PWD}:/work" -w /work balatro-ps3dev:4 sh tools/release_runtime.sh
docker run --rm -v "${PWD}:/work" -w /work ubuntu:22.04 sh tools/release_windows_tools.sh
```

To use the tool, drag `Balatro.exe` onto `dist/make_pkg/make_pkg.bat`. The PKG it makes contains the game,
and must not be shared.

## Layout

| Path | Purpose |
|---|---|
| `love-ps3/` | The game executable. Boot `build/love-ps3.elf`. |
| `runtime/src/lua/` | `bit`, and the LuaJIT behavior the game needs on Lua 5.1 |
| `runtime/src/love/gfx_*.c` | RSX device, textures, shader programs, fonts |
| `runtime/src/love/lua_*.c` | `love.graphics` bindings and platform services |
| `runtime/src/platform/rsx_ring.c` | Command buffer ring |
| `runtime/lua/` | `boot.lua` and the other LÖVE modules |
| `tools/shader_translate.py` | LÖVE GLSL to Cg |
| `tools/cgcomp_full_precision.py` | Fixes Cg's fragment assembly for RSX |
| `tools/texture_cook.py` | PNG to RSX texture files in `cooked/` |
| `tools/shader_build.py` | The game's shaders to files in `USRDIR/shaders` |
| `tools/make_pkg.py` | `Balatro.exe` to an installable PKG |
| `tools/release/` | The files that ship beside the Windows tool |
| `tools/sync_game.ps1` | Copies files to `Desktop\rpcs3\dev_hdd0\game\BLTR00001\USRDIR` |
| `tests/` | Differential and integration tests |

`game/`, `cooked/`, `runs/` and all build output stay out of git. The game files are not ours to
share.

## Facts that cost time

Boot only `build/<name>.elf`. Only that copy goes through `sprxlinker`. The top-level ELF runs and
prints nothing.

Vanilla Lua 5.1 is not enough. `pseudorandom` seeds with a fraction, which Lua 5.1 truncates to 0,
so every random result is the same. `runtime/src/lua/luajit_compat.c` restores LuaJIT's random
generator, `math.log` with a base, and `%s` on any value. `tests/lua_compat` compares 149 results
with real LuaJIT.

Librsx cannot wrap the command buffer safely. Its `rsxContextCallback` is inline assembly with no
memory clobber, so the caller keeps a stale `context->current`. Call `rsx_ring_make_room` between
groups of commands, and never let the library reach the callback.

Cg treats `result.color` and temporary `R0` as separate registers, but on RSX they are the same
register. `tools/cgcomp_full_precision.py` moves all temporaries up one. The same tool removes half
precision, because the shaders do arithmetic on large `time` values.

cgcomp's parser counts the closing bar of `|R1.y|` as a swizzle letter, which gives `y,x,x,x`. Any
destination but `.x` then reads the wrong component. `atan` became constant in each quadrant, which
split the menu background along the screen's center lines. The same tool spells these swizzles out,
in vertex programs too.

Shaders are files, not part of `EBOOT.BIN`, because they come from the game's own shaders. The runtime can ship with
no game content for this reason. `cgcomp.exe` under Wine gives the same bytes as Linux `cgcomp` for all 44 programs.

`pkg.c`'s Windows folder walk stores paths with a leading slash, and overflows the default stack.
`tools/release_windows_tools.sh` builds its POSIX walk with a larger stack.

The tool makes the debug form of PKG only. `package_finalize` needs a retail key and is GPLv3, so it does not ship.

Uniforms reach fragment programs as vertex constants through `TEXCOORD2` to `TEXCOORD7`. This avoids
patching fragment constants for each sprite. Six slots is the limit, because Cg stops at `TEXCOORD7`.

Blend state must be set after each surface bind, or it does not take effect.

Shapes that overlap inside one draw call do not blend over each other under RPCS3. Each drawable has
its own draw call. Only the glyphs of one string share one.

With nothing focused, a pad's `a` is a click at the cursor. Press the d-pad first to focus something.
At the hand, focus starts in the left column, so `dpright` reaches the cards.

The YAML device name must be quoted: `"XInput Pad #1"`. Unquoted, `#1` is a comment.

Toggling Pixel Smoothing reloads every atlas. Images are cached by path and DPI scale, because RSX
memory cannot hold 2x twice.

The game makes no stencil calls. The "17 files" in the first survey were the Joker Stencil card.

The textures fit in memory. The game loads one scale, and 2x without mips is about 120 MB of the
256 MB of RSX memory.

RPCS3 writes a dumped WAV's header only on a clean exit. The driver tests kill it, so
`tests/audio/check_game.py` reads the samples as raw 48 kHz float stereo.

The game's coin and glass sounds have sample steps above 1.0 in the source files. A large step in a
dump is not proof of a click. The `underruns` counter is the measure.

The mixer has two threads. The mixer thread decodes and mixes ahead into a ring of 16 blocks. The
feeder thread only copies to the 8-block port, so slow decoding cannot miss a port deadline. The game keeps all
its music tracks playing and changes their volumes. An almost silent stream skips decoding. It seeks
to the correct position at the next audible volume.

## Next steps

1. Measure sound and saves on a real PS3: frame rate, `underruns=` in the log, and any stall on a
   save. The save compresses on the main thread, because workers are coroutines.
2. Play a full run to ante 8, and fix what it finds. The scripts have not won a run or used a
   joker's effect on purpose.
3. Measure the frame rate of a long run on a real PS3, with `runtime/lua/game_fixes.lua`.

## Long runs

The shipped game leaks UI objects in every round, and the leak is the same on LuaJIT. Each
leaked object is updated and drawn in every frame, so before the fix a run slowed down round by
round. From round 2 to round 35, the Lua work per frame grew from 277k to 576k instructions.
Over the same rounds, the live heap grew from 13 MB to 47 MB. `runtime/lua/game_fixes.lua` fixes
three causes, and with it both numbers stay flat. `harness/README.md` explains the measurement
and the leak test.

The save is not the cause. It costs about 135k instructions and does not grow.

Lua is built with `-ffp-contract=off`. Without the flag, ppu-gcc fuses `a*b+c` in the Lua VM.
Then `%` rounds differently from LuaJIT on x86, and a seed plays a different run than on PC.
RPCS3 does not show the difference. The QEMU PowerPC build in `harness/` shows it.

## Frame rate

A tester reports that a real PS3 runs below 30 fps from the start, and that the game is playable.
In the host runner (`harness/`), a frame in a round costs about 250k Lua instructions. Update uses
64% of them, and draw uses the rest. On this PC, the host build needs about 4 ms per frame. A PPU
runs this interpreter many times slower, so Lua is the likely limit. A real PS3 must confirm this.

Callgrind showed that 39% of the interpreter's machine instructions read table fields, through three
calls per read. `docker/lua/fast_table_access.patch` does the common reads and writes inside the VM
loop. If a metamethod can apply, the fast path does nothing and the normal path runs, so the
results are the same. The machine instructions per frame went down by 23%. With the patch, the
same rounds play with the same saves and draw calls, and Lua's own test suite gives the same output.

Measured and rejected: copying inherited methods into each class saved 2%, because almost every
lookup already hits in the first table. Also, a later change to a parent class then does not reach
its children.

Every 300 frames, `balatro_log.txt` gives the average and worst frame time, and the average time per
frame in update, GC, draw and present (`runtime/lua/frame_timing.lua`). Update includes the GC
time. If present is large, the frame waits for the RSX. If update and draw are large, the PPU is
the limit. The clock reads the time base register, because the game's GC step reads the clock up
to 1000 times a frame, and a system call each time spent the GC budget on the calls.

Five of the game's hot functions run in C. They read the same fields, and do the same arithmetic in
the same order, as the Lua versions:

- `prep_draw` (`runtime/src/love/lua_prep_draw.c`) sets the transform for each drawn object. In
  RPCS3, `tests/prep_draw` compares it with the game's own version on 5000 random cases, and the
  transforms must be equal bit for bit. It runs in about half the time of the Lua version.
- `Node:collides_with_point` (`runtime/src/lua/game_native.c`) tests the cursor against each node,
  about 160 times a frame. With `HARNESS_COMPARE_GAME=1`, the host runner calls both versions on
  every call. A run to ante 5 compared 3.1 million calls with no difference, and the PPC runner gave
  the same result. In the host runner, the Lua instructions per frame went down by about 10%.
- `Moveable:move` (`runtime/src/lua/moveable_native.c`) eases every object toward its target
  position each frame. It calls the game's Lua methods for the rare paths: a changed alignment,
  `lr_clamp`, juice and `get_major`. `Moveable:glue_to_major`, which moves the sprites glued to
  each card, is also in C, and `move` runs it without a Lua call. It changes the objects that it moves, so its two versions
  cannot run on one call. `HARNESS_STATE_DUMP` writes the transforms of every object every 30
  frames, and a run with `HARNESS_LUA_GAME=1` must give the same file as a run with the C
  versions. The files were equal on the host to ante 4 and on the PPC runner to ante 2. The Lua
  instructions per frame went down from about 230k to about 167k.
- `UIElement:draw_self` (`runtime/src/lua/ui_native.c`) draws the text, boxes, rows, columns
  and objects of the UI, about 140 times a frame. An element with an outline, focus, a chosen
  triangle, a button delay or a progress bar runs the game's Lua version, as does every element
  when `G.DEBUG` is set. Those are about 1 element a frame. A miss in the vertex cache calls the
  game's `draw_pixellated_rect`. `HARNESS_DRAW_LOG` writes every graphics call with its
  arguments, and the log and the state dump of a run with `HARNESS_LUA_GAME=1` must be equal to
  the ones of a run with the C versions. They were equal on the host to ante 3, with the default
  driver and with `HARNESS_INPUT=pad`, and on the PPC runner to ante 2. The Lua instructions per
  frame in `love.draw` went down from about 96k to about 70k, and for the whole frame from
  about 161k to about 136k.
- `DynaText:draw` (`runtime/src/lua/text_native.c`) draws each letter of the game's moving text,
  first its shadow and then the letter. When `G.DEBUG` is set, the game's Lua version runs. The
  draw log and the state dump were equal on the host to ante 4, with the default driver and with
  `HARNESS_INPUT=pad`, and on the PPC runner to ante 2. With `glue_to_major`, the Lua instructions
  per frame at ante 3 went down from about 136k to about 115k, and in `love.draw` from about 70k
  to about 56k.

The game seeds `math.random` with `os.time()` at boot, so the host runner returns a fixed time
(`HARNESS_TIME`). Without it, two runs that start in different seconds play different menus, and
their logs cannot be compared.

All of them look up their field names through upvalues. A C function that calls `lua_getfield` hashes the
key string each time, and that made the first `prep_draw` in C only 16% faster than the Lua one.

Measured per function in the host runner, with the instruction counter: `Moveable:move` is about 31%
of a frame's Lua instructions, `UIElement:draw_self` 20%, `Node:collides_with_point` 9% and
`prep_draw` 5%, before these ports. `UIElement:draw_self` calls back into much more game code, so a
C version of it is a larger job.
