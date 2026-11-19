#pragma once

#include <cstdint>

namespace phd {

// Biomes are derived per cell from temperature, moisture, height and water,
// so they move with the climate. They are for plants, cover and the display;
// nothing is painted on.
enum class Biome : std::uint8_t {
    // cold
    ice, tundra, taiga, alpine_meadow, bare_peaks,
    // temperate
    deciduous_forest, grassland, heath, scrubland,
    // hot
    rainforest, savanna, dry_woodland, desert, salt_flat,
    // wet
    marsh, swamp, mangrove, bog, riverbank,
    // water
    beach, tidal_shallows, reef_shallows, lake, river, open_sea, deep_sea,
    count
};

constexpr int biome_count = int(Biome::count);

inline const char* biome_name(Biome biome) {
    static const char* names[] = {
        "ICE", "TUNDRA", "TAIGA", "ALPINE MEADOW", "BARE PEAKS",
        "DECIDUOUS FOREST", "GRASSLAND", "HEATH", "SCRUBLAND",
        "RAINFOREST", "SAVANNA", "DRY WOODLAND", "DESERT", "SALT FLAT",
        "MARSH", "SWAMP", "MANGROVE", "BOG", "RIVERBANK",
        "BEACH", "TIDAL SHALLOWS", "REEF SHALLOWS", "LAKE", "RIVER", "OPEN SEA", "THE DEEP",
    };
    return names[int(biome)];
}

} // namespace phd
