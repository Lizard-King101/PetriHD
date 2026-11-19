#include "batch.hpp"

#include <cstddef>

namespace gfx {

Texture make_texture(int width, int height, gl::GLenum format, const void* pixels) {
    Texture texture{0, width, height};
    gl::GenTextures(1, &texture.id);
    gl::BindTexture(gl::TEXTURE_2D, texture.id);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(format), width, height, 0, format, gl::UNSIGNED_BYTE, pixels);
    return texture;
}

void update_texture(const Texture& texture, gl::GLenum format, const void* pixels) {
    gl::BindTexture(gl::TEXTURE_2D, texture.id);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, texture.width, texture.height, format, gl::UNSIGNED_BYTE, pixels);
}

bool Batch::init() {
    static const char* vertex = R"(
attribute vec2 a_position;
attribute vec2 a_uv;
attribute vec4 a_color;
uniform vec2 u_screen;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(a_position.x / u_screen.x * 2.0 - 1.0, 1.0 - a_position.y / u_screen.y * 2.0, 0.0, 1.0);
}
)";
    static const char* fragment = R"(
uniform sampler2D u_texture;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    gl_FragColor = texture2D(u_texture, v_uv) * v_color;
}
)";
    static const char* const attributes[] = {"a_position", "a_uv", "a_color"};
    program_ = gl::program(vertex, fragment, attributes, 3);
    if (!program_) {
        return false;
    }
    screen_uniform_ = gl::GetUniformLocation(program_, "u_screen");
    gl::GenBuffers(1, &buffer_);
    vertices_.reserve(6 * 4096);
    return true;
}

void Batch::begin(int screen_width, int screen_height) {
    draw_calls_ = 0;
    quads_ = 0;
    texture_ = 0;
    vertices_.clear();
    gl::UseProgram(program_);
    gl::Uniform2f(screen_uniform_, float(screen_width), float(screen_height));
    gl::Uniform1i(gl::GetUniformLocation(program_, "u_texture"), 0);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
}

void Batch::quad(const Texture& texture, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                 float v1, Color color) {
    if (texture.id != texture_) {
        flush();
        texture_ = texture.id;
    }
    const Vertex a{x0, y0, u0, v0, color}, b{x1, y0, u1, v0, color}, c{x1, y1, u1, v1, color},
        d{x0, y1, u0, v1, color};
    vertices_.insert(vertices_.end(), {a, b, c, a, c, d});
    ++quads_;
}

void Batch::flush() {
    if (vertices_.empty()) {
        return;
    }
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::BindBuffer(gl::ARRAY_BUFFER, buffer_);
    gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data(),
                   gl::STREAM_DRAW);
    const auto stride = gl::GLsizei(sizeof(Vertex));
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(0, 2, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, x)));
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, u)));
    gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride, reinterpret_cast<const void*>(offsetof(Vertex, color)));
    gl::DrawArrays(gl::TRIANGLES, 0, gl::GLsizei(vertices_.size()));
    ++draw_calls_;
    vertices_.clear();
}

void Batch::end() { flush(); }

} // namespace gfx
