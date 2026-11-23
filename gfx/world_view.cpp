#include "world_view.hpp"

#include <algorithm>
#include <cmath>

#include "math.hpp"

namespace gfx {
namespace {

constexpr int tpc = art::texels_per_cell;

// A cheap, complete map at every zoom while the first detailed chunks arrive.
Texture preview_texture(const phd::World& world, int scale) {
    const int width = world.width() * scale, height = world.height() * scale;
    std::vector<std::uint8_t> rgb(std::size_t(width) * height * 3);
    for (int y = 0; y < world.height(); ++y) {
        auto* row = rgb.data() + std::size_t(y * scale) * width * 3;
        for (int x = 0; x < world.width(); ++x) {
            auto* pixel = row + x * scale * 3;
            art::biome_rgb(world.biome(std::size_t(y) * world.width() + x), pixel);
            for (int k = 1; k < scale; ++k) {
                std::copy_n(pixel, 3, pixel + k * 3);
            }
        }
        for (int k = 1; k < scale; ++k) {
            std::copy_n(row, width * 3, row + k * width * 3);
        }
    }
    return make_texture(width, height, gl::RGB, rgb.data());
}

} // namespace

bool WorldView::init(const phd::World& world) {
    chunks_x_ = (world.width() + chunk_cells - 1) / chunk_cells;
    chunks_y_ = (world.height() + chunk_cells - 1) / chunk_cells;
    terrain_ = preview_texture(world, tpc);
    terrain_half_ = preview_texture(world, tpc / 2);
    terrain_quarter_ = preview_texture(world, 1);
    baked_.assign(std::size_t(chunks_x_) * chunks_y_, BakedChunk{});
    cloud_rgba_.assign(std::size_t(world.nodes_x()) * world.nodes_y() * 4, 255);
    fog_rgba_ = cloud_rgba_;
    light_rgba_.assign(std::size_t(light_size) * light_size * 4, 255);
    glow_ = make_texture(light_size, light_size, gl::RGBA, light_rgba_.data());
    clouds_ = make_texture(world.nodes_x(), world.nodes_y(), gl::RGBA, cloud_rgba_.data(), true);
    fog_ = make_texture(world.nodes_x(), world.nodes_y(), gl::RGBA, fog_rgba_.data(), true);
    update_sky(world);
    return terrain_.id && terrain_half_.id && terrain_quarter_.id && clouds_.id && fog_.id;
}


bool WorldView::refresh(const phd::World& world, const Camera& camera, int view_w, int view_h, float live_opacity, TerrainBatch* batch) {
    bool ready = true;
    if (live_opacity < 1) ready = refresh_terrain(world, camera, view_w, view_h, false, batch);
    if (live_opacity > 0) {
        if (!live_terrain_.id) {
            live_terrain_ = preview_texture(world, tpc);
            live_baked_.resize(baked_.size());
        }
        ready = refresh_terrain(world, camera, view_w, view_h, true, batch) && ready;
    }
    if (sky_tick_ != world.tick()) update_sky(world);
    return ready;
}

bool WorldView::refresh_terrain(const phd::World& world, const Camera& camera, int view_w, int view_h,
                                bool tilted, TerrainBatch* batch) {
    auto& cache = tilted ? live_baked_ : baked_;
    const Lighting lighting = tilted ? Lighting::tilted : camera.tree_xray ? Lighting::xray : Lighting::flat;
    // The cells in view: flat, a rectangle; tilted, the view reaches further
    // north, and terrain rising from beyond its bottom edge can show too.
    const float half_w = float(view_w) / (2 * camera.zoom);
    const float reach = float(view_h) / (2 * camera.zoom * camera.sin_pitch());
    const float extra = camera.tilted() ? 24.0f : 0.0f;
    const int x0 = std::max(0, int(camera.cx - half_w) / chunk_cells);
    const int y0 = std::max(0, int(std::max(0.0f, camera.cy - reach - extra)) / chunk_cells);
    const int x1 = std::min(chunks_x_ - 1, int(std::max(0.0f, camera.cx + half_w)) / chunk_cells);
    const int y1 = std::min(chunks_y_ - 1, int(std::max(0.0f, camera.cy + reach + extra / 4)) / chunk_cells);
    bool ready = true;
    // Bake adjacent chunks in strips to share edge samples and reduce texture uploads.
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1;) {
            const auto stale = [&](int column) {
                const auto& tile = cache[std::size_t(y * chunks_x_ + column)];
                return tile.lighting != lighting || tile.tick != world.tick();
            };
            if (!stale(x)) { ++x; continue; }
            const int first = x++;
            while (x <= x1 && stale(x)) ++x;
            const int end = x;
            for (int col = first; col < end; ++col) {
                if (cache[std::size_t(y * chunks_x_ + col)].lighting != lighting) ready = false;
            }
            if (!batch) continue;
            const int tx = first * chunk_cells * tpc, ty = y * chunk_cells * tpc;
            const int w = std::min(end * chunk_cells, world.width()) * tpc - tx;
            const int h = std::min(chunk_cells, world.height() - y * chunk_cells) * tpc;
            batch->add([=](const phd::World& source) {
                std::vector<std::uint8_t> pixels(std::size_t(w) * h * 3);
                art::bake(source, art::sun_for(source), tx, ty, w, h, pixels.data(), w * 3, !tilted, lighting == Lighting::xray);
                std::vector<std::uint8_t> half, quarter;
                if (!tilted) {
                    half = half_terrain<3>(pixels, w, h);
                    quarter = half_terrain<3>(half, w / 2, h / 2);
                }
                return [=, pixels = std::move(pixels), half = std::move(half), quarter = std::move(quarter), tick = source.tick()] {
                    update_texture_region(tilted ? live_terrain_ : terrain_, tx, ty, w, h, gl::RGB, pixels.data());
                    if (!tilted) {
                        update_texture_region(terrain_half_, tx / 2, ty / 2, w / 2, h / 2, gl::RGB, half.data());
                        update_texture_region(terrain_quarter_, tx / 4, ty / 4, w / 4, h / 4, gl::RGB, quarter.data());
                    }
                    auto& published = tilted ? live_baked_ : baked_;
                    for (int col = first; col < end; ++col) published[std::size_t(y * chunks_x_ + col)] = {tick, lighting};
                    uploaded_chunks_ += end - first;
                };
            });
        }
    }
    return ready;
}

void WorldView::update_sky(const phd::World& world) {
    const int nx = world.nodes_x(), ny = world.nodes_y();
    // Morning fog where the ground is damp and the air is calm.
    const int hour = world.day_angle();
    const float morning = std::max(0.0f, 1.0f - std::fabs(float(hour - 60)) / 26.0f);
    const float calm = std::clamp((60.0f - float(world.wind_speed())) / 40.0f, 0.0f, 1.0f);
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            const auto at = std::size_t(y * nx + x);
            cloud_rgba_[at * 4 + 3] = std::uint8_t(world.cloud(x, y));
            const int cx = std::min(world.width() - 1, x * phd::World::node + phd::World::node / 2);
            const int cy = std::min(world.height() - 1, y * phd::World::node + phd::World::node / 2);
            const auto cell = std::size_t(cy) * world.width() + cx;
            const float damp = world.water(cell) == phd::Water::none
                                   ? std::clamp((float(world.moisture(cell)) - 110.0f) / 90.0f, 0.0f, 1.0f)
                                   : world.water(cell) == phd::Water::ocean ? 0.2f : 0.8f;
            fog_rgba_[at * 4 + 3] = std::uint8_t(200.0f * damp * morning * calm);
        }
    }
    update_texture(clouds_, gl::RGBA, cloud_rgba_.data());
    update_texture(fog_, gl::RGBA, fog_rgba_.data());
    sky_tick_ = world.tick();
}

void WorldView::draw_firelight(Batch& batch, const phd::World& world, const Camera& camera, int view_w, int view_h,
                               double time) {
    // Only as the daylight goes: full by night, nothing by day.
    const float dark = std::clamp(1.0f - float(world.daylight()) / 255.0f * 1.6f, 0.0f, 1.0f);
    if (dark <= 0) {
        return;
    }
    const float zoom = camera.px(), tilt = camera.sin_pitch();
    const float left = camera.left(float(view_w)), top = camera.cy - float(view_h) / (2 * zoom * tilt);
    // A map of the light over the view, eight texels to the cell: each fire
    // adds a pool that fades to nothing at its reach, and where pools meet
    // they add up, to no more than full light.
    constexpr int per = light_per_cell, reach = 4;
    const int cx0 = int(std::floor(left)) - reach, cy0 = int(std::floor(top)) - reach;
    const int cells_w = std::min(light_size / per, int(float(view_w) / zoom) + 2 * reach + 2);
    const int cells_h = std::min(light_size / per, int(float(view_h) / (zoom * tilt)) + 2 * reach + 2);
    const int w = cells_w * per, h = cells_h * per;
    light_.assign(std::size_t(w) * h, 0.0f);
    const auto beat = std::uint32_t(time * 9);
    bool any = false;
    for (int j = 0; j < cells_h; ++j) {
        for (int i = 0; i < cells_w; ++i) {
            const int cx = cx0 + i, cy = cy0 + j;
            if (cx < 0 || cy < 0 || cx >= world.width() || cy >= world.height()) {
                continue;
            }
            const int burning = world.fire(std::size_t(cy) * world.width() + cx);
            if (!burning) {
                continue;
            }
            any = true;
            // It flickers, each fire on its own beat.
            const float flicker = 0.85f + 0.15f * float(phd::hash3(cx, cy, beat) & 255) / 255.0f;
            const float radius = (1.8f + float(burning) * 0.16f) * float(per) * flicker;
            const float mx = (float(i) + 0.5f) * float(per), my = (float(j) + 0.5f) * float(per);
            for (int y = std::max(0, int(my - radius)); y <= std::min(h - 1, int(my + radius)); ++y) {
                for (int x = std::max(0, int(mx - radius)); x <= std::min(w - 1, int(mx + radius)); ++x) {
                    const float dx = float(x) + 0.5f - mx, dy = float(y) + 0.5f - my;
                    const float fade = 1 - std::sqrt(dx * dx + dy * dy) / radius;
                    if (fade > 0) {
                        light_[std::size_t(y) * w + x] += fade * fade * 0.7f;
                    }
                }
            }
        }
    }
    if (!any) {
        return;
    }
    // In five steps, each fraying into the next in a checker fixed to the
    // ground, so the light is stepped like the rest of the picture.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float v = std::min(1.0f, light_[std::size_t(y) * w + x]);
            const bool odd = ((cx0 * per + x) + (cy0 * per + y)) & 1;
            const int step = std::clamp(int(v * 5 + (odd ? 0.75f : 0.25f)), 0, 5);
            auto* p = &light_rgba_[(std::size_t(y) * w + x) * 4];
            p[0] = p[1] = p[2] = std::uint8_t(step * 51);
        }
    }
    // Its rows are as wide as the region, which goes in the texture's top-left corner.
    update_texture_region(glow_, 0, 0, w, h, gl::RGBA, light_rgba_.data());
    // What is drawn is already dimmed to the night's tint. Drawn twice with
    // the blend that brightens by a share of what is there, full light takes
    // red and green back to about what day would show, warm, and leaves the
    // blue as dim as the night made it.
    std::uint8_t tint[3];
    art::day_tint(world, tint);
    const float warm[3] = {1.0f, 0.72f, 0.4f};
    Color gain{0, 0, 0, 255};
    std::uint8_t* channel[3] = {&gain.r, &gain.g, &gain.b};
    for (int k = 0; k < 3; ++k) {
        const float full = std::max(1.0f, warm[k] * 255.0f / std::max(40.0f, float(tint[k])));
        *channel[k] = std::uint8_t(std::min(1.0f, (std::sqrt(full) - 1) * dark) * 255.0f);
    }
    const float x0 = (float(cx0) - left) * zoom, y0 = (float(cy0) - top) * zoom * tilt;
    const float x1 = x0 + float(cells_w) * zoom, y1 = y0 + float(cells_h) * zoom * tilt;
    const float u1 = float(w) / float(light_size), v1 = float(h) / float(light_size);
    for (int pass = 0; pass < 2; ++pass) {
        batch.quad(glow_, std::floor(x0), std::floor(y0), std::floor(x1), std::floor(y1), 0, 0, u1, v1, gain);
    }
}

void WorldView::draw(Batch& batch, const Font& font, const phd::World& world, const Camera& camera, int view_w,
                     int view_h, double time, bool over_painting) {
    const float zoom = camera.px();
    // The ground level with the focus: tilted, a cell is shorter up the screen.
    const float tilt = over_painting ? camera.sin_pitch() : 1.0f;
    const float left = camera.left(float(view_w)), top = camera.cy - float(view_h) / (2 * zoom * tilt);
    const auto sx = [&](float cells) { return (cells - left) * zoom; };
    const auto sy = [&](float cells) { return (cells - top) * zoom * tilt; };
    const float ww = float(world.width()), wh = float(world.height());
    const float light = float(world.daylight()) / 255.0f;

    std::uint8_t tint[3];
    art::day_tint(world, tint);
    // The level with about one texel per pixel.
    const Texture& level = zoom >= 3 ? terrain_ : zoom >= 1.5f ? terrain_half_ : terrain_quarter_;
    if (!over_painting) {
        batch.quad(level, sx(0), sy(0), sx(ww), sy(wh), 0, 0, 1, 1, {tint[0], tint[1], tint[2], 255});
    }

    // Cloud shadows fall away from the sun; by night there are none to see.
    const auto sun = art::sun_for(world);
    const float node_w = float(world.nodes_x() * phd::World::node), node_h = float(world.nodes_y() * phd::World::node);
    const float ox = -sun.x * 5, oy = -sun.y * 5;
    const std::uint8_t shadow = std::uint8_t(110 * light);
    batch.quad(clouds_, sx(ox), sy(oy), sx(ox + node_w), sy(oy + node_h), 0, 0, 1, 1, {8, 12, 28, shadow});
    if (zoom <= 2.5f) {
        batch.quad(clouds_, sx(0), sy(0), sx(node_w), sy(node_h), 0, 0, 1, 1, {230, 234, 240, std::uint8_t(40 + 30 * light)});
    }
    // Close up the fog is a veil over the scene, not a blanket over the map.
    batch.quad(fog_, sx(0), sy(0), sx(node_w), sy(node_h), 0, 0, 1, 1, {220, 226, 230, std::uint8_t(over_painting ? 150 : 255)});

    // Fire: flickering flames over burning cells, smoke drifting downwind.
    if (!over_painting) {
        const int x0 = std::max(0, int(left)), y0 = std::max(0, int(top));
        const int x1 = std::min(world.width() - 1, int(left + float(view_w) / zoom) + 1);
        const int y1 = std::min(world.height() - 1, int(top + float(view_h) / zoom) + 1);
        const auto flicker = std::uint32_t(time * 12);
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                const int f = world.fire(std::size_t(cy) * world.width() + cx);
                if (!f) {
                    continue;
                }
                const float bx = sx(float(cx)), by = sy(float(cy));
                const float size = std::max(1.0f, zoom * 0.35f);
                for (int k = 0; k < 3; ++k) {
                    const auto h = phd::hash3(cx * 3 + k, cy, flicker);
                    const float px = bx + float(h & 255) / 255.0f * zoom, py = by + float(h >> 8 & 255) / 255.0f * zoom;
                    font.rect(batch, px, py, px + size, py + size,
                              k == 0 ? gfx::Color{255, 226, 96, 240} : gfx::Color{238, 112, 36, 220});
                }
                const auto h = phd::hash3(cx, cy, flicker / 4);
                const float drift = float(h & 15) * zoom * 0.15f;
                const float smoke = std::max(2.0f, zoom * 0.8f);
                font.rect(batch, bx + float(phd::icos(world.wind_angle())) / 1024.0f * drift,
                          by + float(phd::isin(world.wind_angle())) / 1024.0f * drift - zoom * 0.5f,
                          bx + float(phd::icos(world.wind_angle())) / 1024.0f * drift + smoke,
                          by + float(phd::isin(world.wind_angle())) / 1024.0f * drift - zoom * 0.5f + smoke,
                          {70, 66, 64, 70});
            }
        }
    }

    if (over_painting) {
        // Close up, rain and snow fall across the screen. Each drop has its
        // own column and falls again and again; it shows while the ground
        // under it is being rained on, more of them the harder it rains.
        const float wind = float(phd::icos(world.wind_angle())) / 1024.0f * float(world.wind_speed()) / 96.0f;
        const auto falling = [&](float px, float py, bool snow, int roll) {
            const int cx = std::clamp(int(left + px / zoom), 0, world.width() - 1);
            const int cy = std::clamp(int(top + py / (zoom * tilt)), 0, world.height() - 1);
            if (snow != (world.temperature(std::size_t(cy) * world.width() + cx) < 0)) return false;
            const int rain = world.rain_at(cx, cy);
            return rain >= 3 && roll < std::min(255, rain * 14);
        };
        const float w = float(view_w), h = float(view_h);
        for (int i = 0; i < 280; ++i) {
            const auto hsh = phd::hash3(i, 17, 0x7a11u);
            const float home = float(hsh & 1023) / 1023.0f * (w + 60) - 30, start = float(hsh >> 10 & 1023) / 1023.0f;
            const int roll = int(hsh >> 20 & 255);
            const float speed = float(hsh >> 28) / 15.0f;
            const float fall = std::fmod(start + float(time) * (1.3f + 0.5f * speed), 1.0f);
            const float px = std::floor(home + wind * 60 * fall), py = std::floor(fall * (h + 20) - 10);
            if (falling(px, py, false, roll)) {
                const float lean = wind * 5;
                font.rect(batch, px, py, px + 1, py + 4, {170, 190, 230, 110});
                font.rect(batch, px + std::floor(lean + 0.5f), py + 4, px + std::floor(lean + 0.5f) + 1, py + 8, {170, 190, 230, 110});
            }
            const float drift = std::fmod(start + float(time) * (0.10f + 0.06f * speed), 1.0f);
            const float fx = std::floor(home + wind * 40 * drift + std::sin(float(time) * 1.3f + float(i)) * 3);
            const float fy = std::floor(drift * (h + 8) - 4);
            if (falling(fx, fy, true, roll)) {
                const float size = (hsh >> 9 & 3) == 0 ? 2.0f : 1.0f;
                font.rect(batch, fx, fy, fx + size, fy + size, {240, 244, 250, 220});
            }
        }
        // Where rain lands: a short pale dash for a moment.
        const auto beat = std::uint32_t(time * 9);
        for (int i = 0; i < 90; ++i) {
            const auto hsh = phd::hash3(i, 23, beat);
            const float px = std::floor(float(hsh & 1023) / 1023.0f * w), py = std::floor(float(hsh >> 10 & 1023) / 1023.0f * h);
            if (falling(px, py, false, int(hsh >> 20 & 255))) {
                font.rect(batch, px, py, px + 3, py + 1, {200, 214, 236, 80});
            }
        }
    }

    // Rain and snow, in screen space over the nodes where it falls.
    const float node_px = phd::World::node * zoom;
    const int n0x = std::max(0, int(std::max(0.0f, left)) / phd::World::node);
    const int n0y = std::max(0, int(std::max(0.0f, top)) / phd::World::node);
    const int n1x = std::min(world.nodes_x() - 1, int(std::max(0.0f, left + float(view_w) / zoom)) / phd::World::node);
    const int n1y = std::min(world.nodes_y() - 1, int(std::max(0.0f, top + float(view_h) / zoom)) / phd::World::node);
    const auto step = std::uint32_t(time * 12);
    const float wind_x = float(phd::icos(world.wind_angle())) / 1024.0f, wind_y = float(phd::isin(world.wind_angle())) / 1024.0f;
    int drops = 0;
    for (int ny = n0y; ny <= n1y && drops < 4000 && !over_painting; ++ny) {
        for (int nx = n0x; nx <= n1x && drops < 4000; ++nx) {
            const int rain = world.rain(nx, ny);
            if (rain < 3) {
                continue;
            }
            const int cx = std::min(world.width() - 1, nx * phd::World::node + 4);
            const int cy = std::min(world.height() - 1, ny * phd::World::node + 4);
            const bool snowing = world.temperature(std::size_t(cy) * world.width() + cx) < 0;
            const int count = std::min(40, int(float(rain) / 3.0f * (node_px / 16.0f) * (node_px / 16.0f)) + 1);
            const float bx = sx(float(nx * phd::World::node)), by = sy(float(ny * phd::World::node));
            for (int i = 0; i < count; ++i, ++drops) {
                if (snowing) {
                    const auto hsh = phd::hash3(nx, ny, std::uint32_t(i));
                    const float drift = float(time) * 6.0f;
                    float px = std::fmod(float(hsh & 1023) / 1023.0f * node_px + drift * wind_x * 3 + std::sin(float(time) + float(i)) * 2, node_px);
                    float py = std::fmod(float(hsh >> 10 & 1023) / 1023.0f * node_px + drift * (wind_y * 3 + 2), node_px);
                    px = px < 0 ? px + node_px : px;
                    py = py < 0 ? py + node_px : py;
                    font.rect(batch, bx + px, by + py, bx + px + 2, by + py + 2, {240, 244, 250, 200});
                } else {
                    const auto hsh = phd::hash3(nx * 977 + i, ny, step);
                    const float px = float(hsh & 1023) / 1023.0f * node_px, py = float(hsh >> 10 & 1023) / 1023.0f * node_px;
                    const float lean = wind_x * 3;
                    font.rect(batch, bx + px, by + py, bx + px + 1, by + py + 6, {170, 190, 230, 120});
                    if (lean > 1.5f || lean < -1.5f) {
                        font.rect(batch, bx + px + lean, by + py + 3, bx + px + lean + 1, by + py + 6, {170, 190, 230, 90});
                    }
                }
            }
        }
    }

    // Lightning: a flash over the view and a jagged bolt where it struck.
    for (const auto& strike : world.lightning()) {
        if (strike.tick == 0 || world.tick() < strike.tick || world.tick() - strike.tick > 2) {
            continue;
        }
        const float age = float(world.tick() - strike.tick);
        font.rect(batch, 0, 0, float(view_w), float(view_h), {230, 235, 255, std::uint8_t(70 * (3 - age) / 3)});
        float x = sx(float(strike.x) + 0.5f), y = sy(float(strike.y) + 0.5f);
        // From the top of the screen, however far up that is.
        for (int k = 0; k < (over_painting ? 60 : 8) && y > 0; ++k) {
            const auto hsh = phd::hash3(strike.x, strike.y, std::uint32_t(strike.tick) + std::uint32_t(k));
            const float nx2 = x + float(int(hsh & 15) - 8) * 1.5f, ny2 = y - 10;
            font.rect(batch, std::min(x, nx2) - 1, ny2, std::max(x, nx2) + 1, y, {250, 250, 255, 230});
            x = nx2;
            y = ny2;
        }
    }
}

} // namespace gfx
