#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "biome.hpp"
#include "random.hpp"
#include "terrain.hpp"

namespace phd {

struct WorldConfig {
    std::uint32_t seed = 1;
    int width = 384;
    int height = 224;
    int ocean_percent = 20;
    // The valley's climate: mean temperature over the year and the swing
    // between summer and winter, in tenths of a degree C.
    int climate = 90;
    int seasons = 140;
};

enum class Water : std::uint8_t { none, ocean, lake, river };

// A storm front: a moving disc of cloud that carries water from the sea and
// drops it as it goes, more where it climbs.
struct Front {
    std::int32_t x, y;   // 1/256 cell
    std::int16_t vx, vy; // 1/256 cell per tick
    std::uint16_t radius;// cells
    std::uint16_t life;  // ticks left
    std::int32_t load;   // water carried
    std::int16_t last_height;
};

struct Lightning {
    std::uint64_t tick;
    std::int16_t x, y;
};

struct LakeState {
    std::uint16_t level;  // 0..1024 of the way from bottom to spill
    std::uint32_t inflow; // since the last hydrology pass
};

// The world of milestone 1: land and water, climate, day and night, seasons
// and weather. Everything is allocated at creation, never inside a tick.
class World {
public:
    static constexpr int ticks_per_day = 240;
    static constexpr int days_per_year = 16;
    static constexpr int ticks_per_year = ticks_per_day * days_per_year;
    static constexpr int node = 8; // cells per weather node
    static constexpr int max_fronts = 6;
    static constexpr int lightning_kept = 16;

    explicit World(const WorldConfig& config);

    void step();

    std::uint64_t fingerprint() const;
    std::uint64_t tick() const { return tick_; }
    std::uint32_t seed() const { return config_.seed; }
    const WorldConfig& config() const { return config_; }
    int width() const { return config_.width; }
    int height() const { return config_.height; }
    std::size_t cells() const { return std::size_t(config_.width) * config_.height; }
    std::size_t memory_bytes() const;

    // Time. Angles are binary (256 a turn); the year starts at the spring equinox.
    int season_angle() const { return int((tick_ % ticks_per_year) * 256 / ticks_per_year); }
    int season() const { return int((tick_ % ticks_per_year) / (ticks_per_year / 4)); } // 0 spring .. 3 winter
    int day_angle() const { return int((tick_ % ticks_per_day) * 256 / ticks_per_day); } // 0 midnight
    int daylight() const; // 0 night .. 255 noon

    // The static land.
    const Terrain& terrain() const { return terrain_; }
    int elevation(std::size_t i) const { return terrain_.elevation[i]; } // decimetres
    Substrate substrate(std::size_t i) const { return Substrate(terrain_.substrate[i]); }

    // The climate and water, per cell.
    Water water(std::size_t i) const { return Water(water_[i]); }
    int water_depth_dm(std::size_t i) const { return depth_[i]; } // decimetres
    int temperature(std::size_t i) const { return temperature_[i]; } // tenths of a degree C
    int soil_water(std::size_t i) const { return soil_[i]; }         // 0..255
    int moisture(std::size_t i) const { return moisture_[i] >> 8; }  // long-run soil water
    int snow(std::size_t i) const { return snow_[i]; }
    int nutrients(std::size_t i) const { return nutrients_[i]; }
    std::uint32_t flow(std::size_t i) const { return flow_[i]; }
    Biome biome(std::size_t i) const { return Biome(biome_[i]); }
    const std::uint8_t* biomes() const { return biome_.data(); }

    // Weather, on a coarse grid of nodes every 8 cells.
    int nodes_x() const { return nodes_x_; }
    int nodes_y() const { return nodes_y_; }
    int rain(int nx, int ny) const { return rain_[std::size_t(ny * nodes_x_ + nx)]; }
    int cloud(int nx, int ny) const { return cloud_[std::size_t(ny * nodes_x_ + nx)]; }
    int wind_angle() const { return wind_angle_; }
    int wind_speed() const { return wind_speed_; } // 1/256 cell per tick
    const std::vector<Front>& fronts() const { return fronts_; }
    const std::array<Lightning, lightning_kept>& lightning() const { return lightning_; }
    const std::vector<LakeState>& lakes() const { return lakes_; }

    // A cell's temperature over the year, without season or day: the valley's
    // climate, a little colder up high and in hollows, warmer on slopes
    // facing the sun. What the biome follows.
    int mean_temperature(std::size_t i) const;

    std::vector<std::uint8_t> save() const;
    // Returns false and leaves the world unchanged if the data isn't a valid save.
    bool load(const std::uint8_t* data, std::size_t size);

private:
    int temperature_at(std::size_t i, bool water) const;
    int rain_at(int x, int y) const;
    void update_wind();
    void update_fronts();
    void update_soil(int row);
    void update_biomes(int row);
    void update_hydrology();
    void refresh_water();
    Biome classify(int x, int y) const;

    WorldConfig config_;
    Terrain terrain_;
    int nodes_x_, nodes_y_;
    std::uint64_t tick_ = 0;
    Random weather_rng_;
    int wind_angle_ = 0, wind_speed_ = 40;

    std::vector<std::uint8_t> water_;
    std::vector<std::uint16_t> depth_;
    std::vector<std::int16_t> temperature_;
    std::vector<std::uint8_t> soil_;
    std::vector<std::uint16_t> moisture_; // 8.8 fixed point
    std::vector<std::uint8_t> snow_;
    std::vector<std::uint8_t> nutrients_;
    std::vector<std::uint32_t> runoff_;
    std::vector<std::uint32_t> flow_;
    std::vector<std::uint8_t> biome_;
    std::vector<LakeState> lakes_; // parallel to terrain_.basins
    std::vector<Front> fronts_;
    std::array<Lightning, lightning_kept> lightning_{};
    std::size_t lightning_next_ = 0;

    // Scratch, rebuilt every tick or pass; not saved.
    std::vector<std::uint8_t> rain_, cloud_;
    std::vector<std::uint32_t> accumulated_;
    std::vector<std::uint32_t> lake_area_;
    std::vector<std::uint8_t> previous_water_;
};

} // namespace phd
