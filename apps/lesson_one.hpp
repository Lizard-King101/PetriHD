#pragma once

#include "brain.hpp"
#include <filesystem>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace nursery {
std::string fingerprint(std::uint64_t value);
std::string brain_id(const phd::brain::Brain& brain);
void validate_lesson_one(const phd::brain::Brain& candidate, const std::filesystem::path& preset, std::ostream& out);
void lesson_one(const std::filesystem::path& directory, const std::vector<phd::brain::Brain>& pool,
                int workers, bool visual, const std::filesystem::path& preset);
void lesson_two(const std::filesystem::path& directory, const std::filesystem::path& uniform,
                const std::filesystem::path& patches, const std::vector<std::pair<unsigned, phd::brain::Brain>>& swimmers, bool visual);
void breeding_curriculum(const std::filesystem::path& directory, const std::filesystem::path& preset,
                         const std::vector<std::pair<std::string, phd::brain::Brain>>& founders, unsigned seed_base, bool visual);
}
