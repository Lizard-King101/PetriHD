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
    bool tree_xray = false;   // viewer-only canopy cutaway
    bool top_down = false;     // lock the pitch at straight down
    float scale = 1;           // screen pixels per pixel of what is drawn: the pixel-art grid

    static constexpr float flat_limit = 32; // the last zoom that is always top-down
    static constexpr float map_zoom = 24;   // baked map only at this zoom and below

    float live_opacity() const {
        const float t = std::clamp((zoom - map_zoom) / (flat_limit - map_zoom), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }

    // Pitch below the horizontal, in degrees: 90 to 32 px/cell, then 55 at
    // 64, 52 at 128 and 45 at 256, interpolated on log2(zoom).
    float pitch() const {
        if (top_down || zoom <= flat_limit) {
            return 90;
        }
        const float level = std::log2(zoom / flat_limit); // 0 at 32, 1 at 64, 2 at 128, 3 at 256
        const float keys[] = {90, 55, 52, 45}; // low enough at habitat zoom to show trunks and reeds in profile
        const int i = std::min(2, int(level));
        const float t = std::min(1.0f, level - float(i));
        return keys[i] + (keys[i + 1] - keys[i]) * t;
    }
    float sin_pitch() const { return std::sin(pitch() * 3.14159265f / 180); }
    float cos_pitch() const { return std::cos(pitch() * 3.14159265f / 180); }
    bool tilted() const { return zoom > flat_limit; }
    // Pixels of the drawing per cell. Pitch always follows the screen zoom.
    float px() const { return zoom / scale; }

    // A copy for drawing into a target `scale` times smaller, with the focus
    // moved by under a pixel so the ground lands on whole pixels: panning then
    // slides the picture instead of resampling it, and texels don't crawl.
    Camera for_target(float target_scale) const {
        Camera c = *this;
        c.scale = target_scale;
        const float z = c.px(), s = sin_pitch(), k = cos_pitch();
        c.cx = std::round(cx * z) / z;
        c.cy = (std::round((cy * s - focus_z * k) * z) / z + focus_z * k) / s;
        return c;
    }

    // The flat view's top-left cell, for a view w x h pixels.
    float left(float w) const { return cx - w / (2 * px()); }
    float top(float h) const { return cy - h / (2 * px()); }

    // World (cells, display height in cells) to screen pixels.
    void to_screen(float x, float y, float z, float w, float h, float& sx, float& sy) const {
        sx = w / 2 + (x - cx) * px();
        sy = h / 2 + ((y - cy) * sin_pitch() - (z - focus_z) * cos_pitch()) * px();
    }

    // Screen pixels to the ground, taken as level with the focus.
    void to_ground(float sx, float sy, float w, float h, float& x, float& y) const {
        x = cx + (sx - w / 2) / px();
        y = cy + (sy - h / 2) / (px() * sin_pitch());
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
