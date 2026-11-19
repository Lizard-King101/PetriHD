#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phd {

// Writes 8-bit RGB pixels, rows top to bottom, as a PNG with stored
// (uncompressed) deflate blocks. No dependencies.
// From Petri dc03377, write_png (apps/snapshot.cpp).
bool write_png(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb);

} // namespace phd
