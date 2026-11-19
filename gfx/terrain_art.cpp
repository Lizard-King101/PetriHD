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

// A plant's colour now: its leaf gene from yellow-green to blue-green, then
// the season: autumn colour and bare winter branches for deciduous plants,
// straw for dormant grass, browning when it is failing.
Rgb leaf_rgb(const phd::Plant& p, const phd::World& world) {
    const auto& g = p.genome.gene;
    const float leaf = float(g[phd::gene_leaf]) / 255.0f;
    Rgb c = leaf < 0.5f ? mix({152, 170, 66}, {74, 140, 56}, leaf * 2) : mix({74, 140, 56}, {46, 104, 84}, leaf * 2 - 1);
    if (phd::woody(p.genome)) {
        c = scale(c, 0.8f);
    }
    const int t = world.temperature(std::size_t(p.cell));
    if (!phd::evergreen(p.genome)) {
        if (p.flags & phd::plant_dormant) {
            c = phd::woody(p.genome) ? Rgb{112, 98, 86} : Rgb{176, 158, 104};
        } else if (world.season() == 2 && t < 150) {
            // Autumn: yellow, orange or red by the plant's flower gene.
            const Rgb turn = g[phd::gene_flower] < 85 ? Rgb{222, 182, 62} : g[phd::gene_flower] < 170 ? Rgb{212, 118, 42}
                                                                                                     : Rgb{172, 52, 40};
            c = mix(c, turn, std::min(1.0f, float(150 - t) / 90.0f));
        }
    }
    if (p.health < 140) {
        c = mix(c, {150, 118, 70}, float(140 - p.health) / 200.0f);
    }
    return c;
}

Rgb flower_rgb(const phd::Plant& p) {
    static const Rgb colours[6] = {{240, 240, 232}, {240, 210, 60}, {236, 150, 180}, {150, 100, 200}, {210, 60, 60},
                                   {110, 140, 230}};
    return colours[p.genome.gene[phd::gene_flower] * 6 / 256];
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

Tree tree_in(const phd::World& world, int cx, int cy) {
    Tree tree;
    if (cx < 0 || cy < 0 || cx >= world.width() || cy >= world.height()) {
        return tree;
    }
    const phd::Plant* p = world.tree(std::size_t(cy) * world.width() + cx);
    if (!p) {
        return tree;
    }
    // Where in its cell the trunk stands, from the plant itself, so it
    // stays put as the camera moves and as the tree grows.
    const auto h = phd::hash3(int(p->id), int(p->id >> 16), 0x7f4a7c15u);
    tree.x = float(cx) + 0.3f + float(h & 255) / 255.0f * 0.4f;
    tree.y = float(cy) + 0.3f + float(h >> 8 & 255) / 255.0f * 0.4f;
    tree.radius = float(phd::crown16(p->height)) / 16.0f;
    tree.height = float(p->height) / float(phd::cell_decimetres);
    const Rgb c = leaf_rgb(*p, world);
    tree.r = std::uint8_t(std::min(255.0f, c.r));
    tree.g = std::uint8_t(std::min(255.0f, c.g));
    tree.b = std::uint8_t(std::min(255.0f, c.b));
    tree.conifer = phd::evergreen(p->genome) && p->genome.gene[phd::gene_cold] >= 150;
    tree.bare = !phd::evergreen(p->genome) && (p->flags & phd::plant_dormant);
    tree.id = p->id;
    return tree;
}

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
          bool flat) {
    const bool hill_shade = flat;
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

                // Low plants: each of the cell's four slots is a quarter of it,
                // 2 x 2 texels, covered as fully as the plant is tall, in its
                // own colour, with flowers in spring.
                const int slot = (tx % texels_per_cell >= texels_per_cell / 2) + 2 * (ty % texels_per_cell >= texels_per_cell / 2);
                const phd::Plant* low = world.tree(cell) ? nullptr : world.occupant(cell, slot);
                if (low) {
                    const float clumps = float(phd::value_noise(tx, ty, 3, 0x1b873593u)) / 65535.0f;
                    const float cover = std::min(1.0f, 0.35f + float(low->height) / 8.0f);
                    if (clumps < cover) {
                        Rgb green = scale(leaf_rgb(*low, world), 0.92f + float(noise >> 4 & 15) / 15.0f * 0.16f);
                        if (phd::woody(low->genome)) {
                            green = scale(green, (noise >> 9 & 3) == 0 ? 0.75f : 1.0f); // shrubs: darker clumps
                        }
                        c = green;
                        if ((low->flags & phd::plant_flowering) && (noise >> 12 & 7) == 0) {
                            c = flower_rgb(*low);
                        }
                    }
                }

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
                if (top && !flat) {
                    // Forest floor under a tree the tilted view stands up:
                    // leaf litter and needles, shaded, with a little green.
                    c = mix(c, (noise >> 12 & 7) == 0 ? Rgb{78, 98, 52} : Rgb{86, 68, 46}, 0.75f);
                    c = scale(c, 0.8f + grain * 2);
                    top = nullptr;
                } else if (top) {
                    const float dx = (x - top->x) / top->radius, dy = (y - top->y) / top->radius;
                    const float dz = std::sqrt(std::max(0.0f, 1 - dx * dx - dy * dy));
                    const float lit = dx * sun.x + dy * sun.y + dz * sun.z;
                    c = scale(Rgb{float(top->r), float(top->g), float(top->b)}, 0.62f + 0.5f * std::max(0.0f, lit) + grain * 2);
                    if (top->bare && (noise >> 6 & 3) != 0) {
                        c = scale(c, 0.7f); // bare branches: the ground shows through
                    }
                    if ((noise >> 8 & 7) == 0) {
                        c = scale(c, 0.85f); // leaf clumps
                    }
                } else if (shadowed && flat) {
                    c = scale(c, 0.68f); // the tilted view casts its own shadows
                }

                const float s = sample(world, x, y, snow);
                if (s > 20) {
                    const float lying = std::min(1.0f, (s - 20) / 70.0f);
                    if (float(noise >> 8 & 255) / 255.0f < lying * 1.3f) {
                        c = mix(c, {238, 242, 248}, std::min(1.0f, lying + (top ? 0.0f : 0.3f)));
                    }
                }
                // Burnt ground: char and ash, fading as it recovers.
                if (const int burnt = world.burnt(cell)) {
                    const float b = float(burnt) / 255.0f;
                    c = mix(c, (noise >> 14 & 7) == 0 ? Rgb{150, 146, 140} : Rgb{44, 38, 34}, b * (top ? 0.4f : 0.85f));
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
