// petrihd-run: runs a world headless, as fast as the machine allows, and
// prints a census line of key=value pairs every so often.
//
//   petrihd-run seed=1 ticks=3840 every=480 [ocean=20] [climate=90] [seasons=140] [biomes=1]
//               [load=in.phdw] [save=out.phdw] [png=out.png]

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "file.hpp"
#include "png.hpp"
#include "terrain_art.hpp"
#include "world.hpp"

namespace {

void census(const phd::World& world, double tick_us, bool biomes) {
    const auto n = world.cells();
    std::size_t ocean = 0, lake = 0, river = 0, snow = 0, land = 0;
    std::int64_t temperature = 0, soil = 0;
    std::size_t counts[phd::biome_count] = {};
    for (std::size_t i = 0; i < n; ++i) {
        const auto kind = world.water(i);
        ocean += kind == phd::Water::ocean;
        lake += kind == phd::Water::lake;
        river += kind == phd::Water::river;
        snow += world.snow(i) > 20;
        temperature += world.temperature(i);
        if (kind == phd::Water::none) {
            ++land;
            soil += world.soil_water(i);
        }
        ++counts[int(world.biome(i))];
    }
    int present = 0;
    for (const auto count : counts) {
        present += count > 0;
    }
    std::printf("tick=%" PRIu64 " fingerprint=%016" PRIx64
                " day=%" PRIu64 " season=%d ocean_pct=%.1f lake_cells=%zu river_cells=%zu snow_cells=%zu"
                " temp_c=%.1f soil=%.1f fronts=%zu biomes=%d tick_us=%.1f\n",
                world.tick(), world.fingerprint(), world.tick() / phd::World::ticks_per_day + 1, world.season(),
                100.0 * double(ocean) / double(n), lake, river, snow, double(temperature) / double(n) / 10.0,
                land ? double(soil) / double(land) : 0.0, world.fronts().size(), present, tick_us);
    // Plants: how many, how much is forest, what is burning, and how traits
    // differ between the dry and wet thirds of the land.
    std::size_t trees = 0, small = 0, burning = 0, burnt = 0, forest = 0;
    std::uint64_t tree_height = 0;
    // Habitats: 0 dry land, 1 wet land, 2 standing in water.
    std::uint64_t trait_sum[3][4] = {}, trait_n[3] = {};
    for (const auto& p : world.plant_pool()) {
        if (!p.id) {
            continue;
        }
        if (p.slot == phd::whole_cell) {
            ++trees;
            tree_height += p.height;
        } else {
            ++small;
        }
        const auto c = std::size_t(p.cell);
        const int m = world.moisture(c);
        const int band = world.water(c) != phd::Water::none ? 2 : m < 80 ? 0 : m >= 160 ? 1 : -1;
        if (band >= 0) {
            ++trait_n[band];
            trait_sum[band][0] += p.genome.gene[phd::gene_drought];
            trait_sum[band][1] += p.genome.gene[phd::gene_roots];
            trait_sum[band][2] += p.genome.gene[phd::gene_flood];
            trait_sum[band][3] += p.genome.gene[phd::gene_cold];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        burning += world.fire(i) > 0;
        burnt += world.burnt(i) > 0;
        forest += world.water(i) == phd::Water::none && world.canopy_cover(i) >= 140;
    }
    const auto mean = [&](int band, int trait) {
        return trait_n[band] ? double(trait_sum[band][trait]) / double(trait_n[band]) : 0.0;
    };
    std::printf("  plants=%zu trees=%zu tree_height_m=%.1f forest_pct=%.1f burning=%zu burnt=%zu"
                " drought(dry/wet/water)=%.0f/%.0f/%.0f roots=%.0f/%.0f/%.0f flood=%.0f/%.0f/%.0f\n",
                small + trees, trees, trees ? double(tree_height) / double(trees) / 10.0 : 0.0,
                land ? 100.0 * double(forest) / double(land) : 0.0, burning, burnt, mean(0, 0), mean(1, 0), mean(2, 0),
                mean(0, 1), mean(1, 1), mean(2, 1), mean(0, 2), mean(1, 2), mean(2, 2));
    if (biomes) {
        for (int b = 0; b < phd::biome_count; ++b) {
            std::printf("  %-17s %6.2f%%\n", phd::biome_name(phd::Biome(b)), 100.0 * double(counts[b]) / double(n));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    phd::WorldConfig config;
    std::uint64_t ticks = 3840, every = 480;
    bool biomes = false;
    std::string load, save, png;
    for (int i = 1; i < argc; ++i) {
        const char* eq = std::strchr(argv[i], '=');
        if (!eq) {
            std::fprintf(stderr, "expected key=value, got %s\n", argv[i]);
            return 2;
        }
        const std::string key(argv[i], std::size_t(eq - argv[i])), value(eq + 1);
        if (key == "seed") {
            config.seed = std::uint32_t(std::strtoul(value.c_str(), nullptr, 0));
        } else if (key == "ticks") {
            ticks = std::strtoull(value.c_str(), nullptr, 0);
        } else if (key == "every") {
            every = std::strtoull(value.c_str(), nullptr, 0);
        } else if (key == "ocean") {
            config.ocean_percent = std::atoi(value.c_str());
        } else if (key == "climate") {
            config.climate = std::atoi(value.c_str());
        } else if (key == "seasons") {
            config.seasons = std::atoi(value.c_str());
        } else if (key == "biomes") {
            biomes = value != "0";
        } else if (key == "load") {
            load = value;
        } else if (key == "save") {
            save = value;
        } else if (key == "png") {
            png = value;
        } else {
            std::fprintf(stderr, "unknown key %s\n", key.c_str());
            return 2;
        }
    }

    phd::World world(config);
    if (!load.empty()) {
        std::vector<std::uint8_t> data;
        if (!phd::read_file(load, data) || !world.load(data.data(), data.size())) {
            std::fprintf(stderr, "could not load %s\n", load.c_str());
            return 1;
        }
    }
    const auto end = world.tick() + ticks;
    auto started = std::chrono::steady_clock::now();
    std::uint64_t since = 0;
    while (world.tick() < end) {
        world.step();
        ++since;
        if (every && (world.tick() % every == 0 || world.tick() == end)) {
            const auto now = std::chrono::steady_clock::now();
            census(world, std::chrono::duration<double, std::micro>(now - started).count() / double(since), biomes);
            started = now;
            since = 0;
        }
    }
    if (!every) {
        census(world, 0, biomes);
    }
    if (!save.empty() && !phd::write_file_atomic(save, world.save())) {
        std::fprintf(stderr, "could not save %s\n", save.c_str());
        return 1;
    }
    if (!png.empty()) {
        std::vector<std::uint8_t> rgb;
        int w = 0, h = 0;
        art::bake_all(world, rgb, w, h);
        if (!phd::write_png(png, w, h, rgb)) {
            std::fprintf(stderr, "could not write %s\n", png.c_str());
            return 1;
        }
    }
    return 0;
}
