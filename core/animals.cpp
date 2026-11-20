// Animals: bodies built from organs, moving through land, water and air,
// breathing, burning energy and grazing. They have no brains yet. Until they
// do, each wanders by a fixed rule, turns away from where its body cannot
// go, and eats what it can reach when it is hungry.

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <vector>

#include "math.hpp"
#include "world.hpp"

namespace phd {
namespace {

constexpr std::uint32_t purpose_animals = 5;

// Founders. Each is mutated a little as it is placed.
//                           size bld  legs lleg sprl fins tail wing neck gill plnt meat fat  hue  tone patt ptone matr invs eyes nose ears voic
const AnimalGenome grazer {{ 108, 100, 140, 200,   0,   0,  60,   0, 190,   0, 220,   0,  90,  22, 150, 230, 215,  64, 220, 110, 120, 120,  60}};
const AnimalGenome crawler{{  48,  90, 210, 130, 230,   0, 130,   0,  30,  40, 200,  40,  60,   8, 110,  80,  40,  96, 120,  60, 160,  60, 140}};
const AnimalGenome flier  {{  38, 130,  80, 110,  30,   0, 150, 200,  40,   0, 210,   0,  20, 160,  95, 230, 200,  64, 150, 200,  60, 160, 220}};
const AnimalGenome fish   {{  18, 150,   0,   0,   0, 200, 170,   0,  10, 255, 210,   0,  40,  32, 160, 230, 220,  32,  40,  80, 120,  80,  20}};
const AnimalGenome swimmer{{  78, 110,   0,   0,   0, 220, 230,   0,  10, 255, 200,  60,  70, 140, 130, 150, 225, 128, 200, 120, 120, 100,  90}};

constexpr int mm_per_cell = cell_decimetres * 100;

// A speed in mm a second, as 1/256 of a cell a tick. A tick is a tenth of a second.
int pace_of(int mm_per_second) { return mm_per_second * 256 / (mm_per_cell * 10); }

} // namespace

int genetic_distance(const AnimalGenome& a, const AnimalGenome& b) {
    int sum = 0;
    for (int k = 0; k < agene_count; ++k) {
        sum += std::abs(int(a.gene[k]) - int(b.gene[k]));
    }
    return sum;
}

Frame frame(const AnimalGenome& genome, int growth) {
    const auto g = [&](AnimalGene k) { return int(genome.gene[k]); };
    Frame f{};
    f.length = (200 + g(agene_size) * 9) * growth / 255;
    f.girth = f.length * (12 + g(agene_build) / 12) / 100;
    f.neck = f.length * g(agene_neck) / 640;
    f.tail = f.length * (10 + g(agene_tail) / 4) / 100;
    f.leg_pairs = g(agene_legs) / 64;
    f.leg = f.leg_pairs ? f.length * (20 + g(agene_leg_length) / 3) / 100 : 0;
    f.wing = g(agene_wings) < 96 ? 0 : f.length * (60 + (g(agene_wings) - 96)) / 100;
    f.fin = g(agene_fins) < 64 ? 0 : g(agene_fins);

    // Every organ has mass. The trunk is a cylinder of flesh; the rest is
    // counted against it.
    std::int64_t mass = std::int64_t(3) * f.girth * f.girth * f.length / 1000;
    const std::int64_t trunk = mass;
    mass += std::int64_t(f.leg_pairs) * 2 * 3 * (f.girth / 4) * (f.girth / 4) * f.leg / 1000;
    mass += trunk * f.wing / std::max(1, f.length) / 12;
    mass += trunk * f.fin / 255 / 16;
    mass += trunk * (f.neck + f.tail) / std::max(1, f.length) / 8;
    mass += trunk * (g(agene_plant_gut) + g(agene_meat_gut)) / 255 / 10;
    mass += trunk * g(agene_fat) / 255 / 4;
    mass += trunk * (g(agene_eyes) + g(agene_nose) + g(agene_ears)) / 255 / 20;
    f.mass = int(std::clamp<std::int64_t>(mass, 1, 2000000));
    const auto root = std::int64_t(isqrt(std::uint64_t(f.mass)));
    f.mass75 = std::max(1, int(isqrt(std::uint64_t(root * root * root))));

    f.breathes_air = g(agene_gills) <= 160;
    f.breathes_water = g(agene_gills) >= 96;

    // Legs under the body stride further than legs out to its sides.
    f.land_speed = f.leg_pairs ? pace_of(f.leg * (500 - g(agene_sprawl)) / 220) : 2;
    // Fins and a tail drive a swimmer; legs alone only paddle.
    const int drive = f.fin + g(agene_tail) / 2;
    f.water_speed = f.fin ? pace_of(f.length * drive / 180 + 250) : std::max(2, f.land_speed / 4);
    // Wings carry what they are big enough to lift.
    const bool lifts = f.wing && std::int64_t(f.wing) * f.wing / f.mass >= 20;
    f.air_speed = lifts ? pace_of(3500 + f.wing) : 0;

    f.sight = 3 + g(agene_eyes) / 32;
    f.nose = g(agene_nose) < 32 ? 0 : 128 + g(agene_nose) / 2; // its gain, where 128 is unity
    f.hearing = g(agene_ears) < 32 ? 0 : 4 + g(agene_ears) / 8;
    f.wade = f.leg_pairs ? f.leg * 3 / 4 + f.girth / 2 : f.girth;
    f.upkeep = 1 + f.mass75 / 32 + f.mass75 * (g(agene_eyes) + g(agene_nose) + g(agene_ears)) / 255 / 256; // senses cost a little
    f.store = f.mass75 * (4 + g(agene_fat) / 32) + 64;
    f.bite = std::clamp(f.mass75 / 100, 2, 160); // a mouthful grows with the body, as its needs do
    return f;
}

void World::seed_animals(int count) {
    Random rng(stream_seed(config_.seed, purpose_animals));
    const AnimalGenome* founders[] = {&grazer, &grazer, &crawler, &flier, &fish, &fish, &swimmer};
    const int w = config_.width, h = config_.height;
    // Founders start in families: a few of a kind near each other, since one
    // alone on the far side of the map will never find a mate.
    constexpr int family = 4, kinds = int(std::size(founders));
    std::vector<int> home_x(std::size_t(kinds) * (count / (kinds * family) + 1), -1), home_y(home_x.size(), -1);
    for (int k = 0; k < count && int(animals_.size()) < max_animals; ++k) {
        Animal a{};
        a.genome = *founders[k % int(std::size(founders))];
        for (auto& gene : a.genome.gene) {
            gene = static_cast<std::uint8_t>(std::clamp(int(gene) + int(rng.below(17)) - 8, 0, 255));
        }
        // The body plan is what the founder is; only its proportions vary.
        for (const auto plan : {agene_legs, agene_fins, agene_wings, agene_gills}) {
            a.genome.gene[plan] = founders[k % int(std::size(founders))]->gene[plan];
        }
        const Frame f = frame(a.genome);
        // Somewhere it can live: water deep enough to swim in, or dry land.
        bool placed = false;
        const auto group = std::size_t(k % kinds) + std::size_t(kinds) * std::size_t(k / (kinds * family));
        for (int attempt = 0; attempt < 600 && !placed; ++attempt) {
            int x = 2 + int(rng.below(std::uint32_t(w - 4))), y = 2 + int(rng.below(std::uint32_t(h - 4)));
            if (home_x[group] >= 0 && attempt < 400) {
                // Within a few cells of where the first of its family was put.
                x = std::clamp(home_x[group] + int(rng.below(9)) - 4, 2, w - 3);
                y = std::clamp(home_y[group] + int(rng.below(9)) - 4, 2, h - 3);
            }
            const auto cell = std::size_t(y) * w + x;
            const int depth = Water(water_[cell]) == Water::none ? 0 : depth_[cell] * 100;
            // (A lake, where water plants grow; the sea has nothing for it yet.)
            if (f.breathes_air ? depth == 0 : depth >= f.girth * 4 && Water(water_[cell]) == Water::lake) {
                a.x = x * 256 + 128;
                a.y = y * 256 + 128;
                a.medium = std::uint8_t(f.breathes_air ? Medium::land : Medium::water);
                placed = true;
                if (home_x[group] < 0) {
                    home_x[group] = x;
                    home_y[group] = y;
                }
            }
        }
        if (!placed) {
            continue;
        }
        a.id = next_animal_id_++;
        a.lineage = a.id;
        a.heading = static_cast<std::uint8_t>(rng.below(256));
        a.health = 255;
        a.breath = breath_ticks;
        a.energy = f.store * 3 / 4;
        a.growth = 255;
        animals_.push_back(a);
        brains_.push_back(brain::founder());
    }
}

namespace {

// Every cell an animal could see, nearest first: where it is from the
// animal, how far, and which way as a binary angle.
struct Seen {
    std::int8_t dx, dy;
    std::uint8_t distance, angle;
};
constexpr int farthest_sight = 3 + 255 / 32;

// A binary angle from a direction, in integers, good to a degree or two.
int angle_of(int dx, int dy) {
    const int ax = std::abs(dx), ay = std::abs(dy);
    const int low = std::min(ax, ay), high = std::max(ax, ay);
    const int r = high ? low * 64 / high : 0;                // the slope within an octant, 0..64
    const int within = r * (2048 + 17 * (64 - r)) / 4096;    // 0..32
    const int first = ax >= ay ? within : 64 - within;       // 0..64, from +x toward +y
    const int half = dx >= 0 ? first : 128 - first;
    return (dy >= 0 ? half : 256 - half) & 255;
}

const std::vector<Seen>& sight_table() {
    static const std::vector<Seen> table = [] {
        std::vector<Seen> out;
        for (int d = 1; d <= farthest_sight; ++d) {
            for (int dy = -farthest_sight; dy <= farthest_sight; ++dy) {
                for (int dx = -farthest_sight; dx <= farthest_sight; ++dx) {
                    if (int(isqrt(std::uint64_t(dx * dx + dy * dy) * 4) + 1) / 2 == d) {
                        out.push_back({std::int8_t(dx), std::int8_t(dy), std::uint8_t(d), std::uint8_t(angle_of(dx, dy))});
                    }
                }
            }
        }
        return out;
    }();
    return table;
}

std::int8_t input(int value) { return static_cast<std::int8_t>(std::clamp(value, -127, 127)); }

// Whether a body can go where the water is this deep (mm; -1 off the map).
bool suits(const Frame& f, int depth) {
    if (depth < 0) {
        return false;
    }
    if (!f.breathes_air) {
        return depth >= f.girth * 3; // a fish needs water over its back
    }
    return f.breathes_water || f.fin || depth <= f.wade;
}

} // namespace

void World::index_animals() {
    animal_head_.assign(cells(), -1);
    animal_next_.assign(animals_.size(), -1);
    for (std::size_t k = 0; k < animals_.size(); ++k) {
        const auto cell = std::size_t(animals_[k].y >> 8) * config_.width + std::size_t(animals_[k].x >> 8);
        animal_next_[k] = animal_head_[cell];
        animal_head_[cell] = std::int32_t(k);
    }
}

void World::refresh_forage() {
    forage_.assign(cells(), 0);
    for (std::size_t c = 0; c < cells(); ++c) {
        int sum = 0;
        for (int slot = 0; slot < 4; ++slot) {
            if (const auto at = occupancy_[c * 4 + std::size_t(slot)]) {
                const Plant& p = plants_[at - 1];
                sum += p.height <= small_limit_dm && p.energy > 8 ? p.energy - 8 : 0;
            }
        }
        forage_[c] = static_cast<std::uint16_t>(std::min(sum, 65535));
    }
}

brain::Senses World::senses(std::size_t index) const {
    using namespace brain;
    Senses s{};
    const Animal& me = animals_[index];
    const Frame f = frame(me.genome, me.growth);
    const int w = config_.width, h = config_.height;
    const int cx = me.x >> 8, cy = me.y >> 8;
    if (forage_.size() != cells() || animal_next_.size() != animals_.size()) {
        return s; // nothing indexed yet: it has seen nothing
    }
    const auto depth_of = [&](int x, int y) {
        if (x < 1 || y < 1 || x >= w - 1 || y >= h - 1) {
            return -1;
        }
        const auto cell = std::size_t(y) * w + x;
        return Water(water_[cell]) == Water::none ? 0 : int(depth_[cell]) * 100;
    };
    // Sight: sums over each sector, nearer counting for more, and the
    // nearest animal in each.
    int food[sectors] = {}, hazard[sectors] = {}, near[sectors] = {}, mate[sectors] = {}, who[sectors];
    const bool ready = me.growth == 255 && me.drive >= mating_drive;
    std::fill(who, who + sectors, -1);
    const int range = f.sight;
    for (const Seen& at : sight_table()) {
        if (at.distance > range) {
            break;
        }
        const int turn = std::int8_t(std::uint8_t(at.angle - me.heading)); // -128..127, right if positive
        const int off = std::abs(turn);
        if (off > 96) {
            continue; // behind it
        }
        const int sector = off <= 13 ? ahead : off <= 45 ? (turn > 0 ? right : left) : (turn > 0 ? far_right : far_left);
        const int close = range + 1 - at.distance;
        const int x = cx + at.dx, y = cy + at.dy;
        const int depth = depth_of(x, y);
        if (depth < 0) {
            hazard[sector] += close;
            continue;
        }
        const auto cell = std::size_t(y) * w + x;
        // Grazing counts only where its body can get at it: meadow grass is
        // nothing to a fish, nor waterweed in deep water to a walker.
        if (suits(f, depth)) {
            food[sector] += int(forage_[cell]) * close;
        } else {
            hazard[sector] += close;
        }
        for (std::int32_t j = animal_head_[cell]; j >= 0; j = animal_next_[std::size_t(j)]) {
            const Animal& them = animals_[std::size_t(j)];
            if (!them.health || std::size_t(j) == index) {
                continue;
            }
            if (close > near[sector]) {
                near[sector] = close;
                who[sector] = j;
            }
            if (ready && close > mate[sector] && them.growth == 255 && them.drive >= mating_drive &&
                genetic_distance(me.genome, them.genome) <= breeding_distance) {
                mate[sector] = close;
            }
        }
    }
    const int cube = range * range * range;
    for (int sector = 0; sector < sectors; ++sector) {
        s.in[sector_input(sector, in_food)] = input(food[sector] * 3 / cube);
        s.in[sector_input(sector, in_hazard)] = input(hazard[sector] * 900 / cube);
        s.in[sector_input(sector, in_animal)] = input(near[sector] * 127 / range);
        s.in[sector_input(sector, in_mate)] = input(mate[sector] * 127 / range);
        if (who[sector] >= 0) {
            const Animal& them = animals_[std::size_t(who[sector])];
            s.in[sector_input(sector, in_size)] = input((frame(them.genome, them.growth).length - f.length) / 8);
            s.in[sector_input(sector, in_kin)] = input(std::max(0, 127 - genetic_distance(me.genome, them.genome) / 2));
        }
    }
    // Itself, and the cell its next steps reach, seen alone: sector sums blur it.
    const auto here = std::size_t(cy) * w + std::size_t(cx);
    const int ax = (me.x + icos(me.heading) * 256 / 1024) >> 8, ay = (me.y + isin(me.heading) * 256 / 1024) >> 8;
    const int ahead_depth = depth_of(ax, ay);
    s.in[self_input(self_energy)] = input(std::int64_t(me.energy) * 127 / std::max(1, f.store));
    s.in[self_input(self_health)] = input(me.health / 2);
    s.in[self_input(self_breath)] = input(me.breath * 127 / breath_ticks);
    s.in[self_input(self_youth)] = input((255 - me.growth) * 127 / (255 - newborn_growth));
    s.in[self_input(self_drive)] = input(me.drive / 2);
    s.in[self_input(self_land)] = Medium(me.medium) == Medium::land ? 127 : 0;
    s.in[self_input(self_water)] = Medium(me.medium) == Medium::water ? 127 : 0;
    s.in[self_input(self_air)] = Medium(me.medium) == Medium::air ? 127 : 0;
    s.in[self_input(self_food)] = suits(f, depth_of(cx, cy)) ? input(forage_[here] / 2) : 0;
    s.in[self_input(self_ahead_food)] = suits(f, ahead_depth) ? input(forage_[std::size_t(ay) * w + std::size_t(ax)] / 2) : 0;
    s.in[self_input(self_ahead_hazard)] = suits(f, ahead_depth) ? 0 : 127;
    s.in[self_input(self_light)] = input(daylight() / 2);
    s.in[self_input(self_noise)] = input(int(hash3(int(me.id), int(tick_ / 4), 60013) & 255) - 128);
    for (int m = 0; m < memories; ++m) {
        s.in[self_input(self_memory0 + m)] = me.memory[m];
    }
    // Smell: each scent where it is, and across itself fore and aft and
    // side to side, four cells out, which is what tells it where to turn.
    if (f.nose && !scent_plant_.empty()) {
        const int reach = scent_node * 256;
        const int fx = icos(me.heading) * reach / 1024, fy = isin(me.heading) * reach / 1024;
        const auto smell = [&](const std::vector<std::uint32_t>& field, int which, int scale, std::int64_t like) {
            const auto at = [&](int x, int y) { return std::int64_t(scent_at(field, x, y)) * like / 64 * f.nose / 128; };
            s.in[self_input(which)] = input(int(at(me.x, me.y) / (scale * 4)));
            s.in[self_input(which + 1)] = input(int((at(me.x + fx, me.y + fy) - at(me.x - fx, me.y - fy)) / scale));
            s.in[self_input(which + 2)] = input(int((at(me.x - fy, me.y + fx) - at(me.x + fy, me.y - fx)) / scale)); // right of its heading
        };
        smell(scent_plant_, smell_plant, 8, 64);
        smell(scent_animal_, smell_animal, 4, 64);
        if (ready) {
            // Only a body like its own smells like a mate: the scent carries
            // what made it.
            const std::uint32_t strength = std::max<std::uint32_t>(1, scent_at(scent_ready_, me.x, me.y));
            const int legs = int(scent_at(scent_legs_, me.x, me.y) / strength), water = int(scent_at(scent_water_, me.x, me.y) / strength),
                      wings = int(scent_at(scent_wings_, me.x, me.y) / strength);
            const int unlike = std::abs(legs - me.genome.gene[agene_legs]) +
                               std::abs(water - (me.genome.gene[agene_fins] + me.genome.gene[agene_gills]) / 2) +
                               std::abs(wings - me.genome.gene[agene_wings]);
            smell(scent_ready_, smell_mate, 4, std::max(0, 64 - unlike / 2));
        }
    }
    // Hearing: the loudest call that reaches it, other than its own.
    if (f.hearing) {
        int best = 0;
        const Call* heard = nullptr;
        for (const Call& call : calls_) {
            const std::int64_t dx = call.x - me.x, dy = call.y - me.y;
            const int distance = int(isqrt(std::uint64_t(dx * dx + dy * dy))) >> 8; // cells
            const int loud = call.caller == me.id || distance >= f.hearing ? 0 : call.loud * (f.hearing - distance) / f.hearing;
            if (loud > best) {
                best = loud;
                heard = &call;
            }
        }
        if (heard) {
            const int like = std::max(0, 127 - std::abs(int(heard->pitch) - int(me.genome.gene[agene_voice])) * 4);
            const int toward = angle_of(heard->x - me.x, heard->y - me.y) - me.heading;
            s.in[self_input(heard_loud)] = input(best);
            s.in[self_input(heard_kin)] = input(like);
            s.in[self_input(heard_ahead)] = input(icos(toward) * best / 1024 * like / 127);
            s.in[self_input(heard_right)] = input(isin(toward) * best / 1024 * like / 127);
        }
    }
    return s;
}

// A scent at a point (1/256 of a cell), between the nodes around it.
std::uint32_t World::scent_at(const std::vector<std::uint32_t>& field, int x, int y) const {
    const int span = scent_node * 256;
    const int gx = std::clamp(x - span / 2, 0, (scent_w_ - 1) * span - 1), gy = std::clamp(y - span / 2, 0, (scent_h_ - 1) * span - 1);
    const int i = gx / span, j = gy / span;
    const std::int64_t fx = gx % span, fy = gy % span;
    const auto at = [&](int a, int b) { return std::int64_t(field[std::size_t(b) * scent_w_ + a]); };
    const std::int64_t top = at(i, j) * (span - fx) + at(i + 1, j) * fx, bottom = at(i, j + 1) * (span - fx) + at(i + 1, j + 1) * fx;
    return std::uint32_t((top * (span - fy) + bottom * fy) / (std::int64_t(span) * span));
}

void World::spread_scents() {
    const int w = config_.width, h = config_.height;
    scent_w_ = (w + scent_node - 1) / scent_node;
    scent_h_ = (h + scent_node - 1) / scent_node;
    const auto nodes = std::size_t(scent_w_) * scent_h_;
    for (auto* field : {&scent_plant_, &scent_animal_, &scent_ready_, &scent_legs_, &scent_water_, &scent_wings_}) {
        field->resize(nodes, 0);
    }
    // What gives scent off: grazing, every animal, and every animal ready to mate.
    for (std::size_t c = 0; c < cells(); ++c) {
        if (forage_[c]) {
            scent_plant_[(c / w / scent_node) * scent_w_ + (c % w) / scent_node] += forage_[c] / 8u;
        }
    }
    for (const Animal& a : animals_) {
        const auto node = std::size_t((a.y >> 8) / scent_node) * scent_w_ + std::size_t((a.x >> 8) / scent_node);
        scent_animal_[node] += 256;
        if (a.growth == 255 && a.drive >= mating_drive) {
            scent_ready_[node] += 512;
            scent_legs_[node] += 512u * a.genome.gene[agene_legs];
            scent_water_[node] += 512u * ((a.genome.gene[agene_fins] + a.genome.gene[agene_gills]) / 2u);
            scent_wings_[node] += 512u * a.genome.gene[agene_wings];
        }
    }
    // Each spreads to its neighbours and fades by an eighth.
    scent_scratch_.resize(nodes);
    for (auto* field : {&scent_plant_, &scent_animal_, &scent_ready_, &scent_legs_, &scent_water_, &scent_wings_}) {
        for (int y = 0; y < scent_h_; ++y) {
            for (int x = 0; x < scent_w_; ++x) {
                const auto at = [&](int a, int b) {
                    return std::uint64_t((*field)[std::size_t(std::clamp(b, 0, scent_h_ - 1)) * scent_w_ + std::size_t(std::clamp(a, 0, scent_w_ - 1))]);
                };
                const std::uint64_t mixed = (at(x, y) * 4 + at(x - 1, y) + at(x + 1, y) + at(x, y - 1) + at(x, y + 1)) / 8;
                scent_scratch_[std::size_t(y) * scent_w_ + x] = std::uint32_t(mixed * 7 / 8);
            }
        }
        field->swap(scent_scratch_);
        scent_scratch_.resize(nodes);
    }
}

void World::update_animals() {
    const int w = config_.width, h = config_.height;
    // How deep the water is where an animal is, in mm; -1 off the map.
    const auto depth_at = [&](int x, int y) {
        const int cx = x >> 8, cy = y >> 8;
        if (x < 256 || y < 256 || cx >= w - 1 || cy >= h - 1) {
            return -1;
        }
        const auto cell = std::size_t(cy) * w + cx;
        return Water(water_[cell]) == Water::none ? 0 : int(depth_[cell]) * 100;
    };
    if (animal_next_.size() != animals_.size()) {
        index_animals();
    }
    if (forage_.size() != cells() || tick_ % animal_stagger == 0) {
        refresh_forage();
    }
    if (scent_plant_.empty() || tick_ % scent_every == 0) {
        spread_scents();
    }
    // Calls die away.
    calls_.erase(std::remove_if(calls_.begin(), calls_.end(), [&](const Call& call) { return tick_ - call.tick >= std::uint64_t(call_ticks); }),
                 calls_.end());
    // Young are added as we go, and must not move the rest.
    animals_.reserve(max_animals);
    brains_.reserve(max_animals);
    const std::size_t indexed = animal_next_.size();
    for (std::size_t k = 0; k < animals_.size(); ++k) {
        Animal& a = animals_[k];
        if (!a.health) {
            continue;
        }
        const Frame f = frame(a.genome, a.growth);
        const auto cell = std::size_t(a.y >> 8) * w + std::size_t(a.x >> 8);
        const int depth = depth_at(a.x, a.y);

        // Deciding: senses in, and out comes what it will try to do until
        // it decides again. Everything it does from here on is its brain's.
        if ((tick_ + a.id) % 4 == 0 && k < indexed) {
            const brain::Decision d = brain::think(senses(k), brains_[k]);
            a.turn = d.turn;
            a.pace = d.pace;
            a.wants = static_cast<std::uint8_t>((d.eat ? wants_eat : 0) | (d.mate ? wants_mate : 0) | (d.fly ? wants_fly : 0) | (d.call ? wants_call : 0));
            std::copy(d.memory, d.memory + brain::memories, a.memory);
        }

        // Which medium it is in follows from where it is and whether it flies.
        auto medium = Medium(a.medium);
        if (f.air_speed && (a.wants & wants_fly) && (medium == Medium::air || depth <= f.wade)) {
            medium = Medium::air;
            a.z = static_cast<std::int16_t>(std::min<int>(flying_height_dm, a.z + 1));
        } else if (medium == Medium::air && a.z > 0) {
            a.z = static_cast<std::int16_t>(std::max(0, a.z - 2)); // coming down
        } else {
            medium = depth > f.wade ? Medium::water : Medium::land;
            // A swimmer keeps under the surface, and off the bottom.
            a.z = static_cast<std::int16_t>(medium == Medium::water && f.breathes_water ? std::clamp(depth / 200, 1, 6) : 0);
        }
        a.medium = std::uint8_t(medium);

        // Breath: held while it is where it cannot breathe.
        const bool breathing = medium == Medium::water ? f.breathes_water : f.breathes_air;
        if (breathing) {
            a.breath = static_cast<std::uint16_t>(std::min<int>(breath_ticks, a.breath + 4));
        } else if (a.breath > 0) {
            --a.breath;
        } else if (a.health > 0) {
            --a.health;
        }

        // Calling: heard a long way off, at the pitch of its voice, and louder
        // from a bigger body. It costs a little.
        if ((a.wants & wants_call) && (tick_ + a.id) % call_ticks == 0 && calls_.size() < 1024) {
            calls_.push_back({a.x, a.y, a.id, std::uint32_t(tick_), a.genome.gene[agene_voice],
                              static_cast<std::uint8_t>(std::min(127, 48 + f.length / 32))});
            a.energy -= 1 + f.upkeep / 16;
        }

        // Eating: a mouthful of a plant in reach, if it has room for it.
        bool ate = false;
        if ((a.wants & wants_eat) && medium != Medium::air && a.energy < f.store && a.genome.gene[agene_plant_gut] >= 32) {
            // Anything can graze what grows low. Reach is for browsing what grows taller.
            const int reach = std::max(small_limit_dm, medium == Medium::water ? 0 : (f.leg + f.neck + f.girth) / 100 + 2);
            for (int slot = 0; slot < 4 && !ate; ++slot) {
                const auto at = occupancy_[cell * 4 + std::size_t(slot)];
                if (!at) {
                    continue;
                }
                Plant& p = plants_[at - 1];
                if (p.height > reach || p.energy <= 8) {
                    continue;
                }
                const int taken = std::min(f.bite, p.energy - 4);
                p.energy -= taken;
                forage_[cell] = static_cast<std::uint16_t>(std::max(0, int(forage_[cell]) - taken));
                // Grazing shows: now and then a mouthful takes the top off.
                if (p.height > 2 && (hash3(int(a.id), int(tick_), 9157) & 7) == 0) {
                    --p.height;
                }
                a.energy = std::min(f.store, a.energy + taken * a.genome.gene[agene_plant_gut] / 64);
                ate = true;
            }
        }
        a.eating = ate ? 0 : static_cast<std::uint8_t>(std::min(255, a.eating + 1));

        // Moving: where its brain points it, as fast as its brain says and
        // its body allows. Only the edge of the world stops it.
        int pace = ate ? 0 : a.pace;
        if (medium == Medium::air || (medium == Medium::water && !f.breathes_water && !f.fin)) {
            pace = std::max(pace, 160); // it cannot stop there
        }
        const int top = medium == Medium::land ? f.land_speed : medium == Medium::water ? f.water_speed : f.air_speed;
        const int speed = top * pace / 255;
        a.heading = static_cast<std::uint8_t>(a.heading + a.turn);
        const int nx = a.x + icos(a.heading) * speed / 1024, ny = a.y + isin(a.heading) * speed / 1024;
        if (depth_at(nx, ny) >= 0) {
            a.x = nx;
            a.y = ny;
            a.stride += std::uint32_t(speed);
        }

        // Metabolism: upkeep, and what moving cost since the last pass.
        if ((tick_ + a.id) % animal_stagger == 0) {
            // Swimming on legs and flying are dear; walking and finning are not.
            const int effort = medium == Medium::air ? 4 : medium == Medium::water && !f.fin ? 5 : 1;
            // Upkeep rises with age, so how long a body lasts is for selection to settle.
            const int upkeep = f.upkeep * (128 + std::min<int>(a.age, 384)) / 128;
            a.energy -= upkeep + f.upkeep * effort * pace / 255;

            // Growing up: a fed juvenile grows, at the pace its genes set.
            const auto pass = (tick_ + a.id) / animal_stagger;
            if (a.growth < 255 && a.energy > f.store / 2 && pass % (1 + a.genome.gene[agene_maturity] / 64u) == 0) {
                ++a.growth;
                a.energy -= std::max(1, f.mass75 / 16);
            }
            // The drive to mate rises while a grown animal is well fed, and
            // hunger holds it down: nothing breeds its way out of starving.
            if (a.growth == 255 && a.energy > f.store * 5 / 8) {
                a.drive = static_cast<std::uint8_t>(std::min(255, a.drive + 8));
            } else if (a.energy < f.store / 2) {
                a.drive = static_cast<std::uint8_t>(std::max(0, a.drive - 16));
            }
            // Mating: both must be ready, both must choose to, and they must
            // be near enough alike, and together.
            if (a.growth == 255 && a.drive >= mating_drive && (a.wants & wants_mate) && medium != Medium::air) {
                std::int64_t nearest = std::int64_t(320) * 320;
                std::int32_t mate = -1;
                const int cx = a.x >> 8, cy = a.y >> 8;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = cx + dx, y = cy + dy;
                        if (x < 0 || y < 0 || x >= w || y >= h) {
                            continue;
                        }
                        for (std::int32_t j = animal_head_[std::size_t(y) * w + x]; j >= 0; j = animal_next_[std::size_t(j)]) {
                            const Animal& o = animals_[std::size_t(j)];
                            const std::int64_t ox = o.x - a.x, oy = o.y - a.y;
                            if (std::size_t(j) == k || !o.health || o.growth != 255 || o.drive < mating_drive || !(o.wants & wants_mate) ||
                                Medium(o.medium) == Medium::air || ox * ox + oy * oy >= nearest ||
                                genetic_distance(a.genome, o.genome) > breeding_distance) {
                                continue;
                            }
                            nearest = ox * ox + oy * oy;
                            mate = j;
                        }
                    }
                }
                if (mate >= 0) {
                    Animal& o = animals_[std::size_t(mate)];
                    // This one bears the young. Each takes every gene from
                    // one parent or the other, now and then changed a little,
                    // and a brain built from both of theirs.
                    const int brood = 1 + (255 - a.genome.gene[agene_investment]) / 64;
                    int born = 0;
                    for (int b = 0; b < brood && int(animals_.size()) < max_animals; ++b) {
                        Animal young{};
                        for (int gene = 0; gene < agene_count; ++gene) {
                            const std::uint32_t roll = hash3(int(a.id) + b * 7919, int(tick_) + gene * 131, o.id);
                            int value = (roll & 1) ? a.genome.gene[gene] : o.genome.gene[gene];
                            if (((roll >> 1) & 7) == 0) {
                                value += int((roll >> 4) % 13) - 6;
                            }
                            young.genome.gene[gene] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
                        }
                        const Frame small = frame(young.genome, newborn_growth);
                        // What it starts with comes out of its mother, who keeps enough to live on.
                        const int cost = small.store / 2 + small.mass75 / 4;
                        if (a.energy - cost < f.store / 3) {
                            break;
                        }
                        a.energy -= cost;
                        young.id = next_animal_id_++;
                        young.lineage = a.lineage;
                        young.mother = a.id;
                        young.father = o.id;
                        young.x = a.x;
                        young.y = a.y;
                        young.heading = static_cast<std::uint8_t>(hash3(int(young.id), int(tick_), 7717));
                        young.medium = a.medium;
                        young.health = 255;
                        young.breath = breath_ticks;
                        young.growth = newborn_growth;
                        young.energy = small.store / 2;
                        std::uint32_t state = hash3(int(young.id), int(tick_), 0x51ed27u);
                        brain::Brain mind;
                        brain::combine(mind, brains_[k], brains_[std::size_t(mate)], state);
                        if (brain_mode_ == BrainMode::evolving) {
                            brain::mutate(mind, state);
                        } else if (brain_mode_ == BrainMode::shuffled) {
                            brain::shuffle(mind, state);
                        }
                        animals_.push_back(young);
                        brains_.push_back(mind);
                        ++born;
                    }
                    a.young = static_cast<std::uint16_t>(std::min(65535, a.young + born));
                    o.young = static_cast<std::uint16_t>(std::min(65535, o.young + born));
                    a.drive = 0;
                    o.drive = 0;
                    o.energy -= frame(o.genome, o.growth).store / 16;
                }
            }
            if (a.energy <= 0 && a.health > 0) {
                a.health = static_cast<std::uint8_t>(std::max(0, a.health - 16));
                a.energy = 0;
            }
            if ((tick_ + a.id) % (animal_stagger * 15) == 0 && a.age < 65535) {
                ++a.age; // about a day
            }
        }
        if (a.health == 0) {
            // It dies where it is. What it was goes to the soil.
            litter_[cell] = static_cast<std::uint16_t>(std::min(65535, litter_[cell] + f.mass / 2000));
        }
    }
    // The dead leave, and the rest close up, keeping their order.
    std::size_t kept = 0;
    for (std::size_t k = 0; k < animals_.size(); ++k) {
        if (animals_[k].health) {
            if (kept != k) {
                animals_[kept] = animals_[k];
                brains_[kept] = brains_[k];
            }
            ++kept;
        }
    }
    animals_.resize(kept);
    brains_.resize(kept);
    index_animals();
}

} // namespace phd
