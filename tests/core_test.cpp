#include <cinttypes>
#include <cstdio>
#include <vector>

#include "file.hpp"
#include "fnv.hpp"
#include "png.hpp"
#include "random.hpp"
#include "world.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

phd::World run(std::uint32_t seed, int ticks) {
    phd::WorldConfig config;
    config.seed = seed;
    phd::World world(config);
    for (int i = 0; i < ticks; ++i) {
        world.step();
    }
    return world;
}

void test_random_matches_petri() {
    // Petri's World::random with rng_ = 0: the first outputs, which Petri's
    // own runs depended on.
    phd::Random random(0);
    std::uint32_t state = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint32_t value = (state += 0x9e3779b9u);
        value = (value ^ (value >> 16)) * 0x85ebca6bu;
        value = (value ^ (value >> 13)) * 0xc2b2ae35u;
        CHECK(random.next() == (value ^ (value >> 16)));
    }
    phd::Random bounded(7);
    for (int i = 0; i < 1000; ++i) {
        CHECK(bounded.below(10) < 10);
    }
}

void test_fnv_known_values() {
    // Published FNV-1a 64 test vectors.
    phd::Fnv1a empty;
    CHECK(empty.value() == 0xcbf29ce484222325ull);
    phd::Fnv1a a;
    a.add(std::uint8_t('a'));
    CHECK(a.value() == 0xaf63dc4c8601ec8cull);
    // Integers go in least significant byte first.
    phd::Fnv1a wide, bytes;
    wide.add(std::uint16_t(0x6261));
    bytes.add(std::uint8_t('a'));
    bytes.add(std::uint8_t('b'));
    CHECK(wide.value() == bytes.value());
}

void test_determinism() {
    CHECK(run(1, 500).fingerprint() == run(1, 500).fingerprint());
    CHECK(run(1, 500).fingerprint() != run(2, 500).fingerprint());
    CHECK(run(0, 50).fingerprint() == run(0, 50).fingerprint());
}

void test_save_round_trip() {
    auto world = run(5, 300);
    const auto saved = world.save();
    phd::World loaded(phd::WorldConfig{});
    CHECK(loaded.load(saved.data(), saved.size()));
    CHECK(loaded.fingerprint() == world.fingerprint());
    for (int i = 0; i < 300; ++i) {
        world.step();
        loaded.step();
    }
    CHECK(loaded.fingerprint() == world.fingerprint());
    CHECK(loaded.fingerprint() == run(5, 600).fingerprint());

    // A damaged save is refused, and the world is left as it was.
    auto damaged = saved;
    damaged[damaged.size() / 2] ^= 1;
    const auto before = loaded.fingerprint();
    CHECK(!loaded.load(damaged.data(), damaged.size()));
    CHECK(!loaded.load(saved.data(), saved.size() - 1));
    CHECK(loaded.fingerprint() == before);
}

void test_files() {
    const std::string path = "core_test_save.phdw";
    const auto data = run(3, 10).save();
    CHECK(phd::write_file_atomic(path, data));
    std::vector<std::uint8_t> read;
    CHECK(phd::read_file(path, read));
    CHECK(read == data);
    std::remove(path.c_str());

    std::vector<std::uint8_t> rgb(4 * 3 * 3, 200);
    CHECK(phd::write_png("core_test.png", 4, 3, rgb));
    CHECK(phd::read_file("core_test.png", read));
    CHECK(read.size() > 8 && read[1] == 'P' && read[2] == 'N' && read[3] == 'G');
    std::remove("core_test.png");
}

// The fingerprint after 2,400 ticks on seed 1. It must match on every
// platform and build (x86, aarch64, NEON or not). If a change to the world is
// meant to change it, update it in the same commit and say why.
constexpr std::uint64_t golden_seed1_t2400 = 0x6045c1b621e393fcull;

void test_golden() {
    const auto value = run(1, 2400).fingerprint();
    std::printf("seed=1 tick=2400 fingerprint=%016" PRIx64 "\n", value);
    CHECK(value == golden_seed1_t2400);
}

} // namespace

int main() {
    test_random_matches_petri();
    test_fnv_known_values();
    test_determinism();
    test_save_round_trip();
    test_files();
    test_golden();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all core tests passed\n");
    return 0;
}
