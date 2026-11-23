#include "water_surface.hpp"

#include <algorithm>
#include <cmath>

namespace art::water {
namespace {

bool standing(phd::Water kind) { return kind == phd::Water::lake || kind == phd::Water::ocean; }

float standing_level(const phd::World& world, std::size_t cell, phd::Water kind) {
    if (kind == phd::Water::lake) return float(world.lake_surface_dm(world.terrain().basin[cell])) * 0.1f;
    return kind == phd::Water::ocean ? 0.0f : dry;
}

Corner gather(const phd::World& world, int x, int y) {
    Corner out;
    float river_level = 0, channel_bed = 0, high_plane = dry, low_full = -dry, high_full = dry;
    int bed_sum_dm = 0;
    int rivers = 0, channels = 0, frozen = 0, wet = 0;
    const auto& terrain = world.terrain();
    for (int j = y - 1; j <= y; ++j) {
        for (int i = x - 1; i <= x; ++i) {
            const int cx = std::clamp(i, 0, world.width() - 1), cy = std::clamp(j, 0, world.height() - 1);
            const auto cell = std::size_t(cy) * world.width() + cx;
            const auto kind = world.water(cell);
            const float bed = float(world.elevation(cell)) * 0.1f;
            bed_sum_dm += world.elevation(cell);
            if (terrain.channel[cell]) {
                channel_bed += bed;
                ++channels;
            }
            if (terrain.ocean[cell] || terrain.basin[cell]) {
                const float full = terrain.ocean[cell] ? 0.0f : float(terrain.basins[terrain.basin[cell]].spill) * 0.1f;
                low_full = std::min(low_full, full);
                high_full = std::max(high_full, full);
            }
            if (i == x && j == y) out.center.bed = bed;
            if (kind == phd::Water::none) continue;
            const float depth = float(world.water_depth_dm(cell)) * 0.1f;
            if (i == x && j == y) {
                const float level = kind == phd::Water::river ? bed + depth : standing_level(world, cell, kind);
                if (level > bed) out.center.level = level;
            }
            ++wet;
            frozen += world.temperature(cell) < -20 && (kind != phd::Water::ocean || depth < 3);
            if (kind == phd::Water::river) {
                river_level += bed + depth;
                ++rivers;
            } else {
                // A shallow cell can remain flagged wet for a while after the
                // lake falls. The basin stores its actual horizontal plane.
                const float plane = standing_level(world, cell, kind);
                high_plane = std::max(high_plane, plane);
                if (out.level == dry || plane < out.level) {
                    out.level = plane;
                    out.kind = kind;
                }
            }
            if (const int to = terrain.downstream[cell]; to >= 0 && kind != phd::Water::ocean) {
                const float dx = float(to % world.width() - cx), dy = float(to / world.width() - cy);
                const float speed = (kind == phd::Water::river ? 1.0f : 0.2f) / std::max(1.0f, std::sqrt(dx * dx + dy * dy));
                out.flow_x += dx * speed;
                out.flow_y += dy * speed;
            }
        }
    }
    // The bed is the same whatever water is on it. The mean of four cells
    // would dam a channel one cell wide at every corner between its banks,
    // so a corner on a channel lies as low as the channel does: that is cut
    // where a stream can run, not where one is running.
    out.bed = float(bed_sum_dm) * 0.025f;
    if (channels) {
        out.channel = true;
        out.bed = std::min(out.bed, channel_bed / float(channels));
    }
    // Standing waters that fill to different heights have a ridge between
    // them at least as high as the upper one when full.
    if (high_full > low_full) out.bed = std::max(out.bed, high_full);
    out.divide = high_plane != out.level;
    if (rivers && out.level == dry) {
        out.kind = phd::Water::river;
        out.level = river_level / float(rivers);
    }
    if (wet) {
        out.flow_x /= float(wet);
        out.flow_y /= float(wet);
        out.ice = frozen * 2 >= wet;
    }
    return out;
}

} // namespace

Field::Field(const phd::World& world, int x0, int y0, int width, int height)
    : x0_(x0 - 1), y0_(y0 - 1), stride_(width + 2), corners_(std::size_t(stride_) * (height + 2)) {
    bool has_divide = false;
    for (int y = y0_; y <= y0 + height; ++y) {
        for (int x = x0_; x <= x0 + width; ++x) {
            Corner& c = corners_[std::size_t(y - y0_) * stride_ + (x - x0_)];
            c = gather(world, x, y);
            has_divide = has_divide || c.divide;
        }
    }
    for (int y = y0; y < y0 + height; ++y) {
        for (int x = x0; x < x0 + width; ++x) {
            Corner& bank = corners_[std::size_t(y - y0_) * stride_ + (x - x0_)];
            if (bank.kind != phd::Water::none) {
                continue;
            }
            // Carry the nearest surface under the bank. At a river mouth
            // prefer standing water at equal distances so its rim stays flat.
            const Corner* nearest = nullptr;
            int distance = 3;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto& other = corner(x + dx, y + dy);
                    const int d = dx * dx + dy * dy;
                    if (other.kind != phd::Water::none &&
                        (d < distance || (d == distance && standing(other.kind) && !standing(nearest->kind)))) {
                        nearest = &other;
                        distance = d;
                    }
                }
            }
            if (nearest) {
                // Dry ground beside a channel can lie below the surface
                // carried out to it. The water comes down to meet the land
                // there; the land does not rise to meet the water.
                bank.level = std::min(nearest->level, bank.bed);
                bank.ice = nearest->ice;
                bank.flow_x = nearest->flow_x;
                bank.flow_y = nearest->flow_y;
            }
        }
    }
    if (has_divide) resolve_divides(world, x0, y0, width, height);
    for (int y = y0; y < y0 + height - 1; ++y) {
        for (int x = x0; x < x0 + width - 1; ++x) {
            auto& a = corners_[std::size_t(y - y0_) * stride_ + (x - x0_)];
            const auto& b = corner(x + 1, y);
            const auto& c = corner(x, y + 1);
            const auto& d = corner(x + 1, y + 1);
            // A channel that turns, or runs corner to corner, has a bank
            // cell in the crook of each turn with the bed cut at three of
            // its corners. Left standing, it squares the stream off into
            // steps. It is worn down to its corners instead, so the water
            // rounds the bend.
            if (const int cut = a.channel + b.channel + c.channel + d.channel; cut >= 3) {
                const auto cell = std::size_t(std::clamp(y, 0, world.height() - 1)) * world.width() + std::clamp(x, 0, world.width() - 1);
                if (!world.terrain().channel[cell] && !world.terrain().basin[cell] && !world.terrain().ocean[cell]) {
                    a.center.bed = std::min(a.center.bed, (a.bed + b.bed + c.bed + d.bed) * 0.25f);
                }
            }
            if (a.center.level != dry || a.level == dry || b.level == dry || c.level == dry || d.level == dry) {
                continue;
            }
            // A surface crosses this dry cell from its wet neighbours, and
            // meets the ground wherever the ground comes up through it.
            const float flat = plane(x, y);
            a.center.level = flat == dry ? (a.level + b.level + c.level + d.level) * 0.25f : flat;
        }
    }
}

void Field::resolve_divides(const phd::World& world, int x0, int y0, int width, int height) {
    planes_.assign(corners_.size(), dry);
    // Inspect one extra face around the window: a divide on its far corner
    // can affect the bank height at our first or last corner.
    const auto at_cell = [&](int x, int y) {
        const int cx = std::clamp(x, 0, world.width() - 1), cy = std::clamp(y, 0, world.height() - 1);
        const auto cell = std::size_t(cy) * world.width() + cx;
        return standing_level(world, cell, world.water(cell));
    };
    for (int y = y0 - 1; y < y0 + height; ++y) {
        for (int x = x0 - 1; x < x0 + width; ++x) {
            const auto at = std::size_t(y - y0_) * stride_ + (x - x0_);
            Corner* a = &corners_[at];
            Corner* b = a + 1;
            Corner* c = a + stride_;
            Corner* d = c + 1;
            if (!(a->divide || b->divide || c->divide || d->divide)) continue;
            const float plane = at_cell(x, y);
            if (plane == dry) continue;
            planes_[at] = plane;
        }
    }
}

float Field::depth(float x, float y) const {
    const int i = int(std::floor(x)), j = int(std::floor(y));
    const auto& a = corner(i, j);
    const auto& b = corner(i + 1, j);
    const auto& c = corner(i, j + 1);
    const auto& d = corner(i + 1, j + 1);
    if (a.level == dry || b.level == dry || c.level == dry || d.level == dry) {
        return dry;
    }
    const float flat = plane(i, j);
    const float center = a.center.level == dry ? dry : a.center.level - a.center.bed;
    if (flat != dry) {
        return interpolate(flat - a.bed, flat - b.bed, flat - c.bed, flat - d.bed, x - float(i), y - float(j), center);
    }
    return interpolate(a.level - a.bed, b.level - b.bed, c.level - c.bed, d.level - d.bed, x - float(i), y - float(j), center);
}

} // namespace art::water
