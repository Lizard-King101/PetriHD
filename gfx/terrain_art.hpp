#pragma once

#include <cstdint>
#include <vector>

#include "world.hpp"

namespace art {

// The terrain is baked on the CPU into a texture with 4 texels per cell:
// biomes with wavy edges, hill shading from the moving sun, water shaded by
// depth with smooth shorelines, snow and wet ground. It has no GL in it, so
// headless screenshots draw exactly what the window shows.
constexpr int texels_per_cell = 4;

struct Sun {
    float x, y, z; // unit vector toward the light
};

Sun sun_for(const phd::World& world);

// Bakes the texels [tx0, tx0 + w) x [ty0, ty0 + h) into rgb (3 bytes each),
// rows `stride` bytes apart. Without hill shading, for the tilted view,
// which lights its own slopes.
void bake(const phd::World& world, const Sun& sun, int tx0, int ty0, int w, int h, std::uint8_t* rgb, int stride,
          bool hill_shade = true);

// Hill shading uses the land's true slopes: metres of rise over the 1.5 m
// width of a cell. The tilted view lights by the same rule.
constexpr float shade_metres = float(phd::cell_decimetres) / 10.0f;

// The whole world, for screenshots.
void bake_all(const phd::World& world, std::vector<std::uint8_t>& rgb, int& width, int& height);

// The colour grade for the time of day, multiplied over the terrain.
void day_tint(const phd::World& world, std::uint8_t out[3]);

void biome_rgb(phd::Biome biome, std::uint8_t out[3]);

} // namespace art
