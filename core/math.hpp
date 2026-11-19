#pragma once

#include <cstdint>

#include "random.hpp"

namespace phd {

// Binary angles: 256 to a turn. Results are scaled by 1024. Bhaskara I's
// approximation in integers, so every platform agrees exactly.
constexpr int isin(int angle) {
    angle &= 255;
    const bool negative = angle >= 128;
    const std::int64_t a = angle & 127;
    const std::int64_t p = a * (128 - a);
    const int value = int(16 * p * 1024 / (5 * 128 * 128 - 4 * p));
    return negative ? -value : value;
}

constexpr int icos(int angle) { return isin(angle + 64); }

constexpr std::uint32_t isqrt(std::uint64_t n) {
    std::uint64_t root = 0, bit = std::uint64_t(1) << 62;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return std::uint32_t(root);
}

constexpr int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Smooth value noise in [0, 65535], with a lattice every `period` cells.
inline int value_noise(int x, int y, int period, std::uint32_t salt) {
    const int gx = floor_div(x, period), gy = floor_div(y, period);
    const int fx = (x - gx * period) * 256 / period, fy = (y - gy * period) * 256 / period;
    const auto smooth = [](int t) { return t * t * (768 - 2 * t) / 65536; };
    const int sx = smooth(fx), sy = smooth(fy);
    const auto corner = [&](int dx, int dy) { return int(hash3(gx + dx, gy + dy, salt) & 65535); };
    const int top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * sx / 256;
    const int bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * sx / 256;
    return top + (bottom - top) * sy / 256;
}

// Six octaves of value noise, from 128 cells down to 4, in [0, 65535].
inline int fractal_noise(int x, int y, std::uint32_t salt) {
    int sum = 0, weight = 32;
    for (int period = 128; period >= 4; period /= 2) {
        sum += value_noise(x, y, period, salt + std::uint32_t(period)) * weight;
        weight /= 2;
    }
    return sum / 63;
}

} // namespace phd
