// Arenas, the step observer, founder brains and the nursery's score.
// Run from the source directory: it loads saves kept under docs/experiments.

#include "nursery_score.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "file.hpp"

namespace {
using namespace phd;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

bool same(const brain::Brain& a, const brain::Brain& b) { return std::equal(a.weight, a.weight + brain::bytes, b.weight); }

// A brain that does only what its output biases say.
brain::Brain fixed(bool eat, bool mate, int pace = -127) {
    brain::Brain b{};
    auto* bias = b.weight + brain::hidden * brain::unit_bytes + brain::hidden;
    bias[brain::out_pace * brain::output_bytes] = std::int8_t(pace);
    bias[brain::out_eat * brain::output_bytes] = eat ? 127 : -127;
    bias[brain::out_mate * brain::output_bytes] = mate ? 127 : -127;
    bias[brain::out_attack * brain::output_bytes] = -127;
    return b;
}

void test_founder_brains() {
    WorldConfig c;
    c.width = 128; c.height = 96; c.layout = Layout::development;
    World plain(c), named(c), bred(c);
    plain.seed_animals(56);
    named.seed_animals(56, FounderBrains::hand_wired());
    CHECK(plain.save() == named.save());

    // Each kind takes its own pool in turn; where and what the founders are does not change.
    FounderBrains pools = FounderBrains::hand_wired();
    for (int kind = 0; kind < founder_kinds; ++kind) {
        auto& pool = pools.pool[std::size_t(kind)];
        pool.push_back(pool.front());
        pool[0].weight[0] = std::int8_t(10 + kind);
        pool[1].weight[0] = std::int8_t(-10 - kind);
    }
    bred.seed_animals(56, pools);
    CHECK(bred.animals().size() == plain.animals().size());
    int seen[founder_kinds] = {};
    for (std::size_t i = 0; i < bred.animals().size(); ++i) {
        const auto& a = plain.animals()[i];
        const auto& b = bred.animals()[i];
        CHECK(a.id == b.id && a.x == b.x && a.y == b.y && a.energy == b.energy && genetic_distance(a.genome, b.genome) == 0);
        int kind = -1;
        for (int k = 0; k < founder_kinds; ++k) {
            const auto& plan = founder_genome(FounderKind(k));
            if (plan.gene[agene_legs] == b.genome.gene[agene_legs] && plan.gene[agene_fins] == b.genome.gene[agene_fins] &&
                plan.gene[agene_wings] == b.genome.gene[agene_wings] && plan.gene[agene_gills] == b.genome.gene[agene_gills]) kind = k;
        }
        CHECK(kind >= 0 && b.stock == founder_stock(FounderKind(kind)));
        CHECK(same(bred.brain_of(i), pools.pick(FounderKind(kind), std::size_t(seen[kind]++))));
    }
    for (const int n : seen) CHECK(n >= 8);

    // The file of pools, and what it refuses.
    auto file = pools.encode();
    FounderBrains read;
    CHECK(FounderBrains::decode(file.data(), file.size(), read));
    for (int kind = 0; kind < founder_kinds; ++kind) {
        CHECK(read.pool[std::size_t(kind)].size() == 2 && same(read.pool[std::size_t(kind)][1], pools.pool[std::size_t(kind)][1]));
    }
    file[40] ^= 1;
    CHECK(!FounderBrains::decode(file.data(), file.size(), read));
    CHECK(!FounderBrains::decode(file.data(), 12, read));
    FounderBrains missing = pools;
    missing.pool[2].clear();
    bool refused = false;
    try { World(c).seed_animals(7, missing); } catch (const std::invalid_argument&) { refused = true; }
    CHECK(refused);
}

void test_pond_saves() {
    // The first nursery's ponds: every cell planted and refilled each tick (16), beds (17), a refill interval (18).
    WorldConfig c;
    c.layout = Layout::arena; c.width = 18; c.height = 10; c.arena_food = 520;
    World pond(c);
    const auto cell = std::size_t(5 * c.width + 8);
    CHECK(pond.water(cell) == Water::lake && pond.water_depth_dm(cell) == 20 && pond.occupant(cell, 0)->energy == 528);
    CHECK(pond.daylight() == 200 && pond.save()[4] == 28);

    c.arena_refill_ticks = 8;
    const std::vector<ArenaPatch> beds{{8, 4, 2, 2}};
    World timed(c, beds);
    const auto bed = std::size_t(4 * c.width + 8);
    const_cast<Plant*>(timed.occupant(bed, 0))->energy = 8;
    for (int tick = 1; tick <= 6; ++tick) { timed.step(); CHECK(timed.occupant(bed, 0)->energy == 8); }
    const auto saved = timed.save();
    World replay(c);
    CHECK(saved[4] == 30 && replay.load(saved.data(), saved.size()) && saved == replay.save());
    timed.step(); replay.step();
    timed.step(); replay.step();
    CHECK(replay.occupant(bed, 0)->energy == 8 + c.arena_food && timed.save() == replay.save() && !replay.occupant(bed - 1, 0));
    for (const auto bad : {ArenaPatch{0, 2, 1, 1}, ArenaPatch{2, 3, 0, 1}, beds.front()}) {
        bool rejected = false;
        try { World invalid(c, {beds.front(), bad}); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }

    // Saves the first nursery left behind still open and run.
    for (const char* path : {"docs/experiments/lesson1-batch1/retained-1.phdw", "docs/experiments/lesson2-batch1/slot-1.phdw",
                             "docs/experiments/peer-breeding-batch1/confirmed/1d5b9c85cf0cbc1f.phdw"}) {
        std::vector<std::uint8_t> bytes;
        World old(WorldConfig{});
        CHECK(read_file(path, bytes) && old.load(bytes.data(), bytes.size()));
        CHECK(old.config().layout == Layout::arena && old.animals().size() == 2);
        // It is saved again in today's form, soft bodies and all, and that form keeps.
        const auto resaved = old.save();
        World again(WorldConfig{});
        CHECK(resaved != bytes && again.load(resaved.data(), resaved.size()) && again.save() == resaved);
        for (const auto& a : again.animals()) CHECK(!a.genome.gene[agene_hard_mouth] && !a.genome.gene[agene_hard_back]);
        for (int t = 0; t < 64; ++t) old.step();
        CHECK(old.animals().size() >= 2);
    }
}

void test_arena_dials() {
    WorldConfig c;
    c.layout = Layout::arena; c.seed = 5; c.width = 20; c.height = 12;
    c.arena_land = 6; c.arena_food = 400; c.arena_regrow = 25 * 16; c.arena_days = true;
    const std::vector<ArenaPatch> beds{{2, 2, 2, 2}, {10, 3, 3, 3}};
    World arena(c, beds);
    const auto dry = std::size_t(3 * c.width + 3), wet = std::size_t(4 * c.width + 11);
    CHECK(arena.water(dry) == Water::none && arena.water(std::size_t(3 * c.width + 6)) == Water::none);
    CHECK(arena.water(wet) == Water::lake && arena.water(std::size_t(3 * c.width + 7)) == Water::lake);
    CHECK(arena.plant_count() == 13 && arena.occupant(dry, 0) && arena.occupant(wet, 0) && !arena.occupant(std::size_t(8 * c.width + 8), 0));

    // A walker stands on the dry side and a fish swims in the water.
    const auto hold = fixed(false, false);
    const auto walker = arena.add_animal(founder_genome(FounderKind::grazer), 3 * 256 + 128, 3 * 256 + 128, 0, 50, hold);
    const auto fish = arena.add_animal(founder_genome(FounderKind::fish), 11 * 256 + 128, 4 * 256 + 128, 0, 50, hold, FishStock::small_fish);
    CHECK(walker == 1 && fish == 2 && arena.animals().size() == 2);
    CHECK(Medium(arena.animals()[0].medium) == Medium::land && Medium(arena.animals()[1].medium) == Medium::water);
    CHECK(arena.animals()[0].energy == frame(founder_genome(FounderKind::grazer)).store / 2 && !arena.animals()[0].drive);
    CHECK(!arena.add_animal(founder_genome(FounderKind::fish), 100, 100, 0, 50, hold) && !arena.add_animal(founder_genome(FounderKind::fish), 19 * 256, 300, 0, 50, hold));

    // Fodder grows back a step each metabolic pass, to its cap, and light follows the day.
    const_cast<Plant*>(arena.occupant(dry, 0))->energy = 8;
    const int dawn = arena.daylight();
    for (int t = 0; t < animal_stagger; ++t) arena.step();
    CHECK(arena.occupant(dry, 0)->energy == 8 + 25 && arena.occupant(wet, 0)->energy == 8 + 400);
    for (int t = 0; t < World::ticks_per_day / 2 - animal_stagger; ++t) arena.step();
    CHECK(arena.daylight() > dawn + 100);
    for (const auto& a : arena.animals()) CHECK(a.health == 255 && a.breath == breath_ticks);

    const auto saved = arena.save();
    World replay(WorldConfig{});
    CHECK(saved[4] == 31 && replay.load(saved.data(), saved.size()) && replay.save() == saved);
    CHECK(replay.config().arena_land == 6 && replay.config().arena_regrow == 25 * 16 && replay.config().arena_days);
    CHECK(replay.fingerprint() == arena.fingerprint());
    for (int t = 0; t < 100; ++t) { arena.step(); replay.step(); }
    CHECK(replay.save() == arena.save());

    c.arena_land = 19;
    bool rejected = false;
    try { World invalid(c); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

struct Counter : StepObserver {
    int passes = 0, meals = 0, births = 0, deaths = 0;
    std::int64_t gained = 0;
    void metabolised(const World&, const Animal&, const Frame&) override { ++passes; }
    void ate(const World&, const Animal&, int energy, bool) override { ++meals; gained += energy; }
    void born(const World&, const Animal& young, const Animal& mother, const Animal& father) override {
        births += young.mother == mother.id && young.father == father.id;
    }
    void died(const World&, const Animal&, DeathCause, std::uint32_t) override { ++deaths; }
};

void test_observer() {
    // Watching changes nothing, and what is watched adds up.
    WorldConfig c;
    c.width = 128; c.height = 96; c.layout = Layout::development;
    World watched(c), alone(c);
    watched.seed_animals(56);
    alone.seed_animals(56);
    const auto founders = watched.animals().size();
    Counter counter;
    for (int t = 0; t < 2400; ++t) { watched.step(&counter); alone.step(); }
    CHECK(watched.fingerprint() == alone.fingerprint() && watched.save() == alone.save());
    CHECK(counter.passes > 2000 && counter.meals > 0 && counter.gained > 0);
    CHECK(watched.animals().size() == founders + std::size_t(counter.births) - std::size_t(counter.deaths));
}

void test_score() {
    using namespace nursery;
    // A small arena where food and partners are always in reach.
    Level level{"Test", "", 6, 6, 12, 60, 0, 8, 0, 0, 0, 0, false, 1200, 0, false, 0, {3, 80, true, 1, 0, 0, 100}};
    const auto kind = FounderKind::fish;
    const Result fasting = run_trial(level, kind, 11, {fixed(false, false)});
    const Result eating = run_trial(level, kind, 11, {fixed(true, false)});
    const Result breeding = run_trial(level, kind, 11, {fixed(true, true)});
    CHECK(fasting.births == 0 && fasting.ready == 0 && fasting.fed < eating.fed);
    CHECK(eating.fed == 1000 && eating.ready == 1000 && eating.births == 0 && eating.founders_alive == 12 && eating.starved == 0);
    CHECK(breeding.births > 0 && breeding.bred > 0 && breeding.living > 12 && breeding.generation == 1);
    CHECK(fasting.score() < eating.score() && eating.score() < breeding.score() && breeding.score() <= Result::full_score);
    CHECK(!passes(level.gate, fasting) && !passes(level.gate, eating) && passes(level.gate, breeding));

    // A trial is a function of its level, kind, seed and brain.
    const Result again = run_trial(level, kind, 11, {fixed(true, true)});
    CHECK(again.fingerprint == breeding.fingerprint && again.score() == breeding.score());
    CHECK(run_trial(level, kind, 12, {fixed(true, true)}).fingerprint != breeding.fingerprint);

    // Every level's arena fits its founders, for every kind, and no level is short of food by construction.
    for (int k = 0; k < founder_kinds; ++k) {
        for (const auto& l : levels) {
            const Arena a = arena(l, FounderKind(k), 3);
            CHECK(a.planted > 0 && (!l.patches || int(a.beds.size()) == l.patches));
            CHECK(l.plenty == 0 || a.supply * 10 >= a.need * l.plenty * 8 / 10);
            const World w = start(l, FounderKind(k), 3, {brain::founder()});
            int there = l.founders;
            for (const auto& guest : guests(l, FounderKind(k))) there += guest.count;
            CHECK(int(w.animals().size()) == there);
            const bool wet = lives_in_water(FounderKind(k));
            for (const auto& animal : w.animals()) {
                CHECK((Medium(animal.medium) == Medium::water) == wet && animal.id <= std::uint32_t(there));
            }
        }
    }
}

} // namespace

int main() {
    try {
        test_founder_brains();
        test_pond_saves();
        test_arena_dials();
        test_observer();
        test_score();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 1;
    }
    std::puts("nursery tests passed");
    return 0;
}
