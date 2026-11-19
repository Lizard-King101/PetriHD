#pragma once

#include <cstdint>

namespace phd {

// Bit mixer from Petri dc03377, terrain_edge_hash (render/terrain.hpp).
constexpr std::uint32_t mix(std::uint32_t value) {
    value = (value ^ (value >> 16)) * 0x7feb352du;
    return value ^ (value >> 15);
}

// A coordinate hash: the same cell always gives the same value, in any order.
// From Petri dc03377, terrain_edge_hash (render/terrain.hpp).
constexpr std::uint32_t hash2(int x, int y) {
    return mix(std::uint32_t(x) * 0x9e3779b9u ^ std::uint32_t(y) * 0x85ebca6bu ^ 0xc2b2ae35u);
}

// A coordinate hash with a third key, such as a tick or a seed.
constexpr std::uint32_t hash3(int x, int y, std::uint32_t z) {
    return mix(hash2(x, y) ^ z * 0x27d4eb2du);
}

// One generator per purpose: each gets its own stream from the world seed,
// so adding draws to one system never shifts another.
constexpr std::uint32_t stream_seed(std::uint32_t seed, std::uint32_t purpose) {
    return mix(seed ^ mix(purpose * 0x9e3779b9u + 1u));
}

// The small integer generator from Petri dc03377, World::random (core/world.cpp).
// Every 32-bit seed is valid, including zero.
class Random {
public:
    explicit Random(std::uint32_t seed = 0) : state_(seed) {}

    std::uint32_t next() {
        std::uint32_t value = (state_ += 0x9e3779b9u);
        value = (value ^ (value >> 16)) * 0x85ebca6bu;
        value = (value ^ (value >> 13)) * 0xc2b2ae35u;
        return value ^ (value >> 16);
    }

    // A value in [0, n), for n > 0.
    std::uint32_t below(std::uint32_t n) { return std::uint32_t((std::uint64_t(next()) * n) >> 32); }

    std::uint32_t state() const { return state_; }
    void set_state(std::uint32_t state) { state_ = state; }

private:
    std::uint32_t state_;
};

} // namespace phd
