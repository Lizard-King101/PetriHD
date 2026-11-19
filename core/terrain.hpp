#pragma once

#include <cstdint>
#include <vector>

namespace phd {

// A lake basin: cells that fill to the same spill height.
struct Basin {
    std::int16_t spill;   // metres: the surface when full
    std::int16_t bottom;  // metres: the lowest cell
    std::uint32_t area;   // cells
    std::uint32_t volume; // metre-cells when full
    std::int32_t cell;    // one of its cells
};

// Everything fixed at world creation, regenerated from the seed on load.
struct Terrain {
    int width = 0, height = 0;
    std::vector<std::int16_t> elevation; // metres above sea level
    std::vector<std::int16_t> filled;    // elevation with every basin filled to its spill
    std::vector<std::int32_t> downstream;// where water goes next, or -1 off the map
    std::vector<std::int32_t> order;     // downstream cells first
    std::vector<std::uint16_t> basin;    // 1-based index into basins, 0 for none
    std::vector<Basin> basins;           // [0] unused
    std::vector<std::uint8_t> ocean;     // 1 where the sea is
    std::vector<std::uint8_t> coast;     // 1 for land or water within 2 cells of the sea
    std::vector<std::uint8_t> sea_distance; // cells to the sea, capped at 255

    // ocean_percent of the cells end up below sea level.
    void generate(std::uint32_t seed, int width, int height, int ocean_percent);
};

} // namespace phd
