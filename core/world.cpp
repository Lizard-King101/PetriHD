#include "world.hpp"

#include <algorithm>
#include <cstring>

#include "fnv.hpp"
#include "math.hpp"

namespace phd {
namespace {

constexpr std::uint32_t purpose_weather = 1;
constexpr std::uint32_t purpose_soil = 2;

// Rows of the environment update in turn: soil every 4 ticks, biomes every 16.
constexpr int soil_stagger = 4;
constexpr int biome_stagger = 16;
constexpr int hydrology_every = 8;

// How much water each ground holds before the rest runs off.
constexpr int soil_capacity[int(Substrate::count)] = {230, 150, 245, 255, 90, 120}; // loam, sand, clay, peat, rock, gravel
constexpr std::uint32_t river_min_flow = 3000;
constexpr int lake_start_level = 700;

constexpr char save_magic[4] = {'P', 'H', 'D', 'W'};
constexpr std::uint32_t save_version = 3;

// Little-endian serialisation of integers and arrays of them.
struct Writer {
    std::vector<std::uint8_t> out;
    template <class T> void put(T value) {
        for (unsigned byte = 0; byte < sizeof(T); ++byte) {
            out.push_back(static_cast<std::uint8_t>(std::uint64_t(value) >> (8 * byte)));
        }
    }
    template <class T> void array(const std::vector<T>& values) {
        for (const auto v : values) {
            put(v);
        }
    }
};

struct Reader {
    const std::uint8_t* at;
    const std::uint8_t* end;
    bool ok = true;
    template <class T> T get() {
        if (std::size_t(end - at) < sizeof(T)) {
            ok = false;
            return T{};
        }
        std::uint64_t value = 0;
        for (unsigned byte = 0; byte < sizeof(T); ++byte) {
            value |= std::uint64_t(*at++) << (8 * byte);
        }
        return static_cast<T>(value);
    }
    template <class T> void array(std::vector<T>& values) {
        for (auto& v : values) {
            v = get<T>();
        }
    }
};

std::uint64_t checksum(const std::uint8_t* data, std::size_t size) {
    Fnv1a hash;
    hash.bytes(data, size);
    return hash.value();
}

int signed_angle(int a) { return ((a + 128) & 255) - 128; }

} // namespace

World::World(const WorldConfig& config)
    : config_(config),
      nodes_x_((config.width + node - 1) / node),
      nodes_y_((config.height + node - 1) / node),
      weather_rng_(stream_seed(config.seed, purpose_weather)) {
    terrain_.generate(config.seed, config.width, config.height, config.ocean_percent);
    const auto n = cells();
    water_.assign(n, 0);
    depth_.assign(n, 0);
    temperature_.assign(n, 0);
    soil_.assign(n, 0);
    moisture_.assign(n, 0);
    snow_.assign(n, 0);
    nutrients_.assign(n, 0);
    runoff_.assign(n, 0);
    flow_.assign(n, 0);
    accumulated_.assign(n, 0);
    biome_.assign(n, 0);
    lakes_.assign(terrain_.basins.size(), LakeState{lake_start_level, 0});
    lake_area_.assign(lakes_.size(), 0);
    fronts_.reserve(max_fronts);
    rain_.assign(std::size_t(nodes_x_) * nodes_y_, 0);
    cloud_.assign(rain_.size(), 0);

    // A first guess at soil water from the distance to the sea; the weather
    // takes over from here.
    const auto soil_salt = stream_seed(config.seed, purpose_soil);
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            const auto i = std::size_t(y) * config.width + x;
            const int guess = std::clamp(200 - terrain_.sea_distance[i] * 3 +
                                             (value_noise(x, y, 40, soil_salt) - 32768) * 60 / 32768,
                                         30, 215);
            soil_[i] = static_cast<std::uint8_t>(guess);
            moisture_[i] = static_cast<std::uint16_t>(guess << 8);
            nutrients_[i] = static_cast<std::uint8_t>(value_noise(x, y, 24, soil_salt + 1) >> 8);
            temperature_[i] = static_cast<std::int16_t>(temperature_at(i, terrain_.ocean[i]));
        }
    }
    // Let the baseflow carve rivers, then settle the biomes.
    refresh_water();
    for (int pass = 0; pass < 12; ++pass) {
        update_hydrology();
    }
    for (int y = 0; y < config.height; ++y) {
        update_biomes(y);
    }
}

int World::daylight() const {
    const int sun = -icos(day_angle()) + isin(season_angle()) * 160 / 1024;
    return std::clamp((sun + 200) * 255 / 1224, 0, 255);
}

int World::mean_temperature(std::size_t i) const {
    const int dm = std::max<int>(terrain_.elevation[i], 0);
    return config_.climate - dm / 50 + terrain_.aspect[i] / 2 - std::max<int>(terrain_.hollow[i], 0) / 8;
}

int World::temperature_at(std::size_t i, bool water) const {
    // One climate for the valley; the land itself makes the differences.
    // Water swings half as much as land.
    const int damp = water ? 2 : 1;
    const int seasonal = isin(season_angle()) * config_.seasons / 1024 / damp;
    const int night = std::max(0, icos(day_angle())); // 0 by day, up to 1024 at midnight
    const int daily = -icos(day_angle()) * 60 / 1024 / damp;
    // By day, slopes facing the sun warm; by night, cold air drains into hollows.
    const int sun = terrain_.aspect[i] * daylight() / 128;
    const int pooling = -std::max<int>(terrain_.hollow[i], 0) * night / 2048;
    return mean_temperature(i) + seasonal + daily + sun + pooling;
}

void World::step() {
    ++tick_;
    if (tick_ % 60 == 0) {
        update_wind();
    }
    update_fronts();
    for (int y = int(tick_ % soil_stagger); y < config_.height; y += soil_stagger) {
        update_soil(y);
    }
    for (int y = int(tick_ % biome_stagger); y < config_.height; y += biome_stagger) {
        update_biomes(y);
    }
    if (tick_ % hydrology_every == 0) {
        update_hydrology();
    }
}

void World::update_wind() {
    // Westerlies that swing a little with the season, plus a random walk.
    const int prevailing = isin(season_angle()) * 24 / 1024;
    wind_angle_ = (wind_angle_ + signed_angle(prevailing - wind_angle_) / 4 + int(weather_rng_.below(17)) - 8) & 255;
    const int gusty = season() == 1 || season() == 3 ? 10 : 20;
    wind_speed_ = std::clamp(wind_speed_ + int(weather_rng_.below(std::uint32_t(2 * gusty + 1))) - gusty, 16, 96);
}

void World::update_fronts() {
    std::fill(rain_.begin(), rain_.end(), 0);
    std::fill(cloud_.begin(), cloud_.end(), 0);
    const int w = config_.width, h = config_.height;

    // Wetter springs and autumns, drier summers and winters. A front is
    // bigger than much of the valley: its edges and paths are what vary.
    const int chance = 5 + (icos(2 * season_angle()) + 1024) * 8 / 2048;
    if (int(fronts_.size()) < max_fronts && int(weather_rng_.below(1024)) < chance) {
        int x = 0, y = 0;
        for (int attempt = 0; attempt < 6; ++attempt) {
            x = int(weather_rng_.below(std::uint32_t(w)));
            y = int(weather_rng_.below(std::uint32_t(h)));
            if (terrain_.ocean[std::size_t(y) * w + x]) {
                break;
            }
        }
        Front front{};
        front.x = x * 256 + 128;
        front.y = y * 256 + 128;
        front.radius = static_cast<std::uint16_t>(50 + weather_rng_.below(110));
        front.life = static_cast<std::uint16_t>(400 + weather_rng_.below(700));
        front.load = std::int32_t(5000 + weather_rng_.below(9000));
        front.vx = static_cast<std::int16_t>(int(weather_rng_.below(17)) - 8);
        front.vy = static_cast<std::int16_t>(int(weather_rng_.below(17)) - 8);
        front.last_height = terrain_.elevation[std::size_t(y) * w + x];
        fronts_.push_back(front);
    }

    const int wind_x = icos(wind_angle_) * wind_speed_ / 1024, wind_y = isin(wind_angle_) * wind_speed_ / 1024;
    for (auto& front : fronts_) {
        front.x += front.vx + wind_x;
        front.y += front.vy + wind_y;
        if (front.life) {
            --front.life;
        }
        const int cx = floor_div(front.x, 256), cy = floor_div(front.y, 256), r = front.radius;
        if (cx < -r || cy < -r || cx >= w + r || cy >= h + r) {
            front.life = 0;
            continue;
        }
        const auto centre = std::size_t(std::clamp(cy, 0, h - 1)) * w + std::size_t(std::clamp(cx, 0, w - 1));
        const bool over_sea = terrain_.ocean[centre];
        if (over_sea) {
            front.load = std::min(front.load + 12, 16000);
        }
        // Rising air rains harder: the windward side of mountains is wet and
        // the far side lies in a rain shadow.
        const int uplift = std::clamp(terrain_.elevation[centre] - front.last_height, 0, 100);
        front.last_height = terrain_.elevation[centre];
        const int boost = 256 + uplift * 6;

        int spent = 0;
        const int r2 = r * r;
        const int nx0 = std::max(0, (cx - r) / node), nx1 = std::min(nodes_x_ - 1, (cx + r) / node);
        const int ny0 = std::max(0, (cy - r) / node), ny1 = std::min(nodes_y_ - 1, (cy + r) / node);
        for (int ny = ny0; ny <= ny1; ++ny) {
            for (int nx = nx0; nx <= nx1; ++nx) {
                const std::int64_t dx = std::int64_t(nx * node + node / 2) * 256 - front.x;
                const std::int64_t dy = std::int64_t(ny * node + node / 2) * 256 - front.y;
                const int d2 = int((dx * dx + dy * dy) >> 16);
                if (d2 >= r2) {
                    continue;
                }
                const int weight = (r2 - d2) * 256 / r2;
                const auto at = std::size_t(ny * nodes_x_ + nx);
                cloud_[at] = static_cast<std::uint8_t>(
                    std::min(255, cloud_[at] + weight * std::min(front.load, 6000) / 6000));
                if (front.load > 800) {
                    const int rain = int(std::int64_t(weight) * front.load / 98304 * boost / 256);
                    rain_[at] = static_cast<std::uint8_t>(std::min(255, rain_[at] + rain));
                    spent += rain;
                }
            }
        }
        front.load -= spent / 40;

        if (!over_sea && front.load > 4000 && int(weather_rng_.below(4096)) < (front.load - 3000) / 300) {
            const int lx = cx + int(weather_rng_.below(std::uint32_t(r + 1))) - r / 2;
            const int ly = cy + int(weather_rng_.below(std::uint32_t(r + 1))) - r / 2;
            lightning_[lightning_next_] = {tick_, static_cast<std::int16_t>(std::clamp(lx, 0, w - 1)),
                                           static_cast<std::int16_t>(std::clamp(ly, 0, h - 1))};
            lightning_next_ = (lightning_next_ + 1) % lightning_kept;
        }
    }
    fronts_.erase(std::remove_if(fronts_.begin(), fronts_.end(),
                                 [](const Front& f) { return f.life == 0 || f.load <= 0; }),
                  fronts_.end());
}

int World::rain_at(int x, int y) const {
    const int fx = x * 256 / node - 128, fy = y * 256 / node - 128;
    const int nx = floor_div(fx, 256), ny = floor_div(fy, 256), sx = fx - nx * 256, sy = fy - ny * 256;
    const auto at = [&](int ax, int ay) {
        return int(rain_[std::size_t(std::clamp(ay, 0, nodes_y_ - 1) * nodes_x_ + std::clamp(ax, 0, nodes_x_ - 1))]);
    };
    const int top = at(nx, ny) * (256 - sx) + at(nx + 1, ny) * sx;
    const int bottom = at(nx, ny + 1) * (256 - sx) + at(nx + 1, ny + 1) * sx;
    return (top * (256 - sy) + bottom * sy) >> 16;
}

void World::update_soil(int y) {
    const int w = config_.width;
    const auto salt = std::uint32_t(tick_) ^ stream_seed(config_.seed, purpose_soil);
    for (int x = 0; x < w; ++x) {
        const auto i = std::size_t(y) * w + x;
        const auto kind = Water(water_[i]);
        const int t = temperature_at(i, kind == Water::ocean || kind == Water::lake);
        temperature_[i] = static_cast<std::int16_t>(t);
        if (kind == Water::ocean) {
            soil_[i] = 255;
            snow_[i] = 0;
            continue;
        }
        // Rain is bilinear between weather nodes, so storms have soft edges.
        const int rain = rain_at(x, y);
        if (kind == Water::lake) {
            lakes_[terrain_.basin[i]].inflow += std::uint32_t(rain);
            soil_[i] = 255;
            snow_[i] = 0;
            continue;
        }
        int soil = soil_[i], snow = snow_[i];
        if (t < 0) {
            snow = std::min(255, snow + rain);
        } else {
            soil += rain;
        }
        if (snow && t > 0) {
            const int melt = std::min(snow, 1 + t / 30);
            snow -= melt;
            soil += melt;
        }
        if (t > 0) {
            soil -= t / 576 + int(hash3(x, y, salt) % 576 < std::uint32_t(t % 576));
        }
        const auto wet = [&](int nx, int ny) {
            if (nx < 0 || ny < 0 || nx >= w || ny >= config_.height) {
                return false;
            }
            const auto k = Water(water_[std::size_t(ny) * w + nx]);
            return k == Water::river || k == Water::lake;
        };
        if (soil < 180 && (kind == Water::river || wet(x - 1, y) || wet(x + 1, y) || wet(x, y - 1) || wet(x, y + 1))) {
            soil += 2;
        }
        soil = std::max(soil, 0);
        const int capacity = soil_capacity[terrain_.substrate[i]];
        if (soil > capacity) {
            runoff_[i] += std::uint32_t(soil - capacity);
            soil = capacity;
        }
        soil_[i] = static_cast<std::uint8_t>(soil);
        snow_[i] = static_cast<std::uint8_t>(snow);
        auto& moisture = moisture_[i];
        moisture = static_cast<std::uint16_t>(int(moisture) + ((soil << 8) - int(moisture)) / 512);
    }
}

void World::update_hydrology() {
    const auto n = cells();
    for (std::size_t i = 0; i < n; ++i) {
        // Runoff since the last pass plus baseflow from the groundwater.
        accumulated_[i] = runoff_[i] + std::uint32_t(moisture_[i] >> 12);
        runoff_[i] = 0;
    }
    // Upstream to downstream: each cell passes its water on, unless a lake
    // that isn't full takes it, or the sea.
    for (auto at = terrain_.order.rbegin(); at != terrain_.order.rend(); ++at) {
        const auto i = std::size_t(*at);
        if (terrain_.ocean[i]) {
            continue;
        }
        const auto lake = terrain_.basin[i];
        if (lake && lakes_[lake].level < 1024) {
            lakes_[lake].inflow += accumulated_[i];
            continue;
        }
        if (!lake) {
            flow_[i] = (flow_[i] * 3 + accumulated_[i]) / 4;
        }
        const int down = terrain_.downstream[i];
        if (down >= 0) {
            accumulated_[std::size_t(down)] += accumulated_[i];
        }
    }
    for (std::size_t id = 1; id < lakes_.size(); ++id) {
        auto& lake = lakes_[id];
        const auto& basin = terrain_.basins[id];
        const int t = std::max<int>(temperature_[std::size_t(basin.cell)], 0);
        // Evaporation follows the lake's surface as it is now, so lakes in dry
        // basins shrink until what they lose matches what flows in.
        const std::int64_t evaporation = std::int64_t(lake_area_[id]) * (t + 20) / 6;
        const std::int64_t capacity = std::int64_t(basin.volume) * 16 + std::int64_t(basin.area) * 16;
        const std::int64_t change = (std::int64_t(lake.inflow) - evaporation) * 1024 / capacity;
        lake.level = static_cast<std::uint16_t>(std::clamp<std::int64_t>(lake.level + change, 0, 1024));
        lake.inflow = 0;
    }
    refresh_water();
}

void World::refresh_water() {
    const auto n = cells();
    const int w = config_.width, h = config_.height;
    std::fill(lake_area_.begin(), lake_area_.end(), 0);
    for (std::size_t i = 0; i < n; ++i) {
        const int dm = terrain_.elevation[i];
        Water kind = Water::none;
        int depth = 0;
        if (terrain_.ocean[i]) {
            kind = Water::ocean;
            depth = -dm;
        } else if (const auto id = terrain_.basin[i]) {
            const auto& basin = terrain_.basins[id];
            const int surface = basin.bottom + (basin.spill - basin.bottom) * lakes_[id].level / 1024;
            if (surface > dm) {
                kind = Water::lake;
                depth = surface - dm;
                ++lake_area_[id];
            }
        }
        water_[i] = std::uint8_t(kind);
        depth_[i] = static_cast<std::uint16_t>(std::clamp(depth, 0, 65535));
    }
    // Streams: a channel where enough water flows, widening downstream. The
    // banks beside a wide channel flood too, if they are no higher than its
    // surface.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = std::size_t(y) * w + x;
            if (Water(water_[i]) != Water::none || flow_[i] <= river_min_flow) {
                continue;
            }
            const int extra = int(isqrt(flow_[i] - river_min_flow));
            const int depth = std::min(25, 3 + extra / 6);
            const int radius = std::min(2, extra / 40);
            const int surface = terrain_.elevation[i] + depth;
            if (Water(water_[i]) == Water::none) {
                water_[i] = std::uint8_t(Water::river);
                depth_[i] = static_cast<std::uint16_t>(depth);
            }
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if ((dx == 0 && dy == 0) || dx * dx + dy * dy > radius * radius + 1 || nx < 0 || ny < 0 || nx >= w ||
                        ny >= h) {
                        continue;
                    }
                    const auto j = std::size_t(ny) * w + nx;
                    const int bank = surface - terrain_.elevation[j];
                    if (Water(water_[j]) == Water::none && bank > 0) {
                        water_[j] = std::uint8_t(Water::river);
                        depth_[j] = static_cast<std::uint16_t>(std::min(bank, depth));
                    }
                }
            }
        }
    }
}

void World::update_biomes(int y) {
    for (int x = 0; x < config_.width; ++x) {
        biome_[std::size_t(y) * config_.width + x] = std::uint8_t(classify(x, y));
    }
}

Biome World::classify(int x, int y) const {
    const int w = config_.width;
    const auto i = std::size_t(y) * w + x;
    const auto kind = Water(water_[i]);
    const int depth = depth_[i];
    const int dm = terrain_.elevation[i];
    const int t = mean_temperature(i);
    if (kind == Water::ocean) {
        if (depth > 150) {
            return Biome::deep_sea;
        }
        if (depth > 40) {
            return Biome::open_sea;
        }
        return t >= 200 ? Biome::reef_shallows : Biome::tidal_shallows;
    }
    if (kind == Water::lake) {
        return Biome::lake;
    }
    if (kind == Water::river) {
        return Biome::river;
    }
    const int m = moisture_[i] >> 8;
    const auto ground = Substrate(terrain_.substrate[i]);
    const auto next_to = [&](Water want) {
        const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx >= 0 && ny >= 0 && nx < w && ny < config_.height &&
                Water(water_[std::size_t(ny) * w + nx]) == want) {
                return true;
            }
        }
        return false;
    };
    if (ground == Substrate::rock) {
        return Biome::bare_peaks; // rock outcrops and scree
    }
    if (t < -60) {
        return Biome::ice;
    }
    if (ground == Substrate::sand && (terrain_.coast[i] || terrain_.sea_distance[i] <= 4)) {
        return Biome::beach;
    }
    if (m >= 190 || (ground == Substrate::peat && m >= 150)) {
        if (ground == Substrate::peat || t < 50) {
            return Biome::bog;
        }
        if (terrain_.coast[i] && t >= 200) {
            return Biome::mangrove;
        }
        return t < 180 ? Biome::marsh : Biome::swamp;
    }
    if (m >= 120 && (next_to(Water::river) || next_to(Water::lake))) {
        return Biome::riverbank;
    }
    if (dm > 380 && t < 150 && m < 150) {
        return Biome::alpine_meadow; // the windswept tops of the hills
    }
    if (t < 0) {
        return Biome::tundra;
    }
    if (t < 60) {
        return m >= 90 ? Biome::taiga : Biome::tundra;
    }
    if (t < 190) {
        if (m < 50 || ground == Substrate::gravel) {
            return Biome::scrubland;
        }
        if (m < 85) {
            return t < 120 || ground == Substrate::sand ? Biome::heath : Biome::grassland;
        }
        return m < 130 ? Biome::grassland : Biome::deciduous_forest;
    }
    if (m < 35) {
        return ground == Substrate::clay || terrain_.filled[i] > dm ? Biome::salt_flat : Biome::desert;
    }
    if (m < 60) {
        return Biome::desert;
    }
    if (m < 100) {
        return Biome::savanna;
    }
    return m < 150 ? Biome::dry_woodland : Biome::rainforest;
}

std::uint64_t World::fingerprint() const {
    Fnv1a hash;
    hash.add(config_.seed);
    hash.add(std::uint32_t(config_.width));
    hash.add(std::uint32_t(config_.height));
    hash.add(std::uint32_t(config_.ocean_percent));
    hash.add(config_.climate);
    hash.add(config_.seasons);
    hash.add(tick_);
    hash.add(weather_rng_.state());
    hash.add(std::uint32_t(wind_angle_));
    hash.add(std::uint32_t(wind_speed_));
    for (const auto& f : fronts_) {
        hash.add(f.x);
        hash.add(f.y);
        hash.add(f.vx);
        hash.add(f.vy);
        hash.add(f.radius);
        hash.add(f.life);
        hash.add(f.load);
        hash.add(f.last_height);
    }
    for (const auto& lake : lakes_) {
        hash.add(lake.level);
        hash.add(lake.inflow);
    }
    for (const auto& strike : lightning_) {
        hash.add(strike.tick);
        hash.add(strike.x);
        hash.add(strike.y);
    }
    const auto all = [&hash](const auto& values) {
        for (const auto v : values) {
            hash.add(v);
        }
    };
    all(water_);
    all(depth_);
    all(temperature_);
    all(soil_);
    all(moisture_);
    all(snow_);
    all(nutrients_);
    all(runoff_);
    all(flow_);
    all(biome_);
    return hash.value();
}

std::size_t World::memory_bytes() const {
    const auto bytes = [](const auto& v) { return v.capacity() * sizeof(v[0]); };
    return sizeof(*this) + bytes(terrain_.elevation) + bytes(terrain_.filled) + bytes(terrain_.downstream) +
           bytes(terrain_.order) + bytes(terrain_.basin) + bytes(terrain_.basins) + bytes(terrain_.ocean) +
           bytes(terrain_.coast) + bytes(terrain_.sea_distance) + bytes(terrain_.substrate) + bytes(terrain_.hollow) +
           bytes(terrain_.aspect) + bytes(terrain_.slope) + bytes(water_) + bytes(depth_) +
           bytes(temperature_) + bytes(soil_) + bytes(moisture_) + bytes(snow_) + bytes(nutrients_) +
           bytes(runoff_) + bytes(flow_) + bytes(biome_) + bytes(lakes_) + fronts_.capacity() * sizeof(Front) +
           bytes(rain_) + bytes(cloud_) + bytes(accumulated_);
}

// Layout, little-endian: magic, version, the config, the clock and weather,
// lakes, every per-cell array, then the FNV-1a checksum of everything before
// it. The land itself is regenerated from the config.
std::vector<std::uint8_t> World::save() const {
    Writer w;
    w.out.assign(save_magic, save_magic + 4);
    w.put(save_version);
    w.put(config_.seed);
    w.put(std::uint32_t(config_.width));
    w.put(std::uint32_t(config_.height));
    w.put(std::uint32_t(config_.ocean_percent));
    w.put(config_.climate);
    w.put(config_.seasons);
    w.put(tick_);
    w.put(weather_rng_.state());
    w.put(std::uint32_t(wind_angle_));
    w.put(std::uint32_t(wind_speed_));
    w.put(std::uint32_t(lightning_next_));
    for (const auto& strike : lightning_) {
        w.put(strike.tick);
        w.put(strike.x);
        w.put(strike.y);
    }
    w.put(std::uint32_t(fronts_.size()));
    for (const auto& f : fronts_) {
        w.put(f.x);
        w.put(f.y);
        w.put(f.vx);
        w.put(f.vy);
        w.put(f.radius);
        w.put(f.life);
        w.put(f.load);
        w.put(f.last_height);
    }
    w.put(std::uint32_t(lakes_.size()));
    for (const auto& lake : lakes_) {
        w.put(lake.level);
        w.put(lake.inflow);
    }
    w.array(water_);
    w.array(depth_);
    w.array(temperature_);
    w.array(soil_);
    w.array(moisture_);
    w.array(snow_);
    w.array(nutrients_);
    w.array(runoff_);
    w.array(flow_);
    w.array(biome_);
    w.put(checksum(w.out.data(), w.out.size()));
    return std::move(w.out);
}

bool World::load(const std::uint8_t* data, std::size_t size) {
    if (size < 16 || std::memcmp(data, save_magic, 4) != 0) {
        return false;
    }
    Reader tail{data + size - 8, data + size};
    if (tail.get<std::uint64_t>() != checksum(data, size - 8)) {
        return false;
    }
    Reader r{data + 4, data + size - 8};
    if (r.get<std::uint32_t>() != save_version) {
        return false;
    }
    WorldConfig config;
    config.seed = r.get<std::uint32_t>();
    config.width = int(r.get<std::uint32_t>());
    config.height = int(r.get<std::uint32_t>());
    config.ocean_percent = int(r.get<std::uint32_t>());
    config.climate = r.get<std::int32_t>();
    config.seasons = r.get<std::int32_t>();
    if (!r.ok || config.width <= 0 || config.height <= 0 || config.width > 4096 || config.height > 4096) {
        return false;
    }
    World world(config);
    world.tick_ = r.get<std::uint64_t>();
    world.weather_rng_.set_state(r.get<std::uint32_t>());
    world.wind_angle_ = int(r.get<std::uint32_t>());
    world.wind_speed_ = int(r.get<std::uint32_t>());
    world.lightning_next_ = r.get<std::uint32_t>() % lightning_kept;
    for (auto& strike : world.lightning_) {
        strike.tick = r.get<std::uint64_t>();
        strike.x = r.get<std::int16_t>();
        strike.y = r.get<std::int16_t>();
    }
    const auto fronts = r.get<std::uint32_t>();
    if (fronts > std::uint32_t(max_fronts)) {
        return false;
    }
    world.fronts_.resize(fronts);
    for (auto& f : world.fronts_) {
        f.x = r.get<std::int32_t>();
        f.y = r.get<std::int32_t>();
        f.vx = r.get<std::int16_t>();
        f.vy = r.get<std::int16_t>();
        f.radius = r.get<std::uint16_t>();
        f.life = r.get<std::uint16_t>();
        f.load = r.get<std::int32_t>();
        f.last_height = r.get<std::int16_t>();
    }
    if (r.get<std::uint32_t>() != world.lakes_.size()) {
        return false;
    }
    for (auto& lake : world.lakes_) {
        lake.level = r.get<std::uint16_t>();
        lake.inflow = r.get<std::uint32_t>();
    }
    r.array(world.water_);
    r.array(world.depth_);
    r.array(world.temperature_);
    r.array(world.soil_);
    r.array(world.moisture_);
    r.array(world.snow_);
    r.array(world.nutrients_);
    r.array(world.runoff_);
    r.array(world.flow_);
    r.array(world.biome_);
    if (!r.ok || r.at != r.end) {
        return false;
    }
    // The weather grids are rebuilt from the fronts, as at the start of a tick,
    // and the lake areas from the water.
    world.lake_area_.assign(world.lakes_.size(), 0);
    for (std::size_t i = 0; i < world.cells(); ++i) {
        if (Water(world.water_[i]) == Water::lake) {
            ++world.lake_area_[world.terrain_.basin[i]];
        }
    }
    *this = std::move(world);
    return true;
}

} // namespace phd
