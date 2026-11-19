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
    static constexpr int chunk_cells = 32;

    bool init(const phd::World& world);
    // Bakes chunks for up to `budget` seconds, those on screen first.
    // Tilted, the terrain is baked without hill shading, since the tilted
    // view lights its own slopes; switching rebakes what is on screen at once.
    void refresh(const phd::World& world, const Camera& camera, int view_w, int view_h, double budget, bool tilted);
    void bake_everything(const phd::World& world);
    void draw(Batch& batch, const Font& font, const phd::World& world, const Camera& camera, int view_w, int view_h,
              double time);

    const Texture& terrain() const { return terrain_; }
    double bake_ms() const { return bake_ms_; }
    int chunks_per_frame() const { return chunks_last_; }

private:
    void bake_chunk(const phd::World& world, const art::Sun& sun, int chunk);
    void update_sky(const phd::World& world);

    Texture terrain_, clouds_, fog_;
    int chunks_x_ = 0, chunks_y_ = 0, next_ = 0;
    bool shaded_ = true;
    std::vector<std::uint8_t> chunk_rgb_, cloud_rgba_, fog_rgba_;
    std::vector<int> visible_;
    double bake_ms_ = 0;
    int chunks_last_ = 0;
};

} // namespace gfx
