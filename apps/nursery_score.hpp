#pragma once

// What a trial measures, and the score the nursery selects on. A watcher
// follows the founders of one kind and everything descended from them
// through a run; nothing is counted inside the simulation.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "nursery_levels.hpp"

namespace nursery {

// One trial of one brain. The five terms run 0 to 1000 and follow a life
// in order: a brain that cannot do the later ones is still told apart by
// the earlier ones, so selection always has something to go on.
struct Result {
    int fed = 0;       // founders' metabolic passes above half energy; a dead founder's count as unfed
    int ready = 0;     // the mating drive the second-readiest founder reached, against what mating takes
    int bred = 0;      // births, against the founding number
    int recruited = 0; // young born there that grew up, against the founding number
    int sustained = 0; // alive at the end, against twice the founding number
    // And one that counts against it: animals the family killed that were
    // not its food, against the founding number. Left out, a brain does
    // well by killing whatever else eats what it eats, its own kind included.
    int violence = 0;

    int founders = 0, founders_alive = 0, living = 0;
    int births = 0, recruits = 0, generation = 0;
    int starved = 0, suffocated = 0, killed = 0; // deaths in the family, founders or not
    bool meat_eater = false;                     // fed in bursts: its gates ask half as much of `fed`
    int murders = 0;                             // kills by the family, other than a hunter's of another kind
    int founders_starved = 0;
    std::int64_t plants_eaten = 0, meat_eaten = 0; // energy the family gained from each
    std::int64_t passes = 0, fed_passes = 0;       // metabolic passes of the whole family, and those above half energy
    std::uint64_t fingerprint = 0;

    int score() const { return fed + 2 * ready + 4 * bred + 8 * recruited + 16 * sustained - 16 * violence; }
    static constexpr int full_score = 31000;
};

class Watcher : public StepObserver {
public:
    // Founders of the family have ids 1 to `founders`.
    Watcher(int founders, int passes) : founders_(founders), passes_(passes), who_(std::size_t(founders) + 1, founder), drive_(who_.size(), 0) {
        who_[0] = 0;
        for (int id = 1; id <= founders; ++id) founder_ids_.push_back(std::uint32_t(id));
    }
    // Or the founders are these animals.
    Watcher(const std::vector<std::uint32_t>& founders, int passes) : founders_(int(founders.size())), passes_(passes), founder_ids_(founders) {
        for (const auto id : founders) {
            if (who_.size() <= id) who_.resize(std::size_t(id) + 1, 0);
            who_[id] = founder;
        }
        drive_.assign(who_.size(), 0);
    }

    void metabolised(const World&, const Animal& a, const Frame& f) override {
        if (!member(a.id)) return;
        ++result_.passes;
        result_.fed_passes += a.energy * 2 >= f.store;
        if (who_[a.id] & founder) {
            fed_passes_ += a.energy * 2 >= f.store;
            drive_[a.id] = std::max(drive_[a.id], a.drive);
        } else if (a.growth == 255 && !(who_[a.id] & grown)) {
            who_[a.id] |= grown;
            ++result_.recruits;
        }
    }

    void born(const World&, const Animal& young, const Animal& mother, const Animal& father) override {
        if (!member(mother.id) && !member(father.id)) return;
        if (who_.size() <= young.id) { who_.resize(std::size_t(young.id) + 1, 0); generation_.resize(who_.size(), 0); }
        who_[young.id] = born_here;
        if (generation_.size() < who_.size()) generation_.resize(who_.size(), 0);
        const int parent = std::max(generation_of(mother.id), generation_of(father.id));
        generation_[young.id] = std::uint16_t(parent + 1);
        result_.generation = std::max(result_.generation, parent + 1);
        ++result_.births;
    }

    void ate(const World&, const Animal& a, int gained, bool meat) override {
        if (member(a.id)) (meat ? result_.meat_eaten : result_.plants_eaten) += gained;
    }

    // A hunter's kills of other kinds are its living.
    void hunts(bool hunter) { hunter_ = hunter; }

    void died(const World&, const Animal& a, DeathCause cause, std::uint32_t killer) override {
        if (cause == DeathCause::killed && member(killer) && (member(a.id) || !hunter_)) ++result_.murders;
        if (!member(a.id)) return;
        (cause == DeathCause::starved ? result_.starved : cause == DeathCause::suffocated ? result_.suffocated : result_.killed)++;
        result_.founders_starved += cause == DeathCause::starved && (who_[a.id] & founder);
    }

    bool member(std::uint32_t id) const { return id < who_.size() && who_[id]; }

    Result finish(const World& world) {
        Result r = result_;
        r.founders = founders_;
        for (const auto& a : world.animals()) {
            if (!member(a.id)) continue;
            ++r.living;
            r.founders_alive += bool(who_[a.id] & founder);
        }
        std::vector<int> drives;
        for (const auto id : founder_ids_) drives.push_back(drive_[id]);
        std::sort(drives.rbegin(), drives.rend());
        const int second = drives.size() > 1 ? drives[1] : 0;
        r.fed = int(std::int64_t(fed_passes_) * 1000 / std::max(1, founders_ * passes_));
        r.ready = std::min(1000, second * 1000 / mating_drive);
        r.bred = std::min(1000, r.births * 1000 / std::max(1, founders_));
        r.recruited = std::min(1000, r.recruits * 1000 / std::max(1, founders_));
        r.sustained = std::min(1000, r.living * 1000 / std::max(1, 2 * founders_));
        r.fed = std::min(1000, r.fed);
        r.violence = std::min(1000, r.murders * 1000 / std::max(1, founders_));
        r.fingerprint = world.fingerprint();
        return r;
    }

private:
    enum : std::uint8_t { founder = 1, born_here = 2, grown = 4 };
    int generation_of(std::uint32_t id) const { return id < generation_.size() ? generation_[id] : 0; }

    int founders_, passes_;
    bool hunter_ = false;
    std::vector<std::uint32_t> founder_ids_;
    std::vector<std::uint8_t> who_;       // by animal id
    std::vector<std::uint8_t> drive_;     // each founder's highest
    std::vector<std::uint16_t> generation_;
    std::int64_t fed_passes_ = 0;
    Result result_;
};

// Runs a brain through a level: every founder of the kind carries it, and
// their young inherit it as young do in any world. Ends early if the family
// dies out.
inline Result run_trial(const Level& level, FounderKind kind, std::uint32_t seed, const std::vector<brain::Brain>& minds,
                        const FounderBrains* company = nullptr) {
    World world = start(level, kind, seed, minds, company);
    const int ticks = trial_ticks(level, kind);
    Watcher watcher(level.founders, ticks / animal_stagger);
    watcher.hunts(kind == FounderKind::swimmer);
    const bool meat_eater = eats_only_meat(kind);
    for (int t = 0; t < ticks; ++t) {
        world.step(&watcher);
        if (t % 64 == 63) {
            const auto& herd = world.animals();
            if (std::none_of(herd.begin(), herd.end(), [&](const Animal& a) { return watcher.member(a.id); })) break;
        }
    }
    Result r = watcher.finish(world);
    r.meat_eater = meat_eater;
    return r;
}

// The first thing a gate asks that this trial did not do, or nullptr.
inline const char* fails(const Gate& gate, const Result& r) {
    // A hunter that takes some of its own is held to what the rest of the
    // gate asks, that its numbers last; the score still counts them against it.
    if (r.murders && !r.meat_eater) return "killing what is not its food";
    if (r.fed < gate.fed_percent * (r.meat_eater ? 5 : 10)) return "staying fed";
    if (gate.none_starve && r.founders_starved) return "a founder starving";
    if (r.births < gate.births) return "breeding";
    if (r.recruits < gate.recruits) return "young growing up";
    if (r.generation < gate.generation) return "a second generation";
    if (gate.births || gate.recruits ? r.living * 100 < r.founders * gate.living_percent
                                     : r.founders_alive * 100 < r.founders * gate.living_percent) return "lasting to the end";
    return nullptr;
}

inline bool passes(const Gate& gate, const Result& r) { return !fails(gate, r); }

} // namespace nursery
