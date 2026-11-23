#pragma once

#include <cstdint>

// An animal's brain: what it senses, and an integer network that turns the
// senses into what it tries to do. Nothing here changes a world. Results are
// bit-identical on every target.
//
// The network's arithmetic, its mutation and its crossover are from Petri
// dc03377, core/brain.cpp: think, inherit, combine. The senses are new.
namespace phd::brain {

// Sight is split into five sectors about the heading. Behind is unseen.
enum Sector : std::uint8_t { far_left, left, ahead, right, far_right, sectors };

// What it sees in one sector. Each value is scaled to an int8.
enum SectorInput : std::uint8_t {
    in_food,   // grazing, nearer counting for more
    in_animal, // nearest animal in the same medium and reachable height, 0 for none
    in_size,   // that animal's length less its own
    in_kin,    // how like its own that animal's genome is
    in_hazard, // ground its body cannot go on, and the world's edge; nearer counting for more
    in_mate,   // when it is ready to mate: how near the nearest animal is that is ready too and near enough alike
    sector_inputs
};

enum SelfInput : std::uint8_t {
    self_energy,
    self_health,
    self_breath,
    self_youth,        // how far it is from full grown
    self_drive,        // to mate
    self_land,
    self_water,
    self_air,
    self_food,         // grazing where it is
    self_ahead_food,   // grazing on the cell its next steps reach, seen alone
    self_ahead_hazard, // whether that cell is somewhere its body cannot go
    self_light,        // daylight
    self_noise,        // a different number every time: what a wandering mind is made of
    self_memory0,
    self_memory1,
    self_memory2,
    // Smell, for one with a nose. Each scent: how strong it is here, and how
    // much stronger ahead than behind and to the right than to the left.
    smell_plant,
    smell_plant_ahead,
    smell_plant_right,
    smell_animal,
    smell_animal_ahead,
    smell_animal_right,
    smell_mate, // when it is ready to mate: the scent of others that are, of a body like its own
    smell_mate_ahead,
    smell_mate_right,
    // Hearing, for one with ears: the loudest call it can hear just now.
    heard_loud,
    heard_kin,   // how like its own voice the call's pitch is
    heard_ahead, // where the call is, if it is in a voice like its own
    heard_right,
    self_mouth_size, // relative length within striking reach; 127 means no contact (equal size is 0)
    self_pain,       // strength of the most recent hit, fading with time
    pain_ahead,      // bearing of the hit relative to its current heading
    pain_right,
    self_stamina,    // how much it has left for a burst: a sprint, a lunge, staying in the air
    self_inputs
};

constexpr int inputs = sectors * sector_inputs + self_inputs;
constexpr int hidden = 16;
constexpr int memories = 3;

enum Output : std::uint8_t {
    out_turn, // right if positive
    out_pace, // faster if positive
    out_eat,
    out_mate,
    out_fly,
    out_call,
    out_memory0,
    out_memory1,
    out_memory2,
    out_attack,
    out_rest, // lie still: it burns less and mends and recovers faster, and sees less
    outputs
};

// Weight layout, one int8 each; 64 is unity.
//   per hidden unit: a weight for each input, then its bias
//   per output: a weight for each hidden unit, then its bias
constexpr int unit_bytes = inputs + 1;
constexpr int output_bytes = hidden + 1;
constexpr int bytes = hidden * unit_bytes + outputs * output_bytes;

struct Brain {
    std::int8_t weight[bytes];
};

struct Senses {
    std::int8_t in[inputs];
};

constexpr int sector_input(int sector, int which) { return sector * sector_inputs + which; }
constexpr int self_input(int which) { return sectors * sector_inputs + which; }

struct Decision {
    std::int8_t turn = 0;   // binary angles a tick
    std::uint8_t pace = 0;
    bool eat = false, mate = false, fly = false, call = false, attack = false, rest = false;
    std::int8_t memory[memories] = {};
    std::int8_t unit[hidden] = {};  // what each hidden unit said, for looking at
    std::int8_t out[outputs] = {};  // and each output
};

Decision think(const Senses& senses, const Brain& brain);

// Builds a child's brain from two parents, before mutation. Each hidden unit
// comes whole from one parent, with its incoming weights, its bias and its
// outgoing weights, so a unit stays a working part. Each output bias comes
// from either parent. `state` is the caller's random state and is advanced.
void combine(Brain& child, const Brain& first, const Brain& second, std::uint32_t& state);

// Mutates a brain in place: each weight has one chance in 128 of a step of 1
// to 4 either way. Returns the number of changes.
int mutate(Brain& brain, std::uint32_t& state);

// The shuffled control: the same weights in a random order.
void shuffle(Brain& brain, std::uint32_t& state);

// The founders' brain: a handful of reflexes wired by hand, and the rest of
// the network left blank for evolution to use.
// One that `hunts` strikes a smaller body at its mouth when hungry; one
// that also `spares_kin` holds the strike while its own kind is in front.
// One that `pursues` is a hunter by trade: it turns hard toward smaller
// bodies it sees, and strikes whatever is at its mouth and no bigger than
// itself as soon as it is less than full, grown prey as readily as young.
Brain founder(bool hunts = false, bool spares_kin = false, bool pursues = false);

} // namespace phd::brain
