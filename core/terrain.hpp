#pragma once

#include <cstdint>
#include <vector>

namespace phd {

// A cell is about 1.5 m across, so the world is a valley a few hundred metres
// wide. Heights are in decimetres.
constexpr int cell_decimetres = 15;

// What the ground is made of. It sets the ground's colour, how it drains,
// and which biomes can form; plants grow on top of it.
enum class Substrate : std::uint8_t { loam, sand, clay, peat, rock, gravel, count };

// A pond or lake basin: cells that fill to the same spill height.
struct Basin {
    std::int16_t spill;   // dm: the surface when full
    std::int16_t bottom;  // dm: the lowest cell
    std::uint32_t area;   // cells
    std::uint32_t volume; // dm-cells when full
    std::int32_t cell;    // one of its cells
};

// The lie of the land. A valley is made from the seed. The development map
// is laid out by hand for trying things in every kind of place at once: a
// hill with a steep east face in the north-west, an even ramp in the
// north-east, a meadow in the middle with a pad that is one even plane, a wood in
// the west, a lake in the east with a wide shallow shelf, a stream into it
// and out to the sea, and a beach and the sea along the south.
// An arena is a flat training ground: still water, dry ground or both, with
// nothing happening that its settings did not ask for.
enum class Layout : std::uint32_t { valley, development, arena };

// The parts of the development map, for what is planted where.
enum class Zone : std::uint8_t { open, wood, meadow, pad };
Zone development_zone(int x, int y, int width, int height);

// Everything fixed at world creation, regenerated from the seed on load.
struct Terrain {
    int width = 0, height = 0;
    int coast_side = 0;                   // 0 south, 1 north, 2 west, 3 east: where the sea is
    std::vector<std::int16_t> elevation;  // dm above sea level
    std::vector<std::int16_t> filled;     // elevation with every basin filled to its spill
    std::vector<std::int32_t> downstream; // where water goes next, or -1 off the map
    std::vector<std::int32_t> order;      // downstream cells first
    std::vector<std::uint16_t> basin;     // 1-based index into basins, 0 for none
    std::vector<Basin> basins;            // [0] unused
    std::vector<std::uint8_t> channel;    // 1 where enough land drains through for a stream to run
    std::vector<std::uint8_t> ocean;      // 1 where the sea is
    std::vector<std::uint8_t> coast;      // 1 for land or shallows within 2 cells of the sea
    std::vector<std::uint8_t> sea_distance; // cells to the sea, capped at 255
    std::vector<std::uint8_t> substrate;  // Substrate
    std::vector<std::int16_t> hollow;     // dm below the land around it (cold air pools there)
    std::vector<std::int8_t> aspect;      // dm per cell of fall toward the south: sunny slopes > 0
    std::vector<std::uint8_t> slope;      // dm per cell, the steepest fall

    // ocean_percent of the cells end up below sea level.
    void generate(std::uint32_t seed, int width, int height, int ocean_percent, Layout layout = Layout::valley, int arena_depth_dm = 20,
                  int arena_land = 0);

private:
    void flood();
};

} // namespace phd
