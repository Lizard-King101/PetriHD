# Rendering and the camera

**Status:** November 19, 2026. The tilted camera is a prototype on the
`tilt-camera` branch.

## Scale

A cell is 1.5 m across (`phd::cell_decimetres`): a creature is 0.2–3 cells
long, and the world is a valley about 580 × 340 m running down to the sea.
Heights are in decimetres, mostly 0–50 m, and are drawn at true scale
(`--relief`, by default one cell of height per 1.5 m). Hill shading uses the
true slopes too.

![Seed 1: a valley running down to the sea in the west](images/valley_seed1.png)

## Two paths, one texture

The terrain is baked on the CPU at 4 texels per cell (`gfx/terrain_art.cpp`),
16-cell chunks at a time within 3 ms per frame, those on screen first.
Each chunk is also averaged down to 2 texels and 1 texel per cell; the
zoomed-out view draws whichever level gives about one texel per pixel, so
small things like tree crowns don't flicker in and out as the camera moves.

- **Ground and plants are separate layers.** The ground's colour comes from
  what it's made of (loam, sand, clay, peat, rock or gravel) and how wet it
  is. Low plants cover it in patches, as much as the biome allows. Trees are
  round crowns rooted at fixed spots in each cell, lit like domes and casting
  shadows, drawn over whatever ground they stand on. Until milestone 2 has
  plants of their own, the biome decides what grows.

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
- **Trees change level of detail at 32 px per cell.** Flat, they are baked
  crowns. Tilted, the bake leaves the crowns out and paints forest floor
  (leaf litter, needles) where they were, and the tilted view stands real
  trees in exactly those places, with the same sizes and colours
  (`art::tree_in`), casting their own shadows: about 150 triangles each,
  115 trees and 17,500 triangles in a forest view at 64 px per cell.
- **Objects:** the test tree and the six-legged test creature are built each
  frame from flat-shaded boxes and frustums. They stand on `TiltView::ground`,
  which follows the mesh's own triangles, so feet stay on the drawn surface.

## One pixel grid

The world is drawn into an off-screen image at half the window's logical
size (400 × 224 for the 800 × 480 panel) and scaled up by whole pixels with
no smoothing (`gfx/pixel_target.cpp`, `--pixel N`). Ground, plants and
creatures then share one pixel size, which is the pixel-art look of the
concept art. The interface stays at full resolution on top.

- **Snapping:** the camera used for drawing (`Camera::for_target`) moves the
  focus by less than a pixel, so the ground lands on whole pixels. Panning
  then slides the picture instead of resampling it, and texels don't crawl.
- **Depth:** the target has its own depth buffer: 24-bit where the GPU has
  `GL_OES_depth24` (and on desktop GL), otherwise 16-bit. The depth range
  is fitted each frame to what can be on screen, instead of a fixed ±600
  cells, so even a 16-bit buffer resolves thin legs and shadows.
- **Precision:** shaders ask for full precision wherever the GPU has it.
  At half precision, texture coordinates across the 1,536-texel terrain
  texture land up to a texel and a half off, and the error shifts as the
  camera moves.

## Controls

| Key or flag | Effect |
| --- | --- |
| Scroll, pinch | Zoom, 1–256 px per cell (32 with the tilted view off) |
| T | Lock the camera top-down |
| G, `--flat` | Turn the tilted view off |
| O | Move the test tree and creature to the focus |
| `--zoom Z --at X,Y` | Start at a zoom and focus cell |
| `--relief K` | Display cells of height per metre |
| `--pixel N` | Screen pixels per art pixel (default 2; 1 turns the grid off) |

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
- On a Wayland desktop at 2× scale, SDL 2 through `sdl2-compat` gives a
  400 × 240 drawable for the 800 × 480 window, and the compositor stretches
  it. Since the world is drawn at 400 × 224 anyway, only the interface text
  loses sharpness. X11 gets the full size.

## Phase 2

The order now: the terrain reworked to the 1–2 m scale; ground detail by
zoom on the pixel grid; then plants and creature bodies drawn from the
genome as low-poly models through the pixel grid, with procedural legs.

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
