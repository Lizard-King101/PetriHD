#pragma once

#include <cstdint>
#include <vector>

#include "batch.hpp"
#include "camera.hpp"
#include "font.hpp"
#include "terrain_art.hpp"
#include "terrain_batch.hpp"
#include "world.hpp"

namespace gfx {

// Draws the terrain texture, then cloud shadows, fog, rain, snow and lightning. This is the
// flat path. The live view samples a separate terrain bake without crowns.
class WorldView {
public:
    // Camera coverage and the paint history are tracked in cells of this size.
    static constexpr int chunk_cells = 16;

    bool init(const phd::World& world);
    // Returns true once every visible chunk has detail for this lighting mode.
    // A capture finishes all visible work for the requested tick.
    bool refresh(const phd::World& world, const Camera& camera, int view_w, int view_h, float live_opacity, TerrainBatch* batch);
    // `over_painting` draws only the sky's part (cloud shadows, fog, rain,
    // snow, lightning) over a ground the tilted view has drawn, at whatever
    // pitch the camera is: cloud shadows and fog lie on the ground, taken as
    // level with the focus, and rain and snow fall across the screen.
    void draw(Batch& batch, const Font& font, const phd::World& world, const Camera& camera, int view_w, int view_h,
              double time, bool over_painting = false);

    // Firelight, over the tilted view: after dark each burning cell in view
    // lights what is around it, in steps, and fires side by side add up. It is drawn with the blend that
    // brightens what is there (gl::DST_COLOR, gl::ONE), which the caller sets
    // between the batch's begin and end; by day it draws nothing.
    void draw_firelight(Batch& batch, const phd::World& world, const Camera& camera, int view_w, int view_h, double time);

    const Texture& live_terrain() const { return live_terrain_; }
    std::uint64_t uploaded_chunks() const { return uploaded_chunks_; }

private:
    bool refresh_terrain(const phd::World& world, const Camera& camera, int view_w, int view_h,
                         bool live, TerrainBatch* batch);
    void update_sky(const phd::World& world);

    // The terrain at 4 texels per cell, and averaged down to 2 and 1, so the
    // zoomed-out view samples one texel per pixel and small things don't blink.
    Texture terrain_, terrain_half_, terrain_quarter_, live_terrain_, clouds_, fog_, glow_;
    // The firelight over the view: light_per_cell texels to the cell (two art
    // pixels to the texel at 64 px per cell), in the
    // top-left corner of a texture big enough for the widest tilted view.
    static constexpr int light_per_cell = 8, light_size = 512;
    std::vector<float> light_;
    std::vector<std::uint8_t> light_rgba_;
    enum class Lighting { preview, flat, xray, tilted };
    struct BakedChunk {
        std::uint64_t tick = 0;
        Lighting lighting = Lighting::preview;
    };
    int chunks_x_ = 0, chunks_y_ = 0;
    std::uint64_t sky_tick_ = 0;
    std::vector<BakedChunk> baked_, live_baked_;
    std::vector<std::uint8_t> cloud_rgba_, fog_rgba_;
    std::uint64_t uploaded_chunks_ = 0;
};

} // namespace gfx
