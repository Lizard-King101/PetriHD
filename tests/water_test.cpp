#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ground_art.hpp"
#include "water_surface.hpp"

namespace {

bool dev_map_flood() {
    phd::WorldConfig config;
    config.width = 128;
    config.height = 96;
    config.layout = phd::Layout::development;
    config.seed = 1;
    phd::World world(config);

    // A narrow lake holds water below the two high bank corners on its
    // diagonal. The overhead sample at its centre must see that water.
    constexpr int x = 66, y = 43;
    const auto cell = std::size_t(y) * config.width + x;
    const art::water::Field field(world, x, y, 2, 2);
    const float depth = field.depth(x + 0.5f, y + 0.5f);
    std::uint8_t rgb[3], water = 0;
    art::ground::sample(world, x, y, 1, 1, 1, rgb, &water);
    if (!water || world.water_depth_dm(cell) <= 0 || std::abs(depth - world.water_depth_dm(cell) * 0.1f) > 0.001f) {
        std::fprintf(stderr, "Submerged lakebed came through the water at (%d,%d)\n", x, y);
        return false;
    }

    // Follow the first storm over the floodplain. Rain should flood both
    // pockets, and the water should retreat across the meadow during the lull.
    const auto wet = [&](int x, int y) { return world.water(y * config.width + x) == phd::Water::river; };
    // The ground is the same ground under a flood as before it.
    const art::water::Field before(world, 0, 0, config.width + 1, config.height + 1);
    const auto ground_held = [&] {
        const art::water::Field now(world, 0, 0, config.width + 1, config.height + 1);
        for (int y = 0; y <= config.height; ++y) {
            for (int x = 0; x <= config.width; ++x) {
                const auto& was = before.corner(x, y);
                const auto& is = now.corner(x, y);
                if ((is.bed != was.bed || is.center.bed != was.center.bed)) {
                    std::fprintf(stderr, "The ground moved under the flood at %d,%d: %.2f to %.2f\n", x, y, was.bed, is.bed);
                    return false;
                }
            }
        }
        return true;
    };
    const auto basin = world.terrain().basin[cell];
    int peak_stage = 0, peak_area = 0, clear_ticks = 0;
    bool receding = false;
    for (int t = 1; t <= 3 * phd::World::ticks_per_day; ++t) {
        world.step();
        bool rain = false;
        for (int ny = 0; ny < world.nodes_y() && !rain; ++ny) {
            for (int nx = 0; nx < world.nodes_x(); ++nx) {
                if (world.rain(nx, ny) > 0) {
                    rain = true;
                    break;
                }
            }
        }
        clear_ticks = rain ? 0 : clear_ticks + 1;
        if (world.water(16 * config.width + 23) != phd::Water::none) {
            std::fprintf(stderr, "Flood spread onto high ground\n");
            return false;
        }
        const int stage = world.lake_surface_dm(basin);
        if (!peak_stage) {
            const bool backed_up = stage > world.terrain().basins[basin].spill;
            if (!wet(63, 35) || !wet(75, 52) || !backed_up || world.rain_at(63, 35) == 0) continue;
            const auto saved = world.save();
            phd::World restored(config);
            if (world.elevation(cell) + world.water_depth_dm(cell) != stage || !restored.load(saved.data(), saved.size()) ||
                restored.lake_surface_dm(basin) != stage || restored.rain_at(63, 35) != world.rain_at(63, 35)) {
                std::fprintf(stderr, "Reload lost the storm or its raised water level\n");
                return false;
            }
        }
        peak_stage = std::max(peak_stage, stage);
        int area = 0;
        for (int y = 32; y <= 38; ++y) {
            for (int x = 59; x <= 67; ++x) area += wet(x, y);
        }
        if (area > peak_area && !ground_held()) return false;
        peak_area = std::max(peak_area, area);
        if (clear_ticks && area > 0 && area < peak_area) receding = true;
        // The stream's own low bank is in this patch, and stays wet while the
        // stream runs high after the rain. The meadow beyond it must clear.
        if (receding && clear_ticks >= phd::World::ticks_per_day / 2 && area * 10 <= peak_area && !wet(63, 35) && !wet(75, 52) &&
            stage < peak_stage) {
            return true;
        }
    }
    std::fprintf(stderr, "Expected a storm, standing floodwater after rain, and an exposed meadow during the clear spell\n");
    return false;
}

} // namespace

int main() {
    if (!dev_map_flood()) return 1;
    phd::WorldConfig config;
    config.width = 128;
    config.height = 96;
    config.seed = 6;
    config.layout = phd::Layout::development;
    phd::World world(config);
    for (int i = 0; i < 120; ++i) world.step();
    const art::water::Field field(world, 0, 0, config.width + 1, config.height + 1);

    int lake = 0, sea = 0, banks = 0;
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            const auto cell = std::size_t(y) * config.width + x;
            const auto& bank = field.corner(x, y);
            if (bank.kind == phd::Water::none && bank.level != art::water::dry) {
                ++banks;
                if (bank.level > bank.bed) {
                    std::fprintf(stderr, "Open water edge above the bank at %d,%d\n", x, y);
                    return 1;
                }
            }
            const auto kind = world.water(cell);
            if (kind != phd::Water::lake && kind != phd::Water::ocean) continue;
            const float level = kind == phd::Water::lake ? world.lake_surface_dm(world.terrain().basin[cell]) * 0.1f : 0.0f;
            for (int j = y; j <= y + 1; ++j) {
                for (int i = x; i <= x + 1; ++i) {
                    const float plane = field.plane(x, y);
                    if ((plane == art::water::dry ? field.corner(i, j).level : plane) != level) {
                        std::fprintf(stderr, "Standing water bends at %d,%d\n", i, j);
                        return 1;
                    }
                }
            }
            lake += kind == phd::Water::lake;
            sea += kind == phd::Water::ocean;
        }
    }
    // Sample an overhead tile spanning the lake's northern shore. Its wet
    // texels must lie within the geometric intersection, including the
    // triangle diagonal and the boundary between independently baked tiles.
    constexpr int tpc = 4, x0 = 84, y0 = 38, width = 24 * tpc, height = 16 * tpc;
    std::vector<std::uint8_t> rgb(width * height * 3), wet(width * height);
    art::ground::sample(world, x0 * tpc, y0 * tpc, width, height, tpc, rgb.data(), wet.data());
    int covered = 0;
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            const float x = x0 + (float(i) + 0.5f) / tpc, y = y0 + (float(j) + 0.5f) / tpc;
            // The painted edge is ragged, so the thinnest water may be left out.
            const float depth = field.depth(x, y);
            if (wet[j * width + i] ? depth <= art::water::shore_epsilon : depth > 0.2f) {
                std::fprintf(stderr, "Overhead shore and lakebed disagree at %.3f,%.3f\n", x, y);
                return 1;
            }
            covered += wet[j * width + i];
        }
    }
    if (!lake || !sea || !banks || !covered || covered == width * height) {
        std::fprintf(stderr, "Missing water or shoreline in development fixture\n");
        return 1;
    }
    // These two drainage basins meet at a corner when filled. Each lake has
    // its own elevation up to the ridge between them.
    config.layout = phd::Layout::valley;
    config.seed = 2;
    phd::World valley(config);
    for (int i = 0; i < 160; ++i) valley.step();
    constexpr int vx = 60, vy = 62;
    const art::water::Field divide(valley, vx, vy, 2, 2);
    const auto id = valley.terrain().basin[std::size_t(vy) * config.width + vx];
    if (!id || divide.plane(vx, vy) != valley.lake_surface_dm(id) * 0.1f) {
        std::fprintf(stderr, "Touching lake basins lost their separate water levels\n");
        return 1;
    }
    return 0;
}
