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

    // Small painted sprays on fixed, angled planes within a three-dimensional crown.
    // Opaque runs leave windows between leaves, and share the scene depth buffer.
    const auto leaf_spray = [&](V3 centre, float radius_x, float radius_y, Color pigment, unsigned seed,
                                float outer_light, bool needles) {
        // clang-format off
        static constexpr char broad[][17] = {
            "       33       ",
            "    233443  33  ",
            "   233443323433 ",
            " 2234433222332  ",
            " 1223332233322  ",
            "112321224433221 ",
            " 012122333222133",
            "012222312212232 ",
            " 0112332112221  ",
            "  011211221121  ",
            "   0011011  1   ",
            "    01          ",
        };
        static constexpr char pine[][17] = {
            "       3        ",
            "      343       ",
            "    23333       ",
            "   13333232     ",
            "     2232333    ",
            "   2333232223   ",
            "  233222332223  ",
            "13212223332212  ",
            " 11222231222122 ",
            "  01121112211123",
            " 011 1001101  1 ",
            "  0   0    0    ",
        };
        // clang-format on
        const auto byte = [](float x) { return std::uint8_t(std::clamp(x, 0.f, 255.f)); };

        const Color tones[] = {
            {byte(pigment.r * .29f + 8), byte(pigment.g * .35f + 12), byte(pigment.b * .44f + 13), 255},
            {byte(pigment.r * .42f + 12), byte(pigment.g * .49f + 19), byte(pigment.b * .43f + 12), 255},
            {byte((pigment.r * .60f + 24) * outer_light), byte((pigment.g * .70f + 23) * outer_light),
             byte((pigment.b * .46f + 11) * outer_light), 255},
            {byte((pigment.r * .88f + 37) * outer_light), byte((pigment.g * .86f + 39) * outer_light),
             byte((pigment.b * .49f + 9) * outer_light), 255},
            {byte((pigment.r * .92f + 63) * outer_light), byte((pigment.g * .88f + 55) * outer_light),
             byte((pigment.b * .50f + 16) * outer_light), 255},
        };
        const float flip = (seed & 1) ? 1.f : -1.f;
        const float tilt = (float(seed % 9) - 4.f) * 0.019f;
        const auto pos = [&](int column, int row) {
            float u = (float(column) / 8.f - 1.f) * flip, v = 1.f - float(row) / 6.f;
            return centre + V3{u * radius_x + tilt * v * radius_y, -v * radius_y * .70f + u * radius_x * tilt,
                               v * radius_y * .72f};
        };
        for (int row = 0; row < 12; ++row) {
            const char* pixels = needles ? pine[row] : broad[row];
            for (int x = 0; x < 16;) {
                const int start = x++;
                while (x < 16 && pixels[x] == pixels[start]) {
                    ++x;
                }
                if (pixels[start] == ' ') {
                    continue;
                }
                const Color tone = tones[pixels[start] - '0'];
                const V3 a = pos(start, row), b = pos(x, row), c = pos(x, row + 1), d = pos(start, row + 1);
                tri(a, b, c, tone);
                tri(a, c, d, tone);
            }
        }
    };
    const auto painted_crown = [&](float x, float y, float ground_height, float height, float radius, Color leaf,
                                   unsigned id, bool conifer, bool bare, float wx, float wy, bool shrub) {
        const float phase = float(phd::hash3(int(id), 11, 813u) & 1023) * (6.2831853f / 1024.f);
        const Color bark{112, 82, 57, 255};
        const float width = shrub ? std::max(.024f, radius * .18f) : std::max(.075f, radius * .20f);
        const V3 root{x, y, ground_height - .035f},
            fork{x - radius * .08f, y + radius * .025f, ground_height + height * (shrub ? .21f : .33f)};
        beam(root, fork, width, bark);
        const int branch_count = shrub ? 5 : conifer ? 10 : 7;
        const float reach = shrub ? std::min(.58f, radius + .16f * height) : radius;
        for (int k = 0; k < branch_count; ++k) {
            const auto shape_seed = phd::hash3(k, int(id), 7153u);
            const float t = float(k) / float(branch_count - 1), a = phase + float(k) * 2.399963f;
            const V3 side{std::cos(a), std::sin(a), 0};
            const float canopy_z = shrub ? (.22f + t * .61f) : conifer ? (.35f + t * .57f) : (.51f + t * .37f);
            const float branch_reach = reach * (conifer ? (.68f - t * .59f) : (.64f - .53f * t));
            const float size = reach * (conifer ? (.70f - .38f * t) : (.56f + .09f * float(shape_seed & 255) / 255));
            const V3 end{x + side.x * branch_reach + wx * canopy_z, y + side.y * branch_reach + wy * canopy_z,
                         ground_height + height * canopy_z};
            const V3 shoulder = fork + (end - fork) * .53f +
                                V3{side.x * branch_reach * .17f, side.y * branch_reach * .17f, -height * .075f};
            beam(fork, shoulder, width * (shrub ? .48f : .55f), bark);
            beam(shoulder, end, width * (shrub ? .31f : .33f), bark);
            if (bare) {
                continue;
            }
            const float warm = (conifer ? .85f : .94f) + t * .12f;
            const float lobe_height = shrub ? std::max(size * .81f, height * .19f)
                                            : std::max(size * .74f, height * (conifer ? .095f : .083f));
            leaf_spray(end, size, lobe_height, leaf, shape_seed, warm, conifer);
            if (!shrub && size > .34f) {
                const float angle = a + .85f;
                const V3 offset{std::cos(angle) * size * .57f, std::sin(angle) * size * .57f, -lobe_height * .26f};
                leaf_spray(end + offset, size * .64f, lobe_height * .70f, leaf, shape_seed >> 3, warm * .96f, conifer);
            }
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
                const float g = ground(t.x, t.y), r = t.radius, h = std::max(1.2f, t.height);
                const Color leaves{t.r, t.g, t.b, 255}, bark{96, 72, 50, 255};
                // Sway: the crown leans with the wind and rocks, more for tall
                // trees and strong wind, each tree on its own beat.
                const float gust = float(world.wind_speed()) / 96.0f;
                const float rock = std::sin(float(time) * 1.7f + float(t.id % 97)) * 0.5f + 0.5f;
                const float sway = gust * (0.3f + 0.7f * rock) * h * 0.025f;
                const float wx = std::cos(float(world.wind_angle()) * 6.2831853f / 256.0f) * sway;
                const float wy = std::sin(float(world.wind_angle()) * 6.2831853f / 256.0f) * sway;
                if (shoreline_study_) {
                    if (!t.bare) {
                        shadow(t.x - sun.x * r * .8f, t.y - sun.y * r * .8f, r * .86f);
                    }
                    painted_crown(t.x, t.y, g, h, r, leaves, t.id, t.conifer, t.bare, wx, wy, false);
                    continue;
                }
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
        // Each tuft grows from the position and height of a simulated soft plant.
        if (shoreline_study_) {
            const auto tuft = [&](const phd::Plant& p, int slot, int cx, int cy, int depth_dm) {
                const float height = float(int(p.height) - depth_dm) / float(phd::cell_decimetres);
                if (height < 0.07f) {
                    return;
                }
                const auto hash = [&](int k) { return phd::hash3(k, int(p.id), 0x841d5a73u); };
                const auto unit = [](std::uint32_t bits) { return float(bits & 255) / 255.f; };
                const auto s = hash(0);
                const float x = float(cx) + 0.25f + 0.5f * float(slot & 1) + (unit(s) - 0.5f) * 0.23f;
                const float y = float(cy) + 0.25f + 0.5f * float(slot >> 1) + (unit(s >> 8) - 0.5f) * 0.23f;
                const float heading = unit(s >> 16) * 6.2831853f;
                const auto& genes = p.genome.gene;
                const bool reed = genes[phd::gene_flood] > 155;
                const bool dormant = p.flags & phd::plant_dormant;
                const bool flowering = (p.flags & phd::plant_flowering) && !dormant;
                const int hue = int(genes[phd::gene_leaf]) / 10;
                const Color root = dormant ? Color{109, 91, 49, 255} : Color{55, 74, 40, 255};
                const Color shade = dormant ? Color{142, 119, 65, 255} : Color{std::uint8_t(93 - hue), 117, 51, 255};
                const Color green = dormant ? Color{178, 154, 91, 255} : Color{std::uint8_t(139 - hue), 158, 65, 255};
                const Color sunlit = dormant ? Color{203, 186, 128, 255} : Color{std::uint8_t(180 - hue), 182, 77, 255};
                const int count = (reed ? 8 : 7) + int(s >> 29);
                const float span = std::min(reed ? 0.17f : 0.16f, height * (reed ? 0.22f : 0.40f));
                const float bend = (reed ? 0.15f : 0.25f) * height;
                for (int k = 0; k < count; ++k) {
                    const auto a = hash(k + 1);
                    const float angle = heading + float(k) * 2.3999632f;
                    const V3 axis{std::cos(angle), std::sin(angle), 0};
                    const float scale = 0.48f + 0.52f * unit(a >> 8);
                    const float rad = (0.25f + 0.75f * unit(a)) * span;
                    const float bx = x + axis.x * rad, by = y + axis.y * rad;
                    const float g = ground(bx, by) - 0.014f;
                    const V3 base{bx, by, g};
                    const float h = height * scale;
                    const float w = std::clamp(h * (reed ? 0.091f : 0.17f), 0.018f, 0.067f);
                    const V3 half{(0.78f - axis.y * 0.22f) * w, axis.x * w * 0.55f, 0};
                    const V3 lean{(0.4f + 0.78f * axis.x) * bend, (0.22f + axis.y) * bend, 0};
                    const V3 mid = base + lean * 0.40f + V3{0, 0, h * 0.55f};
                    const V3 tip = base + lean * (1.1f + unit(a >> 16)) + V3{0, 0, h};
                    const Color side = (a & 2) ? shade : green;
                    tri(base - half, base + half, mid - half, (a & 1) ? root : shade);
                    tri(base + half, mid + half, mid - half, side);
                    tri(mid - half, mid + half, tip, (a & 4) ? sunlit : green);
                    if (reed && (k == 2 || k == 5) && h > 0.34f && !dormant) {
                        const V3 head = mid + (tip - mid) * 0.69f;
                        const V3 cap = tip + V3{0, 0, 0.028f};
                        const V3 shoulder = head + (cap - head) * 0.36f;
                        const V3 side{0.023f, 0, 0};
                        tri(head, shoulder - side, shoulder + side, {102, 73, 37, 255});
                        tri(shoulder - side, shoulder + side, cap, {140, 99, 46, 255});
                    }
                    if (flowering && !reed && k % 4 == 0 && h > 0.18f) {
                        const Color petals[] = {{239, 234, 196, 255}, {236, 204, 92, 255}, {212, 148, 174, 255},
                                                {163, 125, 187, 255}, {211, 104, 91, 255}, {152, 162, 210, 255}};
                        const Color petals_here = petals[genes[phd::gene_flower] * 6 / 256];
                        for (int j = 0; j < 4; ++j) {
                            const float theta = angle + 1.57f * float(j);
                            const V3 out{std::cos(theta) * 0.044f, std::sin(theta) * 0.044f, 0.016f};
                            const V3 across{-out.y * 0.45f, out.x * 0.45f, 0};
                            tri(tip, tip + out + across, tip + out - across, petals_here);
                        }
                    }
                }
            };
            for (int cy = y0; cy <= y1; ++cy) {
                for (int cx = x0; cx <= x1; ++cx) {
                    const auto cell = std::size_t(cy) * width_ + cx;
                    if (world.tree(cell)) {
                        continue;
                    }
                    const int depth = world.water(cell) == phd::Water::none ? 0 : world.water_depth_dm(cell);
                    for (int slot = 0; slot < 4; ++slot) {
                        const auto* p = world.occupant(cell, slot);
                        if (p && !phd::woody(p->genome)) {
                            tuft(*p, slot, cx, cy, depth);
                        }
                    }
                }
            }
        }
        // Shrubs: woody plants in a cell's quarters, as small domes.
        for (int cy = y0; cy <= y1; ++cy) {
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
                    if (shoreline_study_) {
                        painted_crown(sx, sy, g, hh, rr, c, p->id, false, bare, 0, 0, true);
                        continue;
                    }
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
    if (shoreline_study_) {
        painted_crown(tx, ty, tz, 2.3f, .81f, {88, 139, 59, 255}, 7342u, false, false, 0, 0, false);
    } else {
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
