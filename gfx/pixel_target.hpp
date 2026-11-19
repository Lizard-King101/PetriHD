#pragma once

#include "batch.hpp"
#include "gl.hpp"

namespace gfx {

// The world is drawn into this low-resolution image, then scaled up by a
// whole number with no smoothing, so ground, plants and creatures all share
// one pixel grid. ES 2 framebuffer objects, with a depth buffer: 24-bit
// where the GPU has GL_OES_depth24 or desktop GL, else 16-bit.
class PixelTarget {
public:
    bool init(int width, int height);
    void begin(); // bind, set the viewport, clear colour and depth
    void end(int drawable_w, int drawable_h);

    int width() const { return texture_.width; }
    int height() const { return texture_.height; }
    int depth_bits() const { return depth_bits_; }
    const Texture& texture() const { return texture_; }

private:
    Texture texture_;
    gl::GLuint framebuffer_ = 0, depth_ = 0;
    int depth_bits_ = 0;
};

} // namespace gfx
