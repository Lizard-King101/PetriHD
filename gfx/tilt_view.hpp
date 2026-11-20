#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "batch.hpp"
#include "camera.hpp"
#include "font.hpp"
#include "plant_sprites.hpp"
#include "terrain_art.hpp"
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

    // Whether the image being drawn into has an alpha channel: shadows use it
    // so that overlapping ones darken the ground once.
    void set_target_alpha(bool has) { target_alpha_ = has; }
    bool init(const phd::World& world);
    void refresh(const phd::World& world, int chunks); // re-height this many chunks
    // The display height of the ground at a point, in cells.
    float ground(float x, float y) const;
    void place_test_scene(const phd::World& world, float x, float y);

    // True when the visible ground and foliage have finished their first build.
    bool draw(const phd::World& world, const Camera& camera, const Texture& terrain, const Font& font, int screen_w,
              int screen_h, int view_h, double time, bool capture = false);

    int draw_calls() const { return draw_calls_; }
    int trees() const { return trees_; }
    int triangles() const { return triangles_; }
    int sprites() const { return sprites_drawn_; }
    // Sprites painted into the atlas, and whether it has run out of room.
    int atlas_sprites() const { return int(painted_.size()); }
    bool atlas_full() const { return atlas_full_; }
    // Milliseconds a frame spent painting ground, smoothed, and the most in one frame.
    double ground_ms() const { return ground_ms_; }
    double ground_ms_max() const { return ground_ms_max_; }

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
        // The water over it, as a surface of its own (the painted study only).
        gl::GLuint water_vbo = 0;
        int water_vertices = 0;
        float water_top = -1e9f; // the highest its water stands
        bool water_stale = true;
        bool plants_moved = false; // the plants' ground mesh has changed
    };
    struct SpriteVertex {
        float x, y, z;      // where the plant stands
        float ox, oy, u, v; // this corner's offset in texels, and its place in the atlas
        Color sway;         // r: how far the corner sways; g: the plant's beat; b: set on a trunk's top corners;
                            // a: 0, or one more than the only view (art::sprites::view_for) it is shown in
    };
    struct PlantMesh {
        std::vector<SpriteVertex> sprites;
        std::vector<Vertex> shadows;
        int trees = 0, tall = 0, wide = 0;
        void clear() {
            sprites.clear();
            shadows.clear();
            trees = tall = wide = 0;
        }
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
    void build_water(Chunk& chunk);
    float water_corner(int x, int y) const;
    // The water's surface over a point, or `dry`; and how deep it stands there.
    float water_level(float x, float y) const;
    float wading(float x, float y) const { return std::max(0.0f, water_level(x, y) - ground(x, y)); }
    void draw_water(const phd::World& world, const Camera& camera, int screen_w, int screen_h, int view_h, double time);
    void build_chunk(Chunk& chunk);
    const Sprite* sprite(const art::sprites::Spec& spec);
    void reset_atlas();
    void add_sprite(PlantMesh& mesh, const Sprite& sprite, float x, float y, float z, float sway, std::uint32_t id,
                    Stand stand = Stand::rooted, float lift = 0, int below = 0, std::uint8_t view = 0);
    // Returns the piece that casts the tree's shadow.
    const Sprite* add_tree(PlantMesh& mesh, float x, float y, float height, float radius, bool conifer, bool leafless, std::uint8_t r,
                  std::uint8_t g, std::uint8_t b, std::uint32_t id, std::uint8_t form = 0, bool pale_bark = false, float wade = 0, float limbs = 1);
    bool allocate(int w, int h, int& x, int& y);
    void build_plant_row(const phd::World& world, int cy, int x0, int x1);
    bool update_plants(const phd::World& world, const Camera& camera, bool capture);
    bool draw_plants(const phd::World& world, const Camera& camera, const Font& font, int screen_w, int screen_h,
                     int view_h, double time, bool capture);
    void build_scene(const phd::World& world, const Camera& camera, const Font& font, double time, int view_w,
                     int view_h);

    float relief_;
    bool shoreline_study_;
    bool target_alpha_ = false;
    gl::GLuint shadow_program_ = 0;
    gl::GLint h_focus_ = -1, h_trig_ = -1, h_zoom_ = -1, h_screen_ = -1, h_depth_ = -1, h_alpha_ = -1;

    // The painted ground of the close view (ground_art.hpp), kept around the
    // camera in a wrapping 64-cell window. A small texture says which
    // patches belong to this window and can be sampled at either detail level.
    static constexpr int patch_cells = 4, window_patches = 16;
    struct GroundPatch {
        int x = 0, y = 0; // world patch coordinates of the uploaded pixels
        std::uint32_t signature = 0;
        std::uint64_t checked_tick = 0, painted_frame = 0;
        bool contains(int px, int py) const { return painted_frame && x == px && y == py; }
    };
    bool update_ground(const phd::World& world, const Camera& camera, int screen_w, int view_h, bool capture);
    void paint_ground(const phd::World& world, int patch_x, int patch_y);
    Texture ground_, ground_half_, ground_valid_;
    std::array<GroundPatch, window_patches * window_patches> ground_patches_{};
    std::vector<std::uint8_t> tile_rgba_, tile_half_;
    std::uint64_t ground_frame_ = 0;
    int ground_cursor_ = 0, window_x_ = 0, window_y_ = 0;
    double ground_ms_ = 0, ground_ms_max_ = 0;
    gl::GLint u_ground_window_ = -1;
    int width_ = 0, height_ = 0;
    std::vector<float> corners_; // (width + 1) x (height + 1) display heights, as the world is now
    // The heights each chunk's mesh was last built with. Water rises and
    // falls, and meshes catch up a few a frame: whatever stands on the ground
    // stands on this, the ground as drawn, or it would float until then.
    std::vector<float> drawn_;
    // The painted study keeps the ground and the water apart: the ground's
    // mesh is the bed, and these are the water's surface at each corner that
    // touches water (`dry` elsewhere), and whether it is frozen there.
    static constexpr float dry = -1e9f;
    std::vector<float> water_;
    std::vector<std::uint8_t> ice_;
    std::vector<std::int8_t> flow_x_, flow_y_; // which way it runs and how fast, in 127ths
    std::vector<Chunk*> visible_;
    gl::GLuint water_program_ = 0;
    gl::GLint w_focus_ = -1, w_trig_ = -1, w_zoom_ = -1, w_screen_ = -1, w_depth_ = -1, w_tint_ = -1, w_time_ = -1, w_origin_ = -1;
    Texture ripples_;
    std::vector<Chunk> chunks_;
    std::vector<Vertex> scratch_;
    std::vector<std::uint16_t> index_scratch_;
    std::uint64_t heights_tick_ = 0;
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
    std::unordered_map<std::uint64_t, Sprite> sprite_cache_; // by spec, stand-ins included
    std::vector<Sprite> painted_;                            // the sprites really in the atlas
    PlantMesh plant_mesh_, scene_plants_;
    PlantMesh fire_mesh_, smoke_mesh_; // flames, and smoke, made anew each frame
    void build_fire(const phd::World& world, const Camera& camera, int screen_w, int view_h, double time);
public:
    // The flames, after `draw` and after whatever light the caller has laid
    // over the scene, so the fire's own light doesn't wash them out. (Smoke is
    // drawn with the plants, and is lit like them.)
    void draw_fire();
private:
    int scene_tree_atlas_ = -1;
    Chunk* building_ = nullptr;
    int next_plant_cell_ = 0;
    std::uint64_t plant_start_tick_ = 0;
    art::Sun plant_sun_{};
    std::vector<Chunk*> plant_chunks_;
    gl::GLuint sprite_program_ = 0, scene_sprite_vbo_ = 0;
    gl::GLint s_focus_ = -1, s_trig_ = -1, s_zoom_ = -1, s_screen_ = -1, s_depth_ = -1, s_tint_ = -1, s_wind_ = -1, s_texel_ = -1, s_view_ = -1;
    float white_u_ = 0, white_v_ = 0;
    int sprites_drawn_ = 0;

    bool scene_ = false;
    float scene_x_ = 0, scene_y_ = 0;
    std::vector<Vertex> scene_vertices_;
    int draw_calls_ = 0, triangles_ = 0, trees_ = 0;
};

} // namespace gfx
