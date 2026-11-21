// An offline selective-breeding pilot for the large-swimmer founder brain.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "file.hpp"
#include "lesson_one.hpp"
#include "nursery_film.hpp"
#include "world.hpp"

namespace {
using namespace phd;
namespace fs = std::filesystem;
using brain::Brain;
using nursery::brain_id;
using nursery::fingerprint;

constexpr int population = 16, retained = 4, rounds = 4;
constexpr std::array<unsigned, 3> training_seeds{7001, 7002, 7003};

WorldConfig lake(unsigned seed) {
    WorldConfig c;
    c.seed = seed; c.width = 128; c.height = 96; c.layout = Layout::development;
    return c;
}

void save(const World& w, const fs::path& path) {
    if (!write_file_atomic(path.string(), w.save())) throw std::runtime_error("Could not write " + path.string());
}

Brain source_brain(const std::string& source) {
    if (source == "founder") return brain::founder(true);
    World donor(lake(1));
    std::vector<std::uint8_t> bytes;
    if (!read_file(source, bytes) || !donor.load(bytes.data(), bytes.size()) || donor.tick() != 0)
        throw std::runtime_error("Expected a tick-zero nursery world: " + source);
    for (std::size_t i = 0; i < donor.animals().size(); ++i)
        if (donor.animals()[i].stock == FishStock::large_swimmer) return donor.brain_of(i);
    throw std::runtime_error("No large swimmer in " + source);
}

struct Trial {
    int founders = 0, large = 0, small = 0, births = 0, adults = 0, generation = 0, pair_drive = 0;
    std::uint64_t first_birth = 0;
};

enum Family : unsigned { family_a = 1, family_b = 2, background = 4 };

// Each four-founder spawn cluster gets one A, one B and two helpers.
// Four cyclic assignments put each candidate in every founder slot once.
std::vector<unsigned> family_slots(std::string_view pool, unsigned rotation) {
    if (pool == "a") return {family_a};
    if (pool == "b") return {family_b};
    if (pool == "founder") return {background};
    if (pool != "mixed") throw std::runtime_error("Expected a, b, founder or mixed");
    std::vector<unsigned> slots{family_a, family_b, background, background};
    std::rotate(slots.begin(), slots.begin() + rotation % slots.size(), slots.end());
    return slots;
}

struct PoolTrace {
    std::vector<unsigned> founders, ancestry;
    std::vector<int> fed_threshold;
    std::uint64_t fed_pair_ticks = 0;
    std::uint64_t sync_ticks = 0, near_sync_ticks = 0, a_sync_ticks = 0, ready_pair_ticks = 0;
    int a_parent_young = 0, b_parent_young = 0, a_adults = 0, b_adults = 0;

    PoolTrace(const World& world, const std::vector<unsigned>& slots) {
        std::size_t slot = 0;
        for (const auto& a : world.animals()) {
            if (a.stock != FishStock::large_swimmer) continue;
            founders.resize(std::max(founders.size(), std::size_t(a.id) + 1));
            founders[a.id] = slots[slot++ % slots.size()];
            fed_threshold.resize(founders.size());
            fed_threshold[a.id] = frame(a.genome).store * 5 / 8;
        }
        ancestry = founders;
    }

    void birth(const Animal& young) {
        ancestry.resize(std::max(ancestry.size(), std::size_t(young.id) + 1));
        ancestry[young.id] = ancestry[young.mother] | ancestry[young.father];
        const auto at = [&](std::uint32_t id) { return id < founders.size() ? founders[id] : 0u; };
        const auto direct = at(young.mother) | at(young.father);
        a_parent_young += bool(direct & family_a);
        b_parent_young += bool(direct & family_b);
    }

    void before_birth(const World& world) {
        // Count a tick once even if several pairs qualify. Both animals must
        // be able to breed together once they have accumulated enough drive.
        bool fed = false, sync = false, near = false, a_sync = false, ready = false;
        const auto& herd = world.animals();
        for (auto i = herd.begin(); i != herd.end(); ++i) {
            if (i->stock != FishStock::large_swimmer || i->growth != 255 || Medium(i->medium) == Medium::air) continue;
            for (auto j = i + 1; j != herd.end(); ++j) {
                if (j->stock != FishStock::large_swimmer || j->growth != 255 || Medium(j->medium) == Medium::air ||
                    genetic_distance(i->genome, j->genome) > breeding_distance) continue;
                fed |= i->energy > fed_threshold[i->id] && j->energy > fed_threshold[j->id];
                if (i->drive <= 100 || j->drive <= 100) continue;
                sync = true;
                a_sync |= bool((founders[i->id] | founders[j->id]) & family_a);
                ready |= i->drive >= mating_drive && j->drive >= mating_drive;
                const std::int64_t dx = i->x - j->x, dy = i->y - j->y;
                near |= dx * dx + dy * dy < mating_reach * mating_reach;
            }
        }
        fed_pair_ticks += fed; sync_ticks += sync; near_sync_ticks += near; a_sync_ticks += a_sync; ready_pair_ticks += ready;
    }
};

// Before the first birth, proximity and the less ready partner's drive give
// selection evidence even in worlds where no adult has yet reproduced.
int nearby_drive(const std::vector<Animal>& animals) {
    int best = 0;
    for (auto i = animals.begin(); i != animals.end(); ++i) {
        if (i->stock != FishStock::large_swimmer || i->growth != 255 || Medium(i->medium) == Medium::air) continue;
        for (auto j = i + 1; j != animals.end(); ++j) {
            if (j->stock != FishStock::large_swimmer || j->growth != 255 || Medium(j->medium) == Medium::air) continue;
            const std::int64_t dx = i->x - j->x, dy = i->y - j->y;
            if (dx * dx + dy * dy < mating_reach * mating_reach && genetic_distance(i->genome, j->genome) <= breeding_distance)
                best = std::max<int>(best, std::min(i->drive, j->drive));
        }
    }
    return best;
}

Trial run(World& world, unsigned years, bool stop_at_extinction, PoolTrace* trace = nullptr) {
    Trial r;
    for (const auto& a : world.animals()) r.founders += a.stock == FishStock::large_swimmer;
    r.large = r.founders;
    struct Descendant { int generation = 0; bool matured = false; };
    std::vector<Descendant> line;
    const auto end = std::uint64_t(years) * World::ticks_per_year;
    while (world.tick() < end && (r.large || !stop_at_extinction)) {
        world.step();
        if (!r.large) continue; // a maternal cohort cannot reappear after extinction
        r.large = 0;
        for (const auto& a : world.animals()) {
            if (a.stock != FishStock::large_swimmer) continue;
            ++r.large;
            if (!a.mother) continue;
            if (line.size() <= a.id) line.resize(std::size_t(a.id) + 1);
            auto& child = line[a.id];
            if (!child.generation) {
                child.generation = 1 + std::max(line[a.mother].generation, line[a.father].generation);
                r.generation = std::max(r.generation, child.generation);
                ++r.births;
                if (trace) trace->birth(a);
                if (!r.first_birth) r.first_birth = world.tick();
            }
            if (!child.matured && a.growth == 255) {
                child.matured = true;
                ++r.adults;
                if (trace) {
                    trace->a_adults += bool(trace->ancestry[a.id] & family_a);
                    trace->b_adults += bool(trace->ancestry[a.id] & family_b);
                }
            }
        }
        if (!r.births && world.tick() % animal_stagger == 0)
            r.pair_drive = std::max(r.pair_drive, nearby_drive(world.animals()));
        if (trace && !r.births) trace->before_birth(world);
    }
    for (const auto& a : world.animals()) r.small += a.stock == FishStock::small_fish;
    return r;
}

struct Score {
    int coexist = 0, grandchildren = 0, adults = 0, births = 0, hunting = 0, young = 0, drive = 0;
    auto rank() const { return std::tie(coexist, grandchildren, adults, births, hunting, young, drive); }
};

struct Candidate { Brain brain; Score score{}; bool evaluated = false; int introduced = 1; };

void score(Candidate& c) {
    for (unsigned seed : training_seeds) {
        World w(lake(seed));
        w.seed_animals(56, c.brain);
        const auto r = run(w, 3, true);
        c.score.coexist += r.adults && r.large && r.small;
        c.score.grandchildren += r.generation >= 2;
        c.score.adults += r.adults > 0;
        c.score.births += r.births > 0;
        c.score.hunting += w.hunts().breeders > 0;
        c.score.young += std::min(8, r.births);
        c.score.drive += r.births ? mating_drive : r.pair_drive;
    }
    c.evaluated = true;
}

void vary(Brain& b, Random& rng) {
    auto state = rng.next();
    const auto passes = 4 + rng.below(9);
    for (unsigned i = 0; i < passes; ++i) brain::mutate(b, state);
}

std::vector<Candidate> initial_pool() {
    Random rng(0x716aad1u);
    std::vector<Candidate> pool(population, Candidate{brain::founder(true)});
    // Supply variation in feeding and cruising effort, the two energy-related
    // decisions. The unselected population starts with exactly these brains too.
    for (int i = 1; i < population; ++i) {
        auto& b = pool[i].brain;
        auto* bias = b.weight + brain::hidden * brain::unit_bytes + brain::hidden;
        bias[brain::out_pace * brain::output_bytes] = std::int8_t(int(rng.below(241)) - 120);
        bias[brain::out_eat * brain::output_bytes] = std::int8_t(int(rng.below(97)) - 32);
        vary(b, rng);
    }
    return pool;
}

void next_generation(std::vector<Candidate>& pool, unsigned round) {
    pool.resize(retained);
    Random rng(stream_seed(0x917ec7u, round));
    while (pool.size() < population) {
        unsigned a = rng.below(retained), b = rng.below(retained - 1);
        if (b >= a) ++b;
        auto state = rng.next();
        Candidate child{};
        child.introduced = int(round + 1);
        brain::combine(child.brain, pool[a].brain, pool[b].brain, state);
        vary(child.brain, rng);
        pool.push_back(child);
    }
}

void save_founders(const Brain& b, const fs::path& path) {
    World w(lake(training_seeds.front()));
    w.seed_animals(56, b);
    save(w, path);
}

void train(const fs::path& dir, int jobs, bool visual) {
    fs::create_directories(dir);
    auto selected = initial_pool(), random = selected;
    Random draw(0x2ac19d63u);
    std::optional<nursery::Film> film;
    if (visual) {
        film.emplace(dir);
        const auto& original = selected.front().brain;
        film->add(lake(training_seeds.front()), original, brain_id(original), 0, "founder", "", -1);
        film->publish(false);
    }
    for (int round = 1; round <= rounds; ++round) {
        std::vector<std::future<void>> workers;
        for (int j = 0; j < jobs; ++j) {
            workers.push_back(std::async(std::launch::async, [&, j] {
                for (int i = j; i < population; i += jobs)
                    if (!selected[i].evaluated) score(selected[i]);
            }));
        }
        for (auto& worker : workers) worker.get();
        std::stable_sort(selected.begin(), selected.end(), [](const Candidate& a, const Candidate& b) {
            return a.score.rank() > b.score.rank();
        });
        for (unsigned n = population; n > 1; --n) std::swap(random[n - 1], random[draw.below(n)]);
        const auto& best = selected.front();
        save_founders(best.brain, dir / ("round-" + std::to_string(round) + ".phdw"));
        std::printf("round=%d brain=%s coexist=%d grandchild_maps=%d adult_maps=%d birth_maps=%d hunting_maps=%d capped_young=%d pair_drive=%d\n",
                    round, brain_id(best.brain).c_str(), best.score.coexist, best.score.grandchildren, best.score.adults,
                    best.score.births, best.score.hunting, best.score.young, best.score.drive);
        std::fflush(stdout);
        if (film) {
            const auto config = lake(training_seeds[(round - 1) % training_seeds.size()]);
            const auto& challenger = *std::find_if(selected.begin(), selected.end(), [round](const Candidate& c) {
                return c.introduced == round;
            });
            const auto winner = brain_id(best.brain);
            const bool won = brain_id(challenger.brain) == winner;
            film->add(config, challenger.brain, brain_id(challenger.brain), round,
                      won ? "selected" : "challenger", winner, challenger.score.births);
            if (!won) film->add(config, best.brain, winner, round, "retained", winner, best.score.births);
            film->publish(false);
        }
        if (round < rounds) {
            next_generation(selected, round);
            next_generation(random, round);
        }
    }
    save_founders(random.front().brain, dir / "random-parent.phdw");
    if (film) {
        const auto& best = selected.front();
        const auto id = brain_id(best.brain);
        film->add(lake(7101), best.brain, id, rounds, "release", id, best.score.births, World::ticks_per_year);
        const auto& control = random.front().brain;
        film->add(lake(7101), control, brain_id(control), rounds, "control", id, -1, World::ticks_per_year);
        film->publish(true);
    }
    std::printf("random_parent=%s\n", brain_id(random.front().brain).c_str());
}

unsigned number(std::string_view s) {
    unsigned n = 0;
    const auto parse = std::from_chars(s.data(), s.data() + s.size(), n);
    if (parse.ec != std::errc{} || parse.ptr != s.data() + s.size()) throw std::runtime_error("Invalid number: " + std::string(s));
    return n;
}

void pool_trial(const char* a_path, const char* b_path, unsigned seed, unsigned years, unsigned loss,
                std::string_view pool, unsigned rotation, std::string_view setup) {
    if (loss > 255 || rotation > 3 || (pool != "mixed" && rotation))
        throw std::runtime_error("Expected loss in 0..255 and a mixed-pool rotation in 0..3");
    const auto slots = family_slots(pool, rotation);
    const auto a = source_brain(a_path), b = source_brain(b_path), feeder = brain::founder(true);
    std::vector<Brain> minds;
    for (auto family : slots) minds.push_back(family == family_a ? a : family == family_b ? b : feeder);
    auto config = lake(seed);
    config.hungry_drive_loss = std::uint8_t(loss);
    World w(config);
    if (setup == "dev") w.seed_animals(56, minds);
    else if (setup == "pond" || setup == "stocked") w.seed_swimmer_pond(minds, setup == "stocked");
    else throw std::runtime_error("Expected dev, pond or stocked");
    PoolTrace trace(w, slots);
    const auto r = run(w, years, false, &trace);
    std::printf("seed,setup,pool,rotation,loss,a_brain,b_brain,tick,founders,fed_pair_ticks,sync_ticks,near_sync_ticks,a_sync_ticks,ready_pair_ticks,births,a_parent_young,b_parent_young,"
                "mature_offspring,a_descendant_adults,b_descendant_adults,max_generation,large_swimmers,small_fish,fingerprint\n");
    std::printf("%u,%.*s,%.*s,%u,%u,%s,%s,%llu,%d,%llu,%llu,%llu,%llu,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
                seed, int(setup.size()), setup.data(), int(pool.size()), pool.data(), rotation, loss, brain_id(a).c_str(), brain_id(b).c_str(),
                (unsigned long long)w.tick(), r.founders, (unsigned long long)trace.fed_pair_ticks, (unsigned long long)trace.sync_ticks,
                (unsigned long long)trace.near_sync_ticks, (unsigned long long)trace.a_sync_ticks, (unsigned long long)trace.ready_pair_ticks,
                r.births, trace.a_parent_young, trace.b_parent_young, r.adults, trace.a_adults, trace.b_adults,
                r.generation, r.large, r.small, fingerprint(w.fingerprint()).c_str());
}

// Replay the same sensory samples through all three brains. The hand-wired
// fish supply the trajectory and memory, so every policy sees identical food.
void feeding_probe(const char* a_path, const char* b_path, unsigned seed) {
    const std::array<Brain, 3> policies{source_brain(a_path), source_brain(b_path), brain::founder(true)};
    World w(lake(seed));
    w.seed_swimmer_pond({policies.back()}, true);
    struct Bin { int samples = 0; std::array<int, 3> eat{}; };
    std::array<Bin, 4> bins{};
    while (w.tick() < World::ticks_per_year && !w.animals().empty()) {
        w.step();
        for (std::size_t i = 0; i < w.animals().size(); ++i) {
            const auto& a = w.animals()[i];
            if (a.mother || (w.tick() + a.id) % 4) continue;
            const auto senses = w.senses(i);
            if (senses.in[brain::self_input(brain::self_food)] <= 0) continue;
            const int store = frame(a.genome).store;
            std::size_t band = 3;
            if (a.energy < store / 2) band = 0;
            else if (a.energy <= store * 5 / 8) band = 1;
            else if (a.energy <= store * 3 / 4) band = 2;
            auto& bin = bins[band];
            ++bin.samples;
            for (std::size_t p = 0; p < policies.size(); ++p) bin.eat[p] += brain::think(senses, policies[p]).eat;
        }
    }
    const char* bands[] = {"below_half", "half_to_five_eighths", "above_five_eighths_to_three_quarters", "above_three_quarters"};
    std::printf("seed,energy_band,a_brain,b_brain,samples,a_eat,b_eat,founder_eat\n");
    for (std::size_t i = 0; i < bins.size(); ++i) {
        const auto& bin = bins[i];
        std::printf("%u,%s,%s,%s,%d,%d,%d,%d\n", seed, bands[i], brain_id(policies[0]).c_str(), brain_id(policies[1]).c_str(),
                    bin.samples, bin.eat[0], bin.eat[1], bin.eat[2]);
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string_view(argv[1]) == "train") {
            bool visual = false;
            unsigned jobs = 4;
            fs::path preset;
            for (int i = 3; i < argc; ++i) {
                const std::string_view arg(argv[i]);
                if (arg == "--visual") visual = true;
                else if (arg == "--preset" && i + 1 < argc) preset = argv[++i];
                else jobs = number(arg);
            }
            if (!jobs || jobs > population) throw std::runtime_error("Use 1 to 16 workers");
            std::vector<Brain> minds;
            for (const auto& c : initial_pool()) minds.push_back(c.brain);
            nursery::lesson_one(argv[2], minds, int(jobs), visual, preset);
        } else if (argc >= 8 && std::string_view(argv[1]) == "curriculum") {
            bool visual = false;
            unsigned seed_base = 300000;
            for (int i = 8; i < argc; ++i) {
                const std::string_view arg(argv[i]);
                if (arg == "--visual") visual = true;
                else if (arg == "--seed-base" && i + 1 < argc) seed_base = number(argv[++i]);
                else throw std::runtime_error("Expected --visual or --seed-base NUMBER");
            }
            std::vector<std::pair<std::string, Brain>> founders;
            for (int i = 4; i < 8; ++i) founders.emplace_back(fs::path(argv[i]).stem().string(), source_brain(argv[i]));
            nursery::breeding_curriculum(argv[2], argv[3], founders, seed_base, visual);
        } else if (argc >= 6 && std::string_view(argv[1]) == "lesson2") {
            const auto pool = initial_pool();
            std::vector<std::pair<unsigned, Brain>> swimmers;
            std::vector<bool> entered(pool.size(), false);
            bool visual = false;
            for (int i = 5; i < argc; ++i) {
                if (std::string_view(argv[i]) == "--visual") { visual = true; continue; }
                const unsigned slot = number(argv[i]);
                if (slot < 1 || slot > pool.size() || entered[slot - 1]) throw std::runtime_error("Expected distinct slots from 1 to 16");
                swimmers.emplace_back(slot, pool[slot - 1].brain);
                entered[slot - 1] = true;
            }
            if (swimmers.empty()) throw std::runtime_error("Specify the pool slots entering Lesson 2");
            nursery::lesson_two(argv[2], argv[3], argv[4], swimmers, visual);
        } else if (argc == 4 && std::string_view(argv[1]) == "pond-validation") {
            const auto pool = initial_pool();
            const auto it = std::find_if(pool.begin(), pool.end(), [&](const Candidate& c) { return brain_id(c.brain) == argv[2]; });
            if (it == pool.end()) throw std::runtime_error("Unknown Lesson 1 brain ID: " + std::string(argv[2]));
            nursery::validate_lesson_one(it->brain, argv[3], std::cout);
        } else if (argc >= 3 && argc <= 5 && std::string_view(argv[1]) == "pilot") {
            const bool visual = std::string_view(argv[argc - 1]) == "--visual";
            const int arguments = argc - int(visual);
            if (arguments != 3 && arguments != 4) throw std::runtime_error("Expected DIRECTORY [WORKERS] [--visual]");
            const unsigned jobs = arguments == 4 ? number(argv[3]) : 4;
            if (!jobs || jobs > population) throw std::runtime_error("Use 1 to 16 workers");
            train(argv[2], int(jobs), visual);
        } else if ((argc == 5 || argc == 6) && std::string_view(argv[1]) == "trial") {
            const auto b = source_brain(argv[2]);
            World w(lake(number(argv[3])));
            w.seed_animals(56, b);
            if (argc == 6) save(w, argv[5]);
            const auto r = run(w, number(argv[4]), false);
            std::printf("seed,brain,tick,founders,births,first_birth,mature_offspring,max_generation,large_swimmers,small_fish,hunter_breeders,hunter_young,fingerprint\n"
                        "%u,%s,%llu,%d,%d,%llu,%d,%d,%d,%d,%llu,%llu,%s\n",
                        w.seed(), brain_id(b).c_str(), (unsigned long long)w.tick(), r.founders, r.births,
                        (unsigned long long)r.first_birth, r.adults, r.generation, r.large, r.small,
                        (unsigned long long)w.hunts().breeders, (unsigned long long)w.hunts().young, fingerprint(w.fingerprint()).c_str());
        } else if (argc >= 8 && argc <= 10 && std::string_view(argv[1]) == "pool-trial") {
            pool_trial(argv[2], argv[3], number(argv[4]), number(argv[5]), number(argv[6]), argv[7],
                       argc >= 9 ? number(argv[8]) : 0, argc == 10 ? argv[9] : "dev");
        } else if (argc == 5 && std::string_view(argv[1]) == "feeding-probe") {
            feeding_probe(argv[2], argv[3], number(argv[4]));
        } else {
            throw std::runtime_error("usage: petrihd-nursery train DIRECTORY [WORKERS] [--visual] [--preset FILE]\n"
                                     "       petrihd-nursery lesson2 DIRECTORY UNIFORM.cfg PATCHES.cfg SLOT... [--visual]\n"
                                     "       petrihd-nursery curriculum DIRECTORY POND.cfg A.phdw B.phdw C.phdw D.phdw [--visual] [--seed-base NUMBER]\n"
                                     "       petrihd-nursery pond-validation BRAIN_ID PRESET\n"
                                     "       petrihd-nursery pilot DIRECTORY [WORKERS] [--visual]\n"
                                     "       petrihd-nursery trial founder|DONOR.phdw SEED YEARS [START.phdw]\n"
                                     "       petrihd-nursery pool-trial A.phdw B.phdw SEED YEARS LOSS a|b|founder|mixed [ROTATION [dev|pond|stocked]]\n"
                                     "       petrihd-nursery feeding-probe A.phdw B.phdw SEED");
        }
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
    return 0;
}
