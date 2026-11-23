// The nursery breeds the brains founders start with (docs/NURSERY.md).
//
// Each kind of founder is trained on its own, in arenas where everything is
// set by a dial, up a ladder of levels (nursery_levels.hpp). A brain is tried
// by giving it to every founder in an arena and scoring what they and their
// descendants manage (nursery_score.hpp). The best are kept and bred, and a
// family moves up a level when enough of its brains pass that level's gate on
// seeds they were not bred on.
//
// Several families are bred for each kind and never mixed: in Petri, two
// families that each founded lasting worlds failed when pooled.

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <set>
#include <charconv>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "file.hpp"
#include "fnv.hpp"
#include "nursery_report.hpp"
#include "nursery_score.hpp"
#include "hunting_score.hpp"
#include "taxonomy.hpp"
#include "tuning.hpp"

namespace {
using namespace nursery;
namespace fs = std::filesystem;
using brain::Brain;

struct Options {
    fs::path dir;
    int jobs = int(std::max(1u, std::thread::hardware_concurrency()));
    int pool = 64, keep = 16; // brains tried each round, and kept from it
    int checked = 8, needed = 4; // of the best, how many meet the gate, and how many must pass
    int per_round = 3;        // seeds every brain meets each round; one more from Thin up
    // A family is bred at a level until its kept brains stop getting better,
    // however early it could pass the gate: at least `least` rounds, then
    // until `patience` rounds bring no gain. At `rounds` it stops, and is
    // called stalled if it still cannot pass.
    int least = 10, patience = 8, rounds = 120;
    int families = 4;         // half start from the hand-wired brain, half from random ones
    int screened = 512;       // random brains a random family is picked from
    int last_level = level_count;
    std::uint32_t seed = 1;
    std::vector<FounderKind> kinds{FounderKind::grazer, FounderKind::crawler, FounderKind::flier, FounderKind::fish, FounderKind::swimmer};
};

constexpr int gate_seeds = 4;

std::uint32_t training_seed(const Options& o, int level, int round, int k) {
    return o.seed * 1000000u + 100000u + std::uint32_t(level) * 10000u + std::uint32_t(round) * 16u + std::uint32_t(k);
}
std::uint32_t gate_seed(const Options& o, int level, int k) {
    return o.seed * 1000000u + 900000u + std::uint32_t(level) * 100u + std::uint32_t(k);
}

std::uint32_t exam_seed(const Options& o, int level, int k) {
    return o.seed * 1000000u + 800000u + std::uint32_t(level) * 100u + std::uint32_t(k);
}

// Seeds passed at each level of the exam.
using Exam = std::array<std::uint8_t, level_count>;

// Runs work(0..count-1) on up to `jobs` threads. Each call writes only its
// own slot, so the outcome is the same for any number of threads.
template <class Work>
void parallel(int count, int jobs, Work work) {
    std::atomic<int> next{0};
    std::vector<std::thread> threads;
    const auto run = [&] {
        for (int i = next.fetch_add(1); i < count; i = next.fetch_add(1)) work(i);
    };
    for (int j = 1; j < std::min(jobs, count); ++j) threads.emplace_back(run);
    run();
    for (auto& t : threads) t.join();
}

std::string brain_id(const Brain& b) {
    Fnv1a hash;
    for (const auto w : b.weight) hash.add(w);
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash.value()));
    return text;
}

Brain hand_wired(FounderKind kind) { return brain::founder(kind == FounderKind::swimmer); }

Brain random_brain(Random& rng) {
    Brain b;
    for (auto& w : b.weight) w = std::int8_t(int(rng.below(65)) - 32);
    return b;
}

// The nursery's own variation, coarser than the one-in-128 small steps a
// birth makes in a world: a search needs children that behave differently
// from their parents. Most children change a little and some a lot.
void vary(Brain& b, Random& rng) {
    const auto roll = rng.below(4);
    const int changes = roll == 0 ? 2 : roll < 3 ? 6 : 20;
    const int step = roll == 0 ? 8 : roll < 3 ? 16 : 40;
    for (int n = 0; n < changes; ++n) {
        auto& w = b.weight[rng.below(brain::bytes)];
        w = std::int8_t(std::clamp(int(w) + int(rng.below(std::uint32_t(2 * step + 1))) - step, -127, 127));
    }
}

struct Means {
    std::int64_t fed = 0, ready = 0, bred = 0, recruited = 0, sustained = 0, score = 0;
    int trials = 0;
    void add(const Result& r) {
        fed += r.fed; ready += r.ready; bred += r.bred; recruited += r.recruited; sustained += r.sustained;
        score += r.score(); ++trials;
    }
    int of(std::int64_t sum) const { return trials ? int(sum / trials) : 0; }
};

struct Candidate {
    Brain brain;
    Means means;     // every trial it has had at this level,
    Means review;    // and at the levels below it while here
    int gate = -1;   // gate seeds passed, once checked
    int mean() const { return means.of(means.score); }
    // What it is kept or dropped on: half this level, half the ones below,
    // so that what they taught is not bred away.
    int rank() const { return review.trials ? (mean() + review.of(review.score)) / 2 : mean(); }
};

struct Round {
    int level, round, best, median, fed, ready, bred, recruited, sustained, passing;
    Exam exam{}; // its best brain at every level
};

enum class Status { waiting, training, stalled, finished };

struct Family {
    int id = 0;
    bool random_start = false;
    int level = 0, rounds = 0; // the level it is on, and rounds spent there
    int high = 0, flat = 0;    // the best its kept brains have scored there, and rounds since that rose
    Brain examined{};          // the brain its last exam was of,
    Exam exam{};               // and how that went
    bool has_exam = false;
    std::map<std::string, int> failed; // gate trials lost at this level, by what was missing
    Status status = Status::waiting;
    std::vector<Candidate> pool;
    std::vector<std::vector<Brain>> passed = std::vector<std::vector<Brain>>(level_count);
    std::vector<Round> log;
    Random rng;
};

struct Baseline { Means means; int passed = 0; };

struct Kind {
    FounderKind kind;
    std::vector<Family> families;
    std::vector<Baseline> baseline; // the hand-wired brain at each level's gate
    Exam hand_exam{};               // and in the exam
    int champion = -1, champion_level = 0; // the family whose brains are in the founders file
};

struct Nursery {
    Options o;
    std::vector<Kind> kinds;
    std::atomic<long long> trials{0};
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_report{};
    // The other kinds' brains in a Company arena: every kind's best so far,
    // as they stood when the level began.
    FounderBrains company = FounderBrains::hand_wired();

    double elapsed() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(); }

    Result trial(const Level& level, FounderKind kind, std::uint32_t seed, const Brain& mind) {
        ++trials;
        return run_trial(level, kind, seed, {mind}, &company);
    }

    // How many gate seeds each of these brains passes.
    std::vector<int> at_gate(const Level& level, int level_index, FounderKind kind, const std::vector<const Brain*>& brains, Means* means = nullptr,
                             std::map<std::string, int>* failed = nullptr) {
        std::vector<Result> results(brains.size() * gate_seeds);
        parallel(int(results.size()), o.jobs, [&](int i) {
            results[std::size_t(i)] = trial(level, kind, gate_seed(o, level_index, i % gate_seeds), *brains[std::size_t(i / gate_seeds)]);
        });
        std::vector<int> passed(brains.size(), 0);
        for (std::size_t i = 0; i < results.size(); ++i) {
            passed[i / gate_seeds] += passes(level.gate, results[i]);
            if (failed) if (const char* why = fails(level.gate, results[i])) ++(*failed)[why];
            if (means) means->add(results[i]);
        }
        return passed;
    }

    // The exam: one brain at every level, on seeds nothing was bred on or
    // gated by, and with hand-wired company. It is the same test for every
    // brain of a kind from the first round to the last, so it shows what
    // breeding has changed, and anything an earlier level taught that a
    // later one has lost. Nothing is selected on it.
    Exam exam(FounderKind kind, const Brain& mind) {
        std::vector<Result> results(std::size_t(o.last_level) * gate_seeds);
        parallel(int(results.size()), o.jobs, [&](int i) {
            ++trials;
            results[std::size_t(i)] = run_trial(levels[i / gate_seeds], kind, exam_seed(o, i / gate_seeds, i % gate_seeds), {mind});
        });
        Exam passed{};
        for (std::size_t i = 0; i < results.size(); ++i) passed[i / gate_seeds] += passes(levels[i / gate_seeds].gate, results[i]);
        return passed;
    }

    void measure_baselines() {
        for (auto& k : kinds) {
            const Brain hand = hand_wired(k.kind);
            k.hand_exam = exam(k.kind, hand);
            k.baseline.resize(std::size_t(o.last_level));
            for (int l = 0; l < o.last_level; ++l) {
                auto& b = k.baseline[std::size_t(l)];
                b.passed = at_gate(levels[l], l, k.kind, {&hand}, &b.means)[0];
            }
            std::printf("%-8s hand-wired at the gates:", founder_name(k.kind));
            for (int l = 0; l < o.last_level; ++l) std::printf("  %s %d/%d", levels[l].name, k.baseline[std::size_t(l)].passed, gate_seeds);
            std::printf("\n");
            std::fflush(stdout);
            report(false);
        }
    }

    void found(Kind& k, Family& f) {
        f.rng = Random(stream_seed(o.seed, 0x6e7572u + std::uint32_t(k.kind) * 64u + std::uint32_t(f.id)));
        f.status = Status::training;
        if (!f.random_start) {
            f.pool.assign(std::size_t(o.pool), Candidate{hand_wired(k.kind)});
            for (std::size_t i = 1; i < f.pool.size(); ++i) vary(f.pool[i].brain, f.rng);
            return;
        }
        // Random brains mostly die at once. Screen many on one seed of the
        // first level and start from the best of them.
        std::vector<Candidate> screen(std::size_t(o.screened));
        for (auto& c : screen) c.brain = random_brain(f.rng);
        std::vector<Result> results(screen.size());
        parallel(int(screen.size()), o.jobs, [&](int i) {
            results[std::size_t(i)] = trial(levels[0], k.kind, training_seed(o, 0, 0, o.per_round + f.id), screen[std::size_t(i)].brain);
        });
        std::vector<std::size_t> order(screen.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return results[a].score() > results[b].score(); });
        for (int i = 0; i < o.pool; ++i) f.pool.push_back(Candidate{screen[order[std::size_t(i)]].brain});
    }

    // One round of one family at its level. Returns false when it has
    // nothing more to do.
    bool round(Kind& k, Family& f) {
        if (f.status == Status::waiting) found(k, f);
        if (f.status != Status::training) return false;
        const Level& level = levels[f.level];
        // Each brain meets `per` arenas of this level and one of every level below.
        const int per = o.per_round + (f.level >= 5), each = per + f.level;
        std::vector<Result> results(f.pool.size() * std::size_t(each));
        parallel(int(results.size()), o.jobs, [&](int i) {
            const int n = i % each;
            const Brain& mind = f.pool[std::size_t(i / each)].brain;
            results[std::size_t(i)] = n < per ? trial(level, k.kind, training_seed(o, f.level, f.rounds, n), mind)
                                              : trial(levels[n - per], k.kind, training_seed(o, n - per, f.rounds, 15), mind);
        });
        for (std::size_t i = 0; i < results.size(); ++i) {
            auto& c = f.pool[i / std::size_t(each)];
            (int(i % std::size_t(each)) < per ? c.means : c.review).add(results[i]);
        }
        // Kept brains carry their earlier trials, so one lucky arena does not keep a brain for long.
        std::stable_sort(f.pool.begin(), f.pool.end(), [](const Candidate& a, const Candidate& b) { return a.rank() > b.rank(); });

        // The gate, on seeds no brain was bred on.
        std::vector<const Brain*> unchecked;
        std::vector<Candidate*> owners;
        const int checked = std::min(o.checked, int(f.pool.size()));
        for (int i = 0; i < checked; ++i) {
            if (f.pool[std::size_t(i)].gate < 0) { unchecked.push_back(&f.pool[std::size_t(i)].brain); owners.push_back(&f.pool[std::size_t(i)]); }
        }
        const auto gates = at_gate(level, f.level, k.kind, unchecked, nullptr, &f.failed);
        for (std::size_t i = 0; i < owners.size(); ++i) owners[i]->gate = gates[i];
        std::vector<Brain> passing;
        for (int i = 0; i < checked; ++i) {
            if (f.pool[std::size_t(i)].gate >= level.gate.seeds) passing.push_back(f.pool[std::size_t(i)].brain);
        }

        const auto& best = f.pool.front();
        if (!f.has_exam || !std::equal(std::begin(best.brain.weight), std::end(best.brain.weight), std::begin(f.examined.weight))) {
            f.exam = exam(k.kind, best.brain);
            f.examined = best.brain;
            f.has_exam = true;
        }
        f.log.push_back({f.level, f.rounds, best.mean(), f.pool[f.pool.size() / 2].mean(), best.means.of(best.means.fed),
                         best.means.of(best.means.ready), best.means.of(best.means.bred), best.means.of(best.means.recruited),
                         best.means.of(best.means.sustained), int(passing.size()), f.exam});
        ++f.rounds;

        // Has breeding here stopped paying? The kept brains' mean score, with a hundredth of full marks as a gain.
        std::int64_t kept = 0;
        const int keeping = std::min<int>(o.keep, int(f.pool.size()));
        for (int i = 0; i < keeping; ++i) kept += f.pool[std::size_t(i)].mean();
        kept /= keeping;
        if (kept > f.high + Result::full_score / 100) { f.high = int(kept); f.flat = 0; } else ++f.flat;
        const bool settled = (f.rounds >= o.least && f.flat >= o.patience) || f.rounds >= o.rounds;

        if (int(passing.size()) >= o.needed && settled) {
            std::printf("%-8s family %d passed %-7s after %3d rounds, best %2d%% (%d of the best %d at the gate)\n", founder_name(k.kind), f.id,
                        level.name, f.rounds, best.mean() * 100 / Result::full_score, int(passing.size()), checked);
            f.passed[std::size_t(f.level)] = passing;
            ++f.level;
            f.rounds = f.high = f.flat = 0;
            for (auto& c : f.pool) { c.means = {}; c.review = {}; c.gate = -1; }
            f.failed.clear();
            if (f.level >= o.last_level) f.status = Status::finished;
            crown(k);
        } else if (f.rounds >= o.rounds) {
            f.status = Status::stalled;
            std::string why;
            for (const auto& [what, count] : f.failed) why += (why.empty() ? "" : ", ") + what + " " + std::to_string(count);
            std::printf("%-8s family %d stalled at %-6s after %d rounds; gate trials lost to: %s\n", founder_name(k.kind), f.id, level.name,
                        f.rounds, why.c_str());
        }
        if (f.status == Status::training && (f.rounds > 0 || f.level > 0)) breed(f);
        std::fflush(stdout);
        return f.status == Status::training;
    }

    // Keeps the best and fills the rest of the pool with their young.
    void breed(Family& f) {
        f.pool.resize(std::size_t(std::min<int>(o.keep, int(f.pool.size()))));
        const auto kept = std::uint32_t(f.pool.size());
        while (int(f.pool.size()) < o.pool) {
            const auto a = f.rng.below(kept);
            Candidate child{f.pool[a].brain};
            if (kept > 1 && f.rng.below(2)) {
                auto b = f.rng.below(kept - 1);
                if (b >= a) ++b;
                auto state = f.rng.next();
                brain::combine(child.brain, f.pool[a].brain, f.pool[b].brain, state);
            }
            vary(child.brain, f.rng);
            f.pool.push_back(child);
        }
    }

    // The first thing, in the order of a life, that its best brain does not yet manage.
    static const char* held_up(const Family& f) {
        if (f.log.empty()) return "nothing yet";
        const Round& r = f.log.back();
        const Gate& gate = levels[r.level].gate;
        if (r.fed < 500 || r.fed < gate.fed_percent * 10) return "staying fed";
        if (r.ready < 1000) return "building the drive to mate";
        if (r.bred < 1 || (gate.births && r.bred * levels[r.level].founders < 1000 * gate.births)) return "breeding";
        if (gate.recruits && r.recruited * levels[r.level].founders < 1000 * gate.recruits) return "young growing up";
        if (gate.generation >= 2) return "a second generation";
        return "lasting to the end";
    }

    // The kind's champion: the family furthest up the ladder, then the one with more brains through its last gate.
    void crown(Kind& k) {
        k.champion = -1;
        k.champion_level = 0;
        std::size_t most = 0;
        for (const auto& f : k.families) {
            for (int l = level_count; l > 0; --l) {
                const auto size = f.passed[std::size_t(l - 1)].size();
                if (!size) continue;
                if (l > k.champion_level || (l == k.champion_level && size > most)) { k.champion = f.id; k.champion_level = l; most = size; }
                break;
            }
        }
        write_founders();
        if (k.champion >= 0) {
            const int l = k.champion_level - 1;
            const auto& pool = k.families[std::size_t(k.champion)].passed[std::size_t(l)];
            const World arena = start(levels[l], k.kind, gate_seed(o, l, 0), pool, &company);
            fs::create_directories(o.dir / "champions");
            const auto path = o.dir / "champions" / (std::string(founder_name(k.kind)) + "-" + std::to_string(l + 1) + "-" + levels[l].name + ".phdw");
            write_file_atomic(path.string(), arena.save());
        }
    }

    FounderBrains founders() const {
        FounderBrains brains = FounderBrains::hand_wired();
        for (const auto& k : kinds) {
            if (k.champion >= 0) brains.pool[std::size_t(k.kind)] = k.families[std::size_t(k.champion)].passed[std::size_t(k.champion_level - 1)];
        }
        return brains;
    }

    void write_founders() const {
        if (!write_file_atomic((o.dir / "founders.phdb").string(), founders().encode())) throw std::runtime_error("Could not write founders.phdb");
    }

    // The page's data. Scores are out of 1000 a term.
    std::string json(bool done) const {
        std::ostringstream s;
        s << "{\"running\":" << (done ? "false" : "true") << ",\"elapsed\":" << int(elapsed()) << ",\"trials\":" << trials.load()
          << ",\"jobs\":" << o.jobs << ",\"pool\":" << o.pool << ",\"keep\":" << o.keep << ",\"checked\":" << o.checked
          << ",\"needed\":" << o.needed << ",\"rounds\":" << o.rounds << ",\"gate_seeds\":" << gate_seeds
          << ",\"full\":" << Result::full_score << ",\"levels\":[";
        for (int l = 0; l < o.last_level; ++l) {
            const Level& v = levels[l];
            s << (l ? "," : "") << "{\"name\":\"" << v.name << "\",\"task\":\"" << v.task << "\",\"width\":" << v.width << ",\"height\":" << v.height
              << ",\"founders\":" << v.founders << ",\"start\":" << v.start_energy << ",\"plenty\":" << v.plenty << ",\"bites\":" << v.cell_bites << ",\"real\":" << v.real
              << ",\"patches\":" << v.patches << ",\"patch\":" << v.patch << ",\"hazard\":" << v.hazard << ",\"days\":" << (v.days ? "true" : "false") << ",\"company\":" << (v.company ? "true" : "false")
              << ",\"gate\":{\"seeds\":" << v.gate.seeds << ",\"fed\":" << v.gate.fed_percent << ",\"none_starve\":" << (v.gate.none_starve ? "true" : "false")
              << ",\"births\":" << v.gate.births << ",\"recruits\":" << v.gate.recruits << ",\"generation\":" << v.gate.generation << "}}";
        }
        s << "],\"kinds\":[";
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const Kind& k = kinds[i];
            s << (i ? "," : "") << "{\"name\":\"" << founder_name(k.kind) << "\",\"water\":" << (lives_in_water(k.kind) ? "true" : "false")
              << ",\"champion\":" << k.champion << ",\"champion_level\":" << k.champion_level << ",\"hand_exam\":[";
            for (int l = 0; l < o.last_level; ++l) s << (l ? "," : "") << int(k.hand_exam[std::size_t(l)]);
            s << "],\"levels\":[";
            for (int l = 0; l < o.last_level; ++l) {
                const Arena a = arena(levels[l], k.kind, gate_seed(o, l, 0));
                s << (l ? "," : "") << "{\"ticks\":" << trial_ticks(levels[l], k.kind) << ",\"need\":" << a.need << ",\"supply\":" << a.supply
                  << ",\"planted\":" << a.planted << ",\"food\":" << a.config.arena_food << ",\"regrow\":" << a.config.arena_regrow;
                if (std::size_t(l) < k.baseline.size()) {
                    const auto& b = k.baseline[std::size_t(l)];
                    s << ",\"hand\":{\"passed\":" << b.passed << ",\"score\":" << b.means.of(b.means.score) << ",\"fed\":" << b.means.of(b.means.fed)
                      << ",\"ready\":" << b.means.of(b.means.ready) << ",\"bred\":" << b.means.of(b.means.bred)
                      << ",\"recruited\":" << b.means.of(b.means.recruited) << ",\"sustained\":" << b.means.of(b.means.sustained) << "}";
                }
                s << "}";
            }
            s << "],\"families\":[";
            for (std::size_t j = 0; j < k.families.size(); ++j) {
                const Family& f = k.families[j];
                const char* status = f.status == Status::waiting ? "waiting" : f.status == Status::training ? "training" : f.status == Status::stalled ? "stalled" : "finished";
                s << (j ? "," : "") << "{\"id\":" << f.id << ",\"start\":\"" << (f.random_start ? "random" : "hand-wired") << "\",\"level\":" << f.level
                  << ",\"status\":\"" << status << "\",\"held\":\"" << (f.status == Status::stalled ? held_up(f) : "") << "\",\"passed\":[";
                for (int l = 0; l < o.last_level; ++l) s << (l ? "," : "") << f.passed[std::size_t(l)].size();
                s << "],\"log\":[";
                for (std::size_t r = 0; r < f.log.size(); ++r) {
                    const Round& g = f.log[r];
                    s << (r ? "," : "") << "[" << g.level << "," << g.round << "," << g.best << "," << g.median << "," << g.fed << "," << g.ready << ","
                      << g.bred << "," << g.recruited << "," << g.sustained << "," << g.passing;
                    for (int l = 0; l < o.last_level; ++l) s << "," << int(g.exam[std::size_t(l)]);
                    s << "]";
                }
                s << "]}";
            }
            s << "]}";
        }
        s << "]}";
        return s.str();
    }

    void report(bool done) {
        const auto now = std::chrono::steady_clock::now();
        if (!done && now - last_report < std::chrono::seconds(2)) return;
        last_report = now;
        const std::string page = report_page(json(done), !done);
        write_file_atomic((o.dir / "nursery.html").string(), {page.begin(), page.end()});
    }

    void write_rounds() const {
        std::ostringstream s;
        s << "kind,family,start,level,round,best,median,fed,ready,bred,recruited,sustained,passing_gate,exam_seeds_passed\n";
        for (const auto& k : kinds) for (const auto& f : k.families) for (const auto& r : f.log) {
            s << founder_name(k.kind) << ',' << f.id << ',' << (f.random_start ? "random" : "hand-wired") << ',' << levels[r.level].name << ','
              << r.round + 1 << ',' << r.best << ',' << r.median << ',' << r.fed << ',' << r.ready << ',' << r.bred << ',' << r.recruited << ','
              << r.sustained << ',' << r.passing << ',';
            int exam_total = 0;
            for (const auto seeds : r.exam) exam_total += seeds;
            s << exam_total << '\n';
        }
        const auto text = s.str();
        write_file_atomic((o.dir / "rounds.csv").string(), {text.begin(), text.end()});
    }

    void train() {
        fs::create_directories(o.dir);
        for (const auto kind : o.kinds) {
            Kind k{kind};
            for (int f = 0; f < o.families; ++f) {
                Family family;
                family.id = f;
                family.random_start = f >= (o.families + 1) / 2;
                k.families.push_back(std::move(family));
            }
            kinds.push_back(std::move(k));
        }
        write_founders();
        report(false);
        measure_baselines();
        // A level at a time across every kind, so the page fills evenly.
        for (int level = 0; level < o.last_level; ++level) {
            company = founders();
            for (auto& k : kinds) {
                for (auto& f : k.families) {
                    while (f.level == level && round(k, f)) report(false);
                    write_rounds();
                    report(false);
                }
            }
        }
        write_founders();
        write_rounds();
        report(true);
        std::printf("\n%lld trials in %.0f s on %d threads\n", trials.load(), elapsed(), o.jobs);
        for (const auto& k : kinds) {
            if (k.champion < 0) std::printf("%-8s no family passed a level: the hand-wired brain stays\n", founder_name(k.kind));
            else std::printf("%-8s family %d, through %s: %zu brains\n", founder_name(k.kind), k.champion, levels[k.champion_level - 1].name,
                             k.families[std::size_t(k.champion)].passed[std::size_t(k.champion_level - 1)].size());
        }
        std::printf("wrote %s\n", (o.dir / "founders.phdb").string().c_str());
    }
};

unsigned number(std::string_view s) {
    unsigned n = 0;
    const auto parse = std::from_chars(s.data(), s.data() + s.size(), n);
    if (parse.ec != std::errc{} || parse.ptr != s.data() + s.size()) throw std::runtime_error("Not a number: " + std::string(s));
    return n;
}

FounderKind kind_named(std::string_view name) {
    for (int k = 0; k < founder_kinds; ++k) if (name == founder_name(FounderKind(k))) return FounderKind(k);
    throw std::runtime_error("No such founder: " + std::string(name) + " (grazer, crawler, flier, fish, swimmer)");
}

// What each level supplies against what its founders burn, and how long a trial takes here.
void budget() {
    std::printf("%-8s %-7s %6s %6s %8s %9s %9s %7s %8s\n", "kind", "level", "ticks", "cells", "cell max", "need/pass", "grow/pass", "ratio", "ms/trial");
    for (int k = 0; k < founder_kinds; ++k) {
        const auto kind = FounderKind(k);
        const Brain hand = hand_wired(kind);
        for (int l = 0; l < level_count; ++l) {
            const Arena a = arena(levels[l], kind, 900000u + std::uint32_t(l) * 100u);
            const auto from = std::chrono::steady_clock::now();
            const Result r = run_trial(levels[l], kind, 900000u + std::uint32_t(l) * 100u, {hand});
            if (!guests(levels[l], kind).empty()) {
                std::string with;
                for (const auto& g : guests(levels[l], kind)) with += " " + std::to_string(g.count) + " " + founder_name(g.kind);
                std::printf("%-8s %-7s with%s\n", "", "", with.c_str());
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
            std::printf("%-8s %-7s %6d %6d %8d %9d %9d %6.1fx %8.1f   hand-wired: fed %d ready %d births %d grown %d living %d\n", founder_name(kind),
                        levels[l].name, trial_ticks(levels[l], kind), a.planted, int(a.config.arena_food), a.need, a.supply,
                        double(a.supply) / std::max(1, a.need), ms, r.fed, r.ready, r.births, r.recruits, r.living);
        }
    }
}

// Passes a step's events to several watchers.
struct Crowd : StepObserver {
    std::vector<Watcher*> watchers;
    int kills[founder_kinds][founder_kinds] = {}; // by the killer's kind, then its victim's
    void born(const World& w, const Animal& y, const Animal& m, const Animal& f) override { for (auto* x : watchers) x->born(w, y, m, f); }
    void died(const World& w, const Animal& a, DeathCause c, std::uint32_t k) override {
        if (c == DeathCause::killed) {
            int by = -1, of = -1;
            for (std::size_t i = 0; i < watchers.size(); ++i) {
                if (watchers[i]->member(k)) by = int(i);
                if (watchers[i]->member(a.id)) of = int(i);
            }
            if (by >= 0 && of >= 0) ++kills[by][of];
        }
        for (auto* x : watchers) x->died(w, a, c, k);
    }
    void ate(const World& w, const Animal& a, int g, bool meat) override { for (auto* x : watchers) x->ate(w, a, g, meat); }
    void metabolised(const World& w, const Animal& a, const Frame& f) override { for (auto* x : watchers) x->metabolised(w, a, f); }
};

FounderKind kind_of(const AnimalGenome& g) {
    for (int k = 0; k < founder_kinds; ++k) {
        const auto& f = founder_genome(FounderKind(k));
        if (g.gene[agene_legs] == f.gene[agene_legs] && g.gene[agene_fins] == f.gene[agene_fins] &&
            g.gene[agene_wings] == f.gene[agene_wings] && g.gene[agene_gills] == f.gene[agene_gills]) return FounderKind(k);
    }
    throw std::logic_error("A founder of no known kind");
}

// The exam: the development map, which no brain was bred on. Each kind's
// founders and their descendants are followed as in an arena.
struct WildSeed {
    Result kind[founder_kinds];
    int founders[founder_kinds] = {};
    int last_year[founder_kinds] = {};      // the last year's end with any of the kind alive
    std::vector<int> living[founder_kinds]; // at each year's end
    std::vector<std::int64_t> lake_food;    // edible energy in the lakes at each year's end
    std::vector<std::int64_t> land_food;    // and on dry ground
    std::vector<int> land_fed;              // dry cells with a mouthful for a crawler
    int lake_cells = 0, land_cells = 0;
    int kills[founder_kinds][founder_kinds] = {};
    std::int64_t investment[founder_kinds] = {}; // the gene, summed over the kind's living at the end
};

int late_years = 0; // a meat-eating kind arrives this many years in, once there is something for it to eat

WildSeed wild_run(std::uint32_t seed, const FounderBrains& brains, int years, int animals) {
    WorldConfig config;
    config.seed = seed;
    config.layout = Layout::development;
    config.width = 128;
    config.height = 96;
    World world(config);
    unsigned late = 0;
    for (int k = 0; k < founder_kinds; ++k) if (late_years > 0 && eats_only_meat(FounderKind(k))) late |= 1u << k;
    world.seed_animals(animals, brains, late);
    WildSeed out;
    std::vector<std::uint32_t> ids[founder_kinds];
    for (const auto& a : world.animals()) ids[std::size_t(kind_of(a.genome))].push_back(a.id);
    std::vector<Watcher> watchers;
    watchers.reserve(founder_kinds);
    const int ticks = (years + (late ? late_years : 0)) * World::ticks_per_year;
    Crowd crowd;
    for (int k = 0; k < founder_kinds; ++k) {
        out.founders[k] = int(ids[k].size());
        watchers.emplace_back(ids[k], ticks / animal_stagger);
        watchers.back().hunts(FounderKind(k) == FounderKind::swimmer);
        crowd.watchers.push_back(&watchers.back());
    }
    // The late ones come once the rest have had their years, and the count of years starts then.
    for (int t = 0; t < late_years * World::ticks_per_year && late; ++t) world.step(&crowd);
    for (int k = 0; k < founder_kinds; ++k) {
        if (!(late >> k & 1)) continue;
        const auto before = world.animals().size();
        world.arrive(FounderKind(k), animals / 7, brains);
        std::vector<std::uint32_t> came;
        for (auto i = before; i < world.animals().size(); ++i) came.push_back(world.animals()[i].id);
        out.founders[k] = int(came.size());
        watchers[std::size_t(k)] = Watcher(came, years * World::ticks_per_year / animal_stagger);
        watchers[std::size_t(k)].hunts(FounderKind(k) == FounderKind::swimmer);
    }
    for (int year = 1; year <= years; ++year) {
        for (int t = 0; t < World::ticks_per_year; ++t) world.step(&crowd);
        int alive[founder_kinds] = {};
        for (const auto& a : world.animals()) for (int k = 0; k < founder_kinds; ++k) alive[k] += watchers[std::size_t(k)].member(a.id);
        for (int k = 0; k < founder_kinds; ++k) {
            out.living[k].push_back(alive[k]);
            if (alive[k]) out.last_year[k] = year;
        }
        std::int64_t food = 0, dry_food = 0;
        int lake = 0, dry = 0, dry_fed = 0;
        for (std::size_t c = 0; c < world.cells(); ++c) {
            const bool wet = world.water(c) == Water::lake;
            if (!wet && world.water(c) != Water::none) continue;
            int here = 0;
            for (int slot = 0; slot < 4; ++slot) {
                const Plant* p = world.occupant(c, slot);
                if (p && p->height <= small_limit_dm && p->energy > 8) here += p->energy - 8;
            }
            if (wet) { ++lake; food += here; } else { ++dry; dry_food += here; dry_fed += here >= 28; }
        }
        out.lake_food.push_back(food);
        out.land_food.push_back(dry_food);
        out.land_fed.push_back(dry_fed);
        out.lake_cells = lake;
        out.land_cells = dry;
    }
    for (const auto& a : world.animals()) {
        for (int k = 0; k < founder_kinds; ++k) if (watchers[std::size_t(k)].member(a.id)) out.investment[k] += a.genome.gene[agene_investment];
    }
    for (int k = 0; k < founder_kinds; ++k) out.kind[k] = watchers[std::size_t(k)].finish(world);
    for (int a = 0; a < founder_kinds; ++a) for (int b = 0; b < founder_kinds; ++b) out.kills[a][b] = crowd.kills[a][b];
    return out;
}

std::vector<std::pair<FounderKind, FounderKind>> borrowed_brains; // KIND.brain=OTHER: it thinks with the other's

void wild(const std::string& file, int years, int seeds, int jobs, std::uint32_t first, const fs::path& csv) {
    FounderBrains brains = FounderBrains::hand_wired();
    if (file != "-") {
        std::vector<std::uint8_t> data;
        if (!read_file(file, data) || !FounderBrains::decode(data.data(), data.size(), brains)) throw std::runtime_error("Could not read founder brains from " + file);
    }
    for (const auto& [kind, other] : borrowed_brains) brains.pool[std::size_t(kind)] = brains.pool[std::size_t(other)];
    std::vector<WildSeed> runs(std::size_t(seeds), WildSeed{});
    parallel(seeds, jobs, [&](int i) { runs[std::size_t(i)] = wild_run(first + std::uint32_t(i), brains, years, 56); });
    std::ostringstream out;
    out << "seed,kind,founders,last_year,living,births,grown,generations,starved,killed,suffocated,fed_percent,plant_energy,meat_energy\n";
    std::printf("%s, %d years, seeds %u to %u\n", file == "-" ? "hand-wired founders" : file.c_str(), years, first, first + std::uint32_t(seeds) - 1);
    std::printf("%-8s %9s %7s %7s %7s %6s %8s %7s %5s %6s\n", "kind", "lasted", "living", "births", "grown", "gens", "starved", "killed", "fed%", "meat%");
    for (int k = 0; k < founder_kinds; ++k) {
        int lasted = 0;
        std::vector<int> living, births, grown, gens, starved, killed, fed, meat, last;
        for (int i = 0; i < seeds; ++i) {
            const auto& run = runs[std::size_t(i)];
            const Result& r = run.kind[k];
            lasted += r.living > 0;
            living.push_back(r.living); births.push_back(r.births); grown.push_back(r.recruits); gens.push_back(r.generation);
            starved.push_back(r.starved); killed.push_back(r.killed); last.push_back(run.last_year[k]);
            fed.push_back(int(r.fed_passes * 100 / std::max<std::int64_t>(1, r.passes)));
            meat.push_back(int(r.meat_eaten * 100 / std::max<std::int64_t>(1, r.meat_eaten + r.plants_eaten)));
            out << first + std::uint32_t(i) << ',' << founder_name(FounderKind(k)) << ',' << run.founders[k] << ',' << run.last_year[k] << ',' << r.living << ','
                << r.births << ',' << r.recruits << ',' << r.generation << ',' << r.starved << ',' << r.killed << ',' << r.suffocated << ',' << fed.back() << ','
                << r.plants_eaten << ',' << r.meat_eaten << '\n';
        }
        const auto median = [](std::vector<int> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        std::printf("%-8s %4d of %d %7d %7d %7d %6d %8d %7d %5d %6d   last year alive, by seed:", founder_name(FounderKind(k)), lasted, seeds, median(living),
                    median(births), median(grown), median(gens), median(starved), median(killed), median(fed), median(meat));
        for (const int y : last) std::printf(" %d", y);
        std::printf("\n");
    }
    std::printf("(medians over seeds; fed%% is the kind's metabolic passes above half energy)\n");
    std::printf("young at a birth, by the gene: founders, then the living at the end over all seeds\n");
    for (int k = 0; k < founder_kinds; ++k) {
        std::int64_t sum = 0, living = 0;
        for (const auto& run : runs) { sum += run.investment[k]; living += run.kind[k].living; }
        AnimalGenome mean = founder_genome(FounderKind(k));
        std::printf("%-8s %2d", founder_name(FounderKind(k)), brood_of(mean));
        if (living) {
            mean.gene[agene_investment] = std::uint8_t(sum / living);
            std::printf(" -> %2d (gene %d -> %d, %lld alive)", brood_of(mean), int(founder_genome(FounderKind(k)).gene[agene_investment]),
                        int(sum / living), static_cast<long long>(living));
        }
        std::printf("\n");
    }
    std::printf("kills over all seeds, killer down, victim across:\n%-8s", "");
    for (int b = 0; b < founder_kinds; ++b) std::printf(" %8s", founder_name(FounderKind(b)));
    for (int a = 0; a < founder_kinds; ++a) {
        std::printf("\n%-8s", founder_name(FounderKind(a)));
        for (int b = 0; b < founder_kinds; ++b) {
            int n = 0;
            for (const auto& run : runs) n += run.kills[a][b];
            std::printf(" %8d", n);
        }
    }
    std::printf("\n");
    const auto& one = runs.front();
    std::printf("lakes on seed %u: %d cells; edible energy a cell at each year's end:", first, one.lake_cells);
    for (const auto food : one.lake_food) std::printf(" %lld", static_cast<long long>(food / std::max(1, one.lake_cells)));
    std::printf("\ndry ground on seed %u: %d cells; edible energy a cell at each year's end:", first, one.land_cells);
    for (const auto food : one.land_food) std::printf(" %lld", static_cast<long long>(food / std::max(1, one.land_cells)));
    std::printf("\n  and cells holding 28 or more:");
    for (const auto n : one.land_fed) std::printf(" %d", n);
    std::printf("\n");
    if (!csv.empty()) {
        const auto text = out.str();
        write_file_atomic(csv.string(), {text.begin(), text.end()});
    }
}

// A balancing trial's changes: "growth=150,swimmer.like=fish,swimmer.size=60".
// growth, meat and upkeep are the world's dials, in percent; KIND.GENE is a
// founder's gene, and KIND.like=OTHER gives it the other's body, keeping its
// own plan (legs, fins, wings, gills) so that it is still told apart.
void apply(std::string_view changes) {
    static constexpr const char* genes[agene_count] = {"size", "build", "legs", "leg", "sprawl", "fins", "tail", "wings", "neck", "gills", "plant", "meat",
                                                       "fat", "hue", "tone", "pattern", "pattern_tone", "maturity", "investment", "eyes", "nose",
                                                       "ears", "voice"};
    const auto kind_called = [](std::string_view name) {
        for (int k = 0; k < founder_kinds; ++k) if (name == founder_name(FounderKind(k))) return FounderKind(k);
        throw std::runtime_error("No kind called " + std::string(name));
    };
    while (!changes.empty()) {
        const auto comma = changes.find(',');
        const auto one = changes.substr(0, comma);
        changes = comma == std::string_view::npos ? std::string_view{} : changes.substr(comma + 1);
        const auto equals = one.find('=');
        if (equals == std::string_view::npos) throw std::runtime_error("--set wants NAME=VALUE: " + std::string(one));
        const auto name = one.substr(0, equals), value = one.substr(equals + 1);
        const auto dot = name.find('.');
        if (dot == std::string_view::npos) {
            const int percent = std::stoi(std::string(value));
            if (name == "growth") tuning::plant_growth_percent = percent;
            else if (name == "meat") tuning::meat_percent = percent;
            else if (name == "upkeep") tuning::upkeep_percent = percent;
            else throw std::runtime_error("Nothing to set called " + std::string(name));
            continue;
        }
        const FounderKind kind = kind_called(name.substr(0, dot));
        const auto gene = name.substr(dot + 1);
        AnimalGenome genome = founder_genome(kind);
        if (gene == "brain") {
            borrowed_brains.push_back({kind, kind_called(value)});
            continue;
        }
        if (gene == "like") {
            const AnimalGenome own = genome;
            genome = founder_genome(kind_called(value));
            for (const auto plan : {agene_legs, agene_fins, agene_wings, agene_gills}) genome.gene[plan] = own.gene[plan];
        } else {
            int g = 0;
            while (g < agene_count && gene != genes[g]) ++g;
            if (g == agene_count) throw std::runtime_error("No gene called " + std::string(gene));
            genome.gene[g] = std::uint8_t(std::clamp(std::stoi(std::string(value)), 0, 255));
        }
        set_founder_genome(kind, genome);
    }
}

// How much killing and meat-eating a world sees.
// ---- The hunting school.
//
// A hunter's body can kill; whether it eats depends on getting its mouth to
// the prey, and that is a skill. Here brains are bred for it in arenas: a
// few hunters of a body nobody designed, a breeding herd of grazers of
// bodies nobody designed either, and food enough for the herd. Bodies come
// from sets the wild exam never uses, on land and in water alike, so what
// is kept is a way of hunting and not a way of hunting one animal.

struct Scenario {
    AnimalGenome hunter;
    std::vector<AnimalGenome> prey;
    bool wet;
};

std::vector<Scenario> scenarios(std::uint32_t first, int count) {
    std::vector<Scenario> out;
    for (std::uint32_t seed = first; int(out.size()) < count && seed < first + 64; ++seed) {
        const auto bodies = random_bodies(seed, 10, 5);
        for (const bool wet : {false, true}) {
            const auto lives = [&](const AnimalGenome& g) {
                const Frame f = frame(g);
                return wet ? f.breathes_water && f.fin : f.breathes_air && f.leg_pairs && !f.breathes_water;
            };
            Scenario s{{}, {}, wet};
            bool hunter = false;
            for (std::size_t i = 0; i < bodies.size(); i += 5) {
                if (!lives(bodies[i])) continue;
                if (diet_of(bodies[i]) == Diet::carnivore && !hunter) { s.hunter = bodies[i]; hunter = true; }
                if (diet_of(bodies[i]) == Diet::herbivore) s.prey.push_back(bodies[i]);
            }
            if (hunter && !s.prey.empty() && int(out.size()) < count) out.push_back(s);
        }
    }
    return out;
}

constexpr int hunt_hunters = 4, hunt_herd = 30, hunt_width = 32, hunt_height = 24;
// Down one side lies what the bodies cannot live in: deep water beside the
// land arenas, a bank beside the water ones. A chase that ends there is lost.
constexpr int hunt_hazard = 5;

HuntResult hunt_trial(const Scenario& s, std::uint32_t seed, const Brain& mind, int days) {
    WorldConfig c;
    c.layout = Layout::arena;
    c.seed = seed;
    c.width = hunt_width + hunt_hazard + 2;
    c.height = hunt_height + 2;
    c.ocean_percent = 0;
    c.climate = 180;
    c.seasons = 0;
    c.arena_depth_dm = 20;
    c.arena_light = 200;
    c.arena_land = std::uint16_t(s.wet ? hunt_hazard : hunt_width);
    const int first_x = s.wet ? 1 + hunt_hazard : 1;
    // Food for the herd: a good many mouthfuls a cell, and twice what it burns growing each pass.
    int bite = 2;
    std::int64_t need = 0;
    for (int i = 0; i < hunt_herd; ++i) {
        const auto& g = s.prey[std::size_t(i) % s.prey.size()];
        const Frame f = frame(g);
        bite = std::max(bite, f.bite);
        need += f.upkeep * 2 * 64 / std::max<int>(1, g.gene[agene_plant_gut]);
    }
    c.arena_food = std::uint16_t(std::clamp(24 * bite, 16, 60000));
    c.arena_regrow = std::uint16_t(std::clamp<std::int64_t>(need * 2 * 16 / (hunt_width * hunt_height), 1, std::min(60000, int(c.arena_food) * 16)));
    World world(c, {{first_x, 1, hunt_width, hunt_height}});
    Random rng(stream_seed(seed, 0x68756e74u));
    const auto put = [&](const AnimalGenome& body, const Brain& brain, int energy) {
        const int x = (first_x + int(rng.below(hunt_width))) * 256 + int(rng.below(256));
        const int y = (1 + int(rng.below(hunt_height))) * 256 + int(rng.below(256));
        return world.add_animal(varied(body, rng), x, y, std::uint8_t(rng.below(256)), energy, brain);
    };
    const int ticks = hunt_trial_ticks(s.hunter, days);
    HuntWatch watch(ticks);
    for (int i = 0; i < hunt_hunters; ++i) {
        const auto id = put(s.hunter, mind, 60);
        watch.add_founder(id);
    }
    const Brain grazer = brain::founder();
    for (int i = 0; i < hunt_herd; ++i) put(s.prey[std::size_t(i) % s.prey.size()], grazer, 80);
    for (int t = 0; t < ticks; ++t) {
        if (std::uint64_t(t + 1) == watch.late_start) watch.r.late_adults = watch.adult_count(world);
        world.step(&watch);
        if (t % 64 == 63) {
            const auto& all = world.animals();
            if (std::none_of(all.begin(), all.end(), [&](const Animal& a) { return watch.member(a.id); })) break;
        }
    }
    HuntResult r = watch.finish(world, ticks);
    r.wet = s.wet;
    return r;
}

constexpr char hunter_magic[4] = {'P', 'H', 'D', 'H'};

std::vector<Brain> load_hunters(const std::string& path) {
    std::vector<std::uint8_t> data;
    if (!read_file(path, data) || data.size() < 8 || std::memcmp(data.data(), hunter_magic, 4) != 0) throw std::runtime_error("cannot read hunters from " + path);
    const std::size_t count = data[4] | data[5] << 8;
    if (!count || data.size() != 8 + count * brain::bytes) throw std::runtime_error("damaged hunters file " + path);
    std::vector<Brain> out(count);
    std::size_t at = 8;
    for (auto& b : out) for (auto& w : b.weight) w = std::int8_t(data[at++]);
    return out;
}

struct HuntSum {
    long long meat = 0, score = 0, alive = 0, births = 0, kin = 0, grown = 0, young = 0, lunges = 0, herd = 0;
    long long adults = 0, recruits = 0, late = 0, hits = 0;
    std::int64_t food_hunted = 0, food_meat = 0, food_plants = 0;
    int n = 0, passed[2] = {}, tried[2] = {};
    std::map<std::string, int> failures;
    void add(const HuntResult& r) {
        meat += r.need ? r.hunted_meat * 1000 / r.need : 0;
        score += r.score(); alive += r.alive; births += r.births; kin += r.kin_kills;
        grown += r.kills_grown; young += r.kills_young; lunges += r.lunges; herd += r.herd; ++n;
        adults += r.adults; recruits += r.living_recruits;
        late += r.late_need ? r.late_hunted_meat * 1000 / r.late_need : 0;
        hits += r.hits;
        food_hunted += r.hunted_meat; food_meat += r.meat; food_plants += r.plants;
        ++tried[r.wet];
        if (const auto failure = r.fails()) ++failures[failure]; else ++passed[r.wet];
    }
    void add(const HuntSum& s) {
        meat += s.meat; score += s.score; alive += s.alive; births += s.births; kin += s.kin;
        grown += s.grown; young += s.young; lunges += s.lunges; herd += s.herd; n += s.n;
        adults += s.adults; recruits += s.recruits; late += s.late; hits += s.hits;
        food_hunted += s.food_hunted; food_meat += s.food_meat; food_plants += s.food_plants;
        for (int i = 0; i < 2; ++i) { passed[i] += s.passed[i]; tried[i] += s.tried[i]; }
        for (const auto& [reason, count] : s.failures) failures[reason] += count;
    }
    bool graduates() const {
        return hunt_exam_passes(passed, tried);
    }
    bool better_than(const HuntSum& s) const {
        if (graduates() != s.graduates()) return graduates();
        const int successes = passed[0] + passed[1], theirs = s.passed[0] + s.passed[1];
        return successes != theirs ? successes > theirs : score > s.score;
    }
    void print(const char* name) const {
        const double d = std::max(1, n);
        const double food = double(std::max<std::int64_t>(1, food_meat + food_plants));
        std::printf("%-22s score %6.0f  hunt/cost %5.1f%%  hunted/plants/scavenged %4.1f/%4.1f/%4.1f%%  final hunt/cost %5.1f%%  adults %5.2f  grown young alive %5.2f  births %5.2f  kin kills %4.2f  hits %4.1f%%  pass land %d/%d water %d/%d%s\n",
                    name, score / d, meat / d / 10, 100.0 * food_hunted / food, 100.0 * food_plants / food,
                    100.0 * (food_meat - food_hunted) / food, late / d / 10,
                    adults / d, recruits / d, births / d, kin / d, 100.0 * hits / std::max(1ll, lunges),
                    passed[0], tried[0], passed[1], tried[1], graduates() ? " GRADUATED" : " not graduated");
        if (!failures.empty()) {
            std::printf("  missing:");
            for (const auto& [reason, count] : failures) std::printf(" %s (%d);", reason.c_str(), count);
            std::printf("\n");
        }
    }
};

void hunt(const std::string& out_path, int rounds, int days, int jobs, std::uint32_t seed) {
    const auto train = scenarios(101, 10), pick = scenarios(301, 8), test = scenarios(201, 8);
    std::printf("hunting school: %zu training arenas, %zu to choose on, %zu to report on; %d hunters, a herd of %d, at least %d days (two growth periods)\n",
                train.size(), pick.size(), test.size(), hunt_hunters, hunt_herd, days);
    constexpr int pool = 64, keep = 16, per_round = 4;
    const Brain hand = brain::founder(true, true, true);
    Random rng(stream_seed(seed, 0x7363686fu));
    std::uint32_t state = seed * 2654435761u + 1;
    std::vector<Brain> brains(pool, hand);
    for (int i = 1; i < pool; ++i) {
        if (i >= 48) brains[std::size_t(i)] = random_brain(rng); else vary(brains[std::size_t(i)], rng);
    }
    const auto measure = [&](const std::vector<Brain>& minds, const std::vector<Scenario>& on, int trials, std::uint32_t base) {
        std::vector<HuntResult> results(minds.size() * std::size_t(trials));
        parallel(int(results.size()), jobs, [&](int i) {
            const int b = i / trials, k = i % trials;
            results[std::size_t(i)] = hunt_trial(on[std::size_t(k) % on.size()], base + std::uint32_t(k), minds[std::size_t(b)], days);
        });
        std::vector<HuntSum> sums(minds.size());
        for (std::size_t i = 0; i < results.size(); ++i) sums[i / std::size_t(trials)].add(results[i]);
        return sums;
    };
    for (int round = 0; round < rounds; ++round) {
        // Every brain meets the same arenas, and they are new each round.
        std::vector<Scenario> on;
        for (int k = 0; k < per_round; ++k) on.push_back(train[std::size_t(round * per_round + k) % train.size()]);
        const auto sums = measure(brains, on, per_round, seed * 100000u + std::uint32_t(round) * 16u);
        std::vector<int> order(pool);
        for (int i = 0; i < pool; ++i) order[std::size_t(i)] = i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return sums[std::size_t(a)].better_than(sums[std::size_t(b)]); });
        HuntSum kept;
        for (int i = 0; i < keep; ++i) kept.add(sums[std::size_t(order[std::size_t(i)])]);
        if (round % 5 == 0 || round == rounds - 1) {
            char name[32];
            std::snprintf(name, sizeof(name), "round %d, kept", round);
            kept.print(name);
            std::fflush(stdout);
        }
        std::vector<Brain> next;
        for (int i = 0; i < keep; ++i) next.push_back(brains[std::size_t(order[std::size_t(i)])]);
        while (int(next.size()) < pool) {
            Brain child = next[rng.below(keep)];
            if (rng.below(2)) brain::combine(child, next[rng.below(keep)], next[rng.below(keep)], state);
            vary(child, rng);
            next.push_back(child);
        }
        brains = std::move(next);
    }
    // Choose on unseen arenas; a high score alone does not qualify a pool.
    brains.resize(keep);
    const int trials = int(pick.size()) * 2;
    const auto chosen = measure(brains, pick, trials, seed * 100000u + 50000u);
    std::vector<int> order(keep);
    for (int i = 0; i < keep; ++i) order[std::size_t(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return chosen[std::size_t(a)].better_than(chosen[std::size_t(b)]); });
    std::vector<Brain> best;
    for (int i = 0; i < 4; ++i) best.push_back(brains[std::size_t(order[std::size_t(i)])]);
    std::vector<Brain> both = best;
    both.push_back(hand);
    both.push_back(brain::founder(true, true, false));
    {
        // The hunter's reflexes without its sprint.
        Brain walker = hand;
        auto* pace = walker.weight + brain::hidden * brain::unit_bytes + brain::out_pace * brain::output_bytes;
        pace[14] = 0, pace[8] = 0;
        both.push_back(walker);
    }
    const auto report = measure(both, test, int(test.size()) * 3, seed * 100000u + 70000u);
    std::printf("\nfinal graduation exam on %zu arenas, three runs each:\n", test.size());
    for (int i = 0; i < 4; ++i) { char name[32]; std::snprintf(name, sizeof(name), "candidate %d", i + 1); report[std::size_t(i)].print(name); }
    report[4].print("hand-wired hunter");
    report[5].print("hand-wired opportunist");
    report[6].print("hand-wired, no sprint");
    const auto save_pool = [&](const std::string& path, const std::vector<Brain>& pool) {
        std::vector<std::uint8_t> data(hunter_magic, hunter_magic + 4);
        data.push_back(std::uint8_t(pool.size())); data.push_back(0); data.push_back(0); data.push_back(0);
        for (const auto& b : pool) for (const auto w : b.weight) data.push_back(std::uint8_t(w));
        if (!write_file_atomic(path, data)) throw std::runtime_error("cannot write " + path);
    };
    save_pool(out_path + ".candidates.phdh", best);
    std::vector<Brain> graduated;
    for (int i = 0; i < 4; ++i)
        if (chosen[std::size_t(order[std::size_t(i)])].graduates() && report[std::size_t(i)].graduates()) graduated.push_back(best[std::size_t(i)]);
    std::fflush(stdout);
    if (graduated.empty()) throw std::runtime_error("no hunters graduated; candidates saved to " + out_path + ".candidates.phdh; requested output unchanged");
    save_pool(out_path, graduated);
    std::printf("wrote %zu graduated hunters to %s\n", graduated.size(), out_path.c_str());
}

struct Bloodshed : StepObserver {
    int kills = 0, starved = 0; int fate[3][2] = {}; long long age_sum = 0; int born_carn = 0; void born(const World&, const Animal& y, const Animal&, const Animal&) override { born_carn += diet_of(y.genome) == Diet::carnivore; }
    long long meat = 0, plants = 0;
    // By the diet of the one that killed, ate or died.
    int kills_by[3] = {}, of_young[3] = {}, of_kin[3] = {}, starved_of[3] = {}, killed_of[3] = {};
    long long carcass_by[3] = {}, meat_by[3] = {}, plants_by[3] = {}, left_by_starved = 0;
    void died(const World& world, const Animal& a, DeathCause cause, std::uint32_t killer) override {
        const Frame f = frame(a.genome, a.growth);
        const int energy = flesh_of(a) + std::clamp(a.energy, 0, f.store);
        if (diet_of(a.genome) == Diet::carnivore) { ++fate[int(cause)][a.growth < 255 ? 0 : 1]; age_sum += a.age; } if (cause != DeathCause::killed) { ++starved; ++starved_of[int(diet_of(a.genome))]; left_by_starved += energy; return; }
        ++kills;
        ++killed_of[int(diet_of(a.genome))];
        if (diet_of(a.genome) == Diet::carnivore) {
            // By its own bite on something harder, or by whom?
            const auto bit = last_bit.find(a.id);
            if (bit != last_bit.end() && bit->second == killer) ++carnivore_end[3];
            else for (const auto& k : world.animals()) {
                if (k.id != killer) continue;
                ++carnivore_end[genetic_distance(k.genome, a.genome) < 1000 ? 4 : int(diet_of(k.genome))];
            }
        }
        for (const auto& k : world.animals()) {
            if (k.id != killer) continue;
            const int d = int(diet_of(k.genome));
            ++kills_by[d];
            of_young[d] += a.growth < 255;
            of_kin[d] += genetic_distance(k.genome, a.genome) < breeding_distance;
            carcass_by[d] += energy;
        }
    }
    // Lunges, by the diet of the one lunging.
    long long lunges[3] = {}, empty[3] = {}, other_medium[3] = {}, short_of[3] = {};
    long long hit_young[3] = {}, hit_grown[3] = {}, hit_kin[3] = {}, damage_grown[3] = {}, own_speed[3] = {}, their_speed[3] = {};
    long long first_health[3] = {};
    std::set<std::uint32_t> wounded[3];
    std::map<std::uint32_t, std::uint32_t> last_bit; // whom each last struck
    int carnivore_end[5] = {};                       // killed by a herbivore, omnivore, unrelated carnivore, its own bite, a relative
    static int speed_in(const Animal& a) {
        const Frame f = frame(a.genome, a.growth);
        return Medium(a.medium) == Medium::air ? f.air_speed : Medium(a.medium) == Medium::water ? f.water_speed : f.land_speed;
    }
    void struck(const World& world, const Animal& a, const Animal* b, int damage) override {
        const int d = int(diet_of(a.genome));
        ++lunges[d];
        if (b) last_bit[a.id] = b->id;
        if (!b) {
            // What was within a cell of it, if anything?
            bool near = false, elsewhere = false;
            for (const auto& o : world.animals()) {
                if (o.id == a.id || !o.health) continue;
                const long long dx = o.x - a.x, dy = o.y - a.y;
                if (dx * dx + dy * dy > 256 * 256) continue;
                (o.medium == a.medium ? near : elsewhere) = true;
            }
            ++(near ? short_of : elsewhere ? other_medium : empty)[d];
            return;
        }
        if (genetic_distance(a.genome, b->genome) < breeding_distance) ++hit_kin[d];
        if (b->growth < 255) { ++hit_young[d]; return; }
        ++hit_grown[d];
        damage_grown[d] += damage;
        own_speed[d] += speed_in(a);
        their_speed[d] += speed_in(*b);
        if (wounded[d].insert(b->id).second) first_health[d] += b->health;
    }
    void ate(const World&, const Animal& a, int gained, bool was_meat) override {
        (was_meat ? meat : plants) += gained;
        (was_meat ? meat_by : plants_by)[int(diet_of(a.genome))] += gained;
    }
};

// A world begun from bodies nobody designed. Every body is settled as one
// family, all with the reflexes wired by hand, and a taxonomy counts what
// is alive as the years go by.
struct GenesisRun {
    std::vector<int> settled;                // of each body
    std::vector<std::vector<int>> living;    // at each count, of each body's taxon
    std::vector<int> animals, kinds, fresh, named, herbivores, omnivores, carnivores, kills, meat, winged;
    Bloodshed blood;  // at each count: animals, taxa alive, taxa alive that no founder was
    std::vector<std::string> newcomers;      // the names of those, at the end
};

void genesis(std::uint32_t body_seed, int genera, int each, int family, int years, int maps, int jobs, std::uint32_t first, int every = 0, int late = 0, int pack = 0,
             const std::vector<Brain>& hunters = {}) {
    const auto bodies = random_bodies(body_seed, genera, each);
    // The founders' own taxa, in their order: relatives near enough share a species as subspecies.
    Taxonomy founders;
    std::vector<Taxonomy::Place> place;
    for (const auto& b : bodies) place.push_back(founders.classify(b));
    std::vector<int> counts;
    for (int y = every ? every : 1; y < years; y = every ? y + every : y * 2) counts.push_back(y);
    counts.push_back(years);

    std::vector<GenesisRun> runs(std::size_t(maps), GenesisRun{});
    parallel(maps, jobs, [&](int m) {
        GenesisRun& run = runs[std::size_t(m)];
        WorldConfig config;
        config.seed = first + std::uint32_t(m);
        config.layout = Layout::development;
        config.width = 128;
        config.height = 96;
        World world(config);
        // Hunters can come `late` years after the rest, `pack` of each, once there is something to catch.
        const auto settle = [&](bool late_hunters) {
            std::size_t schooled = 0;
            for (const auto& b : bodies) {
                const bool hunter = diet_of(b) == Diet::carnivore;
                if (late && hunter != late_hunters) continue;
                // Carnivores carry brains from the hunting school if there are any.
                const Brain mind = hunter && !hunters.empty() ? hunters[schooled++ % hunters.size()]
                                                              : brain::founder(diet_of(b) != Diet::herbivore, true, hunter);
                const int settled = world.settle(b, hunter && pack ? pack : family, mind);
                if (!late) run.settled.push_back(settled);
            }
        };
        settle(false);
        Bloodshed blood;
        Taxonomy taxa = founders;
        const auto known = taxa.species().size();
        std::vector<std::size_t> races;
        for (const auto& s : taxa.species()) races.push_back(s.subspecies.size());
        int year = 0;
        for (const int until : counts) {
            for (; year < until; ++year) {
                if (late && year == late) settle(true);
                for (int t = 0; t < World::ticks_per_year; ++t) world.step(&blood);
            }
            run.kills.push_back(blood.kills);
            run.blood = blood;
            run.meat.push_back(int(blood.meat * 10000 / std::max(1ll, blood.meat + blood.plants)));
            taxa.census(world.animals());
            std::vector<int> living;
            for (const auto& p : place) living.push_back(taxa.species()[std::size_t(p.species)].subspecies[std::size_t(p.subspecies)].living);
            run.living.push_back(living);
            int kinds = 0, fresh = 0;
            run.newcomers.clear();
            for (std::size_t s = 0; s < taxa.species().size(); ++s) {
                const auto& species = taxa.species()[s];
                for (std::size_t r = 0; r < species.subspecies.size(); ++r) {
                    if (!species.subspecies[r].living) continue;
                    ++kinds;
                    if (s >= known || r >= races[s]) {
                        ++fresh;
                        run.newcomers.push_back(taxa.name({int(s), int(r)}) + " (" + taxa.common({int(s), int(r)}) + "), " +
                                                std::to_string(species.subspecies[r].living) + " alive");
                    }
                }
            }
            run.animals.push_back(int(world.animals().size()));
            int eats[3] = {};
            for (const auto& a : world.animals()) ++eats[int(diet_of(a.genome))];
            run.herbivores.push_back(eats[0]);
            run.omnivores.push_back(eats[1]);
            run.carnivores.push_back(eats[2]);
            int winged = 0;
            for (const auto& a : world.animals()) winged += frame(a.genome).air_speed > 0;
            run.winged.push_back(winged);
            run.kinds.push_back(kinds);
            run.fresh.push_back(fresh);
            { int n = 0; for (const auto& sp : taxa.species()) n += int(sp.subspecies.size()); run.named.push_back(n); }
        }
    });

    std::printf("%zu bodies in %d genera of %d, %d of each settled on %d maps (seeds %u to %u), %d years\n\n", bodies.size(), genera, each, family, maps,
                first, first + std::uint32_t(maps) - 1, years);
    std::printf("%-3s %-22s %-34s %-34s %-5s %5s %5s %5s  maps alive at year", "", "family", "name", "in plain speech", "lives", "long", "young", "eats");
    for (const int c : counts) std::printf(" %3d", c);
    std::printf("   living at the end\n");
    for (std::size_t b = 0; b < bodies.size(); ++b) {
        const auto& g = bodies[b];
        // A relative so near that it shares its taxon with one above is counted there.
        bool shared = false;
        for (std::size_t e = 0; e < b; ++e) shared = shared || (place[e].species == place[b].species && place[e].subspecies == place[b].subspecies);
        std::printf("%-3zu %-22s %-34s %-34s %-5s %4.1fm %5d %-9s ", b + 1, founders.family(place[b]).c_str(), founders.name(place[b]).c_str(),
                    founders.common(place[b]).c_str(), frame(g).breathes_air ? "land" : "water", body_length_mm(g, 255) / 1000.0, brood_of(g),
                    diet_name(diet_of(g)));
        if (shared) { std::printf("                  (counted with the one above it shares a name with)\n"); continue; }
        std::printf("                  ");
        long long end = 0;
        for (std::size_t c = 0; c < counts.size(); ++c) {
            int alive = 0;
            for (const auto& run : runs) alive += run.living[c][b] > 0;
            std::printf(" %3d", alive);
        }
        for (const auto& run : runs) end += run.living.back()[b];
        std::printf("   %lld\n", end);
    }
    std::printf("\n%-28s", "year");
    for (const int c : counts) std::printf(" %6d", c);
    const auto row = [&](const char* what, std::vector<int> GenesisRun::* field) {
        std::printf("\n%-28s", what);
        for (std::size_t c = 0; c < counts.size(); ++c) {
            long long sum = 0;
            for (const auto& run : runs) sum += (run.*field)[c];
            std::printf(" %6lld", sum / maps);
        }
    };
    row("animals a map", &GenesisRun::animals);
    row("  herbivores", &GenesisRun::herbivores);
    row("  omnivores", &GenesisRun::omnivores);
    row("  carnivores", &GenesisRun::carnivores);
    row("  that can fly", &GenesisRun::winged);
    row("kills so far a map", &GenesisRun::kills);
    row("meat, per 10,000 eaten so far", &GenesisRun::meat);
    row("named kinds alive a map", &GenesisRun::kinds);
    row("of them new since the start", &GenesisRun::fresh);
    row("kinds ever named a map", &GenesisRun::named);
    std::printf("\n\n%-10s %7s %7s %7s %12s %12s %12s %8s %8s\n", "all maps", "kills", "young", "own kin", "meat killed", "meat eaten", "plants eaten", "starved", "killed");
    for (int d = 0; d < 3; ++d) {
        Bloodshed sum;
        for (const auto& run : runs) {
            const auto& b = run.blood;
            sum.kills_by[d] += b.kills_by[d]; sum.of_young[d] += b.of_young[d]; sum.of_kin[d] += b.of_kin[d]; sum.carcass_by[d] += b.carcass_by[d];
            sum.meat_by[d] += b.meat_by[d]; sum.plants_by[d] += b.plants_by[d]; sum.starved_of[d] += b.starved_of[d]; sum.killed_of[d] += b.killed_of[d];
        }
        std::printf("%-10s %7d %7d %7d %12lld %12lld %12lld %8d %8d\n", diet_name(Diet(d)), sum.kills_by[d], sum.of_young[d], sum.of_kin[d], sum.carcass_by[d],
                    sum.meat_by[d], sum.plants_by[d], sum.starved_of[d], sum.killed_of[d]);
    }
    std::printf("lunges       total  at nothing  prey in air/water  just out of reach  hit young  hit grown  hit kin  grown wounded  grown killed  wound/255  own speed  their speed\n");
    for (int d = 1; d < 3; ++d) {
        long long n[10] = {}, grown_killed = 0, wounded = 0;
        for (const auto& run : runs) {
            const Bloodshed& b = run.blood;
            n[0] += b.lunges[d]; n[1] += b.empty[d]; n[2] += b.other_medium[d]; n[3] += b.short_of[d];
            n[4] += b.hit_young[d]; n[5] += b.hit_grown[d]; n[6] += b.hit_kin[d]; n[7] += b.damage_grown[d];
            n[8] += b.own_speed[d]; n[9] += b.their_speed[d];
            wounded += (long long)b.wounded[d].size();
            grown_killed += b.kills_by[d] - b.of_young[d];
        }
        const long long g = std::max(1LL, n[5]);
        std::printf("%-9s %8lld %11lld %18lld %18lld %10lld %10lld %8lld %14lld %13lld %10lld %10lld %12lld\n", diet_name(Diet(d)),
                    n[0], n[1], n[2], n[3], n[4], n[5], n[6], wounded, grown_killed, n[7] / g, n[8] / g, n[9] / g);
    }
    {
        int e[5] = {};
        for (const auto& run : runs) for (int k = 0; k < 5; ++k) e[k] += run.blood.carnivore_end[k];
        std::printf("carnivores killed: by herbivores %d, omnivores %d, unrelated carnivores %d, relatives %d, their own bite on something harder %d\n", e[0], e[1], e[2], e[4], e[3]);
    }
    long long left = 0;
    for (const auto& run : runs) left += run.blood.left_by_starved;
    std::printf("meat left by the starved and suffocated: %lld\n", left); { int f[3][2] = {}; long long age = 0; int born = 0; for (const auto& run : runs) { for (int c = 0; c < 3; ++c) for (int g = 0; g < 2; ++g) f[c][g] += run.blood.fate[c][g]; age += run.blood.age_sum; born += run.blood.born_carn; } std::printf("carnivores born %d; died young/grown: starved %d/%d suffocated %d/%d killed %d/%d; mean age at death %lld days\n", born, f[0][0], f[0][1], f[1][0], f[1][1], f[2][0], f[2][1], age / std::max(1, f[0][0]+f[0][1]+f[1][0]+f[1][1]+f[2][0]+f[2][1])); }
    std::printf("\n\nnew kinds alive at the end on map %u:\n", first);
    for (const auto& n : runs.front().newcomers) std::printf("  %s\n", n.c_str());
    if (runs.front().newcomers.empty()) std::printf("  none\n");
}

// The names a taxonomy gives: to the five founders, then to made-up
// bodies and to each one's drifting relatives.
void names(int random, std::uint32_t seed) {
    Taxonomy taxa;
    const auto line = [&](const char* what, const AnimalGenome& genome) {
        const auto place = taxa.classify(genome);
        std::printf("%-22s %-20s %-34s %s\n", what, taxa.family(place).c_str(), taxa.name(place).c_str(), taxa.common(place).c_str());
    };
    std::printf("%-22s %-20s %-34s %s\n", "", "family", "name", "in plain speech");
    for (int k = 0; k < founder_kinds; ++k) line(founder_name(FounderKind(k)), founder_genome(FounderKind(k)));
    Random rng(stream_seed(seed, 0x6e616d65u));
    // A relative: the same body with its proportions shifted by `by` in all.
    const auto drift = [&](AnimalGenome g, int by) {
        while (by > 0) {
            const auto gene = rng.below(agene_count);
            if (gene == agene_legs || gene == agene_fins || gene == agene_wings || gene == agene_gills) continue;
            const int step = std::min(by, 1 + int(rng.below(30)));
            g.gene[gene] = std::uint8_t(std::clamp(int(g.gene[gene]) + (rng.below(2) ? step : -step), 0, 255));
            by -= step;
        }
        return g;
    };
    for (int n = 0; n < random; ++n) {
        AnimalGenome g;
        for (auto& gene : g.gene) gene = std::uint8_t(rng.below(256));
        std::printf("\n");
        line("a new body", g);
        line("  its sibling", drift(g, 60));
        line("  a drifted population", drift(g, 200));
        line("  drifted further", drift(g, 320));
        line("  further still", drift(g, 600));
    }
    std::printf("\n%zu genera, %zu species\n", taxa.genera().size(), taxa.species().size());
}

const char* usage =
    "usage: petrihd-nursery train DIRECTORY [--jobs N] [--levels N] [--kinds grazer,fish,...] [--families N]\n"
    "                             [--rounds N] [--least N] [--patience N] [--seed N] [--quick] [--set ...]\n"
    "       petrihd-nursery budget\n"
    "       petrihd-nursery names [--random N] [--seed N]\n"
    "       petrihd-nursery hunt FILE [--rounds N] [--days N] [--seed N] [--jobs N]   breed hunters' brains in arenas\n"
    "       petrihd-nursery genesis [--hunters FILE] [--seed N] [--genera N] [--each N] [--family N] [--years N] [--maps N] [--first SEED]\n"
    "       petrihd-nursery wild FOUNDERS|- [--years N] [--seeds N] [--first SEED] [--jobs N] [--csv FILE]\n"
    "                                        [--set growth=N,meat=N,upkeep=N,KIND.GENE=N,KIND.like=KIND]\n"
    "                                        [--late YEARS]   meat-eaters arrive after this long; the count of years starts then\n";

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view command = argc > 1 ? argv[1] : "";
        if (command == "budget" && (argc == 2 || (argc == 4 && std::string_view(argv[2]) == "--set"))) {
            if (argc == 4) apply(argv[3]);
            budget();
        } else if (command == "genesis") {
            int genera = 10, each = 5, family = 6, years = 20, maps = 8, every = 0, late = 0, pack = 0, jobs = int(std::max(1u, std::thread::hardware_concurrency()));
            std::uint32_t seed = 1, first = 7101;
            std::vector<Brain> hunters;
            for (int i = 2; i + 1 < argc; i += 2) {
                const std::string_view arg = argv[i];
                if (arg == "--genera") genera = int(number(argv[i + 1]));
                else if (arg == "--each") each = int(number(argv[i + 1]));
                else if (arg == "--family") family = int(number(argv[i + 1]));
                else if (arg == "--years") years = int(number(argv[i + 1]));
                else if (arg == "--maps") maps = int(number(argv[i + 1]));
                else if (arg == "--jobs") jobs = int(number(argv[i + 1]));
                else if (arg == "--seed") seed = number(argv[i + 1]);
                else if (arg == "--first") first = number(argv[i + 1]);
                else if (arg == "--every") every = int(number(argv[i + 1]));
                else if (arg == "--late") late = int(number(argv[i + 1]));
                else if (arg == "--pack") pack = int(number(argv[i + 1]));
                else if (arg == "--hunters") hunters = load_hunters(argv[i + 1]);
                else if (arg == "--set") apply(argv[i + 1]);
                else throw std::runtime_error(usage);
            }
            if (genera < 1 || each < 1 || family < 1 || years < 1 || maps < 1 || jobs < 1) throw std::runtime_error(usage);
            genesis(seed, genera, each, family, years, maps, jobs, first, every, late, pack, hunters);
        } else if (command == "hunt" && argc >= 3) {
            int rounds = 40, days = 30, jobs = int(std::max(1u, std::thread::hardware_concurrency()));
            std::uint32_t seed = 1;
            for (int i = 3; i + 1 < argc; i += 2) {
                const std::string_view arg = argv[i];
                if (arg == "--rounds") rounds = int(number(argv[i + 1]));
                else if (arg == "--days") days = int(number(argv[i + 1]));
                else if (arg == "--jobs") jobs = int(number(argv[i + 1]));
                else if (arg == "--seed") seed = number(argv[i + 1]);
                else throw std::runtime_error(usage);
            }
            if (rounds < 0 || days < 1 || days > 1000000 || jobs < 1) throw std::runtime_error(usage);
            hunt(argv[2], rounds, days, jobs, seed);
        } else if (command == "names") {
            int random = 6;
            std::uint32_t seed = 1;
            for (int i = 2; i + 1 < argc; i += 2) {
                const std::string_view arg = argv[i];
                if (arg == "--random") random = int(number(argv[i + 1]));
                else if (arg == "--seed") seed = number(argv[i + 1]);
                else throw std::runtime_error(usage);
            }
            names(random, seed);
        } else if (command == "wild" && argc >= 3) {
            int years = 10, seeds = 8, jobs = int(std::max(1u, std::thread::hardware_concurrency()));
            std::uint32_t first = 7101;
            fs::path csv;
            for (int i = 3; i + 1 < argc; i += 2) {
                const std::string_view arg = argv[i];
                if (arg == "--years") years = int(number(argv[i + 1]));
                else if (arg == "--seeds") seeds = int(number(argv[i + 1]));
                else if (arg == "--first") first = number(argv[i + 1]);
                else if (arg == "--jobs") jobs = int(number(argv[i + 1]));
                else if (arg == "--csv") csv = argv[i + 1];
                else if (arg == "--set") apply(argv[i + 1]);
                else if (arg == "--late") late_years = int(number(argv[i + 1]));
                else throw std::runtime_error(usage);
            }
            if (years < 1 || seeds < 1 || jobs < 1) throw std::runtime_error(usage);
            wild(argv[2], years, seeds, jobs, first, csv);
        } else if (command == "train" && argc >= 3) {
            Nursery n;
            auto& o = n.o;
            o.dir = argv[2];
            for (int i = 3; i < argc; ++i) {
                const std::string_view arg = argv[i];
                const auto value = [&] {
                    if (i + 1 >= argc) throw std::runtime_error(std::string(arg) + " needs a value");
                    return std::string_view(argv[++i]);
                };
                if (arg == "--jobs") o.jobs = int(number(value()));
                else if (arg == "--levels") o.last_level = int(number(value()));
                else if (arg == "--families") o.families = int(number(value()));
                else if (arg == "--rounds") o.rounds = int(number(value()));
                else if (arg == "--seed") o.seed = number(value());
                else if (arg == "--least") o.least = int(number(value()));
                else if (arg == "--patience") o.patience = int(number(value()));
                else if (arg == "--set") apply(value());
                else if (arg == "--quick") { o.pool = 24; o.keep = 8; o.checked = 4; o.needed = 2; o.least = 2; o.patience = 2; o.rounds = 6; o.families = 2; o.screened = 96; }
                else if (arg == "--kinds") {
                    o.kinds.clear();
                    std::string_view list = value();
                    while (!list.empty()) {
                        const auto comma = list.find(',');
                        o.kinds.push_back(kind_named(list.substr(0, comma)));
                        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
                    }
                } else throw std::runtime_error("Unknown option " + std::string(arg) + "\n" + usage);
            }
            if (o.jobs < 1 || o.jobs > 256 || o.last_level < 1 || o.last_level > level_count || o.families < 1 || o.families > 16 ||
                o.rounds < 1 || o.kinds.empty() || o.seed > 4000) {
                throw std::runtime_error(usage);
            }
            n.train();
        } else {
            throw std::runtime_error(usage);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
