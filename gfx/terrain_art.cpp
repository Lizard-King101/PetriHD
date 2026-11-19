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

// What the ground is made of, before anything grows on it.
const Rgb substrate_colors[int(phd::Substrate::count)] = {
    {112, 88, 60},   // loam
    {214, 196, 148}, // sand
    {150, 112, 82},  // clay
    {72, 58, 46},    // peat
    {128, 124, 118}, // rock
    {150, 140, 122}, // gravel
};

// What grows on each biome: how much of the ground the low plants cover and
// their colour, and how many trees stand per cell, with their canopy colour.
// Until milestone 2 gives plants of their own, the biome stands in for them.
struct Cover {
    float ground;     // 0..1 of the ground hidden by grass, moss or shrubs
    Rgb plants;
    float trees;      // chance of a tree rooted in a cell
    Rgb canopy;
    float crown;      // crown radius, in cells
};

const Cover covers[phd::biome_count] = {
    {0.0f, {0, 0, 0}, 0, {0, 0, 0}, 0},                              // ice
    {0.55f, {132, 136, 96}, 0.01f, {70, 92, 66}, 0.5f},              // tundra
    {0.7f, {78, 104, 66}, 0.6f, {40, 74, 54}, 0.7f},                 // taiga
    {0.8f, {122, 152, 90}, 0.0f, {0, 0, 0}, 0},                      // alpine meadow
    {0.08f, {120, 128, 96}, 0.0f, {0, 0, 0}, 0},                     // bare rock
    {0.85f, {92, 136, 62}, 0.55f, {62, 118, 50}, 1.0f},              // deciduous forest
    {0.95f, {120, 162, 72}, 0.03f, {70, 124, 54}, 0.9f},             // grassland
    {0.8f, {124, 98, 104}, 0.02f, {72, 98, 60}, 0.6f},               // heath
    {0.5f, {132, 130, 80}, 0.05f, {96, 110, 60}, 0.6f},              // scrubland
    {0.95f, {58, 118, 50}, 0.7f, {30, 96, 42}, 1.1f},                // rainforest
    {0.7f, {178, 162, 88}, 0.06f, {96, 116, 54}, 1.1f},              // savanna
    {0.55f, {150, 146, 82}, 0.28f, {104, 118, 58}, 0.85f},           // dry woodland
    {0.05f, {150, 140, 90}, 0.0f, {0, 0, 0}, 0},                     // desert
    {0.0f, {0, 0, 0}, 0, {0, 0, 0}, 0},                              // salt flat
    {0.85f, {102, 142, 82}, 0.02f, {60, 98, 60}, 0.7f},              // marsh
    {0.8f, {74, 108, 62}, 0.4f, {50, 86, 56}, 0.95f},                // swamp
    {0.7f, {70, 110, 70}, 0.45f, {48, 96, 66}, 0.9f},                // mangrove
    {0.9f, {112, 122, 72}, 0.03f, {62, 86, 60}, 0.5f},               // bog
    {0.9f, {88, 142, 64}, 0.12f, {58, 116, 52}, 1.0f},               // riverbank
    {0.06f, {150, 160, 100}, 0.0f, {0, 0, 0}, 0},                    // beach
};

// A tree rooted in a cell, from the cell's biome and a hash of the cell, so
// it stays put however the camera moves.
struct Tree {
    float x = 0, y = 0, radius = 0;
    Rgb canopy{0, 0, 0};
};

Tree tree_in(const phd::World& world, int cx, int cy) {
    Tree tree;
    if (cx < 0 || cy < 0 || cx >= world.width() || cy >= world.height()) {
        return tree;
    }
    const auto i = std::size_t(cy) * world.width() + cx;
    const auto biome = world.biome(i);
    if (world.water(i) != phd::Water::none || int(biome) >= int(phd::Biome::tidal_shallows) ||
        world.substrate(i) == phd::Substrate::rock) {
        return tree;
    }
    const auto& cover = covers[int(biome)];
    const auto h = phd::hash3(cx, cy, 0x7f4a7c15u);
    if (float(h & 1023) >= cover.trees * 1024.0f) {
        return tree;
    }
    tree.x = float(cx) + 0.15f + float(h >> 10 & 255) / 255.0f * 0.7f;
    tree.y = float(cy) + 0.15f + float(h >> 18 & 255) / 255.0f * 0.7f;
    tree.radius = cover.crown * (0.7f + float(h >> 26 & 63) / 63.0f * 0.6f);
    tree.canopy = scale(cover.canopy, 0.9f + float(h >> 4 & 31) / 31.0f * 0.2f);
    return tree;
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
        return depth < 1.2f ? mix({84, 150, 150}, {52, 118, 150}, depth / 1.2f) : Rgb{46, 108, 148};
    }
    if (depth < 0.5f) {
        return shallow;
    }
    if (depth < 3) {
        return mix(shallow, mid, (depth - 0.5f) / 2.5f);
    }
    if (depth < 12) {
        return mix(mid, deep, (depth - 3) / 9);
    }
    return mix(deep, abyss, std::min(1.0f, (depth - 12) / 60));
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

void bake(const phd::World& world, const Sun& sun, int tx0, int ty0, int w, int h, std::uint8_t* rgb, int stride,
          bool hill_shade) {
    const int cw = world.width(), ch = world.height();
    const auto& terrain = world.terrain();
    const float inv = 1.0f / float(texels_per_cell);
    // In metres.
    const auto elevation = [&](std::size_t i) { return float(terrain.elevation[i]) / 10; };
    const auto surface = [&](std::size_t i) {
        return world.water(i) == phd::Water::none    ? float(terrain.elevation[i]) / 10
               : world.water(i) == phd::Water::ocean ? 0.0f
                                                     : float(terrain.elevation[i] + world.water_depth_dm(i)) / 10;
    };
    const auto snow = [&](std::size_t i) { return float(world.snow(i)); };
    const auto soil = [&](std::size_t i) { return float(world.soil_water(i)); };

    // The trees that can reach this region: every cell's, two cells around it.
    const int rx0 = tx0 / texels_per_cell - 2, ry0 = ty0 / texels_per_cell - 2;
    const int rw = (tx0 + w + texels_per_cell - 1) / texels_per_cell + 2 - rx0 + 1;
    const int rh = (ty0 + h + texels_per_cell - 1) / texels_per_cell + 2 - ry0 + 1;
    std::vector<Tree> trees(std::size_t(rw) * rh);
    for (int j = 0; j < rh; ++j) {
        for (int i = 0; i < rw; ++i) {
            trees[std::size_t(j) * rw + i] = tree_in(world, rx0 + i, ry0 + j);
        }
    }
    // For each cell in the region, the trees whose crown or shadow can reach
    // it (at most 8), so each texel checks a few instead of 25.
    constexpr int per_cell = 8;
    const int cx0 = tx0 / texels_per_cell, cy0 = ty0 / texels_per_cell;
    const int cws = rw - 4, chs = rh - 4;
    std::vector<std::uint8_t> near_count(std::size_t(cws) * chs, 0);
    std::vector<const Tree*> near(std::size_t(cws) * chs * per_cell);
    for (int j = 0; j < chs; ++j) {
        for (int i = 0; i < cws; ++i) {
            const auto at = std::size_t(j) * cws + i;
            const float left = float(cx0 + i), top_edge = float(cy0 + j);
            for (int k = 0; k < 25 && near_count[at] < per_cell; ++k) {
                const Tree& t = trees[std::size_t((j + 2 + k / 5 - 2) * rw + (i + 2 + k % 5 - 2))];
                if (t.radius <= 0) {
                    continue;
                }
                // Reach: the crown, plus its shadow cast up to a radius away.
                const float reach = t.radius * 1.95f;
                const float nx = std::clamp(t.x, left, left + 1) - t.x, ny = std::clamp(t.y, top_edge, top_edge + 1) - t.y;
                if (nx * nx + ny * ny < reach * reach) {
                    near[at * per_cell + near_count[at]++] = &t;
                }
            }
        }
    }

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
            if (depth > 0.08f) {
                const auto kind = world.water(cell) == phd::Water::none ? phd::Water::river : world.water(cell);
                c = water_rgb(depth, kind);
                if (depth < 0.8f && kind != phd::Water::river) {
                    // Sand shows through the shallows; foam at the very edge.
                    c = mix({206, 194, 146}, c, (depth - 0.08f) / 0.72f);
                    if (depth < 0.2f && (noise & 3) != 0) {
                        c = mix(c, {236, 240, 236}, 0.6f);
                    }
                }
                if (world.temperature(cell) < -20 && (kind != phd::Water::ocean || depth < 3)) {
                    c = mix(c, {204, 222, 236}, (noise & 31) == 0 ? 0.6f : 0.85f);
                }
                c = scale(c, 1.0f + grain * 0.5f);
            } else {
                // The ground: what it is made of, darker when wet.
                const auto ground = world.substrate(cell);
                c = substrate_colors[int(ground)];
                const float patch = float(phd::value_noise(tx, ty, 24, 0x68e31da4u) - 32768) / 32768.0f;
                c = scale(c, 1.0f + patch * 0.06f + grain * 1.5f);
                const float wet = sample(world, x, y, soil);
                c = scale(c, 1.0f - std::max(0.0f, wet - 90) / 255.0f * 0.35f);

                // Low plants, in patches: wavy biome edges, and gaps where the
                // ground shows, more of them where the cover is thin.
                const float jx = float(phd::value_noise(tx, ty, 6, 0x51ed270bu) - 32768) / 32768.0f * 0.7f;
                const float jy = float(phd::value_noise(tx, ty, 6, 0x2545f491u) - 32768) / 32768.0f * 0.7f;
                const int bx = std::clamp(int(x + jx), 0, cw - 1), by = std::clamp(int(y + jy), 0, ch - 1);
                auto biome = world.biome(std::size_t(by) * cw + bx);
                if (int(biome) >= int(phd::Biome::tidal_shallows)) {
                    biome = phd::Biome::beach; // water biomes on dry texels are shore
                }
                const auto& cover = covers[int(biome)];
                const float clumps = float(phd::value_noise(tx, ty, 3, 0x1b873593u)) / 65535.0f;
                float plants = clumps < cover.ground ? 1.0f : 0.0f;
                if (wet < 40) {
                    plants *= 0.6f; // drought thins and yellows the cover
                }
                Rgb green = scale(cover.plants, 0.92f + float(noise >> 4 & 15) / 15.0f * 0.16f);
                if (wet < 50) {
                    green = mix(green, {186, 170, 104}, (50 - wet) / 50.0f * 0.6f);
                }
                c = mix(c, green, plants);

                // Trees: round crowns rooted in nearby cells, lit like domes,
                // with the southern ones in front; their shadows fall away
                // from the sun.
                const Tree* top = nullptr;
                bool shadowed = false;
                const auto here = std::size_t(cy - cy0) * cws + std::size_t(cx - cx0);
                for (int k = 0; k < near_count[here]; ++k) {
                    const Tree& t = *near[here * per_cell + k];
                    const float dx = x - t.x, dy = y - t.y;
                    if (dx * dx + dy * dy < t.radius * t.radius && (!top || t.y > top->y)) {
                        top = &t;
                    }
                    const float sx = dx + sun.x * t.radius * 0.9f, sy = dy + sun.y * t.radius * 0.9f;
                    shadowed = shadowed || sx * sx + sy * sy < t.radius * t.radius * 0.85f;
                }
                if (top) {
                    const float dx = (x - top->x) / top->radius, dy = (y - top->y) / top->radius;
                    const float dz = std::sqrt(std::max(0.0f, 1 - dx * dx - dy * dy));
                    const float lit = dx * sun.x + dy * sun.y + dz * sun.z;
                    c = scale(top->canopy, 0.62f + 0.5f * std::max(0.0f, lit) + grain * 2);
                    if ((noise >> 8 & 7) == 0) {
                        c = scale(c, 0.85f); // leaf clumps
                    }
                } else if (shadowed) {
                    c = scale(c, 0.68f);
                }

                const float s = sample(world, x, y, snow);
                if (s > 20) {
                    const float lying = std::min(1.0f, (s - 20) / 70.0f);
                    if (float(noise >> 8 & 255) / 255.0f < lying * 1.3f) {
                        c = mix(c, {238, 242, 248}, std::min(1.0f, lying + (top ? 0.0f : 0.3f)));
                    }
                }
                // Hill shading: slopes facing the sun are lit, the others shaded.
                if (hill_shade && !top) {
                    const float d = 0.5f;
                    const float gx = (sample(world, x + d, y, elevation) - sample(world, x - d, y, elevation)) / (2 * d);
                    const float gy = (sample(world, x, y + d, elevation) - sample(world, x, y - d, elevation)) / (2 * d);
                    const float nx = -gx / shade_metres, ny = -gy / shade_metres;
                    const float len = std::sqrt(nx * nx + ny * ny + 1);
                    const float lit = (nx * sun.x + ny * sun.y + sun.z) / len;
                    shade = std::clamp(1.0f + 1.1f * (lit - sun.z), 0.45f, 1.35f);
                }
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
