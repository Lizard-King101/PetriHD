#include "founders.hpp"
#include "tuning.hpp"

#include <algorithm>
#include <cstring>

#include "fnv.hpp"

namespace phd {
namespace {

//                           size bld  legs lleg sprl fins tail wing neck gill plnt meat fat  hue  tone patt ptone matr invs eyes nose ears voic
AnimalGenome grazer {{ 108, 100, 140, 200,   0,   0,  60,   0, 190,   0, 220,   0,  90,  22, 150, 230, 215,  64, 220, 110, 120, 120,  60}};
AnimalGenome crawler{{  48,  90, 210, 130, 230,   0, 130,   0,  30,  40, 200,  40,  60,   8, 110,  80,  40,  96, 120,  60, 160,  60, 140}};
AnimalGenome flier  {{  38, 130,  80, 110,  30,   0, 150, 200,  40,   0, 210,   0,  20, 160,  95, 230, 200,  64, 150, 200,  60, 160, 220}};
AnimalGenome fish   {{  18, 150,   0,   0,   0, 200, 170,   0,  10, 255, 210,   0,  40,  32, 160, 230, 220,  32,  40,  80, 120,  80,  20}};
AnimalGenome swimmer{{  78, 110,   0,   0,   0, 220, 230,   0,  10, 255, 200,  60,  70, 140, 130, 150, 225, 128, 200, 120, 120, 100,  90}};

AnimalGenome* const genomes[founder_kinds] = {&grazer, &crawler, &flier, &fish, &swimmer};
constexpr const char* names[founder_kinds] = {"grazer", "crawler", "flier", "fish", "swimmer"};

constexpr char file_magic[4] = {'P', 'H', 'D', 'B'};
constexpr std::uint32_t file_version = 1;

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) out.push_back(std::uint8_t(value >> (8 * byte)));
}

std::uint32_t get32(const std::uint8_t* at) {
    return std::uint32_t(at[0]) | std::uint32_t(at[1]) << 8 | std::uint32_t(at[2]) << 16 | std::uint32_t(at[3]) << 24;
}

} // namespace

const char* founder_name(FounderKind kind) { return names[std::size_t(kind)]; }
const AnimalGenome& founder_genome(FounderKind kind) { return *genomes[std::size_t(kind)]; }
void set_founder_genome(FounderKind kind, const AnimalGenome& genome) { *genomes[std::size_t(kind)] = genome; }

FishStock founder_stock(FounderKind kind) {
    return kind == FounderKind::swimmer ? FishStock::large_swimmer : kind == FounderKind::fish ? FishStock::small_fish : FishStock::other;
}

AnimalGenome varied_founder(FounderKind kind, Random& rng) { return varied(founder_genome(kind), rng); }

Habitat habitat_of(const AnimalGenome& genome) {
    const Frame f = frame(genome);
    const int gut = genome.gene[agene_plant_gut];
    if (gut < 32) return Habitat::none;
    const int need = f.upkeep * 2 * 64 / gut; // raw plant energy a pass, at a full walk or swim
    constexpr int land_cell = 130, lake_cell = 50;
    if (f.breathes_air && (f.leg_pairs || f.air_speed) && need * 10 <= land_cell * 22) return Habitat::land;
    if (f.breathes_water && f.fin && need <= lake_cell) return Habitat::water;
    return Habitat::none;
}

Diet diet_of(const AnimalGenome& genome) {
    const int plant = genome.gene[agene_plant_gut] >= 32 ? genome.gene[agene_plant_gut] : 0;
    const int meat = genome.gene[agene_meat_gut] >= 32 ? std::min(255, 2 * genome.gene[agene_meat_gut]) : 0;
    if (meat >= 2 * plant && meat) return Diet::carnivore;
    if (plant >= 2 * meat) return Diet::herbivore;
    return Diet::omnivore;
}

const char* diet_name(Diet diet) {
    return diet == Diet::herbivore ? "herbivore" : diet == Diet::omnivore ? "omnivore" : "carnivore";
}

namespace {

// Where a body would live. An omnivore is judged as if it grazed well. A
// carnivore does not have to live on plants at all: it must breathe there,
// be quick enough to catch what does, and be cheap enough to feed.
Habitat home_of(AnimalGenome genome) {
    if (diet_of(genome) == Diet::carnivore) {
        const Frame f = frame(genome);
        // Speed comes easiest to a giant, and a giant cannot be fed by what
        // it catches: a founder hunter must burn little enough that one
        // grown kill of an ordinary body keeps it for days.
        if (f.upkeep > 120) return Habitat::none;
        if (f.breathes_air && f.leg_pairs && f.land_speed >= 10) return Habitat::land;
        if (f.breathes_water && f.fin && f.water_speed >= 10) return Habitat::water;
        return Habitat::none;
    }
    if (diet_of(genome) != Diet::herbivore) genome.gene[agene_plant_gut] = std::max<std::uint8_t>(genome.gene[agene_plant_gut], 128);
    return habitat_of(genome);
}

constexpr AnimalGene plan_genes[] = {agene_legs, agene_fins, agene_wings, agene_gills};
bool sets_plan(int gene) { return std::find(std::begin(plan_genes), std::end(plan_genes), AnimalGene(gene)) != std::end(plan_genes); }

// `home` is where it must live: 0 either, 1 land, 2 water.
AnimalGenome random_body(Random& rng, Diet diet, int home = 0) {
    for (;;) {
        AnimalGenome g;
        for (auto& gene : g.gene) gene = std::uint8_t(rng.below(256));
        const auto set = [&](AnimalGene gene, std::uint32_t from, std::uint32_t span) { g.gene[gene] = std::uint8_t(from + rng.below(span)); };
        const bool wet = home ? home == 2 : rng.below(4) == 0;
        if (wet) {
            if (rng.below(6) == 0) set(agene_gills, 96, 65); else set(agene_gills, 161, 95);
            set(agene_fins, 64, 192);
            if (rng.below(5)) set(agene_legs, 0, 64);
            set(agene_wings, 0, 96);
            set(agene_size, 0, 40);
        } else {
            if (rng.below(5) == 0) set(agene_gills, 96, 65); else set(agene_gills, 0, 96);
            set(agene_legs, 64, 192);
            if (rng.below(5)) set(agene_fins, 0, 64);
            if (rng.below(10) < 3) set(agene_wings, 96, 160); else set(agene_wings, 0, 96);
            set(agene_size, 10, 110);
        }
        set(agene_fat, 0, 128);
        // Most parts of most bodies are soft; one in four is anything at all.
        for (const auto part : {agene_hard_mouth, agene_hard_front, agene_hard_back}) {
            if (rng.below(4)) set(part, 0, 64);
        }
        if (diet == Diet::herbivore) { set(agene_plant_gut, 128, 128); set(agene_meat_gut, 0, 32); }
        else if (diet == Diet::omnivore) { set(agene_plant_gut, 96, 104); set(agene_meat_gut, 60, 60); }
        else {
            // A hunter: no bigger than the rest, since a body much bigger than its
            // prey cannot be fed by it, but with a mouth hard enough to kill.
            set(agene_plant_gut, 32, 60);
            set(agene_meat_gut, 160, 96);
            set(agene_hard_mouth, 128, 128);
            set(agene_fat, 160, 96);
            set(agene_investment, 160, 96);
        }
        // Wings too small to lift it are only weight.
        if (g.gene[agene_wings] >= 96 && !frame(g).air_speed) set(agene_wings, 0, 96);
        if (diet_of(g) == diet && home_of(g) == (wet ? Habitat::water : Habitat::land)) return g;
    }
}

// The same plan with its proportions shifted by about `by` in all, still somewhere it can live.
AnimalGenome drifted(const AnimalGenome& from, int by, Random& rng) {
    for (;;) {
        AnimalGenome g = from;
        for (int left = by; left > 0;) {
            const auto gene = rng.below(agene_count);
            if (sets_plan(int(gene))) continue;
            const int step = std::min(left, 1 + int(rng.below(40)));
            const int to = std::clamp(int(g.gene[gene]) + (rng.below(2) ? step : -step), 0, 255);
            left -= std::max(1, std::abs(to - int(g.gene[gene])));
            g.gene[gene] = std::uint8_t(to);
        }
        if (home_of(g) == home_of(from) && diet_of(g) == diet_of(from)) return g;
    }
}

} // namespace

std::vector<AnimalGenome> random_bodies(std::uint32_t seed, int genera, int each) {
    Random rng(stream_seed(seed, 0x626f6479u));
    constexpr int apart[] = {120, 200, 300, 420}; // breeding reaches 240
    std::vector<AnimalGenome> out;
    for (int g = 0; g < genera; ++g) {
        // Five families in ten graze, three eat both and two hunt: few hunters to many hunted.
        constexpr Diet mix[] = {Diet::herbivore, Diet::omnivore, Diet::herbivore, Diet::carnivore, Diet::herbivore,
                                Diet::omnivore, Diet::herbivore, Diet::omnivore, Diet::herbivore, Diet::carnivore};
        // Of the two hunting families one is of the land and one of the water.
        const AnimalGenome first = random_body(rng, mix[g % 10], g % 10 == 3 ? 1 : g % 10 == 9 ? 2 : 0);
        out.push_back(first);
        for (int n = 1; n < each; ++n) out.push_back(drifted(first, apart[(n - 1) % 4], rng));
    }
    return out;
}

AnimalGenome varied(const AnimalGenome& parent, Random& rng) {
    AnimalGenome genome = parent;
    for (auto& gene : genome.gene) {
        gene = static_cast<std::uint8_t>(std::clamp(int(gene) + int(rng.below(17)) - 8, 0, 255));
    }
    // The body plan is what the founder is; only its proportions vary.
    for (const auto plan : {agene_legs, agene_fins, agene_wings, agene_gills}) {
        genome.gene[plan] = parent.gene[plan];
    }
    return genome;
}

const std::vector<brain::Brain>& schooled_hunters() {
    static const std::vector<brain::Brain> hunters = {
#include "hunter_brains.inc"
    };
    return hunters;
}

FounderBrains FounderBrains::hand_wired() {
    FounderBrains brains;
    for (int kind = 0; kind < founder_kinds; ++kind) {
        brains.pool[std::size_t(kind)] = {brain::founder(FounderKind(kind) == FounderKind::swimmer)};
    }
    return brains;
}

std::vector<std::uint8_t> FounderBrains::encode() const {
    std::vector<std::uint8_t> out(file_magic, file_magic + 4);
    put32(out, file_version);
    put32(out, brain::inputs);
    put32(out, brain::hidden);
    put32(out, brain::outputs);
    put32(out, founder_kinds);
    for (const auto& brains : pool) {
        put32(out, std::uint32_t(brains.size()));
        for (const auto& b : brains) {
            for (const auto weight : b.weight) out.push_back(std::uint8_t(weight));
        }
    }
    Fnv1a hash;
    hash.bytes(out.data(), out.size());
    put32(out, std::uint32_t(hash.value()));
    return out;
}

bool FounderBrains::decode(const std::uint8_t* data, std::size_t size, FounderBrains& out) {
    constexpr std::size_t header = 24;
    if (size < header + 4 || std::memcmp(data, file_magic, 4) != 0) return false;
    Fnv1a hash;
    hash.bytes(data, size - 4);
    if (get32(data + size - 4) != std::uint32_t(hash.value())) return false;
    if (get32(data + 4) != file_version || get32(data + 8) != std::uint32_t(brain::inputs) ||
        get32(data + 12) != std::uint32_t(brain::hidden) || get32(data + 16) != std::uint32_t(brain::outputs) ||
        get32(data + 20) != std::uint32_t(founder_kinds)) {
        return false;
    }
    FounderBrains read;
    std::size_t at = header;
    const std::size_t end = size - 4;
    for (auto& brains : read.pool) {
        if (end - at < 4) return false;
        const std::size_t count = get32(data + at);
        at += 4;
        if (!count || count > 4096 || (end - at) / brain::bytes < count) return false;
        brains.resize(count);
        for (auto& b : brains) {
            for (auto& weight : b.weight) weight = std::int8_t(data[at++]);
        }
    }
    if (at != end) return false;
    out = std::move(read);
    return true;
}

} // namespace phd
