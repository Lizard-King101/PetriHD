# Petri HD: the plan

**Status:** November 19, 2026. A plan only; nothing is built yet.

Petri HD is a living world on a Raspberry Pi 3 B+ with a 7" touchscreen. Plants
and animals both evolve, and everything they do comes from their genomes and
evolved brains, with no behaviors written in. The world has land, water and
air, days and seasons, weather and fire. The screen is the window into it:
drag to explore, tap any creature to see its body, its brain at work and its
family, and watch species appear, spread and die out.

It is the successor to [Petri](https://github.com/Lizard-King101/Petri), which
asked how much evolution fits on a MatrixPortal M4. Petri HD keeps Petri's
discipline (deterministic integer simulation, matched-seed experiments,
controls, a fixed resource budget) at a budget about a thousand times larger.

## What Petri taught us

These results shape the design. The details are in Petri's
`docs/EMERGENT_LIFE.md`.

| Petri result | What Petri HD does about it |
| --- | --- |
| No meat-eater survived on any of 8 seeds. One diet gene traded plant digestion directly against meat digestion, so hunters starved between kills. | Each kind of digestion is its own organ, with its own upkeep. Being able to eat both costs more but isn't a dead end. |
| Fixed rules saw one cell away and found prey by bumping into it: 1–8 kills per run. | Senses reach far and come from organs: eyes, nose and ears, each with its own range and cost. |
| Random founder brains went extinct within 300 ticks. | A nursery breeds the founders before every world, as in Petri. |
| Founders starved when the senses blurred the cell a step reaches. | Senses include what is directly ahead, at full resolution, as well as the wide view. |
| Evolved brains did about as well as shuffled ones. | A shuffled-brain control and a neutral control run from the first brains milestone, and nothing counts as emergent until it beats them. |
| One founding lineage of 32 won on every seed. | Space, media and seasons give different lineages different ways to win. Species are tracked so their rise and fall can be seen. |

## From Petri

What to bring over from Petri, as of commit `dc03377`. Each port's commit
names the Petri commit it came from.

### Port the code

Small, foundational and already tested:

| What | Where in Petri | Why | Milestone |
| --- | --- | --- | --- |
| Random number generator and hashes | `World::random` (`core/world.cpp`), `terrain_edge_hash` (`render/terrain.hpp`) | A tiny integer generator that accepts any seed, plus a coordinate hash: the core of determinism | 0 |
| Fingerprint and checksum | `World::fingerprint`, the FNV-1a checksum in `core/checkpoint.cpp` | The check that compared the PC with the ESP32 now compares the PC with the Pi | 0 |
| Safe save | The CLI's write-beside-then-rename | Saves survive a power cut | 0 |
| PNG writer | `write_png` in `apps/snapshot.cpp` | About 40 lines, no dependencies: headless screenshots, README visuals, image-comparison tests | 0 |
| Experiment runner | `docs/experiments/compare_*.py`, plus the CLI's `key=value` census line | Runs matched seeds in parallel from a table of treatments and writes CSV | 0 |
| Brain math | `core/brain.cpp`: `think`, `inherit`, `combine`, `turn_for` | The integer network, mutation at 1 in 128 per weight, crossover that takes each hidden unit whole from one parent, and the rotating tie-break. `sense()` is rewritten for the new senses. | 4 |
| Decide, then act | `core/brains_world.cpp` | Everyone decides from the same snapshot, then acts in a rotating order, so decisions can spread across cores and still replay exactly | 4 |
| The two controls | `--neutral-brains`, `--shuffled-brains` | They showed that evolved brains weren't beating chance | 4 |
| Nursery | `apps/nursery.cpp` | Breeds founder families and proves them on seeds they never saw. The method ports; the world it runs changes. | 4 |

### Keep as reference

The ideas carry over; the code is rewritten.

- **Climate and storms:** coarse rainfall nodes every 16 cells, moving storm
  fronts with soil water, and environment updates spread across ticks. The
  starting point for milestone 1.
- **Art:** in `render/panel.cpp`, the pigment palette, body builds, how
  plants lean in storms and sway in the breeze, carcass remains and grazing.
  In `render/terrain.hpp`, the wavy biome edges. They set Petri HD's look.
  The creature art stays generative: every animal is still drawn from its
  genome, with the same pigment and build logic, only with much more detail
  than Petri's LED panel could show.
- **Test patterns:** loading older checkpoint versions
  (`tests/checkpoint_compat.hpp`), and fingerprints compared between two
  platforms.
- **The docs habit:** results tables from matched seeds, gates for each
  phase, and a lab notebook.

### Leave behind

- **Everything specific to the M4:** `firmware/`, `esp32/`, `link/`, the
  render stream, the companion's frame format, `tools/board/`, the diffuser.
- **The memory squeezing:** 9-bit occupancy, 3-byte render cells, and 19
  checkpoint versions with their compatibility code.
- **`companion/`:** the GTK observatory that read the board over USB. The
  touchscreen interface replaces it.
- **The fixed behavior rules:** `update_creature`'s scoring formulas, the
  capture roll in `core/hunting.cpp`, and the scripted care in
  `core/nests.cpp`. In Petri HD these come from brains.
- **The bred founders** (`core/founder_brains*.hpp`): they are matched to
  Petri's senses.
- **The generated watershed table:** it is fixed to 128 × 64. Petri HD's
  rivers come from a height map.

## Hardware budget

The Pi isn't at hand yet, so the budget is set from its specifications and
checked on the PC. When the Pi arrives, it is measured and the budget is
corrected.

| Resource | Raspberry Pi 3 B+ | Petri HD's budget |
| --- | --- | --- |
| CPU | 4 × Cortex-A53, 1.4 GHz, NEON (ARMv8.0: no int8 dot-product instruction) | Simulation on 3 cores, drawing on 1 |
| RAM | 1 GB, shared with the GPU | 256 MB for the whole program: simulation ≤ 64 MB, renderer ≤ 64 MB, GPU ≤ 128 MB |
| GPU | VideoCore IV, OpenGL ES 2.0, textures up to 2048 × 2048 | 60 fps at 800 × 480, never below 30. At most about 4 layers of overdraw. |
| Storage | SD card | A save is a few MB, written every few minutes |
| Display | 7" touch, probably 800 × 480 | Layouts also fit 1024 × 600 |

**Simulation targets:**
- **World:** 384 × 224 cells (86,016 cells, 10.5 × Petri's 128 × 64). The
  whole world at 2 px per cell is 768 × 448, which fits the screen above a
  32 px control bar.
- **Population:** up to 4,096 animals and 65,536 plants, at 10 ticks per
  second at normal speed using no more than half of the three simulation
  cores. The rest goes to fast-forward.
- **Brains:** up to 2 KB of int8 weights per animal, 8 MB for all of them.

**Checking the budget without the Pi:**
- A budget HUD shows tick time, frame time, draw calls and memory.
- The Pi's numbers are estimated as PC time × 8. That factor is an
  assumption, a guess at an in-order A53 against a current desktop core,
  until the Pi has been measured.
- Every build also compiles for aarch64. The tests run under `qemu-aarch64`
  to check that the ARM build, NEON included, gives the same fingerprints.
- Memory is allocated at startup in fixed pools. Nothing is allocated inside
  a tick.

## The world

### Space and movement

The environment is a grid of cells holding terrain, water, soil, climate and
the scent and sound fields. Animals don't live on the grid: each has a
fixed-point position (1/256 of a cell), a heading and a radius that follows
its body size. A spatial hash of 4 × 4-cell buckets answers "who is near".
Continuous movement makes chases, herds, sizes and collisions behave
physically, and gives smooth animation, while the math stays integer and
deterministic. Plants keep grid positions, at most one large or four small
per cell.

### Land and water

- **Scale:** a cell is about 1.5 m, so the world is a valley a few hundred
  metres across, running down to the sea on one side, and creatures are
  roughly one cell long.
- **Terrain:** a generated height map of rolling hills, rocky ridges and
  outcrops, carved by its own streams, with walls on the landward edges.
  The ground is loam, sand, clay, peat, rock or gravel, which sets how it
  drains and what grows.
- **Water:** ocean below sea level; lakes fill basins; rivers follow flow
  accumulation down the height map, which is Petri's watershed idea at full
  scale. Water levels rise in wet seasons and fall in dry ones, so
  shorelines and marshes move.
- **Depth:** every wet cell has a depth, and depth is a real place, not a
  flag. It falls into bands:
  - *shallows*, where legs still touch bottom and waders feed;
  - *open water*, where a land animal must swim;
  - *deep water*, where only swimmers with gills or a large breath store go.

  Animals in water have a depth of their own, from the surface to the
  bottom, so fish can swim under a wading predator or dive away from one.
- **Land animals in water:** they swim slowly on legs, at a high energy cost,
  while their breath store drains. When breath runs out they take damage and
  then drown, leaving a carcass that sinks or washes ashore.
- **Aquatic animals:** fins and gills make them fast and tireless in water.
  Out of it, they flop, dry out and suffocate.
- **Amphibious animals:** not a type but the range between those two. Lungs
  and gills, legs and fins, and breath store are all graded, so a lineage can
  move gradually from water to land or back, paying for each step.
- **Biomes:** not painted on but derived per cell from temperature,
  moisture, height and depth, so they shift with climate:
  - *cold:* ice, tundra, taiga, alpine meadow, bare peaks;
  - *temperate:* deciduous forest, grassland, heath, scrubland;
  - *hot:* rainforest, savanna, dry woodland, desert, salt flat;
  - *wet:* marsh, swamp, mangrove, bog, riverbank;
  - *water:* beach, tidal shallows, reef shallows, lake, river, open sea and
    the deep.

  Each sets which plants can grow, how much cover there is and how fast
  things rot, so different lineages win in different places.
- **Soil:** nutrients per cell, drawn down by plants and returned by dung,
  carcasses and fallen plant matter, through decomposers (see Ecology).

### Climate and time

- **Day and night:** a day lasts 240 ticks. Light sets plant growth and what
  eyes can see, so night favors smell, hearing and glowing signals.
- **Seasons:** a year is 16 days. The valley has one climate, with a strong
  swing between summer and winter; the land makes the local differences.
  Slopes facing the sun are warmer and lose their snow first, cold air pools
  in hollows at night, and water swings less than land.
- **Weather:** moving storm fronts, as in Petri, now with wind direction,
  cloud cover, lightning, fog, drought and snow.
- **Fire:** lightning in dry seasons starts fires. They spread through dry
  plants with the wind and stop at water and bare ground. Burnt ground is
  fertile, so fire drives succession, and plants can evolve to survive or
  exploit it.

### Fields that senses read

These are coarse grids that spread and fade each tick, cheap enough to keep
everywhere:

- **Scent:** several channels: plant, carrion and animal. An animal's scent
  carries a signature from its genome, so kin and species smell alike.
- **Sound:** calls are events with a position, pitch and loudness, heard
  within a range that falls off with distance.
- **Light:** daylight, canopy shade, firelight and glowing animals.

## Flora

Every plant is an individual with its own genome. Plants compete for light,
water and nutrients, and spread by seeds or runners.

- **Growth form:** grass, shrub, tree, reed or water plant, from genes for
  height, woodiness and how many seasons they live, rather than a fixed
  list.
- **Light:** taller plants shade shorter ones. Grass gives way to shrubs and
  then forest unless grazing, drought or fire resets it.
- **Water and roots:** root depth against drought tolerance. Deep roots
  reach groundwater but cost growth.
- **Defense:** thorns and toxins cost growth but deter grazers. Animals can
  evolve a detox organ, which opens an arms race.
- **Reproduction:**
  - Wind-borne seeds, cheap and travelling far.
  - Fruit, which costs more but whose seeds pass through an animal's gut and
    are planted where it goes.
  - Flowers, whose pollen is carried by animals that visit them; a later
    milestone.
- **Seasons:** leaves turn in autumn, dry plants yellow in drought, flowers
  open in spring.
- **Fire:** bark thickness and seeds released only after fire.
- **Structure for animals:** plants are places as well as food.
  - Trees have a canopy that fliers and climbers can roost in, rest in and
    nest in, out of reach of ground animals.
  - Dense shrubs, tall grass and reeds give ground cover.
  - Kelp and water plants give cover underwater.
  - Dead snags and hollow trunks give nest sites.

  How tall, wide and dense a plant grows sets how many animals it can hold
  and how well it hides them.

### Cover and hiding

Cover blocks sight, and only sight. An animal in a canopy, in reeds, in deep
water or in darkness is harder to see by an amount that depends on the cover
and its own size and pigment. Smell, hearing and touch are not blocked.
Hiding is therefore a real defense, and it leaves openings that predators can
evolve to exploit, none of them coded:
- noses that track prey into cover;
- ears that hear it move;
- climbing or flying up to the roost;
- diving after prey underwater;
- night vision for the dark;
- waiting in cover themselves, to ambush.

## Fauna

### The genome builds a body

An animal's genome describes organs, and every organ has mass. Upkeep, speed,
growth time and food needs all follow from total mass, so every ability has a
cost. There are no fixed creature types; land animals, swimmers, fliers and
amphibians are whatever their organs add up to.

| Organ | What it buys | What it costs |
| --- | --- | --- |
| Legs (count, length) | Speed on land, wading | Mass; slow in water |
| Fins and tail | Speed in water | Mass; useless on land |
| Wings (area) | Flight, crossing water and obstacles | Upkeep rises steeply with body mass; animals must land to eat and rest |
| Gills or lungs (or both) | Where it can breathe; time out of its medium drains a breath store | Both cost more than one |
| Plant gut, meat gut, carrion gut | What it can digest | Each has its own upkeep, so eating everything costs more |
| Detox organ | Eating toxic plants | Mass |
| Jaws, horns, claws | Damage | Mass |
| Armor | Less damage taken | Mass, speed |
| Eyes (range, field, color vision) | Seeing; color vision makes camouflage and signals matter | Mass, upkeep |
| Nose | Following the scent fields | Mass |
| Ears and voice | Hearing and making calls | Mass |
| Fat | An energy store for lean seasons | Mass, speed |
| Fur | Cold tolerance | Overheating |
| Light organ | Glowing at night | Upkeep |
| Pigment and pattern | Camouflage or display | Nothing; selection decides |

### Senses

Senses depend on organs. An animal without a nose has no scent inputs.

- **Vision:** the field of view is split into sectors. Each sector reports
  the nearest plant, animal and water: distance, size, color contrast
  against the background, and whether it smells like kin. Color vision adds
  pigment. Range shrinks at night and in fog.
- **Ahead:** the cell the next step reaches, at full resolution: food,
  water, depth, and whoever is there.
- **Smell:** the strength and direction of each scent channel's gradient.
- **Hearing:** the direction, pitch and loudness of the loudest recent call.
- **Touch:** contact on each side of the body and being brushed in cover,
  so an animal can feel what it can't see.
- **Pain:** where and how hard it was just hurt.
- **Depth and medium:** whether it is on land, in water or in the air, its
  depth or height, and whether it is under cover.
- **Body:** energy, health, breath, temperature, age, carried egg.
- **Drives:** see below.
- **Memory:** the brain's own recurrent state.

### Drives

Drives are internal signals, like hormones, that rise and fade with the body
and with what happens to it. They are inputs to the brain like any sense, so
the brain still decides; the drives only tell it about its state, and how
much each one matters is evolved.

| Drive | Rises with | Fades with |
| --- | --- | --- |
| Hunger | Falling energy | Eating |
| Thirst | Time away from water, heat | Drinking |
| Fear | Pain, a larger animal approaching, alarm calls from kin, the scent of a predator's kind | Time spent safe |
| Reward | Eating, mating, a hatched egg, escaping | Decays quickly |
| Mating drive ("lust") | Maturity, good energy, the season | Mating; hunger and fear hold it down |
| Fatigue | Activity, time awake | Rest |

The mating drive gates reproduction: a mate action does nothing unless the
drive is high enough, so animals can't mate their way out of starvation, and
breeding seasons can evolve. The reward signal is also what a later
learning-within-a-lifetime milestone learns from.

### Brain

A fixed-shape network with int8 weights and int16 activations, about 40–64
inputs, 16–32 hidden units with memory, and these outputs:

- turn and speed;
- eat, attack, mate;
- call (with pitch);
- display (flash pigment);
- lay egg, drop carried food;
- rest.

On the Pi, NEON's widening multiply-accumulate does the work. Weights mutate
and recombine as in Petri, where the young take each hidden unit whole from
one parent. Learning within a lifetime is a later, optional milestone.

None of the following is coded; each can only appear from senses, outputs and
costs:

- herding;
- pack hunting;
- ambushes from cover;
- migration with the seasons;
- territorial calls;
- parental care;
- nocturnality.

### Fights

An attack is contact with a lunge. Damage depends on weapon mass, speed and
size against armor. Wounds slow an animal and heal over time; an animal at
zero health dies and leaves a carcass. Fights can be over food, mates or
space, not only predation.

### Reproduction and life history

- **Mates:** sexual, and both must choose to mate. They are compatible only
  if their genomes are similar enough, so species form, split and become
  unable to interbreed without that being coded.
- **Asexual reproduction is rare:** possible only through a gene for it, and
  costly. A clone costs much more energy than a sexual offspring, carries
  more mutations, and the gene lowers fertility. It is a last resort for an
  animal with no mate, not a way of life. Plants can still spread by
  runners.
- **Eggs or live young:** an investment gene. Eggs are cheap and many, but
  they lie in the world where egg-eaters can find them. Live young are few
  and costly.
- **Growing up:** juveniles are small and grow into the adult body. The age
  at maturity is a gene. An animal can sit on eggs or drop food near its
  young, but only its brain decides to.
- **Aging:** upkeep and slow repair rise with age, so lifespan evolves.
- **Disease** comes later: pathogens with their own virulence and
  transmission genes, spread by contact, with immunity as an animal gene.

### Species

Species are detected for display only and never used by the simulation.
Animals are clustered by genetic distance, and each species gets a color and
a generated two-word Latin-style name. The phylogeny records when species
split and when they die out.

## Ecology

- **Decomposers:** fungi as a field that turns dead matter into soil
  nutrients, with visible mushrooms where it is dense. Carcasses rot from
  fresh to bones to gone, feeding scavengers along the way.
- **Algae:** in water, as a field, the base of an aquatic food web for
  filter feeders and grazers.
- **Cycles:** energy flows from sunlight to plants, animals, carcasses,
  decomposers and soil. The observatory shows that flow, so a dead end, like
  meat in Petri, is visible immediately.
- **Coevolution:** fruit and seed carriers, flowers and pollinators,
  toxins and detox.

## Graphics

### Style

Crisp pixel art at integer zooms, with smooth camera movement, smooth
transitions between zooms, and shader lighting. It continues Petri's look,
eight to thirty-two times larger.

| Zoom (px per cell) | View |
| --- | --- |
| 2 | The whole world: biomes, water, forests as texture, animals as dots in species colors |
| 4 | A region: plant sprites and animals as small baked sprites |
| 8 | A valley: animals with heading and gait, eggs, carcasses, tracks |
| 16 | Close: animals with animated limbs, fins and wings, plant detail |
| 32 | Portrait: one animal and its surroundings in full detail |

### Terrain and water

- **Ground:** biome textures generated from noise and dithering, blended at
  the edges. Hill shading comes from the height map, with the sun moving
  through the day.
- **Water:** depth shading by band, from clear shallows where the bottom
  shows to dark open sea, with swimmers dimmed and tinted by how deep they
  are; shoreline foam, rivers drawn flowing along their
  course, ripples when it rains, ice in cold winters.
- **Ground marks:** mud, snow and burnt ground change the surface. Tracks
  stay in mud and snow for a while.

### Light and weather

- **Time of day:** color grading follows the day: warm mornings and
  evenings, blue nights.
- **Night lights:** glowing animals, firelight and eyeshine come from a
  quarter-resolution light map added on top, which is cheap on the
  VideoCore IV.
- **Sky:** cloud shadows drift over the land, rain streaks and snow fall,
  storms darken the screen with flashes of lightning, fog lies in the
  morning.
- **Wind:** plants sway with the wind's direction in a vertex shader, as
  Petri's storms did.

### Plants

Each plant's sprite is generated from its genome:

- **Grass:** blade count, height and color.
- **Shrubs:** shape, leaf color and berries in the fruit's color.
- **Trees:** trunk and layered canopy sized by height. Animals under a canopy
  show as outlines.
- **Water plants:** reeds and floating leaves.

Visible change:
- **Seasons and stress:** autumn color, drought yellow.
- **Life events:** flowers, grazing damage, burnt trunks, dead snags.
- **Growth:** saplings grow into trees.

### Animals

Every animal is generated from its genome, as in Petri: body shape, size,
pigment and pattern, and each organ (legs, fins, wings, eyes, horns, armor,
fur) shows on the body. Petri HD keeps that generator and raises its
fidelity. The art changes with zoom:

- **Overview:** colored dots.
- **Middle zooms:** one sprite per body plan, baked from the genome into a
  2048 × 2048 atlas, cached and rotated to the heading.
- **Close:** a light 2D rig of head, body, tail, limbs, fins and wings,
  animated procedurally:
  - legs swing with speed, tails and bodies undulate in water;
  - wings flap, and fliers cast a shadow on the ground below;
  - juveniles are small, old animals fade, wounds show.

Only animals on screen get rigs, so the cost follows the view, not the
population.

### Interactions on screen

| Interaction | What you see |
| --- | --- |
| Attack | A lunge and a bite flash, hit particles, a limp afterwards |
| Eating | Plants lose leaves; meat and fruit particles |
| Death and decay | A carcass that rots to bones, with scavengers gathering |
| Calls | Rings spreading at the call's pitch color |
| Courtship and display | Pigment pulses |
| Eggs | Nests with eggs; hatching; young that grow |
| Breath | Swimmers surfacing, a breath gauge when inspected |
| Fire | Spreading flames, smoke, ash, then regrowth |

### Overlays

The sense fields are already textures, so a shader can color any of them over
the world with almost no cost:

- **Fields:** scent, temperature, moisture, nutrients, light.
- **Who is where:** species ranges, diet (plant, meat, mixed), lineage
  colors.
- **The selected animal:** what it sees, its senses' cones and sectors, and
  its recent path.

### How it stays inside the GPU budget

- **Batching:** sprites are batched into few draw calls with one atlas.
- **Static terrain:** the terrain is drawn into cached chunk textures,
  redrawn only where something changed.
- **Cheap overlays:** fields upload as small textures and are colored in
  the shader.
- **Smooth motion:** the renderer animates between ticks from the two latest
  snapshots.
- **No instancing:** OpenGL ES 2 has none, so vertex buffers are rebuilt
  each frame for what is on screen only.

## Interface

The screen is 800 × 480 and touch-first; a mouse does the same on the PC.

```
┌───────────────────────────────────────────────────────┬────────────┐
│                                                       │  drawer:   │
│                     world view                        │  inspector │
│              drag · pinch · tap · long-press          │  species   │
│                                                       │  graphs    │
│                                                       │  chronicle │
├───────────────────────────────────────────────────────┴────────────┤
│ ⏸ ▶ ▶▶ ▶▶▶  Day 41 · spring · noon   2,431 animals · 38 species  ☰ │
└────────────────────────────────────────────────────────────────────┘
```

- **Gestures:**
  - drag to pan, pinch to zoom;
  - tap an animal or plant to inspect it;
  - long-press a cell for its soil, water and climate;
  - double-tap to follow an animal.
- **Inspector:**
  - a live portrait;
  - its species name and number;
  - bars for energy, health, breath and age;
  - an organ diagram and its diet;
  - its brain, with inputs, hidden units and outputs lighting up as it
    decides;
  - its family (parents, mate, offspring);
  - a short life log: where it was born, what it ate, its fights, its young.
- **Species page:**
  - its range on a mini-map;
  - population over time;
  - its place in the phylogeny;
  - a typical member.
- **Observatory:**
  - population by species and by diet;
  - the energy-flow diagram;
  - the phylogeny tree;
  - gene distributions;
  - the emergence measures below.
- **Chronicle:** a feed of notable events, each with a link to the place and
  time:
  - a new species;
  - an extinction;
  - the first flier, swimmer or meat-eater species;
  - booms and crashes;
  - great fires and storms.
- **New world:** seed, size, sea level, climate, and which founders to use.
- **Text:** a built-in bitmap font sized for the 7" screen.

## Measuring emergence

Petri's method continues: headless matched-seed runs, controls and gates.

- **Headless runner:** `petrihd-run` runs worlds without drawing, as fast as
  the PC allows, and writes census files and fingerprints.
- **Controls:**
  - shuffled brains (weights permuted at birth);
  - a neutral world (parents chosen at random, not by fitness).
- **Measures, recorded every few hundred ticks:**
  - species count and phylogenetic diversity;
  - trophic levels: meat-eaters' share of animal biomass, and how many
    links a food chain has;
  - herding: how closely neighbors' headings agree, and how they cluster;
  - predator–prey cycles: the lag correlation between their populations;
  - evolutionary activity (Bedau–Packard): whether new adaptations keep
    appearing or the world settles;
  - how varied behavior is: how time is split between moving, eating,
    fighting, calling and resting.
- **Gates:** each milestone states what it must show against its controls on
  8 matched seeds before the next starts.

## Architecture

- **Language and dependencies:** C++17, CMake, SDL2. Graphics through a thin
  OpenGL ES 2 layer: KMS/DRM and the VideoCore IV on the Pi, a GLES or GL 2.1
  context on the PC. Single-header libraries are vendored if needed. No other
  dependencies.
- **Layout:**
  - `core/`: the simulation, deterministic, no dependencies;
  - `gfx/`: the renderer;
  - `ui/`: the interface;
  - `apps/`: `petrihd`, `petrihd-run`, the nursery;
  - `tests/`, `tools/`, `docs/`.
- **Tick:**
  1. Environment: fields spread and fade, water and climate update. The
     slow parts run on a rotation.
  2. Plants, a share of them each tick, staggered by ID.
  3. Sense and decide, in parallel across three cores, reading only the
     state at the start of the tick.
  4. Act, in a fixed order: moves, attacks, eating, mating.
  5. Metabolism, growth, births, deaths and decay.
  6. The fingerprint and the snapshot for the renderer.
- **Determinism:** integer and fixed-point math, one seeded generator per
  purpose, and a fixed order wherever results are combined. A world plays
  out the same on the PC, the Pi and any number of threads.
- **Memory:**
  - structure-of-arrays pools, sized at startup;
  - slot indices of 16 bits;
  - the renderer reads double-buffered snapshots, never the live world.
- **Saves:** versioned binary files, compressed, written to a temporary name
  and renamed, so a power cut never leaves a half-written save.

## Milestones

Each ends with something that runs and a gate that it must pass.

| # | Milestone | Done when |
| --- | --- | --- |
| 0 | **Foundation.** Repo, CMake, an 800 × 480 SDL2 window with mouse-as-touch, the budget HUD, the deterministic core skeleton, fingerprint tests, the headless runner, the aarch64 build under qemu. | An empty world ticks at the same fingerprints on x86 and aarch64, with the HUD showing its cost. |
| 1 | **Land and water.** Terrain, rivers and lakes, climate, day and night, seasons, weather; the terrain renderer and camera. | You can explore a generated world through a full year at 60 fps. |
| 2 | **Plants.** Plant genomes, light and water competition, seeds, succession, fire; plant sprites, wind and seasons. | Forests, grasslands and marshes form and shift with climate and fire, and plant traits differ by region. |
| 3 | **Bodies.** The organ genome, movement in each medium, metabolism and eating; animal sprites and rigs; the inspector. | Land animals, swimmers and fliers built from organs move and feed correctly. |
| 4 | **Senses and brains.** Vision, smell, hearing, the scent and sound fields, the NEON brain, the nursery, both controls. | Worlds last on all 8 seeds, and evolved brains beat the shuffled control on at least one measure. |
| 5 | **Life history and species.** Two-parent mating, compatibility, eggs and live young, growth, aging; species detection, names, the phylogeny. | Species split and go extinct, and some species live on meat. |
| 6 | **The observatory.** Graphs, the energy flow, the phylogeny view, the chronicle, the emergence measures. | A long run tells its own natural history. |
| 7 | **On the Pi.** Full-screen KMS/DRM, touch and pinch, start at boot, read-only system, measured budgets. | The Pi boots into the world, holds the budget, and survives having its power pulled. |
| 8 | **Deeper ecology.** Decomposers you can see, disease, pollination, toxins and detox, learning within a lifetime. | Each lands only if it passes its own gate. |

Milestone 7 moves forward as soon as the Pi is at hand. Milestone 0's ARM
build keeps everything ready for it.

## Decisions

Each has a recommended default; the plan assumes it unless you choose
otherwise.

| Decision | Recommended | Alternative |
| --- | --- | --- |
| Repo name | `PetriHD` | `petri-hd` |
| Start from Petri's code or fresh | Fresh, porting the pieces listed in [From Petri](#from-petri). The world model (continuous positions, plant individuals, organs) is too different to fork. | Fork Petri and keep its history |
| Animal movement | Continuous fixed-point positions | The grid, as in Petri |
| View | Top-down, animals rotating to their heading | A three-quarter view, with 8-direction sprites, which costs more art |
| Art | Pixel art at integer zooms with smooth camera and lighting | Smooth vector-style art |
| Windows builds | Yes, through SDL2 with a desktop GL context, since you have built Petri on Windows | Linux only |
| The MatrixPortal | Not part of Petri HD for now; a mini-map on the LED panel could come later | Retire it |
