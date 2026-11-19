#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "random.hpp"

namespace phd {

struct WorldConfig {
    std::uint32_t seed = 1;
    int width = 384;
    int height = 224;
};

// Milestone 0's world: an environment grid with one placeholder field (soil
// nutrients) that drifts and spreads, enough to exercise determinism, the
// fingerprint, saves and the renderer. Everything is allocated here, never
// inside a tick.
class World {
public:
    explicit World(const WorldConfig& config);

    void step();

    std::uint64_t fingerprint() const;
    std::uint64_t tick() const { return tick_; }
    std::uint32_t seed() const { return seed_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::uint8_t* nutrients() const { return nutrients_.data(); }
    std::size_t memory_bytes() const;

    // Day and season, from the plan: a day is 240 ticks, a year 16 days.
    static constexpr int ticks_per_day = 240;
    static constexpr int days_per_year = 16;

    std::vector<std::uint8_t> save() const;
    // Returns false and leaves the world unchanged if the data isn't a valid save.
    bool load(const std::uint8_t* data, std::size_t size);

private:
    int at(int x, int y) const;

    std::uint32_t seed_;
    int width_;
    int height_;
    std::uint64_t tick_ = 0;
    Random weather_rng_;
    std::vector<std::uint8_t> nutrients_;
};

} // namespace phd
