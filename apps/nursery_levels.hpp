#pragma once

// The nursery's ladder: each level is an arena and a gate. An arena is
// built for a kind of founder from the level's dials, with its food scaled
// to what that body burns, so a level asks the same of a small fish as of a
// large swimmer.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "world.hpp"

namespace nursery {
using namespace phd;

// What a brain must manage, on enough of the gate's seeds, to move up.
struct Gate {
    int seeds;          // of the four gate seeds
    int fed_percent;    // of the founders' metabolic passes spent above half energy
    bool none_starve;   // no founder starves
    int births;         // at least
    int recruits;       // young born there that grew up
    int generation;     // 2 asks for grandchildren
    int living_percent; // alive at the end, against the founding number
};

struct Level {
    const char* name;
    const char* task;
    int width, height;  // cells the body can live on
    int founders;
    int start_energy;   // percent of a full store
    // Food grown each metabolic pass, in tenths of what the founders burn at
    // a full walk or swim. 0: every planted cell is refilled every tick.
    int plenty;
    int cell_bites;     // what a planted cell holds, in mouthfuls;
    int real;           // or, if not 0, in tenths of what a cell of the real world holds
    int patches, patch; // patches of patch x patch cells; 0 patches plants every cell
    int hazard;         // columns of ground the body cannot live on, down one side
    bool days;          // day and night
    int ticks;          // a trial lasts this long,
    int growings;       // plus this many tenths of the time the kind takes to grow up
    bool company;       // other kinds share the arena, with brains of their own
    int prey;           // for a meat-eater: small fish stocked for every hundred cells
    Gate gate;
};

inline constexpr Level levels[] = {
    {"Feed", "Eat when hungry, with food in every cell.",
     12, 8, 4, 40, 0, 8, 0, 0, 0, 0, false, 480, 0, false, 33, {4, 90, true, 0, 0, 0, 100}},
    {"Forage", "Find patches of food that run out and grow back.",
     24, 16, 4, 50, 40, 24, 0, 6, 2, 0, false, 1600, 0, false, 28, {3, 70, true, 0, 0, 0, 100}},
    {"Pair", "Stay fed, find each other and breed.",
     20, 14, 6, 60, 60, 16, 0, 0, 0, 0, false, 2400, 0, false, 25, {3, 0, false, 1, 0, 0, 100}},
    {"Raise", "Young born in the arena grow up there.",
     24, 16, 6, 60, 50, 24, 0, 8, 3, 0, false, 1000, 22, false, 22, {3, 0, false, 1, 2, 0, 100}},
    {"Lean", "The same with less food, day and night, and ground to keep off.",
     36, 24, 8, 50, 25, 24, 0, 10, 3, 3, true, 1000, 30, false, 18, {3, 0, false, 1, 2, 2, 100}},
    {"Thin", "Food in every cell but little of it: keep moving to stay fed.",
     36, 24, 8, 50, 25, 0, 20, 0, 0, 3, true, 1000, 30, false, 15, {3, 0, false, 1, 2, 2, 100}},
    {"Company", "The same, shared with other kinds that eat the same food, or each other.",
     40, 28, 8, 50, 25, 0, 15, 0, 0, 3, true, 1000, 30, true, 12, {3, 0, false, 1, 2, 2, 100}},
};
inline constexpr int level_count = int(sizeof(levels) / sizeof(levels[0]));

// The edible energy a cell holds on the development map, year in and year
// out: measured with `petrihd-nursery wild`. About one mouthful for a
// grazer, most of one for a large swimmer, five for a crawler or a fish.
inline constexpr int real_cell_land = 130, real_cell_water = 50;

inline bool lives_in_water(FounderKind kind) { return !frame(founder_genome(kind)).breathes_air; }

// A body that cannot graze: it lives on what it kills and what it finds dead.
inline bool eats_only_meat(FounderKind kind) { return founder_genome(kind).gene[agene_plant_gut] < 32; }

// Ticks a well-fed newborn of this kind takes to grow up.
inline int growing_ticks(FounderKind kind) {
    return (255 - newborn_growth_of(founder_genome(kind))) * (1 + founder_genome(kind).gene[agene_maturity] / 64) * animal_stagger;
}

inline int trial_ticks(const Level& level, FounderKind kind) {
    // A hunter's meals are few and large, so it is watched for three times as long.
    return level.ticks * (eats_only_meat(kind) ? 3 : 1) + int(std::int64_t(level.growings) * growing_ticks(kind) / 10);
}

// Raw plant energy one founder must eat each metabolic pass to stay level
// at a full walk or swim. A meat-eater needs none.
inline int need_per_pass(FounderKind kind) {
    const auto& genome = founder_genome(kind);
    if (eats_only_meat(kind)) return 0;
    return frame(genome).upkeep * 2 * 64 / std::max<int>(1, genome.gene[agene_plant_gut]);
}

// Who shares a Company arena with the kind being trained: those that live
// where it does. The fish are the large swimmer's competitors and its prey.
struct Guest { FounderKind kind; int count; };
inline std::vector<Guest> guests(const Level& level, FounderKind kind) {
    using K = FounderKind;
    // A meat-eater's arena is stocked with its food at every level: small
    // fish, as thick as a lake they have settled holds at first (one to
    // three cells) and thinning in short steps as the levels go up. They
    // are not being trained. Their own food is kept plentiful, so they feed
    // and breed freely, and they keep the reflexes wired by hand.
    if (eats_only_meat(kind)) return {{K::fish, std::max(level.founders, level.width * level.height * level.prey / 100)}};
    if (!level.company) return {};
    switch (kind) {
    case K::swimmer: return {{K::fish, 12}};
    case K::fish: return {{K::swimmer, 4}};
    case K::grazer: return {{K::crawler, 4}, {K::flier, 4}};
    case K::crawler: return {{K::grazer, 4}, {K::flier, 4}};
    default: return {{K::grazer, 4}, {K::crawler, 4}};
    }
}

struct Arena {
    WorldConfig config;
    std::vector<ArenaPatch> beds;
    int first_x = 1, last_x = 1; // the columns the body can live on, inclusive
    int planted = 0;             // cells
    int need = 0, supply = 0;    // raw energy a pass: what the founders burn, what grows
};

inline constexpr std::uint32_t purpose_beds = 0x6e757201u, purpose_line_up = 0x6e757202u;

inline Arena arena(const Level& level, FounderKind kind, std::uint32_t seed) {
    Arena a;
    const bool wet = lives_in_water(kind);
    auto& c = a.config;
    c.layout = Layout::arena;
    c.seed = seed;
    c.width = level.width + level.hazard + 2;
    c.height = level.height + 2;
    c.ocean_percent = 0;
    c.climate = 180;
    c.seasons = 0;
    c.arena_depth_dm = 20;
    c.arena_light = 200;
    c.arena_days = level.days;
    // Dry ground lies to the west. A swimmer's hazard is a bank there; a
    // walker's is water down the east side.
    c.arena_land = std::uint16_t(wet ? level.hazard : level.width);
    a.first_x = wet ? 1 + level.hazard : 1;
    a.last_x = a.first_x + level.width - 1;

    if (level.patches == 0) {
        a.beds.push_back({a.first_x, 1, level.width, level.height});
    } else {
        Random rng(stream_seed(seed, purpose_beds));
        for (int attempt = 0; attempt < level.patches * 40 && int(a.beds.size()) < level.patches; ++attempt) {
            const ArenaPatch bed{a.first_x + int(rng.below(std::uint32_t(level.width - level.patch + 1))),
                                 1 + int(rng.below(std::uint32_t(level.height - level.patch + 1))), level.patch, level.patch};
            // A cell of open ground between any two patches.
            const bool clear = std::none_of(a.beds.begin(), a.beds.end(), [&](const ArenaPatch& other) {
                return bed.x <= other.x + other.width && other.x <= bed.x + bed.width &&
                       bed.y <= other.y + other.height && other.y <= bed.y + bed.height;
            });
            if (clear) a.beds.push_back(bed);
        }
    }
    for (const auto& bed : a.beds) a.planted += bed.width * bed.height;

    // A mouthful is the largest one there, and the food is for every body there.
    int bite = frame(founder_genome(kind)).bite;
    a.need = level.founders * need_per_pass(kind);
    for (const auto& guest : guests(level, kind)) {
        bite = std::max(bite, frame(founder_genome(guest.kind)).bite);
        a.need += guest.count * need_per_pass(guest.kind);
    }
    c.arena_food = std::uint16_t(level.real ? level.real * (wet ? real_cell_water : real_cell_land) / 10
                                            : std::clamp(level.cell_bites * bite, 16, 60000));
    if (eats_only_meat(kind)) {
        // The plants are the prey's: every cell, a good many mouthfuls, and
        // twice what the stock burns growing each pass.
        a.beds.assign(1, {a.first_x, 1, level.width, level.height});
        a.planted = level.width * level.height;
        bite = frame(founder_genome(FounderKind::fish)).bite;
        c.arena_food = std::uint16_t(24 * bite);
        const auto regrow = (std::int64_t(a.need) * 20 * 16 + 5 * a.planted) / (10 * std::int64_t(a.planted));
        c.arena_regrow = std::uint16_t(std::clamp<std::int64_t>(regrow, 1, int(c.arena_food) * 16));
        a.supply = int(std::int64_t(c.arena_regrow) * a.planted / 16);
    } else if (level.plenty) {
        // In sixteenths of a unit of energy for each cell.
        const auto regrow = (std::int64_t(a.need) * level.plenty * 16 + 5 * a.planted) / (10 * std::int64_t(a.planted));
        c.arena_regrow = std::uint16_t(std::clamp<std::int64_t>(regrow, 1, std::min(60000, int(c.arena_food) * 16)));
        a.supply = int(std::int64_t(c.arena_regrow) * a.planted / 16);
    } else {
        c.arena_refill_ticks = 1;
        a.supply = c.arena_food * a.planted * animal_stagger;
    }
    return a;
}

// A fresh arena with its founders in it: varied bodies of the kind, put down
// anywhere they can live, facing anywhere, each given the next brain in turn.
// Founders of the kind get ids 1 to level.founders. Where a level has
// company, the other kinds take their brains from `company`, or the
// hand-wired ones if none is given.
inline World start(const Level& level, FounderKind kind, std::uint32_t seed, const std::vector<brain::Brain>& minds,
                   const FounderBrains* company = nullptr) {
    const Arena a = arena(level, kind, seed);
    World world(a.config, a.beds);
    Random rng(stream_seed(seed, purpose_line_up));
    const auto put = [&](FounderKind who, const brain::Brain& mind) {
        const auto genome = varied_founder(who, rng);
        const int x = (a.first_x + int(rng.below(std::uint32_t(level.width)))) * 256 + int(rng.below(256));
        const int y = (1 + int(rng.below(std::uint32_t(level.height)))) * 256 + int(rng.below(256));
        world.add_animal(genome, x, y, std::uint8_t(rng.below(256)), level.start_energy, mind, founder_stock(who));
    };
    for (int i = 0; i < level.founders; ++i) put(kind, minds[std::size_t(i) % minds.size()]);
    const FounderBrains hand = FounderBrains::hand_wired();
    for (const auto& guest : guests(level, kind)) {
        for (int i = 0; i < guest.count; ++i) put(guest.kind, (company && !eats_only_meat(kind) ? *company : hand).pick(guest.kind, std::size_t(i)));
    }
    return world;
}

} // namespace nursery
