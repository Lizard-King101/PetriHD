# Plants

**Status:** November 19, 2026. Milestone 2, on the `milestone-2` branch.

Every plant is an individual with its own genome (`core/plants.hpp`,
`core/plants.cpp`). Nothing about grass, shrubs, trees or reeds is written
in; they are what the genes add up to.

## Genome

Sixteen genes, a byte each: height, woodiness, lifespan, root depth,
drought, cold and shade tolerance, evergreen, seed size, fruit or wind
seeds, flood tolerance, bark, serotiny (seeds held until a fire), runners,
leaf colour and flower colour.

The valley starts with seven founder families (grass, herb, reed, shrub,
broadleaf, conifer, palm), each mutated a little, placed by what the
climate and ground could support. From then on only selection decides.

## Life

- **Space:** a cell has four small slots, one per quarter. A woody plant
  must take the whole cell to grow past 1.5 m, so it can only do that once
  the plants beside it have gone; its shade usually sees to that.
- **Growth:** sunlight, less the shade of taller crowns (by how well the
  plant takes shade), times water, warmth and nutrients, less upkeep. Each
  gene that protects (deep roots, bark, drought, cold and flood tolerance)
  costs upkeep. Plants that aren't evergreen go dormant below 6 °C and live
  on what they stored; evergreens harden and pay half.
- **Damage:** frost below the plant's tolerance, flooding deeper than it can
  stand, wilting when water falls short, and hunger.
- **Water:** plants draw from the soil as they grow, so a forest dries the
  ground under it. Deep roots reach the water stored below.
- **Seeds:** in spring and summer, from spare energy. Small wind-borne seeds
  go far, mostly downwind; fruit falls near the parent (until animals carry
  it). Pollen comes from a nearby plant close enough genetically to cross,
  with uniform crossover and mutation of 1 gene in 48. Runners clone into
  neighbouring cells. Big seeds and runners take hold in shade; small seeds
  need light.
- **Death:** age, damage, fire. What dies becomes litter, which rots into
  nutrients faster when warm and damp, and is fuel until it does.
- **Fire:** lightning on dry ground with fuel. Fire spreads to dry
  neighbours with fuel, faster downwind, and stops at water, bare or freshly
  burnt ground and heavy rain. Small plants die; trees with thick bark lose
  their tops and live; held seeds are released. Ash feeds the soil.

The pool holds 262,144 plants (40 bytes each, about 10 MB), four times the
plan's 65,536: at 1.5 m cells the valley carries 100,000–190,000 plants.

## Drawing

Both views draw the real plants. Each small plant covers its quarter of the
cell as fully as it is tall, in its own leaf colour, with flowers in
spring. Trees are crowns where they stand (flat) or trunks and crowns at
their real height, swaying with the wind (tilted). Deciduous plants turn
yellow, orange or red in autumn, by their flower gene, and stand bare in
winter; dormant grass turns to straw; failing plants brown. Burnt ground is
char and ash that fades as it recovers, and fires burn with flames and
smoke in both views.

## The gate

*Forests, grasslands and marshes form and shift with climate and fire, and
plant traits differ by region.*

**8 seeds, 6 years each (23,040 ticks), the default temperate climate.**
Every seed kept 100,000–190,000 plants. Forest (closed canopy) covered
15–30% of the land and rose and fell. Six of the eight had fires; on seed 6
one burnt about a quarter of the valley in year 2, and the forest grew back
from 20% to 24% by year 6.

Traits split by habitat (means of the gene, 0–255, in year 6):

| Seed | Drought: dry land / wet land | Roots: dry / wet | Flood: wet land / in water |
| --- | --- | --- | --- |
| 1 | 120 / 112 | 103 / 117 | 76 / 148 |
| 2 | 123 / 110 | 100 / 119 | 75 / 94 |
| 3 | 127 / 111 | 94 / 134 | 56 / 150 |
| 4 | 120 / 128 | 97 / 113 | 58 / 85 |
| 5 | 123 / 116 | 92 / 120 | 58 / 121 |
| 6 | 115 / 108 | 90 / 107 | 103 / 161 |
| 7 | 119 / 114 | 91 / 118 | 70 / 133 |
| 8 | 127 / 119 | 88 / 106 | 67 / 112 |

- Plants standing in water are more flood-tolerant than those on wet land
  on all 8 seeds.
- Dry-land plants are more drought-tolerant than wet-land ones on 7 of 8.
- Roots are deeper on wet land on all 8: deep roots reach stored water, and
  dry land has little stored to reach, so there they are only a cost. That
  wasn't designed in.

**The same valley (seed 1) in three climates, after 5 years:**

| Climate | Forest | Biomes (share of the map) |
| --- | --- | --- |
| Cold (0 °C mean) | 14% | tundra 29%, bog 14%, taiga 2% |
| Temperate (9 °C) | 23% | scrub 15%, marsh 13%, heath 7%, deciduous forest 6%, grassland 4% |
| Hot (22 °C) | 20% | scrub 22%, swamp 10%, savanna 5%, desert 4%, rainforest 2%, dry woodland 2% |

## Cost, on the PC

- A tick takes about 3.6 ms with 110,000 plants (up from 0.5 ms), so
  roughly 29 ms on the Pi by the ×8 guess, on one core. Plants are the
  first thing to spread across the three simulation cores.
- Frames hold at 1.9–3.4 ms mean, with a 99th percentile of 7 ms.
- A save is 7.6 MB uncompressed; compressing saves is still to do.

## Known gaps

- Rock covers about a quarter of the map: the valley walls and steep
  slopes. The walls are probably too wide.
- Fruit falls next to its parent until there are animals to carry it.
- Toxins, thorns and flowers that need pollinators wait for animals.
- Flat-view grass isn't drawn as individual blades up close yet; that is
  the ground-detail step.
