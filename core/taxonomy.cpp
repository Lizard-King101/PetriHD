#include "taxonomy.hpp"

#include <algorithm>
#include <cstdlib>

#include "fnv.hpp"

namespace phd {
namespace {

// A word for a way a body stands out, as the second or third word of a
// name and in plain speech.
struct Word {
    const char* latin;
    const char* plain;
};

// What a gene is called when it is low and when it is high, and how much
// it counts for in a name, in percent: what can be seen counts for most.
struct Trait {
    AnimalGene gene;
    Word low, high;
    int weight;
};

constexpr Trait traits[] = {
    {agene_size, {"pumilus", "dwarf"}, {"giganteus", "giant"}, 150},
    {agene_build, {"gracilis", "slender"}, {"robustus", "stout"}, 120},
    {agene_leg_length, {"brevipes", "short-legged"}, {"longipes", "long-legged"}, 150},
    {agene_sprawl, {"erectus", "upright"}, {"reptans", "sprawling"}, 80},
    {agene_tail, {"brevicaudus", "short-tailed"}, {"longicaudus", "long-tailed"}, 150},
    {agene_neck, {"brevicollis", "short-necked"}, {"longicollis", "long-necked"}, 150},
    {agene_plant_gut, {"abstinens", "leaf-shy"}, {"herbivorus", "grazing"}, 60},
    {agene_meat_gut, {"mitis", "gentle"}, {"carnivorus", "hunting"}, 70},
    {agene_fat, {"macer", "lean"}, {"pinguis", "fat"}, 100},
    {agene_tone, {"niger", "dark"}, {"pallidus", "pale"}, 130},
    {agene_maturity, {"praecox", "quick-growing"}, {"tardus", "slow-growing"}, 60},
    {agene_investment, {"fecundus", "many-brooded"}, {"unicus", "single-born"}, 70},
    {agene_eyes, {"caecus", "dim-eyed"}, {"oculatus", "sharp-eyed"}, 70},
    {agene_nose, {"simus", "dull-nosed"}, {"nasutus", "keen-nosed"}, 60},
    {agene_ears, {"surdus", "hard-of-hearing"}, {"auritus", "keen-eared"}, 60},
    {agene_voice, {"gravis", "deep-voiced"}, {"canorus", "high-voiced"}, 60},
    {agene_hard_mouth, {"edentulus", "soft-mouthed"}, {"dentatus", "toothed"}, 90},
    {agene_hard_front, {"inermis", "bare-headed"}, {"cornutus", "horned"}, 90},
    {agene_hard_back, {"nudus", "soft-skinned"}, {"loricatus", "armoured"}, 90},
};

// Round the colour wheel as an animal is painted: red at 0, green a third of the way, blue at two thirds.
constexpr Word hues[] = {{"rufus", "red"}, {"fulvus", "tawny"}, {"flavus", "yellow"}, {"viridis", "green"},
                         {"glaucus", "sea-green"}, {"caeruleus", "blue"}, {"violaceus", "violet"}, {"roseus", "rose"}};
constexpr Word patterns[] = {{"concolor", "plain"}, {"striatus", "striped"}, {"maculatus", "spotted"}, {"albiventris", "pale-bellied"}};

int hue_distance(int a, int b) {
    const int d = std::abs(a - b);
    return std::min(d, 256 - d);
}

struct Candidate {
    Word word;
    int weight; // how much this sets it apart
};

// The ways `genome` stands out from `from`, most first. With no `from` it
// is measured against the middle of every gene: what it is, not what it
// has become.
std::vector<Candidate> standing_out(const AnimalGenome& genome, const AnimalGenome* from) {
    std::vector<Candidate> out;
    const BodyPlan plan = body_plan(genome);
    for (const auto& t : traits) {
        if ((t.gene == agene_leg_length || t.gene == agene_sprawl) && plan.leg_pairs == 0) continue;
        // Most bodies are small, and few eat meat: neither is worth a name by itself.
        const int middle = t.gene == agene_size ? 64 : t.gene == agene_meat_gut || t.gene >= agene_hard_mouth ? 32 : 128;
        const int now = genome.gene[t.gene], was = from ? from->gene[t.gene] : middle;
        out.push_back({now < was ? t.low : t.high, std::abs(now - was) * t.weight / 100});
    }
    const int hue = genome.gene[agene_hue];
    // The first of a genus is named for its colour, which is what is seen of it first;
    // those that come after for how they have changed, colour among the rest.
    out.push_back({hues[((hue + 16) & 255) / 32], from ? hue_distance(hue, from->gene[agene_hue]) * 2 : 1000});
    const int pattern = genome.gene[agene_pattern] / 64;
    out.push_back({patterns[pattern], from ? (pattern != from->gene[agene_pattern] / 64 ? 90 : 0) : (pattern ? 60 : 0)});
    std::stable_sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) { return a.weight > b.weight; });
    return out;
}

template <class Taken>
Word pick(const std::vector<Candidate>& words, Taken taken) {
    for (const auto& c : words) if (!taken(c.word.latin)) return c.word;
    return words.front().word;
}

std::uint64_t hash_of(const AnimalGenome& genome) {
    Fnv1a hash;
    hash.bytes(genome.gene, agene_count);
    return hash.value();
}

// A word that can be said, made from the body it names.
std::string coined(std::uint64_t bits) {
    constexpr const char* onsets[] = {"b", "c", "d", "f", "g", "l", "m", "n", "p", "r", "s", "t", "v", "z", "br", "cr", "dr", "gl", "pr", "st", "tr", "th", "ch", "sc"};
    constexpr const char* vowels[] = {"a", "e", "i", "o", "u", "a", "e", "o", "ae", "au", "ia", "eo"};
    constexpr const char* middles[] = {"l", "m", "n", "r", "s", "t", "v", "d", "c", "x", "ll", "nd", "rn", "st", "lt", "mb"};
    const auto take = [&](std::uint64_t n) { const auto v = bits % n; bits /= n; return std::size_t(v); };
    std::string word = onsets[take(24)];
    word += vowels[take(12)];
    const int more = 1 + int(take(2));
    for (int i = 0; i < more; ++i) {
        word += middles[take(16)];
        word += vowels[take(8)]; // single vowels and the two plain repeats
    }
    // Drop a last vowel so that "-us" follows a consonant.
    while (!word.empty() && std::string("aeiou").find(word.back()) != std::string::npos) word.pop_back();
    word += "us";
    word[0] = char(word[0] - 'a' + 'A');
    return word;
}

std::string lower_stem(const std::string& genus) {
    std::string stem = genus.substr(0, genus.size() - 2); // without "us"
    stem[0] = char(stem[0] - 'A' + 'a');
    return stem;
}

} // namespace

BodyPlan body_plan(const AnimalGenome& genome) {
    BodyPlan plan;
    plan.leg_pairs = genome.gene[agene_legs] / 64;
    plan.fins = genome.gene[agene_fins] >= 64;
    plan.wings = genome.gene[agene_wings] >= 96;
    plan.breath = genome.gene[agene_gills] > 160 ? 2 : genome.gene[agene_gills] >= 96 ? 1 : 0;
    return plan;
}

std::string family_name(const BodyPlan& plan) {
    constexpr const char* count[] = {"", "Di", "Tetra", "Hexa"};
    std::string name;
    if (plan.wings) name = plan.leg_pairs ? std::string(count[plan.leg_pairs]) + "pter" : "Apodopter";
    else if (plan.fins) name = plan.leg_pairs ? std::string(count[plan.leg_pairs]) + "pinn" : "Pinn";
    else name = plan.leg_pairs ? std::string(count[plan.leg_pairs]) + "pod" : "Apod";
    if (plan.breath == 2) name += "obranch";
    else if (plan.breath == 1) name += "amph";
    return name + "idae";
}

std::string family_common(const BodyPlan& plan) {
    constexpr const char* count[] = {"legless", "two-legged", "four-legged", "six-legged"};
    std::string name = plan.fins && !plan.leg_pairs ? "finned" : count[plan.leg_pairs];
    if (plan.fins && plan.leg_pairs) name += " finned";
    const char* doing = plan.wings ? "fliers" : plan.breath == 2 ? "gill-breathers" : plan.breath == 1 ? "shore-dwellers"
                      : plan.fins ? "swimmers" : plan.leg_pairs ? "walkers" : "crawlers";
    return name + " " + doing;
}

Taxonomy::Place Taxonomy::find(const AnimalGenome& genome) const {
    Place place;
    int nearest = species_reach + 1;
    for (std::size_t s = 0; s < species_.size(); ++s) {
        const int d = genetic_distance(genome, species_[s].type);
        if (d < nearest) { nearest = d; place.species = int(s); }
    }
    if (place.species < 0) return place;
    const auto& races = species_[std::size_t(place.species)].subspecies;
    nearest = subspecies_reach + 1;
    for (std::size_t r = 0; r < races.size(); ++r) {
        const int d = genetic_distance(genome, races[r].type);
        if (d < nearest) { nearest = d; place.subspecies = int(r); }
    }
    return place;
}

Taxonomy::Place Taxonomy::classify(const AnimalGenome& genome) {
    Place place = find(genome);
    if (place.species < 0) {
        // A new species. It joins the nearest genus of its own body plan that is in reach, or founds one.
        const BodyPlan plan = body_plan(genome);
        int genus = -1, nearest = genus_reach + 1;
        for (std::size_t g = 0; g < genera_.size(); ++g) {
            const int d = genetic_distance(genome, genera_[g].type);
            if (genera_[g].plan == plan && d < nearest) { nearest = d; genus = int(g); }
        }
        if (genus < 0) {
            std::uint64_t bits = hash_of(genome);
            std::string name = coined(bits);
            while (std::any_of(genera_.begin(), genera_.end(), [&](const Genus& g) { return g.name == name; })) {
                bits = bits * 6364136223846793005ull + 1442695040888963407ull;
                name = coined(bits >> 8);
            }
            genera_.push_back({genome, name, plan});
            genus = int(genera_.size()) - 1;
        }
        Species species;
        species.type = genome;
        species.genus = genus;
        nearest = 1 << 30;
        for (std::size_t s = 0; s < species_.size(); ++s) {
            const int d = genetic_distance(genome, species_[s].type);
            if (d < nearest) { nearest = d; species.parent = int(s); }
        }
        // Named for how it stands out: from the genus' first if there is one, else as a body.
        const bool first = std::none_of(species_.begin(), species_.end(), [&](const Species& s) { return s.genus == genus; });
        const Word word = pick(standing_out(genome, first ? nullptr : &genera_[std::size_t(genus)].type), [&](const char* latin) {
            return std::any_of(species_.begin(), species_.end(), [&](const Species& s) { return s.genus == genus && s.name == latin; });
        });
        species.name = word.latin;
        species.common = word.plain;
        species.subspecies.push_back({genome, word.latin, ""});
        species_.push_back(species);
        place.species = int(species_.size()) - 1;
        place.subspecies = 0;
    } else if (place.subspecies < 0) {
        auto& species = species_[std::size_t(place.species)];
        const Word word = pick(standing_out(genome, &species.type), [&](const char* latin) {
            return std::any_of(species.subspecies.begin(), species.subspecies.end(), [&](const Subspecies& r) { return r.name == latin; });
        });
        species.subspecies.push_back({genome, word.latin, word.plain});
        place.subspecies = int(species.subspecies.size()) - 1;
    }
    return place;
}

void Taxonomy::census(const std::vector<Animal>& animals) {
    for (auto& s : species_) {
        s.living = 0;
        for (auto& r : s.subspecies) r.living = 0;
    }
    for (const auto& a : animals) {
        if (!a.health) continue;
        const Place place = classify(a.genome);
        auto& s = species_[std::size_t(place.species)];
        ++s.living;
        ++s.subspecies[std::size_t(place.subspecies)].living;
    }
}

std::string Taxonomy::name(Place place) const {
    if (!place.found()) return "unnamed";
    const auto& s = species_[std::size_t(place.species)];
    std::string out = genera_[std::size_t(s.genus)].name + " " + s.name;
    if (place.subspecies > 0) out += " " + s.subspecies[std::size_t(place.subspecies)].name;
    return out;
}

std::string Taxonomy::common(Place place) const {
    if (!place.found()) return "unnamed";
    const auto& s = species_[std::size_t(place.species)];
    std::string out = s.common + " " + lower_stem(genera_[std::size_t(s.genus)].name);
    if (place.subspecies > 0) out = s.subspecies[std::size_t(place.subspecies)].common + " " + out;
    return out;
}

} // namespace phd
