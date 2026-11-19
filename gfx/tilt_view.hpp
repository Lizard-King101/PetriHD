#pragma once

#include <cstdint>
#include <vector>

#include "batch.hpp"
#include "camera.hpp"
#include "font.hpp"
#include "world.hpp"

namespace gfx {

// The tilted path, used above 32 px per cell: the same baked terrain texture
// draped over a heightfield, so biomes, shorelines and water match the flat
// view exactly, plus depth-tested 3D objects standing on it. A prototype for
// close views: it draws no weather yet.
//
// The ground is a mesh with a vertex at every cell corner, at the average
// surface of the four cells around it (the water's surface where there is
// water, as the terrain bake does), scaled to display height. Water is
// therefore flat at its own level and meets the banks without a seam.
// Meshes are cached per 32-cell chunk and their heights refreshed in turn,
// since lakes and rivers rise and fall.
class TiltView {
public:
    static constexpr int chunk_cells = 32;

    // Display height: cells of height per metre of elevation. True scale is
    // one cell per 1.5 m.
    explicit TiltView(float cells_per_metre = 10.0f / float(phd::cell_decimetres)) : relief_(cells_per_metre) {}

    bool init(const phd::World& world);
    void refresh(const phd::World& world, int chunks); // re-height this many chunks
    // The display height of the ground at a point, in cells.
    float ground(float x, float y) const;
    void place_test_scene(const phd::World& world, float x, float y);

    void draw(const phd::World& world, const Camera& camera, const Texture& terrain, const Font& font, int screen_w,
              int screen_h, int view_h, double time);

    int draw_calls() const { return draw_calls_; }
    int triangles() const { return triangles_; }

private:
    struct Vertex {
        float x, y, z, u, v;
        Color color;
    };
    struct Chunk {
        int x0, y0, w, h;
        float zmin, zmax;
        gl::GLuint vbo = 0, ibo = 0;
        int indices = 0;
    };
    void build_heights(const phd::World& world);
    void build_chunk(Chunk& chunk);
    void build_scene(const phd::World& world, const Camera& camera, const Font& font, double time);

    float relief_;
    int width_ = 0, height_ = 0;
    std::vector<float> corners_; // (width + 1) x (height + 1) display heights
    std::vector<Chunk> chunks_;
    std::vector<Vertex> scratch_;
    std::vector<std::uint16_t> index_scratch_;
    int next_ = 0;

    gl::GLuint program_ = 0, scene_vbo_ = 0;
    gl::GLint u_focus_ = -1, u_trig_ = -1, u_zoom_ = -1, u_screen_ = -1, u_depth_ = -1, u_tint_ = -1, u_texture_ = -1,
               u_sun_ = -1, u_lit_ = -1;

    bool scene_ = false;
    float scene_x_ = 0, scene_y_ = 0;
    std::vector<Vertex> scene_vertices_;
    int draw_calls_ = 0, triangles_ = 0;
};

} // namespace gfx
