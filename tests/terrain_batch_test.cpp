#include <array>
#include <cstdio>
#include <future>

#include "ground_art.hpp"
#include "terrain_art.hpp"
#include "terrain_batch.hpp"

namespace {

std::vector<std::uint8_t> paint(const phd::World& world, int layer) {
    constexpr int size = 64;
    if (layer == 0) {
        std::vector<std::uint8_t> rgb(size * size * 3);
        art::bake(world, art::sun_for(world), 40, 60, size, size, rgb.data(), size * 3);
        return gfx::half_terrain<3>(rgb, size, size);
    }
    std::vector<std::uint8_t> rgba(size * size * 4);
    art::ground::paint(world, 320, 480, size, size, rgba.data(), size * 4, false);
    return rgba;
}

} // namespace

int main() {
    phd::WorldConfig config;
    config.width = 64;
    config.height = 48;
    phd::World world(config);
    while (world.tick() + 1 < phd::World::ticks_per_year / 2) world.step();
    const auto summer = world.tick();
    const std::array expected{paint(world, 0), paint(world, 1)};
    std::array<std::vector<std::uint8_t>, 2> shown;
    std::array<std::uint64_t, 2> ticks{};
    std::promise<void> second_started, allow_second;
    auto gate = allow_second.get_future();
    gfx::TerrainBatch batch;
    const auto queue = [&](bool delay) {
        for (int i = 0; i < 2; ++i) {
            batch.add([&, i, delay](const phd::World& source) {
                if (delay && i == 1) {
                    second_started.set_value();
                    gate.wait();
                }
                return [&, i, image = paint(source, i), tick = source.tick()]() mutable {
                    shown[i] = std::move(image);
                    ticks[i] = tick;
                };
            });
        }
    };
    queue(true);
    batch.finish(world, false);
    second_started.get_future().wait();
    world.step(); // Autumn arrives between painting the map and the close-up ground.
    const bool partial = batch.begin(false) || !shown[0].empty() || !shown[1].empty();
    allow_second.set_value();
    batch.begin(true);
    if (partial || ticks[0] != summer || ticks[1] != summer || shown != expected) {
        std::fprintf(stderr, "Terrain publication mixed ticks or exposed an unfinished batch\n");
        return 1;
    }
    // A capture sees the requested tick immediately, including both texture resolutions.
    queue(false);
    batch.finish(world, true);
    if (ticks[0] != world.tick() || ticks[1] != world.tick() ||
        shown[0] != paint(world, 0) || shown[1] != paint(world, 1)) return 1;
    return 0;
}
