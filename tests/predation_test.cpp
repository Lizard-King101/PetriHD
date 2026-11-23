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
        const auto staged = world.save();
        CHECK(world.load(staged.data(), staged.size())); // rebuild the index after staging contact
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
        CHECK(data[4] == (loss == 16 ? 26 : 27));
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

void test_contact() {
    World world = lake_contact();
    for (int miss = 0; miss < 3; ++miss) {
        World probe = world;
        auto& prey = animal(probe, 5);
        if (miss == 0) prey.x += 512;
        if (miss == 1) prey.x -= 48;
        if (miss == 2) prey.medium = std::uint8_t(Medium::land);
        const auto staged = probe.save();
        CHECK(probe.load(staged.data(), staged.size()));
        const auto wind = animal(probe, 7).stamina;
        for (int t = 0; t < 3; ++t) probe.step();
        CHECK(animal(probe, 5).health == 255 && !animal(probe, 5).pain);
        CHECK(animal(probe, 7).stamina == wind); // no lunge is armed at an empty mouth
    }
    for (int t = 0; t < 3; ++t) world.step();
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

struct AimWatch : StepObserver {
    int hits = 0, misses = 0;
    void struck(const World&, const Animal& a, const Animal* victim, int) override {
        if (a.id == 7) ++(victim ? hits : misses);
    }
};

void test_aim_and_escape() {
    using namespace brain;
    Brain eager{};
    eager.weight[hidden * unit_bytes + out_attack * output_bytes + hidden] = 127;
    Senses s{};
    s.in[self_input(self_mouth_size)] = 127;
    CHECK(!think(s, eager).attack);
    s.in[self_input(self_mouth_size)] = 0;
    CHECK(think(s, eager).attack); // equal-sized contact is not an empty mouth

    World fleeing = lake_contact();
    AimWatch attempts;
    fleeing.step(&attempts);
    CHECK(animal(fleeing, 7).wants & wants_attack);
    CHECK(attempts.hits == 0 && attempts.misses == 0); // winding up
    animal(fleeing, 5).x += 512;
    const auto escaped = fleeing.save();
    CHECK(fleeing.load(escaped.data(), escaped.size()));
    const auto wind = animal(fleeing, 7).stamina;
    fleeing.step(&attempts); fleeing.step(&attempts);
    CHECK(attempts.misses == 1 && attempts.hits == 0);
    CHECK(animal(fleeing, 5).health == 255 && animal(fleeing, 7).stamina < wind);
    for (int t = 0; t < 16; ++t) fleeing.step(&attempts);
    CHECK(attempts.misses == 1); // no repeated lunges once contact has been lost

    World contact = lake_contact();
    contact.step(); // build the initial forage/sensory fields before inspecting them
    const auto index = std::size_t(&animal(contact, 7) - contact.animals().data());
    CHECK(contact.senses(index).in[self_input(self_mouth_size)] < 127);
    animal(contact, 5).medium = std::uint8_t(Medium::air);
    CHECK(contact.senses(index).in[self_input(self_mouth_size)] == 127);
    CHECK(contact.senses(index).in[sector_input(ahead, in_animal)] == 0);
    animal(contact, 5).medium = std::uint8_t(Medium::water);
    AimWatch stationary;
    for (int t = 0; t < 3; ++t) contact.step(&stationary);
    CHECK(stationary.hits == 1 && stationary.misses == 0);
}

void test_scavenging_and_decay() {
    World world = lake_contact();
    hold_outputs(world, 7, false, false);
    auto& prey = animal(world, 5);
    prey.genome.gene[agene_gills] = 0;
    prey.health = 1;
    prey.breath = 0;
    const int reserves = prey.energy + flesh_of(prey);
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
        test_partner_readiness_sense();
        test_hungry_drive_loss();
        test_contact();
        test_aim_and_escape();
        test_scavenging_and_decay();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "predation test failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
