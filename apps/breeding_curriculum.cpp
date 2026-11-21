#include "lesson_one.hpp"
#include "nursery_film.hpp"
#include "pond_trials.hpp"

#include <algorithm>
#include <cstdio>
#include <future>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace nursery {
namespace {
using namespace phd;
namespace fs = std::filesystem;
using brain::Brain;

constexpr unsigned generations = 4, family_limit = World::ticks_per_year * 3;
constexpr std::array<std::pair<unsigned, unsigned>, 6> crosses{{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}}};
constexpr std::array<unsigned, 8> family_crosses{0, 5, 1, 4, 2, 3, 0, 5};
using Seeds = std::array<unsigned, 8>;
constexpr Seeds previous_seeds{7321, 7322, 7323, 7324, 7331, 7332, 7333, 7334};

Seeds seeds_from(unsigned first) {
    Seeds seeds{};
    std::iota(seeds.begin(), seeds.end(), first);
    return seeds;
}

struct Mind { Brain brain; std::string id; };
using Group = std::vector<Mind>;

// Worlds are independent. Collect the results in schedule order for deterministic selection and logs.
template<class Work>
void parallel(std::size_t count, Work work) {
    const auto workers = std::min<std::size_t>(4, count);
    std::vector<std::future<void>> jobs;
    for (std::size_t lane = 0; lane < workers; ++lane) jobs.emplace_back(std::async(std::launch::async, [&, lane] {
        for (auto i = lane; i < count; i += workers) work(i);
    }));
    for (auto& job : jobs) job.get();
}

struct Score {
    int births = 0, healthy = 0, timing = 0, fed = 0;
    std::int64_t energy = 0, food = 0;
    void add(const Trial& t) {
        births += t.birth != 0; healthy += t.healthy_birth;
        timing += t.timing; fed += t.feeding;
        energy += t.entire[0].total() + t.entire[1].total();
        food += t.plant_food[0] + t.plant_food[1];
    }
};

struct Logs {
    std::ostringstream trials, selection, lineage, pairs, families, stages;
    Logs() {
        trials << "refill_ticks,generation,phase,brain,mate,seed,candidate_id,end_tick,birth_tick,mother,father,healthy_birth,"
                  "first_ready_tick,brain_ready_tick,mate_ready_tick,pair_ticks,near_ticks,both_ready_ticks,ready_apart_ticks,"
                  "near_drive,fed_checks,brain_energy,mate_energy,brain_food,mate_food,brain_plant_gain,mate_plant_gain,"
                  "brain_end_reserve_per_mille,mate_end_reserve_per_mille,wait_energy,fingerprint\n";
        selection << "refill_ticks,generation,rank,brain,incumbent,births,healthy_births,family_grandchildren,family_adults,"
                     "family_peak_drive_sum,pair_energy,raw_food,resource_rank_sum,retained\n";
        lineage << "refill_ticks,generation,brain,parent_a,parent_b,hatch_seed,parent_a_body,birth_tick,mother,father,hatch_fingerprint\n";
        pairs << "refill_ticks,phase,brain,mate,births_a,births_b,healthy_births,pair_energy,raw_food\n";
        families << "refill_ticks,phase,generation,seed,brain,mate,candidate_id,end_tick,first_f1_birth,f1_born,max_f1_growth,"
                    "first_f1_adult,max_f1_drive,first_grandchild,grandchild_id,mother,father,f1_parents,live_end,min_stocked_cells,fingerprint\n";
        stages << "refill_ticks,phase,retained_ids,pair_trials,births,weakest_pair_births,healthy_births,energy_per_birth,food_per_birth,"
                  "family_trials,f1_adult_families,grandchildren_a,grandchildren_b,decision\n";
    }
    void save(const fs::path& dir) const {
        write_text(dir / "trials.csv", trials.str()); write_text(dir / "generations.csv", selection.str());
        write_text(dir / "lineage.csv", lineage.str()); write_text(dir / "pairs.csv", pairs.str());
        write_text(dir / "families.csv", families.str()); write_text(dir / "stages.csv", stages.str());
    }
};

void log_trial(Logs& log, int interval, unsigned generation, const char* phase, const Mind& a, const Mind& b, const Trial& t) {
    const unsigned c = t.candidate - 1, r = 1 - c;
    log.trials << interval << ',' << generation << ',' << phase << ',' << a.id << ',' << b.id << ',' << t.seed << ',' << t.candidate
               << ',' << t.end << ',' << t.birth << ',' << t.mother << ',' << t.father << ',' << t.healthy_birth << ',' << t.first_ready
               << ',' << t.ready_tick[c] << ',' << t.ready_tick[r] << ',' << t.pair_ticks << ',' << t.near_ticks << ',' << t.together_ticks
               << ',' << t.apart_ticks << ',' << t.timing << ',' << t.feeding << ',' << t.entire[c].total() << ',' << t.entire[r].total()
               << ',' << t.plant_food[c] << ',' << t.plant_food[r] << ',' << t.plant_energy[c] << ',' << t.plant_energy[r]
               << ',' << t.end_reserve_per_mille[c] << ',' << t.end_reserve_per_mille[r]
               << ',' << t.wait[c].total() + t.wait[r].total() << ',' << fingerprint(t.state) << '\n';
}

struct Family {
    unsigned first_birth = 0, f1 = 0, max_growth = 0, adult = 0, grandchild = 0, child_id = 0, mother = 0, father = 0, f1_parents = 0;
    unsigned end = 0, live = 0, peak_drive = 0;
    int min_stocked = 0;
    std::uint64_t state = 0;
};

Family follow_family(const Preset& pond, unsigned seed, unsigned slot, const Mind& a, const Mind& b) {
    auto w = pond.start(seed, slot, a.brain, b.brain);
    std::vector<std::size_t> reed_cells;
    for (std::size_t c = 0; c < w.cells(); ++c) if (w.occupant(c, 0)) reed_cells.push_back(c);
    Family f; f.min_stocked = int(reed_cells.size());
    std::set<unsigned> direct;
    while (!f.grandchild && !w.animals().empty() && w.tick() < family_limit) {
        w.step();
        const auto tick = unsigned(w.tick());
        for (const auto& fish : w.animals()) {
            if (!fish.mother) continue;
            if ((fish.mother == 1 && fish.father == 2) || (fish.mother == 2 && fish.father == 1)) {
                direct.insert(fish.id);
                if (!f.first_birth) f.first_birth = tick;
                f.max_growth = std::max(f.max_growth, unsigned(fish.growth));
                f.peak_drive = std::max(f.peak_drive, unsigned(fish.drive));
                if (!f.adult && fish.growth == 255) f.adult = tick;
            } else if (!f.grandchild && (direct.count(fish.mother) || direct.count(fish.father))) {
                f.grandchild = tick; f.child_id = fish.id; f.mother = fish.mother; f.father = fish.father;
                f.f1_parents = unsigned(direct.count(fish.mother) + direct.count(fish.father));
            }
        }
        int stocked = 0;
        for (auto c : reed_cells) stocked += w.occupant(c, 0)->energy > 8;
        f.min_stocked = std::min(f.min_stocked, stocked);
    }
    f.f1 = unsigned(direct.size()); f.end = unsigned(w.tick()); f.live = unsigned(w.animals().size()); f.state = w.fingerprint();
    return f;
}

void log_family(Logs& log, int interval, const char* phase, unsigned round, unsigned seed,
                const Mind& a, const Mind& b, unsigned side, const Family& f) {
    log.families << interval << ',' << phase << ',' << round << ',' << seed << ',' << a.id << ',' << b.id
                 << ',' << side << ',' << f.end << ',' << f.first_birth << ',' << f.f1 << ',' << f.max_growth
                 << ',' << f.adult << ',' << f.peak_drive << ',' << f.grandchild << ',' << f.child_id << ',' << f.mother << ',' << f.father
                 << ',' << f.f1_parents << ',' << f.live << ',' << f.min_stocked << ',' << fingerprint(f.state) << '\n';
}

Group generation(const Preset& pond, const Group& parents, unsigned round, unsigned base, Logs& log) {
    Group pool = parents;
    std::set<std::string> ids;
    for (const auto& p : parents) ids.insert(p.id);
    for (const auto& [i, j] : crosses) {
        unsigned children = 0;
        for (unsigned attempt = 0; attempt < 4 && children < 2; ++attempt) {
            Brain child{};
            const auto t = trial(pond, base + attempt, 1 + attempt % 2, parents[i].brain, parents[j].brain, &child);
            if (!t.birth) continue;
            const auto id = brain_id(child);
            if (!ids.insert(id).second) continue;
            pool.push_back({child, id}); ++children;
            log.lineage << pond.refill_ticks << ',' << round << ',' << id << ',' << parents[i].id << ',' << parents[j].id
                        << ',' << t.seed << ',' << t.candidate << ',' << t.birth << ',' << t.mother << ',' << t.father << ',' << fingerprint(t.state) << '\n';
        }
    }

    struct Screen {
        Score score;
        std::array<unsigned, 8> partners{};
        std::array<Trial, 8> trials{};
        std::array<Family, 2> families{};
        unsigned grandfamilies = 0, adult_families = 0, peak_drive = 0, resource_rank = 0;
        auto outcomes() const { return std::make_tuple(score.births, grandfamilies, adult_families, score.healthy); }
    };
    std::vector<Screen> screens(pool.size());
    parallel(pool.size(), [&](std::size_t i) {
        auto& screen = screens[i];
        std::vector<unsigned> partners;
        for (unsigned j = 0; j < parents.size(); ++j) if (parents[j].id != pool[i].id) partners.push_back(j);
        for (unsigned k = 0; k < 8; ++k) {
            const auto j = partners[(k + round - 1) % partners.size()];
            screen.partners[k] = j;
            auto& t = screen.trials[k];
            t = trial(pond, base + 10 + k, 1 + ((k % 2) ^ (k / 4)), pool[i].brain, parents[j].brain);
            screen.score.add(t);
        }
        for (unsigned k = 0; k < screen.families.size(); ++k) {
            auto& f = screen.families[k];
            f = follow_family(pond, base + 25 + k, 1 + k, pool[i], parents[screen.partners[k]]);
            screen.grandfamilies += f.grandchild != 0;
            screen.adult_families += f.adult != 0;
            screen.peak_drive += std::min(unsigned(mating_drive), f.peak_drive);
        }
    });
    for (std::size_t i = 0; i < pool.size(); ++i) {
        auto& s = screens[i];
        for (unsigned k = 0; k < 8; ++k)
            log_trial(log, pond.refill_ticks, round, "selection", pool[i], parents[s.partners[k]], s.trials[k]);
        for (unsigned k = 0; k < s.families.size(); ++k)
            log_family(log, pond.refill_ticks, "selection", round, base + 25 + k, pool[i], parents[s.partners[k]], 1 + k, s.families[k]);
        // Equal weight to energy and food ranks without converting one unit into the other.
        for (const auto& other : screens) if (s.outcomes() == other.outcomes()) {
            s.resource_rank += other.score.energy < s.score.energy;
            s.resource_rank += other.score.food < s.score.food;
        }
    }
    const auto rank = [&](std::size_t i) {
        const auto& s = screens[i]; const auto& v = s.score;
        return std::make_tuple(-v.births, -int(s.grandfamilies), -int(s.adult_families), -v.healthy,
                               s.grandfamilies ? 0 : -int(s.peak_drive), v.births ? 0 : -v.timing, v.births ? 0 : -v.fed,
                               s.resource_rank, v.energy, v.food);
    };
    std::vector<std::size_t> order(pool.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return rank(a) < rank(b); });
    Group keep;
    for (std::size_t pos = 0; pos < order.size(); ++pos) {
        const auto i = order[pos]; const auto& v = screens[i].score;
        if (pos < 4) keep.push_back(pool[i]);
        log.selection << pond.refill_ticks << ',' << round << ',' << pos + 1 << ',' << pool[i].id << ',' << (i < 4)
                      << ',' << v.births << ',' << v.healthy << ',' << screens[i].grandfamilies << ',' << screens[i].adult_families
                      << ',' << screens[i].peak_drive << ',' << v.energy << ',' << v.food << ',' << screens[i].resource_rank << ',' << (pos < 4) << '\n';
    }
    return keep;
}

struct Check {
    std::array<std::array<Trial, 8>, 6> trials{};
    std::array<Family, 8> families{};
    unsigned first_family_seed = 0;
    Score score;
    int weakest = 8, family_trials = 0, adults = 0;
    std::array<int, 2> grandchildren{};
    bool passed() const { return weakest >= 7 && grandchildren[0] >= 3 && grandchildren[1] >= 3; }
};

Check check_pairs(const Preset& pond, const Group& group, const Seeds& seeds, const char* phase, Logs& log) {
    Check check;
    parallel(crosses.size(), [&](std::size_t p) {
        const auto [a,b] = crosses[p];
        for (unsigned k = 0; k < seeds.size(); ++k)
            check.trials[p][k] = trial(pond, seeds[k], 1 + k % 2, group[a].brain, group[b].brain);
    });
    for (unsigned p = 0; p < crosses.size(); ++p) {
        const auto [a,b] = crosses[p];
        Score score; std::array<int, 2> blocks{};
        for (unsigned k = 0; k < seeds.size(); ++k) {
            const auto& t = check.trials[p][k];
            score.add(t); check.score.add(t); blocks[k / 4] += t.birth != 0;
            log_trial(log, pond.refill_ticks, 0, phase, group[a], group[b], t);
        }
        check.weakest = std::min(check.weakest, score.births);
        log.pairs << pond.refill_ticks << ',' << phase << ',' << group[a].id << ',' << group[b].id << ',' << blocks[0] << ',' << blocks[1]
                  << ',' << score.healthy << ',' << score.energy << ',' << score.food << '\n';
    }
    return check;
}

void check_families(const Preset& pond, const Group& group, unsigned first_seed, const char* phase, Check& check, Logs& log) {
    auto& families = check.families;
    check.first_family_seed = first_seed;
    parallel(families.size(), [&](std::size_t k) {
        const auto [a,b] = crosses[family_crosses[k]];
        families[k] = follow_family(pond, first_seed + unsigned(k), 1 + unsigned(k % 2), group[a], group[b]);
    });
    for (unsigned k = 0; k < families.size(); ++k) {
        const auto [a,b] = crosses[family_crosses[k]];
        const auto& f = families[k];
        check.adults += f.adult != 0; check.grandchildren[k / 4] += f.grandchild != 0; ++check.family_trials;
        log_family(log, pond.refill_ticks, phase, 0, first_seed + k, group[a], group[b], 1 + k % 2, f);
    }
}

void stage_result(Logs& log, int interval, const char* phase, const Group& group, const Check& check, const char* decision) {
    auto& out = log.stages;
    out << interval << ',' << phase << ',';
    for (std::size_t i = 0; i < group.size(); ++i) out << (i ? " " : "") << group[i].id;
    out << ",48," << check.score.births << ',' << check.weakest << ',' << check.score.healthy << ',' << std::fixed << std::setprecision(3);
    if (check.score.births) out << double(check.score.energy) / check.score.births;
    out << ',';
    if (check.score.births) out << double(check.score.food) / check.score.births;
    out << ',' << check.family_trials << ',';
    if (check.family_trials) out << check.adults;
    out << ','; if (check.family_trials) out << check.grandchildren[0];
    out << ','; if (check.family_trials) out << check.grandchildren[1];
    out << ',' << decision << '\n';
}

bool replace_incumbents(const Check& incoming, const Check& candidate) {
    if (!candidate.passed()) return false;
    if (!incoming.passed()) return true;
    const auto reliability = [](const Check& c) {
        return std::make_tuple(c.score.births, c.grandchildren[0] + c.grandchildren[1], c.weakest, c.score.healthy);
    };
    if (reliability(candidate) != reliability(incoming)) return reliability(candidate) > reliability(incoming);
    // At equal birth counts, totals and cost per birth have the same ordering.
    const auto& a = incoming.score; const auto& b = candidate.score;
    return b.energy <= a.energy * 1.01 && b.food <= a.food * 1.01 && (b.energy <= a.energy * .99 || b.food <= a.food * .99);
}

void film_stage(Film& film, const Preset& pond, const Group& group, const Check& check, int stage) {
    // One successful birth and one timeout if present, always taken from the blind panel.
    bool recorded[2]{};
    const auto label = "Every " + std::to_string(pond.refill_ticks) + (pond.refill_ticks == 1 ? " tick" : " ticks");
    for (unsigned p = 0; p < crosses.size(); ++p) for (const auto& t : check.trials[p]) {
        const bool birth = t.birth != 0;
        if (recorded[birth]) continue;
        recorded[birth] = true;
        const auto [a,b] = crosses[p];
        auto w = pond.start(t.seed, t.candidate, group[a].brain, group[b].brain);
        const auto name = std::string(birth ? "Birth" : "Timeout") + " · mate " + group[b].id.substr(0,8);
        film.add_pond(w, group[a].id, t.candidate, name.c_str(), stage, label.c_str(), t.end, t.state);
    }
    bool family_recorded[2]{};
    for (unsigned k = 0; k < unsigned(check.family_trials); ++k) {
        const auto& f = check.families[k];
        const bool bred = f.grandchild != 0;
        if (family_recorded[bred]) continue;
        family_recorded[bred] = true;
        const auto [a,b] = crosses[family_crosses[k]];
        const auto slot = 1 + k % 2;
        auto w = pond.start(check.first_family_seed + k, slot, group[a].brain, group[b].brain);
        film.add_pond(w, group[a].id, slot, bred ? "Family · grandchild" : "Family · no grandchild",
                      stage, label.c_str(), f.end, f.state, true);
    }
}
} // namespace

void breeding_curriculum(const std::filesystem::path& dir, const std::filesystem::path& preset_path,
                         const std::vector<std::pair<std::string, phd::brain::Brain>>& founders, unsigned seed_base, bool visual) {
    if (seed_base > std::numeric_limits<unsigned>::max() - 602000u)
        throw std::runtime_error("The curriculum seed blocks exceed the available seed range");
    auto pond = read_preset(preset_path);
    Group parents;
    std::set<std::string> ids;
    std::ostringstream founders_csv; founders_csv << "donor,brain\n";
    for (const auto& [name, brain] : founders) {
        const auto id = brain_id(brain);
        if (!ids.insert(id).second) throw std::runtime_error("Seed the curriculum with four distinct brains");
        parents.push_back({brain, id}); founders_csv << '"' << name << "\"," << id << '\n';
    }
    if (parents.size() != 4) throw std::runtime_error("Expected four founder brains");
    fs::create_directories(dir); write_text(dir / "founders.csv", founders_csv.str());
    Logs log;
    auto bridge = check_pairs(pond, parents, previous_seeds, "lesson2-peers", log);
    stage_result(log, pond.refill_ticks, "lesson2-peers", parents, bridge, "benchmark");
    const Group original = parents;
    int low = 0, high = 0, coarse_low = 0, coarse_high = 0;
    std::optional<Film> film;
    if (visual) film.emplace(dir, 3);
    for (unsigned step = 0;; ++step) {
        const auto base = seed_base + step * 1000;
        auto selected = parents;
        for (unsigned round = 1; round <= generations; ++round)
            selected = generation(pond, selected, round, base + round * 50, log);
        const auto blind = seeds_from(base + 400);
        auto control = check_pairs(pond, parents, blind, "incoming", log);
        auto trained = check_pairs(pond, selected, blind, "selected", log);
        check_families(pond, selected, base + 500, "selected", trained, log);
        check_families(pond, parents, base + 500, "incoming", control, log);
        const bool replace = replace_incumbents(control, trained);
        const bool carry = !replace && control.passed();
        stage_result(log, pond.refill_ticks, "incoming", parents, control, carry ? "carry" : "control");
        stage_result(log, pond.refill_ticks, "selected", selected, trained, replace ? "advance" : trained.passed() ? "qualified" : "hold");
        const bool passed = replace || carry;
        const auto& kept = carry ? parents : selected;
        const auto& result = carry ? control : trained;
        const auto stage_dir = dir / ("refill-" + std::to_string(pond.refill_ticks));
        fs::create_directories(stage_dir); write_preset(stage_dir / "pond.cfg", pond);
        for (unsigned i = 0; i < 4; ++i)
            write_donor(stage_dir / (kept[i].id + ".phdw"), pond.start(blind[0], 1, kept[i].brain, kept[(i+1)%4].brain));
        if (film) { film_stage(*film, pond, kept, result, int(step + 1)); film->publish(false); }
        log.save(dir);
        std::printf("refill=%d births=%d/48 weakest=%d/8 grandchild_blocks=%d/4,%d/4 %s\n", pond.refill_ticks,
                    result.score.births, result.weakest, result.grandchildren[0], result.grandchildren[1], passed ? "passed" : "held");
        std::fflush(stdout);
        if (passed) {
            low = pond.refill_ticks;
            parents = kept;
        } else {
            high = pond.refill_ticks;
            if (!coarse_high) { coarse_low = low; coarse_high = high; }
        }
        if (!low || (high && high - low <= 64) || unsigned(low) >= family_limit) break;
        pond.refill_ticks = high ? low + (high - low) / 2 : low * 2;
    }
    // None of these seeds were used for hatching, screening, or deciding which groups advanced.
    // Freeze the same four brains across intervals to separate environmental pressure from changes in the cohort.
    if (low) {
        std::set<int> audit_intervals{1, coarse_low, coarse_high, low, high};
        if (coarse_high) {
            const int stride = std::max(64, (coarse_high - coarse_low) / 8);
            for (int interval = coarse_low; interval < coarse_high; interval += stride) audit_intervals.insert(interval);
        }
        bool continuous = true;
        int confirmed = 0;
        for (const auto interval : audit_intervals) if (interval) {
            pond.refill_ticks = interval;
            for (unsigned panel = 0; panel < 2; ++panel) {
                const auto audit_base = seed_base + 600000 + panel * 1000;
                for (const Group* group : std::array<const Group*, 2>{&original, &parents}) {
                    const auto phase = std::string(group == &original ? "founder-audit-" : "checkpoint-audit-") + (panel ? 'b' : 'a');
                    auto audit = check_pairs(pond, *group, seeds_from(audit_base + 400), phase.c_str(), log);
                    check_families(pond, *group, audit_base + 500, phase.c_str(), audit, log);
                    stage_result(log, interval, phase.c_str(), *group, audit, audit.passed() ? "pass" : "miss");
                    if (group == &parents && !audit.passed()) continuous = false;
                }
            }
            if (continuous) confirmed = interval;
        }
        if (confirmed) {
            pond.refill_ticks = confirmed;
            const auto checkpoint = dir / "confirmed";
            fs::create_directories(checkpoint);
            write_preset(checkpoint / "pond.cfg", pond);
            std::ostringstream manifest;
            manifest << "brain,mate,refill_ticks,seed,birth_tick,end_tick,fingerprint\n";
            for (unsigned i = 0; i < parents.size(); ++i) {
                const auto& mate = parents[(i + 1) % parents.size()];
                const auto seed = seed_base + 600400;
                write_donor(checkpoint / (parents[i].id + ".phdw"), pond.start(seed, 1, parents[i].brain, mate.brain));
                const auto t = trial(pond, seed, 1, parents[i].brain, mate.brain);
                manifest << parents[i].id << ',' << mate.id << ',' << confirmed << ',' << seed << ',' << t.birth << ',' << t.end
                         << ',' << fingerprint(t.state) << '\n';
            }
            write_text(checkpoint / "brains.csv", manifest.str());
        }
        std::printf("frozen checkpoint confirmed through %d ticks on both audit panels\n", confirmed);
        log.save(dir);
    }
    if (film) film->publish(true);
}
} // namespace nursery
