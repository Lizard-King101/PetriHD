#include "png.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace phd {
namespace {

std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        }
        table[n] = c;
    }
    return table;
}

std::uint32_t crc(const std::uint8_t* data, std::size_t size) {
    static const auto table = make_crc_table();
    std::uint32_t c = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        c = table[(c ^ data[i]) & 255] ^ (c >> 8);
    }
    return c ^ 0xffffffffu;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void chunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
    put32(png, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    put32(png, crc(body.data(), body.size()));
}

} // namespace

bool write_png(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb) {
    if (width <= 0 || height <= 0 || rgb.size() != std::size_t(width) * height * 3) {
        return false;
    }
    std::vector<std::uint8_t> raw;
    raw.reserve(std::size_t(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + std::size_t(y) * width * 3, rgb.begin() + std::size_t(y + 1) * width * 3);
    }
    std::vector<std::uint8_t> z{0x78, 0x01};
    for (std::size_t at = 0; at < raw.size(); at += 65535) {
        const auto n = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.push_back(n & 255);
        z.push_back((n >> 8) & 255);
        z.push_back(~n & 255);
        z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    }
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, b << 16 | a);
    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, std::uint32_t(width));
    put32(ihdr, std::uint32_t(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const bool written = std::fwrite(png.data(), 1, png.size(), file) == png.size();
    return std::fclose(file) == 0 && written;
}

} // namespace phd
