#include "terrain_art.hpp"

#include <algorithm>
#include <cmath>

#include "ground_art.hpp"
#include "math.hpp"
#include "palette.hpp"

namespace art {
namespace {

using palette::mix;
using palette::Rgb;
using palette::scale;

const Rgb biome_colors[phd::biome_count] = {
    {226, 236, 242}, {150, 150, 118}, {58, 92, 68},   {122, 152, 96},  {148, 143, 138}, // cold
    {72, 126, 56},   {126, 166, 76},  {132, 112, 98}, {152, 142, 92},                   // temperate
    {34, 104, 46},   {188, 170, 96},  {138, 140, 72}, {222, 196, 142}, {232, 226, 212}, // hot
    {92, 132, 92},   {58, 88, 60},    {56, 100, 72},  {112, 104, 80},  {96, 142, 72},   // wet
    {226, 212, 162}, {80, 176, 178},  {70, 196, 190}, {50, 120, 165},  {60, 135, 170},
    {30, 85, 145},   {14, 34, 82},                                                     // water
};

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

} // namespace

namespace palette {

Rgb leaf(const phd::Plant& p, const phd::World& world) {
    const auto& g = p.genome.gene;
    const float amount = float(g[phd::gene_leaf]) / 255.0f;
    constexpr Rgb yellow{150, 158, 66}, green{100, 130, 58}, blue{62, 106, 74};
    Rgb c = amount < 0.5f ? mix(yellow, green, amount * 2) : mix(green, blue, amount * 2 - 1);
    if (phd::woody(p.genome)) {
        c = {c.r * 0.7f, c.g * 0.82f, c.b * 0.8f}; // woody leaves are darker and greener
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

Rgb flower(const phd::Plant& p) { return petals[p.genome.gene[phd::gene_flower] * 6 / 256]; }

} // namespace palette

Tree tree_in(const phd::World& world, int cx, int cy) {
    Tree tree;
    if (cx < 0 || cy < 0 || cx >= world.width() || cy >= world.height()) {
        return tree;
    }
    const auto cell = std::size_t(cy) * world.width() + cx;
    const phd::Plant* p = world.tree(cell);
    if (!p) {
        if (const int tall = world.snag(cell)) {
            // A snag stands where its tree did: grey, leafless, with what is
            // left of its limbs close to the trunk.
            const auto place = world.snag_place(cell);
            tree.x = float(cx) + 0.3f + float(place & 255) / 255.0f * 0.4f;
            tree.y = float(cy) + 0.3f + float(place >> 8 & 255) / 255.0f * 0.4f;
            tree.radius = float(phd::crown16(tall)) / 16.0f;
            tree.height = float(tall) / float(phd::cell_decimetres);
            tree.r = 104, tree.g = 92, tree.b = 82;
            tree.bare = tree.snag = true;
            tree.id = phd::hash3(cx, cy, place);
        }
        return tree;
    }
    // Where in its cell the trunk stands, from the plant itself, so it
    // stays put as the camera moves and as the tree grows.
    const auto h = phd::hash3(int(p->id), int(p->id >> 16), phd::tree_place_salt);
    tree.x = float(cx) + 0.3f + float(h & 255) / 255.0f * 0.4f;
    tree.y = float(cy) + 0.3f + float(h >> 8 & 255) / 255.0f * 0.4f;
    tree.radius = float(phd::crown16(p->height)) / 16.0f;
    tree.height = float(p->height) / float(phd::cell_decimetres);
    const Rgb c = palette::leaf(*p, world);
    tree.r = std::uint8_t(std::min(255.0f, c.r));
    tree.g = std::uint8_t(std::min(255.0f, c.g));
    tree.b = std::uint8_t(std::min(255.0f, c.b));
    tree.conifer = phd::evergreen(p->genome) && p->genome.gene[phd::gene_cold] >= 150;
    tree.bare = !phd::evergreen(p->genome) && (p->flags & phd::plant_dormant);
    // The crown's outline runs in the family. Trees that scatter small seeds
    // on the wind are pioneers with loose, airy crowns, and thin-barked ones
    // among them have white bark. Trees of dry ground spread wide and flat,
    // trees that need full light grow narrow and tall, and the rest are round.
    const auto& gene = p->genome.gene;
    if (gene[phd::gene_seed] < 96 && !phd::fruiting(p->genome)) {
        tree.form = 3;
        tree.pale = gene[phd::gene_bark] < 110;
    } else if (gene[phd::gene_drought] >= 150) {
        tree.form = 1;
    } else if (gene[phd::gene_shade] < 100) {
        tree.form = 2;
    }
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
          bool flat, bool tree_xray) {
    const bool hill_shade = flat;
    const int cw = world.width(), ch = world.height();
    const auto& terrain = world.terrain();
    const float inv = 1.0f / float(texels_per_cell);
    // In metres.
    const auto elevation = [&](std::size_t i) { return float(terrain.elevation[i]) / 10; };
    const auto snow = [&](std::size_t i) { return float(world.snow(i)); };

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

    std::vector<std::uint8_t> ground_rgb(std::size_t(w) * h * 3), ground_water(std::size_t(w) * h);
    ground::sample(world, tx0, ty0, w, h, texels_per_cell, ground_rgb.data(), ground_water.data());

    for (int ty = ty0; ty < ty0 + h; ++ty) {
        auto* out = rgb + std::size_t(ty - ty0) * std::size_t(stride);
        for (int tx = tx0; tx < tx0 + w; ++tx, out += 3) {
            const float x = (float(tx) + 0.5f) * inv, y = (float(ty) + 0.5f) * inv;
            const int cx = std::clamp(int(x), 0, cw - 1), cy = std::clamp(int(y), 0, ch - 1);
            const auto cell = std::size_t(cy) * cw + cx;
            const auto noise = phd::hash2(tx, ty);
            const float grain = float(int(noise & 15) - 8) / 255.0f;

            // The ground and the water are the close view's painting, one
            // texel of it for each of these.
            const auto* painted = &ground_rgb[(std::size_t(ty - ty0) * w + (tx - tx0)) * 3];
            Rgb c{float(painted[0]), float(painted[1]), float(painted[2])};
            float shade = 1;
            if (!ground_water[std::size_t(ty - ty0) * w + (tx - tx0)]) {
                // Bushes stand out from the turf as darker clumps, in their
                // sprites' tones; low plants in bloom show a few flowers.
                const int slot = (tx % texels_per_cell >= texels_per_cell / 2) + 2 * (ty % texels_per_cell >= texels_per_cell / 2);
                const phd::Plant* low = world.tree(cell) ? nullptr : world.occupant(cell, slot);
                if (low && phd::woody(low->genome) && low->height >= 4) {
                    const float clumps = float(phd::value_noise(tx, ty, 3, 0x1b873593u)) / 65535.0f;
                    if (clumps < std::min(0.85f, 0.3f + float(low->height) / 12.0f)) {
                        c = palette::leaf_tone(palette::leaf(*low, world), (noise >> 9 & 3) == 0 ? 1.3f : 2.1f + float(noise >> 4 & 15) / 15.0f);
                    }
                } else if (low && (low->flags & phd::plant_flowering) && (noise >> 12 & 15) == 0) {
                    c = palette::flower(*low);
                }

                // Trees: round crowns rooted in nearby cells, lit like domes,
                // with the southern ones in front; their shadows fall away
                // from the sun.
                const Tree* top = nullptr;
                bool shadowed = false, trunk = false;
                const auto here = std::size_t(cy - cy0) * cws + std::size_t(cx - cx0);
                for (int k = 0; k < near_count[here]; ++k) {
                    const Tree& t = *near[here * per_cell + k];
                    const float dx = x - t.x, dy = y - t.y;
                    bool covers = dx * dx + dy * dy < t.reach() * t.reach();
                    if (covers && t.bare) {
                        // A leafless tree is its limbs: five spokes from the
                        // trunk, a texel wide, with the ground between them.
                        const float turn = std::atan2(dy, dx) * (5 / 6.2831853f) + float(t.id % 16) / 16.0f;
                        const float off = std::fabs(turn - std::floor(turn + 0.5f)) * (6.2831853f / 5) * std::sqrt(dx * dx + dy * dy);
                        covers = off < 0.6f * inv;
                    }
                    if (tree_xray && flat && !t.bare) {
                        trunk |= int(t.x * texels_per_cell) == tx && int(t.y * texels_per_cell) == ty;
                        covers = covers && (tx & 1) == 0 && (ty & 1) == 0;
                    }
                    if (covers && (!top || t.y > top->y)) {
                        top = &t;
                    }
                    const float sx = dx + sun.x * t.radius * 0.9f, sy = dy + sun.y * t.radius * 0.9f;
                    shadowed = shadowed || (!t.snag && sx * sx + sy * sy < t.radius * t.radius * 0.85f);
                }
                if (top && !flat) {
                    // Forest floor under a tree the tilted view stands up:
                    // leaf litter and needles, shaded, with a little green.
                    c = mix(c, (noise >> 12 & 7) == 0 ? Rgb{78, 98, 52} : palette::litter, 0.75f);
                    c = scale(c, 0.8f + grain * 2);
                    top = nullptr;
                } else if (top) {
                    const float dx = (x - top->x) / top->radius, dy = (y - top->y) / top->radius;
                    const float dz = std::sqrt(std::max(0.0f, 1 - dx * dx - dy * dy));
                    const float lit = dx * sun.x + dy * sun.y + dz * sun.z;
                    // The tones of its sprite, from shade to highlight.
                    c = palette::leaf_tone({float(top->r), float(top->g), float(top->b)},
                                           1.3f + 1.9f * std::max(0.0f, lit) + grain * 8);
                    if (top->bare) {
                        c = (noise >> 6 & 3) == 0 ? Rgb{128, 112, 98} : Rgb{104, 92, 82}; // grey wood
                    } else if ((noise >> 8 & 7) == 0) {
                        c = scale(c, 0.85f); // leaf clumps
                    }
                } else if (trunk) {
                    c = {104, 82, 57};
                } else if (shadowed && flat) {
                    c = scale(c, 0.68f); // the tilted view casts its own shadows
                }

                // Snow lies on a crown of leaves in patches, with the leaves
                // showing between; it does not lie on bare limbs. (The
                // ground's own snow is in its painting.)
                const float s = top && !top->bare ? sample(world, x, y, snow) : 0;
                if (s > 20) {
                    const float lying = std::min(1.0f, (s - 20) / 70.0f);
                    if (float(noise >> 8 & 255) / 255.0f < lying * 0.6f) {
                        c = mix(c, palette::snow, 0.75f);
                    }
                }
                // Burnt ground: char and ash, fading as it recovers.
                if (const int burnt = top ? world.burnt(cell) : 0) {
                    const float b = float(burnt) / 255.0f;
                    c = mix(c, (noise >> 14 & 7) == 0 ? palette::ash : palette::charred, b * (top ? 0.4f : 0.85f));
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
