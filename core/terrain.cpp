#include "terrain.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>

#include "math.hpp"

namespace phd {
namespace {

constexpr std::uint32_t salt_warp_x = 11, salt_warp_y = 12, salt_hills = 13, salt_continents = 14,
                        salt_ridges = 15, salt_ranges = 16;
constexpr int min_lake_area = 4;

} // namespace

void Terrain::generate(std::uint32_t seed, int w, int h, int ocean_percent) {
    width = w;
    height = h;
    const std::size_t n = std::size_t(w) * h;
    const auto salt = [seed](std::uint32_t purpose) { return stream_seed(seed, purpose); };

    // Heights: warped fractal hills, broad continents, ridged ranges, and a
    // falling edge so the map tends to end in sea.
    std::vector<int> raw(n);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int wx = x + (value_noise(x, y, 48, salt(salt_warp_x)) - 32768) * 14 / 32768;
            const int wy = y + (value_noise(x, y, 48, salt(salt_warp_y)) - 32768) * 14 / 32768;
            int metres = (fractal_noise(wx, wy, salt(salt_hills)) - 32768) * 2200 / 32768;
            metres += (value_noise(wx, wy, 160, salt(salt_continents)) - 32768) * 1400 / 32768;
            int ridge = 65535 - std::abs(2 * value_noise(wx, wy, 72, salt(salt_ridges)) - 65535);
            ridge = int(std::int64_t(ridge) * ridge / 65536);
            ridge = int(std::int64_t(ridge) * ridge / 65536);
            const int ranges = std::clamp((value_noise(x, y, 200, salt(salt_ranges)) - 30000) * 4, 0, 65535);
            metres += int(std::int64_t(ridge) * ranges / 65536 * 2600 / 65536);
            const int edge = std::min(std::min(x, y), std::min(w - 1 - x, h - 1 - y));
            if (edge < 40) {
                metres -= (40 - edge) * (40 - edge) * 5;
            }
            raw[std::size_t(y) * w + x] = metres;
        }
    }
    // Two passes of a 3 x 3 blur soften the creases where noise lattices meet.
    std::vector<int> blurred(n);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int sum = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = std::clamp(x + dx, 0, w - 1), sy = std::clamp(y + dy, 0, h - 1);
                        sum += raw[std::size_t(sy) * w + sx];
                    }
                }
                blurred[std::size_t(y) * w + x] = floor_div(sum, 9);
            }
        }
        raw.swap(blurred);
    }
    // Shift so the chosen share of cells is under the sea.
    std::vector<int> sorted = raw;
    const auto nth = sorted.begin() + std::ptrdiff_t(n * std::size_t(std::clamp(ocean_percent, 0, 99)) / 100);
    std::nth_element(sorted.begin(), nth, sorted.end());
    const int sea = *nth;
    elevation.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        int metres = raw[i] - sea;
        // Flatten the lowlands a little into plains and coasts.
        if (metres > 0 && metres < 300) {
            metres = metres * metres / 300;
        }
        elevation[i] = static_cast<std::int16_t>(std::clamp(metres, -6000, 6000));
    }

    // Priority flood from the map's edge (Barnes et al.): fills every basin to
    // its spill and gives each cell the neighbour its water leaves by. Ties
    // break on the index, so it is deterministic.
    filled.assign(n, 0);
    downstream.assign(n, -1);
    order.clear();
    order.reserve(n);
    std::vector<std::uint8_t> seen(n, 0);
    // Ties break on a hash of the cell, then the cell itself, so water
    // crossing flats and filled lakes wanders instead of running in straight
    // lines, and the order is still fixed.
    using Entry = std::pair<std::int64_t, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    const auto meander = salt(17);
    const auto push = [&](int index, int level, int from) {
        seen[std::size_t(index)] = 1;
        filled[std::size_t(index)] = static_cast<std::int16_t>(level);
        downstream[std::size_t(index)] = from;
        open.push({std::int64_t(level) << 32 | hash3(index % w, index / w, meander), index});
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                const int i = y * w + x;
                push(i, std::max<int>(elevation[std::size_t(i)], 0), -1);
            }
        }
    }
    // Eight neighbours, so rivers can run diagonally.
    const int dx8[] = {1, -1, 0, 0, 1, 1, -1, -1}, dy8[] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    while (!open.empty()) {
        const int index = open.top().second;
        const int level = filled[std::size_t(index)];
        open.pop();
        order.push_back(index);
        const int x = index % w, y = index / w;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + dx8[k], ny = y + dy8[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[std::size_t(ny * w + nx)]) {
                continue;
            }
            const int next = ny * w + nx;
            push(next, std::max<int>(elevation[std::size_t(next)], level), index);
        }
    }

    ocean.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        ocean[i] = filled[i] == 0 && elevation[i] < 0;
    }

    // Lakes: connected cells below their spill that share it.
    basin.assign(n, 0);
    basins.assign(1, Basin{0, 0, 0, 0, -1});
    std::vector<int> stack, members;
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
        if (b.area < min_lake_area) {
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
        coast[i] = sea_distance[i] <= 2 && (!ocean[i] || elevation[i] > -12);
    }
}

} // namespace phd
