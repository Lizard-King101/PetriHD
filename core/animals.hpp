#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace phd {

// An animal's genome describes organs, and what it is follows from what they
// add up to: there are no kinds of animal, only bodies.
enum AnimalGene : std::uint8_t {
    agene_size,       // the trunk's length: 0.2 m to 2.5 m
    agene_build,      // slender to stout
    agene_legs,       // pairs: none below 64, then one, two, three
    agene_leg_length,
    agene_sprawl,     // legs under the body, or out to its sides
    agene_fins,       // none below 64
    agene_tail,
    agene_wings,      // none below 96, then longer
    agene_neck,
    agene_gills,      // lungs below 96, gills above 160, both between
    agene_plant_gut,
    agene_meat_gut,   // energy it can digest from a carcass
    agene_fat,        // a store for lean times, and weight to carry
    agene_hue,
    agene_tone,
    agene_pattern,    // plain, stripes, spots, pale belly
    agene_pattern_tone,
    agene_maturity,   // how long it takes to grow up
    agene_investment, // many small young at a time, or one large one
    agene_eyes,       // how far it sees
    agene_nose,       // none below 32, then keener
    agene_ears,       // none below 32, then hearing further
    agene_voice,      // the pitch of its call
    // Hard tissue, part by part. Nothing here is a weapon or armour by name:
    // when one body strikes another, the softer part takes the wound.
    agene_hard_mouth, // what it strikes with
    agene_hard_front, // head and shoulders: struck from ahead
    agene_hard_back,  // flanks and rump: struck from the side or behind
    agene_count
};

struct AnimalGenome {
    std::uint8_t gene[agene_count];
};

// What the organs add up to. Lengths in millimetres, mass in grams, speeds
// in 1/256 of a cell a tick at full pace (0: it cannot move that way).
struct Frame {
    int length, girth; // the trunk, and its radius
    int neck, tail;
    int leg_pairs, leg;
    int wing;          // one wing's length
    int fin;           // 0..255: how much fin it carries
    int mass, mass75;  // and mass to the power 3/4, which metabolism follows
    int land_speed, water_speed, air_speed;
    bool breathes_air, breathes_water;
    int wade;          // the deepest water it can stand in
    int sight;         // how far it sees, in cells
    int nose;          // how keen its nose is, 0 for none
    int hearing;       // how far it hears, in cells, 0 for deaf
    int upkeep;        // energy a metabolic pass, at rest
    int store;         // the most energy it can hold
    int bite;          // plant energy a mouthful
    int stamina;       // what it holds for a burst: sprinting, lunging, staying in the air
};

// `growth` is how much of its adult length it has reached, out of 255.
Frame frame(const AnimalGenome& genome, int growth = 255);
inline int body_length_mm(const AnimalGenome& genome, int growth) {
    return (200 + genome.gene[agene_size] * 9) * growth / 255;
}

// How hard a part of it is now, 0..255: the young are soft.
inline int hardness(const AnimalGenome& genome, int growth, AnimalGene part) { return genome.gene[part] * growth / 255; }

// How unlike two genomes are. Two animals can breed only if this is small,
// so kinds that drift apart stop interbreeding without that being coded.
int genetic_distance(const AnimalGenome& a, const AnimalGenome& b);
constexpr int breeding_distance = 240;

enum class Medium : std::uint8_t { land, water, air };
enum Wants : std::uint8_t { wants_eat = 1, wants_mate = 2, wants_fly = 4, wants_call = 8, wants_attack = 16, wants_rest = 32 };

// Maternal founder cohorts for the census. These labels never guide an action.
enum class FishStock : std::uint8_t { other, small_fish, large_swimmer };
enum class HuntHistory : std::uint8_t { none, fed_from_kill, bred_after_kill };

struct Carcass {
    std::int32_t x, y, energy;
    std::uint32_t killer; // zero for a natural death
    std::uint16_t length_mm;
    std::uint8_t heading;
    std::int32_t next = -1; // another carcass in the cell, rebuilt on load
};

// Observations of the large-swimmer cohort, cumulative even after a hunter dies.
struct HuntStats {
    std::uint64_t kills = 0, small_fish_killed = 0, own_kill_energy = 0, scavenged_energy = 0;
    std::uint64_t breeders = 0, young = 0, last_birth_tick = 0;
    std::uint16_t generations = 0; // longest unbroken chain of hunters that fed and then bred
};

// A call: where it was made, by whom, at what pitch and how loud. It is
// heard for a few ticks.
struct Call {
    std::int32_t x, y;
    std::uint32_t caller, tick;
    std::uint8_t pitch, loud;
};
constexpr int call_ticks = 8;   // how long a call can be heard, and the least time between one animal's calls
constexpr int scent_node = 4;   // cells to a node of the scent grids
constexpr int scent_every = 4;  // ticks between the scents spreading

// How young get their brains. These values are stored in world saves.
enum class BrainMode : std::uint8_t { evolving, frozen, shuffled, neutral };
const char* brain_mode_name(BrainMode mode);
std::optional<BrainMode> parse_brain_mode(std::string_view name);

struct Animal {
    std::uint32_t id;
    std::uint32_t lineage; // the founder it descends from
    std::int32_t x, y;     // 1/256 of a cell
    std::int16_t z;        // dm: under the surface when it swims, over the ground when it flies
    std::uint8_t heading;  // a binary angle: 0 east, 64 south
    std::int8_t turn;      // how fast it is turning, binary angles a tick
    std::uint8_t pace;     // 0 still .. 255 flat out
    std::uint8_t medium;   // Medium
    std::uint8_t health;
    std::uint8_t eating;   // ticks since its last mouthful, capped
    std::uint16_t breath;  // ticks it can still go without breathing
    std::uint16_t age;     // days
    std::uint8_t growth;   // 96 newborn .. 255 full grown: its length, out of 255 of the adult's
    std::uint8_t drive;    // to mate: it rises while a grown animal is well fed
    std::uint16_t young;   // how many it has had
    std::uint32_t mother, father; // 0 for a founder
    std::uint8_t wants;    // what its brain last chose to try: wants_eat and the rest
    std::int8_t memory[3]; // what its brain last told itself
    std::uint32_t stride;  // how far it has moved, 1/256 of a cell: its gait follows this
    std::int32_t energy;
    AnimalGenome genome;
    std::uint8_t pain, pain_bearing; // strength and world bearing of the last strike
    FishStock stock;
    HuntHistory hunting;
    std::uint16_t hunting_ancestors; // consecutive qualifying parents before this animal
    // Its wind. `energy` above is its reserve of food, slow to fill and slow
    // to empty; this is what a burst spends, refilled from that reserve
    // while it goes easy.
    std::uint16_t stamina;
};

constexpr int animal_stagger = 16;  // each animal's metabolism runs every 16 ticks
// Every step is paid for from its wind, and its wind is refilled from its
// reserve of food. Up to this pace it goes as fast as its frame says and
// spends less than it gets back; above it, it sprints, up to twice that
// speed, and spends far more.
constexpr int cruise_pace = 176;
constexpr int walk_ticks = 384;     // a full wind, spent at a steady cruise with none coming back, lasts this long
constexpr int trudge_pace = 64;     // all one with no wind left can manage
constexpr int wind_cost = 12;       // passes of upkeep a full wind costs to refill; half that lying still
constexpr int sprint_gain = 334;    // so that flat out, pace 255, is twice the frame's speed
constexpr int flight_wind = 24;     // wind a tick in the air costs
constexpr int lunges_in_a_wind = 96;  // a lunge is short: refilling what it takes costs an eighth of a pass of upkeep
constexpr int lunge_windup = 2;      // ticks from a contact decision to striking: fleeing prey can escape it
constexpr int breath_ticks = 600;   // a full breath
constexpr int flying_height_dm = 30;
constexpr int newborn_growth = 64;   // the smallest a newborn can be
// How a body spends on its young is one gene, and it is a trade. With little
// invested in each, a mother bears many at once, each born small: quick to
// lose to a mouth, and a long time growing. With much invested she bears
// one, born large. She spends on a brood only what she can spare, so a
// hungry mother's brood is smaller than her genes allow. Which pays is for
// the place to settle: where the young are eaten, many; where they are
// safe and food is short, few.
inline int brood_of(const AnimalGenome& genome) {
    const int spread = 255 - genome.gene[agene_investment];
    return 1 + spread * spread * 15 / (255 * 255); // 1 to 16
}
struct Animal;
// The energy in a body's flesh, apart from its reserves: what it was born
// with, and most of what it has spent on growing since.
int flesh_of(const Animal& animal);

inline int newborn_growth_of(const AnimalGenome& genome) { return newborn_growth + genome.gene[agene_investment] / 4; } // 64 to 127
constexpr int mating_drive = 200;   // at this it looks for a mate
constexpr std::uint8_t usual_hungry_drive_loss = 16;
constexpr int mating_reach = 320;   // strictly less than 1.25 cells, in 1/256 cell

} // namespace phd
