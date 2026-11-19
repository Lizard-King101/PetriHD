#include "tilt_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "terrain_art.hpp"

namespace gfx {
namespace {

struct V3 {
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1 / l) : V3{0, 0, 1};
}

const char* vertex_source = R"(
attribute vec3 a_position;
attribute vec2 a_uv;
attribute vec4 a_color;
uniform vec3 u_focus;
uniform vec2 u_trig;   // sin and cos of the pitch
uniform float u_zoom;
uniform vec4 u_screen; // screen width, height, and the view's centre
uniform float u_depth;
varying vec2 v_uv;
varying vec4 v_color;
uniform vec3 u_sun;
uniform float u_lit; // 1: a_color carries the ground's normal; 0: a lit colour
void main() {
    vec3 p = a_position - u_focus;
    float sx = u_screen.z + p.x * u_zoom;
    float sy = u_screen.w + (p.y * u_trig.x - p.z * u_trig.y) * u_zoom;
    float d = -(p.y * u_trig.y + p.z * u_trig.x) * u_depth;
    v_uv = a_uv;
    // The ground is lit from the slopes it really has, the same rule as the
    // flat bake uses, so near-flat ground matches the flat view.
    vec3 n = normalize(a_color.xyz * 2.0 - 1.0);
    float shade = clamp(1.0 + 1.1 * (dot(n, u_sun) - u_sun.z), 0.45, 1.35);
    v_color = mix(a_color, vec4(shade, shade, shade, 1.0), u_lit);
    gl_Position = vec4(sx / u_screen.x * 2.0 - 1.0, 1.0 - sy / u_screen.y * 2.0, d, 1.0);
}
)";

const char* fragment_source = R"(
uniform sampler2D u_texture;
uniform vec4 u_tint;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    gl_FragColor = texture2D(u_texture, v_uv) * v_color * u_tint;
}
)";

} // namespace

bool TiltView::init(const phd::World& world) {
    static const char* const attributes[] = {"a_position", "a_uv", "a_color"};
    program_ = gl::program(vertex_source, fragment_source, attributes, 3);
    if (!program_) {
        return false;
    }
    u_focus_ = gl::GetUniformLocation(program_, "u_focus");
    u_trig_ = gl::GetUniformLocation(program_, "u_trig");
    u_zoom_ = gl::GetUniformLocation(program_, "u_zoom");
    u_screen_ = gl::GetUniformLocation(program_, "u_screen");
    u_depth_ = gl::GetUniformLocation(program_, "u_depth");
    u_tint_ = gl::GetUniformLocation(program_, "u_tint");
    u_texture_ = gl::GetUniformLocation(program_, "u_texture");
    u_sun_ = gl::GetUniformLocation(program_, "u_sun");
    u_lit_ = gl::GetUniformLocation(program_, "u_lit");
    gl::GenBuffers(1, &scene_vbo_);

    width_ = world.width();
    height_ = world.height();
    corners_.assign(std::size_t(width_ + 1) * (height_ + 1), 0);
    build_heights(world);
    for (int y = 0; y < height_; y += chunk_cells) {
        for (int x = 0; x < width_; x += chunk_cells) {
            Chunk chunk{x, y, std::min(chunk_cells, width_ - x), std::min(chunk_cells, height_ - y), 0, 0};
            gl::GenBuffers(1, &chunk.vbo);
            gl::GenBuffers(1, &chunk.ibo);
            build_chunk(chunk);
            chunks_.push_back(chunk);
        }
    }
    return true;
}

void TiltView::build_heights(const phd::World& world) {
    const auto surface = [&](int x, int y) {
        x = std::clamp(x, 0, width_ - 1);
        y = std::clamp(y, 0, height_ - 1);
        const auto i = std::size_t(y) * width_ + x;
        switch (world.water(i)) {
        case phd::Water::ocean: return 0.0f;
        case phd::Water::none: return float(world.elevation(i)) / 10;
        default: return float(world.elevation(i) + world.water_depth_dm(i)) / 10;
        }
    };
    for (int j = 0; j <= height_; ++j) {
        for (int i = 0; i <= width_; ++i) {
            const float metres = (surface(i - 1, j - 1) + surface(i, j - 1) + surface(i - 1, j) + surface(i, j)) / 4;
            corners_[std::size_t(j) * (width_ + 1) + i] = metres * relief_;
        }
    }
}

void TiltView::build_chunk(Chunk& chunk) {
    scratch_.clear();
    index_scratch_.clear();
    chunk.zmin = 1e9f;
    chunk.zmax = -1e9f;
    const auto corner = [&](int x, int y) {
        x = std::clamp(x, 0, width_);
        y = std::clamp(y, 0, height_);
        return corners_[std::size_t(y) * (width_ + 1) + x];
    };
    for (int j = 0; j <= chunk.h; ++j) {
        for (int i = 0; i <= chunk.w; ++i) {
            const int x = chunk.x0 + i, y = chunk.y0 + j;
            const float z = corner(x, y);
            chunk.zmin = std::min(chunk.zmin, z);
            chunk.zmax = std::max(chunk.zmax, z);
            // The normal, packed into the colour: the shader lights with it.
            // In metres per cell over the bake's slope scale, so lighting
            // doesn't depend on the display relief and matches the flat view.
            const float scale = 1.0f / (relief_ * art::shade_metres);
            const float gx = (corner(x + 1, y) - corner(x - 1, y)) / 2 * scale;
            const float gy = (corner(x, y + 1) - corner(x, y - 1)) / 2 * scale;
            const float len = std::sqrt(gx * gx + gy * gy + 1);
            const auto pack = [](float v) { return std::uint8_t(std::clamp((v * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f)); };
            const Color normal{pack(-gx / len), pack(-gy / len), pack(1 / len), 255};
            scratch_.push_back({float(x), float(y), z, float(x) / float(width_), float(y) / float(height_), normal});
        }
    }
    const int row = chunk.w + 1;
    for (int j = 0; j < chunk.h; ++j) {
        for (int i = 0; i < chunk.w; ++i) {
            const auto a = std::uint16_t(j * row + i), b = std::uint16_t(a + 1), c = std::uint16_t(a + row),
                       d = std::uint16_t(c + 1);
            index_scratch_.insert(index_scratch_.end(), {a, b, d, a, d, c});
        }
    }
    gl::BindBuffer(gl::ARRAY_BUFFER, chunk.vbo);
    gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(scratch_.size() * sizeof(Vertex)), scratch_.data(), gl::DYNAMIC_DRAW);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, chunk.ibo);
    gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, gl::GLsizeiptr(index_scratch_.size() * 2), index_scratch_.data(),
                   gl::STATIC_DRAW);
    chunk.indices = int(index_scratch_.size());
}

void TiltView::refresh(const phd::World& world, int count) {
    // Water levels change slowly, so the heights are rebuilt in turn.
    build_heights(world);
    for (int k = 0; k < count && !chunks_.empty(); ++k) {
        build_chunk(chunks_[std::size_t(next_++ % int(chunks_.size()))]);
    }
}

float TiltView::ground(float x, float y) const {
    x = std::clamp(x, 0.0f, float(width_) - 0.001f);
    y = std::clamp(y, 0.0f, float(height_) - 0.001f);
    const int i = int(x), j = int(y);
    const float fx = x - float(i), fy = y - float(j);
    const auto at = [&](int a, int b) { return corners_[std::size_t(b) * (width_ + 1) + a]; };
    // The same split as the mesh's triangles (a-b-d, a-d-c), so objects
    // stand exactly on the surface that is drawn.
    if (fx >= fy) {
        return at(i, j) + (at(i + 1, j) - at(i, j)) * fx + (at(i + 1, j + 1) - at(i + 1, j)) * fy;
    }
    return at(i, j) + (at(i, j + 1) - at(i, j)) * fy + (at(i + 1, j + 1) - at(i, j + 1)) * fx;
}

void TiltView::place_test_scene(const phd::World& world, float x, float y) {
    // The nearest dry land, so the tree and the creature have ground to stand on.
    const int cx = int(x), cy = int(y);
    for (int r = 0; r < 40; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) {
                    continue;
                }
                const int px = cx + dx, py = cy + dy;
                if (px < 3 || py < 3 || px >= width_ - 3 || py >= height_ - 3 ||
                    world.water(std::size_t(py) * width_ + px) != phd::Water::none) {
                    continue;
                }
                scene_ = true;
                scene_x_ = float(px) + 0.5f;
                scene_y_ = float(py) + 0.5f;
                return;
            }
        }
    }
}

void TiltView::build_scene(const phd::World& world, const Camera& camera, const Font& font, double time, int view_w,
                           int view_h) {
    scene_vertices_.clear();
    trees_ = 0;
    float wu, wv;
    font.white(wu, wv);
    const auto sun_art = art::sun_for(world);
    const V3 sun{sun_art.x, sun_art.y, sun_art.z};
    const auto lit = [&](Color c, V3 normal) {
        const float s = 0.55f + 0.5f * std::max(0.0f, dot(normalize(normal), sun));
        return Color{std::uint8_t(std::min(255.0f, float(c.r) * s)), std::uint8_t(std::min(255.0f, float(c.g) * s)),
                     std::uint8_t(std::min(255.0f, float(c.b) * s)), c.a};
    };
    const auto tri = [&](V3 a, V3 b, V3 c, Color color) {
        scene_vertices_.push_back({a.x, a.y, a.z, wu, wv, color});
        scene_vertices_.push_back({b.x, b.y, b.z, wu, wv, color});
        scene_vertices_.push_back({c.x, c.y, c.z, wu, wv, color});
    };
    const auto quad = [&](V3 a, V3 b, V3 c, V3 d, Color color, V3 outward) {
        const Color shaded = lit(color, outward);
        tri(a, b, c, shaded);
        tri(a, c, d, shaded);
    };
    // A box from p0 to p1 with a square section `thick` across.
    const auto beam = [&](V3 p0, V3 p1, float thick, Color color) {
        const V3 d = p1 - p0;
        V3 side = cross(d, {0, 0, 1});
        if (dot(side, side) < 1e-8f) {
            side = {1, 0, 0};
        }
        side = normalize(side) * (thick / 2);
        const V3 up = normalize(cross(side, d)) * (thick / 2);
        const V3 c[8] = {p0 - side - up, p0 + side - up, p0 + side + up, p0 - side + up,
                         p1 - side - up, p1 + side - up, p1 + side + up, p1 - side + up};
        quad(c[0], c[1], c[2], c[3], color, d * -1.0f);
        quad(c[4], c[7], c[6], c[5], color, d);
        quad(c[0], c[4], c[5], c[1], color, up * -1.0f);
        quad(c[3], c[2], c[6], c[7], color, up);
        quad(c[0], c[3], c[7], c[4], color, side * -1.0f);
        quad(c[1], c[5], c[6], c[2], color, side);
    };
    // A frustum of `sides` faces around a vertical axis.
    const auto frustum = [&](float x, float y, float z0, float z1, float r0, float r1, int sides, Color color) {
        for (int k = 0; k < sides; ++k) {
            const float a0 = float(k) * 6.2831853f / float(sides), a1 = float(k + 1) * 6.2831853f / float(sides);
            const V3 b0{x + std::cos(a0) * r0, y + std::sin(a0) * r0, z0}, b1{x + std::cos(a1) * r0, y + std::sin(a1) * r0, z0};
            const V3 t0{x + std::cos(a0) * r1, y + std::sin(a0) * r1, z1}, t1{x + std::cos(a1) * r1, y + std::sin(a1) * r1, z1};
            const float mid = (a0 + a1) / 2;
            const V3 out{std::cos(mid) * (z1 - z0), std::sin(mid) * (z1 - z0), r0 - r1};
            quad(b0, b1, t1, t0, color, out);
            if (r1 > 0) {
                tri({x, y, z1}, t0, t1, lit(color, {0, 0, 1}));
            }
            tri({x, y, z0}, b1, b0, lit(color, {0, 0, -1}));
        }
    };
    const auto shadow = [&](float x, float y, float radius) {
        const Color dark{10, 14, 10, 90};
        for (int k = 0; k < 10; ++k) {
            const float a0 = float(k) * 0.6283185f, a1 = float(k + 1) * 0.6283185f;
            const float sx0 = x + std::cos(a0) * radius, sy0 = y + std::sin(a0) * radius;
            const float sx1 = x + std::cos(a1) * radius, sy1 = y + std::sin(a1) * radius;
            tri({x, y, ground(x, y) + 0.05f}, {sx0, sy0, ground(sx0, sy0) + 0.05f}, {sx1, sy1, ground(sx1, sy1) + 0.05f}, dark);
        }
    };

    // The trees the flat view draws as crowns, standing up: the same spots,
    // sizes and colours (art::tree_in), so the switch at 32 px per cell keeps
    // every tree where it was. Those whose tops can reach the view: tall ones
    // just north of it show above its top edge.
    {
        const float half_w = float(view_w) / (2 * camera.px()) + 2;
        const float reach = float(view_h) / (2 * camera.px() * camera.sin_pitch());
        const int x0 = std::max(0, int(camera.cx - half_w)), x1 = std::min(width_ - 1, int(camera.cx + half_w));
        const int y0 = std::max(0, int(camera.cy - reach - 8)), y1 = std::min(height_ - 1, int(camera.cy + reach + 2));
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                const auto t = art::tree_in(world, cx, cy);
                if (t.radius <= 0) {
                    continue;
                }
                ++trees_;
                const float g = ground(t.x, t.y), r = t.radius, h = 1.2f + r * 2.6f;
                const Color leaves{t.r, t.g, t.b, 255}, bark{96, 72, 50, 255};
                shadow(t.x - sun.x * r * 0.9f, t.y - sun.y * r * 0.9f, r * 0.95f);
                frustum(t.x, t.y, g - 0.05f, g + h * 0.45f, 0.08f + 0.06f * r, 0.05f + 0.03f * r, 5, bark);
                if (t.conifer) {
                    frustum(t.x, t.y, g + h * 0.2f, g + h * 0.62f, r, r * 0.45f, 7, leaves);
                    frustum(t.x, t.y, g + h * 0.5f, g + h, r * 0.7f, 0.0f, 7, leaves);
                } else {
                    frustum(t.x, t.y, g + h * 0.35f, g + h * 0.6f, r * 0.55f, r, 7, leaves);
                    frustum(t.x, t.y, g + h * 0.6f, g + h * 0.85f, r, r * 0.8f, 7, leaves);
                    frustum(t.x, t.y, g + h * 0.85f, g + h, r * 0.8f, r * 0.25f, 7, leaves);
                }
            }
        }
    }
    if (!scene_) {
        return;
    }

    // The test plant: a tree about two cells tall.
    const float tx = scene_x_, ty = scene_y_, tz = ground(tx, ty);
    shadow(tx - sun.x * 0.6f, ty - sun.y * 0.6f, 0.7f);
    frustum(tx, ty, tz - 0.05f, tz + 1.0f, 0.10f, 0.06f, 6, {112, 80, 52, 255});
    frustum(tx, ty, tz + 0.7f, tz + 1.3f, 0.45f, 0.75f, 7, {58, 118, 54, 255});
    frustum(tx, ty, tz + 1.3f, tz + 2.3f, 0.75f, 0.0f, 7, {66, 132, 58, 255});

    // The test creature: three segments and six legs, walking a circle
    // beside the tree, each foot on the ground wherever it steps.
    const float orbit = 0.7f, angle = float(time) * 0.6f;
    const float ox = tx + 1.4f, oy = ty + 0.3f;
    const V3 at{ox + std::cos(angle) * orbit, oy + std::sin(angle) * orbit, 0};
    const V3 forward{-std::sin(angle), std::cos(angle), 0}, right{std::cos(angle), std::sin(angle), 0};
    const float base = ground(at.x, at.y);
    const float body_z = base + 0.24f;
    shadow(at.x, at.y, 0.32f);
    const Color shell{176, 92, 58, 255}, head{120, 60, 40, 255}, leg{70, 44, 32, 255};
    const auto segment = [&](float along, float length, float thick, Color color) {
        const V3 centre = at + forward * along;
        const float z = body_z + 0.02f * std::sin(float(time) * 8);
        beam({centre.x - forward.x * length / 2, centre.y - forward.y * length / 2, z},
             {centre.x + forward.x * length / 2, centre.y + forward.y * length / 2, z}, thick, color);
    };
    segment(-0.26f, 0.34f, 0.24f, shell);
    segment(0.02f, 0.2f, 0.18f, shell);
    segment(0.2f, 0.14f, 0.13f, head);
    for (int pair = 0; pair < 3; ++pair) {
        for (int side = -1; side <= 1; side += 2) {
            // Alternating tripods: legs 0 and 2 on one side with 1 on the other.
            const float phase = float(time) * 8 + float((pair + (side > 0)) % 2) * 3.14159f;
            const float along = -0.12f + 0.13f * float(pair);
            const V3 hip = at + forward * along + right * (0.09f * float(side));
            const V3 foot_xy = at + forward * (along + 0.07f * std::sin(phase)) + right * (0.34f * float(side));
            const float lift = std::max(0.0f, std::cos(phase)) * 0.05f;
            const V3 knee_xy = at + forward * along + right * (0.26f * float(side));
            const V3 hip3{hip.x, hip.y, body_z};
            const V3 knee3{knee_xy.x, knee_xy.y, body_z + 0.08f + lift};
            const V3 foot3{foot_xy.x, foot_xy.y, ground(foot_xy.x, foot_xy.y) + lift};
            beam(hip3, knee3, 0.035f, leg);
            beam(knee3, foot3, 0.03f, leg);
        }
    }
}

void TiltView::draw(const phd::World& world, const Camera& camera, const Texture& terrain, const Font& font,
                    int screen_w, int screen_h, int view_h, double time) {
    draw_calls_ = 0;
    triangles_ = 0;
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::DepthFunc(gl::LEQUAL);
    gl::DepthMask(1);
    gl::UseProgram(program_);
    gl::Uniform3f(u_focus_, camera.cx, camera.cy, camera.focus_z);
    gl::Uniform2f(u_trig_, camera.sin_pitch(), camera.cos_pitch());
    gl::Uniform1f(u_zoom_, camera.px());
    gl::Uniform4f(u_screen_, float(screen_w), float(screen_h), float(screen_w) / 2, float(view_h) / 2);
    // Fit the depth range to what can be on screen, so the depth buffer's
    // steps stay small: the view's reach north and south, and the tallest
    // relief above or below the focus.
    const float reach = float(view_h) / (2 * camera.px() * camera.sin_pitch()) + 40;
    const float relief = 3000 * relief_ + 4;
    gl::Uniform1f(u_depth_, 1.0f / (reach * camera.cos_pitch() + relief * camera.sin_pitch() + 2));
    gl::Uniform1i(u_texture_, 0);
    std::uint8_t tint[3];
    art::day_tint(world, tint);
    gl::Uniform4f(u_tint_, float(tint[0]) / 255, float(tint[1]) / 255, float(tint[2]) / 255, 1);

    const auto stride = gl::GLsizei(sizeof(Vertex));
    const auto attributes = [&] {
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::EnableVertexAttribArray(2);
        gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, x)));
        gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, u)));
        gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride, reinterpret_cast<const void*>(offsetof(Vertex, color)));
    };

    const auto sun = art::sun_for(world);
    gl::Uniform3f(u_sun_, sun.x, sun.y, sun.z);
    gl::Uniform1f(u_lit_, 1);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, terrain.id);
    for (const auto& chunk : chunks_) {
        // Cull chunks whose box can't reach the view.
        float sx0, sy0, sx1, sy1;
        camera.to_screen(float(chunk.x0), float(chunk.y0), chunk.zmax, float(screen_w), float(view_h), sx0, sy0);
        camera.to_screen(float(chunk.x0 + chunk.w), float(chunk.y0 + chunk.h), chunk.zmin, float(screen_w),
                         float(view_h), sx1, sy1);
        if (sx1 < 0 || sx0 > float(screen_w) || sy1 < 0 || sy0 > float(view_h)) {
            continue;
        }
        gl::BindBuffer(gl::ARRAY_BUFFER, chunk.vbo);
        gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, chunk.ibo);
        attributes();
        gl::DrawElements(gl::TRIANGLES, chunk.indices, gl::UNSIGNED_SHORT, nullptr);
        ++draw_calls_;
        triangles_ += chunk.indices / 3;
    }

    build_scene(world, camera, font, time, screen_w, view_h);
    if (!scene_vertices_.empty()) {
        gl::Uniform1f(u_lit_, 0);
        gl::BindTexture(gl::TEXTURE_2D, font.texture().id);
        gl::BindBuffer(gl::ARRAY_BUFFER, scene_vbo_);
        gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(scene_vertices_.size() * sizeof(Vertex)),
                       scene_vertices_.data(), gl::STREAM_DRAW);
        attributes();
        gl::DrawArrays(gl::TRIANGLES, 0, gl::GLsizei(scene_vertices_.size()));
        ++draw_calls_;
        triangles_ += int(scene_vertices_.size() / 3);
    }
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, 0);
    gl::Disable(gl::DEPTH_TEST);
}

} // namespace gfx
