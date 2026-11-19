#include <cinttypes>
#include <algorithm>
#include <cstdio>
#include <vector>

#include "file.hpp"
#include "fnv.hpp"
#include "png.hpp"
#include "random.hpp"
#include "world.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

phd::World run(std::uint32_t seed, int ticks) {
    phd::WorldConfig config;
    config.seed = seed;
    phd::World world(config);
    for (int i = 0; i < ticks; ++i) {
        world.step();
    }
    return world;
}

void test_random_matches_petri() {
    // Petri's World::random with rng_ = 0: the first outputs, which Petri's
    // own runs depended on.
    phd::Random random(0);
    std::uint32_t state = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint32_t value = (state += 0x9e3779b9u);
        value = (value ^ (value >> 16)) * 0x85ebca6bu;
        value = (value ^ (value >> 13)) * 0xc2b2ae35u;
        CHECK(random.next() == (value ^ (value >> 16)));
    }
    phd::Random bounded(7);
    for (int i = 0; i < 1000; ++i) {
        CHECK(bounded.below(10) < 10);
    }
}

void test_fnv_known_values() {
    // Published FNV-1a 64 test vectors.
    phd::Fnv1a empty;
    CHECK(empty.value() == 0xcbf29ce484222325ull);
    phd::Fnv1a a;
    a.add(std::uint8_t('a'));
    CHECK(a.value() == 0xaf63dc4c8601ec8cull);
    // Integers go in least significant byte first.
    phd::Fnv1a wide, bytes;
    wide.add(std::uint16_t(0x6261));
    bytes.add(std::uint8_t('a'));
    bytes.add(std::uint8_t('b'));
    CHECK(wide.value() == bytes.value());
}

void test_determinism() {
    CHECK(run(1, 500).fingerprint() == run(1, 500).fingerprint());
    CHECK(run(1, 500).fingerprint() != run(2, 500).fingerprint());
    CHECK(run(0, 50).fingerprint() == run(0, 50).fingerprint());
}

void test_land_and_water() {
    phd::WorldConfig config;
    config.seed = 4;
    phd::World world(config);
    const auto& terrain = world.terrain();
    const auto n = world.cells();
    std::size_t ocean = 0, rivers = 0;
    for (std::size_t i = 0; i < n; ++i) {
        ocean += terrain.ocean[i];
        rivers += world.water(i) == phd::Water::river;
        // Water never flows uphill over the filled surface.
        const int down = terrain.downstream[i];
        if (down >= 0) {
            CHECK(terrain.filled[std::size_t(down)] <= terrain.filled[i]);
        }
        CHECK(terrain.filled[i] >= terrain.elevation[i]);
    }
    const auto percent = 100 * ocean / n;
    CHECK(percent >= 8 && percent <= 26); // asked for 20; the sea stops short of flooding an inland basin
    CHECK(rivers > 100);
    CHECK(terrain.order.size() == n);
    // Over the year, slopes facing the sun are warmer than slopes facing
    // away; on a still night (the world starts at midnight) cold air pools in
    // the hollows.
    std::int64_t sunny = 0, shaded = 0, hollows = 0, rises = 0;
    std::size_t sunny_n = 0, shaded_n = 0, hollows_n = 0, rises_n = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (terrain.ocean[i]) {
            continue;
        }
        const int t = world.mean_temperature(i);
        if (terrain.aspect[i] > 8) {
            sunny += t;
            ++sunny_n;
        } else if (terrain.aspect[i] < -8) {
            shaded += t;
            ++shaded_n;
        }
        if (terrain.hollow[i] > 30) {
            hollows += world.temperature(i);
            ++hollows_n;
        } else if (terrain.hollow[i] < -30) {
            rises += world.temperature(i);
            ++rises_n;
        }
    }
    CHECK(sunny_n && shaded_n && hollows_n && rises_n);
    CHECK(sunny * std::int64_t(shaded_n) > shaded * std::int64_t(sunny_n));
    CHECK(hollows * std::int64_t(rises_n) < rises * std::int64_t(hollows_n));

    // Through a year: snow in the northern winter, rain somewhere, light
    // that comes and goes each day.
    std::size_t winter_snow = 0;
    int darkest = 255, brightest = 0;
    bool rained = false;
    for (int t = 0; t < phd::World::ticks_per_year; ++t) {
        world.step();
        if (world.season() == 3 && t % 240 == 0) {
            for (std::size_t i = 0; i < n / 4; ++i) {
                winter_snow += world.snow(i) > 20;
            }
        }
        for (int nx = 0; nx < world.nodes_x() && !rained; ++nx) {
            for (int ny = 0; ny < world.nodes_y(); ++ny) {
                rained = rained || world.rain(nx, ny) > 0;
            }
        }
        darkest = std::min(darkest, world.daylight());
        brightest = std::max(brightest, world.daylight());
    }
    CHECK(winter_snow > 0);
    CHECK(rained);
    CHECK(darkest == 0 && brightest > 200);
}

void test_plants_and_fire() {
    phd::WorldConfig config;
    config.seed = 6;
    phd::World world(config);
    const auto start = world.plant_count();
    CHECK(start > 50000);
    // Every occupied slot points at a live plant in that cell, and trees fill
    // all four.
    for (std::size_t c = 0; c < world.cells(); ++c) {
        for (int s = 0; s < 4; ++s) {
            if (const phd::Plant* p = world.occupant(c, s)) {
                CHECK(p->id != 0 && std::size_t(p->cell) == c);
                CHECK(p->slot == s || p->slot == phd::whole_cell);
            }
        }
    }
    // Through two years the valley stays green.
    for (int t = 0; t < 2 * phd::World::ticks_per_year; ++t) {
        world.step();
    }
    CHECK(world.plant_count() > start / 3);
    std::size_t trees = 0;
    for (const auto& p : world.plant_pool()) {
        trees += p.id && p.slot == phd::whole_cell;
    }
    CHECK(trees > 1000);

    // A fire set in the driest grassy cell burns it and its plants.
    std::size_t target = 0;
    int driest = 256;
    for (std::size_t c = 0; c < world.cells(); ++c) {
        if (world.water(c) == phd::Water::none && world.occupant(c, 0) && !world.tree(c) && world.soil_water(c) < driest) {
            driest = world.soil_water(c);
            target = c;
        }
    }
    CHECK(world.ignite(target));
    for (int t = 0; t < 40; ++t) {
        world.step();
    }
    CHECK(world.burnt(target) > 0);
    CHECK(world.occupant(target, 0) == nullptr || world.occupant(target, 0)->age < 40);
}

void test_save_round_trip() {
    auto world = run(5, 300);
    const auto saved = world.save();
    phd::World loaded(phd::WorldConfig{});
    CHECK(loaded.load(saved.data(), saved.size()));
    CHECK(loaded.fingerprint() == world.fingerprint());
    for (int i = 0; i < 300; ++i) {
        world.step();
        loaded.step();
    }
    CHECK(loaded.fingerprint() == world.fingerprint());
    CHECK(loaded.fingerprint() == run(5, 600).fingerprint());

    // A damaged save is refused, and the world is left as it was.
    auto damaged = saved;
    damaged[damaged.size() / 2] ^= 1;
    const auto before = loaded.fingerprint();
    CHECK(!loaded.load(damaged.data(), damaged.size()));
    CHECK(!loaded.load(saved.data(), saved.size() - 1));
    CHECK(loaded.fingerprint() == before);
}

void test_files() {
    const std::string path = "core_test_save.phdw";
    const auto data = run(3, 10).save();
    CHECK(phd::write_file_atomic(path, data));
    std::vector<std::uint8_t> read;
    CHECK(phd::read_file(path, read));
    CHECK(read == data);
    std::remove(path.c_str());

    std::vector<std::uint8_t> rgb(4 * 3 * 3, 200);
    CHECK(phd::write_png("core_test.png", 4, 3, rgb));
    CHECK(phd::read_file("core_test.png", read));
    CHECK(read.size() > 8 && read[1] == 'P' && read[2] == 'N' && read[3] == 'G');
    std::remove("core_test.png");
}

// The fingerprint after 2,400 ticks on seed 1. It must match on every
// platform and build (x86, aarch64, NEON or not). If a change to the world is
// meant to change it, update it in the same commit and say why.
constexpr std::uint64_t golden_seed1_t2400 = 0x6d5dc4b7a824c293ull;

void test_golden() {
    const auto value = run(1, 2400).fingerprint();
    std::printf("seed=1 tick=2400 fingerprint=%016" PRIx64 "\n", value);
    CHECK(value == golden_seed1_t2400);
}

} // namespace

int main() {
    test_random_matches_petri();
    test_fnv_known_values();
    test_determinism();
    test_land_and_water();
    test_plants_and_fire();
    test_save_round_trip();
    test_files();
    test_golden();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all core tests passed\n");
    return 0;
}
