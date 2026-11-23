#include "terrain.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <queue>
#include <utility>

#include "math.hpp"

namespace phd {
namespace {

constexpr std::uint32_t salt_warp_x = 11, salt_warp_y = 12, salt_hills = 13, salt_broad = 14, salt_ridges = 15,
                        salt_outcrops = 16, salt_meander = 17, salt_coast = 18, salt_side = 19, salt_soil = 20;
constexpr int min_pond_area = 3;

// Box blur of radius r, separable, edges clamped.
void box_blur(std::vector<int>& values, int w, int h, int r) {
    std::vector<int> pass(values.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int d = -r; d <= r; ++d) {
                sum += values[std::size_t(y) * w + std::size_t(std::clamp(x + d, 0, w - 1))];
            }
            pass[std::size_t(y) * w + x] = floor_div(sum, 2 * r + 1);
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int d = -r; d <= r; ++d) {
                sum += pass[std::size_t(std::clamp(y + d, 0, h - 1)) * w + x];
            }
            values[std::size_t(y) * w + x] = floor_div(sum, 2 * r + 1);
        }
    }
}

} // namespace

void Terrain::flood() {
    // Priority flood from the map's edge (Barnes et al.): fills every basin to
    // its spill and gives each cell the neighbour its water leaves by. Ties
    // break on a hash of the cell, then the cell itself, so water crossing
    // flats wanders instead of running in straight lines, and the order is
    // still fixed. Eight neighbours, so streams can run diagonally.
    const int w = width, h = height;
    const std::size_t n = std::size_t(w) * h;
    filled.assign(n, 0);
    downstream.assign(n, -1);
    order.clear();
    order.reserve(n);
    std::vector<std::uint8_t> seen(n, 0);
    using Entry = std::pair<std::int64_t, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    const auto push = [&](int index, int level, int from) {
        seen[std::size_t(index)] = 1;
        filled[std::size_t(index)] = static_cast<std::int16_t>(level);
        downstream[std::size_t(index)] = from;
        open.push({(std::int64_t(level) + 32768) << 32 | hash3(index % w, index / w, salt_meander), index});
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                const int i = y * w + x;
                push(i, std::max<int>(elevation[std::size_t(i)], 0), -1);
            }
        }
    }
    const int dx[] = {1, -1, 0, 0, 1, 1, -1, -1}, dy[] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        const int index = open.top().second;
        const int level = filled[std::size_t(index)];
        open.pop();
        order.push_back(index);
        const int x = index % w, y = index / w;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[std::size_t(ny * w + nx)]) {
                continue;
            }
            const int next = ny * w + nx;
            push(next, std::max<int>(elevation[std::size_t(next)], level), index);
        }
    }
}

Zone development_zone(int x, int y, int width, int height) {
    const int u = x * 100 / std::max(1, width - 1), v = y * 100 / std::max(1, height - 1);
    if (u >= 42 && u <= 56 && v >= 58 && v <= 76) {
        return Zone::pad; // south of the stream, clear of its floods
    }
    if (u < 40 && v >= 42 && v <= 78) {
        return Zone::wood;
    }
    if (u >= 40 && u <= 62 && v >= 32 && v <= 78) {
        return Zone::meadow;
    }
    return Zone::open;
}

namespace {

// The development map's ground, in dm above the sea. In whole numbers, as
// all the world is, so it comes out the same on every machine. Places are
// in hundredths of a cell and shares in 1024ths.
int development_height(int x, int y, int w, int h, std::uint32_t seed) {
    const auto smooth = [](std::int64_t t) {
        t = std::clamp<std::int64_t>(t, 0, 1024);
        return int(t * t * (3 * 1024 - 2 * t) / (1024 * 1024));
    };
    const auto length = [](std::int64_t dx, std::int64_t dy) { return std::int64_t(isqrt(std::uint64_t(dx * dx + dy * dy))); };
    const int px = x * 100, py = y * 100;
    const int u = x * 1000 / (w - 1), v = y * 1000 / (h - 1); // thousandths across and down
    int dm = 60; // the meadow, 6 m above the sea
    // The hill: 24 m, gentle to the south and west, steep on its east face.
    {
        std::int64_t dx = px - 18 * w;
        const std::int64_t dy = py - 16 * h;
        dx = dx > 0 ? dx * 21 / 10 : dx;
        dm += 240 * smooth(1024 - length(dx, dy) * 1024 / (33 * w)) / 1024;
    }
    // The ramp: one even slope, 11 m up to the north edge.
    dm += 110 * std::clamp((340 - v) * 1024 / 340, 0, 1024) / 1024 * smooth((u - 550) * 1024 / 80) / 1024;
    // The lake: a bowl 4.8 m deep, with a wide shelf on its west side.
    {
        const std::int64_t dx = std::int64_t(px - 77 * w) * 1024 / 1700, dy = std::int64_t(py - 56 * h) * 1024 / 1300;
        const std::int64_t d2 = (dx * dx + dy * dy) / 1024;
        if (d2 < 1024) {
            const bool shelf = dx < -256;
            dm -= int((shelf ? 9 : 48) * (1024 - d2) / 1024) + (shelf ? 0 : 4);
        }
    }
    // Channels 1.8 m deep and 2 cells to either side: the stream from the
    // hill's foot to the lake, and the lake's way out to the sea. Ends are in
    // hundredths of the map's width and height. The land rises gently away
    // from them, 3.5 cm a cell, so rain runs to them and not across the flat.
    std::int64_t nearest = 4000;
    const auto channel = [&](int ax, int ay, int bx, int by) {
        ax *= w, bx *= w, ay *= h, by *= h;
        const std::int64_t lx = bx - ax, ly = by - ay, qx = px - ax, qy = py - ay;
        const std::int64_t t = std::clamp<std::int64_t>((qx * lx + qy * ly) * 1024 / (lx * lx + ly * ly), 0, 1024);
        nearest = std::min(nearest, length(qx - lx * t / 1024, qy - ly * t / 1024));
    };
    channel(30, 30, 50, 44);
    channel(50, 44, 74, 56); // on into the deep of the lake, so the stream has somewhere to go
    channel(77, 68, 77, 84);
    dm += int(nearest * 35 / 10000) - int(18 * std::max<std::int64_t>(0, 200 - nearest) / 200);
    // The beach and the sea: shelving gently, then away.
    if (v > 780) {
        const int t = (v - 780) * 1024 / 220;
        dm -= 70 * t / 1024 + 60 * t / 1024 * t / 1024;
    }
    // A little unevenness everywhere but the pad, which is one even plane.
    if (development_zone(x, y, w, h) != Zone::pad) {
        dm += (value_noise(x, y, 9, stream_seed(seed, 0x6465u)) - 32768) * 2 / 32768;
    }
    return dm;
}

} // namespace

void Terrain::generate(std::uint32_t seed, int w, int h, int ocean_percent, Layout layout, int arena_depth_dm, int arena_land) {
    width = w;
    height = h;
    const std::size_t n = std::size_t(w) * h;
    if (layout == Layout::arena) {
        coast_side = 0;
        elevation.assign(n, std::int16_t(arena_depth_dm + 2));
        filled = elevation;
        downstream.assign(n, -1);
        order.resize(n);
        std::iota(order.begin(), order.end(), 0);
        basin.assign(n, 0); channel.assign(n, 0); ocean.assign(n, 0); coast.assign(n, 0);
        sea_distance.assign(n, 255); substrate.assign(n, std::uint8_t(Substrate::gravel));
        hollow.assign(n, 0); aspect.assign(n, 0); slope.assign(n, 0);
        // The western columns are dry ground, level with the bank; the rest is water.
        const int dry = std::clamp(arena_land, 0, w - 2);
        const auto area = std::uint32_t((w - 2 - dry) * (h - 2));
        basins = {Basin{}, Basin{std::int16_t(arena_depth_dm), 0, area, area * std::uint32_t(arena_depth_dm), w + 1 + dry}};
        for (int y = 1; y < h - 1; ++y) for (int x = 1 + dry; x < w - 1; ++x) {
            const int cell = y * w + x;
            elevation[cell] = 0; filled[cell] = std::int16_t(arena_depth_dm); basin[cell] = 1;
        }
        return;
    }
    const auto salt = [seed](std::uint32_t purpose) { return stream_seed(seed, purpose); };
    coast_side = int(salt(salt_side) % 4);
    if (layout == Layout::development) {
        coast_side = 0;
        elevation.resize(n);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                elevation[std::size_t(y) * w + x] = static_cast<std::int16_t>(std::clamp(development_height(x, y, w, h, seed), -2000, 3000));
            }
        }
        flood();
    } else {

    // A valley: land rising away from the sea, rolling hills, rocky ridges
    // and outcrops, and walls on the three landward edges.
    std::vector<int> raw(n);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int wx = x + (value_noise(x, y, 48, salt(salt_warp_x)) - 32768) * 10 / 32768;
            const int wy = y + (value_noise(x, y, 48, salt(salt_warp_y)) - 32768) * 10 / 32768;
            // How far inland, 0 at the coast edge to 1024 at the far edge, wobbled
            // so the shore isn't straight.
            int inland = coast_side == 0 ? (h - 1 - y) * 1024 / (h - 1)
                         : coast_side == 1 ? y * 1024 / (h - 1)
                         : coast_side == 2 ? x * 1024 / (w - 1)
                                           : (w - 1 - x) * 1024 / (w - 1);
            inland += (value_noise(x, y, 72, salt(salt_coast)) - 32768) * 260 / 32768;
            int dm = inland * 420 / 1024;
            dm += (fractal_noise(wx, wy, salt(salt_hills)) - 32768) * 240 / 32768;
            dm += (value_noise(wx, wy, 128, salt(salt_broad)) - 32768) * 160 / 32768;
            // Fine bumps, so streams on an even slope wander instead of cutting
            // straight parallel lines.
            dm += (value_noise(wx, wy, 7, salt(salt_broad) + 1) - 32768) * 22 / 32768;
            int ridge = 65535 - std::abs(2 * value_noise(wx, wy, 40, salt(salt_ridges)) - 65535);
            ridge = int(std::int64_t(ridge) * ridge / 65536);
            ridge = int(std::int64_t(ridge) * ridge / 65536);
            const int outcrops = std::clamp((value_noise(x, y, 90, salt(salt_outcrops)) - 34000) * 5, 0, 65535);
            dm += int(std::int64_t(ridge) * outcrops / 65536 * 200 / 65536);
            // Valley walls away from the sea.
            const int sides[4] = {y, h - 1 - y, x, w - 1 - x}; // distance to the north, south, west, east edges
            const int sea_edge = coast_side == 0 ? 1 : coast_side == 1 ? 0 : coast_side == 2 ? 2 : 3;
            for (int e = 0; e < 4; ++e) {
                if (e != sea_edge && sides[e] < 28) {
                    dm += (28 - sides[e]) * (28 - sides[e]) / 2;
                }
            }
            if (sides[sea_edge] < 16) {
                dm -= (16 - sides[sea_edge]) * (16 - sides[sea_edge]) * 2;
            }
            raw[std::size_t(y) * w + x] = dm;
        }
    }
    box_blur(raw, w, h, 1);
    // Shift so the chosen share of cells is under the sea.
    std::vector<int> sorted = raw;
    const auto nth = sorted.begin() + std::ptrdiff_t(n * std::size_t(std::clamp(ocean_percent, 0, 99)) / 100);
    std::nth_element(sorted.begin(), nth, sorted.end());
    // Inland hollows below that level don't count as sea, so raise the sea
    // until enough of the map is open water joined to the edge.
    int sea = *nth;
    elevation.resize(n);
    const auto target = n * std::size_t(std::clamp(ocean_percent, 0, 99)) / 100;
    // Stop short if one step would flood a whole inland basin at once.
    for (int attempt = 0, last_sea = sea; attempt < 24; ++attempt) {
        for (std::size_t i = 0; i < n; ++i) {
            int dm = raw[i] - sea;
            if (dm < 0) {
                dm = dm * 2; // the sea shelves away faster than the land rises
            }
            elevation[i] = static_cast<std::int16_t>(std::clamp(dm, -2000, 3000));
        }
        flood();
        std::size_t sea_cells = 0;
        for (std::size_t i = 0; i < n; ++i) {
            sea_cells += filled[i] == 0 && elevation[i] < 0;
        }
        if (sea_cells * 10 > target * 13 && attempt > 0) {
            sea = last_sea;
            attempt = 22; // one more pass at the previous level, then stop
            continue;
        }
        if (sea_cells * 10 >= target * 9 || attempt == 23) {
            break;
        }
        last_sea = sea;
        sea += 8;
    }
    } // a valley

    // Land below sea level that the sea can't reach would be a dry pit; in
    // a valley it is a wet hollow just above the water table.
    for (std::size_t i = 0; i < n; ++i) {
        if (elevation[i] < 0 && filled[i] > 0) {
            elevation[i] = static_cast<std::int16_t>(3);
        }
    }
    flood();
    // (The development map's channels are cut by hand, and stay as cut.)
    std::vector<int> carry(n, 1), carve(n, 0);
    for (auto at = order.rbegin(); at != order.rend(); ++at) {
        const int down = downstream[std::size_t(*at)];
        if (down >= 0) {
            carry[std::size_t(down)] += carry[std::size_t(*at)];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (layout == Layout::valley && elevation[i] > 0 && carry[i] > 120) {
            int bits = 0;
            for (int f = carry[i] / 120; f > 1; f >>= 1) {
                ++bits;
            }
            carve[i] = std::min(45, 8 + bits * 5);
        }
    }
    std::vector<int> wide = carve;
    box_blur(wide, w, h, 2);
    for (std::size_t i = 0; i < n; ++i) {
        if (elevation[i] > 0) {
            const int cut = std::max(carve[i], wide[i] * 2);
            elevation[i] = static_cast<std::int16_t>(std::max(elevation[i] - cut, std::min<int>(elevation[i], 2)));
        }
    }
    flood();

    ocean.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        ocean[i] = filled[i] == 0 && elevation[i] < 0;
    }

    // Ponds and lakes: connected cells below their spill that share it.
    basin.assign(n, 0);
    basins.assign(1, Basin{0, 0, 0, 0, -1});
    std::vector<int> stack, members;
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    for (std::size_t start = 0; start < n; ++start) {
        if (ocean[start] || basin[start] || filled[start] <= elevation[start]) {
            continue;
        }
        const auto id = static_cast<std::uint16_t>(basins.size());
        if (id == 0xffff) {
            break;
        }
        Basin b{filled[start], elevation[start], 0, 0, std::int32_t(start)};
        members.clear();
        stack.assign(1, int(start));
        basin[start] = id;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            members.push_back(i);
            b.bottom = std::min(b.bottom, elevation[std::size_t(i)]);
            b.volume += std::uint32_t(b.spill - elevation[std::size_t(i)]);
            const int x = i % w, y = i / w;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx[k], ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    continue;
                }
                const auto j = std::size_t(ny * w + nx);
                if (!basin[j] && !ocean[j] && filled[j] == b.spill && filled[j] > elevation[j]) {
                    basin[j] = id;
                    stack.push_back(int(j));
                }
            }
        }
        b.area = std::uint32_t(members.size());
        if (b.area < min_pond_area) {
            for (const int i : members) {
                basin[std::size_t(i)] = 0xffff; // a puddle: marked so it isn't revisited
            }
            continue;
        }
        basins.push_back(b);
    }
    for (auto& id : basin) {
        if (id == 0xffff) {
            id = 0;
        }
    }

    // Distance to the sea (city-block, two passes), and the coast.
    sea_distance.assign(n, 255);
    for (std::size_t i = 0; i < n; ++i) {
        if (ocean[i]) {
            sea_distance[i] = 0;
        }
    }
    const auto relax = [&](int x, int y, int nx, int ny) {
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
            return;
        }
        auto& here = sea_distance[std::size_t(y * w + x)];
        here = static_cast<std::uint8_t>(std::min<int>(here, sea_distance[std::size_t(ny * w + nx)] + 1));
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            relax(x, y, x - 1, y);
            relax(x, y, x, y - 1);
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            relax(x, y, x + 1, y);
            relax(x, y, x, y + 1);
        }
    }
    coast.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        coast[i] = sea_distance[i] <= 2 && (!ocean[i] || elevation[i] > -15);
    }

    // The shape of the land: hollows, which way slopes face, how steep.
    std::vector<int> around(elevation.begin(), elevation.end());
    for (auto& v : around) {
        v = std::max(v, 0);
    }
    box_blur(around, w, h, 10);
    hollow.assign(n, 0);
    aspect.assign(n, 0);
    slope.assign(n, 0);
    const auto at = [&](int x, int y) {
        return int(elevation[std::size_t(std::clamp(y, 0, h - 1)) * w + std::size_t(std::clamp(x, 0, w - 1))]);
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = std::size_t(y) * w + x;
            hollow[i] = static_cast<std::int16_t>(std::clamp(around[i] - std::max<int>(elevation[i], 0), -2000, 2000));
            const int gx = (at(x + 1, y) - at(x - 1, y)) / 2, gy = (at(x, y + 1) - at(x, y - 1)) / 2;
            aspect[i] = static_cast<std::int8_t>(std::clamp(-gy, -127, 127)); // falls toward +y, the south
            slope[i] = static_cast<std::uint8_t>(std::min<int>(255, int(isqrt(std::uint64_t(gx * gx + gy * gy)))));
        }
    }

    // The ground: sand by the sea and on low dunes, gravel in stream beds,
    // rock on steep slopes and outcrops, peat in wet hollows, clay on low
    // flats, loam elsewhere.
    substrate.assign(n, std::uint8_t(Substrate::loam));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = std::size_t(y) * w + x;
            const int dm = elevation[i];
            const int patch = value_noise(x, y, 20, salt(salt_soil));
            Substrate s = Substrate::loam;
            if (ocean[i]) {
                s = dm > -40 ? Substrate::sand : Substrate::clay;
            } else if (slope[i] > 9 || (slope[i] > 5 && hollow[i] < -60)) {
                s = Substrate::rock;
            } else if ((sea_distance[i] <= 3 && dm < 25) || (sea_distance[i] <= 10 && dm < 40 && patch > 40000)) {
                s = Substrate::sand;
            } else if (carry[i] > 400) {
                s = Substrate::gravel;
            } else if (hollow[i] > 25 && patch > 26000) {
                s = Substrate::peat;
            } else if (slope[i] < 2 && dm < 120 && patch < 22000) {
                s = Substrate::clay;
            }
            substrate[i] = std::uint8_t(s);
        }
    }
    // Where a stream can run: a cell that at least this many drain through.
    // A stream comes and goes with the weather, and its bed has to be there
    // either way. (No stream has been seen with fewer than 19.)
    std::vector<int> drains(n, 1);
    channel.assign(n, 0);
    for (auto at = order.rbegin(); at != order.rend(); ++at) {
        const auto i = std::size_t(*at);
        if (ocean[i]) {
            continue;
        }
        channel[i] = drains[i] >= 16 && !basin[i];
        if (const int down = downstream[i]; down >= 0) {
            drains[std::size_t(down)] += drains[i];
        }
    }
}

} // namespace phd
