#pragma once

// Names for what lives in a world. Nothing in the simulation knows of
// kinds: an animal is its genome. A taxonomy looks at genomes from outside
// and sorts them into families, genera, species and subspecies, founding
// each the first time it meets something that fits none it has, and naming
// it from the body it was founded on. It changes nothing in a world.
//
//   family      the body's plan: how many legs, fins, wings, what it breathes
//   genus       species of one family within twice the breeding distance of its first
//   species     animals that could breed with its first
//   subspecies  a population within a species that has drifted from the rest
//
// The first animal a species was founded on is its type, and stays so:
// a lineage that drifts out of reach of every type becomes a new species.

#include <cstdint>
#include <string>
#include <vector>

#include "animals.hpp"

namespace phd {

struct BodyPlan {
    int leg_pairs = 0;
    bool fins = false, wings = false;
    int breath = 0; // 0 lungs, 1 lungs and gills, 2 gills
    bool operator==(const BodyPlan& o) const { return leg_pairs == o.leg_pairs && fins == o.fins && wings == o.wings && breath == o.breath; }
};
BodyPlan body_plan(const AnimalGenome& genome);
std::string family_name(const BodyPlan& plan);   // "Pinnobranchidae"
std::string family_common(const BodyPlan& plan); // "finned gill-breathers"

class Taxonomy {
public:
    // How far from a type a genome can be and belong to it.
    static constexpr int species_reach = breeding_distance;         // it could breed with the type
    static constexpr int subspecies_reach = breeding_distance * 2 / 3; // wider than founders of one body vary
    static constexpr int genus_reach = breeding_distance * 2;

    struct Subspecies {
        AnimalGenome type;
        std::string name, common; // the third word of its name, and that word in plain speech
        int living = 0;
    };
    struct Species {
        AnimalGenome type;
        std::string name, common;
        int genus = 0;
        int parent = -1;          // the species nearest it when it was founded, or -1
        std::vector<Subspecies> subspecies; // the first is the species' own
        int living = 0;
    };
    struct Genus {
        AnimalGenome type;
        std::string name; // "Tavorus"
        BodyPlan plan;
    };
    struct Place {
        int species = -1, subspecies = -1;
        bool found() const { return species >= 0; }
    };

    // Where a genome belongs, founding a subspecies, species or genus for it if none fits.
    Place classify(const AnimalGenome& genome);
    // The same without founding anything: not found if nothing fits.
    Place find(const AnimalGenome& genome) const;

    // Counts the living: classifies every animal, and sets each taxon's `living`.
    void census(const std::vector<Animal>& animals);

    // "Tavorus pumilus", and "Tavorus pumilus pallidus" for any but the species' own subspecies.
    std::string name(Place place) const;
    // "dwarf tavor", "pale dwarf tavor".
    std::string common(Place place) const;
    std::string family(Place place) const { return family_name(genera_[std::size_t(species_[std::size_t(place.species)].genus)].plan); }

    const std::vector<Genus>& genera() const { return genera_; }
    const std::vector<Species>& species() const { return species_; }

private:
    std::vector<Genus> genera_;
    std::vector<Species> species_;
};

} // namespace phd
