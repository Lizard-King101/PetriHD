// Diagnostic counterfactuals run on a private copy prepared by prepare_hunting_review.py.
// The selective interventions identify causes; they are not production rules.
#define main nursery_original_main
#include "nursery.cpp"
#undef main
#include "probe_hooks.hpp"

namespace {
struct ReviewWatch : Bloodshed {
    long long basal = 0, passes = 0, fed = 0;
    void metabolised(const World&, const Animal& a, const Frame& f) override {
        if (diet_of(a.genome) != Diet::carnivore) return;
        basal += f.upkeep; ++passes; fed += a.energy > f.store / 2;
    }
};
struct Review {
    int living[5] = {};
    ReviewWatch watch;
    probe::Costs costs;
    std::uint64_t fingerprint = 0;
};
Review review(int variant, int map, int years, int body_seed, bool hand) {
    // Interventions apply from founding. In particular, suppressing prey flight
    // changes the entire prey ecology, not just a hunter's immediate chase.
    probe::no_sprint = variant == 1 || variant == 5;
    probe::no_flight = variant == 2;
    probe::reachable_sight = variant == 4 || variant == 5;
    probe::gate_strikes = variant == 6;
    probe::wind_percent = variant == 3 ? 50 : 100;
    probe::costs = {};
    WorldConfig config;
    config.layout = Layout::development; config.seed = map; config.width = 128; config.height = 96;
    World world(config);
    const auto bodies = random_bodies(body_seed, 10, 5);
    const auto& hunters = schooled_hunters();
    std::size_t schooled = 0;
    const auto settle = [&](bool carnivores) {
        for (const auto& body : bodies) {
            const bool hunter = diet_of(body) == Diet::carnivore;
            if (hunter != carnivores) continue;
            world.settle(body, 6, hunter && !hand ? hunters[schooled++ % hunters.size()] : brain::founder(diet_of(body) != Diet::herbivore, true, hunter));
        }
    };
    settle(false);
    Review r;
    for (int year = 0; year < years; ++year) {
        if (year == 8) { settle(true); probe::costs = {}; }
        for (int t = 0; t < World::ticks_per_year; ++t) world.step(&r.watch);
        for (int i = 0; i < 5; ++i) if (year + 1 == 4 * (i + 1)) {
            for (const auto& a : world.animals()) r.living[i] += diet_of(a.genome) == Diet::carnivore;
        }
    }
    r.costs = probe::costs; r.fingerprint = world.fingerprint();
    return r;
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "bodies") {
        std::puts("set,scenario,wet,hunter_speed,hunter_upkeep,prey_types,prey_can_fly");
        for (int base : {101, 201, 301}) {
            const auto cases = scenarios(base, base == 101 ? 10 : 8);
            for (int i = 0; i < int(cases.size()); ++i) {
                const auto& s = cases[std::size_t(i)]; const Frame f = frame(s.hunter);
                int flies = 0; for (const auto& p : s.prey) flies += frame(p).air_speed > 0;
                std::printf("%d,%d,%d,%d,%d,%zu,%d\n", base, i, s.wet, s.wet ? f.water_speed : f.land_speed, f.upkeep, s.prey.size(), flies);
            }
        }
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "arena") {
        const auto cases = scenarios(201, 8);
        const auto& minds = schooled_hunters();
        struct ArenaResult { HuntResult result; probe::Costs costs; };
        std::vector<ArenaResult> results(cases.size() * 3 * minds.size());
        parallel(int(results.size()), 8, [&](int i) {
            const int trial = i % (int(cases.size()) * 3), mind = i / (int(cases.size()) * 3);
            probe::costs = {};
            results[std::size_t(i)].result = hunt_trial(cases[std::size_t(trial) % cases.size()], 170000u + trial, minds[std::size_t(mind)], 30);
            results[std::size_t(i)].costs = probe::costs;
        });
        std::puts("mind,scenario,replicate,wet,meat,plants,family_energy_cost,basal,wind,calls,growth,healing,births,alive,herd");
        for (int i = 0; i < int(results.size()); ++i) {
            const auto& r = results[std::size_t(i)].result; const auto& c = results[std::size_t(i)].costs;
            const int trial = i % (int(cases.size()) * 3);
            std::printf("%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d,%d,%d\n", i / (int(cases.size()) * 3), trial % int(cases.size()), trial / int(cases.size()),
                cases[std::size_t(trial) % cases.size()].wet, r.meat, r.plants, r.need, c.basal, c.wind, c.calls, c.growth, c.healing, r.births, r.alive, r.herd);
        }
        return 0;
    }
    const int maps = argc > 1 ? std::atoi(argv[1]) : 4;
    const int years = argc > 2 ? std::atoi(argv[2]) : 20;
    const int first = argc > 3 ? std::atoi(argv[3]) : 7101;
    const int body_seed = argc > 4 ? std::atoi(argv[4]) : 1;
    const int only = argc > 5 ? std::atoi(argv[5]) : -1;
    constexpr const char* names[] = {"baseline", "hunter_no_sprint", "prey_no_flight", "hunter_half_wind_cost", "hunter_same_medium_sight", "no_sprint_and_same_medium", "hunter_no_empty_strikes", "hand_wired"};
    const int variants = only < 0 ? 8 : 1;
    std::vector<Review> runs(std::size_t(maps * variants));
    parallel(int(runs.size()), std::min(8, maps * variants), [&](int i) {
        const int v = only < 0 ? i / maps : only;
        runs[std::size_t(i)] = review(v, first + i % maps, years, body_seed, v == 7);
        std::fprintf(stderr, "%s map %d complete\n", names[v], first + i % maps);
    });
    std::puts("variant,map,body_seed,carn_y4,carn_y8,carn_y12,carn_y16,carn_y20,born,starved,suffocated,killed,kills,lunges,hits,meat,plants,basal,wind,calls,growth,healing,metabolic_passes,fed_passes,animal_ticks,sprint_requested_ticks,low_stamina_ticks,fingerprint");
    for (int i = 0; i < int(runs.size()); ++i) {
        const int v = only < 0 ? i / maps : only;
        const auto& r = runs[std::size_t(i)]; const auto& w = r.watch; const auto& c = r.costs;
        std::printf("%s,%d,%d", names[v], first + i % maps, body_seed);
        for (int n : r.living) std::printf(",%d", n);
        std::printf(",%d,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%llx\n", w.born_carn,
            w.fate[0][0]+w.fate[0][1], w.fate[1][0]+w.fate[1][1], w.fate[2][0]+w.fate[2][1], w.kills_by[2],
            w.lunges[2], w.hit_grown[2]+w.hit_young[2], w.meat_by[2], w.plants_by[2], c.basal, c.wind, c.calls, c.growth, c.healing,
            w.passes, w.fed, c.ticks, c.sprint, c.spent, (unsigned long long)r.fingerprint);
    }
}
