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
  the plants beside it have gone; its shade usually sees to that. It also
  needs room: no more than one other tree within two cells. Forests still
  close their canopy, but trunks stand a few metres apart, with shrubs,
  flowers and light between them.
- **Growth:** sunlight, less the shade of taller crowns (by how well the
  plant takes shade), times water, warmth and nutrients, less upkeep. Each
  gene that protects (deep roots, bark, drought, cold and flood tolerance)
  costs upkeep. Plants that aren't evergreen go dormant below 6 °C and live
  on what they stored; evergreens harden and pay half.
- **Damage:** frost below the plant's tolerance, flooding deeper than it can
  stand, wilting when water falls short, and hunger.
- **Water affinity.** The flood gene is how deep a plant can stand in water
  (up to 1.6 m). Deeper than that its roots drown, however far its top is
  above the surface, faster the deeper; so a rising lake kills the trees and
  bushes it floods. On land a plant made for water needs wetter ground, so
  reeds keep to the margins. Under water, light dims with the depth over a
  plant's top. Before this, a plant taller than the water was never harmed
  by it. Over 3 years on seeds 1, 3 and 6 this left 2-11% fewer plants and
  10-27% fewer trees, and sharpened the split in the flood gene: 51-60 on
  dry land against 147-174 in water, from 72-104 against 111-139.
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
- **Snags.** A tree of 5 m or more that dies of anything leaves its trunk
  standing where it stood, and only half its mass falls as litter. A snag
  takes no light or water and other plants grow under it. It counts as half
  a tree when a new one looks for room: there is room beside one tree and
  one snag, not more. It rots in about four years, sooner where it is warm
  and damp, in about a year standing in a lake or river, and at a quarter of the
  pace while frozen,
  losing height as it goes, then falls as litter. A fire burns a standing snag away, and a tree that fire
  kills leaves a new one. A tree that takes a snag's cell brings it down.
  Over 3 years on seeds 1, 3 and 6 snags left 3-14% fewer trees than the
  flood rules alone, with 1,257-3,298 snags standing at the end.
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

**8 seeds, 6 years each (23,040 ticks), the default temperate climate,
re-run on November 20, 2026 with the flood rules and snags.** Sampled at the
end of each year, the seeds held 72,000-224,000 plants, 1,538-3,765 trees
and 583-4,068 snags. Closed forest covered 6.5-14.7% of the land and rose
and fell. Every seed had fires; single fires burnt 17,000-38,000 cells on
seeds 1, 6 and 7 and about 5,000 on seeds 5 and 8. Seed 7's last-year fire
took its forest from 13.0% to 8.1% and its trees from 2,689 to 1,578.

Against the run before those rules (2,700-5,100 trees, 9-17% forest), the
forests are thinner: trees that stand in water now die, and snags hold
half a tree's room for about four years. Seed 1, whose lakes are still
filling, is the thinnest at 6.5-8.1% forest.

Traits split by habitat (means of the gene, 0-255, in year 6):

| Seed | Drought: dry land / wet land | Roots: dry / wet | Flood: wet land / in water |
| --- | --- | --- | --- |
| 1 | 146 / 117 | 109 / 114 | 77 / 193 |
| 2 | 159 / 117 | 130 / 125 | 68 / 160 |
| 3 | 150 / 116 | 114 / 131 | 59 / 187 |
| 4 | 151 / 135 | 124 / 116 | 59 / 141 |
| 5 | 150 / 120 | 117 / 127 | 60 / 164 |
| 6 | 137 / 111 | 104 / 113 | 84 / 183 |
| 7 | 142 / 118 | 114 / 128 | 63 / 161 |
| 8 | 156 / 125 | 121 / 110 | 57 / 132 |

- Plants standing in water are more flood-tolerant than those on wet land
  on all 8 seeds, by 75-128 points; it was 28-91 before the flood rules.
- Dry-land plants are more drought-tolerant than wet-land ones on all 8.
- Roots are deeper on wet land on 5 of 8, down from 7 of 8, so that
  pattern is weaker than it was.

**The same valley (seed 1) in three climates, after 5 years.** The old sim
is the one before the flood rules and snags, run again for this table.

| Climate | Trees, old / new | Snags | Biomes now (share of the map) |
| --- | --- | --- | --- |
| Cold (0 °C mean) | 1,474 / 1,112 | 5,150 | tundra 29%, bog 13%, taiga 1.1% (was 1.5%) |
| Temperate (9 °C) | 2,929 / 1,887 | 1,831 | scrub 21%, marsh 12%, heath 8%, grassland 1.7% (was 2.8%), deciduous forest 0.9% (was 1.5%) |
| Hot (22 °C) | 2,546 / 2,504 | 763 | scrub 23%, swamp 10%, savanna 7%, desert 4%, rainforest 0.9%, dry woodland 0.9% |

- The three climates still give three different valleys.
- The hot valley is unchanged. The temperate one lost about a third of its
  trees, part of it to a 21,000-cell fire the old run did not have; runs
  diverge once the rules differ, so one seed can't separate the two.
- The cold valley has nearly five snags to a living tree at year 5. They
  are the founding trees: 4,587 had already died by year 2, and in the cold
  a snag stands for six years or more. Run for 12 years, the same valley
  has 5,236 snags at year 6, 2,440 at year 8 and 657 at year 12, with the
  living trees steady at 1,050-1,120.
- The snag counts in these tables are from before two later changes: snags
  now rot slowly while frozen, and in about a year in lakes, where they did
  not rot at all. With that, seed 1 at year 3 has 2,323 snags, not
  3,298: 2,245 on land, 69 in rivers and 9 in lakes. So nearly all snags
  are trees that died on land, not drowned ones.

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
