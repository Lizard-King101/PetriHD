#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "batch.hpp"
#include "camera.hpp"
#include "creature.hpp"
#include "creature_actions.hpp"
#include "font.hpp"
#include "pixel_target.hpp"
#include "plant_sprites.hpp"
#include "terrain_art.hpp"
#include "terrain_batch.hpp"
#include "water_surface.hpp"
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
// The body an animal's organs make, in the renderer's terms: its proportions
// in cells, its pigments and its pattern.
creature::Body animal_body(const phd::AnimalGenome& genome, int growth = 255);

class TiltView {
public:
    static constexpr int chunk_cells = 32;

    // Display height: cells of height per metre of elevation. True scale is
    // one cell per 1.5 m.
    TiltView(const CreatureActions& actions, float cells_per_metre, bool shoreline_study)
        : actions_(actions), relief_(cells_per_metre), shoreline_study_(shoreline_study) {}

    // Whether the image being drawn into has an alpha channel: shadows use it
    // so that overlapping ones darken the ground once.
    void set_target_alpha(bool has) { target_alpha_ = has; }
    bool init(const phd::World& world);
    void refresh(const phd::World& world, int chunks); // re-height this many chunks
    bool schedule_ground(const phd::World& world, const Camera& camera, int screen_w, int view_h, TerrainBatch* batch);
    // The display height of the ground at a point, in cells.
    float ground(float x, float y) const { return sample_ground(x, y, false); }
    // How high an animal's middle is drawn, for marking it on the screen.
    float animal_height(const phd::Animal& animal) const;
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
    // Milliseconds of close-up ground uploads, smoothed, and the most in one frame.
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
        Color sway;         // r: how far the corner sways; g: the plant's beat; b: 128 on stems, 255 on a trunk's top;
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
    float sample_ground(float x, float y, bool walking, bool* near_water = nullptr) const;
    void build_water(Chunk& chunk);
    bool water_quad(int x, int y, std::array<Vertex, 4>& quad) const;
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

    const CreatureActions& actions_;
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
        std::uint64_t checked_tick = 0;
        bool valid = false;
        bool contains(int px, int py) const { return valid && x == px && y == py; }
    };
    void bind_ground(const Camera& camera);
    void upload_ground(int patch_x, int patch_y, const std::vector<std::uint8_t>& pixels, const std::vector<std::uint8_t>& half);
    Texture ground_, ground_half_, ground_valid_;
    std::array<GroundPatch, window_patches * window_patches> ground_patches_{};
    bool ground_mask_dirty_ = true;
    int window_x_ = 0, window_y_ = 0;
    double ground_ms_ = 0, ground_ms_max_ = 0, ground_upload_ms_ = 0;
    gl::GLint u_ground_window_ = -1;
    int width_ = 0, height_ = 0;
    std::vector<float> corners_; // (width + 1) x (height + 1) display heights, as the world is now
    // The heights each chunk's mesh was last built with. Water rises and
    // falls, and meshes catch up a few a frame: whatever stands on the ground
    // stands on this, the ground as drawn, or it would float until then.
    std::vector<float> drawn_;
    std::vector<float> drawn_centers_; // same index as the top-left corner; dry for an unsubdivided cell
    // The painted study keeps the ground and the water apart: the ground's
    // mesh is the bed, and these are the water surface and ice at corners.
    // The surface extends under the banks so its intersection with the bed
    // gives the shoreline.
    static constexpr float dry = -1e9f;
    struct WaterCorner {
        float level = dry, bed = 0; // metres above sea level
        float plane = dry;          // separate plane for the cell starting here at a basin divide
        std::int8_t flow_x = 0, flow_y = 0;
        bool ice = false;
        art::water::CellCenter center;
    };
    std::vector<WaterCorner> water_;
    const WaterCorner& water_corner(int x, int y) const { return water_[std::size_t(y) * (width_ + 1) + x]; }
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
    gl::GLint s_focus_ = -1, s_trig_ = -1, s_zoom_ = -1, s_screen_ = -1, s_depth_ = -1, s_tint_ = -1, s_wind_ = -1, s_texel_ = -1, s_view_ = -1, s_xray_ = -1;
    float white_u_ = 0, white_v_ = 0;
    int sprites_drawn_ = 0;

    bool scene_ = false;
    float scene_x_ = 0, scene_y_ = 0;
    // Each study animal has a body, its current pose and the geometry that
    // pose produced for this frame.
    struct Animal {
        creature::Body body;
        creature::Pose pose;
        std::size_t first_vertex = 0, end_vertex = 0;
        std::size_t first_foot = 0, end_foot = 0;
        float pivot_z = 0; // the height used to rasterize this pose
        TexelGridAxis grid_x{}, grid_y{};
    };
    std::vector<Animal> animals_;
    std::vector<creature::Foot> animal_feet_;
    std::vector<creature::Vertex> animal_vertices_;
    // Tiny bodies rasterize straight into the art target at one texel per
    // pixel. Shallow swimmers need their own batch above the water.
    std::array<std::vector<creature::Vertex>, 2> distant_animals_;
    struct ShadowVertex {
        float x, y, depth, ink;  // screen position in creature texels, clip depth and ink level
        float ax, ay, cx, cy;    // framebuffer anchor, then the contact centre in local creature texels
        float ux, uy, vx, vy;    // screen pixel offset to coordinates on the oval
    };
    std::vector<ShadowVertex> animal_shadows_;
    struct ShadowProjection;
    void add_mesh_shadow(const Animal& animal, art::Sun sun, const ShadowProjection& grid, float width, float height);
    gl::GLuint animal_shadow_program_ = 0, animal_shadow_vbo_ = 0;
    gl::GLint animal_shadow_size_ = -1, animal_shadow_texels_ = -1;
    void draw_animal_shadows(art::Sun sun, const Camera& camera, int screen_w, int screen_h, float depth_scale);
    bool swim_ = false; // whether there is water near the scene deep enough to swim in
    float swim_x_ = 0, swim_y_ = 0;
    gl::GLuint animal_program_ = 0, animal_vbo_ = 0;
    Texture animal_patterns_;
    gl::GLint a_depth_code_ = -1, piece_depth_scale_ = -1, piece_grid_ = -1;
    gl::GLint a_focus_ = -1, a_trig_ = -1, a_zoom_ = -1, a_screen_ = -1, a_depth_ = -1, a_tint_ = -1, a_ink_ = -1, a_nudge_ = -1, a_under_ = -1;
    void pose_animals(double time);
    // The world's own animals, where it has any: those in view, each eased
    // from tick to tick so it moves smoothly at any frame rate.
    void pose_world_animals(const phd::World& world, const Camera& camera, int screen_w, int view_h);
    struct Eased {
        float x, y, heading, phase, height;
        std::uint32_t seen;
    };
    std::unordered_map<std::uint32_t, Eased> eased_;
    std::uint32_t eased_frame_ = 0;
    // Animals are drawn into a picture of their own first, with pixels the
    // size of the world's texels on screen (one at 64 px per cell, two at
    // 128, four at 256), so a close look shows them in the same big pixels
    // as everything around them. Alpha records each texel's surface depth.
    struct AnimalPiece {
        float x0, y0, x1, y1; // on screen, in art pixels
        float anchor_x, anchor_y; // local origin in framebuffer pixels, measured from bottom left
        float texel_x, texel_y; // corresponding source texel in the atlas
        float nearest, farthest; // in camera-space cells
        int last_code; // zero is transparent, positive codes cover the tile's depth range
        bool after_water;
    };
    std::vector<AnimalPiece> pieces_;
    struct PieceVertex {
        float x, y, z, anchor_x, anchor_y, texel_x, texel_y, first, last, cells_per_alpha;
    };
    std::vector<PieceVertex> piece_vertices_;
    struct AnimalLayer {
        PixelTarget target;
        std::size_t first = 0, end = 0;
    };
    std::vector<AnimalLayer> animal_layers_;
    int animal_layer_limit_ = 0;
    PixelTarget portrait_;
    std::uint32_t portrait_id_ = 0;
    int portrait_growth_ = -1, portrait_medium_ = -1;
    creature::Gesture portrait_gesture_{};
    creature::PortraitFrame portrait_frame_{};
    void upload_animal_mesh(const std::vector<creature::Vertex>& vertices);
    void draw_animal_mesh(int first, int count, float ink);
    gl::GLuint piece_program_ = 0, piece_vbo_ = 0;
    void draw_pieces(const AnimalLayer& layer, bool after_water, float depth_scale, float texel_size);
public:
    // Before the frame's own target is bound: poses the animals and draws
    // them into their layer. `draw` then lays them into the scene.
    const Texture* portrait(const phd::Animal& animal, int width, int height);
    void prepare_animals(const phd::World& world, const Camera& camera, int screen_w, int screen_h, int view_h, double time);
private:
    // (Those swimming just under the surface are laid in after the water,
    // tinted by it, because the water hides whatever its bed is too deep to show.)
    std::vector<Vertex> scene_vertices_;
    int draw_calls_ = 0, triangles_ = 0, trees_ = 0;
};

} // namespace gfx
