#include "hunting_score.hpp"

#include <cstdio>
#include <stdexcept>

namespace {
using namespace nursery;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

HuntResult successful() {
    HuntResult r;
    r.founders = 4; r.births = 4; r.recruits = r.living_recruits = 4;
    r.alive = r.adults = r.late_adults = 8;
    r.need = r.meat = r.hunted_meat = 1000;
    r.late_need = r.late_meat = r.late_hunted_meat = 200;
    r.completed = true;
    return r;
}

void test_success() {
    const auto good = successful();
    CHECK(good.passes() && good.score() == 31000);
    auto r = good;
    r.plants = 4000;
    CHECK(!r.passes() && r.score() < good.score());
    r = good; r.hunted_meat = r.late_hunted_meat = 0; // scavenging is not hunting
    CHECK(!r.passes() && r.score() == 0);
    r = good; r.need *= 5; // descendants' costs cannot disappear from the denominator
    CHECK(!r.passes() && r.meat_permille() == 200);
    r = good; r.births = r.recruits = 100; r.living_recruits = 0;
    CHECK(!r.passes() && r.score() < good.score()); // dead offspring earn no replacement credit
    r = good; r.late_hunted_meat = 0;
    CHECK(!r.passes() && r.score() < good.score());
    r = good; r.plants = r.late_plants = 100;
    CHECK(!r.passes()); // a final-quarter subsidy is visible even if the whole trial looks fine
    r = good; ++r.late_adults;
    CHECK(!r.passes() && r.score() < good.score());
    r = good; r.kin_kills = 1;
    CHECK(!r.passes() && r.score() < 0);
    r = good; r.completed = false;
    CHECK(!r.passes());
    r = good; r.need = 0;
    CHECK(!r.passes());
    const auto g = founder_genome(FounderKind::swimmer);
    CHECK(hunt_trial_ticks(g, 1) > World::ticks_per_day);
    CHECK(hunt_trial_ticks(g, 200) == 200 * World::ticks_per_day);
    auto boundary = g;
    boundary.gene[agene_maturity] = 49;
    const int before = hunt_trial_ticks(boundary, 1);
    boundary.gene[agene_maturity] = 50;
    CHECK(hunt_trial_ticks(boundary, 1) > before); // a varied child's maturity can cross the 64 boundary
    const int both[2] = {4, 4}, land_only[2] = {4, 0};
    const int enough[2] = {3, 3}, lopsided[2] = {4, 2};
    CHECK(hunt_exam_passes(enough, both));
    CHECK(!hunt_exam_passes(lopsided, both) && !hunt_exam_passes(enough, land_only));
}

struct BudgetWatch : HuntWatch {
    using HuntWatch::HuntWatch;
    std::int64_t charges[6] = {};
    void spent(const World& w, const Animal& a, int amount, EnergyUse use) override {
        if (member(a.id)) charges[int(use)] += amount;
        HuntWatch::spent(w, a, amount, use);
    }
};

void test_budget_and_observation() {
    WorldConfig c;
    c.layout = Layout::arena; c.width = 12; c.height = 8; c.arena_food = 4000;
    World watched(c);
    auto g = founder_genome(FounderKind::fish);
    auto b = brain::Brain{};
    auto* bias = b.weight + brain::hidden * brain::unit_bytes + brain::hidden;
    bias[brain::out_eat * brain::output_bytes] = 127;
    bias[brain::out_pace * brain::output_bytes] = 40;
    const auto id = watched.add_animal(g, 5 * 256, 4 * 256, 0, 40, b);
    World alone = watched;
    const int initial = watched.animals()[0].energy;
    BudgetWatch watch(128); watch.add_founder(id);
    for (int t = 0; t < 128; ++t) { watched.step(&watch); alone.step(); }
    CHECK(watched.save() == alone.save() && watched.fingerprint() == alone.fingerprint());
    CHECK(watched.animals().size() == 1);
    const auto r = watch.finish(watched, 128);
    CHECK(r.need > 0 && r.plants > 0 && !r.meat && !r.hunted_meat);
    CHECK(initial + r.plants - watched.animals()[0].energy == r.need);
    CHECK(watch.charges[int(EnergyUse::upkeep)] > 0 && watch.charges[int(EnergyUse::stamina)] > 0);
    CHECK(r.late_need > 0 && r.late_need < r.need && r.completed);

    // Cost, food and development track descendants, while unrelated prey do not count.
    Animal young = watched.animals()[0]; young.id = id + 10; young.growth = 200;
    watch.born(watched, young, watched.animals()[0], watched.animals()[0]);
    const auto old_cost = watch.r.need;
    watch.spent(watched, young, 99, EnergyUse::growth);
    CHECK(watch.r.need == old_cost + 99);
    watch.metabolised(watched, young, frame(young.genome, young.growth));
    young.growth = 255;
    watch.metabolised(watched, young, frame(young.genome));
    watch.metabolised(watched, young, frame(young.genome));
    CHECK(watch.r.recruits == 1);
    watch.ate_carcass(watched, young, 50, Carcass{0, 0, 100, id, 100, 0});
    watch.ate_carcass(watched, young, 20, Carcass{0, 0, 100, 0, 100, 0});
    CHECK(watch.r.meat == 70 && watch.r.hunted_meat == 50);
    young.id = id + 100;
    watch.spent(watched, young, 99, EnergyUse::upkeep);
    CHECK(watch.r.need == old_cost + 99);

    // Reproduction transfers reserves to children; charge tissue, growth and
    // parental effort once, rather than counting those transferred reserves twice.
    World breeding(c);
    bias[brain::out_mate * brain::output_bytes] = 127;
    bias[brain::out_pace * brain::output_bytes] = -127;
    const auto first = breeding.add_animal(g, 5 * 256, 4 * 256, 0, 100, b);
    const auto second = breeding.add_animal(g, 5 * 256 + 24, 4 * 256, 128, 100, b);
    for (const auto& a : breeding.animals()) const_cast<Animal&>(a).drive = 255;
    std::int64_t start_energy = 0;
    for (const auto& a : breeding.animals()) start_energy += a.energy;
    BudgetWatch family(256); family.add_founder(first); family.add_founder(second);
    World breeding_alone = breeding;
    for (int t = 0; t < 256; ++t) { breeding.step(&family); breeding_alone.step(); }
    CHECK(breeding.save() == breeding_alone.save());
    std::int64_t end_energy = 0;
    for (const auto& a : breeding.animals()) end_energy += a.energy;
    CHECK(family.r.births > 0 && family.r.starved == 0 && family.r.suffocated == 0 && family.r.killed == 0);
    CHECK(family.charges[int(EnergyUse::reproduction)] > 0 && family.charges[int(EnergyUse::growth)] > 0);
    CHECK(start_energy + family.r.plants + family.r.meat - end_energy == family.r.need);
}
}

int main() {
    try { test_success(); test_budget_and_observation(); }
    catch (const std::exception& e) { std::fprintf(stderr, "hunting test failed: %s\n", e.what()); return 1; }
    return 0;
}
