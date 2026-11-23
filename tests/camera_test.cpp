// The camera's maths, without a window: zooming keeps the ground under the
// pointer where it is, and dragging over hills keeps the grabbed ground under
// the finger, whatever the pitch.
#include <cmath>
#include <cstdio>

#include "camera.hpp"
#include "pixel_target.hpp"

namespace {

int failures = 0;

#define CHECK_NEAR(a, b, tolerance)                                                                   \
    do {                                                                                              \
        if (std::fabs(double(a) - double(b)) > (tolerance)) {                                         \
            std::printf("%s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, double(a), double(b)); \
            ++failures;                                                                               \
        }                                                                                             \
    } while (0)

// A hilly ground in display cells.
float ground(float x, float y) { return 6 * std::sin(x * 0.21f) + 9 * std::cos(y * 0.17f); }

// What the app does each frame: follow the ground under the focus, sliding
// the pivot along the line of sight so the picture stays put.
void follow(gfx::Camera& c) {
    for (int frame = 0; frame < 60; ++frame) {
        const float rise = (ground(c.cx, c.cy) - c.focus_z) * 0.2f;
        c.focus_z += rise;
        if (c.sin_pitch() > 0.01f) {
            c.cy += rise * c.cos_pitch() / c.sin_pitch();
        }
    }
}

} // namespace

int main() {
    const float w = 800, h = 448;
    // Both maps share an overhead projection throughout the crossfade.
    gfx::Camera transition;
    const float stops[][2] = {{16, 0}, {24, 0}, {26, 0.15625f}, {28, 0.5f}, {30, 0.84375f}, {32, 1}};
    for (const auto& [zoom, opacity] : stops) {
        transition.zoom = zoom;
        CHECK_NEAR(transition.pitch(), 90, 0);
        CHECK_NEAR(transition.live_opacity(), opacity, 1e-6);
    }
    transition.zoom = 64;
    transition.top_down = true;
    CHECK_NEAR(transition.live_opacity(), 1, 0);
    CHECK_NEAR(transition.pitch(), 90, 0);
    // Panning by one world-target pixel carries both the sprite and the
    // shadow grid exactly that far, including fractional zoom and half ties.
    for (const float zoom : {40.0f, 147.0f, 160.0f, 256.0f}) {
        const float px = zoom / 2, texel = std::max(1.0f, px / 32.0f);
        const double world_px = double(384.1f) * px;
        for (const int width : {287, 288}) {
            const auto start = gfx::TexelGridAxis::place(world_px, 380.0 * px, width, texel);
            for (int step = -16; step <= 16; ++step) {
                const float camera_x = 380.0f + float(step) / px;
                const auto grid = gfx::TexelGridAxis::place(world_px, double(camera_x) * px, width, texel);
                CHECK_NEAR(grid.display, start.display - step, 0);
                CHECK_NEAR(grid.shift, start.shift, 1e-6);
                CHECK_NEAR((grid.origin + grid.shift) * texel, grid.display, 1e-4);
            }
        }
    }
    for (const float zoom : {2.0f, 32.0f, 64.0f, 128.0f, 256.0f}) {
        gfx::Camera c;
        c.cx = 150;
        c.cy = 90;
        c.zoom = zoom;
        c.focus_z = ground(c.cx, c.cy);

        // Zooming about a point keeps the ground there under it (at the
        // focus's height).
        float gx, gy;
        c.to_ground(600, 100, w, h, gx, gy);
        c.zoom_about(zoom * 1.41f, 600, 100, w, h);
        float sx, sy;
        c.to_screen(gx, gy, c.focus_z, w, h, sx, sy);
        CHECK_NEAR(sx, 600, 0.01);
        CHECK_NEAR(sy, 100, 0.01);

        // Following the ground's height never moves the picture.
        c.zoom = zoom;
        c.focus_z += 4; // the pivot is off the ground, as after a pan onto a hill
        float before_x, before_y;
        c.to_screen(160, 80, ground(160, 80), w, h, before_x, before_y);
        follow(c);
        float after_x, after_y;
        c.to_screen(160, 80, ground(160, 80), w, h, after_x, after_y);
        CHECK_NEAR(after_x, before_x, 0.01);
        CHECK_NEAR(after_y, before_y, 0.05);

        // A vertical drag moves the picture by exactly as far as the finger,
        // up or down, over the hills.
        for (const float drag : {120.0f, -120.0f}) {
            float px, py;
            c.to_screen(c.cx, c.cy - 1, c.focus_z, w, h, px, py);
            c.cy -= drag / (c.zoom * c.sin_pitch());
            float qx, qy;
            c.to_screen(c.cx + 0, c.cy - 1 + drag / (c.zoom * c.sin_pitch()), c.focus_z, w, h, qx, qy);
            CHECK_NEAR(qy - py, drag, 0.05);
            follow(c);
        }
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all camera tests passed\n");
    return 0;
}
