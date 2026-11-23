#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "batch.hpp"

// Animals as procedural bodies. A body is built anew each frame from what
// its organs add up to and how it is moving: a spine of rings from the tail
// to the snout, legs that step, paddle or tuck, fins, wings and eyes.
// It is drawn through the art target's pixel grid with stepped light and an
// ink outline (the shader is in tilt_view.cpp), so it reads as drawn pixel
// art while moving with a model's freedom. There is no GL here.
namespace gfx::creature {

struct Vertex {
    float x, y, z;             // cells; z is display height
    std::int8_t nx, ny, nz, _; // the normal, in 127ths
    float along, around;       // body pattern coordinates; on an eye, signed corner offsets in cells
    Color primary;             // its pigment; a is how it takes light (see below)
    Color secondary;           // its pattern's pigment; a is the pattern
};

// Vertex::primary.a: lit from one side; lit the same from both (a fin, a
// wing); or not lit at all (an eye).
constexpr std::uint8_t lit = 255, two_sided = 128, unlit = 64;

enum class Pattern : std::uint8_t { plain, stripes, spots, pale_belly };

// What a body's organs add up to. Lengths are in cells (1.5 m).
struct Body {
    float length = 0.8f; // the trunk, hips to shoulders
    float girth = 0.2f;  // its radius at the thickest
    float neck = 0.15f, head = 0.13f, snout = 0.1f;
    float tail = 0.3f;
    int leg_pairs = 2;
    float leg_length = 0.4f, leg_girth = 0.045f;
    float sprawl = 0.1f; // 0: legs under the body; 1: out to the sides
    float tail_fin = 0, dorsal_fin = 0, side_fins = 0;
    float wing = 0;      // one wing's length
    float eye = 0.022f;
    Color primary{170, 130, 84, 255}, secondary{226, 208, 170, 255};
    Pattern pattern = Pattern::pale_belly;
    float mouth_hardness = 0, front_hardness = 0, back_hardness = 0; // effective at this growth, 0..1
    bool frill = false;
};

enum class Medium : std::uint8_t { land, water, air };

enum class Action { idle, call, feed, mate, strike, recoil };
struct Gesture {
    Action action = Action::idle;
    float amount = 0; // signed for a strike's draw-back and forward thrust
    float side = 0;   // side of the body toward the food, prey or away from the blow
    float forward = 1; // target direction along the spine
    float food_height = 0; // cells above ground on land, relative to the swimmer in water
};

struct Pose {
    float x = 0, y = 0;  // where it is, in cells
    float heading = 0;   // radians; 0 faces east
    float height = 0;    // in water or air: the display height of its middle. On land it stands on the ground.
    float phase = 0;     // how far through its stride, stroke or wingbeat, in turns; it only ever grows
    float pace = 1;      // 0 standing still to 1 at full stride
    Medium medium = Medium::land;
    float grain = 0;     // cells to a pixel of the picture it is drawn into: an eye is never smaller than one
    Gesture gesture;
};

// The display height of the ground at a point, for feet to find.
using Ground = std::function<float(float, float)>;

// Middle of the sole after the leg has reached for the ground, in cells.
struct Foot {
    float x, y, z;
};

enum class Detail { full, distant };

// Appends the body's triangles and, when requested, the world positions
// of the soles in a land pose, including any feet swinging forward.
void build(std::vector<Vertex>& out, const Body& body, const Pose& pose, const Ground& ground,
           std::vector<Foot>* feet = nullptr, Detail detail = Detail::full);

// How thick its legs are at the shin, as drawn at this grain.
inline float shin(const Body& body, float grain) { return body.leg_girth > grain * 0.55f ? body.leg_girth : grain * 0.55f; }

// How high its middle rides above the ground when it stands.
float ride_height(const Body& body);

struct PortraitFrame {
    static constexpr float sin_pitch = 0.5f, cos_pitch = 0.8660254f;
    float cx, cy, zoom, depth_scale;
};

Pose portrait_pose(const Body& body, Medium medium);
PortraitFrame frame_portrait(const Body& adult, int width, int height);

} // namespace gfx::creature
