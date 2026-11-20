#pragma once

#include <algorithm>
#include <cstdint>

namespace phd {

// A plant's genes, each a byte. Grass, shrubs, trees and reeds aren't types:
// they are what these add up to.
enum Gene : int {
    gene_height,    // how tall it can grow
    gene_woody,     // soft stems (0) to wood (255); woody plants can become trees
    gene_lifespan,  // annual (0) to centuries (255)
    gene_roots,     // shallow to deep: deep roots reach stored water, at a cost
    gene_drought,   // how little water it needs
    gene_cold,      // how much frost it takes
    gene_shade,     // how well it grows under others
    gene_evergreen, // keeps its leaves through the cold (>= 128) or goes dormant
    gene_seed,      // many small seeds (0) to few large ones (255)
    gene_fruit,     // wind-borne seeds (< 128) or fruit (>= 128)
    gene_flood,     // how deep a flood it stands
    gene_bark,      // fire protection
    gene_serotiny,  // holds seeds until a fire releases them
    gene_runners,   // spreads by runners into neighbouring cells
    gene_leaf,      // leaf colour: yellow-green to blue-green
    gene_flower,    // flower colour
    gene_count
};

struct PlantGenome {
    std::uint8_t gene[gene_count];
};

enum PlantFlag : std::uint8_t {
    plant_dormant = 1,   // leafless or withered in the cold
    plant_flowering = 2, // mature, in spring
    plant_scorched = 4,  // survived a fire
};

// One plant. A cell has four small slots, one per quarter; a woody plant
// takes the whole cell (slot 4) to grow past 1.5 m.
struct Plant {
    std::uint32_t id;      // 0: this pool entry is free
    std::uint32_t lineage; // the founder it descends from
    std::int32_t cell;
    std::uint8_t slot;
    std::uint8_t health;
    std::uint8_t flags;
    std::uint8_t seeds;    // held for a fire to release
    std::uint16_t height;  // dm
    std::uint16_t age;     // growth passes, about 15 a day
    std::int32_t energy;
    PlantGenome genome;
};

constexpr int whole_cell = 4;
constexpr int small_limit_dm = 15; // taller needs the whole cell
constexpr int passes_per_day = 15;
// Where in its cell a tree stands comes from a hash of its id with this, so
// the snag it leaves stands in the same place.
constexpr std::uint32_t tree_place_salt = 0x7f4a7c15u;

inline bool woody(const PlantGenome& g) { return g.gene[gene_woody] >= 128; }
inline bool evergreen(const PlantGenome& g) { return g.gene[gene_evergreen] >= 128; }
inline bool fruiting(const PlantGenome& g) { return g.gene[gene_fruit] >= 128; }

// How tall it can grow, in dm: soft plants to about 2.6 m, woody up to 22 m.
inline int max_height(const PlantGenome& g) {
    return woody(g) ? 4 + g.gene[gene_height] * 216 / 255 : 2 + g.gene[gene_height] / 10;
}

// Lifespan in growth passes: annuals live one year, perennials up to about
// 60 years.
inline int lifespan(const PlantGenome& g) {
    const int days = g.gene[gene_lifespan] < 48 ? 16 : 16 + (g.gene[gene_lifespan] - 48) * 5;
    return days * passes_per_day;
}

// Age at first seeds, in passes.
inline int maturity(const PlantGenome& g) {
    return passes_per_day * (woody(g) ? 10 + g.gene[gene_height] / 12 : 3);
}

// Crown radius in 1/16 cell, from height.
inline int crown16(int height_dm) { return 6 + height_dm / 5; }

// Genetic distance: plants this far apart can't cross.
inline int genetic_distance(const PlantGenome& a, const PlantGenome& b) {
    int d = 0;
    for (int i = 0; i < gene_count; ++i) {
        d += std::abs(int(a.gene[i]) - int(b.gene[i]));
    }
    return d;
}

} // namespace phd
