#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "animals.hpp"
#include "brain.hpp"
#include "random.hpp"

namespace phd {

// The bodies a world starts with. They are starting points, not kinds the
// simulation knows about: once placed, an animal is only its genome.
enum class FounderKind : std::uint8_t { grazer, crawler, flier, fish, swimmer };
constexpr int founder_kinds = 5;

const char* founder_name(FounderKind kind);
const AnimalGenome& founder_genome(FounderKind kind);
FishStock founder_stock(FounderKind kind);
// For a balancing trial: another body for a kind, in every world made after.
void set_founder_genome(FounderKind kind, const AnimalGenome& genome);

// A founder as it is placed: its proportions varied a little, its body plan
// (legs, fins, wings, gills) kept. Draws one number per gene.
AnimalGenome varied_founder(FounderKind kind, Random& rng);

// Bodies nobody designed. A world can be started from these in place of
// the five above.
//
// Where a body could make a living, if anywhere: it can breathe and move
// there, it can eat plants, and it can gather them about as fast as it
// burns them. The last is measured against what a cell of the development
// map holds: a body that must clear more than two cells of dry ground, or
// one of a lake, every metabolic pass starves whatever it does.
enum class Habitat : std::uint8_t { none, land, water };
Habitat habitat_of(const AnimalGenome& genome);

// What a body is built to eat, from its two guts. Plants are digested in
// proportion to the plant gut, and meat in full from half way up the meat
// gut. Twice as much of one as of the other makes it that eater; anything
// between is an omnivore.
enum class Diet : std::uint8_t { herbivore, omnivore, carnivore };
Diet diet_of(const AnimalGenome& genome);
const char* diet_name(Diet diet);

// The same body with its proportions varied a little, its plan (legs, fins,
// wings, gills) kept. Draws one number per gene.
AnimalGenome varied(const AnimalGenome& genome, Random& rng);

// `genera` bodies drawn at random from those that could live somewhere,
// each followed by `each - 1` relatives drifted from it by a little or a
// lot: the nearest can breed with it, the furthest cannot. About half are
// plant-eaters, three in ten eat both, and two in ten are hunters, which
// need only be able to breathe and move where they are put: whether there
// is enough to catch is theirs to find out.
std::vector<AnimalGenome> random_bodies(std::uint32_t seed, int genera, int each);

// Brains for founders built to hunt, bred in the nursery's hunting school
// on bodies and prey nobody designed. Founders take them in turn.
const std::vector<brain::Brain>& schooled_hunters();
// Hunters come to a world after the rest, and few of them: a hunter set
// down among founders that have not yet multiplied eats them out and starves.
constexpr int hunters_arrive_year = 8, hunters_each = 2;

// The brains founders are given, a pool for each kind. A kind's founders
// take its brains in turn. The nursery breeds these pools; the default is
// the reflexes wired by hand.
struct FounderBrains {
    std::array<std::vector<brain::Brain>, founder_kinds> pool;

    static FounderBrains hand_wired();
    const brain::Brain& pick(FounderKind kind, std::size_t n) const {
        const auto& brains = pool[std::size_t(kind)];
        return brains[n % brains.size()];
    }
    bool complete() const {
        for (const auto& brains : pool) if (brains.empty()) return false;
        return true;
    }

    // A file of pools. It records the shape of the network it was bred for,
    // and decode refuses one bred for another.
    std::vector<std::uint8_t> encode() const;
    static bool decode(const std::uint8_t* data, std::size_t size, FounderBrains& out);
};

} // namespace phd
