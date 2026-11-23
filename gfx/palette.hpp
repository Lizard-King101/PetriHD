#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "world.hpp"

// One palette for every view. The bird's-eye terrain, the painted ground of
// the close view and the plant sprites all take their colours from here, so
// zooming in changes the detail and not the colours.
namespace art::palette {

struct Rgb {
    float r, g, b;
};

inline Rgb mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
inline Rgb scale(Rgb a, float s) { return {a.r * s, a.g * s, a.b * s}; }

// What the ground is made of, before anything grows on it.
inline constexpr Rgb substrate[int(phd::Substrate::count)] = {
    {120, 97, 69},   // loam
    {208, 190, 142}, // sand
    {150, 112, 82},  // clay
    {72, 58, 46},    // peat
    {128, 124, 118}, // rock
    {150, 140, 122}, // gravel
};

// The bed of a lake or river, seen through shallow water: pale to dark.
inline constexpr Rgb bed_pale{141, 127, 94}, bed_dark{104, 104, 80};
inline constexpr Rgb waterline{196, 192, 143}; // the pale line where water meets the bank
inline constexpr Rgb damp_soil{115, 92, 61};
inline constexpr Rgb litter{86, 68, 46};       // the forest floor
inline constexpr Rgb snow{238, 242, 248}, ice{204, 222, 236};
inline constexpr Rgb charred{44, 38, 34}, ash{150, 146, 140};

// Open water by its depth in metres: green-blue shallows to a dark deep.
inline Rgb water(float depth) {
    constexpr Rgb shallow{73, 150, 146}, mid{42, 130, 146}, deep{42, 111, 141}, deeper{26, 74, 124}, abyss{14, 36, 82};
    if (depth < 0.3f) {
        return shallow;
    }
    if (depth < 1.5f) {
        return mix(shallow, mid, (depth - 0.3f) / 1.2f);
    }
    if (depth < 5) {
        return mix(mid, deep, (depth - 1.5f) / 3.5f);
    }
    if (depth < 20) {
        return mix(deep, deeper, (depth - 5) / 15);
    }
    return mix(deeper, abyss, (depth - 20) / 60);
}

// The same depth bands as the translucent water shader, for the overhead bake.
inline Rgb shallows(Rgb bed, float depth) {
    const float t = std::clamp((depth - 3.0f) / 3.0f, 0.0f, 1.0f);
    const float transmission = std::min(0.45f, 0.74f / (1.0f + 0.70f * depth)) * (1.0f - t * t * (3.0f - 2.0f * t));
    const float opacity = std::min(1.0f - std::round(transmission * 24.0f) / 24.0f,
                                   0.22f + (std::floor(depth * 6.0f) + 0.5f) / 6.0f);
    return mix(bed, water(depth), opacity);
}

// A plant's leaf pigment now: its leaf gene from yellow-green to blue-green,
// then the season: autumn colour and bare winter wood for deciduous plants,
// straw for dormant grass, browning when it is failing.
Rgb leaf(const phd::Plant& plant, const phd::World& world);
Rgb flower(const phd::Plant& plant);
// The six petal colours, by the flower gene.
inline constexpr Rgb petals[6] = {{240, 236, 208}, {238, 208, 84}, {226, 150, 176},
                                  {158, 116, 194}, {211, 92, 80},  {128, 150, 222}};

// Five tones of foliage from one pigment: deep shade, shade, body, lit,
// highlight. Shade leans blue, light leans yellow.
using Tones = std::array<Rgb, 5>;
inline Tones leaf_tones(Rgb p) {
    return {{
        {p.r * .29f + 8, p.g * .35f + 12, p.b * .44f + 13},
        {p.r * .42f + 12, p.g * .49f + 19, p.b * .43f + 12},
        {p.r * .60f + 24, p.g * .70f + 23, p.b * .46f + 11},
        {p.r * .88f + 37, p.g * .86f + 39, p.b * .49f + 9},
        {p.r * .92f + 63, p.g * .88f + 55, p.b * .50f + 16},
    }};
}
// A point on those tones, 0 to 4.
inline Rgb leaf_tone(Rgb pigment, float t) {
    const Tones tones = leaf_tones(pigment);
    t = std::clamp(t, 0.0f, 4.0f);
    const int i = std::min(3, int(t));
    return mix(tones[std::size_t(i)], tones[std::size_t(i) + 1], t - float(i));
}

// Low growth covering the ground, from its pigment: dull at 0, sunlit at 1.
inline Rgb turf(Rgb p, float t) { return mix(p, {p.r * 1.3f + 10, p.g * 1.18f + 6, p.b * 1.05f}, t); }
// Grass in the shade of trees.
inline Rgb shaded_turf(Rgb p) { return {p.r * 0.56f, p.g * 0.64f, p.b * 0.8f}; }

} // namespace art::palette
