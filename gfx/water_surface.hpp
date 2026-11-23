#pragma once

#include <algorithm>
#include <vector>

#include "world.hpp"

namespace art::water {

constexpr float dry = -1e9f;
// Ignore roundoff when the bed exactly meets the surface, in metres.
constexpr float shore_epsilon = 0.00001f;

struct CellCenter {
    // Heights at the simulation cell's centre, in metres. The bed is the
    // cell's own; the level is its water, or the surface its wet
    // neighbours carry across a dry cell, or dry where no surface reaches.
    float bed = dry, level = dry;
};

struct Corner {
    // Metres. The bed is the ground and depends on the terrain alone: water
    // comes and goes over it without moving it.
    // `painted` is for the single-mesh view.
    float bed = 0, level = dry, painted = 0;
    float painted_bed = 0; // mean terrain under the single-mesh view's painted surface
    float flow_x = 0, flow_y = 0;
    bool ice = false;
    phd::Water kind = phd::Water::none; // water touching this corner before extending the surface under the bank
    bool divide = false; // the corner touches lakes at different elevations
    bool channel = false; // a stream's bed is cut through this corner
    CellCenter center;   // the interior vertex of the cell starting here
};

// Both renderers split a cell along the top-left to bottom-right diagonal.
inline float interpolate(float a, float b, float c, float d, float fx, float fy, float center = dry) {
    const float base = fx >= fy ? a + (b - a) * fx + (d - b) * fy : a + (c - a) * fy + (d - c) * fx;
    if (center == dry) return base;
    // Subdividing the original diagonal at its midpoint leaves all four
    // edges unchanged. This is the barycentric weight of its new vertex.
    return base + (center - (a + d) * 0.5f) * (2 * std::min({fx, fy, 1 - fx, 1 - fy}));
}

// A rectangular window of integer terrain corners. Neighbouring windows
// gather the same bank and river-mouth samples at their common edge.
class Field {
public:
    Field(const phd::World& world, int x0, int y0, int width, int height);
    const Corner& corner(int x, int y) const { return corners_[std::size_t(y - y0_) * stride_ + (x - x0_)]; }
    float plane(int x, int y) const { return planes_.empty() ? dry : planes_[std::size_t(y - y0_) * stride_ + (x - x0_)]; }
    float depth(float x, float y) const; // signed metres at a point inside the window, or dry

private:
    void resolve_divides(const phd::World& world, int x0, int y0, int width, int height);
    int x0_, y0_, stride_;
    std::vector<Corner> corners_;
    std::vector<float> planes_; // cell-level overrides at a divide between standing waters
};

} // namespace art::water
