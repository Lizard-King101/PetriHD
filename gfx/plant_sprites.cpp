#include "plant_sprites.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "math.hpp"

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
};

float unit(std::uint32_t h) { return float(h & 0xffff) / 65535.0f; }
std::uint32_t hash(int a, int b, std::uint32_t seed) { return phd::hash3(a, b, seed); }

using Tones = std::array<Rgb, 5>;

// Five leaf tones from one pigment: deep shade, shade, body, lit, highlight.
// Shade leans blue, light leans yellow.
Tones leaf_tones(Rgb p) {
    const auto b = [](float v) { return std::uint8_t(std::clamp(v, 0.0f, 255.0f)); };
    return {{
        {b(p.r * .29f + 8), b(p.g * .35f + 12), b(p.b * .44f + 13)},
        {b(p.r * .42f + 12), b(p.g * .49f + 19), b(p.b * .43f + 12)},
        {b(p.r * .60f + 24), b(p.g * .70f + 23), b(p.b * .46f + 11)},
        {b(p.r * .88f + 37), b(p.g * .86f + 39), b(p.b * .49f + 9)},
        {b(p.r * .92f + 63), b(p.g * .88f + 55), b(p.b * .50f + 16)},
    }};
}

constexpr Rgb bark_light{150, 112, 78}, bark_mid{112, 82, 57}, bark_dark{74, 54, 40};
constexpr Rgb twig_light{128, 112, 98}, twig_mid{104, 92, 82}, twig_dark{72, 62, 56};

// A crown of leaves: several overlapping rounded masses, lit from the upper
// left, with a leaf-sized pattern laid over the shading so tones break into
// dabs instead of bands. Edges are ragged and the underside is dark.
void canopy(Canvas& c, float cx, float cy, float rx, float ry, const Tones& tones, std::uint32_t seed) {
    struct Blob {
        float x, y, r, z;
    };
    // Round masses spread through the crown's ellipse: more of them for a
    // crown that is long one way, so a tall shrub is a stack of clumps.
    const float small = std::min(rx, ry), large = std::max(rx, ry);
    const int count = std::clamp(int((3 + small / 7) * large / small), 3, 14);
    std::vector<Blob> blobs;
    blobs.push_back({cx, cy, small * 0.72f, 0});
    const float phase = unit(hash(1, 2, seed)) * 6.2831853f;
    for (int i = 0; i < count; ++i) {
        const auto h = hash(i, 7, seed);
        const float a = phase + float(i) * 2.3999632f;
        const float out = 0.68f * std::sqrt((float(i) + 0.5f) / float(count));
        const float r = std::max(1.5f, std::min(small * (0.44f + 0.2f * unit(h)), small * (1.02f - out)));
        const float bx = std::cos(a) * out * (rx - r * 0.55f), by = std::sin(a) * out * (ry - r * 0.55f);
        // Masses lower on the screen are nearer the camera.
        blobs.push_back({cx + bx, cy + by, r, by * 0.7f + unit(h >> 16) * r * 0.3f});
    }

    const int x0 = int(std::floor(cx - rx)) - 1, x1 = int(std::ceil(cx + rx)) + 1;
    const int y0 = int(std::floor(cy - ry)) - 1, y1 = int(std::ceil(cy + ry)) + 1;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    std::vector<std::int8_t> tone(std::size_t(bw) * bh, -1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float best = -1e9f, nx = 0, ny = 0, nz = 0, cover = 0;
            for (const auto& b : blobs) {
                const float dx = (float(x) + 0.5f - b.x) / b.r, dy = (float(y) + 0.5f - b.y) / b.r;
                const float d2 = dx * dx + dy * dy;
                if (d2 >= 1) {
                    continue;
                }
                const float up = std::sqrt(1 - d2), z = b.z + up * b.r;
                cover = std::max(cover, up);
                if (z > best) {
                    best = z;
                    nx = dx;
                    ny = dy;
                    nz = up;
                }
            }
            if (best < -1e8f) {
                continue;
            }
            const auto h = hash(x, y, seed);
            if (cover < 0.34f && (h & 3) == 0) {
                continue; // a gap between leaves at the silhouette
            }
            // Light from the upper left: on each mass, and on the crown as a whole.
            const float light = -0.50f * nx - 0.62f * ny + 0.60f * nz;
            const float wx = (float(x) + 0.5f - cx) / rx, wy = (float(y) + 0.5f - cy) / ry;
            const float whole = -0.55f * wx - 0.75f * wy;
            float t = 1.9f + 1.35f * light + 1.5f * whole;
            if (nz < 0.5f && cover > nz + 0.15f) {
                t -= 1.3f * (1 - nz * 2); // the crease where one mass meets another
            }
            // Leaf dabs: each texel belongs to the nearest of a scatter of
            // leaves, one to a 3-texel square and anywhere within it, so they
            // fall in no rows. A leaf has its own tone, lit at its top left
            // and shaded at its bottom right.
            const int gx = phd::floor_div(x, 3), gy = phd::floor_div(y, 3);
            float near = 1e9f, lx = 0, ly = 0;
            std::uint32_t leaf = 0;
            for (int j = gy - 1; j <= gy + 1; ++j) {
                for (int i = gx - 1; i <= gx + 1; ++i) {
                    const auto lh = hash(i, j, seed ^ 0x51ed27u);
                    const float px = float(i * 3) + unit(lh) * 3, py = float(j * 3) + unit(lh >> 16) * 3;
                    const float ddx = float(x) + 0.5f - px, ddy = float(y) + 0.5f - py;
                    const float d = ddx * ddx + ddy * ddy;
                    if (d < near) {
                        near = d;
                        lx = ddx;
                        ly = ddy;
                        leaf = lh;
                    }
                }
            }
            float dab = (unit(hash(int(leaf & 0xffff), 11, seed)) - 0.5f) * 0.9f;
            const float side = lx + ly;
            dab += side < -0.9f ? 0.5f : side > 0.9f ? -0.5f : 0.0f;
            tone[std::size_t(y - y0) * bw + (x - x0)] = std::int8_t(std::clamp(int(std::floor(t + dab + 0.5f)), 0, 4));
        }
    }
    const auto at = [&](int x, int y) {
        return x < x0 || x > x1 || y < y0 || y > y1 ? std::int8_t(-1) : tone[std::size_t(y - y0) * bw + (x - x0)];
    };
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int t = at(x, y);
            if (t < 0) {
                continue;
            }
            if (at(x, y + 1) < 0 && float(y) > cy) {
                t = 0; // the underside
            } else if (at(x, y - 1) < 0 && float(y) < cy && t < 4 && float(x) < cx + rx * 0.3f) {
                ++t; // the lit rim
            }
            c.put(x, y, tones[std::size_t(t)]);
        }
    }
}

// A trunk or limb from (x0, y0) up to (x1, y1), tapering, lit on its left.
// Rows above `shade_above` are in the crown's shadow.
void limb(Canvas& c, float x0, float y0, float x1, float y1, float w0, float w1, bool grey, std::uint32_t seed,
          float shade_above = -1e9f) {
    const Rgb light = grey ? twig_light : bark_light, mid = grey ? twig_mid : bark_mid, dark = grey ? twig_dark : bark_dark;
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
            // Needles in strokes that droop outward on both sides.
            const int stroke = ((x + (s < 0 ? y : -y)) % 3 + 3) % 3;
            t += (stroke == 0 ? 0.7f : stroke == 1 ? 0.0f : -0.6f) + (unit(h >> 8) - 0.5f) * 0.5f;
            c.put(x, y, tones[std::size_t(std::clamp(int(std::floor(t + 0.5f)), 0, 4))]);
        }
    }
}

// One tier of a conifer, seen from above and in front: boughs drooping
// outward from the trunk to a ragged hem, lit on the left, dark at the top
// where the tier above hangs over it.
void bough(Canvas& c, float cx, int top, int bottom, float radius, bool tip, const Tones& tones, std::uint32_t seed) {
    const int height = std::max(1, bottom - top);
    for (int y = top; y < bottom; ++y) {
        const float u = (float(y - top) + 0.5f) / float(height);
        const float half = std::max(0.6f, radius * (tip ? std::pow(u, 0.8f) : 0.28f + 0.72f * std::pow(u, 0.7f)));
        for (int x = int(std::floor(cx - half)) - 1; x <= int(std::ceil(cx + half)) + 1; ++x) {
            const float off = float(x) + 0.5f - cx, s = off / half;
            const auto h = hash(x, y, seed);
            if (std::fabs(s) > 1 || (half > 3 && std::fabs(s) > 0.84f && (h & 1))) {
                continue;
            }
            // The hem: boughs end at different lengths, and curl up at the sides.
            const int hang = int(hash(phd::floor_div(x, 2), 7, seed) % 3) + int(s * s * s * s * std::min(4.0f, float(height) / 4));
            if (y >= bottom - hang && height > 5) {
                continue;
            }
            float t = 2.15f - 1.25f * s + 0.9f * (0.45f - u);
            if (!tip && u < 0.3f && std::fabs(off) < radius * 0.7f) {
                t -= 1.3f; // under the boughs above
            }
            // Needles in strokes that droop outward on both sides.
            const int stroke = ((x + (s < 0 ? y : -y)) % 3 + 3) % 3;
            t += (stroke == 0 ? 0.7f : stroke == 1 ? 0.0f : -0.6f) + (unit(h >> 8) - 0.5f) * 0.5f;
            c.put(x, y, tones[std::size_t(std::clamp(int(std::floor(t + 0.5f)), 0, 4))]);
        }
    }
    for (int x = int(cx - radius) - 1; x <= int(cx + radius) + 1; ++x) {
        for (int y = bottom - 1; y > top; --y) {
            if (c.solid(x, y)) {
                c.put(x, y, tones[0]); // the underside of the hem
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
    k = k * 8 + state;
    k = k * 8 + flower;
    return k * 16 + variant;
}

Image paint(const Spec& spec) {
    const std::uint32_t seed = phd::hash3(int(spec.key() & 0x7fffffff), int(spec.key() >> 31), 0x3c6ef372u);
    const Rgb pigment{spec.r, spec.g, spec.b};
    const Tones tones = leaf_tones(pigment);
    const bool is_bare = spec.state & bare;
    const int height = std::max<int>(spec.height, 3), radius = std::max<int>(spec.radius, 2);

    switch (spec.kind) {
    case Kind::crown: {
        const float ry = float(radius) * 0.88f;
        Canvas c(radius * 2 + 5, int(ry) * 2 + 7);
        const int ax = c.w / 2, bottom = c.h - 1;
        const float cy = float(int(ry) + 3);
        if (is_bare) {
            // The trunk comes up to the crown's middle; the branches fork from just below it.
            const float girth = std::clamp(float(radius) * 0.2f, 2.0f, 9.0f);
            for (int k = -1; k <= 1; ++k) {
                twigs(c, float(ax), cy + ry * 0.55f, float(k) * 0.6f, ry * 0.7f, std::max(1.0f, girth * 0.5f),
                      radius > 20 ? 3 : 2, hash(k, 9, seed));
            }
        } else {
            canopy(c, float(ax) + 0.5f, cy, float(radius), ry, tones, seed);
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
        limb(c, float(ax), float(bottom), float(ax), 0, float(girth), std::max(1.0f, float(girth) * 0.7f), is_bare, seed);
        c.put(ax - girth / 2 - 1, bottom, scaled(is_bare ? twig_dark : bark_dark, 0.9f)); // the root flare
        c.put(ax + (girth + 1) / 2, bottom, scaled(is_bare ? twig_dark : bark_dark, 0.8f));
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
        Canvas c(radius * 2 + 5, height + 3);
        const int ax = c.w / 2, bottom = c.h - 1;
        if (is_bare) {
            for (int k = 0; k < 5; ++k) {
                twigs(c, float(ax) + float(k - 2) * 0.8f, float(bottom), float(k - 2) * 0.42f, float(height) * 0.6f, 1,
                      height > 12 ? 2 : 1, hash(k, 5, seed));
            }
        } else {
            // A dome sitting on the ground: the canvas cuts its underside flat.
            const float ry = float(height) * 0.6f;
            canopy(c, float(ax) + 0.5f, float(bottom) - ry * 0.62f, float(radius), ry, tones, seed);
        }
        return finish(c, ax);
    }
    case Kind::grass:
    case Kind::reed: {
        const bool reed = spec.kind == Kind::reed;
        Canvas c(radius * 2 + 7, height + 4);
        const int ax = c.w / 2, bottom = c.h - 1;
        const int hue = spec.r; // yellow-green to blue-green
        const BladeTones green{{55, 74, 40},
                               {std::uint8_t(93 - hue), 117, 51},
                               {std::uint8_t(139 - hue), 158, 65},
                               {std::uint8_t(180 - hue), 182, 77}};
        const BladeTones straw{{109, 91, 49}, {142, 119, 65}, {178, 154, 91}, {203, 186, 128}};
        static constexpr Rgb petals[] = {{239, 234, 196}, {236, 204, 92}, {212, 148, 174},
                                         {163, 125, 187}, {211, 104, 91}, {152, 162, 210}};
        // Reeds carry seed heads whenever they are green; grasses flower in season.
        const bool heads = reed ? !is_bare : (spec.state & flowering) != 0;
        clump(c, ax, bottom, height, radius, reed, is_bare ? straw : green, heads, petals[spec.flower % 6], seed);
        return finish(c, ax);
    }
    }
    return {};
}

} // namespace art::sprites
