#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "world.hpp"

namespace nursery {
using namespace phd;

// Costs and food follow the same family, including every descendant. Food
// supplied by plants, scavenging and killing kin cannot qualify a hunter.
struct HuntResult {
    std::int64_t meat = 0, plants = 0, hunted_meat = 0, need = 0;
    std::int64_t late_meat = 0, late_plants = 0, late_hunted_meat = 0, late_need = 0;
    int founders = 0, alive = 0, adults = 0, births = 0, recruits = 0, living_recruits = 0;
    int late_adults = 0, kin_kills = 0, kills_grown = 0, kills_young = 0, lunges = 0, hits = 0, herd = 0;
    int starved = 0, suffocated = 0, killed = 0, ticks = 0;
    bool wet = false, completed = false;

    static int fraction(std::int64_t part, std::int64_t whole) {
        return whole > 0 ? int(std::clamp<std::int64_t>(part * 1000 / whole, 0, 1000)) : 0;
    }
    int meat_permille() const { return fraction(hunted_meat, need); }
    int hunt_share() const { return fraction(hunted_meat, meat + plants); }
    int late_coverage() const { return fraction(late_hunted_meat, late_need); }
    int late_share() const { return fraction(late_hunted_meat, late_meat + late_plants); }
    int quality() const {
        return kin_kills ? 0 : std::min(meat_permille(), std::min(1000, hunt_share() * 1000 / 800));
    }
    const char* fails() const {
        if (!founders) return "no founders settled";
        if (!completed) return "family died before the end";
        if (kin_kills) return "killing its own family";
        if (!need || hunted_meat < need) return "own kills covering family costs";
        if (hunt_share() < 800) return "depending on plants or scavenging";
        if (!late_need || late_hunted_meat < late_need || late_share() < 800) return "hunting through the final quarter";
        if (living_recruits < founders) return "raising a surviving adult replacement generation";
        if (adults < 2 * founders || adults < late_adults) return "sustaining the adult family";
        return nullptr;
    }
    bool passes() const { return !fails(); }
    int score() const {
        const int bred = fraction(births, founders);
        const int raised = fraction(living_recruits, founders);
        const int sustained = fraction(adults, 2 * founders);
        const int late = completed ? std::min({late_coverage(), std::min(1000, late_share() * 1000 / 800),
                                              fraction(adults, std::max(2 * founders, late_adults))}) : 0;
        // Life-cycle rewards only count to the extent hunting pays for them.
        return quality() + quality() * (2 * bred + 4 * raised + 8 * sustained + 16 * late) / 1000
               - 16000 * fraction(kin_kills, founders) / 1000;
    }
};

inline bool hunt_exam_passes(const int (&passed)[2], const int (&tried)[2]) {
    return tried[0] && tried[1] && passed[0] * 4 >= tried[0] * 3 && passed[1] * 4 >= tried[1] * 3;
}

inline int hunt_trial_ticks(const AnimalGenome& hunter, int days) {
    // Allow two growth periods plus the time to become ready to mate. Use
    // the slowest maturity/newborn investment after founder variation (+/-8)
    // and the child's mutation (+/-6).
    const int maturity = std::min(255, int(hunter.gene[agene_maturity]) + 14);
    const int investment = std::max(0, int(hunter.gene[agene_investment]) - 14);
    const int grow = (255 - (newborn_growth + investment / 4)) * (1 + maturity / 64) * animal_stagger;
    return std::max(days * World::ticks_per_day, 2 * grow + ((mating_drive + 7) / 8) * animal_stagger);
}

struct HuntWatch : StepObserver {
    enum : std::uint8_t { founder = 1, offspring = 2, grown = 4 };
    std::vector<std::uint8_t> mine;
    HuntResult r;
    std::uint64_t late_start = 0;

    explicit HuntWatch(int ticks = 0) : late_start(std::uint64_t(ticks) * 3 / 4 + 1) {}
    bool member(std::uint32_t id) const { return id < mine.size() && mine[id]; }
    void add_founder(std::uint32_t id) {
        if (!id) return;
        if (mine.size() <= id) mine.resize(std::size_t(id) + 1, 0);
        mine[id] = founder;
        ++r.founders;
    }
    void born(const World&, const Animal& young, const Animal& mother, const Animal& father) override {
        if (!member(mother.id) && !member(father.id)) return;
        if (mine.size() <= young.id) mine.resize(std::size_t(young.id) + 1, 0);
        mine[young.id] = offspring;
        ++r.births;
    }
    void metabolised(const World&, const Animal& a, const Frame&) override {
        if (member(a.id) && (mine[a.id] & offspring) && !(mine[a.id] & grown) && a.growth == 255) {
            mine[a.id] |= grown;
            ++r.recruits;
        }
    }
    void ate(const World& w, const Animal& a, int gained, bool meat) override {
        if (!member(a.id)) return;
        (meat ? r.meat : r.plants) += gained;
        if (w.tick() >= late_start) (meat ? r.late_meat : r.late_plants) += gained;
    }
    void ate_carcass(const World& w, const Animal& a, int gained, const Carcass& c) override {
        ate(w, a, gained, true);
        if (!member(a.id) || !member(c.killer)) return;
        r.hunted_meat += gained;
        if (w.tick() >= late_start) r.late_hunted_meat += gained;
    }
    void spent(const World& w, const Animal& a, int cost, EnergyUse) override {
        if (!member(a.id)) return;
        r.need += cost;
        if (w.tick() >= late_start) r.late_need += cost;
    }
    void died(const World&, const Animal& a, DeathCause cause, std::uint32_t killer) override {
        if (member(a.id)) ++(cause == DeathCause::starved ? r.starved : cause == DeathCause::suffocated ? r.suffocated : r.killed);
        if (cause == DeathCause::killed && member(killer))
            ++(member(a.id) ? r.kin_kills : a.growth < 255 ? r.kills_young : r.kills_grown);
    }
    void struck(const World&, const Animal& a, const Animal* victim, int) override {
        if (member(a.id)) { ++r.lunges; r.hits += victim != nullptr; }
    }
    int adult_count(const World& w) const {
        return int(std::count_if(w.animals().begin(), w.animals().end(), [&](const Animal& a) {
            return member(a.id) && a.growth == 255;
        }));
    }
    HuntResult finish(const World& w, int expected_ticks) const {
        HuntResult out = r;
        out.ticks = int(w.tick());
        out.completed = w.tick() == std::uint64_t(expected_ticks);
        for (const auto& a : w.animals()) {
            if (!member(a.id)) { ++out.herd; continue; }
            ++out.alive;
            if (a.growth == 255) {
                ++out.adults;
                if (mine[a.id] & offspring) ++out.living_recruits;
            }
        }
        return out;
    }
};

} // namespace nursery
