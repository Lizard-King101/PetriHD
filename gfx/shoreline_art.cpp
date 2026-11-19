#include "shoreline_art.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "math.hpp"
#include "world.hpp"

namespace gfx::shoreline {
namespace {

constexpr int size = extent * texels_per_cell;

struct Rgb {
    float r, g, b;
};

Rgb blend(Rgb a, Rgb b, float amount) {
    const float t = std::clamp(amount, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb light(Rgb color, float amount) {
    const float factor = 1.0f + amount;
    return {color.r * factor, color.g * factor, color.b * factor};
}

float noise(int x, int y, int period, std::uint32_t salt) {
    return float(phd::value_noise(x, y, period, salt)) / 65535.0f;
}

struct Patch {
    std::vector<std::uint8_t> rgb = std::vector<std::uint8_t>(size * size * 3);
    std::vector<float> dampness = std::vector<float>(size * size);
    std::vector<float> grass = std::vector<float>(size * size);

    Rgb get(int x, int y) const {
        const auto* p = &rgb[3 * (y * size + x)];
        return {float(p[0]), float(p[1]), float(p[2])};
    }

    void put(int x, int y, Rgb color) {
        if (x < 0 || x >= size || y < 0 || y >= size) {
            return;
        }
        auto* p = &rgb[3 * (y * size + x)];
        p[0] = std::uint8_t(std::clamp(color.r, 0.0f, 255.0f));
        p[1] = std::uint8_t(std::clamp(color.g, 0.0f, 255.0f));
        p[2] = std::uint8_t(std::clamp(color.b, 0.0f, 255.0f));
    }
};

// Interpolate between cell centres. At the map boundary the last sample is
// extended, including when the shoreline warp reaches past the patch.
template <class Sample>
float field(const phd::World& world, float x, float y, Sample sample) {
    x -= 0.5f;
    y -= 0.5f;
    const int cx = int(x), cy = int(y);
    const float fx = x - cx, fy = y - cy;
    const auto at = [&](int dx, int dy) {
        const int col = std::clamp(cx + dx, 0, world.width() - 1);
        const int row = std::clamp(cy + dy, 0, world.height() - 1);
        return float(sample(row * world.width() + col));
    };
    return (at(0, 0) * (1 - fx) + at(1, 0) * fx) * (1 - fy)
         + (at(0, 1) * (1 - fx) + at(1, 1) * fx) * fy;
}

void paint_ground(Patch& patch, const phd::World& world) {
    const auto cover_at = [&](int cell) {
        if (world.tree(cell)) {
            return 0.22f;
        }
        float cover = 0.2f;
        for (int slot = 0; slot < 4; ++slot) {
            if (world.occupant(cell, slot)) {
                cover += 0.2f;
            }
        }
        return cover;
    };
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const int tx = left * texels_per_cell + px, ty = top * texels_per_cell + py;
            const float x = (tx + 0.5f) / texels_per_cell, y = (ty + 0.5f) / texels_per_cell;
            const float warp = (noise(tx, ty, 13, 87) - 0.5f) * 0.16f + (noise(tx, ty, 37, 73) - 0.5f) * 0.22f;
            const float depth = field(world, x + warp, y + warp, [&](int i) { return world.water_depth_dm(i); });
            const float wet = field(world, x + warp, y + warp, [&](int i) { return world.water(i) != phd::Water::none; });
            const float cover = field(world, x + warp, y + warp, cover_at);
            const float canopy = field(world, x, y, [&](int i) { return world.canopy_cover(i) / 255.0f; });

            const float broad = noise(tx, ty, 58, 279), mottling = noise(tx, ty, 13, 931), grain = noise(tx, ty, 3, 1489);
            const float lush = std::clamp((cover * 0.9f + broad * 0.62f + mottling * 0.25f - wet * 1.85f - 0.37f) * 1.35f, 0.0f, 1.0f);
            const float pool = depth + (broad - 0.5f) * 1.1f + (mottling - 0.5f) * 0.7f;

            const Rgb soil = blend({142, 114, 78}, {102, 83, 62}, (broad * 0.6f + mottling * 0.4f) * 1.3f);
            Rgb turf = blend({100, 122, 58}, {169, 166, 68}, broad * 0.72f + mottling * 0.28f);
            turf = blend(turf, {71, 93, 51}, canopy * 0.88f);
            Rgb land = blend(soil, turf, lush);
            land = blend(land, {115, 92, 61}, std::clamp((wet - 0.09f) * 3.6f, 0.0f, 0.76f));

            const Rgb bed = blend({141, 127, 94}, {104, 104, 80}, mottling * 0.65f + grain * 0.35f);
            const Rgb shallows = blend(bed, {73, 150, 146}, 0.19f + pool * 0.10f);
            Rgb water = blend(shallows, {42, 130, 146}, (pool - 3.0f) / 12.0f);
            water = blend(water, {42, 111, 141}, (pool - 13.0f) / 37.0f);
            const float edge = wet - 0.34f;
            Rgb color = blend(land, water, edge * 8.0f);
            if (edge > 0.015f && edge < 0.052f && mottling > 0.43f) {
                color = blend(color, {196, 192, 143}, 0.47f);
            }
            const float grain_strength = 0.09f - 0.055f * std::min(1.0f, pool / 12.0f);
            patch.put(px, py, light(color, (mottling - 0.5f) * 0.09f + (grain - 0.5f) * grain_strength));
            patch.dampness[py * size + px] = std::max(wet, std::min(2.0f, pool / 8.0f));
            patch.grass[py * size + px] = lush;
        }
    }
}

void paint_pebble(Patch& patch, int x, int y, Rgb base, float wet, std::uint32_t hash) {
    const int width_roll = int((hash >> 19) & 7);
    const int rx = 1 + int(width_roll > 4) + int(width_roll == 7), ry = 1 + int(rx == 3);
    const float tone = float((hash >> 25) & 15) / 15.0f;
    const Rgb stone = blend(base, blend({92, 83, 69}, {158, 144, 114}, tone), 0.71f - 0.31f * std::min(wet, 1.4f));
    const float contrast = 1.0f - 0.39f * std::min(1.5f, wet);
    for (int dy = -ry; dy <= ry; ++dy) {
        for (int dx = -rx; dx <= rx; ++dx) {
            const float radius = float(dx * dx) / (rx * rx + 0.4f) + float(dy * dy) / (ry * ry + 0.4f);
            if (radius <= 1.45f) {
                const float shade = dy < 0 ? 0.18f : dy > 0 ? -0.20f : dx == -rx ? 0.07f : 0.0f;
                patch.put(x + dx, y + dy, light(stone, shade * contrast));
            }
        }
    }
}

void paint_details(Patch& patch) {
    for (int y = 3; y < size - 5; y += 4) {
        for (int x = 3; x < size - 7; x += 4) {
            const auto hash = phd::hash3(x, y, 11675);
            const int ax = x + int(hash & 3), ay = y + int((hash >> 2) & 3);
            const float wet = patch.dampness[ay * size + ax], grass = patch.grass[ay * size + ax];
            const float cluster = noise(x, y, 30, 132);
            const Rgb base = patch.get(ax, ay);
            if (wet < 0.23f && grass > 0.55f) {
                if (float((hash >> 12) & 255) / 255.0f > 0.10f + cluster * 0.32f) {
                    continue;
                }
                const int length = 2 + int((hash >> 18) & 3), lean = int((hash >> 22) & 1) + 1;
                for (int k = 0; k < length; ++k) {
                    patch.put(ax + (k * lean) / length, ay - k, light(base, 0.10f + 0.045f * k));
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
            } else if (wet > 0.6f && ((hash >> 8) & 511) < (wet > 1.3f ? 3u : 6u)) {
                const int length = 2 + int((hash >> 22) & 7);
                for (int k = 0; k < length; ++k) {
                    patch.put(ax + k, ay + int(k > 3), light(base, 0.1f + 0.045f * int(k < length / 2)));
                }
            }
        }
    }
}

} // namespace

Texture make_study_texture(const phd::World& world) {
    if (world.width() < left + extent || world.height() < top + extent) {
        return {};
    }
    Patch patch;
    paint_ground(patch, world);
    paint_details(patch);
    return make_texture(size, size, gl::RGB, patch.rgb.data());
}

} // namespace gfx::shoreline
