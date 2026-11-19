#pragma once

#include <cstdint>
#include <vector>

#include "gl.hpp"

namespace gfx {

struct Color {
    std::uint8_t r, g, b, a;
};

struct Texture {
    gl::GLuint id = 0;
    int width = 0, height = 0;
};

// Creates a texture with nearest filtering, for crisp pixel art.
Texture make_texture(int width, int height, gl::GLenum format, const void* pixels);
void update_texture(const Texture& texture, gl::GLenum format, const void* pixels);

// Batches textured, tinted quads into as few draw calls as possible: one per
// change of texture. Coordinates are logical screen pixels, origin top left.
// OpenGL ES 2 has no instancing, so the vertex buffer is rebuilt each frame.
class Batch {
public:
    bool init();
    void begin(int screen_width, int screen_height);
    void quad(const Texture& texture, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              Color color);
    void end();

    int draw_calls() const { return draw_calls_; }
    int quads() const { return quads_; }

private:
    struct Vertex {
        float x, y, u, v;
        Color color;
    };
    void flush();

    gl::GLuint program_ = 0, buffer_ = 0;
    gl::GLint screen_uniform_ = -1;
    gl::GLuint texture_ = 0;
    std::vector<Vertex> vertices_;
    int draw_calls_ = 0, quads_ = 0;
};

} // namespace gfx
