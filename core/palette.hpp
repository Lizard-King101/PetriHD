#pragma once

#include <cstdint>
#include <vector>

#include "world.hpp"

namespace phd {

// Milestone 0's view of the world: the nutrient field as a soil-to-moss ramp.
// Shared by the window and headless screenshots so both show the same thing.
inline void world_rgb(const World& world, std::vector<std::uint8_t>& rgb) {
    const auto n = std::size_t(world.width()) * world.height();
    rgb.resize(n * 3);
    const auto* field = world.nutrients();
    for (std::size_t i = 0; i < n; ++i) {
        const int v = field[i];
        rgb[i * 3 + 0] = static_cast<std::uint8_t>(70 + (v * 10) / 255);
        rgb[i * 3 + 1] = static_cast<std::uint8_t>(52 + (v * 100) / 255);
        rgb[i * 3 + 2] = static_cast<std::uint8_t>(34 + (v * 20) / 255);
    }
}

} // namespace phd
