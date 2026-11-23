#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// Plants as painted pixel-art sprites. Each is painted once on the CPU, as it
// looks from the three-quarter camera, at a fixed number of texels per cell:
// a bigger plant gets more pixels, never bigger ones. There is no GL here, so
// a headless renderer can draw exactly the same plants.
//
// A tree is painted in pieces, which the renderer stands at their own
// heights: a trunk, and a crown or a stack of boughs. As the camera tilts
// the pieces slide over one another, so a tree seen from above is its crown
// over its spot, and from the side a crown on a long trunk.
namespace art::sprites {

// One texel per art pixel at 64 px per cell through the half-size target.
constexpr int texels_per_cell = 32;
// The camera's pitch at that zoom, 55°: an upright metre is drawn `rise`
// times as long as a metre across the screen, a metre into the ground `lie`.
constexpr float rise = 0.574f;
constexpr float lie = 0.819f;

enum class Kind : std::uint8_t {
    crown,     // a broadleaf tree's ball of leaves, or its bare branches
    bough,     // one tier of a conifer
    trunk,     // painted longer than is ever shown: the renderer shows as much as the tilt leaves
    bush,
    cone_bush,
    grass,
    reed,
    waterweed, // a plant growing under water, standing on the bed
    pad,       // its leaves floating on the surface, seen at the camera's angle
    flame,     // one tongue of a fire; the variant is the frame of its flicker. It glows: see Image
    smoke,     // a puff; the variant is how thin it has grown
};

enum State : std::uint8_t {
    bare = 1,      // leafless wood, or withered grass
    flowering = 2,
    tip = 4,       // a conifer's topmost bough, drawn to a point
    pale = 8,      // white bark with dark scars, as a birch has
    dead = 16,     // a snag's top: the trunk broken off in splinters, and stubs of limbs below it
};

// The outline of a broadleaf crown.
enum class Form : std::uint8_t {
    round,     // a ball of leaves
    spreading, // broad, with a flat underside
    columnar,  // narrow and tall
    open,      // loose clumps with limbs showing between them
};

// Bare branches look quite different from the side and from above, so a
// directional crown is painted for each of these camera pitches, in degrees,
// and the renderer shows the nearest. (A ball of leaves looks much the same
// from anywhere, so round leafy crowns have one picture.)
constexpr int views = 3;
constexpr float view_pitch[views] = {55, 70, 90};
inline int view_for(float pitch_degrees) { return pitch_degrees < 62.5f ? 0 : pitch_degrees < 80 ? 1 : 2; }

// What a sprite looks like. Plants are quantised to a spec, so similar
// plants share a sprite.
struct Spec {
    Kind kind = Kind::bush;
    std::uint16_t height = 0; // texels from the bottom to the top; a crown takes its height from its radius
    std::uint16_t radius = 0; // texels: half the crown's or the clump's width
    std::uint8_t r = 0, g = 0, b = 0; // leaf pigment
    std::uint8_t state = 0;
    std::uint8_t flower = 0;  // petal colour, 0-5
    std::uint8_t variant = 0; // a different shape for the same numbers
    std::uint8_t form = 0;    // a crown's Form
    std::uint8_t view = 0;    // which of `view_pitch` it is painted for

    std::uint64_t key() const;
};

struct Image {
    int width = 0, height = 0;
    int anchor_x = 0; // the column that stands on the plant's spot
    int anchor_y = 0; // rows above the bottom row: 0 for what stands on the ground, a crown's middle
    // Alpha tags clear pixels (0), opaque pigment (255), glowing pixels
    // (200), and the tree foliage that X-ray can see through (240).
    std::vector<std::uint8_t> rgba;
};
constexpr std::uint8_t glow_alpha = 200, foliage_alpha = 240;

Image paint(const Spec& spec);

// A step that grows with the size, so big plants don't each get a sprite.
inline int quantise(float texels, int least = 1) {
    const int v = int(texels + 0.5f);
    const int step = std::max(least, v < 24 ? 1 : v < 64 ? 2 : v < 128 ? 4 : v < 256 ? 8 : 16);
    return (v + step / 2) / step * step;
}

} // namespace art::sprites
