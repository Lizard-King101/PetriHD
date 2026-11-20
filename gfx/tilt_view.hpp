#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "batch.hpp"
#include "camera.hpp"
#include "font.hpp"
#include "plant_sprites.hpp"
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
    explicit TiltView(float cells_per_metre = 10.0f / float(phd::cell_decimetres), bool shoreline_study = false)
        : relief_(cells_per_metre), shoreline_study_(shoreline_study) {}

    bool init(const phd::World& world);
    void refresh(const phd::World& world, int chunks); // re-height this many chunks
    // The display height of the ground at a point, in cells.
    float ground(float x, float y) const;
    void place_test_scene(const phd::World& world, float x, float y);

    void draw(const phd::World& world, const Camera& camera, const Texture& terrain, const Font& font, int screen_w,
              int screen_h, int view_h, double time);

    int draw_calls() const { return draw_calls_; }
    int trees() const { return trees_; }
    int triangles() const { return triangles_; }
    int sprites() const { return sprites_drawn_; }

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
        // The plants standing in it, as sprite quads, and their shadows:
        // rebuilt when the world has moved on, not every frame.
        gl::GLuint sprite_vbo = 0, shadow_vbo = 0;
        int sprite_vertices = 0, shadow_vertices = 0, trees = 0;
        int tall = 0, wide = 0; // texels its sprites reach above and beside their spots
        std::uint64_t built_tick = 0;
        int built_atlas = -1;
        bool stale = false; // the world's heights have moved on from its mesh
    };
    struct SpriteVertex {
        float x, y, z;      // where the plant stands
        float ox, oy, u, v; // this corner's offset in texels, and its place in the atlas
        Color sway;         // r: how far the corner sways; g: the plant's beat; b: set on a trunk's top corners
    };
    // How a sprite hangs from its spot: its foot fixed and its top swaying,
    // the whole of it swaying (a crown up on its trunk), or a trunk, shown
    // only as long as the camera's tilt leaves it.
    enum class Stand { rooted, rigid, upright };
    struct Sprite {
        float u0, v0, u1, v1;
        int w, h, anchor, anchor_y;
        art::sprites::Spec spec;
    };
    struct Shelf {
        int y, h, x, end; // a row of the atlas: how far along it is filled, and where it stops
    };

    void build_heights(const phd::World& world);
    void build_chunk(Chunk& chunk);
    const Sprite* sprite(const art::sprites::Spec& spec);
    void reset_atlas();
    void add_sprite(const Sprite& sprite, float x, float y, float z, float sway, std::uint32_t id,
                    Stand stand = Stand::rooted, float lift = 0, int below = 0);
    void add_tree(float x, float y, float height, float radius, bool conifer, bool leafless, std::uint8_t r,
                  std::uint8_t g, std::uint8_t b, std::uint32_t id);
    bool allocate(int w, int h, int& x, int& y);
    void build_plants(const phd::World& world, Chunk& chunk);
    void draw_plants(const phd::World& world, const Camera& camera, const Font& font, int screen_w, int screen_h,
                     int view_h, double time);
    void build_scene(const phd::World& world, const Camera& camera, const Font& font, double time, int view_w,
                     int view_h);

    float relief_;
    bool shoreline_study_;
    Texture bank_texture_;
    int width_ = 0, height_ = 0;
    std::vector<float> corners_; // (width + 1) x (height + 1) display heights, as the world is now
    // The heights each chunk's mesh was last built with. Water rises and
    // falls, and meshes catch up a few a frame: whatever stands on the ground
    // stands on this, the ground as drawn, or it would float until then.
    std::vector<float> drawn_;
    std::vector<Chunk> chunks_;
    std::vector<Vertex> scratch_;
    std::vector<std::uint16_t> index_scratch_;
    int next_ = 0;

    gl::GLuint program_ = 0, scene_vbo_ = 0;
    gl::GLint u_focus_ = -1, u_trig_ = -1, u_zoom_ = -1, u_screen_ = -1, u_depth_ = -1, u_tint_ = -1, u_texture_ = -1,
               u_sun_ = -1, u_lit_ = -1;

    // Plant sprites: painted on the CPU into one atlas, as plants need them.
    Texture atlas_;
    int atlas_generation_ = 0;
    std::vector<Shelf> shelves_;
    bool atlas_full_ = false;
    int frames_full_ = 0;
    std::unordered_map<std::uint64_t, Sprite> sprite_cache_;
    std::vector<SpriteVertex> sprite_scratch_;
    std::vector<Vertex> shadow_scratch_;
    std::vector<Chunk*> plant_chunks_;
    gl::GLuint sprite_program_ = 0, scene_sprite_vbo_ = 0;
    gl::GLint s_focus_ = -1, s_trig_ = -1, s_zoom_ = -1, s_screen_ = -1, s_depth_ = -1, s_tint_ = -1, s_wind_ = -1, s_texel_ = -1;
    float white_u_ = 0, white_v_ = 0;
    unsigned plant_cursor_ = 0;
    int sprites_drawn_ = 0, tallest_ = 0, widest_ = 0;

    bool scene_ = false;
    float scene_x_ = 0, scene_y_ = 0;
    std::vector<Vertex> scene_vertices_;
    int draw_calls_ = 0, triangles_ = 0, trees_ = 0;
};

} // namespace gfx
