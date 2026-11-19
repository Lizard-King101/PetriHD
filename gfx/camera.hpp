#pragma once

#include <algorithm>
#include <cmath>

namespace gfx {

// An orthographic camera over the world. Up to 32 px per cell it looks
// straight down, exactly as the flat renderer always has. Beyond that it
// tilts smoothly toward a three-quarter view, still orthographic, looking
// north. Camera state is display only: nothing here touches the world.
struct Camera {
    float cx = 0, cy = 0;      // the focus, in cells, on the ground
    float focus_z = 0;         // display height of the ground at the focus, in cells
    float zoom = 2;            // pixels per cell, as shown
    float target_zoom = 2;     // the power-of-two zoom it is moving to
    bool top_down = false;     // lock the pitch at straight down

    static constexpr float flat_limit = 32; // the last zoom that is always top-down

    // Pitch below the horizontal, in degrees: 90 to 32 px/cell, then 72 at
    // 64, 56 at 128 and 45 at 256, interpolated on log2(zoom).
    float pitch() const {
        if (top_down || zoom <= flat_limit) {
            return 90;
        }
        const float level = std::log2(zoom / flat_limit); // 0 at 32, 1 at 64, 2 at 128, 3 at 256
        const float keys[] = {90, 72, 56, 45};
        const int i = std::min(2, int(level));
        const float t = std::min(1.0f, level - float(i));
        return keys[i] + (keys[i + 1] - keys[i]) * t;
    }
    float sin_pitch() const { return std::sin(pitch() * 3.14159265f / 180); }
    float cos_pitch() const { return std::cos(pitch() * 3.14159265f / 180); }
    bool tilted() const { return zoom > flat_limit; }

    // The flat view's top-left cell, for a view w x h pixels.
    float left(float w) const { return cx - w / (2 * zoom); }
    float top(float h) const { return cy - h / (2 * zoom); }

    // World (cells, display height in cells) to screen pixels.
    void to_screen(float x, float y, float z, float w, float h, float& sx, float& sy) const {
        sx = w / 2 + (x - cx) * zoom;
        sy = h / 2 + ((y - cy) * sin_pitch() - (z - focus_z) * cos_pitch()) * zoom;
    }

    // Screen pixels to the ground, taken as level with the focus.
    void to_ground(float sx, float sy, float w, float h, float& x, float& y) const {
        x = cx + (sx - w / 2) / zoom;
        y = cy + (sy - h / 2) / (zoom * sin_pitch());
    }

    // Sets the zoom and keeps the ground point under (sx, sy) where it is.
    void zoom_about(float new_zoom, float sx, float sy, float w, float h) {
        float gx, gy;
        to_ground(sx, sy, w, h, gx, gy);
        zoom = new_zoom;
        cx = gx - (sx - w / 2) / zoom;
        cy = gy - (sy - h / 2) / (zoom * sin_pitch());
    }

    // Keeps the view over the world. The flat view clamps its edges as
    // before; tilted, only the focus is kept on the map.
    void clamp(float w, float h, int world_w, int world_h) {
        if (tilted()) {
            cx = std::clamp(cx, 0.0f, float(world_w));
            cy = std::clamp(cy, 0.0f, float(world_h));
            return;
        }
        const auto axis = [](float& centre, float visible, int size) {
            centre = visible >= float(size) ? float(size) / 2
                                            : std::clamp(centre, visible / 2, float(size) - visible / 2);
        };
        axis(cx, w / zoom, world_w);
        axis(cy, h / zoom, world_h);
    }
};

} // namespace gfx
