#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phd {

// Writes beside the destination, flushes to disk, then renames over it, so a
// power cut leaves either the old file or the new one, never half of one.
// From Petri dc03377, write_checkpoint (apps/main.cpp).
bool write_file_atomic(const std::string& path, const std::vector<std::uint8_t>& data);

bool read_file(const std::string& path, std::vector<std::uint8_t>& data);

} // namespace phd
