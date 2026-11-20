#pragma once

#include <cstdint>

#include "world.hpp"

// The ground of the close view, painted on the CPU at 32 texels per cell:
// one texel per art pixel at 64 px per cell, the same density as the plant
// sprites. Soil, low growth, the wet margin and the waterline, the bed seen
// through shallows, pebbles, blades of grass, snow and burnt ground, all
// from the world and in the colours of the bird's-eye terrain (palette.hpp).
// It can paint any part of the world, a tile at a time, and tiles meet
// without a seam. There is no GL here.
namespace art::ground {

constexpr int texels_per_cell = 32;

// Paints the texels [tx0, tx0 + w) x [ty0, ty0 + h) as opaque RGBA,
// with rows `stride` bytes apart.
// With `water` false the water is left out: its bed is painted, for a view
// that draws the water over it as a surface of its own.
void paint(const phd::World& world, int tx0, int ty0, int w, int h, std::uint8_t* rgba, int stride, bool water = true);

// The same ground at `per_cell` texels to the cell (a divisor of 32), one
// texel of the painting for each: the bird's-eye bake draws its ground from
// this, so both views are one picture at two sizes. Fills rgb (3 bytes a
// texel, rows of w) and whether each texel is water.
void sample(const phd::World& world, int tx0, int ty0, int w, int h, int per_cell, std::uint8_t* rgb, std::uint8_t* water);

// A number that changes when the picture of these cells would: water, what
// grows, snow, fire, the season. Compare it to know when to paint again.
std::uint32_t signature(const phd::World& world, int cx0, int cy0, int w, int h);

} // namespace art::ground
