#include "plant_sprites.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "math.hpp"
#include "palette.hpp"

namespace art::sprites {
namespace {

struct Rgb {
    std::uint8_t r, g, b;
};

Rgb scaled(Rgb c, float k) {
    const auto f = [k](std::uint8_t v) { return std::uint8_t(std::clamp(float(v) * k, 0.0f, 255.0f)); };
    return {f(c.r), f(c.g), f(c.b)};
}

struct Canvas {
    int w, h;
    std::vector<std::uint8_t> px;

    Canvas(int width, int height) : w(width), h(height), px(std::size_t(width) * height * 4, 0) {}

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    bool solid(int x, int y) const { return inside(x, y) && px[(std::size_t(y) * w + x) * 4 + 3] != 0; }
    void put(int x, int y, Rgb c) {
        if (!inside(x, y)) {
            return;
        }
        auto* p = &px[(std::size_t(y) * w + x) * 4];
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = 255;
    }
    // A texel that gives its own light.
    void glow(int x, int y, Rgb c) {
        if (inside(x, y)) {
            put(x, y, c);
            px[(std::size_t(y) * w + x) * 4 + 3] = glow_alpha;
        }
    }
};

float unit(std::uint32_t h) { return float(h & 0xffff) / 65535.0f; }
std::uint32_t hash(int a, int b, std::uint32_t seed) { return phd::hash3(a, b, seed); }

using Tones = std::array<Rgb, 5>;

// The palette's five leaf tones for a pigment: deep shade, shade, body, lit,
// highlight.
Rgb bytes(palette::Rgb c) {
    const auto b = [](float v) { return std::uint8_t(std::clamp(v, 0.0f, 255.0f)); };
    return {b(c.r), b(c.g), b(c.b)};
}
Tones leaf_tones(Rgb p) {
    const auto t = palette::leaf_tones({float(p.r), float(p.g), float(p.b)});
    return {{bytes(t[0]), bytes(t[1]), bytes(t[2]), bytes(t[3]), bytes(t[4])}};
}

constexpr Rgb bark_light{150, 112, 78}, bark_mid{112, 82, 57}, bark_dark{74, 54, 40};
constexpr Rgb pale_light{232, 228, 212}, pale_mid{198, 196, 184}, pale_dark{142, 146, 146}, pale_scar{62, 58, 56};
constexpr Rgb twig_light{128, 112, 98}, twig_mid{104, 92, 82}, twig_dark{72, 62, 56};

// A crown of leaves, built from clumps. Each clump is a small rounded mass
// of about the same size whatever the plant, so a bigger crown has more of
// them, not bigger ones. A clump is lit on its upper left and dark beneath,
// it shades the clump below it, and the crown as a whole is lit the same
// way. The form sets where the clumps go: a ball, a broad flat-bottomed
// parasol, a tall egg, or a loose scatter with sky and limbs between.
void canopy(Canvas& c, float cx, float cy, float rx, float ry, const Tones& tones, std::uint32_t seed,
            Form form = Form::round) {
    struct Clump {
        float x, y, r, z;
    };
    const bool open = form == Form::open;
    const float small = std::min(rx, ry);
    const float size = std::clamp(small * 0.34f, 2.6f, open ? 6.0f : 9.5f);
    const int count = std::clamp(int(rx * ry / (size * size) * (open ? 1.2f : 1.5f) + 1.5f), 2, 120);
    std::vector<Clump> clumps;
    const float phase = unit(hash(1, 2, seed)) * 6.2831853f;
    for (int i = 0; i < count; ++i) {
        const auto h = hash(i, 7, seed);
        const float a = phase + float(i) * 2.3999632f;
        const float out = std::sqrt((float(i) + 0.5f) / float(count)) * (open ? 0.92f : 1.0f);
        const float r = size * (open ? 0.55f + 0.75f * unit(h) : 0.72f + 0.6f * unit(h)) * (1.12f - 0.3f * out);
        float x = std::cos(a) * out * std::max(0.0f, rx - r * 0.8f) + (unit(h >> 8) - 0.5f) * size * 0.5f;
        float y = std::sin(a) * out * std::max(0.0f, ry - r * 0.8f) + (unit(h >> 12) - 0.5f) * size * 0.5f;
        if (form == Form::spreading && y > 0) {
            y *= 0.4f; // a flat underside
        } else if (form == Form::columnar) {
            x *= 0.5f + 0.5f * std::pow(std::clamp((y / ry + 1) / 2, 0.0f, 1.0f), 0.6f); // narrowing to the top
        }
        // The crown bulges toward the camera, and clumps lower on the screen are nearer.
        const float z = std::sqrt(std::max(0.0f, 1 - out * out)) * small * 0.55f + y * 0.6f + unit(h >> 16) * r * 0.4f;
        clumps.push_back({cx + x, cy + y, r, z});
    }

    const int x0 = int(std::floor(cx - rx)) - 2, x1 = int(std::ceil(cx + rx)) + 2;
    const int y0 = int(std::floor(cy - ry)) - 2, y1 = int(std::ceil(cy + ry)) + 2;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    struct Texel {
        float z = -1e9f, light = 0;
        std::int16_t clump = -1;
    };
    std::vector<Texel> texels(std::size_t(bw) * bh);
    // Each clump is drawn over its own few texels, the nearest winning.
    for (std::size_t k = 0; k < clumps.size(); ++k) {
        const Clump& b = clumps[k];
        const int reach = int(b.r + 1.8f) + 1;
        for (int y = std::max(y0, int(b.y) - reach); y <= std::min(y1, int(b.y) + reach); ++y) {
            for (int x = std::max(x0, int(b.x) - reach); x <= std::min(x1, int(b.x) + reach); ++x) {
                // A ragged outline: each clump's edge steps in and out by a leaf.
                const float ragged = b.r + (unit(hash(phd::floor_div(x, 2) + int(k) * 7, phd::floor_div(y, 2), seed)) - 0.5f) * 1.6f;
                const float dx = (float(x) + 0.5f - b.x) / ragged, dy = (float(y) + 0.5f - b.y) / ragged;
                const float d2 = dx * dx + dy * dy;
                if (d2 >= 1) {
                    continue;
                }
                const float up = std::sqrt(1 - d2), z = b.z + up * b.r;
                Texel& texel = texels[std::size_t(y - y0) * bw + (x - x0)];
                if (z > texel.z) {
                    texel = {z, -0.45f * dx - 0.72f * dy + 0.5f * up, std::int16_t(k)};
                }
            }
        }
    }
    static const Texel none;
    const auto at = [&](int x, int y) -> const Texel& {
        return x < x0 || x > x1 || y < y0 || y > y1 ? none : texels[std::size_t(y - y0) * bw + (x - x0)];
    };
    const Rgb ink = scaled(tones[0], 0.72f);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Texel& here = at(x, y);
            if (here.clump < 0) {
                continue;
            }
            const float wx = (float(x) + 0.5f - cx) / rx, wy = (float(y) + 0.5f - cy) / ry;
            float t = 1.6f + 1.7f * here.light - 0.6f * wx - 1.3f * wy;
            // The clump above and to the left hangs over this one.
            const Texel& over = at(x - 2, y - 3);
            if (over.clump >= 0 && over.clump != here.clump && over.z > here.z + 2.0f) {
                t -= 1.4f;
            }
            // Leaves: short dabs, staggered row by row, a tone up or down.
            const auto leaf = hash(phd::floor_div(x + (y & 1), 2), y, seed ^ 0x51ed27u);
            t += (unit(leaf) - 0.5f) * 0.75f;
            int tone = std::clamp(int(std::floor(t + 0.5f)), 0, 4);
            const Texel& above = at(x, y - 1);
            const Texel& below = at(x, y + 1);
            if (above.clump != here.clump && above.z < here.z - 1.5f && here.light > 0.15f && tone < 4) {
                ++tone; // the lit top of a clump
            }
            if (below.clump < 0 || at(x + 1, y).clump < 0) {
                // The outline: dark underneath and on the right, lit on the upper left.
                c.put(x, y, wy > -0.15f || wx > 0.45f ? (wy > 0.3f ? ink : tones[0]) : tones[std::size_t(std::max(1, tone - 1))]);
                continue;
            }
            c.put(x, y, tones[std::size_t(tone)]);
        }
    }
}

// A trunk or limb from (x0, y0) up to (x1, y1), tapering, lit on its left.
// Rows above `shade_above` are in the crown's shadow.
// Bark is brown (0), the grey of leafless wood (1), or pale with dark scars (2).
void limb(Canvas& c, float x0, float y0, float x1, float y1, float w0, float w1, int bark, std::uint32_t seed,
          float shade_above = -1e9f) {
    const Rgb light = bark == 2 ? pale_light : bark == 1 ? twig_light : bark_light;
    const Rgb mid = bark == 2 ? pale_mid : bark == 1 ? twig_mid : bark_mid;
    const Rgb dark = bark == 2 ? pale_dark : bark == 1 ? twig_dark : bark_dark;
    const int steps = std::max(1, int(std::max(std::fabs(y1 - y0), std::fabs(x1 - x0)) + 0.5f));
    const bool steep = std::fabs(y1 - y0) >= std::fabs(x1 - x0);
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const float x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t, w = w0 + (w1 - w0) * t;
        const int n = std::max(1, int(w + 0.5f));
        const float k = y < shade_above ? 0.72f : 1.0f;
        for (int j = 0; j < n; ++j) {
            Rgb tone = n == 1 ? mid : j == 0 ? light : j >= n - 1 - int(n > 4) ? dark : mid;
            if (n > 2 && j > 0 && j < n - 1 && (hash(int(x) + j, int(y), seed) & 7) == 0) {
                tone = dark; // a fleck of bark
            }
            if (bark == 2 && n > 1 && j < n - 1 && hash(phd::floor_div(int(x) + j, 3), int(y), seed) % 11 == 0) {
                tone = pale_scar; // a scar across the bark
            }
            const int offset = j - n / 2;
            c.put(int(std::floor(x + 1e-3f)) + (steep ? offset : 0), int(std::floor(y + 1e-3f)) + (steep ? 0 : offset),
                  scaled(tone, k)); // the nudge keeps a whole step from rounding down a row
        }
    }
}

// Bare branches, forking as they rise.
void twigs(Canvas& c, float x, float y, float angle, float length, float width, int depth, std::uint32_t seed) {
    const float ex = x + std::sin(angle) * length, ey = y - std::cos(angle) * length * 0.9f;
    limb(c, x, y, ex, ey, width, std::max(1.0f, width * 0.6f), true, seed);
    if (depth == 0 || length < 3) {
        return;
    }
    const int forks = 2 + int(hash(depth, int(length), seed) % 2);
    for (int k = 0; k < forks; ++k) {
        const auto h = hash(k, depth, seed);
        const float spread = (float(k) - float(forks - 1) / 2) * 0.75f + (unit(h) - 0.5f) * 0.5f;
        twigs(c, ex, ey, angle * 0.6f + spread, length * (0.62f + 0.14f * unit(h >> 16)), std::max(1.0f, width * 0.6f),
              depth - 1, h);
    }
}

// Needles on a conifer's skirt: sprays fanning out from the trunk, each lit
// on its left edge and shaded on its right, so a tier reads as drooping
// branches. `s` is -1 to 1 across the tier; returns the spray and where in it.
struct Spray {
    int index;
    float across; // 0 at its left edge, 1 at its right
};
Spray spray_at(float s, float radius) {
    const int sprays = std::max(3, int(radius * 2 / 4.5f + 0.5f));
    const float f = std::clamp(s * 0.5f + 0.5f, 0.0f, 0.9999f) * float(sprays);
    return {int(f), f - std::floor(f)};
}
float spray_tone(Spray spray) { return spray.across < 0.3f ? 0.75f : spray.across > 0.72f ? -0.85f : 0.0f; }

// A leafless crown as the camera sees it at `pitch`: limbs growing up and out
// from the top of the trunk in three dimensions, forking as they go, then
// drawn from the back to the front. From the side they fan upward; from
// above they spread like spokes. Returns the picture with the trunk's top at
// its anchor.
struct Stick {
    float x0, y0, z0, x1, y1, z1, w0, w1;
    std::uint32_t seed;
};
void grow(std::vector<Stick>& sticks, float x, float y, float z, float around, float up, float length, float width, int depth,
          std::uint32_t seed) {
    const float ex = x + std::cos(up) * std::cos(around) * length, ey = y + std::cos(up) * std::sin(around) * length;
    const float ez = z + std::sin(up) * length, thin = std::max(1.0f, width * 0.6f);
    sticks.push_back({x, y, z, ex, ey, ez, width, thin, seed});
    if (depth == 0 || length < 3) {
        return;
    }
    const int forks = 2 + int(hash(depth, int(length), seed) % 2);
    for (int k = 0; k < forks; ++k) {
        const auto h = hash(k, depth, seed);
        const float turn = (float(k) - float(forks - 1) / 2) * 0.9f + (unit(h) - 0.5f) * 0.6f;
        const float rise = std::clamp(up * 0.8f + (unit(h >> 8) - 0.5f) * 0.5f, 0.1f, 1.4f);
        grow(sticks, ex, ey, ez, around + turn, rise, length * (0.62f + 0.14f * unit(h >> 16)), thin, depth - 1, h);
    }
}
// A snag has no crown: `broken` paints the top of its trunk snapped off in
// splinters, and two or three stubs of limbs at different heights below it.
Image bare_crown(int radius, bool narrow, int bark, float pitch, std::uint32_t seed, bool broken = false) {
    std::vector<Stick> sticks;
    if (broken) {
        // The radius is the stubs' reach, two fifths of the crown's; the
        // trunk is as thick as that crown's was.
        const float trunk = std::clamp(float(radius) * 0.5f, 2.0f, 9.0f);
        const int splinters = trunk < 3 ? 2 : 3;
        for (int k = 0; k < splinters; ++k) {
            const auto h = hash(k, 21, seed);
            const float x = (float(k) - float(splinters - 1) / 2) * trunk / float(splinters);
            sticks.push_back({x, 0, 0, x + (unit(h) - 0.5f), 0, 2 + unit(h >> 8) * (3 + trunk), std::max(1.0f, trunk / float(splinters)), 1, h});
        }
        const int stubs = 2 + int(hash(5, 6, seed) % 2);
        const float phase = unit(hash(3, 4, seed)) * 6.2831853f;
        for (int k = 0; k < stubs; ++k) {
            const auto h = hash(k, 9, seed);
            grow(sticks, 0, 0, -float(radius) * (0.4f + 1.6f * unit(h >> 8)), phase + float(k) * 2.4f, 0.55f,
                 float(radius) * (0.5f + 0.4f * unit(h >> 16)), std::max(1.0f, trunk * 0.4f), radius > 14 ? 1 : 0, h);
        }
    }
    const int limbs = radius > 30 ? 5 : 4;
    const float girth = std::max(1.0f, std::clamp(float(radius) * 0.2f, 2.0f, 9.0f) * 0.5f);
    const float phase = unit(hash(3, 4, seed)) * 6.2831853f;
    for (int k = 0; k < limbs && !broken; ++k) {
        const auto h = hash(k, 9, seed);
        // From the very top of the trunk, which is the picture's anchor.
        grow(sticks, 0, 0, 0, phase + (float(k) + unit(h) * 0.5f) * 6.2831853f / float(limbs),
             narrow ? 1.2f : 0.95f, float(radius) * (narrow ? 0.44f : 0.5f), girth, radius > 20 ? 3 : 2, h);
    }
    // South is down the screen and nearer the camera; height shows by the cosine of the pitch.
    const float s = std::sin(pitch * 0.01745329f), c = std::cos(pitch * 0.01745329f);
    std::sort(sticks.begin(), sticks.end(), [](const Stick& a, const Stick& b) { return a.y0 + a.y1 < b.y0 + b.y1; });
    float left = 0, right = 0, top = 0, foot = 0;
    for (const Stick& k : sticks) {
        left = std::min({left, k.x0, k.x1});
        right = std::max({right, k.x0, k.x1});
        top = std::min({top, k.y0 * s - k.z0 * c, k.y1 * s - k.z1 * c});
        foot = std::max({foot, k.y0 * s - k.z0 * c, k.y1 * s - k.z1 * c});
    }
    const int pad = 6, ax = int(-left) + pad, ay = int(-top) + pad;
    Canvas canvas(ax + int(right) + pad + 1, ay + int(foot) + pad + 1);
    for (const Stick& k : sticks) {
        limb(canvas, float(ax) + k.x0, float(ay) + k.y0 * s - k.z0 * c, float(ax) + k.x1, float(ay) + k.y1 * s - k.z1 * c, k.w0,
             k.w1, bark, k.seed);
    }
    return {canvas.w, canvas.h, ax, canvas.h - 1 - ay, std::move(canvas.px)};
}

// A conifer: tiers of drooping boughs, each wider than the one above, lit on
// the left, each tier shading the top of the next.
void spire(Canvas& c, float cx, int top, int bottom, float radius, const Tones& tones, std::uint32_t seed) {
    const int height = std::max(1, bottom - top);
    const int tiers = std::clamp(height / 8, 2, 11);
    const auto envelope = [&](float f) { return radius * std::pow(std::clamp(f, 0.0f, 1.0f), 0.8f); };
    for (int y = top; y < bottom; ++y) {
        const float f = (float(y - top) + 0.5f) / float(height);
        const float in_tiers = f * float(tiers);
        const int tier = std::min(tiers - 1, int(in_tiers));
        const float u = in_tiers - float(tier);
        const float half = std::max(0.6f, envelope(float(tier + 1) / float(tiers)) * (0.42f + 0.58f * u));
        const float skirt = envelope(float(tier) / float(tiers)); // the tier above, at its widest
        for (int x = int(std::floor(cx - half)) - 1; x <= int(std::ceil(cx + half)) + 1; ++x) {
            const float off = float(x) + 0.5f - cx, s = off / half;
            const auto h = hash(x, y, seed);
            if (std::fabs(s) > 1 || (half > 3 && std::fabs(s) > 0.84f && (h & 1))) {
                continue;
            }
            float t = 2.15f - 1.25f * s + 0.9f * (0.45f - u);
            if (tier > 0 && u < 0.3f && std::fabs(off) < skirt) {
                t -= 1.3f; // under the boughs above
            }
            t += spray_tone(spray_at(s, half)) * u + (unit(h >> 8) - 0.5f) * 0.6f;
            if (u > 0.82f && tier < tiers - 1) {
                t -= 1.0f; // the hem of each tier
            }
            c.put(x, y, tones[std::size_t(std::clamp(int(std::floor(t + 0.5f)), 0, 4))]);
        }
    }
}

// One tier of a conifer, seen from above and in front: sprays drooping
// outward from the trunk to a hem of points, lit on the left, dark at the
// top where the tier above hangs over it.
void bough(Canvas& c, float cx, int top, int bottom, float radius, bool tip, const Tones& tones, std::uint32_t seed) {
    const int height = std::max(1, bottom - top);
    const float deepest = std::min(6.0f, float(height) * 0.4f);
    for (int y = top; y < bottom; ++y) {
        const float u = (float(y - top) + 0.5f) / float(height);
        const float half = std::max(0.6f, radius * (tip ? std::pow(u, 0.8f) : 0.28f + 0.72f * std::pow(u, 0.7f)));
        for (int x = int(std::floor(cx - half)) - 1; x <= int(std::ceil(cx + half)) + 1; ++x) {
            const float off = float(x) + 0.5f - cx, s = off / half;
            if (std::fabs(s) > 1) {
                continue;
            }
            // The hem: each spray ends in a point, some longer than others,
            // and the hem curls up at the sides.
            const Spray spray = spray_at(s, radius);
            const float length = deepest * (0.45f + 0.55f * unit(hash(spray.index, 7, seed)));
            const float point = length * (1 - std::fabs(spray.across - 0.5f) * 2);
            const float curl = s * s * s * s * std::min(4.0f, float(height) / 4);
            if (height > 5 && float(bottom - y) <= deepest - point + curl) {
                continue;
            }
            float t = 2.0f - 1.2f * s + 1.1f * (u - 0.55f) + spray_tone(spray) * std::min(1.0f, u * 1.6f) +
                      (unit(hash(spray.index, 3, seed)) - 0.5f) * 0.8f;
            if (!tip && u < 0.32f && std::fabs(off) < radius * 0.7f) {
                t -= 1.4f; // under the boughs above
            }
            t += (unit(hash(x, y, seed) >> 8) - 0.5f) * 0.6f;
            c.put(x, y, tones[std::size_t(std::clamp(int(std::floor(t + 0.5f)), 0, 4))]);
        }
    }
    const Rgb ink = scaled(tones[0], 0.72f);
    for (int x = int(cx - radius) - 1; x <= int(cx + radius) + 1; ++x) {
        for (int y = bottom - 1; y > top; --y) {
            if (c.solid(x, y)) {
                c.put(x, y, ink); // the underside of the hem
                break;
            }
        }
    }
}

struct BladeTones {
    Rgb root, shade, body, lit;
};

// A clump of grass or reeds: blades fanning from one spot, dark at the root
// and lit at the tip, some carrying a flower or a seed head.
void clump(Canvas& c, int ax, int bottom, int height, int spread, bool reed, const BladeTones& tones, bool flowers,
           Rgb petal, std::uint32_t seed) {
    const int count = (reed ? 4 : 5) + spread + int(hash(0, 0, seed) & 1);
    for (int k = 0; k < count; ++k) {
        const auto h = hash(k, 1, seed);
        const float place = (float(k) + 0.5f) / float(count) * 2 - 1; // -1 to 1 across the clump
        const float base_x = float(ax) + place * float(spread) * 0.45f;
        const int length = std::max(2, int(float(height) * (0.55f + 0.45f * unit(h)) + 0.5f));
        const float lean = (place + (unit(h >> 16) - 0.5f) * 0.8f) * float(spread) * (reed ? 0.5f : 1.1f);
        int tip_x = int(base_x), tip_y = bottom;
        for (int i = 0; i < length; ++i) {
            const float t = float(i) / float(length);
            tip_x = int(std::floor(base_x + lean * t * t + 0.5f));
            tip_y = bottom - i;
            const Rgb tone = i == 0 ? tones.root : t > 0.8f ? tones.lit : t < 0.55f && ((h >> (i & 7)) & 1) ? tones.shade : tones.body;
            c.put(tip_x, tip_y, tone);
            if (height > 16 && t < 0.4f && (k & 1)) {
                c.put(tip_x + 1, tip_y, tones.shade); // thicker at the foot
            }
        }
        if (reed && k % 3 == 1 && length > 9 && flowers) {
            for (int i = 0; i < 3 + int(length > 16); ++i) {
                c.put(tip_x, tip_y - i + 2, i == 0 ? Rgb{102, 73, 37} : Rgb{140, 99, 46});
            }
        } else if (!reed && flowers && k % 3 == 0 && length > 4) {
            c.put(tip_x, tip_y, petal);
            c.put(tip_x - 1, tip_y, petal);
            c.put(tip_x + 1, tip_y, scaled(petal, 0.82f));
            c.put(tip_x, tip_y - 1, scaled(petal, 1.1f));
            if (length > 10) {
                c.put(tip_x, tip_y + 1, scaled(petal, 0.7f));
                c.put(tip_x, tip_y, Rgb{244, 226, 130});
            }
        }
    }
}

// A plant under water: a few wavering stems with small leaves to either
// side, dark at the foot.
void weed(Canvas& c, int ax, int bottom, int height, int spread, const BladeTones& tones, std::uint32_t seed) {
    const int stems = 2 + spread / 2 + int(hash(0, 0, seed) & 1);
    for (int k = 0; k < stems; ++k) {
        const auto h = hash(k, 3, seed);
        const float base = float(ax) + (float(k) - float(stems - 1) / 2) * float(spread) * 0.7f;
        const int length = std::max(2, int(float(height) * (0.6f + 0.4f * unit(h)) + 0.5f));
        const float phase = unit(h >> 8) * 6.2831853f, swing = 0.8f + float(spread) * 0.25f;
        for (int i = 0; i < length; ++i) {
            const float t = float(i) / float(length);
            const int x = int(std::floor(base + std::sin(phase + float(i) * 0.55f) * swing * t + 0.5f)), y = bottom - i;
            c.put(x, y, i == 0 ? tones.root : t > 0.75f ? tones.lit : tones.body);
            if (i > 1 && i % 3 == int(h & 1)) {
                c.put(x + ((i / 3 + k) & 1 ? 1 : -1), y, tones.shade); // a leaf
            }
        }
    }
}

// Leaves floating on the water, as the three-quarter camera sees them: round
// pads flattened by the angle, each with a notch, lit at the far edge.
void pads(Canvas& c, float cx, float cy, float radius, int count, const Tones& tones, bool flower, Rgb petal, std::uint32_t seed) {
    for (int k = 0; k < count; ++k) {
        const auto h = hash(k, 5, seed);
        const float a = unit(h) * 6.2831853f, out = count > 1 ? radius * 0.95f : 0;
        const float r = radius * (0.75f + 0.25f * unit(h >> 8));
        const float px = cx + std::cos(a + float(k) * 2.4f) * out, py = cy + std::sin(a + float(k) * 2.4f) * out * 0.6f;
        const float notch = unit(h >> 16) * 6.2831853f;
        for (int y = int(py - r) - 1; y <= int(py + r) + 1; ++y) {
            for (int x = int(px - r) - 1; x <= int(px + r) + 1; ++x) {
                const float dx = (float(x) + 0.5f - px) / r, dy = (float(y) + 0.5f - py) / (r * 0.6f);
                if (dx * dx + dy * dy >= 1) {
                    continue;
                }
                const float turn = std::atan2(dy, dx) - notch;
                if (std::fabs(std::remainder(turn, 6.2831853f)) < 0.3f && dx * dx + dy * dy > 0.08f) {
                    continue; // the notch
                }
                c.put(x, y, tones[std::size_t(dy < -0.45f ? 3 : dy > 0.5f ? 1 : 2)]);
            }
        }
        if (flower && k == 0) {
            const int fx = int(px), fy = int(py - r * 0.2f);
            c.put(fx - 1, fy, petal), c.put(fx + 1, fy, scaled(petal, 0.85f)), c.put(fx, fy - 1, scaled(petal, 1.08f));
            c.put(fx, fy + 1, scaled(petal, 0.8f));
            c.put(fx, fy, Rgb{244, 226, 130});
        }
    }
}

// One tongue of flame at one moment of its flicker: wide at the foot and
// drawn up to a wavering point, white-hot at the heart, then yellow, orange
// and red at the edge, with a spark or two thrown off above.
void flame(Canvas& c, int ax, int bottom, int height, int frame, std::uint32_t seed) {
    const float radius = std::clamp(float(height) * 0.3f, 2.0f, 9.0f), phase = unit(hash(1, 1, seed)) * 6.2831853f;
    constexpr Rgb heart{255, 246, 196}, yellow{255, 214, 84}, orange{244, 140, 40}, red{206, 70, 30};
    for (int i = 0; i < height; ++i) {
        const float f = (float(i) + 0.5f) / float(height);
        const float half = std::max(0.5f, radius * std::pow(1 - f, 0.65f) * (0.82f + 0.18f * std::sin(f * 9 + float(frame) * 2.1f + phase)));
        const float lean = std::sin(f * 4.2f + float(frame) * 1.5708f + phase) * radius * 0.55f * f;
        for (int x = int(std::floor(float(ax) + lean - half)); x <= int(std::ceil(float(ax) + lean + half)); ++x) {
            const float d = std::fabs(float(x) + 0.5f - (float(ax) + 0.5f + lean)) / half;
            if (d > 1) {
                continue;
            }
            c.glow(x, bottom - i, d < 0.4f && f < 0.4f ? heart : d < 0.68f && f < 0.68f ? yellow : d > 0.85f || f > 0.85f ? red : orange);
        }
    }
    for (int k = 0; k < 2; ++k) {
        const auto h = hash(k, frame, seed);
        c.glow(ax + int(h % 7) - 3, bottom - height - 1 - int((h >> 8) % 4), k ? orange : yellow);
    }
}

// A puff of smoke: a few rounded masses, lighter above, wearing into holes
// as it thins.
void puff(Canvas& c, float cx, float cy, float radius, int thin, std::uint32_t seed) {
    constexpr Rgb pale{156, 152, 148}, mid{116, 112, 110}, dark{80, 76, 76};
    for (int k = 0; k < 4; ++k) {
        const auto h = hash(k, 2, seed);
        const float a = unit(h) * 6.2831853f, r = radius * (0.55f + 0.25f * unit(h >> 8));
        const float px = cx + std::cos(a) * radius * 0.4f, py = cy + std::sin(a) * radius * 0.3f;
        for (int y = int(py - r) - 1; y <= int(py + r) + 1; ++y) {
            for (int x = int(px - r) - 1; x <= int(px + r) + 1; ++x) {
                const float dx = (float(x) + 0.5f - px) / r, dy = (float(y) + 0.5f - py) / r;
                const float d = dx * dx + dy * dy;
                if (d >= 1 || (thin == 2 && ((x + y) & 1)) || (thin == 1 && d > 0.45f && ((x + y) & 1))) {
                    continue;
                }
                c.put(x, y, dx + dy < -0.5f ? pale : dx + dy > 0.7f ? dark : mid);
            }
        }
    }
}

Image finish(Canvas& canvas, int anchor_x, int anchor_y = 0) {
    return {canvas.w, canvas.h, anchor_x, anchor_y, std::move(canvas.px)};
}

} // namespace

std::uint64_t Spec::key() const {
    std::uint64_t k = std::uint64_t(kind);
    k = k * 1024 + height;
    k = k * 512 + radius;
    k = k * 256 + r;
    k = k * 256 + g;
    k = k * 256 + b;
    k = k * 32 + state;
    k = k * 8 + flower;
    k = k * 16 + variant;
    k = k * 4 + form;
    return k * 4 + view;
}

Image paint(const Spec& spec) {
    const std::uint32_t seed = phd::hash3(int(spec.key() & 0x7fffffff), int(spec.key() >> 31), 0x3c6ef372u);
    const Rgb pigment{spec.r, spec.g, spec.b};
    const Tones tones = leaf_tones(pigment);
    const bool is_bare = spec.state & bare;
    const int height = std::max<int>(spec.height, 3), radius = std::max<int>(spec.radius, 2);

    switch (spec.kind) {
    case Kind::crown: {
        // The radius is the crown's reach as the world has it; the form sets
        // how much of that the leaves fill, across and up.
        const Form form = Form(spec.form & 3);
        const float rx = float(radius) * (form == Form::columnar ? 0.68f : 1.0f);
        const float ry = float(radius) * (form == Form::spreading ? 0.6f : form == Form::columnar ? 1.2f : 0.88f);
        // Clumps at the rim reach a little past the crown's ellipse.
        Canvas c(radius * 2 + 13, int(ry) * 2 + 15);
        const int ax = c.w / 2, bottom = c.h - 1;
        const float cy = float(int(ry) + 7);
        if (is_bare) {
            return bare_crown(radius, form == Form::columnar, (spec.state & pale) ? 2 : 1,
                              view_pitch[std::min<int>(spec.view, views - 1)],
                              phd::hash3(radius, spec.variant, 0x2c1b3c6du), // the same tree from every pitch
                              (spec.state & dead) != 0);
        } else {
            if (form == Form::open || form == Form::spreading) {
                // Limbs reaching into the crown, seen wherever the leaves part.
                const float girth = std::clamp(float(radius) * 0.09f, 1.0f, 4.0f);
                for (int k = -2; k <= 2; ++k) {
                    const auto h = hash(k, 13, seed);
                    limb(c, float(ax), cy + ry * 0.3f, float(ax) + (float(k) + unit(h) - 0.5f) * rx * 0.3f,
                         cy - ry * (0.15f + 0.3f * unit(h >> 16)), girth, 1, (spec.state & pale) ? 2 : 0, h, 1e9f);
                }
            }
            canopy(c, float(ax) + 0.5f, cy, rx, ry, tones, seed, form);
        }
        return finish(c, ax, bottom - int(cy));
    }
    case Kind::bough: {
        Canvas c(radius * 2 + 5, height + 2);
        const int ax = c.w / 2;
        bough(c, float(ax) + 0.5f, 1, c.h, float(radius), (spec.state & tip) != 0, tones, seed);
        return finish(c, ax);
    }
    case Kind::trunk: {
        // The radius is the girth at the ground.
        const int girth = std::clamp<int>(spec.radius, 1, 9);
        Canvas c(girth + 4, height + 1);
        const int ax = c.w / 2, bottom = c.h - 1;
        const int bark = (spec.state & pale) ? 2 : is_bare ? 1 : 0;
        limb(c, float(ax), float(bottom), float(ax), 0, float(girth), std::max(1.0f, float(girth) * 0.7f), bark, seed);
        const Rgb foot = bark == 2 ? pale_dark : bark == 1 ? twig_dark : bark_dark;
        c.put(ax - girth / 2 - 1, bottom, scaled(foot, 0.9f)); // the root flare
        c.put(ax + (girth + 1) / 2, bottom, scaled(foot, 0.8f));
        return finish(c, ax);
    }
    case Kind::cone_bush: {
        Canvas c(radius * 2 + 5, height + 2);
        const int ax = c.w / 2, bottom = c.h - 1;
        const float girth = std::clamp(float(radius) * 0.16f, 1.0f, 7.0f);
        limb(c, float(ax), float(bottom), float(ax), float(bottom - height / 2), girth, 1, false, seed,
             float(bottom - 1) + 0.5f);
        spire(c, float(ax) + 0.5f, 1, bottom, float(radius), tones, seed);
        return finish(c, ax);
    }
    case Kind::bush: {
        Canvas c(radius * 2 + 11, height + 9);
        const int ax = c.w / 2, bottom = c.h - 1;
        if (is_bare) {
            for (int k = 0; k < 5; ++k) {
                twigs(c, float(ax) + float(k - 2) * 0.8f, float(bottom), float(k - 2) * 0.42f, float(height) * 0.6f, 1,
                      height > 12 ? 2 : 1, hash(k, 5, seed));
            }
        } else {
            // A mound of leaves resting on the ground, its underside rounded
            // and only just cut by the foot of the canvas.
            const float ry = float(height) * 0.53f;
            canopy(c, float(ax) + 0.5f, float(bottom) - ry * 0.86f, float(radius), ry, tones, seed);
        }
        return finish(c, ax);
    }
    case Kind::flame: {
        Canvas c(24, height + 8);
        flame(c, c.w / 2, c.h - 1, height, spec.variant & 3, phd::hash3(height, 3, 0x1e57u)); // the same tongue in each frame
        return finish(c, c.w / 2);
    }
    case Kind::smoke: {
        Canvas c(radius * 3 + 6, radius * 3 + 6);
        puff(c, float(c.w) / 2, float(c.h) / 2, float(radius), spec.variant % 3, seed);
        return finish(c, c.w / 2, c.h / 2);
    }
    case Kind::waterweed: {
        Canvas c(radius * 2 + 9, height + 3);
        const int ax = c.w / 2, bottom = c.h - 1;
        const palette::Rgb p{float(spec.r), float(spec.g), float(spec.b)};
        const BladeTones green{bytes(palette::scale(p, 0.5f)), bytes(palette::scale(p, 0.72f)), bytes(palette::scale(p, 0.95f)),
                               bytes({p.r * 1.15f + 6, p.g * 1.15f, p.b * 1.05f})};
        const BladeTones brown{{74, 62, 40}, {98, 82, 50}, {124, 106, 66}, {146, 128, 84}};
        weed(c, ax, bottom, height, radius, is_bare ? brown : green, seed);
        return finish(c, ax);
    }
    case Kind::pad: {
        const int count = 1 + spec.variant % 3;
        Canvas c(radius * 4 + 5, radius * 3 + 5);
        pads(c, float(c.w) / 2, float(c.h) / 2, float(radius), count, tones, (spec.state & flowering) != 0,
             bytes(palette::petals[spec.flower % 6]), seed);
        return finish(c, c.w / 2, c.h / 2);
    }
    case Kind::grass:
    case Kind::reed: {
        const bool reed = spec.kind == Kind::reed;
        Canvas c(radius * 2 + 7, height + 4);
        const int ax = c.w / 2, bottom = c.h - 1;
        // Blades from the plant's pigment: root, body, lit, tip.
        const palette::Rgb p{float(spec.r), float(spec.g), float(spec.b)};
        const BladeTones green{bytes(palette::scale(p, 0.56f)), bytes(palette::scale(p, 0.9f)),
                               bytes({p.r * 1.25f + 8, p.g * 1.25f, p.b * 1.12f}),
                               bytes({p.r * 1.45f + 28, p.g * 1.42f - 4, p.b * 1.3f})};
        const BladeTones straw{{109, 91, 49}, {142, 119, 65}, {178, 154, 91}, {203, 186, 128}};
        // Reeds carry seed heads whenever they are green; grasses flower in season.
        const bool heads = reed ? !is_bare : (spec.state & flowering) != 0;
        clump(c, ax, bottom, height, radius, reed, is_bare ? straw : green, heads, bytes(palette::petals[spec.flower % 6]), seed);
        return finish(c, ax);
    }
    }
    return {};
}

} // namespace art::sprites
