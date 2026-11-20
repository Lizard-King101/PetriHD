// Plants: every one an individual with its own genome, growing on light,
// water, warmth and nutrients, competing for space and shade, seeding,
// crossing and mutating, dying back into the soil. And fire.

#include <algorithm>

#include "math.hpp"
#include "world.hpp"

namespace phd {
namespace {

// Founders: what the valley starts with, each mutated a little so selection
// has variation to work on from the first year.
//                       hgt  wdy  life root drt  cold shd  evg  seed frt  fld  bark sero run  leaf flwr
const PlantGenome grass  {{ 40,  20, 110,  60, 120, 150,  60, 100,  40,  30,  70,  20,   0, 150, 100, 200}};
const PlantGenome herb   {{ 25,  10,  20,  40,  90, 110,  70,  60,  30,  40,  50,  10,   0,  40, 110, 255}};
const PlantGenome reed   {{ 90,  40, 120, 100,  30, 140,  40,  60,  40,  20, 240,  20,   0, 220, 130,  60}};
const PlantGenome shrub  {{ 70, 200, 150, 140, 170, 140,  90, 150, 120, 200,  40,  80,  20,  60,  80, 170}};
const PlantGenome broadleaf{{200, 230, 220, 160, 100, 150, 120,  40, 160, 180,  40, 120,  20,  20,  90,  80}};
const PlantGenome conifer{{210, 240, 230, 120, 120, 230, 150, 240,  90,  20,  30, 140, 160,  10, 150,  40}};
const PlantGenome palm   {{190, 220, 200, 180, 130,  60, 100, 230, 170, 220,  80, 100,   0,  30,  70, 150}};

enum Founder : std::uint32_t { founder_grass = 1, founder_herb, founder_reed, founder_shrub, founder_broadleaf,
                               founder_conifer, founder_palm };

const PlantGenome& founder_genome(std::uint32_t f) {
    switch (f) {
    case founder_herb: return herb;
    case founder_reed: return reed;
    case founder_shrub: return shrub;
    case founder_broadleaf: return broadleaf;
    case founder_conifer: return conifer;
    case founder_palm: return palm;
    default: return grass;
    }
}

constexpr int cross_distance = 700; // genomes further apart than this can't cross

// Trees need room: a woody plant can only take a whole cell if no more than
// this many other trees stand within two cells of it. Forests still close
// their canopy, but trunks stand a few metres apart, not in every cell.
constexpr int tree_spacing = 2;
constexpr int trees_nearby = 1;
constexpr int snag_least_dm = 50; // a dead tree shorter than 5 m just falls

} // namespace

bool World::room_for_tree(int cell) const {
    const int w = config_.width, h = config_.height, cx = cell % w, cy = cell / w;
    int near = 0;
    for (int dy = -tree_spacing; dy <= tree_spacing; ++dy) {
        for (int dx = -tree_spacing; dx <= tree_spacing; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= w || y >= h) {
                continue;
            }
            // In halves: a living tree is a whole one, a snag half, the
            // snag in this very cell included. There is room beside one
            // tree and one snag, but not more.
            const auto i = std::size_t(y) * w + x;
            near += (dx || dy) && tree(i) ? 2 : snag_height_[i] ? 1 : 0;
            if (near > trees_nearby * 2 + 1) {
                return false;
            }
        }
    }
    return true;
}

std::int32_t World::place(const PlantGenome& genome, std::uint32_t lineage, int cell, int slot, int height) {
    if (free_.empty()) {
        return -1;
    }
    const auto index = free_.back();
    free_.pop_back();
    Plant& p = plants_[index];
    p = Plant{};
    p.id = next_plant_id_++;
    p.lineage = lineage;
    p.cell = cell;
    p.slot = static_cast<std::uint8_t>(slot);
    p.health = 255;
    p.height = static_cast<std::uint16_t>(std::max(1, height));
    p.energy = 20 + genome.gene[gene_seed] / 4;
    p.genome = genome;
    const auto base = std::size_t(cell) * 4;
    if (slot == whole_cell) {
        for (int s = 0; s < 4; ++s) {
            occupancy_[base + std::size_t(s)] = index + 1;
        }
    } else {
        occupancy_[base + std::size_t(slot)] = index + 1;
    }
    return std::int32_t(index);
}

void World::kill_plant(std::uint32_t index) {
    Plant& p = plants_[index];
    const auto base = std::size_t(p.cell) * 4;
    for (int s = 0; s < 4; ++s) {
        if (occupancy_[base + std::size_t(s)] == index + 1) {
            occupancy_[base + std::size_t(s)] = 0;
        }
    }
    // What it was becomes litter: fuel for fires, then nutrients. A tree of
    // any size leaves its trunk standing as a snag, where it stood, and only
    // its leaves and branches fall now.
    int mass = woody(p.genome) ? p.height * 4 : p.height;
    if (p.slot == whole_cell && p.height >= snag_least_dm) {
        const auto c = std::size_t(p.cell);
        snag_height_[c] = static_cast<std::uint8_t>(std::min<int>(255, p.height));
        snag_rot_[c] = 255;
        snag_place_[c] = static_cast<std::uint16_t>(hash3(int(p.id), int(p.id >> 16), tree_place_salt));
        mass /= 2;
    }
    litter_[std::size_t(p.cell)] = static_cast<std::uint16_t>(std::min(65535, litter_[std::size_t(p.cell)] + mass));
    p.id = 0;
    free_.push_back(index);
}

void World::seed_plants() {
    const int w = config_.width, h = config_.height;
    auto& rng = plant_rng_;
    const auto mutate = [&](PlantGenome g) {
        for (auto& v : g.gene) {
            v = static_cast<std::uint8_t>(std::clamp(int(v) + int(rng.below(25)) - 12, 0, 255));
        }
        return g;
    };
    const auto put = [&](std::uint32_t founder, int cell, int slot) {
        const auto g = mutate(founder_genome(founder));
        const int top = max_height(g);
        const int height = slot == whole_cell ? std::max(small_limit_dm + 1, top * int(40 + rng.below(61)) / 100)
                                              : std::max(1, top * int(30 + rng.below(71)) / 100);
        const auto index = place(g, founder, cell, slot, height);
        if (index >= 0) {
            Plant& p = plants_[std::size_t(index)];
            p.age = static_cast<std::uint16_t>(rng.below(std::uint32_t(std::min(lifespan(g) / 2, 60000)) + 1));
            p.energy = slot == whole_cell ? 300 : 60;
        }
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cell = y * w + x;
            const auto i = std::size_t(cell);
            const auto kind = Water(water_[i]);
            if (kind == Water::ocean) {
                continue;
            }
            const int roll = int(rng.below(100));
            if (kind != Water::none) {
                if (depth_[i] < 10 && roll < 60) {
                    put(founder_reed, cell, int(rng.below(4)));
                }
                continue;
            }
            const int t = mean_temperature(i), m = moisture_[i] >> 8;
            const auto ground = Substrate(terrain_.substrate[i]);
            // What could grow here, from the climate and the ground.
            std::uint32_t tree = 0;
            int tree_chance = 0, slots = 3, shrubs = 10;
            std::uint32_t ground_plant = founder_grass;
            if (ground == Substrate::rock) {
                slots = roll < 30 ? 1 : 0;
            } else if (ground == Substrate::sand && terrain_.sea_distance[i] <= 6) {
                slots = 2;
            } else if (m >= 190 || ground == Substrate::peat) {
                ground_plant = founder_reed;
                tree = t >= 60 ? founder_broadleaf : founder_conifer;
                tree_chance = 10;
            } else if (t < 60) {
                tree = founder_conifer;
                tree_chance = m >= 90 ? 50 : 6;
            } else if (t < 190) {
                tree = founder_broadleaf;
                tree_chance = m >= 130 ? 55 : m >= 85 ? 15 : 3;
                shrubs = m < 85 ? 25 : 10;
            } else {
                tree = m >= 150 ? founder_broadleaf : founder_palm;
                tree_chance = m >= 150 ? 70 : m >= 100 ? 30 : m >= 60 ? 6 : 0;
                shrubs = m < 60 ? 30 : 10;
                slots = m < 60 ? 1 : 3;
            }
            if (tree && roll < tree_chance && room_for_tree(cell)) {
                put(tree, cell, whole_cell);
                continue;
            }
            for (int s = 0; s < slots; ++s) {
                const int pick = int(rng.below(100));
                put(pick < shrubs ? founder_shrub : pick < shrubs + 8 ? founder_herb : ground_plant, cell, s);
            }
        }
    }
    update_canopy();
}

void World::update_canopy() {
    std::fill(canopy_height_.begin(), canopy_height_.end(), 0);
    std::fill(canopy_cover_.begin(), canopy_cover_.end(), 0);
    const int w = config_.width, h = config_.height;
    for (const auto& p : plants_) {
        if (!p.id || p.slot != whole_cell) {
            continue;
        }
        const int r16 = crown16(p.height), cx = p.cell % w, cy = p.cell / w;
        const int reach = (r16 + 15) / 16;
        const int density = p.flags & plant_dormant ? 90 : 210; // bare branches let light through
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= w || y >= h) {
                    continue;
                }
                const int d16 = int(isqrt(std::uint64_t((dx * dx + dy * dy) * 256)));
                if (d16 > r16) {
                    continue;
                }
                const auto i = std::size_t(y) * w + x;
                canopy_height_[i] = std::max<std::uint16_t>(canopy_height_[i], p.height);
                canopy_cover_[i] = static_cast<std::uint8_t>(
                    std::min(250, canopy_cover_[i] + density * (r16 - d16 / 2) / std::max(1, r16)));
            }
        }
    }
}

void World::update_plants() {
    const auto n = std::uint32_t(plants_.size());
    for (auto index = std::uint32_t(tick_ % plant_stagger); index < n; index += plant_stagger) {
        if (plants_[index].id) {
            grow_plant(index);
        }
    }
    if (tick_ % 64 == 0) {
        update_canopy();
    }
}

void World::grow_plant(std::uint32_t index) {
    Plant& p = plants_[index];
    const auto& g = p.genome.gene;
    const auto c = std::size_t(p.cell);
    const int t = temperature_[c];
    const auto kind = Water(water_[c]);
    const bool tree_slot = p.slot == whole_cell;

    // Leafless or withered in the cold, unless evergreen.
    const bool dormant = !evergreen(p.genome) && t < 60;
    p.flags = static_cast<std::uint8_t>((p.flags & ~(plant_dormant | plant_flowering)) | (dormant ? plant_dormant : 0));

    int damage = 0;
    // Frost: a plant takes cold down to its tolerance; dormant ones a little more.
    const int frost = -int(g[gene_cold]) - (dormant ? 60 : 0);
    if (t < frost) {
        damage += (frost - t) / 10 + 2;
    }
    // Flooding: its roots drown in water deeper than it can stand, however
    // far its top is above the surface; the more so the deeper. The flood
    // gene is its affinity for water: how deep it can stand in it.
    if (kind != Water::none) {
        const int over = kind == Water::ocean ? 16 : depth_[c] - g[gene_flood] / 16;
        if (over > 0) {
            damage += std::min(8, 2 + over);
        }
    }

    // Light, less the shade of taller crowns, by how well it takes shade.
    int shade = 0;
    if (canopy_height_[c] > p.height + (tree_slot ? 10 : 0)) {
        shade = tree_slot ? canopy_cover_[c] / 2 : canopy_cover_[c];
    }
    if (!tree_slot) {
        for (int s = 0; s < 4; ++s) {
            const auto other = occupancy_[c * 4 + std::size_t(s)];
            if (other && other != index + 1 && plants_[other - 1].height > p.height) {
                shade += 30;
            }
        }
    }
    shade = std::min(shade, 245) * (320 - g[gene_shade]) / 320;
    int light = daylight() * (256 - shade) / 256;
    if (kind != Water::none && depth_[c] > p.height) {
        light = light * 12 / (12 + depth_[c] - p.height); // under water, dimmer the deeper
    }

    // Water: the soil's, and what deep roots reach in the ground below.
    int available = soil_[c] + (moisture_[c] >> 8) * g[gene_roots] / 384;
    if (kind != Water::none) {
        available = 255;
    }
    // A plant made for water needs wet ground when it stands on land.
    const int need = 30 + (255 - g[gene_drought]) / 2 + (kind == Water::none ? g[gene_flood] / 4 : 0);
    const int water = std::min(256, available * 256 / need);
    if (available * 3 < need * 2) {
        damage += 1 + (need * 2 - available * 3) / 40; // wilting
    }
    const int warmth = t <= 0 ? 0 : t < 220 ? t * 256 / 220 : std::max(64, 256 - (t - 220) * 2);
    const int fertility = 128 + nutrients_[c] / 2;

    const int leaf = woody(p.genome) ? 8 + p.height / 6 : 4 + p.height / 2;
    const int made = dormant ? 0
                             : int(std::int64_t(std::min(leaf, 60)) * 4 * light / 256 * water / 256 * warmth / 256 *
                                   fertility / 256);
    int upkeep = 1 + p.height * (woody(p.genome) ? 1 : 2) / 28 + (g[gene_roots] + g[gene_bark] + g[gene_drought] +
                                                                 g[gene_cold] + g[gene_flood]) / 400;
    if (dormant) {
        upkeep /= 4; // living on what it stored
    } else if (t < 60) {
        upkeep /= 2; // evergreen, hardened against the cold
    }
    p.energy = std::clamp(p.energy + made - upkeep, -200, 4000);
    if (made > 0 && kind == Water::none) {
        soil_[c] = static_cast<std::uint8_t>(std::max(0, soil_[c] - made / 6)); // transpiration
    }

    // Health: hurt by frost, flood, drought and hunger; mends when fed.
    if (p.energy < -50) {
        damage += 3; // starving
    }
    p.health = static_cast<std::uint8_t>(damage ? std::max(0, p.health - damage)
                                               : std::min(255, p.health + (p.energy > 0 ? 3 : 0)));
    ++p.age;
    if (p.health == 0 || p.age > lifespan(p.genome)) {
        kill_plant(index);
        return;
    }

    // Growth, and room for it: past 1.5 m a woody plant needs the whole cell.
    const int top = max_height(p.genome);
    const int cost = woody(p.genome) ? 6 + p.height / 12 : 3;
    if (!dormant && p.energy > cost * 2 && p.height < top && nutrients_[c] > 0) {
        bool room = true;
        if (!tree_slot && p.height >= small_limit_dm) {
            room = false;
            if (woody(p.genome)) {
                bool clear = true;
                for (int s = 0; s < 4; ++s) {
                    const auto other = occupancy_[c * 4 + std::size_t(s)];
                    clear = clear && (!other || other == index + 1);
                }
                if (clear && room_for_tree(int(c))) {
                    for (int s = 0; s < 4; ++s) {
                        occupancy_[c * 4 + std::size_t(s)] = index + 1;
                    }
                    if (snag_height_[c]) {
                        // The new tree takes the old one's place: the snag comes down.
                        litter_[c] = static_cast<std::uint16_t>(std::min(65535, litter_[c] + snag_height_[c] * 2));
                        snag_height_[c] = 0;
                    }
                    p.slot = whole_cell;
                    room = true;
                }
            }
        }
        if (room) {
            p.height = static_cast<std::uint16_t>(std::min(top, p.height + (woody(p.genome) ? 1 : 2)));
            p.energy -= cost;
            if ((p.height & 3) == 0) {
                nutrients_[c] = static_cast<std::uint8_t>(nutrients_[c] - 1);
            }
        }
    }

    // Spring and summer: flowers, then seeds from what is spare.
    if (p.age >= maturity(p.genome) && !dormant && season() <= 1) {
        p.flags |= plant_flowering;
        if (p.energy > 60 + p.height) {
            reproduce(index);
        }
    }
}

void World::reproduce(std::uint32_t index) {
    // Copied out first: establishing seedlings can't touch this plant, but
    // keep the parent's values stable regardless.
    const Plant parent = plants_[index];
    const auto& g = parent.genome.gene;
    auto& rng = plant_rng_;
    const int w = config_.width, h = config_.height;
    const int cost = 4 + g[gene_seed] / 8;
    const int spare = plants_[index].energy - (60 + parent.height);
    const int count = std::min(6, spare / cost);
    if (count <= 0) {
        return;
    }
    plants_[index].energy -= count * cost;
    const int px = parent.cell % w, py = parent.cell / w;

    // Pollen from a nearby plant close enough to cross with, if there is one.
    const Plant* donor = nullptr;
    for (int attempt = 0; attempt < 4 && !donor; ++attempt) {
        const int dx = int(rng.below(7)) - 3, dy = int(rng.below(7)) - 3;
        const int x = px + dx, y = py + dy;
        if (x < 0 || y < 0 || x >= w || y >= h) {
            continue;
        }
        const auto other = occupancy_[(std::size_t(y) * w + x) * 4 + rng.below(4)];
        if (other && other != index + 1 &&
            genetic_distance(plants_[other - 1].genome, parent.genome) < cross_distance) {
            donor = &plants_[other - 1];
        }
    }

    for (int k = 0; k < count; ++k) {
        PlantGenome child = parent.genome;
        if (donor) {
            const auto mask = rng.next();
            for (int i = 0; i < gene_count; ++i) {
                if (mask >> i & 1) {
                    child.gene[i] = donor->genome.gene[i];
                }
            }
        }
        for (auto& v : child.gene) {
            if (rng.below(48) == 0) {
                v = static_cast<std::uint8_t>(std::clamp(int(v) + int(rng.below(33)) - 16, 0, 255));
            }
        }
        // Fire-adapted plants hold some seeds until a fire.
        if (g[gene_serotiny] > 128 && int(rng.below(256)) < g[gene_serotiny]) {
            plants_[index].seeds = static_cast<std::uint8_t>(std::min(255, plants_[index].seeds + 1));
            continue;
        }
        // Wind carries small seeds far, mostly downwind; fruit falls close
        // (until there are animals to carry it).
        int distance, angle;
        if (fruiting(parent.genome)) {
            distance = 1 + int(rng.below(3));
            angle = int(rng.below(256));
        } else {
            distance = 1 + int(rng.below(std::uint32_t(2 + (255 - g[gene_seed]) / 24)));
            angle = wind_angle_ + int(rng.below(129)) - 64;
        }
        const int x = px + icos(angle) * distance / 1024, y = py + isin(angle) * distance / 1024;
        if (x >= 0 && y >= 0 && x < w && y < h) {
            establish(child, parent.lineage, y * w + x, false);
        }
    }
    // Runners: a clone in a neighbouring cell.
    if (g[gene_runners] > 150 && int(rng.below(256)) < g[gene_runners] - 120) {
        const int x = px + int(rng.below(3)) - 1, y = py + int(rng.below(3)) - 1;
        if (x >= 0 && y >= 0 && x < w && y < h) {
            establish(parent.genome, parent.lineage, y * w + x, true);
        }
    }
}

bool World::establish(const PlantGenome& genome, std::uint32_t lineage, int cell, bool clone) {
    const auto c = std::size_t(cell);
    const auto kind = Water(water_[c]);
    if (kind == Water::ocean || (kind != Water::none && depth_[c] > genome.gene[gene_flood] / 16)) {
        return false;
    }
    int slot = -1;
    for (int s = 0, start = int(plant_rng_.below(4)); s < 4; ++s) {
        if (!occupancy_[c * 4 + std::size_t((start + s) % 4)]) {
            slot = (start + s) % 4;
            break;
        }
    }
    if (slot < 0) {
        return false;
    }
    // Big seeds and runners take hold in shade; small seeds need light.
    const int chance = (clone ? 160 : 70 + genome.gene[gene_seed] / 2) - canopy_cover_[c] * (300 - genome.gene[gene_shade]) / 1200;
    if (int(plant_rng_.below(256)) >= chance) {
        return false;
    }
    return place(genome, lineage, cell, slot, 1) >= 0;
}

int World::fuel(std::size_t i) const {
    int mass = litter_[i] / 4 + snag_height_[i] / 4;
    for (int s = 0; s < 4; ++s) {
        if (const Plant* p = occupant(i, s)) {
            mass += p->slot == whole_cell ? p->height / 2 : p->height * 6;
            if (p->slot == whole_cell) {
                break;
            }
        }
    }
    return std::min(255, mass);
}

bool World::ignite(std::size_t i) {
    if (Water(water_[i]) != Water::none || fire_[i] || fuel(i) < 20) {
        return false;
    }
    fire_[i] = static_cast<std::uint8_t>(std::min(12, 4 + fuel(i) / 24));
    return true;
}

void World::update_fire() {
    const int w = config_.width, h = config_.height;
    fire_next_ = fire_;
    const int wind_x = icos(wind_angle_), wind_y = isin(wind_angle_);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = std::size_t(y) * w + x;
            if (!fire_[i]) {
                if (burnt_[i] && tick_ % 32 == 0) {
                    --burnt_[i];
                }
                continue;
            }
            // Heavy rain puts it out.
            const int rain = rain_at(x, y);
            if (rain > 10 && int(fire_rng_.below(40)) < rain) {
                fire_next_[i] = 0;
                continue;
            }
            // A snag already standing burns away. (A tree this fire kills
            // leaves a new one, charred, for the next fire.)
            snag_height_[i] = 0;
            // Burn what is here: small plants die; trees with thick bark lose
            // their tops and live, the rest die. Fire-held seeds are released.
            for (int s = 0; s < 4; ++s) {
                const auto at = occupancy_[i * 4 + std::size_t(s)];
                if (!at) {
                    continue;
                }
                Plant& p = plants_[at - 1];
                const int held = p.seeds;
                const auto genome = p.genome;
                const auto lineage = p.lineage;
                if (p.slot == whole_cell && p.genome.gene[gene_bark] > 150) {
                    p.height = static_cast<std::uint16_t>(std::max(small_limit_dm + 1, p.height / 2));
                    p.health = static_cast<std::uint8_t>(std::max(1, p.health - 90));
                    p.flags |= plant_scorched;
                    p.seeds = 0;
                } else {
                    kill_plant(at - 1);
                }
                for (int k = 0; k < std::min(held, 6); ++k) {
                    const int sx = x + int(fire_rng_.below(5)) - 2, sy = y + int(fire_rng_.below(5)) - 2;
                    if (sx >= 0 && sy >= 0 && sx < w && sy < h) {
                        establish(genome, lineage, sy * w + sx, false);
                    }
                }
                if (p.slot == whole_cell) {
                    break;
                }
            }
            nutrients_[i] = static_cast<std::uint8_t>(std::min(255, nutrients_[i] + litter_[i] / 64 + 20)); // ash
            litter_[i] = 0;
            burnt_[i] = 255;
            // Spread to dry neighbours with fuel, more readily downwind.
            for (int k = 0; k < 8; ++k) {
                const int dx = k < 3 ? k - 1 : k < 5 ? (k == 3 ? -1 : 1) : k - 6, dy = k < 3 ? -1 : k < 5 ? 0 : 1;
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    continue;
                }
                const auto j = std::size_t(ny) * w + nx;
                if (fire_[j] || fire_next_[j] || Water(water_[j]) != Water::none || burnt_[j] > 200) {
                    continue;
                }
                const int dry = std::max(0, 200 - soil_[j]);
                const int downwind = 1024 + (dx * wind_x + dy * wind_y) * wind_speed_ / 48;
                const int chance = int(std::int64_t(fuel(j)) * 4 * dry / 200 * std::max(0, downwind) / 1024);
                if (int(fire_rng_.below(1024)) < chance) {
                    fire_next_[j] = static_cast<std::uint8_t>(std::min(12, 4 + fuel(j) / 24));
                }
            }
            fire_next_[i] = static_cast<std::uint8_t>(fire_[i] - 1);
        }
    }
    fire_.swap(fire_next_);
}

} // namespace phd
