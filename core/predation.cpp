#include "world.hpp"

#include <algorithm>

#include "math.hpp"
#ifdef PETRIHD_SWIMMER_TRACE
#include "swimmer_trace.hpp"
#endif

namespace phd {
namespace {

// A mouth can reach at most one cell. Only its own cell and its eight
// neighbours need visiting, even when the lake is crowded.
template<class Visit>
void near_mouth(const Animal& a, int width, int height, Visit visit) {
    const int cx = a.x >> 8, cy = a.y >> 8;
    for (int y = std::max(0, cy - 1); y <= std::min(height - 1, cy + 1); ++y) {
        for (int x = std::max(0, cx - 1); x <= std::min(width - 1, cx + 1); ++x) {
            visit(std::size_t(y) * width + x);
        }
    }
}

int reach(int length, int other_length) {
    return std::min(256, (length * 3 / 4 + other_length / 2) * 256 / (cell_decimetres * 100));
}

} // namespace

std::int32_t World::bite_target(const Animal& a, int length) const {
    std::int32_t target = -1;
    int nearest = 256 * 256 + 1;
    const int fx = icos(a.heading), fy = isin(a.heading);
    near_mouth(a, width(), height(), [&](std::size_t cell) {
        for (auto j = animal_head_[cell]; j >= 0; j = animal_next_[j]) {
            const Animal& b = animals_[j];
            if (a.id == b.id || !b.health || a.medium != b.medium) continue;
            const int dx = b.x - a.x, dy = b.y - a.y;
            if (dx * fx + dy * fy < 0) continue;
            const int dz = (int(b.z) - a.z) * 256 / cell_decimetres;
            if (std::abs(dz) > 256) continue;
            const int distance = dx * dx + dy * dy + dz * dz;
            const int radius = reach(length, body_length_mm(b.genome, b.growth));
            if (distance > radius * radius || distance > nearest) continue;
            if (distance < nearest || target < 0 || b.id < animals_[target].id) {
                nearest = distance;
                target = j;
            }
        }
    });
    return target;
}

void World::strike(Animal& a, const Frame& body) {
    a.energy -= 1 + body.upkeep / 2; // a missed lunge also costs energy
    const auto target = bite_target(a, body.length);
    if (target < 0) return;
    Animal& b = animals_[target];
    const int damage = std::clamp(64 * body.length / body_length_mm(b.genome, b.growth), 8, 255);
    b.health = std::uint8_t(std::max(0, int(b.health) - damage));
    b.pain = std::uint8_t(std::min(damage, 127));
    b.pain_bearing = std::uint8_t(a.x == b.x && a.y == b.y ? a.heading + 128 : angle_of(a.x - b.x, a.y - b.y));
    if (!b.health) {
        leave_carcass(b, a.id);
        if (a.stock == FishStock::large_swimmer) {
            ++hunts_.kills;
            hunts_.small_fish_killed += b.stock == FishStock::small_fish;
        }
    }
}

void World::index_carcass(std::size_t index) {
    if (carcass_head_.empty()) carcass_head_.assign(cells(), -1);
    Carcass& c = carcasses_[index];
    auto& head = carcass_head_[std::size_t(c.y >> 8) * width() + (c.x >> 8)];
    c.next = head;
    head = std::int32_t(index);
}

void World::leave_carcass(const Animal& a, std::uint32_t killer) {
#ifdef PETRIHD_SWIMMER_TRACE
    swimmer_trace::death(*this, a, killer);
#endif
    const Frame f = frame(a.genome, a.growth);
    // Its remaining reserves plus the tissue energy used when building a body.
    const int energy = std::max(1, f.mass75 / 4) + std::clamp(a.energy, 0, f.store);
    carcasses_.push_back({a.x, a.y, energy, killer, std::uint16_t(f.length), a.heading});
    index_carcass(carcasses_.size() - 1);
}

bool World::eat_meat(Animal& a, const Frame& f) {
    if (carcasses_.empty()) return false;
    int nearest = 256 * 256 + 1;
    Carcass* meal = nullptr;
    near_mouth(a, width(), height(), [&](std::size_t cell) {
        if (carcass_head_[cell] < 0) return;
        const int depth = Water(water_[cell]) == Water::none ? 0 : depth_[cell] * 100;
        if (Medium(a.medium) == Medium::water ? depth == 0 : depth > f.wade) return;
        for (auto j = carcass_head_[cell]; j >= 0; j = carcasses_[j].next) {
            Carcass& c = carcasses_[j];
            if (!c.energy) continue;
            const int dx = c.x - a.x, dy = c.y - a.y, distance = dx * dx + dy * dy;
            const int radius = reach(f.length, c.length_mm);
            if (distance <= radius * radius && distance < nearest) {
                nearest = distance;
                meal = &c;
            }
        }
    });
    if (!meal) return false;
    const int efficiency = std::min(255, 2 * a.genome.gene[agene_meat_gut]);
    const int room = f.store - a.energy;
    const int taken = std::min({meal->energy, f.bite * 8, (room * 255 + efficiency - 1) / efficiency});
    const int absorbed = std::min(room, (taken * efficiency + 127) / 255);
    meal->energy -= taken;
    a.energy += absorbed;
    if (absorbed && meal->killer == a.id && a.hunting == HuntHistory::none) a.hunting = HuntHistory::fed_from_kill;
    if (a.stock == FishStock::large_swimmer) {
        (meal->killer == a.id ? hunts_.own_kill_energy : hunts_.scavenged_energy) += absorbed;
    }
    return absorbed > 0;
}

void World::decay_carcasses() {
    for (auto& c : carcasses_) {
        const auto cell = std::size_t(c.y >> 8) * width() + (c.x >> 8);
        carcass_head_[cell] = -1;
        const int remaining = std::max(0, c.energy - 1 - c.energy / 32);
        const int litter = c.energy / 16 - remaining / 16;
        litter_[cell] = std::uint16_t(std::min(65535, litter_[cell] + litter));
        c.energy = remaining;
    }
    carcasses_.erase(std::remove_if(carcasses_.begin(), carcasses_.end(),
                                    [](const Carcass& c) { return c.energy == 0; }), carcasses_.end());
    for (std::size_t i = 0; i < carcasses_.size(); ++i) index_carcass(i);
}

void World::record_hunter_birth(Animal& young, Animal& mother, Animal& father) {
    for (Animal* parent : {&mother, &father}) {
        if (parent->stock != FishStock::large_swimmer || parent->hunting == HuntHistory::none) continue;
        if (parent->hunting == HuntHistory::fed_from_kill) {
            ++hunts_.breeders;
            parent->hunting = HuntHistory::bred_after_kill;
        }
        const auto chain = std::uint16_t(std::min(65535, 1 + parent->hunting_ancestors));
        young.hunting_ancestors = std::max(young.hunting_ancestors, chain);
    }
    if (young.hunting_ancestors) {
        ++hunts_.young;
        hunts_.generations = std::max(hunts_.generations, young.hunting_ancestors);
        hunts_.last_birth_tick = tick_;
    }
}

} // namespace phd
