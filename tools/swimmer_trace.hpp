#pragma once

#include "world.hpp"

namespace swimmer_trace {
void mating_pass(const phd::World& world, const phd::Animal& animal);
void death(const phd::World& world, const phd::Animal& animal, std::uint32_t killer);
}
