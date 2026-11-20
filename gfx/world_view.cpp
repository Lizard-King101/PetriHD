#include "world_view.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

#include "math.hpp"

namespace gfx {
namespace {

constexpr int tpc = art::texels_per_cell;

double seconds() { return double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency()); }

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
    chunk_rgb_.resize(std::size_t(chunk_cells * tpc) * chunk_cells * tpc * 3);
    half_rgb_.resize(chunk_rgb_.size() / 4);
    quarter_rgb_.resize(chunk_rgb_.size() / 16);
    terrain_ = preview_texture(world, tpc);
    terrain_half_ = preview_texture(world, tpc / 2);
    terrain_quarter_ = preview_texture(world, 1);
    baked_.assign(std::size_t(chunks_x_) * chunks_y_, BakedChunk{});
    pending_.reserve(baked_.size());
    cloud_rgba_.assign(std::size_t(world.nodes_x()) * world.nodes_y() * 4, 255);
    fog_rgba_ = cloud_rgba_;
    light_rgba_.assign(std::size_t(light_size) * light_size * 4, 255);
    glow_ = make_texture(light_size, light_size, gl::RGBA, light_rgba_.data());
    clouds_ = make_texture(world.nodes_x(), world.nodes_y(), gl::RGBA, cloud_rgba_.data(), true);
    fog_ = make_texture(world.nodes_x(), world.nodes_y(), gl::RGBA, fog_rgba_.data(), true);
    update_sky(world);
    return terrain_.id && terrain_half_.id && terrain_quarter_.id && clouds_.id && fog_.id;
}

void WorldView::bake_chunk(const phd::World& world, const art::Sun& sun, int chunk, bool flat) {
    const int cx = chunk % chunks_x_, cy = chunk / chunks_x_;
    const int tx = cx * chunk_cells * tpc, ty = cy * chunk_cells * tpc;
    const int w = std::min(chunk_cells, world.width() - cx * chunk_cells) * tpc;
    const int h = std::min(chunk_cells, world.height() - cy * chunk_cells) * tpc;
    art::bake(world, sun, tx, ty, w, h, chunk_rgb_.data(), w * 3, flat);
    update_texture_region(terrain_, tx, ty, w, h, gl::RGB, chunk_rgb_.data());
    // Box-filter down by two, twice.
    const auto halve = [](const std::uint8_t* from, int fw, int fh, std::uint8_t* to) {
        for (int y = 0; y < fh / 2; ++y) {
            for (int x = 0; x < fw / 2; ++x) {
                for (int c = 0; c < 3; ++c) {
                    const auto at = [&](int dx, int dy) { return int(from[(std::size_t(2 * y + dy) * fw + 2 * x + dx) * 3 + c]); };
                    to[(std::size_t(y) * (fw / 2) + x) * 3 + c] = std::uint8_t((at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
                }
            }
        }
    };
    halve(chunk_rgb_.data(), w, h, half_rgb_.data());
    update_texture_region(terrain_half_, tx / 2, ty / 2, w / 2, h / 2, gl::RGB, half_rgb_.data());
    halve(half_rgb_.data(), w / 2, h / 2, quarter_rgb_.data());
    update_texture_region(terrain_quarter_, tx / 4, ty / 4, w / 4, h / 4, gl::RGB, quarter_rgb_.data());
}

bool WorldView::refresh(const phd::World& world, const Camera& camera, int view_w, int view_h, double budget,
                        bool tilted, bool capture) {
    const double start = seconds();
    const Lighting lighting = tilted ? Lighting::tilted : Lighting::flat;
    // The cells in view: flat, a rectangle; tilted, the view reaches further
    // north, and terrain rising from beyond its bottom edge can show too.
    const float half_w = float(view_w) / (2 * camera.zoom);
    const float reach = float(view_h) / (2 * camera.zoom * camera.sin_pitch());
    const float extra = camera.tilted() ? 24.0f : 0.0f;
    const int x0 = std::max(0, int(camera.cx - half_w) / chunk_cells);
    const int y0 = std::max(0, int(std::max(0.0f, camera.cy - reach - extra)) / chunk_cells);
    const int x1 = std::min(chunks_x_ - 1, int(std::max(0.0f, camera.cx + half_w)) / chunk_cells);
    const int y1 = std::min(chunks_y_ - 1, int(std::max(0.0f, camera.cy + reach + extra / 4)) / chunk_cells);
    pending_.clear();
    int missing = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int chunk = y * chunks_x_ + x;
            const auto& baked = baked_[std::size_t(chunk)];
            const bool needs_detail = baked.lighting != lighting;
            if (needs_detail || baked.tick != world.tick()) {
                const float dx = (x + 0.5f) * chunk_cells - camera.cx;
                const float dy = (y + 0.5f) * chunk_cells - camera.cy;
                pending_.push_back({chunk, needs_detail, baked.tick, dx * dx + dy * dy});
                missing += needs_detail;
            }
        }
    }
    const auto sun = art::sun_for(world);
    chunks_last_ = 0;
    // Fill holes near the focus first. Then service the oldest visible tick
    // first so distant chunks still catch up while the simulation is playing.
    std::sort(pending_.begin(), pending_.end(), [](const BakeJob& a, const BakeJob& b) {
        if (a.needs_detail != b.needs_detail) return a.needs_detail;
        if (!a.needs_detail && a.tick != b.tick) return a.tick < b.tick;
        return a.distance < b.distance;
    });
    for (const BakeJob& job : pending_) {
        bake_chunk(world, sun, job.chunk, !tilted);
        baked_[std::size_t(job.chunk)] = {world.tick(), lighting};
        ++chunks_last_;
        if (!capture && seconds() - start >= budget) {
            break;
        }
    }
    if (sky_tick_ != world.tick()) {
        update_sky(world);
    }
    const double ms = (seconds() - start) * 1000.0;
    bake_ms_ = bake_ms_ * 0.9 + ms * 0.1;
    return chunks_last_ >= missing;
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
        const auto falling = [&](float px, float py, bool& snowing) {
            const int cx = std::clamp(int(left + px / zoom), 0, world.width() - 1);
            const int cy = std::clamp(int(top + py / (zoom * tilt)), 0, world.height() - 1);
            snowing = world.temperature(std::size_t(cy) * world.width() + cx) < 0;
            return world.rain(std::min(world.nodes_x() - 1, cx / phd::World::node), std::min(world.nodes_y() - 1, cy / phd::World::node));
        };
        const float w = float(view_w), h = float(view_h);
        for (int i = 0; i < 280; ++i) {
            const auto hsh = phd::hash3(i, 17, 0x7a11u);
            const float home = float(hsh & 1023) / 1023.0f * (w + 60) - 30, start = float(hsh >> 10 & 1023) / 1023.0f;
            bool snowing = false;
            const int rain = falling(std::clamp(home, 0.0f, w - 1), h / 2, snowing);
            if (rain < 3 || int(hsh >> 20 & 255) >= std::min(255, rain * 14)) {
                continue;
            }
            if (snowing) {
                const float fall = std::fmod(start + float(time) * (0.10f + 0.06f * float(hsh >> 28) / 15.0f), 1.0f);
                const float px = std::floor(home + wind * 40 * fall + std::sin(float(time) * 1.3f + float(i)) * 3);
                const float py = std::floor(fall * (h + 8) - 4), size = (hsh >> 9 & 3) == 0 ? 2.0f : 1.0f;
                font.rect(batch, px, py, px + size, py + size, {240, 244, 250, 220});
            } else {
                const float fall = std::fmod(start + float(time) * (1.3f + 0.5f * float(hsh >> 28) / 15.0f), 1.0f);
                const float py = std::floor(fall * (h + 20) - 10), lean = wind * 5;
                const float px = std::floor(home + wind * 60 * fall);
                font.rect(batch, px, py, px + 1, py + 4, {170, 190, 230, 110});
                font.rect(batch, px + std::floor(lean + 0.5f), py + 4, px + std::floor(lean + 0.5f) + 1, py + 8, {170, 190, 230, 110});
            }
        }
        // Where rain lands: a short pale dash for a moment.
        const auto beat = std::uint32_t(time * 9);
        for (int i = 0; i < 90; ++i) {
            const auto hsh = phd::hash3(i, 23, beat);
            const float px = std::floor(float(hsh & 1023) / 1023.0f * w), py = std::floor(float(hsh >> 10 & 1023) / 1023.0f * h);
            bool snowing = false;
            const int rain = falling(px, py, snowing);
            if (rain >= 3 && !snowing && int(hsh >> 20 & 255) < std::min(255, rain * 14)) {
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
