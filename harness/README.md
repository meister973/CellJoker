# Host game runner

The runner runs the game's own Lua with no console and no emulator. It plays one long run by itself and reports the cost of each round. Use it to find slowdowns and leaks that take many rounds to show.

The runner uses the same Lua 5.1.5 as the PS3 build, with the same patch, `docker/lua/fast_table_access.patch`. `run.sh` builds Lua from the repository's patch on every run, so the runner never tests an old copy. The runner also uses the same C modules from `runtime/src/lua` and `runtime/src/love/lua_data.c`. It uses the same Lua runtime files from `runtime/lua`, which include `love_thread.lua` and `game_fixes.lua`. Graphics and audio are stubs in `host_love_native.lua`, with the same methods as the PS3 objects.

Time is simulated. Each frame adds 1/60 s, and each read of the clock adds 2 µs. The game's `nuGC` collects garbage until 0.3 ms pass. Without the step, no time passes inside a frame, and `nuGC` always runs its 1000-step maximum. With the same seed, every round after round 0 gives the same result every time. Round 0 is the boot and the main menu, and its draw count changes by about 1 between runs.

## What it measures

The runner writes one `metric:` line per round:

| Field | Meaning |
|---|---|
| `instr_avg_k`, `instr_max_k` | Lua VM instructions per frame, in thousands, in all coroutines. |
| `draw_instr_avg_k` | The part of `instr_avg_k` spent in `love.draw`. The rest is update. |
| `cpu_avg_ms`, `cpu_max_ms` | Host CPU time per frame. Use this to compare runs on the same PC only. |
| `draws_avg`, `draws_max` | Draw calls per frame, counted as the PS3 renderer issues them. |
| `save_instr_max_k` | The largest save of the round. The save runs inside the frame on the PS3. |
| `retained_kb` | The heap after a full garbage collection. If it grows each round, something leaks. |

## Run it

You need Docker. Run these commands from the repository root in Git Bash. The second `-v` is your unzipped copy of the game, and the runner never changes it.

```sh
docker build -t balatro-harness:3 harness
MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD:/work" -v "D:/path/to/unzipped/Balatro:/game:ro" \
    -e HARNESS_MAX_ANTE=12 balatro-harness:3 sh harness/run.sh
```

The full log goes to `harness/out/`.

## Options

| Variable | Effect |
|---|---|
| `HARNESS_MAX_ANTE` | The run stops in the shop after this ante. The default is 12. |
| `HARNESS_SEED` | The seed of the run. The default is `PS3TEST1`. |
| `HARNESS_INPUT=pad` | The runner presses pad buttons, so the game's controller code runs. It buys nothing. |
| `HARNESS_BUY=0` | The runner buys no jokers. |
| `HARNESS_CENSUS=1` | After each round, the runner counts the live objects and names the groups that grow. This is slow. |
| `HARNESS_PROFILE=1` | The runner samples the main coroutine and prints its busiest functions per round. In this mode the `instr_*` fields are not valid. |
| `HARNESS_NO_FIXES=1` | The runner runs without `runtime/lua/game_fixes.lua`. Use this as a baseline. |
| `HARNESS_MAX_GROWTH_KB` | The leak check. If `retained_kb` grows more than this per round, the run exits with 1. |
| `HARNESS_STOCK_LUA=1` | The runner uses Lua 5.1.5 without the patch. Use this as a baseline. |
| `HARNESS_CLOCK_STEP_US` | The time that each read of the clock adds, in microseconds. The default is 2. |
| `HARNESS_LUA_GAME=1` | The runner uses the game's Lua engine functions, not the C versions in `runtime/src/lua/game_native.c`. Use this as a baseline. |
| `HARNESS_COMPARE_GAME=1` | The runner calls the Lua and the C version of each ported engine function on every call. At the first different result, the run stops with an error. The log counts the calls. |
| `HARNESS_STATE_DUMP` | A file. Every `HARNESS_STATE_EVERY` frames (default 30), the runner writes the transform, velocity and flags of every Moveable to it. A run with `HARNESS_LUA_GAME=1` and a run without it must give the same file. This checks the C versions that change objects, such as `Moveable:move`. |
| `HARNESS_STATE_EVERY` | The frames between two writes of `HARNESS_STATE_DUMP`. The default is 30. |
| `HARNESS_CALLGRIND_FROM` | The frame where callgrind starts to collect. The run stops `HARNESS_CALLGRIND_FRAMES` frames later (default 600). See the next section. |

To run the leak check, use this command. Without the fixes it fails at about 1,000 KB per round. With the fixes it passes at about 25 KB per round.

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD:/work" -v "D:/path/to/unzipped/Balatro:/game:ro" \
    -e HARNESS_MAX_ANTE=5 -e HARNESS_MAX_GROWTH_KB=100 balatro-harness:3 sh harness/run.sh
```

## Find where the native time goes

Instruction counts show the Lua work, but not the cost of each instruction. Callgrind counts the host's machine instructions in each C function of the interpreter. This command collects 600 frames from the middle of a run:

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD:/work" -v "D:/path/to/unzipped/Balatro:/game:ro" \
    -e HARNESS_CFLAGS=-DHARNESS_CALLGRIND -e HARNESS_CALLGRIND_FROM=4000 \
    -e "RUNNER=valgrind --tool=callgrind --instr-atstart=no --callgrind-out-file=/work/harness/out/callgrind.out" \
    balatro-harness:3 sh -c 'sh harness/run.sh; callgrind_annotate --inclusive=no /work/harness/out/callgrind.out | head -40'
```

With stock Lua, 39% of the native instructions were in `luaV_gettable` and `luaH_get`, which read table fields. The patch reads string fields, and integer indexes in the array part, inside the VM loop. It also writes existing string fields there. A lookup that a metamethod can change takes the normal path. For these 600 frames, the total went from 17.26 to 13.31 billion machine instructions, which is 23% fewer. The game played the same rounds, with the same saves and draw calls. Lua 5.1's own test suite gives the same output with and without the patch.

## Other targets

`run_luajit.sh` runs the same runner on LuaJIT 2.1, which is the interpreter of the PC game. If a result is different between the two, the port's interpreter causes it.

`Dockerfile.ppc` builds the runner for 32-bit big-endian PowerPC and runs it under `qemu-ppc-static`. The game logic must match the host run. Heap sizes are smaller, because pointers are 32 bits. QEMU timings mean nothing. The image builds with `-ffp-contract=off`, as the PS3 build does. To see the rounding difference that the flag removes, add `-e CC=powerpc-linux-gnu-gcc` to `docker run`.

## Limits

The runner does not measure PS3 frame time. It counts Lua work and draw calls. Real frame rates come from a real PS3 only.
