#pragma once

#include <cstdint>
#include <vector>

#include "batch.hpp"
#include "camera.hpp"
#include "font.hpp"
#include "terrain_art.hpp"
#include "world.hpp"

namespace gfx {

// Draws the world: the terrain texture (baked a few chunks at a time, so the
// sun, snow and water keep moving without a frame ever paying for all of
// it), then cloud shadows, fog, rain and snow, and lightning. This is the
// flat path, used up to 32 px per cell; the tilted path draws the same
// terrain texture over a heightfield (tilt_view.hpp).
class WorldView {
public:
    // Small enough that one chunk's bake (about 0.4 ms on the PC, perhaps
    // 3-4 ms on the Pi) never takes a frame by itself.
    static constexpr int chunk_cells = 16;

    bool init(const phd::World& world);
    // Returns true once every visible chunk has detail for this lighting mode.
    // A capture finishes all visible work for the requested tick.
    bool refresh(const phd::World& world, const Camera& camera, int view_w, int view_h, double budget,
                 bool tilted, bool capture = false);
    void draw(Batch& batch, const Font& font, const phd::World& world, const Camera& camera, int view_w, int view_h,
              double time);

    const Texture& terrain() const { return terrain_; }
    double bake_ms() const { return bake_ms_; }
    int chunks_per_frame() const { return chunks_last_; }

private:
    void bake_chunk(const phd::World& world, const art::Sun& sun, int chunk, bool flat);
    void update_sky(const phd::World& world);

    // The terrain at 4 texels per cell, and averaged down to 2 and 1, so the
    // zoomed-out view samples one texel per pixel and small things don't blink.
    Texture terrain_, terrain_half_, terrain_quarter_, clouds_, fog_;
    enum class Lighting { preview, flat, tilted };
    struct BakedChunk {
        std::uint64_t tick = 0;
        Lighting lighting = Lighting::preview;
    };
    struct BakeJob {
        int chunk;
        bool needs_detail;
        std::uint64_t tick;
        float distance;
    };
    int chunks_x_ = 0, chunks_y_ = 0;
    std::uint64_t sky_tick_ = 0;
    std::vector<BakedChunk> baked_;
    std::vector<std::uint8_t> chunk_rgb_, half_rgb_, quarter_rgb_, cloud_rgba_, fog_rgba_;
    std::vector<BakeJob> pending_;
    double bake_ms_ = 0;
    int chunks_last_ = 0;
};

} // namespace gfx
