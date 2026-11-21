#include <cinttypes>
#include <algorithm>
#include <cstdio>
#include <cstring>
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
    for (int ny = 0; ny < world.nodes_y(); ++ny) {
        for (int nx = 0; nx < world.nodes_x(); ++nx) {
            CHECK(loaded.rain(nx, ny) == world.rain(nx, ny));
            CHECK(loaded.cloud(nx, ny) == world.cloud(nx, ny));
        }
    }
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
// Joined river reaches now flood enclosed low ground and can back up into
// ponds. The new water coverage and depth also change plant growth.
constexpr std::uint64_t golden_seed1_t2400 = 0x46926542209791dcull;

// Animals: bodies from organs, each keeping to where its body can live,
// feeding, and the same on every run and after a save.
// These two controls measure grazing and inheritance from identical founders.
// Predatory founders and feeding from a kill have their own contact tests.
void seed_grazers(phd::World& world) {
    world.seed_animals(56);
    for (std::size_t k = 0; k < world.animals().size(); ++k) {
        if (world.animals()[k].stock == phd::FishStock::large_swimmer) {
            const_cast<phd::brain::Brain&>(world.brain_of(k)) = phd::brain::founder();
        }
    }
}

void test_animals() {
    using namespace phd;
    // What the organs add up to.
    AnimalGenome legs{}, fins{}, wings{};
    legs.gene[agene_size] = 100, legs.gene[agene_legs] = 140, legs.gene[agene_leg_length] = 200;
    fins.gene[agene_size] = 20, fins.gene[agene_fins] = 200, fins.gene[agene_tail] = 170, fins.gene[agene_gills] = 255;
    wings.gene[agene_size] = 38, wings.gene[agene_legs] = 80, wings.gene[agene_wings] = 200;
    const Frame walker = frame(legs), swimmer = frame(fins), flier = frame(wings);
    CHECK(walker.land_speed > walker.water_speed && walker.air_speed == 0 && walker.breathes_air && !walker.breathes_water);
    CHECK(swimmer.water_speed > swimmer.land_speed && swimmer.breathes_water && !swimmer.breathes_air);
    CHECK(flier.air_speed > flier.land_speed);
    AnimalGenome heavy = wings;
    heavy.gene[agene_size] = 200, heavy.gene[agene_build] = 200;
    CHECK(frame(heavy).air_speed == 0); // wings too small for the body do not lift it
    CHECK(frame(heavy).upkeep > flier.upkeep);
    // A newborn is the same body, smaller, and cheaper to keep.
    CHECK(frame(legs, newborn_growth).mass * 8 < walker.mass && frame(legs, newborn_growth).upkeep < walker.upkeep);
    // Unlike bodies cannot breed; near ones can.
    AnimalGenome cousin = legs;
    cousin.gene[agene_size] += 10, cousin.gene[agene_hue] += 20;
    CHECK(genetic_distance(legs, fins) > breeding_distance && genetic_distance(legs, cousin) <= breeding_distance);

    WorldConfig config;
    config.width = 128;
    config.height = 96;
    config.layout = Layout::development;
    World world(config), twin(config);
    const auto empty = world.fingerprint();
    seed_grazers(world);
    seed_grazers(twin);
    // Exercise takeoff even when every flier spawns within sight of grazing.
    // The decision to fly over barren ground is tested in test_brains.
    for (World* start : {&world, &twin}) {
        for (const auto& a : start->animals()) {
            if (frame(a.genome).air_speed) {
                const_cast<Animal&>(a).wants |= wants_fly;
                break;
            }
        }
    }
    CHECK(world.animals().size() >= 40);
    CHECK(world.fingerprint() != empty);
    const auto founders = world.animals().size();
    bool flew = false;
    for (int t = 0; t < 12 * World::ticks_per_day; ++t) {
        world.step();
        twin.step();
        if (t % 60 == 0) {
            for (const Animal& a : world.animals()) {
                flew = flew || Medium(a.medium) == Medium::air;
            }
        }
    }
    CHECK(world.fingerprint() == twin.fingerprint());
    int fish = 0, stranded = 0, fed = 0, walkers = 0, drowning = 0, born = 0, juveniles = 0, parents = 0;
    for (const Animal& a : world.animals()) {
        const Frame f = frame(a.genome, a.growth);
        born += a.mother != 0;
        juveniles += a.growth < 255;
        parents += a.young > 0;
        if (a.mother) {
            CHECK(a.father != 0 && a.father != a.mother && a.growth >= newborn_growth);
        }
        const auto cell = std::size_t(a.y >> 8) * config.width + std::size_t(a.x >> 8);
        const bool wet = world.water(cell) != Water::none;
        if (!f.breathes_air) {
            ++fish;
            stranded += !wet;
        } else if (!f.air_speed) {
            ++walkers;
            fed += a.energy > f.store / 4;
            drowning += a.breath < breath_ticks / 2;
        }
    }
    CHECK(fish > 10 && stranded * 4 <= fish);     // fish keep to the water
    CHECK(walkers > 10 && fed * 4 >= walkers * 3); // grazers find enough to eat
    CHECK(drowning * 4 <= walkers);                // and keep out of deep water, flood or no
    CHECK(flew);
    // Scent and sound: an animal with a nose smells the grazing it stands
    // in, and something has called.
    bool smelled = false;
    for (std::size_t k = 0; k < world.animals().size(); ++k) {
        smelled = smelled || world.senses(k).in[brain::self_input(brain::smell_plant)] > 0;
    }
    bool called = !world.calls().empty();
    for (int t = 0; t < 400 && !called; ++t) {
        world.step();
        twin.step();
        called = !world.calls().empty();
    }
    CHECK(smelled && called);
    // They breed: young are born to two parents, start small and grow.
    CHECK(born > 10 && juveniles > 0 && parents >= 2);
    CHECK(world.animals().size() > founders && world.animals().size() < 1000);

    // A save keeps them, and the world goes on the same from it.
    const auto saved = world.save();
    World loaded(config);
    CHECK(loaded.load(saved.data(), saved.size()));
    CHECK(loaded.animals().size() == world.animals().size());
    for (int t = 0; t < 100; ++t) {
        world.step();
        loaded.step();
    }
    CHECK(loaded.fingerprint() == world.fingerprint());
}

void test_neutral_inheritance() {
    using namespace phd;
    WorldConfig config;
    config.seed = 2;
    config.width = 128;
    config.height = 96;
    config.layout = Layout::development;
    World neutral(config), evolving(config);
    neutral.set_brain_mode(BrainMode::neutral);
    seed_grazers(neutral);
    seed_grazers(evolving);
    const brain::Brain founder = brain::founder();
    bool shared_mutant = false, different_brain = false;
    for (int t = 0; t < 3013; ++t) {
        neutral.step();
        if (different_brain) continue;
        evolving.step();
        CHECK(neutral.animals().size() == evolving.animals().size());
        if (neutral.animals().size() != evolving.animals().size()) return;
        for (std::size_t k = 0; k < neutral.animals().size(); ++k) {
            const auto& child = neutral.animals()[k];
            if (!child.mother) continue;
            const auto& other = evolving.animals()[k];
            CHECK(child.id == other.id && child.mother == other.mother && child.father == other.father);
            CHECK(std::memcmp(child.genome.gene, other.genome.gene, agene_count) == 0);
            // Identical founder brains produce the same early mutations. Once
            // donors carry different weights, a newborn can inherit different wiring.
            const auto& mind = neutral.brain_of(k);
            if (std::memcmp(mind.weight, evolving.brain_of(k).weight, brain::bytes) == 0) {
                shared_mutant |= std::memcmp(mind.weight, founder.weight, brain::bytes) != 0;
            } else {
                different_brain = true;
            }
        }
    }
    CHECK(shared_mutant && different_brain);

    // Between two forage surveys the live plants and the sensed food differ.
    const auto saved = neutral.save();
    World loaded(config);
    CHECK(loaded.load(saved.data(), saved.size()));
    CHECK(loaded.brain_mode() == BrainMode::neutral);
    CHECK(loaded.animals().size() == neutral.animals().size());
    if (loaded.animals().size() != neutral.animals().size()) return;
    for (std::size_t k = 0; k < neutral.animals().size(); ++k) {
        const auto live = neutral.senses(k), restored = loaded.senses(k);
        CHECK(std::memcmp(live.in, restored.in, brain::inputs) == 0);
    }
    for (int t = 0; t < 271; ++t) {
        neutral.step();
        loaded.step();
    }
    CHECK(loaded.save() == neutral.save());
}

// The brain: its arithmetic, how it is inherited, and the founders' reflexes.
void test_brains() {
    using namespace phd;
    using namespace phd::brain;
    const Brain wired = founder();
    Senses calm{};
    calm.in[self_input(self_energy)] = 120;
    CHECK(think(calm, wired).turn == 0 && !think(calm, wired).eat && !think(calm, wired).mate);
    Senses hungry = calm;
    hungry.in[self_input(self_energy)] = 40;
    CHECK(think(hungry, wired).eat);
    Senses grazing = hungry;
    grazing.in[sector_input(right, in_food)] = 80;
    CHECK(think(grazing, wired).turn > 0);
    grazing.in[sector_input(right, in_food)] = 0, grazing.in[sector_input(left, in_food)] = 80;
    CHECK(think(grazing, wired).turn < 0);
    Senses blocked = calm;
    blocked.in[self_input(self_ahead_hazard)] = 127;
    CHECK(think(blocked, wired).turn > 0 && think(blocked, wired).pace < think(calm, wired).pace);
    Senses ready = calm;
    ready.in[self_input(self_drive)] = 127, ready.in[sector_input(far_left, in_mate)] = 100;
    CHECK(think(ready, wired).mate && think(ready, wired).turn < 0);
    CHECK(think(ready, wired).call && !think(calm, wired).call); // ready to mate, it calls
    // It turns toward a mate's scent, and toward a call in a voice like its own.
    Senses scented = calm;
    scented.in[self_input(smell_mate_right)] = 60;
    CHECK(think(scented, wired).turn > 0);
    Senses hearing = calm;
    hearing.in[self_input(heard_right)] = -80;
    CHECK(think(hearing, wired).turn < 0);
    Senses barren = hungry;
    CHECK(think(barren, wired).fly); // nothing to eat in sight: one with wings would take off
    barren.in[self_input(self_food)] = 100;
    CHECK(!think(barren, wired).fly);

    // A child takes each hidden unit whole from one parent or the other.
    Brain a{}, b{}, child{};
    std::fill(std::begin(a.weight), std::end(a.weight), std::int8_t(1));
    std::fill(std::begin(b.weight), std::end(b.weight), std::int8_t(2));
    std::uint32_t state = 12345;
    combine(child, a, b, state);
    int from_b = 0;
    for (int h = 0; h < hidden; ++h) {
        const std::int8_t first = child.weight[h * unit_bytes];
        from_b += first == 2;
        for (int i = 0; i < unit_bytes; ++i) {
            CHECK(child.weight[h * unit_bytes + i] == first);
        }
        for (int o = 0; o < outputs; ++o) {
            CHECK(child.weight[hidden * unit_bytes + o * output_bytes + h] == first);
        }
    }
    CHECK(from_b > 2 && from_b < hidden - 2);
    // Mutation touches about one weight in 128, by a little.
    Brain mutant = wired;
    int changes = 0;
    for (int k = 0; k < 64; ++k) {
        Brain copy = wired;
        changes += mutate(copy, state);
        mutant = copy;
    }
    CHECK(changes > 64 * bytes / 128 / 2 && changes < 64 * bytes / 128 * 2);
    int moved = 0;
    for (int i = 0; i < bytes; ++i) {
        CHECK(std::abs(mutant.weight[i] - wired.weight[i]) <= 4);
        moved += mutant.weight[i] != wired.weight[i];
    }
    CHECK(moved > 0);
    // The shuffled control keeps every weight and loses the wiring.
    Brain scrambled = wired;
    shuffle(scrambled, state);
    long sum = 0, sum_wired = 0;
    for (int i = 0; i < bytes; ++i) {
        sum += scrambled.weight[i], sum_wired += wired.weight[i];
    }
    CHECK(sum == sum_wired && std::memcmp(scrambled.weight, wired.weight, bytes) != 0);
}

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
    test_animals();
    test_brains();
    test_neutral_inheritance();
    test_golden();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all core tests passed\n");
    return 0;
}
