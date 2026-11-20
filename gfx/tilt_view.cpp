#include "tilt_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "math.hpp"
#include "shoreline_art.hpp"
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
#ifdef SHORELINE_STUDY
uniform vec3 u_bank_region; // origin in cells, width in cells
varying vec2 v_bank;
#endif
varying vec4 v_color;
uniform vec3 u_sun;
uniform float u_lit; // 1: a_color carries the ground's normal; 0: a lit colour
void main() {
    vec3 p = a_position - u_focus;
    float sx = u_screen.z + p.x * u_zoom;
    float sy = u_screen.w + (p.y * u_trig.x - p.z * u_trig.y) * u_zoom;
    float d = -(p.y * u_trig.y + p.z * u_trig.x) * u_depth;
    v_uv = a_uv;
#ifdef SHORELINE_STUDY
    v_bank = (a_position.xy - u_bank_region.xy) / u_bank_region.z;
#endif
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
#ifdef SHORELINE_STUDY
uniform sampler2D u_bank;
uniform float u_lit;
varying vec2 v_bank;
#endif
uniform vec4 u_tint;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    vec4 surface = texture2D(u_texture, v_uv);
#ifdef SHORELINE_STUDY
    if (u_lit > 0.5 && all(greaterThanEqual(v_bank, vec2(0.0))) && all(lessThanEqual(v_bank, vec2(1.0)))) {
        surface = texture2D(u_bank, v_bank);
    }
#endif
    gl_FragColor = surface * v_color * u_tint;
}
)";


// Plant sprites: each quad hangs from the spot where its plant stands. The
// spot is projected and snapped to a whole pixel, then the corner is offset
// in texels, so a sprite's texels land one to a pixel at 64 px per cell and
// never crawl. The whole sprite takes its spot's depth, so plants sort by
// where they stand, and a crown by where it hangs: looking down, the taller
// tree is on top. The top corners sway with the wind. A trunk's top corners
// carry its true length: it is shown as long as the tilt makes it, cut from
// the foot of its sprite, so its texels are never squashed.
const char* sprite_vertex_source = R"(
attribute vec3 a_position;
attribute vec4 a_uv;    // the corner's offset in texels, then its texture coordinates
attribute vec4 a_color; // r: sway, in sixteenths of a texel; g: the plant's beat; b: a trunk's top
uniform vec3 u_focus;
uniform vec2 u_trig;
uniform float u_zoom;
uniform vec4 u_screen;
uniform float u_depth;
uniform vec2 u_wind; // strength across the screen, and the time
uniform float u_texel; // one row of the atlas
varying vec2 v_uv;
void main() {
    vec3 p = a_position - u_focus;
    float texel = u_zoom / 32.0;
    float sx = floor(u_screen.z + p.x * u_zoom + 0.5);
    float sy = floor(u_screen.w + (p.y * u_trig.x - p.z * u_trig.y) * u_zoom + 0.5);
    float rock = sin(u_wind.y + a_color.g * 6.2831853) * 0.5 + 0.5;
    float sway = floor(a_color.r * 15.9375 * u_wind.x * (0.3 + 0.7 * rock) + 0.5);
    sx += (a_uv.x + sway) * texel;
    float standing = floor(a_uv.y * u_trig.y + 0.5);
    sy -= mix(a_uv.y, standing, a_color.b) * texel;
    float d = -(p.y * u_trig.y + p.z * u_trig.x) * u_depth - 0.12 * u_depth;
    v_uv = vec2(a_uv.z, a_uv.w - a_color.b * standing * u_texel);
    gl_Position = vec4(sx / u_screen.x * 2.0 - 1.0, 1.0 - sy / u_screen.y * 2.0, d, 1.0);
}
)";

const char* sprite_fragment_source = R"(
uniform sampler2D u_texture;
uniform vec4 u_tint;
varying vec2 v_uv;
void main() {
    vec4 c = texture2D(u_texture, v_uv);
    if (c.a < 0.5) {
        discard;
    }
    gl_FragColor = vec4(c.rgb * u_tint.rgb, 1.0);
}
)";

constexpr int atlas_width = 2048, atlas_height = 2048;

} // namespace

bool TiltView::init(const phd::World& world) {
    static const char* const attributes[] = {"a_position", "a_uv", "a_color"};
    if (shoreline_study_) {
        bank_texture_ = shoreline::make_study_texture(world);
    }
    const std::string defines = bank_texture_.id ? "#define SHORELINE_STUDY\n" : "";
    program_ = gl::program((defines + vertex_source).c_str(), (defines + fragment_source).c_str(), attributes, 3);
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
    if (bank_texture_.id) {
        gl::UseProgram(program_);
        gl::Uniform1i(gl::GetUniformLocation(program_, "u_bank"), 1);
        gl::Uniform3f(gl::GetUniformLocation(program_, "u_bank_region"), shoreline::left, shoreline::top, shoreline::extent);
    }
    gl::GenBuffers(1, &scene_vbo_);
    if (shoreline_study_) {
        sprite_program_ = gl::program(sprite_vertex_source, sprite_fragment_source, attributes, 3);
        if (!sprite_program_) {
            return false;
        }
        s_focus_ = gl::GetUniformLocation(sprite_program_, "u_focus");
        s_trig_ = gl::GetUniformLocation(sprite_program_, "u_trig");
        s_zoom_ = gl::GetUniformLocation(sprite_program_, "u_zoom");
        s_screen_ = gl::GetUniformLocation(sprite_program_, "u_screen");
        s_depth_ = gl::GetUniformLocation(sprite_program_, "u_depth");
        s_tint_ = gl::GetUniformLocation(sprite_program_, "u_tint");
        s_wind_ = gl::GetUniformLocation(sprite_program_, "u_wind");
        s_texel_ = gl::GetUniformLocation(sprite_program_, "u_texel");
        gl::UseProgram(sprite_program_);
        gl::Uniform1i(gl::GetUniformLocation(sprite_program_, "u_texture"), 0);
        gl::GenBuffers(1, &scene_sprite_vbo_);
        atlas_ = make_texture(atlas_width, atlas_height, gl::RGBA, nullptr);
        reset_atlas();
    }

    width_ = world.width();
    height_ = world.height();
    corners_.assign(std::size_t(width_ + 1) * (height_ + 1), 0);
    build_heights(world);
    drawn_ = corners_;
    for (int y = 0; y < height_; y += chunk_cells) {
        for (int x = 0; x < width_; x += chunk_cells) {
            Chunk chunk{x, y, std::min(chunk_cells, width_ - x), std::min(chunk_cells, height_ - y), 0, 0};
            gl::GenBuffers(1, &chunk.vbo);
            gl::GenBuffers(1, &chunk.ibo);
            gl::GenBuffers(1, &chunk.sprite_vbo);
            gl::GenBuffers(1, &chunk.shadow_vbo);
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
            float& corner = corners_[std::size_t(j) * (width_ + 1) + i];
            const float now = metres * relief_;
            if (corner == now) {
                continue;
            }
            corner = now;
            if (chunks_.empty()) {
                continue;
            }
            // Its height, and the slopes of the corners beside it, are in
            // the meshes of the chunks around it.
            const int across = (width_ + chunk_cells - 1) / chunk_cells;
            const int x0 = std::max(i - 2, 0) / chunk_cells, x1 = std::min(i + 1, width_ - 1) / chunk_cells;
            const int y0 = std::max(j - 2, 0) / chunk_cells, y1 = std::min(j + 1, height_ - 1) / chunk_cells;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    chunks_[std::size_t(y) * across + x].stale = true;
                }
            }
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
            // The ground here is now this high. If that is news, the plants
            // standing on it are rebuilt with the mesh, so they move as one.
            float& was = drawn_[std::size_t(y) * (width_ + 1) + x];
            if (was != z) {
                was = z;
                chunk.built_atlas = -1;
            }
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
    chunk.stale = false;
}

void TiltView::refresh(const phd::World& world, int count) {
    // Water levels change slowly. The heights are read again, and the
    // meshes follow: those on screen as they are drawn, the rest in turn.
    build_heights(world);
    for (int k = 0; k < count && !chunks_.empty(); ++k) {
        Chunk& chunk = chunks_[std::size_t(next_++ % int(chunks_.size()))];
        if (chunk.stale) {
            build_chunk(chunk);
        }
    }
}

float TiltView::ground(float x, float y) const {
    x = std::clamp(x, 0.0f, float(width_) - 0.001f);
    y = std::clamp(y, 0.0f, float(height_) - 0.001f);
    const int i = int(x), j = int(y);
    const float fx = x - float(i), fy = y - float(j);
    const auto at = [&](int a, int b) { return drawn_[std::size_t(b) * (width_ + 1) + a]; };
    // The heights the meshes were built with, and the same split as their
    // triangles (a-b-d, a-d-c), so objects stand exactly on the surface
    // that is drawn.
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
    // A frustum of `sides` faces around an axis from (x, y, z0) up to
    // (x + lean_x, y + lean_y, z1): the lean is how trees sway.
    const auto leaning = [&](float x, float y, float z0, float z1, float r0, float r1, int sides, Color color,
                             float lean_x, float lean_y) {
        const float ux = x + lean_x, uy = y + lean_y;
        for (int k = 0; k < sides; ++k) {
            const float a0 = float(k) * 6.2831853f / float(sides), a1 = float(k + 1) * 6.2831853f / float(sides);
            const V3 b0{x + std::cos(a0) * r0, y + std::sin(a0) * r0, z0}, b1{x + std::cos(a1) * r0, y + std::sin(a1) * r0, z0};
            const V3 t0{ux + std::cos(a0) * r1, uy + std::sin(a0) * r1, z1}, t1{ux + std::cos(a1) * r1, uy + std::sin(a1) * r1, z1};
            const float mid = (a0 + a1) / 2;
            const V3 out{std::cos(mid) * (z1 - z0), std::sin(mid) * (z1 - z0), r0 - r1};
            quad(b0, b1, t1, t0, color, out);
            if (r1 > 0) {
                tri({ux, uy, z1}, t0, t1, lit(color, {0, 0, 1}));
            }
            tri({x, y, z0}, b1, b0, lit(color, {0, 0, -1}));
        }
    };
    const auto frustum = [&](float x, float y, float z0, float z1, float r0, float r1, int sides, Color color) {
        leaning(x, y, z0, z1, r0, r1, sides, color, 0, 0);
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
        // The study draws its plants as sprites (draw_plants), not from here.
        for (int cy = y0; cy <= (shoreline_study_ ? y0 - 1 : y1); ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                const auto t = art::tree_in(world, cx, cy);
                if (t.radius <= 0) {
                    continue;
                }
                ++trees_;
                const float g = ground(t.x, t.y), r = t.radius, h = std::max(1.2f, t.height);
                const Color leaves{t.r, t.g, t.b, 255}, bark{96, 72, 50, 255};
                // Sway: the crown leans with the wind and rocks, more for tall
                // trees and strong wind, each tree on its own beat.
                const float gust = float(world.wind_speed()) / 96.0f;
                const float rock = std::sin(float(time) * 1.7f + float(t.id % 97)) * 0.5f + 0.5f;
                const float sway = gust * (0.3f + 0.7f * rock) * h * 0.025f;
                const float wx = std::cos(float(world.wind_angle()) * 6.2831853f / 256.0f) * sway;
                const float wy = std::sin(float(world.wind_angle()) * 6.2831853f / 256.0f) * sway;
                const auto at = [&](float f) { return f * f; }; // how far up the lean has built
                if (!t.bare) {
                    shadow(t.x - sun.x * r * 0.9f, t.y - sun.y * r * 0.9f, r * 0.95f);
                }
                leaning(t.x, t.y, g - 0.05f, g + h * 0.45f, 0.08f + 0.05f * r, 0.05f + 0.03f * r, 5, bark, wx * at(0.45f),
                        wy * at(0.45f));
                const auto layer = [&](float f0, float f1, float r0, float r1, Color colour) {
                    leaning(t.x + wx * at(f0), t.y + wy * at(f0), g + h * f0, g + h * f1, r0, r1, 7, colour,
                            wx * (at(f1) - at(f0)), wy * (at(f1) - at(f0)));
                };
                if (t.bare) {
                    // Winter: a thin, open crown of grey branches.
                    layer(0.4f, 0.75f, r * 0.15f, r * 0.55f, Color{104, 92, 82, 255});
                    layer(0.75f, 1.0f, r * 0.55f, r * 0.1f, Color{104, 92, 82, 255});
                } else if (t.conifer) {
                    layer(0.2f, 0.62f, r, r * 0.45f, leaves);
                    layer(0.5f, 1.0f, r * 0.7f, 0.0f, leaves);
                } else {
                    layer(0.35f, 0.6f, r * 0.55f, r, leaves);
                    layer(0.6f, 0.85f, r, r * 0.8f, leaves);
                    layer(0.85f, 1.0f, r * 0.8f, r * 0.25f, leaves);
                }
            }
        }
        // Shrubs: woody plants in a cell's quarters, as small domes.
        for (int cy = y0; cy <= (shoreline_study_ ? y0 - 1 : y1); ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                const auto i = std::size_t(cy) * width_ + cx;
                if (world.tree(i)) {
                    continue;
                }
                for (int slot = 0; slot < 4; ++slot) {
                    const phd::Plant* p = world.occupant(i, slot);
                    if (!p || !phd::woody(p->genome) || p->height < 4) {
                        continue;
                    }
                    const float sx = float(cx) + 0.25f + 0.5f * float(slot & 1), sy = float(cy) + 0.25f + 0.5f * float(slot >> 1);
                    const float hh = float(p->height) / float(phd::cell_decimetres), rr = std::min(0.3f, 0.1f + hh * 0.2f);
                    const float g = ground(sx, sy);
                    const bool bare = (p->flags & phd::plant_dormant) && !phd::evergreen(p->genome);
                    const Color c = bare ? Color{112, 96, 80, 255} : Color{std::uint8_t(60 + p->genome.gene[phd::gene_leaf] / 6),
                                                                          std::uint8_t(110 - p->genome.gene[phd::gene_leaf] / 12), 52, 255};
                    frustum(sx, sy, g - 0.02f, g + hh * 0.6f, rr * 0.7f, rr, 6, c);
                    frustum(sx, sy, g + hh * 0.6f, g + hh, rr, rr * 0.3f, 6, c);
                }
            }
        }
        // Fire: flickering flames over burning cells.
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                const int f = world.fire(std::size_t(cy) * width_ + cx);
                if (!f) {
                    continue;
                }
                for (int k = 0; k < 3; ++k) {
                    const auto hsh = phd::hash3(cx * 3 + k, cy, std::uint32_t(time * 10));
                    const float fx = float(cx) + 0.2f + float(hsh & 255) / 255.0f * 0.6f;
                    const float fy = float(cy) + 0.2f + float(hsh >> 8 & 255) / 255.0f * 0.6f;
                    const float fh = (0.4f + float(hsh >> 16 & 255) / 255.0f * 0.8f) * float(f) / 8.0f;
                    const float g = ground(fx, fy);
                    frustum(fx, fy, g, g + fh, 0.18f, 0.0f, 5, k == 0 ? Color{255, 220, 90, 255} : Color{240, 120, 40, 255});
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
    if (!shoreline_study_) {
        frustum(tx, ty, tz - 0.05f, tz + 1.0f, 0.10f, 0.06f, 6, {112, 80, 52, 255});
        frustum(tx, ty, tz + 0.7f, tz + 1.3f, 0.45f, 0.75f, 7, {58, 118, 54, 255});
        frustum(tx, ty, tz + 1.3f, tz + 2.3f, 0.75f, 0.0f, 7, {66, 132, 58, 255});
    }

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

void TiltView::reset_atlas() {
    const std::vector<std::uint8_t> clear(std::size_t(atlas_width) * atlas_height * 4, 0);
    update_texture(atlas_, gl::RGBA, clear.data());
    shelves_.clear();
    sprite_cache_.clear();
    atlas_full_ = false;
    ++atlas_generation_; // every chunk's quads now point at nothing
}

// Room for a w x h image: on a shelf of images about as tall, or a new shelf.
bool TiltView::allocate(int w, int h, int& x, int& y) {
    // Shelf heights step up by about a quarter, so few shelves cover every
    // size and none wastes much height.
    int tall = 8;
    while (tall < h) {
        tall = (tall * 5 / 4 + 3) / 4 * 4;
    }
    for (auto& s : shelves_) {
        if (s.h == tall && s.x + w <= s.end) {
            x = s.x;
            y = s.y;
            s.x += w;
            return true;
        }
    }
    // A new shelf, in whichever half of the atlas is less full.
    constexpr int columns = 2, column_width = atlas_width / columns;
    int tops[columns] = {};
    for (const auto& s : shelves_) {
        tops[s.end / column_width - 1] += s.h;
    }
    const int column = tops[1] < tops[0] ? 1 : 0;
    if (w > column_width || tops[column] + tall > atlas_height) {
        return false;
    }
    x = column * column_width;
    y = tops[column];
    shelves_.push_back({y, tall, x + w, x + column_width});
    return true;
}

const TiltView::Sprite* TiltView::sprite(const art::sprites::Spec& spec) {
    const auto key = spec.key();
    if (const auto found = sprite_cache_.find(key); found != sprite_cache_.end()) {
        return &found->second;
    }
    const auto image = art::sprites::paint(spec);
    // A clear texel around every sprite, so a swaying edge samples nothing.
    int x = 0, y = 0;
    if (!allocate(image.width + 2, image.height + 2, x, y)) {
        // No room: stand in the nearest sprite of the same kind already
        // painted. The atlas is emptied and repainted later, not now.
        atlas_full_ = true;
        const Sprite* nearest = nullptr;
        int best = 1 << 30;
        for (const auto& entry : sprite_cache_) {
            const auto& other = entry.second.spec;
            if (other.kind != spec.kind || other.state != spec.state) {
                continue;
            }
            const int apart = std::abs(int(other.height) - int(spec.height)) * 4 + std::abs(int(other.radius) - int(spec.radius)) * 4 +
                              (std::abs(int(other.r) - int(spec.r)) + std::abs(int(other.g) - int(spec.g)) +
                               std::abs(int(other.b) - int(spec.b))) / 4 + (other.flower != spec.flower ? 8 : 0);
            if (apart < best) {
                best = apart;
                nearest = &entry.second;
            }
        }
        // Remembered under this spec too, so the search is made once.
        return nearest ? &sprite_cache_.emplace(key, *nearest).first->second : nullptr;
    }
    ++x, ++y;
    update_texture_region(atlas_, x, y, image.width, image.height, gl::RGBA, image.rgba.data());
    const Sprite made{float(x) / atlas_width, float(y) / atlas_height, float(x + image.width) / atlas_width,
                      float(y + image.height) / atlas_height, image.width, image.height, image.anchor_x,
                      image.anchor_y, spec};
    return &sprite_cache_.emplace(key, made).first->second;
}

// One sprite, hung from its spot. `lift` raises the spot above the ground, in
// cells, and `below` is how many of the sprite's rows hang beneath it. A
// trunk's top corners carry its true length and the shader shortens it.
void TiltView::add_sprite(const Sprite& s, float x, float y, float z, float sway, std::uint32_t id, Stand stand,
                          float lift, int below) {
    const float tpc = float(art::sprites::texels_per_cell);
    const float left = -float(s.anchor), right = float(s.w - s.anchor);
    float top = float(s.h - below), bottom = -float(below), top_v = s.v0;
    const auto beat = std::uint8_t(phd::hash3(int(id), 5, 0x9e3779b9u));
    const auto swing = std::uint8_t(std::clamp(sway * 16, 0.0f, 255.0f));
    Color high{swing, beat, 0, 0}, low{std::uint8_t(stand == Stand::rigid ? swing : 0), beat, 0, 0};
    if (stand == Stand::upright) {
        // No more than is painted, at the lowest the camera tilts.
        top = std::min(lift * tpc, float(s.h - 1) / 0.708f);
        top_v = s.v1;
        high.b = 255;
        lift = 0;
    }
    z += lift;
    const SpriteVertex a{x, y, z, left, top, s.u0, top_v, high}, b{x, y, z, right, top, s.u1, top_v, high},
        c{x, y, z, right, bottom, s.u1, s.v1, low}, d{x, y, z, left, bottom, s.u0, s.v1, low};
    sprite_scratch_.insert(sprite_scratch_.end(), {a, b, c, a, c, d});
    // How far it can reach up the screen, at the lowest the camera tilts.
    const float reach = stand == Stand::upright ? top * 0.72f : lift * tpc * 0.72f + top;
    tallest_ = std::max(tallest_, int(reach) + 1);
    widest_ = std::max({widest_, s.anchor, s.w - s.anchor});
}

// A tree in pieces, each at its own height, so they slide over one another
// as the camera tilts: a trunk, and a crown or a stack of boughs.
void TiltView::add_tree(float x, float y, float h, float r, bool conifer, bool leafless, std::uint8_t cr,
                        std::uint8_t cg, std::uint8_t cb, std::uint32_t id) {
    using namespace art::sprites;
    const float tpc = float(texels_per_cell), z = ground(x, y);
    const float sway = h * 0.025f * tpc;
    const auto trunk = [&](float length, float girth, float swing) {
        Spec spec;
        spec.kind = Kind::trunk;
        // Painted as long as it shows at 45 degrees, the lowest tilt, and a little over.
        spec.height = std::uint16_t(std::min(1000, quantise(length * tpc * 0.72f, 4) + 10));
        spec.radius = std::uint16_t(girth + 0.5f);
        spec.state = leafless ? bare : 0;
        if (const Sprite* s = sprite(spec)) {
            add_sprite(*s, x, y, z, swing, id, Stand::upright, length);
        }
    };
    Spec spec;
    spec.r = cr, spec.g = cg, spec.b = cb;
    if (conifer) {
        // Tiers of boughs up the trunk, each narrower than the one below and
        // standing a little in front of it, as a cone's skirt does.
        const float reach = r * 0.7f, stem = std::max(0.15f, h / 9);
        const int tiers = std::clamp(int(h * tpc * rise / 9), 3, 9);
        const float step = (h - stem) / float(tiers);
        trunk(stem + step * 1.5f, std::clamp(reach * tpc * 0.16f, 1.0f, 7.0f), sway * (stem + step) / h);
        for (int k = 0; k < tiers; ++k) {
            const bool last = k == tiers - 1;
            const float wide = reach * std::pow(1 - float(k) / float(tiers), 0.8f), up = stem + step * float(k);
            spec.kind = Kind::bough;
            spec.radius = std::uint16_t(std::clamp(quantise(wide * tpc, 2), 2, 500));
            spec.height = std::uint16_t(
                std::clamp(quantise(step * tpc * rise * (last ? 1.0f : 1.7f) + wide * tpc * 0.45f, 2), 4, 1000));
            spec.state = last ? tip : 0;
            spec.variant = std::uint8_t((id + unsigned(k)) % 2);
            if (const Sprite* s = sprite(spec)) {
                add_sprite(*s, x, y + wide * 0.5f, z, sway * (up + step) / h, id, Stand::rigid, up);
            }
        }
        return;
    }
    // A ball of leaves with its top at the tree's height, clear of the ground.
    const float lift = std::max(h - r, r * 1.4f);
    trunk(lift, std::clamp(r * tpc * 0.2f, 2.0f, 9.0f), sway);
    spec.kind = Kind::crown;
    spec.radius = std::uint16_t(std::min(500, quantise(r * tpc, 2)));
    spec.state = leafless ? bare : 0;
    spec.variant = std::uint8_t(id % 3);
    if (const Sprite* s = sprite(spec)) {
        add_sprite(*s, x, y, z, sway, id, Stand::rigid, lift, s->anchor_y);
    }
}

// The plants of one chunk as sprite quads and shadows.
void TiltView::build_plants(const phd::World& world, Chunk& chunk) {
    using namespace art::sprites;
    sprite_scratch_.clear();
    shadow_scratch_.clear();
    chunk.trees = 0;
    tallest_ = widest_ = 0;
    const auto sun = art::sun_for(world);
    const auto unit = [](std::uint32_t bits) { return float(bits & 255) / 255.0f; };
    // A shadow on the ground: a polygon as a fan from its first corner.
    const auto shadow = [&](float x, float y, float radius, int sides) {
        const Color dark{10, 14, 10, 80};
        const auto corner = [&](int k) {
            const float a = (float(k) + 0.5f) * 6.2831853f / float(sides);
            const float px = x + std::cos(a) * radius, py = y + std::sin(a) * radius;
            return Vertex{px, py, ground(px, py) + 0.05f, white_u_, white_v_, dark};
        };
        const Vertex first = corner(0);
        for (int k = 1; k + 1 < sides; ++k) {
            shadow_scratch_.insert(shadow_scratch_.end(), {first, corner(k), corner(k + 1)});
        }
    };
    const float tpc = float(texels_per_cell);
    for (int cy = chunk.y0; cy < chunk.y0 + chunk.h; ++cy) {
        for (int cx = chunk.x0; cx < chunk.x0 + chunk.w; ++cx) {
            const auto cell = std::size_t(cy) * width_ + cx;
            const bool wet = world.water(cell) != phd::Water::none;
            if (const auto t = art::tree_in(world, cx, cy); t.radius > 0) {
                ++chunk.trees;
                const float r = t.radius;
                add_tree(t.x, t.y, std::max(1.2f, t.height), r, t.conifer, t.bare, (t.r & 0xe0) | 0x10,
                         (t.g & 0xe0) | 0x10, (t.b & 0xe0) | 0x10, t.id);
                if (!t.bare && !wet) {
                    shadow(t.x - sun.x * r * 0.8f, t.y - sun.y * r * 0.8f, r * 0.86f, 10);
                }
                continue;
            }
            const int depth = wet ? world.water_depth_dm(cell) : 0;
            for (int slot = 0; slot < 4; ++slot) {
                const phd::Plant* p = world.occupant(cell, slot);
                if (!p) {
                    continue;
                }
                const auto place = phd::hash3(0, int(p->id), 0x841d5a73u);
                const auto& genes = p->genome.gene;
                const bool dormant = p->flags & phd::plant_dormant;
                Spec spec;
                spec.variant = std::uint8_t(p->id % 3);
                if (phd::woody(p->genome)) {
                    if (p->height < 4) {
                        continue;
                    }
                    const float x = float(cx) + 0.25f + 0.5f * float(slot & 1) + (unit(place) - 0.5f) * 0.3f;
                    const float y = float(cy) + 0.25f + 0.5f * float(slot >> 1) + (unit(place >> 8) - 0.5f) * 0.3f;
                    const float hh = float(p->height) / float(phd::cell_decimetres);
                    const float reach = std::min(0.58f, std::min(0.3f, 0.1f + hh * 0.2f) + 0.16f * hh);
                    const bool leafless = dormant && !phd::evergreen(p->genome);
                    const bool cone = phd::evergreen(p->genome) && genes[phd::gene_cold] >= 150;
                    float front; // how far south of its spot the sprite touches the ground, at 55 degrees
                    if (cone) {
                        spec.kind = Kind::cone_bush;
                        spec.radius = std::uint16_t(quantise(reach * 0.6f * tpc, 2));
                        spec.height = std::uint16_t(quantise(hh * tpc * rise * 1.15f + 3, 2));
                        front = 0.1f;
                    } else {
                        // A dome: its top at the shrub's height, its near edge on the ground in front.
                        spec.kind = Kind::bush;
                        spec.radius = std::uint16_t(quantise(reach * tpc, 2));
                        spec.height = std::uint16_t(quantise(hh * tpc * rise + reach * tpc * lie * 0.5f, 2));
                        front = reach * 0.5f;
                    }
                    const int leaf = genes[phd::gene_leaf] / 52 * 52 + 26; // five leaf colours
                    spec.r = std::uint8_t(leafless ? 112 : 60 + leaf / 6);
                    spec.g = std::uint8_t(leafless ? 96 : 110 - leaf / 12);
                    spec.b = std::uint8_t(leafless ? 80 : 52);
                    spec.state = leafless ? bare : 0;
                    const Sprite* s = sprite(spec);
                    if (s) {
                        // Hung from its middle, so it settles over its spot as the camera rises.
                        const float lift = hh * 0.45f;
                        add_sprite(*s, x, y, ground(x, y), hh * 0.03f * tpc, p->id, Stand::rooted, lift,
                                   int((front * lie + lift * rise) * tpc + 0.5f));
                    }
                    if (s && !leafless && !wet && hh > 0.4f) {
                        shadow(x - sun.x * reach * 0.5f, y + front * 0.6f - sun.y * reach * 0.5f, reach * 0.8f, 6);
                    }
                    continue;
                }
                // Soft plants: grass, herbs and reeds, as tall as what stands clear of the water.
                const float height = float(int(p->height) - depth) / float(phd::cell_decimetres);
                if (height < 0.07f) {
                    continue;
                }
                const float x = float(cx) + 0.25f + 0.5f * float(slot & 1) + (unit(place) - 0.5f) * 0.4f;
                const float y = float(cy) + 0.25f + 0.5f * float(slot >> 1) + (unit(place >> 8) - 0.5f) * 0.4f;
                const bool reed = genes[phd::gene_flood] > 155;
                const bool flowers = (p->flags & phd::plant_flowering) && !dormant;
                spec.kind = reed ? Kind::reed : Kind::grass;
                spec.height = std::uint16_t(std::max(3, quantise(height * tpc * rise)));
                spec.radius = std::uint16_t(std::clamp(int(float(spec.height) * (reed ? 0.2f : 0.35f)), 2, reed ? 4 : 6));
                spec.r = std::uint8_t(genes[phd::gene_leaf] / 64 * 6 + 3);
                spec.state = std::uint8_t((dormant ? bare : 0) | (flowers ? flowering : 0));
                spec.flower = flowers ? std::uint8_t(genes[phd::gene_flower] * 6 / 256) : 0;
                const Sprite* s = sprite(spec);
                if (s) {
                    add_sprite(*s, x, y, ground(x, y), std::max(1.0f, height * 0.06f * tpc), p->id);
                }
            }
        }
    }
}

void TiltView::draw_plants(const phd::World& world, const Camera& camera, const Font& font, int screen_w, int screen_h,
                           int view_h, double time) {
    font.white(white_u_, white_v_);
    sprites_drawn_ = 0;
    trees_ = 0;
    // The chunks whose plants can show: a tall tree south of the view reaches up into it.
    const float texel = camera.px() / float(art::sprites::texels_per_cell);
    plant_chunks_.clear();
    for (auto& chunk : chunks_) {
        // How far its sprites reach beyond its ground: measured when it was
        // last built, with room to grow and sway; a guess until then, and
        // again once that is long ago.
        const bool measured = chunk.built_atlas == atlas_generation_ && world.tick() - chunk.built_tick < 2000;
        const float beside = float(measured ? chunk.wide * 5 / 4 + 24 : 260) * texel;
        const float above = float(measured ? chunk.tall * 5 / 4 + 24 : 440) * texel;
        float sx0, sy0, sx1, sy1;
        camera.to_screen(float(chunk.x0), float(chunk.y0), chunk.zmax, float(screen_w), float(view_h), sx0, sy0);
        camera.to_screen(float(chunk.x0 + chunk.w), float(chunk.y0 + chunk.h), chunk.zmin, float(screen_w),
                         float(view_h), sx1, sy1);
        if (sx1 + beside < 0 || sx0 - beside > float(screen_w) || sy1 < 0 || sy0 - above > float(view_h)) {
            continue;
        }
        plant_chunks_.push_back(&chunk);
    }
    if (plant_chunks_.empty()) {
        return;
    }
    // A full atlas is emptied now and then, so sprites nothing uses any more
    // (last season's colours, plants that have grown) make room.
    if (atlas_full_ && ++frames_full_ > 600) {
        frames_full_ = 0;
        reset_atlas();
    }
    // Bring them up to date: all that have never been built, and two a frame
    // of those the world has moved on from, in turn.
    const auto count = unsigned(plant_chunks_.size());
    {
        int refreshed = 0;
        for (unsigned k = 0; k < count; ++k) {
            Chunk& chunk = *plant_chunks_[(plant_cursor_ + k) % count];
            const bool unbuilt = chunk.built_atlas != atlas_generation_;
            if (!unbuilt && (chunk.built_tick == world.tick() || refreshed >= 2)) {
                continue;
            }
            refreshed += !unbuilt;
            build_plants(world, chunk);
            chunk.built_tick = world.tick();
            chunk.built_atlas = atlas_generation_;
            chunk.tall = tallest_;
            chunk.wide = widest_;
            chunk.sprite_vertices = int(sprite_scratch_.size());
            chunk.shadow_vertices = int(shadow_scratch_.size());
            gl::BindBuffer(gl::ARRAY_BUFFER, chunk.sprite_vbo);
            gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(sprite_scratch_.size() * sizeof(SpriteVertex)),
                           sprite_scratch_.data(), gl::DYNAMIC_DRAW);
            gl::BindBuffer(gl::ARRAY_BUFFER, chunk.shadow_vbo);
            gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(shadow_scratch_.size() * sizeof(Vertex)),
                           shadow_scratch_.data(), gl::DYNAMIC_DRAW);
        }
    }
    plant_cursor_ += 2;

    // The test tree, standing with the rest.
    sprite_scratch_.clear();
    if (scene_) {
        add_tree(scene_x_, scene_y_, 2.7f, 0.81f, false, false, 88, 136, 56, 7342u);
    }

    // Shadows first, on the ground, with the terrain's program.
    gl::Uniform1f(u_lit_, 0);
    gl::BindTexture(gl::TEXTURE_2D, font.texture().id);
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::EnableVertexAttribArray(2);
    for (const Chunk* chunk : plant_chunks_) {
        if (!chunk->shadow_vertices) {
            continue;
        }
        gl::BindBuffer(gl::ARRAY_BUFFER, chunk->shadow_vbo);
        const auto stride = gl::GLsizei(sizeof(Vertex));
        gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, x)));
        gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, u)));
        gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride, reinterpret_cast<const void*>(offsetof(Vertex, color)));
        gl::DrawArrays(gl::TRIANGLES, 0, chunk->shadow_vertices);
        ++draw_calls_;
        triangles_ += chunk->shadow_vertices / 3;
    }

    gl::UseProgram(sprite_program_);
    gl::Uniform3f(s_focus_, camera.cx, camera.cy, camera.focus_z);
    gl::Uniform2f(s_trig_, camera.sin_pitch(), camera.cos_pitch());
    gl::Uniform1f(s_zoom_, camera.px());
    gl::Uniform4f(s_screen_, float(screen_w), float(screen_h), float(screen_w) / 2, float(view_h) / 2);
    const float reach = float(view_h) / (2 * camera.px() * camera.sin_pitch()) + 40;
    gl::Uniform1f(s_depth_, 1.0f / (reach * camera.cos_pitch() + (3000 * relief_ + 4) * camera.sin_pitch() + 2));
    std::uint8_t tint[3];
    art::day_tint(world, tint);
    gl::Uniform4f(s_tint_, float(tint[0]) / 255, float(tint[1]) / 255, float(tint[2]) / 255, 1);
    // Plants lean with the wind's push across the screen, and rock on their own beats.
    const float gust = float(world.wind_speed()) / 96.0f;
    const float across = std::cos(float(world.wind_angle()) * 6.2831853f / 256.0f);
    gl::Uniform2f(s_wind_, gust * (across < 0 ? std::min(across, -0.35f) : std::max(across, 0.35f)),
                  float(std::fmod(time * 1.7, 6.283185307179586)));
    gl::Uniform1f(s_texel_, 1.0f / float(atlas_height));
    gl::BindTexture(gl::TEXTURE_2D, atlas_.id);
    const auto draw_sprites = [&](gl::GLuint vbo, int vertices) {
        if (!vertices) {
            return;
        }
        gl::BindBuffer(gl::ARRAY_BUFFER, vbo);
        const auto stride = gl::GLsizei(sizeof(SpriteVertex));
        gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, x)));
        gl::VertexAttribPointer(1, 4, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, ox)));
        gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride,
                                reinterpret_cast<const void*>(offsetof(SpriteVertex, sway)));
        gl::DrawArrays(gl::TRIANGLES, 0, vertices);
        ++draw_calls_;
        triangles_ += vertices / 3;
        sprites_drawn_ += vertices / 6;
    };
    for (const Chunk* chunk : plant_chunks_) {
        draw_sprites(chunk->sprite_vbo, chunk->sprite_vertices);
        trees_ += chunk->trees;
    }
    if (!sprite_scratch_.empty()) {
        gl::BindBuffer(gl::ARRAY_BUFFER, scene_sprite_vbo_);
        gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(sprite_scratch_.size() * sizeof(SpriteVertex)),
                       sprite_scratch_.data(), gl::STREAM_DRAW);
        draw_sprites(scene_sprite_vbo_, int(sprite_scratch_.size()));
    }
    gl::UseProgram(program_);
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
    if (bank_texture_.id) {
        gl::ActiveTexture(gl::TEXTURE0 + 1);
        gl::BindTexture(gl::TEXTURE_2D, bank_texture_.id);
    }
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, terrain.id);
    int caught_up = 0;
    for (auto& chunk : chunks_) {
        // Cull chunks whose box can't reach the view.
        float sx0, sy0, sx1, sy1;
        camera.to_screen(float(chunk.x0), float(chunk.y0), chunk.zmax, float(screen_w), float(view_h), sx0, sy0);
        camera.to_screen(float(chunk.x0 + chunk.w), float(chunk.y0 + chunk.h), chunk.zmin, float(screen_w),
                         float(view_h), sx1, sy1);
        if (sx1 < 0 || sx0 > float(screen_w) || sy1 < 0 || sy0 > float(view_h)) {
            continue;
        }
        // What is on screen follows the water at once, a few chunks a frame.
        if (chunk.stale && caught_up < 8) {
            ++caught_up;
            build_chunk(chunk);
        }
        gl::BindBuffer(gl::ARRAY_BUFFER, chunk.vbo);
        gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, chunk.ibo);
        attributes();
        gl::DrawElements(gl::TRIANGLES, chunk.indices, gl::UNSIGNED_SHORT, nullptr);
        ++draw_calls_;
        triangles_ += chunk.indices / 3;
    }

    if (shoreline_study_) {
        draw_plants(world, camera, font, screen_w, screen_h, view_h, time);
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
