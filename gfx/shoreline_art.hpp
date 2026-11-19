#pragma once

#include "batch.hpp"

namespace phd {
class World;
}

namespace gfx::shoreline {

// World-space bounds of the spring shoreline study, in cells.
constexpr int left = 174;
constexpr int top = 61;
constexpr int extent = 24;
constexpr int texels_per_cell = 32;

// Paints the current ground and shallows for the fixed study frame. Returns
// an empty texture if the world does not contain the whole patch.
Texture make_study_texture(const phd::World& world);

} // namespace gfx::shoreline
