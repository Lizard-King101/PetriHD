#pragma once

#include <cstdint>

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
    agene_meat_gut,   // nothing to eat with it yet; it has mass
    agene_fat,        // a store for lean times, and weight to carry
    agene_hue,
    agene_tone,
    agene_pattern,    // plain, stripes, spots, pale belly
    agene_pattern_tone,
    agene_maturity,   // how long it takes to grow up
    agene_investment, // many small broods of several young, or one at a time
    agene_eyes,       // how far it sees
    agene_nose,       // none below 32, then keener
    agene_ears,       // none below 32, then hearing further
    agene_voice,      // the pitch of its call
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
};

// `growth` is how much of its adult length it has reached, out of 255.
Frame frame(const AnimalGenome& genome, int growth = 255);

// How unlike two genomes are. Two animals can breed only if this is small,
// so kinds that drift apart stop interbreeding without that being coded.
int genetic_distance(const AnimalGenome& a, const AnimalGenome& b);
constexpr int breeding_distance = 240;

enum class Medium : std::uint8_t { land, water, air };
enum Wants : std::uint8_t { wants_eat = 1, wants_mate = 2, wants_fly = 4, wants_call = 8 };

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

// How young get their brains. Evolving is the world as meant; the others are
// controls: frozen passes brains on unchanged, shuffled scrambles each one
// at birth.
enum class BrainMode : std::uint8_t { evolving, frozen, shuffled };

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
};

constexpr int animal_stagger = 16;  // each animal's metabolism runs every 16 ticks
constexpr int breath_ticks = 600;   // a full breath
constexpr int flying_height_dm = 30;
constexpr int newborn_growth = 96;
constexpr int mating_drive = 200;   // at this it looks for a mate

} // namespace phd
