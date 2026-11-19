#include "world.hpp"

#include <algorithm>
#include <cstring>

#include "fnv.hpp"

namespace phd {
namespace {

constexpr std::uint32_t purpose_weather = 1;
constexpr std::uint32_t purpose_terrain = 2;
constexpr std::uint32_t purpose_drift = 3;

// The environment updates one row in 16 per tick, as in Petri's staggered updates.
constexpr int stagger = 16;

constexpr char save_magic[4] = {'P', 'H', 'D', 'W'};
constexpr std::uint32_t save_version = 1;

template <class T> void put(std::vector<std::uint8_t>& out, T value) {
    for (unsigned byte = 0; byte < sizeof(T); ++byte) {
        out.push_back(static_cast<std::uint8_t>(std::uint64_t(value) >> (8 * byte)));
    }
}

template <class T> T get(const std::uint8_t*& at) {
    std::uint64_t value = 0;
    for (unsigned byte = 0; byte < sizeof(T); ++byte) {
        value |= std::uint64_t(*at++) << (8 * byte);
    }
    return static_cast<T>(value);
}

std::uint64_t checksum(const std::uint8_t* data, std::size_t size) {
    Fnv1a hash;
    hash.bytes(data, size);
    return hash.value();
}

} // namespace

World::World(const WorldConfig& config)
    : seed_(config.seed),
      width_(config.width),
      height_(config.height),
      weather_rng_(stream_seed(config.seed, purpose_weather)),
      nutrients_(std::size_t(config.width) * std::size_t(config.height)) {
    // Value noise on a 16-cell lattice, from the seed.
    const auto salt = stream_seed(seed_, purpose_terrain);
    constexpr int cell = 16;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int gx = x / cell, gy = y / cell, fx = x % cell, fy = y % cell;
            const auto corner = [&](int dx, int dy) { return int(hash3(gx + dx, gy + dy, salt) & 255); };
            const int top = corner(0, 0) * (cell - fx) + corner(1, 0) * fx;
            const int bottom = corner(0, 1) * (cell - fx) + corner(1, 1) * fx;
            nutrients_[std::size_t(y) * width_ + x] =
                static_cast<std::uint8_t>((top * (cell - fy) + bottom * fy) / (cell * cell));
        }
    }
}

int World::at(int x, int y) const {
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);
    return nutrients_[std::size_t(y) * width_ + x];
}

void World::step() {
    ++tick_;
    const auto drift = stream_seed(seed_, purpose_drift) ^ std::uint32_t(tick_);
    for (int y = int(tick_ % stagger); y < height_; y += stagger) {
        for (int x = 0; x < width_; ++x) {
            const int around = (at(x - 1, y) + at(x + 1, y) + at(x, y - 1) + at(x, y + 1) + 2) / 4;
            int value = (at(x, y) * 3 + around + 2) / 4;
            value += int(hash3(x, y, drift) % 3) - 1;
            value -= value >> 7;
            nutrients_[std::size_t(y) * width_ + x] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        }
    }
    // A passing shower now and then.
    if (tick_ % 8 == 0) {
        const int cx = int(weather_rng_.below(std::uint32_t(width_)));
        const int cy = int(weather_rng_.below(std::uint32_t(height_)));
        constexpr int radius = 6;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int d2 = dx * dx + dy * dy, x = cx + dx, y = cy + dy;
                if (d2 > radius * radius || x < 0 || y < 0 || x >= width_ || y >= height_) {
                    continue;
                }
                auto& cell = nutrients_[std::size_t(y) * width_ + x];
                cell = static_cast<std::uint8_t>(std::min(255, cell + 48 * (radius * radius - d2) / (radius * radius)));
            }
        }
    }
}

std::uint64_t World::fingerprint() const {
    Fnv1a hash;
    hash.add(seed_);
    hash.add(std::uint32_t(width_));
    hash.add(std::uint32_t(height_));
    hash.add(tick_);
    hash.add(weather_rng_.state());
    hash.bytes(nutrients_.data(), nutrients_.size());
    return hash.value();
}

std::size_t World::memory_bytes() const { return sizeof(*this) + nutrients_.capacity(); }

// Layout, little-endian: magic, version, seed, width, height, tick, weather
// state, the nutrient grid, then the FNV-1a checksum of everything before it.
std::vector<std::uint8_t> World::save() const {
    std::vector<std::uint8_t> out(save_magic, save_magic + 4);
    put(out, save_version);
    put(out, seed_);
    put(out, std::uint32_t(width_));
    put(out, std::uint32_t(height_));
    put(out, tick_);
    put(out, weather_rng_.state());
    out.insert(out.end(), nutrients_.begin(), nutrients_.end());
    put(out, checksum(out.data(), out.size()));
    return out;
}

bool World::load(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t header = 4 + 4 * 4 + 8 + 4;
    if (size < header + 8 || std::memcmp(data, save_magic, 4) != 0) {
        return false;
    }
    const auto* at = data + size - 8;
    if (get<std::uint64_t>(at) != checksum(data, size - 8)) {
        return false;
    }
    at = data + 4;
    if (get<std::uint32_t>(at) != save_version) {
        return false;
    }
    const auto seed = get<std::uint32_t>(at);
    const auto width = get<std::uint32_t>(at);
    const auto height = get<std::uint32_t>(at);
    if (width != std::uint32_t(width_) || height != std::uint32_t(height_) ||
        size != header + std::size_t(width) * height + 8) {
        return false;
    }
    seed_ = seed;
    tick_ = get<std::uint64_t>(at);
    weather_rng_.set_state(get<std::uint32_t>(at));
    std::memcpy(nutrients_.data(), at, nutrients_.size());
    return true;
}

} // namespace phd
