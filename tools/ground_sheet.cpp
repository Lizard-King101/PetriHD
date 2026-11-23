// Paints a piece of the close view's ground to a PNG and times it, to judge
// the painter without opening a window:
//   petrihd-ground out.png [--seed N] [--ticks N] [--at X Y] [--cells N]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ground_art.hpp"
#include "png.hpp"
#include "world.hpp"

int main(int argc, char** argv) {
    std::string path = "ground.png";
    phd::WorldConfig config;
    long long ticks = 120;
    int at_x = 174, at_y = 61, cells = 24;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::uint32_t(std::atoll(argv[++i]));
        } else if (arg == "--ticks" && i + 1 < argc) {
            ticks = std::atoll(argv[++i]);
        } else if (arg == "--at" && i + 2 < argc) {
            at_x = std::atoi(argv[++i]);
            at_y = std::atoi(argv[++i]);
        } else if (arg == "--cells" && i + 1 < argc) {
            cells = std::atoi(argv[++i]);
        } else {
            path = arg;
        }
    }
    phd::World world(config);
    for (long long t = 0; t < ticks; ++t) {
        world.step();
    }
    // In tiles of 8 cells, as the renderer paints it, so seams would show.
    constexpr int tile = 8 * art::ground::texels_per_cell;
    const int tiles = (cells + 7) / 8, size = tiles * tile;
    std::vector<std::uint8_t> rgba(std::size_t(size) * size * 4), rgb(std::size_t(size) * size * 3);
    const auto start = std::chrono::steady_clock::now();
    for (int j = 0; j < tiles; ++j) {
        for (int i = 0; i < tiles; ++i) {
            art::ground::paint(world, at_x * art::ground::texels_per_cell + i * tile,
                               at_y * art::ground::texels_per_cell + j * tile, tile, tile,
                               &rgba[(std::size_t(j) * tile * size + std::size_t(i) * tile) * 4], size * 4);
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    for (std::size_t i = 0; i < std::size_t(size) * size; ++i) {
        rgb[i * 3] = rgba[i * 4], rgb[i * 3 + 1] = rgba[i * 4 + 1], rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    if (!phd::write_png(path.c_str(), size, size, rgb)) {
        std::fprintf(stderr, "could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("wrote %s: %d tiles of %d x %d texels, %.2f ms a tile\n", path.c_str(), tiles * tiles, tile, tile,
                ms / double(tiles * tiles));
    return 0;
}
