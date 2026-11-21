// Raw observations for the large-swimmer mating study. A living adult has one
// row per compatible partner, or one unpaired row. Deaths have a separate row.
#include "swimmer_trace.hpp"

#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <string_view>

namespace {

void row(const phd::World& world, const char* phase, const phd::Animal& a, int capacity,
         const phd::Animal* partner, std::uint32_t killer = 0) {
    std::printf("%" PRIu32 ",%" PRIu64 ",%s,%" PRIu32 ",%d,%d,%d,%d,%d,%d,%d,%d,%d,",
                world.seed(), world.tick(), phase, a.id, a.energy, capacity, a.drive,
                a.drive >= phd::mating_drive, !!(a.wants & phd::wants_mate), a.hunting != phd::HuntHistory::none,
                a.young, a.health, a.breath);
    if (partner) {
        const auto& b = *partner;
        const std::int64_t dx = a.x - b.x, dy = a.y - b.y;
        const auto distance = dx * dx + dy * dy;
        std::printf("%" PRIu32 ",%d,%d,%d,%d,%d,%d,%" PRId64 ",%d", b.id,
                    b.stock == phd::FishStock::large_swimmer, b.energy,
                    phd::frame(b.genome, b.growth).store, b.drive, b.drive >= phd::mating_drive,
                    !!(b.wants & phd::wants_mate), distance, distance < std::int64_t(phd::mating_reach) * phd::mating_reach);
    } else {
        std::fputs(",,,,,,,,", stdout);
    }
    std::printf(",%" PRIu32 "\n", killer);
}

void observe(const phd::World& world, const char* phase, const phd::Animal& a) {
    if (a.stock != phd::FishStock::large_swimmer || !a.health || a.growth != 255) return;
    const int capacity = phd::frame(a.genome, a.growth).store;
    bool found = false;
    for (const auto& b : world.animals()) {
        if (a.id == b.id || !b.health || b.growth != 255 || phd::Medium(a.medium) == phd::Medium::air ||
            phd::Medium(b.medium) == phd::Medium::air ||
            phd::genetic_distance(a.genome, b.genome) > phd::breeding_distance) continue;
        row(world, phase, a, capacity, &b);
        found = true;
    }
    if (!found) row(world, phase, a, capacity, nullptr);
}

void run(std::uint32_t seed) {
    phd::WorldConfig config;
    config.seed = seed;
    config.layout = phd::Layout::development;
    config.width = 128;
    config.height = 96;
    phd::World world(config);
    world.set_brain_mode(phd::BrainMode::evolving);
    world.seed_animals(56);
    for (;;) {
        int alive = 0;
        for (const auto& a : world.animals()) {
            if (a.stock != phd::FishStock::large_swimmer) continue;
            ++alive;
            observe(world, "sample", a);
        }
        if (!alive || world.tick() == 40 * phd::World::ticks_per_year) break;
        world.step();
    }
    std::fprintf(stderr, "seed=%" PRIu32 " tick=%" PRIu64 " fingerprint=%016" PRIx64 "\n",
                 seed, world.tick(), world.fingerprint());
}

} // namespace

namespace swimmer_trace {
void mating_pass(const phd::World& world, const phd::Animal& animal) {
    observe(world, "pass", animal);
}

void death(const phd::World& world, const phd::Animal& animal, std::uint32_t killer) {
    if (animal.stock == phd::FishStock::large_swimmer && animal.growth == 255) {
        row(world, "death", animal, phd::frame(animal.genome, animal.growth).store, nullptr, killer);
    }
}
} // namespace swimmer_trace

int main(int argc, char** argv) {
    std::uint32_t first = 1, last = 8;
    if (argc == 2) {
        const std::string_view arg(argv[1]);
        const auto result = std::from_chars(arg.data(), arg.data() + arg.size(), first);
        if (result.ec != std::errc{} || result.ptr != arg.data() + arg.size()) return 2;
        last = first;
    } else if (argc != 1) {
        std::fputs("usage: petrihd-swimmer-trace [seed]\n", stderr);
        return 2;
    }
    std::puts("seed,tick,phase,id,energy,capacity,drive,ready,wants_mate,ate_own_kill,offspring,health,breath,"
              "other_id,other_is_large,other_energy,other_capacity,other_drive,other_ready,other_wants_mate,distance_squared,in_range,killer");
    for (auto seed = first;; ++seed) {
        run(seed);
        if (seed == last) break;
    }
    return std::fflush(stdout) == 0 && !std::ferror(stdout) ? 0 : 1;
}
