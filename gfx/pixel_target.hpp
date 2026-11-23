#pragma once

#include <cmath>

#include "batch.hpp"
#include "gl.hpp"

namespace gfx {

// One axis of a body-local texel grid placed on the world's finer target.
struct TexelGridAxis {
    float origin = 0;  // physical origin on screen, in creature texels
    float display = 0; // presented origin, in whole target pixels
    float shift = 0;   // physical-to-presented shift, in creature texels

    static TexelGridAxis place(double world_px, double camera_px, int extent, float texel_size) {
        // Camera::for_target gives the terrain an integral translation.
        // Recover it before rounding the sprite so panning cannot flip a tie.
        const double pivot = extent * 0.5 + world_px - std::round(camera_px);
        const double origin_px = pivot - 0.5 * texel_size; // the pivot samples the centre of texel zero
        const double display_px = std::floor(origin_px + 0.5);
        return {float(origin_px / texel_size), float(display_px), float((display_px - origin_px) / texel_size)};
    }
};

// The world is drawn into this low-resolution image, then scaled up by a
// whole number with no smoothing, so ground, plants and creatures all share
// one pixel grid. ES 2 framebuffer objects, with a depth buffer: 24-bit
// where the GPU has GL_OES_depth24 or desktop GL, else 16-bit. The image is
// RGBA where the GPU can draw to one, else RGB; its alpha is scratch space
// while drawing and is set back to opaque at the end.
class PixelTarget {
public:
    // Reuses the GL objects if the target needs a larger backing image.
    bool init(int width, int height, int min_alpha_bits = 0);
    void begin(); // bind, set the viewport, clear colour and depth
    void end(int drawable_w, int drawable_h);

    int width() const { return texture_.width; }
    int height() const { return texture_.height; }
    int depth_bits() const { return depth_bits_; }
    // Whether the image keeps an alpha channel, which shadows use as scratch.
    bool has_alpha() const { return alpha_bits_ > 0; }
    int alpha_bits() const { return alpha_bits_; }
    const Texture& texture() const { return texture_; }

private:
    Texture texture_;
    gl::GLuint framebuffer_ = 0, depth_ = 0;
    int depth_bits_ = 0;
    int alpha_bits_ = 0;
};

} // namespace gfx
