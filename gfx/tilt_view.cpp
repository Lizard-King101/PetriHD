#include "tilt_view.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <tuple>

#include "math.hpp"
#include "ground_art.hpp"
#include "palette.hpp"
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
#ifdef PAINTED_GROUND
uniform vec4 u_ground_window; // a whole-window origin in cells, then the validity map's offset in UV
varying vec2 v_ground;
#endif
varying vec4 v_color;
uniform vec3 u_sun;
uniform float u_lit; // 1: a_color carries the ground's normal; 0: a lit colour
void main() {
    vec3 p = a_position - u_focus;
    float sx = u_screen.z + p.x * u_zoom;
    float sy = u_screen.w + (p.y * u_trig.x - p.z * u_trig.y) * u_zoom;
    float d = -(p.y * u_trig.y + p.z * u_trig.x) * u_depth;
#ifdef SHADOW
    // A shadow is laid over the ground in flat pieces: nearer by enough that
    // the ground's own bumps between its corners don't cut it.
    d -= 0.4 * u_depth;
#endif
    v_uv = a_uv;
#ifdef PAINTED_GROUND
    // The window is 64 cells square and wraps.
    v_ground = (a_position.xy - u_ground_window.xy) / 64.0;
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
#ifdef PAINTED_GROUND
uniform sampler2D u_ground;
uniform sampler2D u_ground_valid;
uniform vec4 u_ground_window;
uniform float u_lit;
varying vec2 v_ground;
#endif
uniform vec4 u_tint;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
#ifdef PAINTED_GROUND
    // A reassigned slot can still contain the previous bank's pixels.
    vec2 local = v_ground - u_ground_window.zw;
    if (u_lit > 0.5 && all(greaterThanEqual(local, vec2(0.0))) && all(lessThan(local, vec2(1.0))) &&
        texture2D(u_ground_valid, local).r > 0.5) {
        gl_FragColor = texture2D(u_ground, v_ground) * v_color * u_tint;
        return;
    }
#endif
    gl_FragColor = texture2D(u_texture, v_uv) * v_color * u_tint;
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
attribute vec4 a_color; // r: sway, in sixteenths of a texel; g: the plant's beat; b: a trunk's top; a: its only view
uniform vec3 u_focus;
uniform vec2 u_trig;
uniform float u_zoom;
uniform vec4 u_screen;
uniform float u_depth;
uniform vec2 u_wind; // strength across the screen, and the time
uniform float u_texel; // one row of the atlas
uniform float u_view;  // one more than the view the camera's pitch calls for
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
    // A picture painted for another pitch is put out of sight.
    if (a_color.a > 0.0 && abs(a_color.a * 255.0 - u_view) > 0.5) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    }
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
    // A texel that glows (art::sprites::glow_alpha) is not dimmed by the hour.
    gl_FragColor = vec4(c.a < 0.9 ? c.rgb : c.rgb * u_tint.rgb, 1.0);
}
)";

// A shadow is a plant's own sprite laid on the ground: only its outline is
// used. Where the image has an alpha channel, the ground's alpha holds how
// dark a shadow makes it, and the first shadow to fall on a pixel uses that
// up, so overlapping shadows darken the ground once and not twice.
const char* shadow_fragment_source = R"(
uniform sampler2D u_texture;
uniform float u_alpha;
varying vec2 v_uv;
void main() {
    if (texture2D(u_texture, v_uv).a < 0.5) {
        discard;
    }
    gl_FragColor = vec4(0.04, 0.055, 0.04, u_alpha);
}
)";
// Water is a surface of its own over the bed. Each fragment knows how deep
// the water is under it (the surface less the bed, as the two meshes have
// them), so the shore is wherever that reaches nothing, wandering by a noise
// laid on the ground's own texels and lapping in and out. The bed shows
// through the shallows in steps; deep water is opaque. Glints drift over it.
const char* water_vertex_source = R"(
attribute vec3 a_position;
attribute vec2 a_uv;    // metres of water under this corner, and whether it is frozen
attribute vec4 a_color; // r, g: which way the water runs and how fast, about 128
uniform vec3 u_focus;
uniform vec2 u_trig;
uniform float u_zoom;
uniform vec4 u_screen;
uniform float u_depth;
uniform vec2 u_origin; // a whole number of cells near the view, so the place keeps its precision
varying vec2 v_water;
varying vec2 v_place;
varying vec2 v_flow;
void main() {
    vec3 p = a_position - u_focus;
    float sx = u_screen.z + p.x * u_zoom;
    float sy = u_screen.w + (p.y * u_trig.x - p.z * u_trig.y) * u_zoom;
    float d = -(p.y * u_trig.y + p.z * u_trig.x) * u_depth;
    v_water = a_uv;
    v_flow = (a_color.xy * 255.0 - 128.0) / 127.0;
    v_place = a_position.xy - u_origin;
    gl_Position = vec4(sx / u_screen.x * 2.0 - 1.0, 1.0 - sy / u_screen.y * 2.0, d, 1.0);
}
)";

const char* water_fragment_source = R"(
uniform sampler2D u_texture; // r: a smooth noise; g: a speckle; b: a broad noise. It repeats every 4 cells.
uniform vec4 u_tint;
uniform float u_time;
varying vec2 v_water;
varying vec2 v_place;
varying vec2 v_flow;
// A glint is a short light dash lying along the flow. Each is carried
// downstream for a moment, then gone; two sets take turns, so there are
// always some. Returns how bright the dash over this texel is, if it shows.
float glint(vec2 texel, vec2 flow, float phase, float set) {
    vec2 q = floor(texel - flow * (phase - 0.5) * 40.0);
    // Along the screen in slow water, whichever way it drifts; along a river's run.
    vec2 dash = abs(flow.x) >= abs(flow.y) || length(flow) < 0.5 ? vec2(floor(q.x / 4.0), q.y) : vec2(q.x, floor(q.y / 4.0));
    float lit = texture2D(u_texture, (dash * vec2(7.0, 13.0) + set + 0.5) / 128.0).g;
    return lit * step(0.3, 1.0 - abs(2.0 * phase - 1.0));
}
void main() {
    vec2 texel = floor(v_place * 32.0); // the ground's texels
    vec4 noise = texture2D(u_texture, (texel + 0.5) / 128.0);
    float lap = sin(u_time * 1.4 + noise.b * 6.2831853) * 0.012;
    float depth = v_water.x + (noise.r - 0.5) * 0.16 + (noise.b - 0.5) * 0.14 + lap;
    if (depth <= 0.0) {
        discard;
    }
    vec3 shallow = vec3(0.286, 0.588, 0.573), mid = vec3(0.165, 0.510, 0.573), deep = vec3(0.165, 0.435, 0.553),
         deeper = vec3(0.102, 0.290, 0.486);
    vec3 c = depth < 1.5 ? mix(shallow, mid, clamp((depth - 0.3) / 1.2, 0.0, 1.0))
           : depth < 5.0 ? mix(mid, deep, (depth - 1.5) / 3.5)
                         : mix(deep, deeper, clamp((depth - 5.0) / 15.0, 0.0, 1.0));
    // The bed shows through the first metre, in six steps.
    float alpha = min(1.0, 0.22 + (floor(depth * 6.0) + 0.5) / 6.0);
    if (v_water.y > 0.5) {
        // Ice: pale and nearly opaque, with a few clear flaws.
        c = mix(c, vec3(0.800, 0.871, 0.925), noise.g > 0.9 ? 0.6 : 0.85);
        alpha = max(alpha, 0.9);
    } else {
        if (depth < 0.05 && noise.g > 0.3) {
            c = vec3(0.769, 0.753, 0.561); // the pale line where water meets the bank
            alpha = 0.55;
        }
        // Glints run with the water: fast and many in a river, few and
        // barely drifting on a lake or the sea.
        float speed = length(v_flow);
        vec2 flow = speed > 0.05 ? v_flow : vec2(0.12, 0.0);
        float t = u_time * 0.5;
        float lit = max(glint(texel, flow, fract(t), 0.0), glint(texel, flow, fract(t + 0.5), 37.0));
        if (depth > 0.2 && lit > 0.986 - 0.026 * min(speed, 1.0)) {
            c += 0.10;
        }
    }
    gl_FragColor = vec4(c * u_tint.rgb, alpha);
}
)";

constexpr float shadow_light = 0.70f; // how much of the ground's light a shadow leaves

constexpr int atlas_width = 2048, atlas_height = 2048;

} // namespace

bool TiltView::init(const phd::World& world) {
    static const char* const attributes[] = {"a_position", "a_uv", "a_color"};
    const std::string defines = shoreline_study_ ? "#define PAINTED_GROUND\n" : "";
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
    if (shoreline_study_) {
        gl::UseProgram(program_);
        gl::Uniform1i(gl::GetUniformLocation(program_, "u_ground"), 1);
        gl::Uniform1i(gl::GetUniformLocation(program_, "u_ground_valid"), 2);
        u_ground_window_ = gl::GetUniformLocation(program_, "u_ground_window");
        constexpr int patch_size = patch_cells * art::ground::texels_per_cell;
        constexpr int size = window_patches * patch_size;
        tile_rgba_.resize(patch_size * patch_size * 4);
        tile_half_.resize(tile_rgba_.size() / 4);
        ground_valid_ = make_texture(window_patches, window_patches, gl::RGBA, nullptr);
        for (Texture* texture : {&ground_, &ground_half_}) {
            *texture = make_texture(texture == &ground_ ? size : size / 2, texture == &ground_ ? size : size / 2, gl::RGBA, nullptr);
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::GLint(gl::REPEAT));
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::GLint(gl::REPEAT));
        }
    }
    gl::GenBuffers(1, &scene_vbo_);
    if (shoreline_study_) {
        sprite_program_ = gl::program(sprite_vertex_source, sprite_fragment_source, attributes, 3);
        if (!sprite_program_) {
            return false;
        }
        shadow_program_ = gl::program((std::string("#define SHADOW\n") + vertex_source).c_str(), shadow_fragment_source, attributes, 3);
        if (!shadow_program_) {
            return false;
        }
        h_focus_ = gl::GetUniformLocation(shadow_program_, "u_focus");
        h_trig_ = gl::GetUniformLocation(shadow_program_, "u_trig");
        h_zoom_ = gl::GetUniformLocation(shadow_program_, "u_zoom");
        h_screen_ = gl::GetUniformLocation(shadow_program_, "u_screen");
        h_depth_ = gl::GetUniformLocation(shadow_program_, "u_depth");
        h_alpha_ = gl::GetUniformLocation(shadow_program_, "u_alpha");
        gl::UseProgram(shadow_program_);
        gl::Uniform1i(gl::GetUniformLocation(shadow_program_, "u_texture"), 0);
        water_program_ = gl::program(water_vertex_source, water_fragment_source, attributes, 3);
        if (!water_program_) {
            return false;
        }
        w_focus_ = gl::GetUniformLocation(water_program_, "u_focus");
        w_trig_ = gl::GetUniformLocation(water_program_, "u_trig");
        w_zoom_ = gl::GetUniformLocation(water_program_, "u_zoom");
        w_screen_ = gl::GetUniformLocation(water_program_, "u_screen");
        w_depth_ = gl::GetUniformLocation(water_program_, "u_depth");
        w_tint_ = gl::GetUniformLocation(water_program_, "u_tint");
        w_time_ = gl::GetUniformLocation(water_program_, "u_time");
        w_origin_ = gl::GetUniformLocation(water_program_, "u_origin");
        gl::UseProgram(water_program_);
        gl::Uniform1i(gl::GetUniformLocation(water_program_, "u_texture"), 0);
        {
            // 128 texels square, four cells: noises that meet themselves at the edges.
            constexpr int size = 128;
            std::vector<std::uint8_t> rgba(size * size * 4);
            const auto smooth = [&](int x, int y, int lattice, std::uint32_t salt) {
                const int step = size / lattice, ix = x / step, iy = y / step;
                float fx = float(x % step) / float(step), fy = float(y % step) / float(step);
                fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
                const auto at = [&](int i, int j) { return float(phd::hash3(i % lattice, j % lattice, salt) & 255) / 255.0f; };
                const float top = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * fx;
                const float bottom = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * fx;
                return top + (bottom - top) * fy;
            };
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    auto* p = &rgba[(std::size_t(y) * size + x) * 4];
                    p[0] = std::uint8_t(smooth(x, y, 16, 0x51a3u) * 255);
                    p[1] = std::uint8_t(phd::hash3(x, y, 0x77c1u) & 255);
                    p[2] = std::uint8_t(smooth(x, y, 4, 0x1f0bu) * 255);
                    p[3] = 255;
                }
            }
            ripples_ = make_texture(size, size, gl::RGBA, rgba.data());
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::GLint(gl::REPEAT));
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::GLint(gl::REPEAT));
        }
        s_focus_ = gl::GetUniformLocation(sprite_program_, "u_focus");
        s_trig_ = gl::GetUniformLocation(sprite_program_, "u_trig");
        s_zoom_ = gl::GetUniformLocation(sprite_program_, "u_zoom");
        s_screen_ = gl::GetUniformLocation(sprite_program_, "u_screen");
        s_depth_ = gl::GetUniformLocation(sprite_program_, "u_depth");
        s_tint_ = gl::GetUniformLocation(sprite_program_, "u_tint");
        s_wind_ = gl::GetUniformLocation(sprite_program_, "u_wind");
        s_texel_ = gl::GetUniformLocation(sprite_program_, "u_texel");
        s_view_ = gl::GetUniformLocation(sprite_program_, "u_view");
        gl::UseProgram(sprite_program_);
        gl::Uniform1i(gl::GetUniformLocation(sprite_program_, "u_texture"), 0);
        gl::GenBuffers(1, &scene_sprite_vbo_);
        atlas_ = make_texture(atlas_width, atlas_height, gl::RGBA, nullptr);
        reset_atlas();
    }

    width_ = world.width();
    height_ = world.height();
    corners_.assign(std::size_t(width_ + 1) * (height_ + 1), 0);
    water_.assign(corners_.size(), dry);
    ice_.assign(corners_.size(), 0);
    flow_x_.assign(corners_.size(), 0);
    flow_y_.assign(corners_.size(), 0);
    build_heights(world);
    drawn_ = corners_;
    for (int y = 0; y < height_; y += chunk_cells) {
        for (int x = 0; x < width_; x += chunk_cells) {
            Chunk chunk{x, y, std::min(chunk_cells, width_ - x), std::min(chunk_cells, height_ - y), 0, 0};
            gl::GenBuffers(1, &chunk.vbo);
            gl::GenBuffers(1, &chunk.ibo);
            gl::GenBuffers(1, &chunk.sprite_vbo);
            gl::GenBuffers(1, &chunk.shadow_vbo);
            gl::GenBuffers(1, &chunk.water_vbo);
            build_chunk(chunk);
            chunks_.push_back(chunk);
        }
    }
    return true;
}

void TiltView::build_heights(const phd::World& world) {
    heights_tick_ = world.tick();
    struct Cell {
        float bed, level; // metres: the ground, and the water's surface over it
        bool wet, frozen;
        float flow_x, flow_y; // toward the cell its water runs to: a river's at full speed, still water's slowly
    };
    const auto& downstream = world.terrain().downstream;
    const auto cell_at = [&](int x, int y) {
        x = std::clamp(x, 0, width_ - 1);
        y = std::clamp(y, 0, height_ - 1);
        const auto i = std::size_t(y) * width_ + x;
        const auto kind = world.water(i);
        const float bed = float(world.elevation(i)) / 10;
        const float level = kind == phd::Water::none    ? bed
                            : kind == phd::Water::ocean ? std::max(bed, 0.0f)
                                                        : bed + float(world.water_depth_dm(i)) / 10;
        float flow_x = 0, flow_y = 0;
        if (const int to = downstream[i]; to >= 0 && (kind == phd::Water::river || kind == phd::Water::lake)) {
            const float dx = float(to % width_ - x), dy = float(to / width_ - y);
            const float speed = (kind == phd::Water::river ? 1.0f : 0.2f) / std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            flow_x = dx * speed, flow_y = dy * speed;
        }
        return Cell{bed, level, kind != phd::Water::none,
                    world.temperature(i) < -20 && (kind != phd::Water::ocean || level - bed < 3), flow_x, flow_y};
    };
    for (int j = 0; j <= height_; ++j) {
        for (int i = 0; i <= width_; ++i) {
            const Cell around[4] = {cell_at(i - 1, j - 1), cell_at(i, j - 1), cell_at(i - 1, j), cell_at(i, j)};
            float metres = 0, level = 0, wet_bed = 0, flow_x = 0, flow_y = 0;
            int wet = 0, frozen = 0;
            for (const Cell& c : around) {
                // Without the painted study the water is part of the ground:
                // the mesh is its surface, with the water painted on.
                metres += (shoreline_study_ ? c.bed : c.level) / 4;
                if (c.wet) {
                    ++wet;
                    level += c.level;
                    wet_bed += c.bed;
                    frozen += c.frozen;
                    flow_x += c.flow_x;
                    flow_y += c.flow_y;
                }
            }
            float surface = dry;
            std::uint8_t ice = 0;
            if (shoreline_study_ && wet) {
                // A corner that touches water is no higher than the bed of
                // that water, so every wet cell is under its surface, even a
                // stream one cell wide between high banks.
                metres = std::min(metres, wet_bed / float(wet));
                surface = level / float(wet) * relief_;
                ice = frozen * 2 >= wet;
            }
            const auto at = std::size_t(j) * (width_ + 1) + i;
            flow_x_[at] = std::int8_t(wet ? flow_x / float(wet) * 127 : 0);
            flow_y_[at] = std::int8_t(wet ? flow_y / float(wet) * 127 : 0);
            const float now = metres * relief_;
            const bool ground_moved = corners_[at] != now, water_moved = water_[at] != surface || ice_[at] != ice;
            corners_[at] = now;
            water_[at] = surface;
            ice_[at] = ice;
            if (chunks_.empty() || (!ground_moved && !water_moved)) {
                continue;
            }
            // Its height, and the slopes of the corners beside it, are in
            // the meshes of the chunks around it.
            const int across = (width_ + chunk_cells - 1) / chunk_cells;
            const int x0 = std::max(i - 2, 0) / chunk_cells, x1 = std::min(i + 1, width_ - 1) / chunk_cells;
            const int y0 = std::max(j - 2, 0) / chunk_cells, y1 = std::min(j + 1, height_ - 1) / chunk_cells;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    Chunk& chunk = chunks_[std::size_t(y) * across + x];
                    chunk.stale = chunk.stale || ground_moved;
                    chunk.water_stale = true;
                }
            }
        }
    }
}

// The water's surface at a corner: where the corner touches water, that
// water's level; one corner further out (diagonals too, so every cell around
// a wet one has all four), the level beside it, so the surface carries on
// under the bank and the shore can fall anywhere in the next cell.
float TiltView::water_corner(int x, int y) const {
    const auto at = [&](int i, int j) {
        return i < 0 || j < 0 || i > width_ || j > height_ ? dry : water_[std::size_t(j) * (width_ + 1) + i];
    };
    if (at(x, y) != dry) {
        return at(x, y);
    }
    float sum = 0;
    int count = 0;
    for (const float beside : {at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1), at(x - 1, y - 1), at(x + 1, y - 1),
                               at(x - 1, y + 1), at(x + 1, y + 1)}) {
        if (beside != dry) {
            sum += beside;
            ++count;
        }
    }
    if (!count) {
        return dry;
    }
    // Never above the ground here: this corner touches no water, so the
    // shore falls short of it. (Water standing on a slope would otherwise
    // spill a cell downhill and end in a straight edge.)
    return std::min(sum / float(count), corners_[std::size_t(y) * (width_ + 1) + x] - 0.1f * relief_);
}

float TiltView::water_level(float x, float y) const {
    x = std::clamp(x, 0.0f, float(width_) - 0.001f);
    y = std::clamp(y, 0.0f, float(height_) - 0.001f);
    const int i = int(x), j = int(y);
    const float fx = x - float(i), fy = y - float(j);
    const float a = water_corner(i, j), b = water_corner(i + 1, j), c = water_corner(i, j + 1), d = water_corner(i + 1, j + 1);
    if (a == dry || b == dry || c == dry || d == dry) {
        return dry;
    }
    // The same split as the mesh's triangles.
    return fx >= fy ? a + (b - a) * fx + (d - b) * fy : a + (c - a) * fy + (d - c) * fx;
}

void TiltView::build_water(Chunk& chunk) {
    scratch_.clear();
    chunk.water_top = dry;
    const auto vertex = [&](int x, int y, float level) {
        const auto at = std::size_t(y) * (width_ + 1) + x;
        chunk.water_top = std::max(chunk.water_top, level);
        return Vertex{float(x), float(y), level, (level - corners_[at]) / relief_, float(ice_[at]),
                      Color{std::uint8_t(128 + flow_x_[at]), std::uint8_t(128 + flow_y_[at]), 0, 255}};
    };
    for (int j = 0; j < chunk.h; ++j) {
        for (int i = 0; i < chunk.w; ++i) {
            const int x = chunk.x0 + i, y = chunk.y0 + j;
            const float la = water_corner(x, y), lb = water_corner(x + 1, y), lc = water_corner(x, y + 1), ld = water_corner(x + 1, y + 1);
            if (la == dry || lb == dry || lc == dry || ld == dry) {
                continue;
            }
            const Vertex a = vertex(x, y, la), b = vertex(x + 1, y, lb), c = vertex(x, y + 1, lc), d = vertex(x + 1, y + 1, ld);
            if (std::max({a.u, b.u, c.u, d.u}) < -0.25f) {
                continue; // dry land well above the water beside it
            }
            scratch_.insert(scratch_.end(), {a, b, d, a, d, c});
        }
    }
    gl::BindBuffer(gl::ARRAY_BUFFER, chunk.water_vbo);
    gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(scratch_.size() * sizeof(Vertex)), scratch_.data(), gl::DYNAMIC_DRAW);
    chunk.water_vertices = int(scratch_.size());
    chunk.water_stale = false;
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
                chunk.plants_moved = true;
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
    if (heights_tick_ != world.tick()) {
        build_heights(world);
    }
    for (int k = 0; k < count && !chunks_.empty(); ++k) {
        Chunk& chunk = chunks_[std::size_t(next_++ % int(chunks_.size()))];
        if (chunk.stale) {
            build_chunk(chunk);
        }
    }
}

// One 4 x 4-cell patch, uploaded at both scales before the shader can see it.
void TiltView::paint_ground(const phd::World& world, int patch_x, int patch_y) {
    constexpr int size = patch_cells * art::ground::texels_per_cell;
    art::ground::paint(world, patch_x * size, patch_y * size, size, size, tile_rgba_.data(), size * 4, false);
    const int px = (patch_x % window_patches) * size, py = (patch_y % window_patches) * size;
    update_texture_region(ground_, px, py, size, size, gl::RGBA, tile_rgba_.data());
    for (int y = 0; y < size / 2; ++y) {
        for (int x = 0; x < size / 2; ++x) {
            const auto* in = &tile_rgba_[(std::size_t(2 * y) * size + 2 * x) * 4];
            auto* out = &tile_half_[(std::size_t(y) * (size / 2) + x) * 4];
            for (int c = 0; c < 3; ++c) {
                out[c] = std::uint8_t((in[c] + in[4 + c] + in[size * 4 + c] + in[size * 4 + 4 + c] + 2) / 4);
            }
            out[3] = 255;
        }
    }
    update_texture_region(ground_half_, px / 2, py / 2, size / 2, size / 2, gl::RGBA, tile_half_.data());
}

bool TiltView::update_ground(const phd::World& world, const Camera& camera, int screen_w, int view_h, bool capture) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    ++ground_frame_;
    // Keep the coverage bounds aligned to eight-cell tiles at both zoom levels.
    const auto first_patch = [](float focus) {
        return 2 * int(std::floor(focus / (2 * patch_cells))) - window_patches / 2;
    };
    const int first_x = first_patch(camera.cx), first_y = first_patch(camera.cy);
    const auto wrap = [](int i) { return (i % window_patches + window_patches) % window_patches; };
    const auto patch_at = [&](int x, int y) -> GroundPatch& {
        return ground_patches_[std::size_t(wrap(y)) * window_patches + wrap(x)];
    };
    bool mask_changed = ground_frame_ == 1 || window_x_ != first_x || window_y_ != first_y;
    window_x_ = first_x;
    window_y_ = first_y;

    float gx0, gy0, gx1, gy1;
    camera.to_ground(0, 0, float(screen_w), float(view_h), gx0, gy0);
    camera.to_ground(float(screen_w), float(view_h), float(screen_w), float(view_h), gx1, gy1);
    const auto tile_start = [](float cell) { return 2 * int(std::floor(cell / (2 * patch_cells))); };
    const int x0 = std::max({tile_start(gx0 - 1), first_x, 0});
    const int y0 = std::max({tile_start(gy0 - 4), first_y, 0});
    const int x1 = std::min({tile_start(gx1 + 1) + 1, first_x + window_patches - 1, (width_ - 1) / patch_cells});
    const int y1 = std::min({tile_start(gy1 + 4) + 1, first_y + window_patches - 1, (height_ - 1) / patch_cells});

    const auto signature = [&](int x, int y) {
        return art::ground::signature(world, x * patch_cells, y * patch_cells, patch_cells, patch_cells, false);
    };
    const auto fill = [&](int x, int y) {
        paint_ground(world, x, y);
        patch_at(x, y) = {x, y, signature(x, y), world.tick(), ground_frame_};
        mask_changed = true;
    };
    int missing = 0, nearest_x = 0, nearest_y = 0;
    float nearest = std::numeric_limits<float>::max();
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (patch_at(x, y).contains(x, y)) continue;
            if (capture) {
                fill(x, y);
                continue;
            }
            ++missing;
            const float dx = (x + 0.5f) * patch_cells - camera.cx;
            const float dy = (y + 0.5f) * patch_cells - camera.cy;
            const float distance = dx * dx + dy * dy;
            if (distance < nearest) {
                nearest = distance;
                nearest_x = x;
                nearest_y = y;
            }
        }
    }
    if (missing) fill(nearest_x, nearest_y);

    // Once present, patches only need checking after a tick. Repaint at most
    // one changed patch every other interactive frame. A capture drains them.
    if ((capture || (missing == 0 && (ground_frame_ & 1))) && x1 >= x0 && y1 >= y0) {
        const int across = x1 - x0 + 1, visible = across * (y1 - y0 + 1);
        for (int k = 0; k < visible; ++k) {
            const int at = (ground_cursor_ + k) % visible;
            const int x = x0 + at % across, y = y0 + at / across;
            auto& patch = patch_at(x, y);
            if (patch.checked_tick == world.tick() || (!capture && ground_frame_ - patch.painted_frame < 30)) {
                continue;
            }
            patch.checked_tick = world.tick();
            const auto current = signature(x, y);
            if (patch.signature != current) {
                paint_ground(world, x, y);
                patch.signature = current;
                patch.painted_frame = ground_frame_;
                if (!capture) {
                    ground_cursor_ = (at + 1) % visible;
                    break;
                }
            }
        }
    }

    // Only this 1 KiB map changes ownership on a jump. Unwritten or reused
    // texture memory never needs clearing and cannot appear at another bank.
    if (mask_changed) {
        std::array<std::uint32_t, window_patches * window_patches> valid;
        for (int y = 0; y < window_patches; ++y) {
            for (int x = 0; x < window_patches; ++x) {
                const int px = first_x + x, py = first_y + y;
                valid[std::size_t(y) * window_patches + x] = patch_at(px, py).contains(px, py) ? 0xffffffffu : 0;
            }
        }
        update_texture(ground_valid_, gl::RGBA, valid.data());
    }
    constexpr float span = float(patch_cells * window_patches);
    const float origin_x = std::floor(camera.cx / span) * span, origin_y = std::floor(camera.cy / span) * span;
    gl::Uniform4f(u_ground_window_, origin_x, origin_y,
                  (first_x * patch_cells - origin_x) / span, (first_y * patch_cells - origin_y) / span);
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, ground_valid_.id);
    gl::ActiveTexture(gl::TEXTURE0 + 1);
    gl::BindTexture(gl::TEXTURE_2D, camera.zoom >= 45 ? ground_.id : ground_half_.id);
    gl::ActiveTexture(gl::TEXTURE0);

    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    ground_ms_ = ground_ms_ * 0.95 + ms * 0.05;
    ground_ms_max_ = std::max(ground_ms_max_, ms);
    return missing <= 1;
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
        // Fire: flickering flames over burning cells. (The study paints its own: build_fire.)
        for (int cy = y0; cy <= (shoreline_study_ ? y0 - 1 : y1); ++cy) {
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
    if (!shoreline_study_ || !scene_plants_.sprites.empty()) {
        shadow(tx - sun.x * 0.6f, ty - sun.y * 0.6f, 0.7f);
    }
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
    painted_.clear();
    atlas_full_ = false;
    building_ = nullptr;
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
    // Once the atlas is full nothing more is painted until it is emptied:
    // painting a sprite only to find no room for it, for every new plant,
    // costs far more than the frame has.
    art::sprites::Image image;
    int x = 0, y = 0;
    bool placed = false;
    if (!atlas_full_) {
        image = art::sprites::paint(spec);
        // A clear texel around every sprite, so a swaying edge samples nothing.
        placed = allocate(image.width + 2, image.height + 2, x, y);
    }
    if (!placed) {
        // No room: stand in the nearest sprite of the same kind already
        // painted. The atlas is emptied and repainted later, not now.
        atlas_full_ = true;
        const Sprite* nearest = nullptr;
        int best = 1 << 30;
        for (const Sprite& entry : painted_) {
            const auto& other = entry.spec;
            if (other.kind != spec.kind || other.state != spec.state || other.form != spec.form || other.view != spec.view) {
                continue;
            }
            const int apart = std::abs(int(other.height) - int(spec.height)) * 4 + std::abs(int(other.radius) - int(spec.radius)) * 4 +
                              (std::abs(int(other.r) - int(spec.r)) + std::abs(int(other.g) - int(spec.g)) +
                               std::abs(int(other.b) - int(spec.b))) / 4 + (other.flower != spec.flower ? 8 : 0);
            if (apart < best) {
                best = apart;
                nearest = &entry;
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
    painted_.push_back(made);
    return &sprite_cache_.emplace(key, made).first->second;
}

// One sprite, hung from its spot. `lift` raises the spot above the ground, in
// cells, and `below` is how many of the sprite's rows hang beneath it. A
// trunk's top corners carry its true length and the shader shortens it.
void TiltView::add_sprite(PlantMesh& mesh, const Sprite& s, float x, float y, float z, float sway, std::uint32_t id, Stand stand,
                          float lift, int below, std::uint8_t view) {
    const float tpc = float(art::sprites::texels_per_cell);
    const float left = -float(s.anchor), right = float(s.w - s.anchor);
    float top = float(s.h - below), bottom = -float(below), top_v = s.v0;
    const auto beat = std::uint8_t(phd::hash3(int(id), 5, 0x9e3779b9u));
    const auto swing = std::uint8_t(std::clamp(sway * 16, 0.0f, 255.0f));
    Color high{swing, beat, 0, view}, low{std::uint8_t(stand == Stand::rigid ? swing : 0), beat, 0, view};
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
    mesh.sprites.insert(mesh.sprites.end(), {a, b, c, a, c, d});
    // How far it can reach up the screen, at the lowest the camera tilts.
    const float reach = stand == Stand::upright ? top * 0.72f : lift * tpc * 0.72f + top;
    mesh.tall = std::max(mesh.tall, int(reach) + 1);
    mesh.wide = std::max({mesh.wide, s.anchor, s.w - s.anchor});
}

// A tree in pieces, each at its own height, so they slide over one another
// as the camera tilts: a trunk, and a crown or a stack of boughs.
const TiltView::Sprite* TiltView::add_tree(PlantMesh& mesh, float x, float y, float h, float r, bool conifer, bool leafless, std::uint8_t cr,
                        std::uint8_t cg, std::uint8_t cb, std::uint32_t id, std::uint8_t form, bool pale_bark, float wade, float limbs) {
    using namespace art::sprites;
    // A tree standing in water shows from the surface up.
    const float tpc = float(texels_per_cell), z = ground(x, y) + wade;
    h = std::max(1.0f, h - wade);
    const float sway = h * 0.025f * tpc;
    const auto trunk = [&](float length, float girth, float swing) {
        Spec spec;
        spec.kind = Kind::trunk;
        // Painted as long as it shows at 45 degrees, the lowest tilt, and a little over.
        spec.height = std::uint16_t(std::min(1000, quantise(length * tpc * 0.72f, 4) + 10));
        spec.radius = std::uint16_t(girth + 0.5f);
        spec.state = std::uint8_t(pale_bark && !conifer ? pale : leafless ? bare : 0);
        if (const Sprite* s = sprite(spec)) {
            add_sprite(mesh, *s, x, y, z, swing, id, Stand::upright, length);
        }
    };
    Spec spec;
    spec.r = cr, spec.g = cg, spec.b = cb;
    const Sprite* caster = nullptr;
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
                add_sprite(mesh, *s, x, y + wide * 0.5f, z, sway * (up + step) / h, id, Stand::rigid, up);
                caster = caster ? caster : s; // the lowest and widest tier
            }
        }
        return caster;
    }
    // A ball of leaves with its top at the tree's height, clear of the ground.
    // `limbs` is how much of its crown's reach is left: all of it, or a snag's stubs.
    const float lift = std::max(h - r * limbs, r * limbs * 1.4f);
    trunk(lift, std::clamp(r * tpc * 0.2f, 2.0f, 9.0f), sway);
    spec.kind = Kind::crown;
    spec.radius = std::uint16_t(std::min(500, quantise(r * limbs * tpc, 2)));
    spec.state = std::uint8_t((leafless ? bare : 0) | (pale_bark ? pale : 0) | (limbs < 1 ? dead : 0));
    spec.variant = std::uint8_t(id % 3);
    spec.form = form;
    if (leafless) {
        // Bare limbs are painted for each pitch, and the shader shows the one
        // the camera is at. The one from straight above is also the shadow.
        spec.variant = std::uint8_t(limbs < 1 ? id % 4 : id % 2);
        spec.form = std::uint8_t(form == std::uint8_t(Form::columnar) ? form : 0);
        for (int view = 0; view < views; ++view) {
            spec.view = std::uint8_t(view);
            if (const Sprite* s = sprite(spec)) {
                add_sprite(mesh, *s, x, y, z, sway, id, Stand::rigid, lift, s->anchor_y, std::uint8_t(view + 1));
                caster = s;
            }
        }
        return caster;
    }
    if (const Sprite* s = sprite(spec)) {
        add_sprite(mesh, *s, x, y, z, sway, id, Stand::rigid, lift, s->anchor_y);
        caster = s;
    }
    return caster;
}

// Append a run of cells in the same row-major order as a whole chunk.
void TiltView::build_plant_row(const phd::World& world, int cy, int x0, int x1) {
    using namespace art::sprites;
    const auto unit = [](std::uint32_t bits) { return float(bits & 255) / 255.0f; };
    const float tpc = float(texels_per_cell);
    // A shadow on the ground: the plant's own sprite laid flat about (x, y),
    // each of its rows covering `row` cells of ground, in a few pieces so it
    // follows a slope. Its columns sit on the ground's texels.
    // A sprite that stands on the ground ends in a flat foot, so its shadow
    // is `doubled`: the outline, then the same again upside down beneath it.
    const auto shadow = [&](const Sprite& s, float x, float y, float row, bool doubled = false) {
        const float w = float(s.w) / tpc, h = float(s.h) * row * (doubled ? 2.0f : 1.0f);
        const float left = std::floor((x - w / 2) * tpc + 0.5f) / tpc, top = y - h / 2;
        const int nx = std::clamp(int(w) + 1, 1, 8), ny = doubled ? 2 : std::clamp(int(h) + 1, 1, 8);
        const auto corner = [&](int i, int j) {
            const float fx = float(i) / float(nx), fy = float(j) / float(ny);
            const float px = left + w * fx, py = top + h * fy;
            const float down = doubled ? 1 - std::fabs(1 - 2 * fy) : fy;
            return Vertex{px, py, ground(px, py) + 0.05f, s.u0 + (s.u1 - s.u0) * fx, s.v0 + (s.v1 - s.v0) * down,
                          Color{255, 255, 255, 255}};
        };
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const Vertex a = corner(i, j), b = corner(i + 1, j), c = corner(i + 1, j + 1), d = corner(i, j + 1);
                plant_mesh_.shadows.insert(plant_mesh_.shadows.end(), {a, b, c, a, c, d});
            }
        }
    };
    // A sprite drawn for the three-quarter camera shows a round crown this
    // much shorter than it is wide; laid flat, its rows cover that much more.
    const float lying = 1 / (tpc * lie);
    // A plant's sprite takes the palette's colour for it, the one the ground
    // and the bird's-eye view use, in steps so similar plants share a sprite.
    const auto pigment = [&](Spec& spec, const phd::Plant& plant) {
        const auto c = art::palette::leaf(plant, world);
        const auto step = [](float v) { return std::uint8_t((int(std::clamp(v, 0.0f, 255.0f)) & 0xe0) | 0x10); };
        spec.r = step(c.r), spec.g = step(c.g), spec.b = step(c.b);
    };
    for (int cx = x0; cx < x1; ++cx) {
        const auto cell = std::size_t(cy) * width_ + cx;
        const bool wet = world.water(cell) != phd::Water::none;
        if (const auto t = art::tree_in(world, cx, cy); t.radius > 0) {
            ++plant_mesh_.trees;
            const float r = t.radius;
            const Sprite* caster = add_tree(plant_mesh_, t.x, t.y, std::max(1.2f, t.height), r, t.conifer, t.bare,
                                            (t.r & 0xe0) | 0x10, (t.g & 0xe0) | 0x10, (t.b & 0xe0) | 0x10, t.id, t.form, t.pale,
                                            wading(t.x, t.y), t.snag ? 0.4f : 1.0f);
            if (caster && !wet) {
                shadow(*caster, t.x - plant_sun_.x * t.reach() * 0.8f, t.y - plant_sun_.y * t.reach() * 0.8f, t.bare ? 1 / tpc : lying);
            }
            if (!t.snag) {
                continue; // a living tree has the cell to itself; under a snag the rest grows on
            }
        }
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
                // What stands clear of the water, from its surface up.
                const float wade = wading(x, y), hh = float(p->height) / float(phd::cell_decimetres) - wade;
                if (hh < 0.25f) {
                    continue;
                }
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
                pigment(spec, *p);
                spec.state = leafless ? bare : 0;
                const Sprite* s = sprite(spec);
                if (s) {
                    // Hung from its middle, so it settles over its spot as the camera rises.
                    const float lift = hh * 0.45f;
                    add_sprite(plant_mesh_, *s, x, y, ground(x, y) + wade, hh * 0.03f * tpc, p->id, Stand::rooted, lift,
                               int((front * lie + lift * rise) * tpc + 0.5f));
                }
                if (s && !leafless && !wet && hh > 0.4f) {
                    // A bush's sprite stands up; its shadow is the same outline, shorter.
                    if (cone) {
                        shadow(*s, x - plant_sun_.x * reach * 0.5f, y + front * 0.6f - plant_sun_.y * reach * 0.5f, lying * 0.25f, true);
                    } else {
                        shadow(*s, x - plant_sun_.x * reach * 0.5f, y + front * 0.5f - plant_sun_.y * reach * 0.5f, lying * 0.6f);
                    }
                }
                continue;
            }
            // Soft plants: grass, herbs and reeds, as tall as what stands clear of the water.
            const float x = float(cx) + 0.25f + 0.5f * float(slot & 1) + (unit(place) - 0.5f) * 0.4f;
            const float y = float(cy) + 0.25f + 0.5f * float(slot >> 1) + (unit(place >> 8) - 0.5f) * 0.4f;
            const float tall = float(p->height) / float(phd::cell_decimetres);
            const float wade = wading(x, y), height = tall - wade;
            if (height < 0.07f && wade > 0) {
                // Under the water: it stands on the bed, and the water is
                // drawn over it. One made for water that reaches most of the
                // way up floats its leaves on the surface.
                pigment(spec, *p);
                spec.kind = Kind::waterweed;
                spec.height = std::uint16_t(std::max(3, quantise(tall * tpc * rise)));
                spec.radius = std::uint16_t(std::clamp(int(float(spec.height) * 0.3f), 2, 5));
                spec.state = dormant ? bare : 0;
                if (const Sprite* s = sprite(spec)) {
                    add_sprite(plant_mesh_, *s, x, y, ground(x, y), 1.0f, p->id);
                }
                if (genes[phd::gene_flood] > 155 && tall > wade * 0.5f && !dormant) {
                    const bool flowers = p->flags & phd::plant_flowering;
                    spec.kind = Kind::pad;
                    spec.height = 0;
                    spec.radius = std::uint16_t(std::clamp(int(3 + tall * 3), 3, 6));
                    spec.state = flowers ? flowering : 0;
                    spec.flower = flowers ? std::uint8_t(genes[phd::gene_flower] * 6 / 256) : 0;
                    if (const Sprite* s = sprite(spec)) {
                        add_sprite(plant_mesh_, *s, x, y, ground(x, y) + wade, 0, p->id, Stand::rigid, 0, s->anchor_y);
                    }
                }
                continue;
            }
            if (height < 0.07f) {
                continue;
            }
            const bool reed = genes[phd::gene_flood] > 155;
            const bool flowers = (p->flags & phd::plant_flowering) && !dormant;
            spec.kind = reed ? Kind::reed : Kind::grass;
            spec.height = std::uint16_t(std::max(3, quantise(height * tpc * rise)));
            spec.radius = std::uint16_t(std::clamp(int(float(spec.height) * (reed ? 0.2f : 0.35f)), 2, reed ? 4 : 6));
            pigment(spec, *p);
            spec.state = std::uint8_t((dormant ? bare : 0) | (flowers ? flowering : 0));
            spec.flower = flowers ? std::uint8_t(genes[phd::gene_flower] * 6 / 256) : 0;
            const Sprite* s = sprite(spec);
            if (s) {
                add_sprite(plant_mesh_, *s, x, y, ground(x, y) + wade, std::max(1.0f, height * 0.06f * tpc), p->id);
            }
        }
    }
}

bool TiltView::update_plants(const phd::World& world, const Camera& camera, bool capture) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::microseconds(1500);
    if (building_ && (std::find(plant_chunks_.begin(), plant_chunks_.end(), building_) == plant_chunks_.end() ||
                      (capture && plant_start_tick_ != world.tick()))) {
        building_->plants_moved = true;
        building_ = nullptr;
    }
    const auto priority = [&](const Chunk& chunk) {
        const bool has_mesh = chunk.built_atlas == atlas_generation_;
        const float dx = chunk.x0 + chunk.w * 0.5f - camera.cx, dy = chunk.y0 + chunk.h * 0.5f - camera.cy;
        return std::make_tuple(has_mesh, has_mesh ? chunk.built_tick : 0, dx * dx + dy * dy);
    };
    do {
        if (!building_) {
            for (Chunk* chunk : plant_chunks_) {
                if (chunk->built_atlas == atlas_generation_ && chunk->built_tick == world.tick() && !chunk->plants_moved) {
                    continue;
                }
                if (!building_ || priority(*chunk) < priority(*building_)) {
                    building_ = chunk;
                }
            }
            if (!building_) return true;
            plant_mesh_.clear();
            next_plant_cell_ = 0;
            plant_start_tick_ = world.tick();
            plant_sun_ = art::sun_for(world);
            building_->plants_moved = false;
        }

        Chunk& chunk = *building_;
        const int x = next_plant_cell_ % chunk.w, y = next_plant_cell_ / chunk.w;
        const int cells = std::min(4, chunk.w - x);
        build_plant_row(world, chunk.y0 + y, chunk.x0 + x, chunk.x0 + x + cells);
        next_plant_cell_ += cells;
        if (next_plant_cell_ == chunk.w * chunk.h) {
            // Keep the last complete buffers on screen until both replacements
            // are ready. If the world ticked mid-build, this mesh stays due.
            gl::BindBuffer(gl::ARRAY_BUFFER, chunk.sprite_vbo);
            gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(plant_mesh_.sprites.size() * sizeof(SpriteVertex)),
                           plant_mesh_.sprites.data(), gl::DYNAMIC_DRAW);
            gl::BindBuffer(gl::ARRAY_BUFFER, chunk.shadow_vbo);
            gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(plant_mesh_.shadows.size() * sizeof(Vertex)),
                           plant_mesh_.shadows.data(), gl::DYNAMIC_DRAW);
            chunk.sprite_vertices = int(plant_mesh_.sprites.size());
            chunk.shadow_vertices = int(plant_mesh_.shadows.size());
            chunk.trees = plant_mesh_.trees;
            chunk.tall = plant_mesh_.tall;
            chunk.wide = plant_mesh_.wide;
            chunk.built_tick = plant_start_tick_;
            chunk.built_atlas = atlas_generation_;
            building_ = nullptr;
        }
    } while (capture || Clock::now() < deadline);

    return std::all_of(plant_chunks_.begin(), plant_chunks_.end(), [&](const Chunk* chunk) {
        return chunk->built_atlas == atlas_generation_;
    });
}

// Fire, close up: tongues of flame flickering over each burning cell and up
// in the crown of a burning tree, smoke rising from it and leaning with the
// wind, and a thin smoke over ground that has just burnt. They move every
// frame, so they are made every frame, for the cells in view.
void TiltView::build_fire(const phd::World& world, const Camera& camera, int screen_w, int view_h, double time) {
    using namespace art::sprites;
    fire_mesh_.clear();
    smoke_mesh_.clear();
    float gx0, gy0, gx1, gy1;
    camera.to_ground(0, 0, float(screen_w), float(view_h), gx0, gy0);
    camera.to_ground(float(screen_w), float(view_h), float(screen_w), float(view_h), gx1, gy1);
    const int x0 = std::max(0, int(gx0) - 3), x1 = std::min(width_ - 1, int(gx1) + 3);
    const int y0 = std::max(0, int(gy0) - 3), y1 = std::min(height_ - 1, int(gy1) + 14); // smoke rises into view from the south
    const float tpc = float(texels_per_cell);
    const float wind = float(world.wind_speed()) / 96.0f;
    const float wind_x = std::cos(float(world.wind_angle()) * 6.2831853f / 256.0f) * wind;
    const float wind_y = std::sin(float(world.wind_angle()) * 6.2831853f / 256.0f) * wind;
    const auto unit = [](std::uint32_t bits) { return float(bits & 255) / 255.0f; };
    const int flicker = int(time * 9);
    const auto smoke = [&](float x, float y, std::uint32_t h, float pace, int least_thin) {
        const float age = float(std::fmod(time * double(pace) + double(unit(h >> 8)), 1.0));
        Spec spec;
        spec.kind = Kind::smoke;
        spec.radius = std::uint16_t(quantise(4 + age * 8, 2));
        spec.variant = std::uint8_t(std::max(least_thin, age < 0.4f ? 0 : age < 0.75f ? 1 : 2));
        if (const Sprite* s = sprite(spec)) {
            const float sx = x + wind_x * age * 3, sy = y + wind_y * age * 1.5f;
            add_sprite(smoke_mesh_, *s, sx, sy, ground(sx, sy), 0, h, Stand::rigid, 0.8f + age * 6, s->anchor_y);
        }
    };
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            const auto cell = std::size_t(cy) * width_ + cx;
            const int burning = world.fire(cell);
            if (!burning) {
                const auto h = phd::hash3(cx, cy, 0x50c1u);
                if (world.burnt(cell) > 225 && (h >> 20 & 3) == 0) {
                    smoke(float(cx) + 0.2f + unit(h) * 0.6f, float(cy) + 0.5f, h, 0.2f, 2); // still smouldering
                }
                continue;
            }
            Spec spec;
            spec.kind = Kind::flame;
            for (int k = 0; k < 3; ++k) {
                const auto h = phd::hash3(cx * 3 + k, cy, 0x5f1eu);
                const float x = float(cx) + 0.15f + unit(h) * 0.7f, y = float(cy) + 0.15f + unit(h >> 8) * 0.7f;
                const float tall = (0.5f + unit(h >> 16) * 0.9f) * float(burning) / 8.0f;
                spec.height = std::uint16_t(std::clamp(quantise(tall * tpc * rise, 4), 6, 48));
                spec.variant = std::uint8_t((int(h >> 24) + flicker) & 3);
                if (const Sprite* s = sprite(spec)) {
                    add_sprite(fire_mesh_, *s, x, y, ground(x, y), 0, h);
                }
                if (k < 2) {
                    smoke(x, y, h, 0.3f, 0);
                }
            }
            if (const phd::Plant* tree = world.tree(cell)) {
                // The crown is alight too.
                const auto h = phd::hash3(cx, cy, 0x7ee5u);
                const float x = float(cx) + 0.5f, y = float(cy) + 0.5f;
                spec.height = 40;
                spec.variant = std::uint8_t((int(h >> 24) + flicker) & 3);
                if (const Sprite* s = sprite(spec)) {
                    add_sprite(fire_mesh_, *s, x, y, ground(x, y), 0, h, Stand::rigid, float(tree->height) / float(phd::cell_decimetres) * 0.6f);
                }
            }
        }
    }
}

void TiltView::draw_fire() {
    if (!shoreline_study_ || fire_mesh_.sprites.empty()) {
        return;
    }
    // The sprite program still has this frame's camera from draw_plants.
    // The caller's light may have left its own blend set.
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(gl::LEQUAL);
    gl::DepthMask(1);
    gl::UseProgram(sprite_program_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, atlas_.id);
    gl::BindBuffer(gl::ARRAY_BUFFER, scene_sprite_vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(fire_mesh_.sprites.size() * sizeof(SpriteVertex)), fire_mesh_.sprites.data(),
                   gl::STREAM_DRAW);
    const auto stride = gl::GLsizei(sizeof(SpriteVertex));
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, x)));
    gl::VertexAttribPointer(1, 4, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, ox)));
    gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, sway)));
    gl::DrawArrays(gl::TRIANGLES, 0, gl::GLsizei(fire_mesh_.sprites.size()));
    ++draw_calls_;
    triangles_ += int(fire_mesh_.sprites.size()) / 3;
    sprites_drawn_ += int(fire_mesh_.sprites.size()) / 6;
    gl::Disable(gl::DEPTH_TEST);
}

bool TiltView::draw_plants(const phd::World& world, const Camera& camera, const Font& font, int screen_w, int screen_h,
                           int view_h, double time, bool capture) {
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
    // A full atlas is emptied now and then, so sprites nothing uses any more
    // (last season's colours, plants that have grown) make room.
    if (atlas_full_ && ++frames_full_ > 600) {
        frames_full_ = 0;
        reset_atlas();
    }
    const bool ready = update_plants(world, camera, capture);

    // Give the world plants their atlas slots before inserting the test tree,
    // just as in a full capture. Once painted it can draw during later pans.
    scene_plants_.clear();
    if (scene_ && (ready || scene_tree_atlas_ == atlas_generation_)) {
        add_tree(scene_plants_, scene_x_, scene_y_, 2.7f, 0.81f, false, false, 88, 136, 56, 7342u);
        scene_tree_atlas_ = atlas_generation_;
    }

    // Shadows first, on the ground: each plant's outline from the atlas.
    const float reach = float(view_h) / (2 * camera.px() * camera.sin_pitch()) + 40;
    const float depth_scale = 1.0f / (reach * camera.cos_pitch() + (3000 * relief_ + 4) * camera.sin_pitch() + 2);
    gl::UseProgram(shadow_program_);
    gl::Uniform3f(h_focus_, camera.cx, camera.cy, camera.focus_z);
    gl::Uniform2f(h_trig_, camera.sin_pitch(), camera.cos_pitch());
    gl::Uniform1f(h_zoom_, camera.px());
    gl::Uniform4f(h_screen_, float(screen_w), float(screen_h), float(screen_w) / 2, float(view_h) / 2);
    gl::Uniform1f(h_depth_, depth_scale);
    gl::Uniform1f(h_alpha_, target_alpha_ ? 1.0f : 1.0f - shadow_light);
    gl::BindTexture(gl::TEXTURE_2D, atlas_.id);
    gl::DepthMask(0);
    if (target_alpha_) {
        gl::ColorMask(0, 0, 0, 1);
        gl::ClearColor(0, 0, 0, shadow_light);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        gl::ColorMask(1, 1, 1, 1);
        gl::BlendFuncSeparate(gl::ZERO, gl::DST_ALPHA, gl::ONE, gl::ZERO);
    }
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::EnableVertexAttribArray(2);
    for (const Chunk* chunk : plant_chunks_) {
        if (chunk->built_atlas != atlas_generation_ || !chunk->shadow_vertices) {
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
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::DepthMask(1);

    gl::UseProgram(sprite_program_);
    gl::Uniform3f(s_focus_, camera.cx, camera.cy, camera.focus_z);
    gl::Uniform2f(s_trig_, camera.sin_pitch(), camera.cos_pitch());
    gl::Uniform1f(s_zoom_, camera.px());
    gl::Uniform4f(s_screen_, float(screen_w), float(screen_h), float(screen_w) / 2, float(view_h) / 2);
    gl::Uniform1f(s_depth_, depth_scale);
    std::uint8_t tint[3];
    art::day_tint(world, tint);
    gl::Uniform4f(s_tint_, float(tint[0]) / 255, float(tint[1]) / 255, float(tint[2]) / 255, 1);
    // Plants lean with the wind's push across the screen, and rock on their own beats.
    const float gust = float(world.wind_speed()) / 96.0f;
    const float across = std::cos(float(world.wind_angle()) * 6.2831853f / 256.0f);
    gl::Uniform2f(s_wind_, gust * (across < 0 ? std::min(across, -0.35f) : std::max(across, 0.35f)),
                  float(std::fmod(time * 1.7, 6.283185307179586)));
    gl::Uniform1f(s_texel_, 1.0f / float(atlas_height));
    gl::Uniform1f(s_view_, float(1 + art::sprites::view_for(std::asin(std::min(1.0f, camera.sin_pitch())) * 57.29578f)));
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
        if (chunk->built_atlas != atlas_generation_) continue;
        draw_sprites(chunk->sprite_vbo, chunk->sprite_vertices);
        trees_ += chunk->trees;
    }
    if (!scene_plants_.sprites.empty()) {
        gl::BindBuffer(gl::ARRAY_BUFFER, scene_sprite_vbo_);
        gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(scene_plants_.sprites.size() * sizeof(SpriteVertex)),
                       scene_plants_.sprites.data(), gl::STREAM_DRAW);
        draw_sprites(scene_sprite_vbo_, int(scene_plants_.sprites.size()));
    }
    build_fire(world, camera, screen_w, view_h, time);
    if (!smoke_mesh_.sprites.empty()) {
        gl::BindBuffer(gl::ARRAY_BUFFER, scene_sprite_vbo_);
        gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(smoke_mesh_.sprites.size() * sizeof(SpriteVertex)),
                       smoke_mesh_.sprites.data(), gl::STREAM_DRAW);
        draw_sprites(scene_sprite_vbo_, int(smoke_mesh_.sprites.size()));
    }
    gl::UseProgram(program_);
    return ready;
}

// The water, last and without writing depth: over the bed, the shadows on
// it and whatever wades, and behind everything that stands clear of it.
void TiltView::draw_water(const phd::World& world, const Camera& camera, int screen_w, int screen_h, int view_h, double time) {
    gl::UseProgram(water_program_);
    gl::Uniform3f(w_focus_, camera.cx, camera.cy, camera.focus_z);
    gl::Uniform2f(w_trig_, camera.sin_pitch(), camera.cos_pitch());
    gl::Uniform1f(w_zoom_, camera.px());
    gl::Uniform4f(w_screen_, float(screen_w), float(screen_h), float(screen_w) / 2, float(view_h) / 2);
    const float reach = float(view_h) / (2 * camera.px() * camera.sin_pitch()) + 40;
    gl::Uniform1f(w_depth_, 1.0f / (reach * camera.cos_pitch() + (3000 * relief_ + 4) * camera.sin_pitch() + 2));
    std::uint8_t tint[3];
    art::day_tint(world, tint);
    gl::Uniform4f(w_tint_, float(tint[0]) / 255, float(tint[1]) / 255, float(tint[2]) / 255, 1);
    gl::Uniform1f(w_time_, float(std::fmod(time, 3600.0)));
    gl::Uniform2f(w_origin_, std::floor(camera.cx / 4) * 4 - 64, std::floor(camera.cy / 4) * 4 - 64);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, ripples_.id);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::DepthMask(0);
    const auto stride = gl::GLsizei(sizeof(Vertex));
    for (const Chunk* chunk : visible_) {
        if (!chunk->water_vertices) {
            continue;
        }
        gl::BindBuffer(gl::ARRAY_BUFFER, chunk->water_vbo);
        gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, x)));
        gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, u)));
        gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride, reinterpret_cast<const void*>(offsetof(Vertex, color)));
        gl::DrawArrays(gl::TRIANGLES, 0, chunk->water_vertices);
        ++draw_calls_;
        triangles_ += chunk->water_vertices / 3;
    }
    gl::DepthMask(1);
    gl::UseProgram(program_);
}

bool TiltView::draw(const phd::World& world, const Camera& camera, const Texture& terrain, const Font& font,
                    int screen_w, int screen_h, int view_h, double time, bool capture) {
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
    const bool ground_ready = !shoreline_study_ || update_ground(world, camera, screen_w, view_h, capture);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, terrain.id);
    int caught_up = 0;
    visible_.clear();
    for (auto& chunk : chunks_) {
        // Cull chunks whose box can't reach the view.
        float sx0, sy0, sx1, sy1;
        camera.to_screen(float(chunk.x0), float(chunk.y0), std::max(chunk.zmax, chunk.water_top), float(screen_w), float(view_h), sx0, sy0);
        camera.to_screen(float(chunk.x0 + chunk.w), float(chunk.y0 + chunk.h), chunk.zmin, float(screen_w),
                         float(view_h), sx1, sy1);
        if (sx1 < 0 || sx0 > float(screen_w) || sy1 < 0 || sy0 > float(view_h)) {
            continue;
        }
        // What is on screen follows the water at once, a few chunks a frame.
        if (chunk.stale && (capture || caught_up < 8)) {
            ++caught_up;
            build_chunk(chunk);
        }
        if (shoreline_study_) {
            if (chunk.water_stale && !chunk.stale) {
                build_water(chunk);
            }
            visible_.push_back(&chunk);
        }
        gl::BindBuffer(gl::ARRAY_BUFFER, chunk.vbo);
        gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, chunk.ibo);
        attributes();
        gl::DrawElements(gl::TRIANGLES, chunk.indices, gl::UNSIGNED_SHORT, nullptr);
        ++draw_calls_;
        triangles_ += chunk.indices / 3;
    }

    const bool plants_ready = !shoreline_study_ || draw_plants(world, camera, font, screen_w, screen_h, view_h, time, capture);
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
    if (shoreline_study_) {
        draw_water(world, camera, screen_w, screen_h, view_h, time);
    }
    gl::Disable(gl::DEPTH_TEST);
    return ground_ready && plants_ready;
}

} // namespace gfx
