# pico8native — the user's own PICO-8 in NX Redux

Runs the official PICO-8 (`pico8_64` and `pico8.dat` from the Raspberry Pi
download, put in `Bios/P8` or `Bios/PICO` by the user, never shipped) in two
ways:

- **Carts in the roms folder** run in minarch like any console, through the
  `pico8native_libretro.so` bridge core: minarch's menu, save states, the game
  switcher, auto-resume, Game Tracker. Without the PICO-8 files, P8.pak runs
  fake08 as before (`pico8_runtime.sh` decides, for P8.pak, Splore and
  `PICO8_findRuntime` in nextui).
- **Splore**, PICO-8's cart browser, is an Xtras install (`catalog/splore`,
  refused without the PICO-8 files) that runs `pico8_64 -splore` directly on
  the device's SDL, no minarch. Carts favourited there are copied into the
  roms folder when Splore exits (`pico8_copy_favourites.sh`).

## Pieces

| File | What it is |
|------|------------|
| `core.c`, `core_args.c` | The libretro core. Forks and execs `pico8_64 -run <cart>` (ASLR off, dies with minarch, its own process group) and steps it one frame per `retro_run`. |
| `p8ipc.c` | The shared memory between core and shim: one 128×128 frame, its audio, the pad, commands (save, load, quit), process-shared semaphores. |
| `shim/` | `pico8shim.so`, preloaded into `pico8_64`. Bridge mode: captures frames from `SDL_UpdateTexture`, pulls audio from PICO-8's callback, feeds the pad, keeps a virtual clock, takes save states. Standalone mode (`P8SHIM_STANDALONE=1`, Splore): SDL goes straight through, minus the sensor subsystem the device SDL lacks; Menu opens "Quit Splore?". |
| `shim/state.c`, `p8state.c` | Save states as a snapshot of PICO-8's memory: its writable segment plus a dlmalloc arena at a fixed address, LZ4-compressed (about 2–3 MB, 0.1 s to save, 0.2 s to load). |
| `lib/libSDL2-2.0.so.0` (in P8.pak) | The toolchain's SDL2 with the offscreen video driver, for the bridge's `pico8_64` only. Splore uses the device's SDL. |

## Environment

The core sets these for `pico8_64`; Splore's launcher sets the standalone ones.

| Variable | Meaning |
|----------|---------|
| `P8NATIVE_IPC` | Path of the core's shared memory file (bridge mode). |
| `P8NATIVE_HOME` | PICO-8's home: `$SHARED_USERDATA_PATH/P8-native/home` (config, Splore cache, favourites). |
| `P8NATIVE_CDATA` | Where cart save data goes: `Saves/P8/cdata`, shared with fake08. |
| `P8NATIVE_STATES` | Save states on or off for this run. |
| `P8SHIM_STANDALONE` | `1`: Splore, no core. |
| `NX_BUTTON_LAYOUT` | `xbox`: in Splore the shim swaps A/B and X/Y, as minarch does for carts, and the quit dialog's Quit is the system's A. |
| `P8SHIM_CAP_LABELS` | `1`: the quit dialog names the printed caps (Xbox layout with Hint labels = printed caps). |

PICO-8 must run with `host_framerate_control 1`; the core adds it to
`config.txt` when the line is missing.

## Known limits

- No rewind for native carts (minarch's default, off): each push would copy a
  whole memory snapshot.
- No save states, switcher entry or auto-resume for carts played inside
  Splore; favourite them to get a copy in the roms folder.
- Splore keeps the launcher's CPU setting (tg5040: schedutil, 408–600 MHz).
- The cart's own picture stands in for missing art: the whole cartridge for
  box art, its 128×128 label for screenshots, until art is scraped.

## Tests

`sh workspace/all/pico8native/tests/run_tests.sh` (in the toolchain container;
it starts one itself on a non-Linux host): the ipc, the shim pieces, the shim
against a fake SDL and a fake PICO-8 in both modes, the core with a real fork,
the favourites copy and the launchers.

## Credits

Save states, the Splore cart copying idea and the TrimUI controller mapping
come from [minui-pico-8-pak](https://github.com/josegonzalez/minui-pico-8-pak)
by Jose Diaz-Gonzalez (MIT, `LICENSE-minui-pico-8-pak.txt`). dlmalloc
(MIT-0) and LZ4 (BSD 2-clause) are built into the shim. PICO-8 is by
Lexaloffle Games; NX Redux ships none of it.
