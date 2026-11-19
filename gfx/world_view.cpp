#include "world_view.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

#include "math.hpp"

namespace gfx {
namespace {

constexpr int tpc = art::texels_per_cell;

double seconds() { return double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency()); }

} // namespace

bool WorldView::init(const phd::World& world) {
    chunks_x_ = (world.width() + chunk_cells - 1) / chunk_cells;
    chunks_y_ = (world.height() + chunk_cells - 1) / chunk_cells;
    chunk_rgb_.resize(std::size_t(chunk_cells * tpc) * chunk_cells * tpc * 3);
    terrain_ = make_texture(world.width() * tpc, world.height() * tpc, gl::RGB, nullptr);
    cloud_rgba_.assign(std::size_t(world.nodes_x()) * world.nodes_y() * 4, 255);
    fog_rgba_ = cloud_rgba_;
    clouds_ = make_texture(world.nodes_x(), world.nodes_y(), gl::RGBA, cloud_rgba_.data(), true);
    fog_ = make_texture(world.nodes_x(), world.nodes_y(), gl::RGBA, fog_rgba_.data(), true);
    bake_everything(world);
    return terrain_.id && clouds_.id && fog_.id;
}

void WorldView::bake_chunk(const phd::World& world, const art::Sun& sun, int chunk) {
    const int cx = chunk % chunks_x_, cy = chunk / chunks_x_;
    const int tx = cx * chunk_cells * tpc, ty = cy * chunk_cells * tpc;
    const int w = std::min(chunk_cells, world.width() - cx * chunk_cells) * tpc;
    const int h = std::min(chunk_cells, world.height() - cy * chunk_cells) * tpc;
    art::bake(world, sun, tx, ty, w, h, chunk_rgb_.data(), w * 3);
    update_texture_region(terrain_, tx, ty, w, h, gl::RGB, chunk_rgb_.data());
}

void WorldView::bake_everything(const phd::World& world) {
    const auto sun = art::sun_for(world);
    for (int c = 0; c < chunks_x_ * chunks_y_; ++c) {
        bake_chunk(world, sun, c);
    }
}

void WorldView::refresh(const phd::World& world, const Camera& camera, int view_w, int view_h, double budget) {
    const double start = seconds();
    const int x0 = std::max(0, int(camera.x) / chunk_cells), y0 = std::max(0, int(camera.y) / chunk_cells);
    const int x1 = std::min(chunks_x_ - 1, int(camera.x + float(view_w) / camera.zoom) / chunk_cells);
    const int y1 = std::min(chunks_y_ - 1, int(camera.y + float(view_h) / camera.zoom) / chunk_cells);
    visible_.clear();
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            visible_.push_back(y * chunks_x_ + x);
        }
    }
    const auto sun = art::sun_for(world);
    chunks_last_ = 0;
    // Round robin over what is on screen; off-screen chunks catch up when
    // they come into view.
    while (!visible_.empty() && chunks_last_ < int(visible_.size())) {
        bake_chunk(world, sun, visible_[std::size_t(next_++ % int(visible_.size()))]);
        ++chunks_last_;
        if (seconds() - start > budget) {
            break;
        }
    }
    update_sky(world);
    const double ms = (seconds() - start) * 1000.0;
    bake_ms_ = bake_ms_ * 0.9 + ms * 0.1;
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
}

void WorldView::draw(Batch& batch, const Font& font, const phd::World& world, const Camera& camera, int view_w,
                     int view_h, double time) {
    const float zoom = camera.zoom;
    const auto sx = [&](float cells) { return (cells - camera.x) * zoom; };
    const auto sy = [&](float cells) { return (cells - camera.y) * zoom; };
    const float ww = float(world.width()), wh = float(world.height());
    const float light = float(world.daylight()) / 255.0f;

    std::uint8_t tint[3];
    art::day_tint(world, tint);
    batch.quad(terrain_, sx(0), sy(0), sx(ww), sy(wh), 0, 0, 1, 1, {tint[0], tint[1], tint[2], 255});

    // Cloud shadows fall away from the sun; by night there are none to see.
    const auto sun = art::sun_for(world);
    const float node_w = float(world.nodes_x() * phd::World::node), node_h = float(world.nodes_y() * phd::World::node);
    const float ox = -sun.x * 5, oy = -sun.y * 5;
    const std::uint8_t shadow = std::uint8_t(110 * light);
    batch.quad(clouds_, sx(ox), sy(oy), sx(ox + node_w), sy(oy + node_h), 0, 0, 1, 1, {8, 12, 28, shadow});
    if (zoom <= 2.5f) {
        batch.quad(clouds_, sx(0), sy(0), sx(node_w), sy(node_h), 0, 0, 1, 1, {230, 234, 240, std::uint8_t(40 + 30 * light)});
    }
    batch.quad(fog_, sx(0), sy(0), sx(node_w), sy(node_h), 0, 0, 1, 1, {220, 226, 230, 255});

    // Rain and snow, in screen space over the nodes where it falls.
    const float node_px = phd::World::node * zoom;
    const int n0x = std::max(0, int(camera.x) / phd::World::node), n0y = std::max(0, int(camera.y) / phd::World::node);
    const int n1x = std::min(world.nodes_x() - 1, int(camera.x + float(view_w) / zoom) / phd::World::node);
    const int n1y = std::min(world.nodes_y() - 1, int(camera.y + float(view_h) / zoom) / phd::World::node);
    const auto step = std::uint32_t(time * 12);
    const float wind_x = float(phd::icos(world.wind_angle())) / 1024.0f, wind_y = float(phd::isin(world.wind_angle())) / 1024.0f;
    int drops = 0;
    for (int ny = n0y; ny <= n1y && drops < 4000; ++ny) {
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
        for (int k = 0; k < 8; ++k) {
            const auto hsh = phd::hash3(strike.x, strike.y, std::uint32_t(strike.tick) + std::uint32_t(k));
            const float nx2 = x + float(int(hsh & 15) - 8) * 1.5f, ny2 = y - 10;
            font.rect(batch, std::min(x, nx2) - 1, ny2, std::max(x, nx2) + 1, y, {250, 250, 255, 230});
            x = nx2;
            y = ny2;
        }
    }
}

} // namespace gfx
