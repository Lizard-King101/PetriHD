#include "lesson_one.hpp"
#include "file.hpp"
#include "fnv.hpp"
#include "nursery_film.hpp"
#include "pond_trials.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
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
using namespace phd;
using brain::Brain;
namespace fs = std::filesystem;

std::string fingerprint(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

std::string brain_id(const Brain& b) {
    Fnv1a hash;
    for (auto w : b.weight) hash.add(w);
    return fingerprint(hash.value());
}

constexpr int panel_size = 4, keep = 4, fed_goal = 25;
constexpr unsigned calibration = 7301, training = 7311, validation_a = 7321, validation_b = 7331;

World Preset::start(unsigned seed, unsigned candidate, const Brain& mind, const Brain& reference) const {
    WorldConfig c;
    c.layout = Layout::training_pond; c.seed = seed; c.width = width; c.height = height;
    c.ocean_percent = 0; c.climate = temperature_c10; c.seasons = 0;
    c.pond_depth_dm = std::uint16_t(depth_dm); c.pond_light = std::uint8_t(light);
    c.pond_food = std::uint16_t(food_per_cell);
    c.pond_refill_ticks = unsigned(refill_ticks);
    World w(c, patches);
    w.seed_training_pair(candidate == 1 ? mind : reference, candidate == 2 ? mind : reference,
                         start_energy_percent, gap_cells);
    return w;
}

struct Field { const char* name; int Preset::*member; int minimum, maximum; };
constexpr Field fields[] = {
    {"width", &Preset::width, 8, 128}, {"height", &Preset::height, 8, 128},
    {"depth_dm", &Preset::depth_dm, 10, 1000}, {"temperature_c10", &Preset::temperature_c10, 0, 350},
    {"light", &Preset::light, 0, 255}, {"food_per_cell", &Preset::food_per_cell, 330, 65535},
    {"start_energy_percent", &Preset::start_energy_percent, 1, 100}, {"gap_cells", &Preset::gap_cells, 1, 124},
    {"tick_limit", &Preset::tick_limit, 1, std::numeric_limits<int>::max()},
    {"refill_ticks", &Preset::refill_ticks, 1, std::numeric_limits<int>::max()}
};

Preset read_preset(const fs::path& path) {
    Preset result;
    if (path.empty()) return result;
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Could not open pond preset " + path.string());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto split = line.find('=');
        if (line.substr(0, split) == "food_patch" && split != std::string::npos) {
            std::istringstream coords(line.substr(split + 1));
            PondPatch bed{};
            char a, b, c;
            if (!(coords >> bed.x >> a >> bed.y >> b >> bed.width >> c >> bed.height) ||
                a != ',' || b != ',' || c != ',' || !(coords >> std::ws).eof())
                throw std::runtime_error("Expected food_patch=x,y,width,height in " + path.string());
            result.patches.push_back(bed);
            continue;
        }
        const auto at = std::find_if(std::begin(fields), std::end(fields), [&](const Field& f) { return line.substr(0, split) == f.name; });
        if (split == std::string::npos || at == std::end(fields)) throw std::runtime_error("Unknown pond setting " + line);
        int value = 0;
        const auto end = line.data() + line.size();
        const auto parsed = std::from_chars(line.data() + split + 1, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != end || value < at->minimum || value > at->maximum)
            throw std::runtime_error("Invalid pond setting " + line);
        result.*(at->member) = value;
    }
    if (result.gap_cells > result.width - 4) throw std::runtime_error("The starting gap does not fit in the pond");
    return result;
}

void write_text(const fs::path& path, const std::string& text) {
    if (!write_file_atomic(path.string(), {text.begin(), text.end()})) throw std::runtime_error("Could not write " + path.string());
}

void write_donor(const fs::path& path, const World& donor) {
    if (!write_file_atomic(path.string(), donor.save())) throw std::runtime_error("Could not save " + path.string());
}

void write_preset(const fs::path& path, const Preset& preset) {
    std::ostringstream settings;
    if (preset.refill_ticks > 1) settings << "# Breeding pond. Still freshwater, fixed light and temperature, periodic reed refills.\n";
    else settings << (preset.patches.empty() ? "# Lesson 1. Still freshwater, fixed light and temperature, reeds refilled every tick.\n"
                                        : "# Lesson 2. Still freshwater, fixed light and temperature, reed beds refilled every tick.\n");
    for (const auto& f : fields) {
        if (f.member == &Preset::refill_ticks && preset.refill_ticks == 1) continue;
        settings << f.name << '=' << preset.*(f.member) << '\n';
    }
    for (const auto& p : preset.patches) settings << "food_patch=" << p.x << ',' << p.y << ',' << p.width << ',' << p.height << '\n';
    write_text(path, settings.str());
}

Brain reference_brain() {
    auto b = brain::founder(true);
    // A dependable partner stays by the food. Its appetite and mate decision still come from the founder network.
    auto* pace = b.weight + brain::hidden * brain::unit_bytes + brain::out_pace * brain::output_bytes;
    std::fill(pace, pace + brain::hidden, std::int8_t(0));
    pace[brain::hidden] = -127;
    return b;
}


Trial trial(const Preset& preset, unsigned seed, unsigned slot, const Brain& b, const Brain& reference, Brain* first_child) {
    World w = preset.start(seed, slot, b, reference);
    Trial t;
    t.seed = seed; t.candidate = slot;
    const auto& partner = w.animals()[2 - slot];
    const auto home = std::find_if(preset.patches.begin(), preset.patches.end(), [&](const PondPatch& bed) {
        return bed.contains(partner.x >> 8, partner.y >> 8);
    });
    while (!t.birth && w.tick() < unsigned(preset.tick_limit)) {
        // A tick's charges belong to the state in which the founders entered it.
        // Reading drive after a birth would see both parents reset to zero.
        std::array<bool, 2> ready{};
        if (w.animals().size() == 2) {
            const auto& left = w.animals()[0];
            const auto& right = w.animals()[1];
            const std::int64_t dx = left.x - right.x, dy = left.y - right.y;
            const bool near = dx * dx + dy * dy < mating_reach * mating_reach;
            ++t.pair_ticks;
            t.near_ticks += near;
            ready = {left.drive >= mating_drive, right.drive >= mating_drive};
            if (ready[0] && ready[1]) {
                ++t.together_ticks;
                t.apart_ticks += !near;
            }
        }
        PondTick pass;
        w.step(&pass);
        t.timing = std::max(t.timing, std::min(mating_drive, pass.near_pair_drive));
        t.feeding = std::min(fed_goal, t.feeding + int(pass.founders[slot - 1].fed_check));
        for (unsigned i = 0; i < 2; ++i) {
            const auto& sample = pass.founders[i];
            t.plant_food[i] += sample.plant_taken;
            t.plant_energy[i] += sample.plant_energy_gained;
            t.entire[i].add(sample);
            if (sample.grazed_cell >= 0 && !preset.patches.empty() &&
                (home == preset.patches.end() || !home->contains(sample.grazed_cell % w.width(), sample.grazed_cell / w.width())))
                ++t.side_bites[i];
            if (!t.ready_tick[i] && sample.reached_ready) t.ready_tick[i] = unsigned(w.tick());
            if (t.first_ready) t.wait[i].add(sample);
            if (ready[i]) {
                if (ready[1 - i]) {
                    t.together[i].add(sample);
                } else {
                    ++t.alone_ticks[i];
                    t.alone[i].add(sample);
                }
            }
        }
        if (!t.first_ready && (pass.founders[0].reached_ready || pass.founders[1].reached_ready)) t.first_ready = unsigned(w.tick());
        for (const auto& a : w.animals()) {
            if (a.id <= 2) t.bites[a.id - 1] += a.eating == 0;
            if ((a.mother == 1 && a.father == 2) || (a.mother == 2 && a.father == 1)) {
                if (first_child && !t.birth) *first_child = w.brain_of(std::size_t(&a - w.animals().data()));
                t.birth = unsigned(w.tick()); t.mother = a.mother; t.father = a.father; ++t.young;
            }
        }
    }
    t.end = unsigned(w.tick());
    int healthy = 0;
    for (const auto& a : w.animals()) if (a.id <= 2) {
        const auto store = frame(a.genome, a.growth).store;
        t.end_reserve_per_mille[a.id - 1] = a.energy * 1000 / store;
        healthy += a.health == 255 && a.energy >= store / 3;
    }
    t.healthy_birth = t.birth && healthy == 2;
    t.state = w.fingerprint();
    return t;
}

namespace {
struct Panel {
    std::array<Trial, panel_size> trials{};
    int births = 0, timing = 0, feeding = 0;
    auto rank() const { return std::tie(births, timing, feeding); }
};

Panel evaluate(const Preset& preset, unsigned first_seed, const Brain& b, const Brain& reference) {
    Panel panel;
    for (unsigned i = 0; i < panel.trials.size(); ++i) {
        auto& t = panel.trials[i];
        t = trial(preset, first_seed + i, 1 + i % 2, b, reference);
        panel.births += t.birth != 0; panel.timing += t.timing; panel.feeding += t.feeding;
    }
    return panel;
}

enum class TrialLog { scores, waiting, foraging };

void log_header(std::ostream& out, TrialLog detail = TrialLog::scores) {
    out << "panel,brain,reference,seed,candidate_id,end_tick,birth_tick,young,mother,father,near_drive,fed_checks,first_ready_tick,"
           "candidate_bites,reference_bites,candidate_wait_move,reference_wait_move,candidate_wait_call,reference_wait_call,fingerprint";
    if (detail != TrialLog::scores) out << ",candidate_ready_tick,reference_ready_tick,candidate_only_ready_ticks,reference_only_ready_ticks,"
                       "both_ready_ticks,both_ready_out_of_reach_ticks,candidate_wait_upkeep,reference_wait_upkeep,"
                       "candidate_wait_strike,reference_wait_strike,candidate_only_ready_active,reference_only_ready_active,"
                       "candidate_only_ready_energy,reference_only_ready_energy,candidate_both_ready_energy,reference_both_ready_energy";
    if (detail == TrialLog::foraging) out << ",pool_slot,pair_ticks,in_range_ticks,candidate_trial_energy,reference_trial_energy,"
                                             "candidate_side_bites,reference_side_bites";
    out << '\n';
}

void log_trials(std::ostream& out, const char* name, const std::string& id, const std::string& ref, const Panel& p,
                TrialLog detail = TrialLog::scores, unsigned pool_slot = 0) {
    for (const auto& t : p.trials) {
        const auto c = t.candidate - 1, r = 1 - c;
        out << name << ',' << id << ',' << ref << ',' << t.seed << ',' << t.candidate << ',' << t.end << ',' << t.birth
            << ',' << t.young << ',' << t.mother << ',' << t.father << ',' << t.timing << ',' << t.feeding << ',' << t.first_ready
            << ',' << t.bites[c] << ',' << t.bites[r] << ',' << t.wait[c].movement << ',' << t.wait[r].movement
            << ',' << t.wait[c].calls << ',' << t.wait[r].calls << ',' << fingerprint(t.state);
        if (detail != TrialLog::scores) {
            out << ',' << t.ready_tick[c] << ',' << t.ready_tick[r] << ',' << t.alone_ticks[c] << ',' << t.alone_ticks[r]
                << ',' << t.together_ticks << ',' << t.apart_ticks << ',' << t.wait[c].upkeep << ',' << t.wait[r].upkeep
                << ',' << t.wait[c].strikes << ',' << t.wait[r].strikes << ',' << t.alone[c].active() << ',' << t.alone[r].active()
                << ',' << t.alone[c].total() << ',' << t.alone[r].total() << ',' << t.together[c].total() << ',' << t.together[r].total();
        }
        if (detail == TrialLog::foraging) {
            out << ',' << pool_slot << ',' << t.pair_ticks << ',' << t.near_ticks << ',' << t.entire[c].total() << ',' << t.entire[r].total()
                << ',' << t.side_bites[c] << ',' << t.side_bites[r];
        }
        out << '\n';
    }
}

void record_pond_panel(Film& film, const Preset& preset, const Brain& mind, const Brain& reference, const Panel& panel,
                       const char* name, int rank, const char* result) {
    const auto id = brain_id(mind);
    for (const auto& t : panel.trials) {
        auto w = preset.start(t.seed, t.candidate, mind, reference);
        film.add_pond(w, id, t.candidate, name, rank, result, t.end, t.state);
    }
}

void summarize_foraging(std::ostream& out, unsigned slot, const std::string& id, const char* food, const Panel& a, const Panel& b) {
    std::int64_t pair_ticks = 0, near = 0, ready = 0, ready_near = 0, birth_tick = 0, wait_to_birth = 0;
    std::int64_t wait_energy_at_birth = 0, entire_energy = 0, candidate_bites = 0, side_bites = 0, reference_bites = 0, reference_side = 0;
    for (const Panel* panel : {&a, &b}) for (const auto& t : panel->trials) {
        const auto c = t.candidate - 1, r = 1 - c;
        pair_ticks += t.pair_ticks; near += t.near_ticks;
        ready += t.together_ticks; ready_near += t.together_ticks - t.apart_ticks;
        candidate_bites += t.bites[c]; side_bites += t.side_bites[c];
        reference_bites += t.bites[r]; reference_side += t.side_bites[r];
        entire_energy += t.entire[c].total() + t.entire[r].total();
        if (t.birth) {
            birth_tick += t.birth; wait_to_birth += t.birth - t.first_ready;
            wait_energy_at_birth += t.wait[c].total() + t.wait[r].total();
        }
    }
    const int births = a.births + b.births;
    out << slot << ',' << id << ',' << food << ',' << a.births << ',' << b.births
        << ',' << pair_ticks << ',' << near << ',' << ready << ',' << ready_near;
    const auto ratio = [&](std::int64_t numerator, std::int64_t denominator) {
        out << ',';
        if (denominator) out << double(numerator) / double(denominator);
    };
    ratio(100 * near, pair_ticks); ratio(100 * ready_near, ready);
    ratio(birth_tick, births); ratio(wait_to_birth, births); ratio(wait_energy_at_birth, births); ratio(entire_energy, births);
    out << ',' << candidate_bites << ',' << side_bites << ',' << reference_bites << ',' << reference_side << '\n';
}
} // namespace

void validate_lesson_one(const Brain& mind, const fs::path& preset_path, std::ostream& out) {
    const auto preset = read_preset(preset_path);
    const auto reference = reference_brain();
    const auto id = brain_id(mind), ref_id = brain_id(reference);
    log_header(out, TrialLog::waiting);
    log_trials(out, "validation-a", id, ref_id, evaluate(preset, validation_a, mind, reference), TrialLog::waiting);
    log_trials(out, "validation-b", id, ref_id, evaluate(preset, validation_b, mind, reference), TrialLog::waiting);
}

void lesson_one(const fs::path& dir, const std::vector<Brain>& pool, int workers, bool visual, const fs::path& preset_path) {
    const auto preset = read_preset(preset_path);
    fs::create_directories(dir);
    write_preset(dir / "pond.cfg", preset);

    const Brain reference = reference_brain();
    const auto ref_id = brain_id(reference);
    const auto check = evaluate(preset, calibration, reference, reference);
    std::ostringstream trials;
    log_header(trials);
    log_trials(trials, "calibration", ref_id, ref_id, check);
    std::printf("reference=%s calibration=%d/4\n", ref_id.c_str(), check.births);
    if (check.births < 3) {
        write_text(dir / "trials.csv", trials.str());
        throw std::runtime_error("Reference pair did not qualify on the separate calibration panel");
    }
    struct Record { Panel train, a, b; bool retained = false, promoted = false; };
    std::vector<Record> results(pool.size());
    std::vector<std::future<void>> jobs;
    for (int j = 0; j < workers; ++j) jobs.push_back(std::async(std::launch::async, [&, j] {
        for (std::size_t i = j; i < pool.size(); i += workers) results[i].train = evaluate(preset, training, pool[i], reference);
    }));
    for (auto& job : jobs) job.get();
    std::vector<std::size_t> order(pool.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return results[a].train.rank() > results[b].train.rank(); });

    std::set<std::string> retained;
    std::ostringstream ranks;
    ranks << "rank,pool_slot,brain,training_births,mean_timing,mean_feeding,validation_a_births,validation_b_births,status\n";
    ranks << std::fixed << std::setprecision(4);
    std::optional<Film> film;
    if (visual) film.emplace(dir, 1);
    for (std::size_t j = 0; j < order.size(); ++j) {
        const auto i = order[j];
        const auto& mind = pool[i];
        const auto id = brain_id(mind);
        auto& r = results[i];
        r.retained = retained.size() < keep && retained.insert(id).second;
        log_trials(trials, "training", id, ref_id, r.train);
        if (r.retained) {
            r.a = evaluate(preset, validation_a, mind, reference);
            r.b = evaluate(preset, validation_b, mind, reference);
            r.promoted = r.a.births >= 3 && r.b.births >= 3;
            log_trials(trials, "validation-a", id, ref_id, r.a);
            log_trials(trials, "validation-b", id, ref_id, r.b);
            write_donor(dir / ("retained-" + std::to_string(retained.size()) + ".phdw"), preset.start(training, 1, mind, reference));
        }
        const char* status = r.promoted ? "promoted" : r.retained ? "retained" : "not retained";
        ranks << j + 1 << ',' << i + 1 << ',' << id << ',' << r.train.births << ',' << double(r.train.timing) / (panel_size * mating_drive)
              << ',' << double(r.train.feeding) / (panel_size * fed_goal) << ',';
        if (r.retained) ranks << r.a.births;
        ranks << ',';
        if (r.retained) ranks << r.b.births;
        ranks << ',' << status << '\n';
        std::printf("rank=%zu brain=%s training=%d/4", j + 1, id.c_str(), r.train.births);
        if (r.retained) std::printf(" validation=%d/4,%d/4", r.a.births, r.b.births);
        std::printf(" %s\n", status);
        if (film) {
            record_pond_panel(*film, preset, mind, reference, r.train, "Training", int(j + 1), status);
            if (r.retained) {
                record_pond_panel(*film, preset, mind, reference, r.a, "Validation A", int(j + 1), status);
                record_pond_panel(*film, preset, mind, reference, r.b, "Validation B", int(j + 1), status);
            }
            if (!j) film->publish(false);
        }
    }
    write_text(dir / "trials.csv", trials.str());
    write_text(dir / "rankings.csv", ranks.str());
    if (film) film->publish(true);
}

void lesson_two(const fs::path& dir, const fs::path& uniform_path, const fs::path& patch_path,
                const std::vector<std::pair<unsigned, Brain>>& swimmers, bool visual) {
    const auto uniform = read_preset(uniform_path), patches = read_preset(patch_path);
    if (!uniform.patches.empty() || patches.patches.empty()) throw std::runtime_error("Expected a uniform control and a pond with reed beds");
    for (const auto& f : fields) {
        if (f.member != &Preset::food_per_cell && uniform.*(f.member) != patches.*(f.member))
            throw std::runtime_error(std::string("Pond presets differ in ") + f.name);
    }
    const auto reference = reference_brain();
    const auto ref = brain_id(reference);
    const auto check = evaluate(patches, calibration, reference, reference);
    fs::create_directories(dir);
    write_preset(dir / "pond.cfg", patches);
    write_preset(dir / "uniform.cfg", uniform);
    std::ostringstream controls, trials, calibration_log, summary;
    log_header(controls, TrialLog::foraging); log_header(trials, TrialLog::foraging);
    log_header(calibration_log, TrialLog::foraging);
    log_trials(calibration_log, "calibration", ref, ref, check, TrialLog::foraging);
    summary << std::fixed << std::setprecision(3)
            << "pool_slot,brain,food_layout,births_a,births_b,pair_ticks,in_range_ticks,both_ready_ticks,both_ready_in_range_ticks,"
               "in_range_pct,both_ready_in_range_pct,mean_birth_tick,mean_first_ready_to_birth_ticks,mean_wait_energy_at_birth,"
               "whole_pair_energy_per_birth,candidate_bites,candidate_side_bites,reference_bites,reference_side_bites\n";
    std::optional<Film> film;
    if (visual) film.emplace(dir, 2);
    std::printf("patch reference=%s calibration=%d/4\n", ref.c_str(), check.births);
    for (const auto& [slot, mind] : swimmers) {
        const auto id = brain_id(mind);
        write_donor(dir / ("slot-" + std::to_string(slot) + ".phdw"), patches.start(validation_a, 1, mind, reference));
        for (const Preset* preset : {&uniform, &patches}) {
            const auto a = evaluate(*preset, validation_a, mind, reference);
            const auto b = evaluate(*preset, validation_b, mind, reference);
            const bool patched = !preset->patches.empty();
            auto& log = patched ? trials : controls;
            log_trials(log, "validation-a", id, ref, a, TrialLog::foraging, slot);
            log_trials(log, "validation-b", id, ref, b, TrialLog::foraging, slot);
            summarize_foraging(summary, slot, id, patched ? "patches" : "uniform", a, b);
            std::printf("slot=%u brain=%s food=%s births=%d/4,%d/4\n", slot, id.c_str(), patched ? "patches" : "uniform", a.births, b.births);
            if (film && patched) {
                const auto result = std::to_string(a.births + b.births) + "/8 births";
                record_pond_panel(*film, *preset, mind, reference, a, "Validation A", slot, result.c_str());
                record_pond_panel(*film, *preset, mind, reference, b, "Validation B", slot, result.c_str());
            }
        }
    }
    write_text(dir / "calibration.csv", calibration_log.str());
    write_text(dir / "controls.csv", controls.str()); write_text(dir / "trials.csv", trials.str());
    write_text(dir / "summary.csv", summary.str());
    if (film) film->publish(true);
}
} // namespace nursery
