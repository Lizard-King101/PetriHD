# Petri HD

An evolving world of plants and animals for a Raspberry Pi 3 B+ with a 7"
touchscreen, the successor to [Petri](https://github.com/Lizard-King101/Petri).
The design is in [docs/PLAN.md](docs/PLAN.md).

The valley runs down to the sea, with hills, streams, ponds, seasons,
weather and fire. More than 100,000 plants can grow, compete, set seed,
evolve, burn and come back. Each has its own genome.
Animals sense food and neighbours, graze, hunt, call and mate
using inherited brains. A new world starts with fifty bodies nobody
designed; those built to hunt arrive in year 8. They have a reserve of
food and a faster one of energy for sprinting, flying and lunging, and
they sleep to get both back.
[Animals and brain experiments](docs/ANIMALS.md) · [Plants](docs/PLANTS.md)

## Build

Needs CMake, a C++17 compiler and SDL2 (only for the window).

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
```

- `build/petrihd`: the window. Drag to pan, scroll or pinch to zoom,
  long-press or right-click a cell to inspect it. Space pauses, 1–4 set the
  speed, H shows the current simulation tick and performance HUD, S saves,
  and F12 takes a screenshot named for the tick. Press X or tap Tree X-ray
  to see animals and ground through the canopy. Tap an animal to inspect
  its three-quarter portrait. For captures, `--tree-xray` and `--top-down`
  start with those views enabled.
  The window opens at 800×480 for the Pi LCD. Drag a window edge or maximize
  it to see more of the map at the same zoom. F11 toggles desktop fullscreen.
  Use `--size 1600x900` for a larger starting window or `--fullscreen` to fill the display.
  The default `--performance desktop` uses the usual two-screen-pixel art scale.
  Use `--performance low` on the Pi or a slower desktop. It targets 30 FPS and
  fits the world rendering into 400×240 pixels while keeping the controls at window resolution.
  You can override either profile's art scale with `--pixel N`.
  `--seed N`, `--load FILE`, `--ticks N` (run ahead
  first), `--shot FILE.png` (draw one frame and quit), `--frames N --speed
  0-3` (time N frames and quit).
  From 24 to 32 px per cell the overhead map fades into live creatures and brush.
  Above 32 px per cell the camera tilts toward a three-quarter view (zoom up
  to 256); T locks it top-down and G turns it off. See
  [docs/RENDERING.md](docs/RENDERING.md).
- `build/petrihd-run`: headless. `seed=1 ticks=3840 every=480` prints a
  census line of `key=value` pairs; `ocean=38` sets the share of sea,
  `biomes=1` lists biome shares, and `save=`, `load=` and `png=` write and
  read saves and full-resolution screenshots.
- `tools/check_arm.sh`: builds for aarch64, runs the tests under qemu and
  checks the fingerprints match x86. Needs `aarch64-linux-gnu-gcc` and
  `qemu-user`.

## Shoreline art study

Run `build/petrihd --shoreline-study` to explore the painted foliage, ground
and shallows at the reference bank. Every plant there is a pixel-art sprite,
painted once from the plant's size, kind, colour and season
(`gfx/plant_sprites.cpp`); `build/petrihd-sprites sheet.png` paints a sheet
of them without opening a window. What works and what doesn't is in
[docs/SHORELINE_REVIEW.md](docs/SHORELINE_REVIEW.md). This study opens seed 1 at noon on the first day,
centered on cell (186, 73), at 64 pixels per cell with a 55° camera pitch.
The ground is painted at the same density wherever the camera goes
(`gfx/ground_art.cpp`), in the colours of the bird's-eye view, and repainted
as water, plants and seasons change; `build/petrihd-ground ground.png` paints
a piece of it without opening a window. `--at X,Y` opens the study anywhere
on the map. Drag to pan, and scroll or pinch to zoom. The study starts paused. Press Space
to play and watch the foliage and test creature move.

Add `--shot shoreline.png` to capture the reference frame and exit. The
capture hides the budget HUD and uses a fixed animation phase.

![Shoreline study with layered crowns, reeds and a creature at the water’s edge](docs/images/shoreline_study.png)

## The nursery

Founder brains are bred before a world starts, in arenas where everything is
set by a dial: every kind of founder climbs a ladder of levels, from eating
when hungry to raising two generations on little food.

```sh
cmake --build build --target petrihd-nursery
build/petrihd-nursery train build/nursery
build/petrihd --dev-map --founders build/nursery/founders.phdb
```

Open `build/nursery/nursery.html` while it trains. [How the nursery works and what it has found](docs/NURSERY.md)

## Development map

Run `build/petrihd --dev-map` for a small world (128 x 96 cells, 192 x 144 m)
laid out by hand, drawn the painted way, for trying things in every kind of
place at once. It is a real world: the same simulation runs on it.

![The development map from above](docs/images/dev_map.png)

| Where | What is there |
| --- | --- |
| North-west | A 24 m hill, gentle to the south and west and steep on its east face |
| North-east | A ramp: one even slope, 11 m up to the north edge |
| Middle | A meadow, and south of the stream a pad of short turf on one even plane |
| West | A mixed wood |
| East | A lake about 3.5 m deep with a wide shallow shelf on its west side |
| Across the middle | A stream from the hill's foot into the lake, and the lake's outlet south to the sea |
| South | A beach and the sea |

It opens on the pad at 64 px per cell, playing. `--at X,Y`, `--zoom` and
`--ticks` work as elsewhere. The layout is `phd::Layout::development`
(`core/terrain.cpp`), kept in saves from version 6.

During a flood, press Space to pause and H to see the tick. Right-click or
long-press a cell to read its coordinates, ground elevation and water depth.
Older saves recalculate flood coverage when opened, at the saved tick.

One storm crosses the dev map every two simulation days. This opens the
meadow just before the first one arrives.

```sh
build/petrihd --dev-map --seed 1 --at 63.2,35.7 --zoom 53 --ticks 210
```

Rain reaches this spot around tick 241. Watch the water spread through the
grass, then pull back after the rain ends around tick 400. Press 3 for 4×
speed or 2 to return to normal.

## Layout

- `core/`: the simulation. Deterministic, integer, no dependencies.
- `gfx/`: the OpenGL ES 2 renderer, loaded through SDL.
- `ui/`: input and interface.
- `apps/`: `petrihd` and `petrihd-run`.
- `tests/`, `tools/`, `cmake/`, `docs/`.
