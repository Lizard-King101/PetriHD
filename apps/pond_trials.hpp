#pragma once

#include "world.hpp"
#include <filesystem>
#include <string>

namespace nursery {
using phd::PondPatch;

struct Preset {
    int width = 18, height = 10, depth_dm = 20, temperature_c10 = 180, light = 200, food_per_cell = 600;
    int start_energy_percent = 60, gap_cells = 1, tick_limit = 960, refill_ticks = 1;
    std::vector<PondPatch> patches;

    phd::World start(unsigned seed, unsigned candidate, const phd::brain::Brain& mind, const phd::brain::Brain& mate) const;
};

struct Expenditure {
    std::int64_t movement = 0, calls = 0, upkeep = 0, strikes = 0;

    void add(const phd::PondTick::Founder& sample) {
        movement += sample.movement_cost; calls += sample.call_cost;
        upkeep += sample.upkeep_cost; strikes += sample.strike_cost;
    }
    std::int64_t active() const { return movement + calls + strikes; }
    std::int64_t total() const { return active() + upkeep; }
};

struct Trial {
    unsigned seed = 0, candidate = 0, end = 0, first_ready = 0, birth = 0, mother = 0, father = 0;
    int timing = 0, feeding = 0, young = 0;
    std::array<int, 2> bites{};
    std::array<std::int64_t, 2> plant_food{}, plant_energy{};
    std::array<int, 2> end_reserve_per_mille{};
    bool healthy_birth = false;
    std::array<unsigned, 2> ready_tick{}, alone_ticks{};
    unsigned pair_ticks = 0, near_ticks = 0, together_ticks = 0, apart_ticks = 0;
    std::array<unsigned, 2> side_bites{};
    std::array<Expenditure, 2> entire{}, wait{}, alone{}, together{};
    std::uint64_t state = 0;
};

Preset read_preset(const std::filesystem::path& path);
void write_preset(const std::filesystem::path& path, const Preset& preset);
void write_text(const std::filesystem::path& path, const std::string& text);
void write_donor(const std::filesystem::path& path, const phd::World& donor);
Trial trial(const Preset& preset, unsigned seed, unsigned slot, const phd::brain::Brain& mind,
            const phd::brain::Brain& mate, phd::brain::Brain* first_child = nullptr);

} // namespace nursery
