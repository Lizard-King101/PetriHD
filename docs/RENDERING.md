# Rendering and the camera

**Status:** November 19, 2026. The tilted camera is a prototype on the
`tilt-camera` branch.

## Scale

A simulation cell is about 1–2 m across: a creature is 0.2–3 cells long and
the world is a valley a few hundred metres wide. Elevation stays in metres
for the climate (temperature lapse, rain shadows), but it is drawn at a
diorama scale: `--relief`, by default 0.008 cells of height per metre, so a
2,000 m peak stands 16 cells tall. Nothing in the simulation depends on
either number.

## Two paths, one texture

The terrain is baked on the CPU at 4 texels per cell (`gfx/terrain_art.cpp`),
a few 32-cell chunks per frame within 3 ms, those on screen first.

| Zoom (px per cell) | Path | Pitch |
| --- | --- | --- |
| 1–32 | Flat (`gfx/world_view.cpp`): one textured quad, with clouds, fog, rain, snow and lightning over it | 90° |
| above 32 | Tilted (`gfx/tilt_view.cpp`): the same texture over a heightfield, depth-tested, with 3D objects | 72° at 64, 56° at 128, 45° at 256 |

The camera (`gfx/camera.hpp`) is orthographic throughout. Pitch follows
log2(zoom) between those keys, and zooming keeps the ground under the cursor
in place. Tilting turns about the ground at the focus, not about sea level.

- **Heightfield:** one vertex per cell corner, at the average surface of the
  four cells around it, using the water's surface wherever there is water.
  That is the same rule the bake uses for shorelines, so lakes and the sea
  are flat at their own level, rivers sit in their beds, and nothing floats
  or z-fights. One vertex buffer per chunk (at most 1,089 vertices, inside
  ES 2's 16-bit index limit), refreshed in turn as water levels move.
- **Lighting:** the flat bake hill-shades from the sun. Tilted, the bake skips
  that and the shader lights the mesh from its own normals, with the same
  formula and slope scale (`art::shade_metres`), so the change at 32 px per
  cell keeps the same shading. When the path switches, what is on screen is
  rebaked at once.
- **Objects:** the test tree and the six-legged test creature are built each
  frame from flat-shaded boxes and frustums. They stand on `TiltView::ground`,
  which follows the mesh's own triangles, so feet stay on the drawn surface.

## Controls

| Key or flag | Effect |
| --- | --- |
| Scroll, pinch | Zoom, 1–256 px per cell (32 with the tilted view off) |
| T | Lock the camera top-down |
| G, `--flat` | Turn the tilted view off |
| O | Move the test tree and creature to the focus |
| `--zoom Z --at X,Y` | Start at a zoom and focus cell |
| `--relief K` | Display cells of height per metre |

## Results on the PC

Seed 1, tick 1,620 (day 7, summer, evening). Each row of the contact sheet is
one place at 32, 64, 128 and 256 px per cell: a mountainside, a lake shore,
and a plain.

![The same three places at four zooms](images/tilt_zoom_sheet.png)

Mean and 99th-percentile frame times over 600 frames at normal speed, CPU
only:

| Zoom | Path | Mean frame | p99 | Terrain bake |
| --- | --- | --- | --- | --- |
| 32 | Flat | 3.30 ms | 4.66 ms | 3.30 ms |
| 64 | Tilted | 1.83 ms | 2.86 ms | 1.75 ms |
| 128 | Tilted | 1.81 ms | 2.85 ms | 1.73 ms |
| 256 | Tilted | 1.84 ms | 2.96 ms | 1.70 ms |

The tilted view is cheaper here because fewer chunks are on screen to bake.
It draws 2,300–4,400 triangles in 2–3 draw calls. The GPU's own time isn't
measured yet, and none of this has run on the Pi; the Pi's fill rate and the
cost of baking are the numbers to check there.

## Known gaps

- At 128 and 256 the ground is the 4-texel bake enlarged, so it shows as
  blocks: there is no close detail yet.
- The tilted view draws no weather: no clouds, fog, rain or lightning.
- Water is opaque and flat, with no shallows seen through, no waves and no
  flow.
- Relief is gentle at the default scale. `--relief 0.02` gives taller
  mountains.
- The test objects only appear in the tilted view; the flat view has no
  sprites for them.
- On this Wayland desktop, SDL makes the 800 × 480 window 400 × 240, so it
  renders at half size. That needs looking into separately.

## Phase 2

1. Close-up ground detail above 64 px per cell, from seeds local to each
   chunk: grain, pebbles, grass clumps and leaf litter that follow the biome
   and moisture and never rearrange as the camera moves. Either a second
   texture baked at 16 texels per cell for the chunks on screen, or a small
   detail texture per biome blended in the shader.
2. Water as its own translucent surface over a visible bed, with depth tint,
   shoreline foam and flow along `downstream`.
3. Weather in the tilted view: cloud shadows projected on the mesh, rain and
   snow in world space.
4. Sprites for plants and animals in the flat view, sharing their shapes with
   the 3D versions.
5. Measuring on the Pi, then choosing the relief, the zoom keys and the
   level-of-detail steps.
