#pragma once

// Dials for balancing trials: the nursery's `wild --set` turns them to see
// what a change to the world would do before it is made. They are not part
// of a world and are not saved; at 100 each the simulation is unchanged.
// A change that is kept goes into the code it scales, and its dial goes.

namespace phd::tuning {

inline int plant_growth_percent = 100; // what a plant makes from light each pass
inline int meat_percent = 100;         // what a gut absorbs of the carcass it eats
inline int upkeep_percent = 100;       // what a body burns at rest

} // namespace phd::tuning
