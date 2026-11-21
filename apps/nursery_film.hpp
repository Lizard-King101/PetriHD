#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "world.hpp"

namespace nursery {

class Film {
public:
    explicit Film(std::filesystem::path directory, unsigned lesson = 0);
    void add(const phd::WorldConfig& config, const phd::brain::Brain& brain, const std::string& id,
             int round, const char* role, const std::string& winner, int birth_maps, unsigned ticks = 2400);
    void add_pond(phd::World& start, const std::string& id, unsigned candidate, const char* panel,
                  int rank, const char* result, unsigned ticks, std::uint64_t expected_fingerprint, bool family = false);
    void publish(bool complete) const;
private:
    std::filesystem::path directory_;
    unsigned lesson_;
    std::string session_;
    std::vector<std::string> keys_, takes_, chapters_;
};
} // namespace nursery
