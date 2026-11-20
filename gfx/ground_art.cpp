#include "ground_art.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "math.hpp"
#include "palette.hpp"

namespace art::ground {
namespace {

using palette::mix;
using palette::Rgb;

constexpr int tpc = texels_per_cell;
// Painted this far beyond the region asked for, so pebbles and blades that
// straddle an edge are whole on both sides of it.
constexpr int margin = 12;
// Low growth where no plant says what colour it is: a forest floor.
constexpr Rgb floor_pigment{100, 130, 58};

Rgb light(Rgb c, float amount) { return palette::scale(c, 1.0f + amount); }

float noise(int x, int y, int period, std::uint32_t salt) {
    return float(phd::value_noise(x, y, period, salt)) / 65535.0f;
}

// One layer of phd::value_noise over a region, with its lattice hashed once
// instead of four times a texel: the same numbers, several times faster.
class Layer {
public:
    Layer(int tx0, int ty0, int w, int h, int period, std::uint32_t salt) : tx0_(tx0), ty0_(ty0) {
        const int gx0 = phd::floor_div(tx0, period), gy0 = phd::floor_div(ty0, period);
        across_ = phd::floor_div(tx0 + w - 1, period) - gx0 + 2;
        const int down = phd::floor_div(ty0 + h - 1, period) - gy0 + 2;
        lattice_.resize(std::size_t(across_) * down);
        for (int j = 0; j < down; ++j) {
            for (int i = 0; i < across_; ++i) {
                lattice_[std::size_t(j) * across_ + i] = int(phd::hash3(gx0 + i, gy0 + j, salt) & 65535);
            }
        }
        // For each column and each row: its lattice square, and how far across it.
        const auto steps = [period](int from, int count, int g0, std::vector<Step>& out) {
            out.resize(std::size_t(count));
            for (int k = 0; k < count; ++k) {
                const int g = phd::floor_div(from + k, period), t = (from + k - g * period) * 256 / period;
                out[std::size_t(k)] = {g - g0, t * t * (768 - 2 * t) / 65536};
            }
        };
        steps(tx0, w, gx0, columns_);
        steps(ty0, h, gy0, rows_);
    }
    float operator()(int x, int y) const {
        const Step cx = columns_[std::size_t(x - tx0_)], cy = rows_[std::size_t(y - ty0_)];
        const int* at = &lattice_[std::size_t(cy.square) * across_ + cx.square];
        const int top = at[0] + (at[1] - at[0]) * cx.smooth / 256;
        const int bottom = at[across_] + (at[across_ + 1] - at[across_]) * cx.smooth / 256;
        return float(top + (bottom - top) * cy.smooth / 256) / 65535.0f;
    }

private:
    struct Step {
        int square, smooth;
    };
    int tx0_, ty0_, across_ = 0;
    std::vector<int> lattice_;
    std::vector<Step> columns_, rows_;
};

// What the painter needs to know of each cell around the region, gathered
// once, so a texel blends its four nearest cells without asking the world.
struct Cells {
    int x0, y0, w, h;
    std::vector<float> depth, wet, cover, grown, canopy, snow, burnt, frozen, damp;
    std::vector<Rgb> soil, bed, pigment; // pigment is weighted by `grown`

    Cells(const phd::World& world, int cx0, int cy0, int cw, int ch) : x0(cx0), y0(cy0), w(cw), h(ch) {
        const std::size_t n = std::size_t(w) * h;
        for (auto* v : {&depth, &wet, &cover, &grown, &canopy, &snow, &burnt, &frozen, &damp}) {
            v->resize(n);
        }
        soil.resize(n), bed.resize(n), pigment.resize(n);
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                const int cx = std::clamp(x0 + i, 0, world.width() - 1), cy = std::clamp(y0 + j, 0, world.height() - 1);
                const auto cell = std::size_t(cy) * world.width() + cx;
                const auto at = std::size_t(j) * w + i;
                const auto kind = world.water(cell);
                // In decimetres. The sea lies at zero, over ground below it.
                depth[at] = kind == phd::Water::none    ? 0.0f
                            : kind == phd::Water::ocean ? float(std::max(0, -world.elevation(cell)))
                                                        : float(world.water_depth_dm(cell));
                wet[at] = kind != phd::Water::none;
                frozen[at] = world.temperature(cell) < -20 && (kind != phd::Water::ocean || depth[at] < 30);
                canopy[at] = float(world.canopy_cover(cell)) / 255.0f;
                snow[at] = float(world.snow(cell));
                burnt[at] = float(world.burnt(cell)) / 255.0f;
                damp[at] = std::max(0.0f, float(world.soil_water(cell)) - 90) / 255.0f;
                const Rgb ground = palette::substrate[int(world.substrate(cell))];
                soil[at] = ground;
                bed[at] = mix(ground, palette::bed_pale, 0.7f);
                // Low growth: how much of the cell it covers, and its colour.
                Rgb sum{0, 0, 0};
                int plants = 0;
                for (int slot = 0; slot < 4; ++slot) {
                    if (const phd::Plant* p = world.occupant(cell, slot)) {
                        const Rgb c = palette::leaf(*p, world);
                        sum = {sum.r + c.r, sum.g + c.g, sum.b + c.b};
                        ++plants;
                    }
                }
                if (world.tree(cell)) {
                    // Under a tree: a thin floor of whatever green grows nearby.
                    cover[at] = 0.22f;
                    grown[at] = 0.25f;
                } else {
                    cover[at] = 0.2f + 0.2f * float(plants);
                    grown[at] = float(plants) / 4.0f;
                }
                pigment[at] = palette::scale(plants ? palette::scale(sum, 1 / float(plants)) : floor_pigment, grown[at]);
            }
        }
    }

    // Between cell centres, at a point in cells.
    struct At {
        std::size_t a, b, c, d;
        float fx, fy;
    };
    At at(float x, float y) const {
        x = std::clamp(x - 0.5f - float(x0), 0.0f, float(w) - 1.001f);
        y = std::clamp(y - 0.5f - float(y0), 0.0f, float(h) - 1.001f);
        const int i = int(x), j = int(y);
        const auto a = std::size_t(j) * w + i;
        return {a, a + 1, a + std::size_t(w), a + std::size_t(w) + 1, x - float(i), y - float(j)};
    }
    static float blend(const std::vector<float>& v, const At& p) {
        return (v[p.a] * (1 - p.fx) + v[p.b] * p.fx) * (1 - p.fy) + (v[p.c] * (1 - p.fx) + v[p.d] * p.fx) * p.fy;
    }
    static Rgb blend(const std::vector<Rgb>& v, const At& p) {
        const float wa = (1 - p.fx) * (1 - p.fy), wb = p.fx * (1 - p.fy), wc = (1 - p.fx) * p.fy, wd = p.fx * p.fy;
        return {v[p.a].r * wa + v[p.b].r * wb + v[p.c].r * wc + v[p.d].r * wd,
                v[p.a].g * wa + v[p.b].g * wb + v[p.c].g * wc + v[p.d].g * wd,
                v[p.a].b * wa + v[p.b].b * wb + v[p.c].b * wc + v[p.d].b * wd};
    }
};

// The region being painted, with its margin.
struct Patch {
    int tx0, ty0, w, h;
    std::vector<Rgb> ground;  // the ground alone: what details take their colours from
    std::vector<Rgb> picture; // the ground with details over it
    std::vector<float> dampness, grass;

    Patch(int x, int y, int width, int height)
        : tx0(x), ty0(y), w(width), h(height), ground(std::size_t(width) * height), dampness(std::size_t(width) * height),
          grass(std::size_t(width) * height) {}

    bool inside(int tx, int ty) const { return tx >= tx0 && ty >= ty0 && tx < tx0 + w && ty < ty0 + h; }
    std::size_t index(int tx, int ty) const { return std::size_t(ty - ty0) * w + (tx - tx0); }
    void put(int tx, int ty, Rgb color) {
        if (inside(tx, ty)) {
            picture[index(tx, ty)] = color;
        }
    }
};

// The five noises the ground is drawn from, at one texel.
struct Noise {
    float warp_fine, warp_broad, broad, mottling, grain;
};
constexpr int periods[5] = {13, 37, 58, 13, 3};
constexpr std::uint32_t salts[5] = {87, 73, 279, 931, 1489};

struct Ground {
    Rgb color;
    float dampness, grass; // for the details: how wet, and how much turf
    bool water;
};

// The ground at one texel (tx, ty at 32 to the cell): soil, low growth, the
// wet margin, the waterline and the water. Every view's ground comes from
// here; the bird's-eye bake asks for one texel in eight each way.
Ground ground_at(const Cells& cells, int tx, int ty, const Noise& n, bool with_water = true) {
    const float x = (float(tx) + 0.5f) / tpc, y = (float(ty) + 0.5f) / tpc;
    // The shoreline and the edges of growth wander, so cells don't show.
    const float warp = (n.warp_fine - 0.5f) * 0.16f + (n.warp_broad - 0.5f) * 0.22f;
    const auto warped = cells.at(x + warp, y + warp), straight = cells.at(x, y);
    const float depth = Cells::blend(cells.depth, warped), wet = Cells::blend(cells.wet, warped);
    const float cover = Cells::blend(cells.cover, warped), grown = Cells::blend(cells.grown, warped);
    const float canopy = Cells::blend(cells.canopy, straight);

    const float broad = n.broad, mottling = n.mottling, grain = n.grain;
    float lush = std::clamp((cover * 0.9f + broad * 0.62f + mottling * 0.25f - wet * 1.85f - 0.37f) * 1.35f, 0.0f, 1.0f);
    lush *= std::clamp(grown * 5, 0.0f, 1.0f); // nothing green where nothing grows
    lush *= 1 - 0.5f * std::clamp((canopy - 0.25f) * 1.6f, 0.0f, 1.0f); // and less in deep shade
    const float pool = depth + (broad - 0.5f) * 1.1f + (mottling - 0.5f) * 0.7f; // decimetres

    // Soil: its substrate, lighter and darker in drifts, darker when damp.
    const Rgb earth = Cells::blend(cells.soil, warped);
    Rgb soil = mix(palette::scale(earth, 1.18f), palette::scale(earth, 0.85f), (broad * 0.6f + mottling * 0.4f) * 1.3f);
    soil = palette::scale(soil, 1.0f - Cells::blend(cells.damp, straight) * 0.3f);
    soil = mix(soil, palette::litter, std::clamp((canopy - 0.25f) * 1.2f, 0.0f, 0.75f));
    // Low growth, in the colour of the plants that stand here.
    Rgb pigment = Cells::blend(cells.pigment, warped);
    pigment = grown > 0.02f ? palette::scale(pigment, 1 / grown) : floor_pigment;
    Rgb turf = palette::turf(pigment, 0.3f + broad * 0.55f + mottling * 0.25f);
    turf = mix(turf, palette::shaded_turf(pigment), canopy * 0.88f);
    Rgb land = mix(soil, turf, lush);
    land = mix(land, mix(palette::damp_soil, palette::scale(earth, 0.8f), 0.4f), std::clamp((wet - 0.09f) * 3.6f, 0.0f, 0.76f));

    // Burnt ground: char with flecks of ash, fading as it recovers.
    if (const float burnt = Cells::blend(cells.burnt, straight); burnt > 0.01f) {
        land = mix(land, grain > 0.82f ? palette::ash : palette::charred, burnt * 0.85f);
    }
    // Snow: lying in patches that join up as it deepens.
    if (const float snow = Cells::blend(cells.snow, straight); snow > 20) {
        const float lying = std::min(1.0f, (snow - 20) / 70.0f);
        if (mottling * 0.6f + grain * 0.4f < lying * 1.3f) {
            land = mix(land, light(palette::snow, (broad - 0.5f) * 0.06f), std::min(1.0f, lying + 0.3f));
            lush *= 1 - lying;
        }
    }

    // Water: the bed through the shallows, then deepening.
    const Rgb floor = Cells::blend(cells.bed, warped);
    const Rgb bed = mix(floor, palette::scale(floor, 0.78f), mottling * 0.65f + grain * 0.35f);
    Rgb water = palette::shallows(bed, std::max(0.0f, pool) / 10);
    if (Cells::blend(cells.frozen, straight) > 0.5f) {
        water = mix(water, palette::ice, grain > 0.9f ? 0.6f : 0.85f);
    }
    const float edge = wet - 0.34f;
    if (!with_water) {
        water = bed; // the water itself is drawn over this
    }
    Rgb color = mix(land, water, edge * 8.0f);
    if (with_water && edge > 0.015f && edge < 0.052f && mottling > 0.43f) {
        color = mix(color, palette::waterline, 0.47f);
    }
    const float grain_strength = 0.09f - 0.055f * std::clamp(pool / 12.0f, 0.0f, 1.0f);
    return {light(color, (mottling - 0.5f) * 0.09f + (grain - 0.5f) * grain_strength),
            std::max(wet, std::clamp(pool / 8.0f, 0.0f, 2.0f)), lush, edge > 0.06f};
}

void paint_ground(Patch& patch, const Cells& cells, bool with_water) {
    const auto layer = [&](int k) { return Layer(patch.tx0, patch.ty0, patch.w, patch.h, periods[k], salts[k]); };
    const Layer layers[5] = {layer(0), layer(1), layer(2), layer(3), layer(4)};
    for (int ty = patch.ty0; ty < patch.ty0 + patch.h; ++ty) {
        for (int tx = patch.tx0; tx < patch.tx0 + patch.w; ++tx) {
            const Ground g = ground_at(cells, tx, ty, {layers[0](tx, ty), layers[1](tx, ty), layers[2](tx, ty), layers[3](tx, ty), layers[4](tx, ty)}, with_water);
            const auto at = patch.index(tx, ty);
            patch.ground[at] = g.color;
            patch.dampness[at] = g.dampness;
            patch.grass[at] = g.grass;
        }
    }
}

void paint_pebble(Patch& patch, int x, int y, Rgb base, float wet, std::uint32_t hash) {
    const int width_roll = int((hash >> 19) & 7);
    const int rx = 1 + int(width_roll > 4) + int(width_roll == 7), ry = 1 + int(rx == 3);
    const float tone = float((hash >> 25) & 15) / 15.0f;
    const Rgb stone = mix(base, mix({92, 83, 69}, {158, 144, 114}, tone), 0.71f - 0.31f * std::min(wet, 1.4f));
    const float contrast = 1.0f - 0.39f * std::min(1.5f, wet);
    for (int dy = -ry; dy <= ry; ++dy) {
        for (int dx = -rx; dx <= rx; ++dx) {
            const float radius = float(dx * dx) / (float(rx * rx) + 0.4f) + float(dy * dy) / (float(ry * ry) + 0.4f);
            if (radius <= 1.45f) {
                const float shade = dy < 0 ? 0.18f : dy > 0 ? -0.20f : dx == -rx ? 0.07f : 0.0f;
                patch.put(x + dx, y + dy, light(stone, shade * contrast));
            }
        }
    }
}

// Blades of grass on turf, pebbles on bare and wet ground, glints on water.
// Each is placed by the world's own texel grid, so neighbouring regions
// agree about the ones they share.
void paint_details(Patch& patch, bool with_water) {
    patch.picture = patch.ground;
    const int gx0 = phd::floor_div(patch.tx0, 4) * 4 + 3, gy0 = phd::floor_div(patch.ty0, 4) * 4 + 3;
    for (int y = gy0; y + 4 < patch.ty0 + patch.h; y += 4) {
        for (int x = gx0; x + 4 < patch.tx0 + patch.w; x += 4) {
            const auto hash = phd::hash3(x, y, 11675);
            const int ax = x + int(hash & 3), ay = y + int((hash >> 2) & 3);
            if (!patch.inside(ax, ay)) {
                continue;
            }
            const auto at = patch.index(ax, ay);
            const float wet = patch.dampness[at], grass = patch.grass[at];
            const float cluster = noise(x, y, 30, 132);
            const Rgb base = patch.ground[at];
            if (wet < 0.23f && grass > 0.55f) {
                if (float((hash >> 12) & 255) / 255.0f > 0.10f + cluster * 0.32f) {
                    continue;
                }
                const int length = 2 + int((hash >> 18) & 3), lean = int((hash >> 22) & 1) + 1;
                for (int k = 0; k < length; ++k) {
                    patch.put(ax + (k * lean) / length, ay - k, light(base, 0.10f + 0.045f * float(k)));
                }
                if (((hash >> 4) & 3) == 0) {
                    patch.put(ax - 1, ay - 1, light(base, -0.10f));
                    patch.put(ax - 1, ay - 2, light(base, 0.16f));
                }
                continue;
            }
            const float pebbles = wet > 0.15f ? std::max(1.0f, (cluster - 0.28f) * 102.0f) * (1.0f - 0.47f * std::min(1.0f, wet))
                                               : std::max(1.0f, (cluster - 0.45f) * 52.0f);
            if (wet < 1.5f && float((hash >> 10) & 255) < pebbles) {
                paint_pebble(patch, ax, ay, base, wet, hash);
            } else if (with_water && wet > 0.6f && ((hash >> 8) & 511) < (wet > 1.3f ? 3u : 6u)) {
                const int length = 2 + int((hash >> 22) & 7);
                for (int k = 0; k < length; ++k) {
                    patch.put(ax + k, ay + int(k > 3), light(base, 0.1f + 0.045f * float(k < length / 2)));
                }
            }
        }
    }
}

} // namespace

void paint(const phd::World& world, int tx0, int ty0, int w, int h, std::uint8_t* rgba, int stride, bool water) {
    Patch patch(tx0 - margin, ty0 - margin, w + 2 * margin, h + 2 * margin);
    // The cells under it, and one more each way for blending and the warp.
    const int cx0 = phd::floor_div(patch.tx0, tpc) - 2, cy0 = phd::floor_div(patch.ty0, tpc) - 2;
    const int cx1 = phd::floor_div(patch.tx0 + patch.w - 1, tpc) + 2, cy1 = phd::floor_div(patch.ty0 + patch.h - 1, tpc) + 2;
    const Cells cells(world, cx0, cy0, cx1 - cx0 + 1, cy1 - cy0 + 1);
    paint_ground(patch, cells, water);
    paint_details(patch, water);
    const auto byte = [](float v) { return std::uint8_t(std::clamp(v, 0.0f, 255.0f)); };
    for (int y = 0; y < h; ++y) {
        auto* out = rgba + std::size_t(y) * std::size_t(stride);
        const Rgb* in = &patch.picture[patch.index(tx0, ty0 + y)];
        for (int x = 0; x < w; ++x, out += 4) {
            out[0] = byte(in[x].r), out[1] = byte(in[x].g), out[2] = byte(in[x].b), out[3] = 255;
        }
    }
}

void sample(const phd::World& world, int tx0, int ty0, int w, int h, int per_cell, std::uint8_t* rgb, std::uint8_t* water) {
    const int step = tpc / per_cell;
    const int cx0 = phd::floor_div(tx0, per_cell) - 2, cy0 = phd::floor_div(ty0, per_cell) - 2;
    const int cx1 = phd::floor_div(tx0 + w - 1, per_cell) + 2, cy1 = phd::floor_div(ty0 + h - 1, per_cell) + 2;
    const Cells cells(world, cx0, cy0, cx1 - cx0 + 1, cy1 - cy0 + 1);
    const auto byte = [](float v) { return std::uint8_t(std::clamp(v, 0.0f, 255.0f)); };
    // The broad noises from their lattices; the finest has more lattice
    // than there are texels here, so it is asked for directly.
    const auto layer = [&](int k) { return Layer(tx0 * step, ty0 * step, w * step, h * step, periods[k], salts[k]); };
    const Layer layers[4] = {layer(0), layer(1), layer(2), layer(3)};
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            // The middle of this texel, in the painting's own texels.
            const int tx = (tx0 + i) * step + step / 2, ty = (ty0 + j) * step + step / 2;
            const Ground g = ground_at(cells, tx, ty, {layers[0](tx, ty), layers[1](tx, ty), layers[2](tx, ty), layers[3](tx, ty),
                                                       noise(tx, ty, periods[4], salts[4])});
            auto* out = rgb + (std::size_t(j) * w + i) * 3;
            out[0] = byte(g.color.r), out[1] = byte(g.color.g), out[2] = byte(g.color.b);
            water[std::size_t(j) * w + i] = g.water;
        }
    }
}

std::uint32_t signature(const phd::World& world, int cx0, int cy0, int w, int h, bool water) {
    std::uint32_t sum = std::uint32_t(world.season()) + 1;
    for (int cy = std::max(0, cy0 - 1); cy < std::min(world.height(), cy0 + h + 1); ++cy) {
        for (int cx = std::max(0, cx0 - 1); cx < std::min(world.width(), cx0 + w + 1); ++cx) {
            const auto cell = std::size_t(cy) * world.width() + cx;
            // Each is taken as coarsely as the painting shows it.
            std::uint32_t v = std::uint32_t(world.water(cell)) | std::uint32_t(water ? world.water_depth_dm(cell) : 0) << 3;
            v = v * 31 + std::uint32_t(world.snow(cell) / 10);
            v = v * 31 + std::uint32_t(world.burnt(cell) / 24);
            v = v * 31 + std::uint32_t(world.canopy_cover(cell) / 32);
            v = v * 31 + std::uint32_t(std::max(0, world.soil_water(cell) - 90) / 40);
            v = v * 31 + std::uint32_t(world.temperature(cell) < -20) + (world.tree(cell) ? 2u : 0u);
            for (int slot = 0; slot < 4; ++slot) {
                if (const phd::Plant* p = world.occupant(cell, slot)) {
                    v = v * 31 + (p->id & 1023) + 1 + ((p->flags & phd::plant_dormant) ? 4096u : 0u) + (p->health < 140 ? 8192u : 0u);
                } else {
                    v *= 31;
                }
            }
            sum = sum * 0x9e3779b1u + v;
        }
    }
    return sum;
}

} // namespace art::ground
