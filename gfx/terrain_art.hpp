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
// rows `stride` bytes apart. `flat` is the top-down look; the tilted view
// bakes without it: no hill shading (it lights its own slopes) and no tree
// crowns (it stands real trees in their places), with forest floor where the
// crowns were.
void bake(const phd::World& world, const Sun& sun, int tx0, int ty0, int w, int h, std::uint8_t* rgb, int stride,
          bool flat = true);

// The tree filling a cell, if any, as both views draw it: where it stands,
// its size, its colour this season. From the plant itself.
struct Tree {
    float x = 0, y = 0, radius = 0; // trunk position and crown radius, in cells; radius 0 for none
    float height = 0;               // in cells
    std::uint8_t r = 0, g = 0, b = 0; // canopy colour, with the season
    bool conifer = false;
    bool bare = false;              // deciduous, in winter
    std::uint32_t id = 0;           // the plant
};
Tree tree_in(const phd::World& world, int cx, int cy);

// Hill shading uses the land's true slopes: metres of rise over the 1.5 m
// width of a cell. The tilted view lights by the same rule.
constexpr float shade_metres = float(phd::cell_decimetres) / 10.0f;

// The whole world, for screenshots.
void bake_all(const phd::World& world, std::vector<std::uint8_t>& rgb, int& width, int& height);

// The colour grade for the time of day, multiplied over the terrain.
void day_tint(const phd::World& world, std::uint8_t out[3]);

void biome_rgb(phd::Biome biome, std::uint8_t out[3]);

} // namespace art
