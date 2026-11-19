#include "terrain_art.hpp"

#include <algorithm>
#include <cmath>

#include "math.hpp"

namespace art {
namespace {

struct Rgb {
    float r, g, b;
};

Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb scale(Rgb a, float s) { return {a.r * s, a.g * s, a.b * s}; }

const Rgb biome_colors[phd::biome_count] = {
    {226, 236, 242}, {150, 150, 118}, {58, 92, 68},   {122, 152, 96},  {148, 143, 138}, // cold
    {72, 126, 56},   {126, 166, 76},  {132, 112, 98}, {152, 142, 92},                   // temperate
    {34, 104, 46},   {188, 170, 96},  {138, 140, 72}, {222, 196, 142}, {232, 226, 212}, // hot
    {92, 132, 92},   {58, 88, 60},    {56, 100, 72},  {112, 104, 80},  {96, 142, 72},   // wet
    {226, 212, 162}, {80, 176, 178},  {70, 196, 190}, {50, 120, 165},  {60, 135, 170},
    {30, 85, 145},   {14, 34, 82},                                                     // water
};

bool forested(phd::Biome b) {
    using phd::Biome;
    return b == Biome::taiga || b == Biome::deciduous_forest || b == Biome::rainforest || b == Biome::dry_woodland ||
           b == Biome::swamp || b == Biome::mangrove;
}

// Bilinear sample of a per-cell value at a point in cell units (centres at +0.5).
template <class F> float sample(const phd::World& world, float x, float y, F value) {
    const int w = world.width(), h = world.height();
    x -= 0.5f;
    y -= 0.5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    const auto at = [&](int cx, int cy) {
        cx = std::clamp(cx, 0, w - 1);
        cy = std::clamp(cy, 0, h - 1);
        return value(std::size_t(cy) * w + cx);
    };
    const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const float bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

Rgb water_rgb(float depth, phd::Water kind) {
    const Rgb shallow{74, 172, 176}, mid{38, 112, 162}, deep{20, 62, 124}, abyss{12, 30, 74};
    if (kind == phd::Water::river) {
        return depth < 2 ? mix({84, 150, 150}, {52, 118, 150}, depth / 2) : Rgb{46, 108, 148};
    }
    if (depth < 1) {
        return shallow;
    }
    if (depth < 8) {
        return mix(shallow, mid, (depth - 1) / 7);
    }
    if (depth < 40) {
        return mix(mid, deep, (depth - 8) / 32);
    }
    return mix(deep, abyss, std::min(1.0f, (depth - 40) / 160));
}

} // namespace

void biome_rgb(phd::Biome biome, std::uint8_t out[3]) {
    const auto c = biome_colors[int(biome)];
    out[0] = std::uint8_t(c.r);
    out[1] = std::uint8_t(c.g);
    out[2] = std::uint8_t(c.b);
}

Sun sun_for(const phd::World& world) {
    const float light = float(world.daylight()) / 255.0f;
    if (light < 0.05f) {
        return {-0.35f, -0.5f, 0.79f}; // the moon
    }
    // East in the morning, south at noon, west in the evening.
    const float azimuth = float(world.day_angle() - 64) * 3.14159265f / 128.0f;
    const float z = 0.25f + 0.7f * light;
    const float across = std::sqrt(std::max(0.0f, 1 - z * z));
    return {std::cos(azimuth) * across, std::sin(azimuth) * across, z};
}

void day_tint(const phd::World& world, std::uint8_t out[3]) {
    const float light = float(world.daylight()) / 255.0f;
    const Rgb night{70, 84, 138}, warm{255, 190, 150}, day{255, 255, 255};
    const Rgb c = light < 0.4f ? mix(night, warm, light / 0.4f) : mix(warm, day, std::min(1.0f, (light - 0.4f) / 0.35f));
    out[0] = std::uint8_t(c.r);
    out[1] = std::uint8_t(c.g);
    out[2] = std::uint8_t(c.b);
}

void bake(const phd::World& world, const Sun& sun, int tx0, int ty0, int w, int h, std::uint8_t* rgb, int stride) {
    const int cw = world.width(), ch = world.height();
    const auto& terrain = world.terrain();
    const float inv = 1.0f / float(texels_per_cell);
    const auto elevation = [&](std::size_t i) { return float(terrain.elevation[i]); };
    const auto surface = [&](std::size_t i) {
        return world.water(i) == phd::Water::none ? float(terrain.elevation[i])
               : world.water(i) == phd::Water::ocean ? 0.0f
                                                      : float(terrain.elevation[i]) + float(world.water_depth_dm(i)) / 10;
    };
    const auto snow = [&](std::size_t i) { return float(world.snow(i)); };
    const auto soil = [&](std::size_t i) { return float(world.soil_water(i)); };

    for (int ty = ty0; ty < ty0 + h; ++ty) {
        auto* out = rgb + std::size_t(ty - ty0) * std::size_t(stride);
        for (int tx = tx0; tx < tx0 + w; ++tx, out += 3) {
            const float x = (float(tx) + 0.5f) * inv, y = (float(ty) + 0.5f) * inv;
            const int cx = std::clamp(int(x), 0, cw - 1), cy = std::clamp(int(y), 0, ch - 1);
            const auto cell = std::size_t(cy) * cw + cx;
            const auto noise = phd::hash2(tx, ty);
            const float grain = float(int(noise & 15) - 8) / 255.0f;

            const float ground = sample(world, x, y, elevation);
            const float depth = sample(world, x, y, surface) - ground;
            Rgb c;
            float shade = 1;
            if (depth > 0.25f) {
                const auto kind = world.water(cell) == phd::Water::none ? phd::Water::river : world.water(cell);
                c = water_rgb(depth, kind);
                if (depth < 1.6f && kind != phd::Water::river) {
                    // Sand shows through the shallows; foam at the very edge.
                    c = mix({206, 194, 146}, c, (depth - 0.25f) / 1.35f);
                    if (depth < 0.5f && (noise & 3) != 0) {
                        c = mix(c, {236, 240, 236}, 0.6f);
                    }
                }
                if (world.temperature(cell) < -20 && (kind != phd::Water::ocean || depth < 12)) {
                    c = mix(c, {204, 222, 236}, (noise & 31) == 0 ? 0.6f : 0.85f);
                }
                c = scale(c, 1.0f + grain * 0.5f);
            } else {
                // Wavy biome edges: pick the biome of a nearby point, warped by smooth noise.
                const float jx = float(phd::value_noise(tx, ty, 6, 0x51ed270bu) - 32768) / 32768.0f * 0.7f;
                const float jy = float(phd::value_noise(tx, ty, 6, 0x2545f491u) - 32768) / 32768.0f * 0.7f;
                const int bx = std::clamp(int(x + jx), 0, cw - 1), by = std::clamp(int(y + jy), 0, ch - 1);
                auto biome = world.biome(std::size_t(by) * cw + bx);
                if (int(biome) >= int(phd::Biome::tidal_shallows)) {
                    biome = phd::Biome::beach; // water biomes on dry texels are shore
                }
                c = biome_colors[int(biome)];
                const float patch = float(phd::value_noise(tx, ty, 24, 0x68e31da4u) - 32768) / 32768.0f;
                c = scale(c, 1.0f + patch * 0.07f + grain);
                if (forested(biome)) {
                    if ((noise >> 4 & 3) == 0) {
                        c = scale(c, 0.72f);
                    } else if ((noise >> 4 & 15) == 1) {
                        c = scale(c, 1.18f);
                    }
                }
                const float wet = sample(world, x, y, soil);
                if (wet > 150) {
                    c = scale(c, 1.0f - (wet - 150) / 255.0f * 0.35f);
                } else if (wet < 50) {
                    c = mix(c, {196, 176, 112}, (50 - wet) / 50.0f * 0.35f); // drought yellow
                }
                const float s = sample(world, x, y, snow);
                if (s > 20) {
                    const float cover = std::min(1.0f, (s - 20) / 70.0f);
                    if (float(noise >> 8 & 255) / 255.0f < cover * 1.3f) {
                        c = mix(c, {238, 242, 248}, std::min(1.0f, cover + 0.3f));
                    }
                }
                // Hill shading: slopes facing the sun are lit, the others shaded.
                const float d = 0.5f;
                const float gx = (sample(world, x + d, y, elevation) - sample(world, x - d, y, elevation)) / (2 * d);
                const float gy = (sample(world, x, y + d, elevation) - sample(world, x, y - d, elevation)) / (2 * d);
                const float nx = -gx / 30.0f, ny = -gy / 30.0f;
                const float len = std::sqrt(nx * nx + ny * ny + 1);
                const float lit = (nx * sun.x + ny * sun.y + sun.z) / len;
                shade = std::clamp(1.0f + 1.1f * (lit - sun.z), 0.45f, 1.35f);
            }
            out[0] = std::uint8_t(std::clamp(c.r * shade, 0.0f, 255.0f));
            out[1] = std::uint8_t(std::clamp(c.g * shade, 0.0f, 255.0f));
            out[2] = std::uint8_t(std::clamp(c.b * shade, 0.0f, 255.0f));
        }
    }
}

void bake_all(const phd::World& world, std::vector<std::uint8_t>& rgb, int& width, int& height) {
    width = world.width() * texels_per_cell;
    height = world.height() * texels_per_cell;
    rgb.resize(std::size_t(width) * height * 3);
    bake(world, sun_for(world), 0, 0, width, height, rgb.data(), width * 3);
}

} // namespace art
