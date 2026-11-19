// petrihd-run: runs a world headless, as fast as the machine allows, and
// prints a census line of key=value pairs every so often.
//
//   petrihd-run seed=1 ticks=2400 every=240 [load=in.phdw] [save=out.phdw] [png=out.png]

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "file.hpp"
#include "palette.hpp"
#include "png.hpp"
#include "world.hpp"

namespace {

void census(const phd::World& world, double tick_us) {
    const auto n = std::size_t(world.width()) * world.height();
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += world.nutrients()[i];
    }
    std::printf("tick=%" PRIu64 " fingerprint=%016" PRIx64 " nutrients_mean=%.2f tick_us=%.1f\n", world.tick(),
                world.fingerprint(), double(sum) / double(n), tick_us);
}

} // namespace

int main(int argc, char** argv) {
    phd::WorldConfig config;
    std::uint64_t ticks = 2400, every = 240;
    std::string load, save, png;
    for (int i = 1; i < argc; ++i) {
        const char* eq = std::strchr(argv[i], '=');
        if (!eq) {
            std::fprintf(stderr, "expected key=value, got %s\n", argv[i]);
            return 2;
        }
        const std::string key(argv[i], std::size_t(eq - argv[i])), value(eq + 1);
        if (key == "seed") {
            config.seed = std::uint32_t(std::strtoul(value.c_str(), nullptr, 0));
        } else if (key == "ticks") {
            ticks = std::strtoull(value.c_str(), nullptr, 0);
        } else if (key == "every") {
            every = std::strtoull(value.c_str(), nullptr, 0);
        } else if (key == "load") {
            load = value;
        } else if (key == "save") {
            save = value;
        } else if (key == "png") {
            png = value;
        } else {
            std::fprintf(stderr, "unknown key %s\n", key.c_str());
            return 2;
        }
    }

    phd::World world(config);
    if (!load.empty()) {
        std::vector<std::uint8_t> data;
        if (!phd::read_file(load, data) || !world.load(data.data(), data.size())) {
            std::fprintf(stderr, "could not load %s\n", load.c_str());
            return 1;
        }
    }
    const auto end = world.tick() + ticks;
    auto started = std::chrono::steady_clock::now();
    std::uint64_t since = 0;
    while (world.tick() < end) {
        world.step();
        ++since;
        if (every && (world.tick() % every == 0 || world.tick() == end)) {
            const auto now = std::chrono::steady_clock::now();
            census(world, std::chrono::duration<double, std::micro>(now - started).count() / double(since));
            started = now;
            since = 0;
        }
    }
    if (!every) {
        census(world, 0);
    }
    if (!save.empty() && !phd::write_file_atomic(save, world.save())) {
        std::fprintf(stderr, "could not save %s\n", save.c_str());
        return 1;
    }
    if (!png.empty()) {
        std::vector<std::uint8_t> rgb;
        phd::world_rgb(world, rgb);
        if (!phd::write_png(png, world.width(), world.height(), rgb)) {
            std::fprintf(stderr, "could not write %s\n", png.c_str());
            return 1;
        }
    }
    return 0;
}
