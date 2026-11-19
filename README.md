# Petri HD

An evolving world of plants and animals for a Raspberry Pi 3 B+ with a 7"
touchscreen, the successor to [Petri](https://github.com/Lizard-King101/Petri).
The design is in [docs/PLAN.md](docs/PLAN.md).

**Status:** milestone 1, land and water. A generated world of mountains,
coasts, rivers and lakes, with seasons, day and night, storm fronts, rain,
snow and 26 biomes. No plants or animals yet.

## Build

Needs CMake, a C++17 compiler and SDL2 (only for the window).

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
```

- `build/petrihd`: the window. Drag to pan, scroll or pinch to zoom,
  long-press or right-click a cell to inspect it. Space pauses, 1–4 set the
  speed, H toggles the budget HUD, S saves, F12 takes a screenshot.
  `--seed N`, `--size 1024x600`, `--load FILE`, `--ticks N` (run ahead
  first), `--shot FILE.png` (draw one frame and quit), `--frames N --speed
  0-3` (time N frames and quit).
- `build/petrihd-run`: headless. `seed=1 ticks=3840 every=480` prints a
  census line of `key=value` pairs; `ocean=38` sets the share of sea,
  `biomes=1` lists biome shares, and `save=`, `load=` and `png=` write and
  read saves and full-resolution screenshots.
- `tools/check_arm.sh`: builds for aarch64, runs the tests under qemu and
  checks the fingerprints match x86. Needs `aarch64-linux-gnu-gcc` and
  `qemu-user`.

## Layout

- `core/`: the simulation. Deterministic, integer, no dependencies.
- `gfx/`: the OpenGL ES 2 renderer, loaded through SDL.
- `ui/`: input and interface.
- `apps/`: `petrihd` and `petrihd-run`.
- `tests/`, `tools/`, `cmake/`, `docs/`.
