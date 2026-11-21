#include "world.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {
using namespace phd;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

// Stage a contact in a mutable test world before it builds its spatial index.
// The production accessors give observers a read-only view.
Animal& animal(World& world, std::uint32_t id) {
    const auto& herd = world.animals();
    const auto at = std::find_if(herd.begin(), herd.end(), [id](const Animal& a) { return a.id == id; });
    CHECK(at != herd.end());
    return const_cast<Animal&>(*at);
}

void hold_outputs(World& world, std::uint32_t id, bool attack, bool mate) {
    Animal& a = animal(world, id);
    auto& mind = const_cast<brain::Brain&>(world.brain_of(std::size_t(&a - world.animals().data())));
    mind = {};
    auto* biases = mind.weight + brain::hidden * brain::unit_bytes + brain::hidden;
    biases[brain::out_pace * brain::output_bytes] = -127;
    biases[brain::out_eat * brain::output_bytes] = 127;
    biases[brain::out_attack * brain::output_bytes] = attack ? 127 : 0;
    biases[brain::out_mate * brain::output_bytes] = mate ? 127 : 0;
    a.turn = 0;
    a.pace = 0;
}

World lake_contact() {
    WorldConfig config;
    config.width = 128;
    config.height = 96;
    config.layout = Layout::development;
    World world(config);
    world.seed_animals(14);
    Animal& hunter = animal(world, 7);
    Animal& fish = animal(world, 5);
    CHECK(hunter.stock == FishStock::large_swimmer && fish.stock == FishStock::small_fish);
    for (std::size_t c = 0; c < world.cells(); ++c) {
        if (world.water(c) != Water::lake || world.water_depth_dm(c) < 15) continue;
        const int x = int(c % world.width()) * 256 + 128, y = int(c / world.width()) * 256 + 128;
        if (std::any_of(world.animals().begin(), world.animals().end(), [&](const Animal& a) {
                return std::abs(x - a.x) < 768 && std::abs(y - a.y) < 768;
            })) continue;
        hunter.x = x; hunter.y = y; hunter.heading = 0;
        fish.x = x + 24; fish.y = y; fish.heading = 64;
        hunter.genome.gene[agene_plant_gut] = 0;
        hunter.energy = frame(hunter.genome).store / 4;
        hold_outputs(world, 7, true, false);
        hold_outputs(world, 5, false, false);
        return world;
    }
    throw std::runtime_error("no open water for contact fixture");
}

void test_founder() {
    using namespace brain;
    Senses s{};
    const auto hunter = founder(true);
    s.in[self_input(self_energy)] = 30;
    CHECK(!think(s, hunter).attack);
    s.in[self_input(self_mouth_size)] = -80;
    CHECK(think(s, hunter).attack && !think(s, founder()).attack);
    s.in[self_input(self_energy)] = 127;
    CHECK(!think(s, hunter).attack);
    const auto calm = think(s, founder());
    s.in[self_input(self_pain)] = 100;
    s.in[self_input(pain_right)] = 100;
    const auto hurt = think(s, founder());
    CHECK(hurt.turn < calm.turn && hurt.pace > calm.pace);
}

void test_nursery_seeding() {
    WorldConfig c;
    c.width = 128; c.height = 96; c.layout = Layout::development;
    World original(c), nursery(c), mixed(c);
    original.seed_animals(56);
    auto candidate = brain::founder(true);
    candidate.weight[0] = 42;
    nursery.seed_animals(56, candidate);
    auto other = candidate;
    other.weight[0] = -37;
    const std::vector<brain::Brain> pool{candidate, other, brain::founder(true), brain::founder(true)};
    mixed.seed_animals(56, pool);
    const auto data = nursery.save();
    CHECK(nursery.load(data.data(), data.size()));
    CHECK(original.animals().size() == nursery.animals().size() && mixed.animals().size() == nursery.animals().size());
    int large = 0;
    for (std::size_t i = 0; i < nursery.animals().size(); ++i) {
        const auto& a = original.animals()[i];
        const auto& b = nursery.animals()[i];
        CHECK(a.id == b.id && a.x == b.x && a.y == b.y && a.heading == b.heading && a.energy == b.energy);
        CHECK(a.stock == b.stock && genetic_distance(a.genome, b.genome) == 0);
        const bool swimmer = a.stock == FishStock::large_swimmer;
        const auto& expected = swimmer ? candidate : original.brain_of(i);
        CHECK(std::equal(expected.weight, expected.weight + brain::bytes, nursery.brain_of(i).weight));
        const auto& m = mixed.animals()[i];
        CHECK(a.id == m.id && a.x == m.x && a.y == m.y && a.heading == m.heading && a.energy == m.energy);
        CHECK(a.stock == m.stock && genetic_distance(a.genome, m.genome) == 0);
        const auto& mixed_brain = swimmer ? pool[large % pool.size()] : original.brain_of(i);
        CHECK(std::equal(mixed_brain.weight, mixed_brain.weight + brain::bytes, mixed.brain_of(i).weight));
        large += swimmer;
    }
    CHECK(large == 8);
}

void test_partner_readiness_sense() {
    using namespace brain;
    World world = lake_contact();
    auto& observer = animal(world, 7);
    auto& sender = animal(world, 14);
    sender.genome = observer.genome;
    sender.x = observer.x + 256; sender.y = observer.y;
    sender.drive = mating_drive; // only the sender contributes to the initial scent field
    hold_outputs(world, 7, false, false);
    hold_outputs(world, 14, false, false);
    world.step(); // build the spatial index and lay scent before testing the thresholds

    auto& me = animal(world, 7);
    auto& them = animal(world, 14);
    me.heading = 0;
    const auto index = std::size_t(&me - world.animals().data());
    constexpr int visible = sector_input(ahead, in_mate);
    constexpr int scent = self_input(smell_mate);
    CHECK(!(them.wants & wants_mate));
    me.drive = mating_drive - 1;
    CHECK(world.senses(index).in[visible] == 0 && world.senses(index).in[scent] == 0);
    me.drive = mating_drive;
    CHECK(world.senses(index).in[visible] == 127 && world.senses(index).in[scent] > 0);
    them.drive = mating_drive - 1;
    CHECK(world.senses(index).in[visible] == 0 && world.senses(index).in[scent] > 0);
    them.drive = mating_drive;
    --them.growth;
    CHECK(world.senses(index).in[visible] == 0);
    ++them.growth;
    them.genome.gene[agene_legs] = 255;
    CHECK(world.senses(index).in[visible] == 0);
}

void test_hungry_drive_loss() {
    for (const int loss : {16, 8, 4}) {
        WorldConfig c;
        c.width = 128; c.height = 96; c.layout = Layout::development;
        c.hungry_drive_loss = std::uint8_t(loss);
        World world(c);
        world.seed_animals(7);
        hold_outputs(world, 7, false, false);
        auto& swimmer = animal(world, 7);
        swimmer.genome.gene[agene_plant_gut] = 0;
        const int store = frame(swimmer.genome).store;
        swimmer.drive = 204;
        swimmer.energy = store / 4;
        const auto data = world.save();
        CHECK(data[4] == (loss == 16 ? 14 : 15));
        World replay(WorldConfig{});
        CHECK(replay.load(data.data(), data.size()) && replay.config().hungry_drive_loss == loss);
        CHECK(replay.fingerprint() == world.fingerprint());
        for (int t = 0; t < animal_stagger; ++t) { world.step(); replay.step(); }
        CHECK(animal(world, 7).drive == 204 - loss && world.save() == replay.save());
        animal(world, 7).energy = store * 9 / 16;
        for (int t = 0; t < animal_stagger; ++t) world.step();
        CHECK(animal(world, 7).drive == 204 - loss);
        animal(world, 7).energy = store;
        for (int t = 0; t < animal_stagger; ++t) world.step();
        CHECK(animal(world, 7).drive == 204 - loss + 8);
    }
}

void test_nursery_pond() {
    WorldConfig c;
    c.width = 128; c.height = 96; c.layout = Layout::development;
    World lake(c), pond(c), stocked(c);
    const std::vector<brain::Brain> pool{brain::founder(true), brain::founder()};
    lake.seed_animals(28, pool);
    pond.seed_swimmer_pond(pool, false);
    stocked.seed_swimmer_pond(pool, true);
    CHECK(pond.animals().size() == 4 && stocked.animals().size() == 4);
    CHECK(lake.plant_count() == pond.plant_count() && stocked.plant_count() > pond.plant_count());
    for (std::size_t i = 0; i < pond.animals().size(); ++i) {
        const auto& a = pond.animals()[i];
        for (auto* world : {&lake, &stocked}) {
            const auto& b = animal(*world, a.id);
            CHECK(a.x == b.x && a.y == b.y && a.energy == b.energy && a.drive == b.drive && a.heading == b.heading);
            CHECK(a.stock == FishStock::large_swimmer && genetic_distance(a.genome, b.genome) == 0);
            const auto index = std::size_t(&b - world->animals().data());
            CHECK(std::equal(pond.brain_of(i).weight, pond.brain_of(i).weight + brain::bytes, world->brain_of(index).weight));
        }
        const auto cell = std::size_t(a.y >> 8) * pond.width() + (a.x >> 8);
        CHECK(stocked.occupant(cell, 3) && stocked.occupant(cell, 3)->height <= small_limit_dm);
    }
    const auto data = stocked.save();
    CHECK(pond.load(data.data(), data.size()));
    for (int i = 0; i < 32; ++i) { pond.step(); stocked.step(); }
    CHECK(pond.save() == stocked.save());
}

void test_lesson_one_pond() {
    WorldConfig c;
    c.layout = Layout::training_pond; c.width = 18; c.height = 10; c.climate = 175;
    c.pond_depth_dm = 24; c.pond_light = 181; c.pond_food = 520;
    World pond(c);
    pond.seed_training_pair(brain::founder(true), brain::founder(true));
    auto& left = animal(pond, 1);
    const auto& right = animal(pond, 2);
    CHECK(pond.animals().size() == 2 && !left.drive && !right.drive && left.health == 255 && right.health == 255);
    CHECK(left.energy == frame(left.genome).store * 60 / 100 && right.energy == frame(right.genome).store * 60 / 100);
    CHECK(right.x - left.x == 256 && left.y == right.y && genetic_distance(left.genome, right.genome) <= breeding_distance);
    for (int y = 1; y < c.height - 1; ++y) for (int x = 1; x < c.width - 1; ++x) {
        const int cell = y * c.width + x;
        CHECK(pond.water(cell) == Water::lake && pond.water_depth_dm(cell) == 24);
        CHECK(pond.occupant(cell, 0) && pond.occupant(cell, 0)->energy == 528);
    }
    hold_outputs(pond, 1, false, true);
    hold_outputs(pond, 2, false, true);
    const auto cell = std::size_t(left.y >> 8) * c.width + (left.x >> 8);
    for (int i = 0; i < 37; ++i) pond.step();
    auto saved = pond.save();
    World replay(c);
    CHECK(saved[4] == 16 && replay.load(saved.data(), saved.size()) && saved == replay.save());
    int birth_tick = 0;
    for (int i = 0; i < 960 && !birth_tick; ++i) {
        PondTick pass;
        pond.step(&pass); replay.step();
        CHECK(pond.daylight() == 181 && pond.temperature(cell) == 175 && pond.water_depth_dm(cell) == 24);
        CHECK(pond.fronts().empty() && !pond.wind_speed() && !pond.flow(cell) && !pond.rain_at(8, 5));
        CHECK(pond.occupant(cell, 0)->energy > 8);
        if (pond.animals().size() > 2) {
            CHECK(pass.near_pair_drive >= mating_drive && pass.birth_drives[0] >= mating_drive && pass.birth_drives[1] >= mating_drive);
            CHECK(!animal(pond, 1).drive && !animal(pond, 2).drive);
            birth_tick = int(pond.tick());
        }
    }
    CHECK(birth_tick >= 25 * animal_stagger - animal_stagger && pond.save() == replay.save());
}

void test_patch_pond() {
    WorldConfig c;
    c.layout = Layout::training_pond; c.width = 18; c.height = 10; c.seed = 7321;
    World uniform(c);
    c.pond_food = 4800;
    const std::vector<PondPatch> beds{{2, 4, 3, 2}, {8, 4, 2, 2}, {13, 4, 3, 2}};
    World pond(c, beds);
    std::int64_t sparse_food = 0, uniform_food = 0;
    for (std::size_t cell = 0; cell < pond.cells(); ++cell) {
        const int x = int(cell) % c.width, y = int(cell) / c.width;
        const bool stocked = (y == 4 || y == 5) && ((x >= 2 && x <= 4) || x == 8 || x == 9 || (x >= 13 && x <= 15));
        const auto* reed = pond.occupant(cell, 0);
        CHECK(bool(reed) == stocked && pond.water_depth_dm(cell) == uniform.water_depth_dm(cell));
        if (reed) sparse_food += reed->energy - 8;
        if (const auto* source = uniform.occupant(cell, 0)) uniform_food += source->energy - 8;
    }
    CHECK(pond.plant_count() == 16 && uniform_food == 76800 && sparse_food == uniform_food);
    pond.seed_training_pair(brain::founder(true), brain::founder(true));
    uniform.seed_training_pair(brain::founder(true), brain::founder(true));
    for (unsigned id : {1u, 2u}) {
        const auto& a = animal(pond, id);
        const auto& b = animal(uniform, id);
        CHECK(a.x == b.x && a.y == b.y && a.heading == b.heading && a.energy == b.energy);
        CHECK(pond.occupant((a.y >> 8) * c.width + (a.x >> 8), 0));
    }
    const auto side = 4 * c.width + 2;
    const_cast<Plant*>(pond.occupant(side, 0))->energy = 8;
    pond.step();
    CHECK(pond.occupant(side, 0)->energy == c.pond_food + 8);
    const auto saved = pond.save();
    CHECK(saved[4] == 17 && uniform.load(saved.data(), saved.size()) && uniform.save() == saved);
    for (int i = 0; i < 500; ++i) { pond.step(); uniform.step(); }
    CHECK(pond.plant_count() == 16 && uniform.save() == pond.save());
    for (const auto bad : {PondPatch{0, 2, 1, 1}, PondPatch{2, 3, 0, 1}, beds.front()}) {
        bool rejected = false;
        try { World invalid(c, {beds.front(), bad}); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
}

void test_pond_energy_charges() {
    WorldConfig c;
    c.layout = Layout::training_pond; c.width = 18; c.height = 10;
    World pond(c);
    brain::Brain active{};
    auto* bias = active.weight + brain::hidden * brain::unit_bytes + brain::hidden;
    for (const auto output : {brain::out_pace, brain::out_call, brain::out_attack}) bias[output * brain::output_bytes] = 127;
    pond.seed_training_pair(active, active, 60, 8);
    bool moved = false, called = false, struck = false, metabolized = false;
    for (int tick = 0; tick < 2 * animal_stagger; ++tick) {
        const std::array<int, 2> before{animal(pond, 1).energy, animal(pond, 2).energy};
        PondTick pass;
        pond.step(&pass);
        for (unsigned i = 0; i < 2; ++i) {
            const auto& s = pass.founders[i];
            CHECK(before[i] - animal(pond, i + 1).energy == s.movement_cost + s.call_cost + s.strike_cost + s.upkeep_cost);
            moved |= s.movement_cost > 0; called |= s.call_cost > 0;
            struck |= s.strike_cost > 0; metabolized |= s.upkeep_cost > 0;
        }
    }
    CHECK(moved && called && struck && metabolized);
}

void test_periodic_pond_refill() {
    WorldConfig c;
    c.layout = Layout::training_pond; c.width = 18; c.height = 10; c.pond_refill_ticks = 8;
    const std::vector<PondPatch> beds{{8, 4, 2, 2}};
    World pond(c, beds);
    const auto cell = std::size_t(4 * c.width + 8);
    const_cast<Plant*>(pond.occupant(cell, 0))->energy = 8;
    for (int tick = 1; tick <= 6; ++tick) { pond.step(); CHECK(pond.occupant(cell, 0)->energy == 8); }
    const auto saved = pond.save();
    World replay(c);
    CHECK(saved[4] == 18 && replay.load(saved.data(), saved.size()) && saved == replay.save());
    pond.step(); replay.step();
    CHECK(replay.occupant(cell, 0)->energy == 8);
    pond.step(); replay.step();
    CHECK(replay.occupant(cell, 0)->energy == 8 + c.pond_food && pond.save() == replay.save());
    CHECK(!replay.occupant(cell - 1, 0));
}

void test_contact() {
    World world = lake_contact();
    for (int miss = 0; miss < 3; ++miss) {
        World probe = world;
        auto& prey = animal(probe, 5);
        if (miss == 0) prey.x += 512;
        if (miss == 1) prey.x -= 48;
        if (miss == 2) prey.medium = std::uint8_t(Medium::land);
        const auto energy = animal(probe, 7).energy;
        probe.step();
        CHECK(animal(probe, 5).health == 255 && !animal(probe, 5).pain);
        CHECK(animal(probe, 7).energy < energy);
    }
    world.step();
    CHECK(animal(world, 5).health < 255 && animal(world, 5).pain > 0);
    const auto index = std::size_t(&animal(world, 5) - world.animals().data());
    const auto sensed = world.senses(index);
    CHECK(sensed.in[brain::self_input(brain::pain_right)] > 0);
    CHECK(sensed.in[brain::self_input(brain::pain_ahead)] == 0);
    const auto initial_pain = animal(world, 5).pain;
    world.step();
    CHECK(animal(world, 5).pain < initial_pain);
    const auto wounded = world.save();
    World replay = lake_contact();
    CHECK(replay.load(wounded.data(), wounded.size()));
    while (!world.hunts().kills && world.tick() < 24) {
        world.step();
        replay.step();
    }
    CHECK(world.hunts().kills == 1 && world.hunts().small_fish_killed == 1);
    CHECK(world.hunts().own_kill_energy > 0 && !world.hunts().scavenged_energy);
    CHECK(world.carcasses().size() == 1 && world.carcasses().front().killer == 7);
    CHECK(replay.save() == world.save());
    const auto remains = world.save();
    CHECK(replay.load(remains.data(), remains.size()));
    world.step();
    replay.step();
    CHECK(replay.save() == world.save());

    // A second compatible adult supplies a mate. Only one parent has fed on its kill.
    auto& hunter = animal(world, 7);
    auto& mate = animal(world, 14);
    mate.genome = hunter.genome;
    mate.x = hunter.x - 32; mate.y = hunter.y;
    hunter.energy = mate.energy = frame(hunter.genome).store;
    hunter.drive = mate.drive = 255;
    hold_outputs(world, 7, false, true);
    hold_outputs(world, 14, false, true);
    for (int i = 0; i < 40; ++i) world.step();
    CHECK(world.hunts().breeders == 1 && world.hunts().young > 0);
    CHECK(world.hunts().generations == 1 && world.hunts().last_birth_tick > 9);
    CHECK(animal(world, 7).hunting == HuntHistory::bred_after_kill);
}

void test_scavenging_and_decay() {
    World world = lake_contact();
    hold_outputs(world, 7, false, false);
    auto& prey = animal(world, 5);
    prey.genome.gene[agene_gills] = 0;
    prey.health = 1;
    prey.breath = 0;
    const int reserves = prey.energy + frame(prey.genome).mass75 / 4;
    world.step();
    CHECK(world.carcasses().size() == 1 && world.carcasses()[0].killer == 0);
    CHECK(world.carcasses()[0].energy < reserves);
    CHECK(world.hunts().scavenged_energy > 0 && world.hunts().scavenged_energy <= std::uint64_t(reserves - world.carcasses()[0].energy));
    CHECK(!world.hunts().kills && !world.hunts().own_kill_energy && animal(world, 7).hunting == HuntHistory::none);

    // Natural remains keep decaying even after the last living animal is gone.
    for (const auto& living : world.animals()) {
        auto& a = animal(world, living.id);
        a.genome.gene[agene_gills] = 0;
        a.x = world.carcasses()[0].x; a.y = world.carcasses()[0].y;
        a.health = 1; a.breath = 0;
    }
    world.step();
    CHECK(world.animals().empty() && !world.carcasses().empty());
    const auto dead = world.save();
    World replay = lake_contact();
    CHECK(replay.load(dead.data(), dead.size()));
    for (int t = 0; t < 2 * World::ticks_per_year && !world.carcasses().empty(); ++t) {
        world.step();
        replay.step();
    }
    CHECK(world.carcasses().empty() && replay.save() == world.save());
}
} // namespace

int main() {
    try {
        test_founder();
        test_nursery_seeding();
        test_partner_readiness_sense();
        test_hungry_drive_loss();
        test_nursery_pond();
        test_lesson_one_pond();
        test_patch_pond();
        test_pond_energy_charges();
        test_periodic_pond_refill();
        test_contact();
        test_scavenging_and_decay();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "predation test failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
