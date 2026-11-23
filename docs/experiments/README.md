# The first nursery (a record)

These are the notes and data of the first nursery, which trained only the
large swimmer's brain. It was replaced by [the nursery](../NURSERY.md), which
trains every kind of founder in arenas. The programs described here
(`train`, `lesson2`, `curriculum`, `pilot`, `trial`, `pool-trial`,
`feeding-probe`, the replay viewer and the comparison scripts) are no longer
in the tree; they are in the repository's history at commit `cb33c70`. The
saved ponds under this directory still load.

What it found is worth keeping:

- On the development map no candidate ever bred, so its ranking had nothing
  to choose between, and the random-parent control did better than the
  selected brain.
- In the pond, eleven of sixteen brains tied at the top score.
- Selecting for low food and energy per birth bred lean brains that failed
  sooner than the unselected ones as food grew scarce.

## The large-swimmer nursery

### Lesson 1 in the training pond

Each trial starts two compatible, healthy adults one cell apart at 60%
reserves and zero mating drive. The 18 by 10 cell pond has a 16 by 8 cell
swimming area, two metres of still freshwater, a temperature of 18 °C and
fixed light of 200/255. Every wet cell carries a short reed with 600 edible
energy restored before each tick. Feeding consumes actual bites. The
simulation runs the usual brain, movement, metabolic and reproduction rules.

One swimmer carries the candidate brain. The reference carries the
hand-wired founder brain with its pace output set to minimum so it stays
by the food. Its other decisions come from its own network. A pairing of
two references must produce a birth on at least three of four calibration
seeds (7301–7304) before scoring starts. Initial headings change by seed.
The candidate occupies the left and right bodies twice each per panel.

Each trial ends at its first candidate-parented birth or at tick 960.
The ranking compares these totals across the same four training setups
(7311–7314), in order.

1. Setups with a birth. Either parent role counts and a brood counts once.
2. Mean timing. At either founder's metabolic check while the pair is strictly within 1.25 cells, take the lower drive before a birth resets it. Divide the best value, capped at 200, by 200.
3. Mean feeding. Count the candidate's metabolic checks above 62.5% reserves, cap at 25 and divide by 25.

The first four distinct brains in rank order are retained. Exact ties
use the original pool order. Each retained brain earns promotion after
births on at least three of four setups in **each** fresh block (7321–7324,
7331–7334). The weights stay fixed throughout all three panels. The
trial log also records actual bite counts, parent IDs, and movement and
calling energy spent after the first ready tick through the final tick.

```sh
cmake --build build --target petrihd-nursery
build/petrihd-nursery train build/lesson1 4 --visual
```

Open `build/lesson1/nursery.html`. Choose a brain and a trial to replay.
The gold swimmer is the candidate and the green swimmer is the reference.
Both have a bite counter and energy and drive gauges. **First readiness**
and **First birth** jump to the exact recorded tick and pause. At a birth the
viewer holds the final frame and shows the drive recorded before reset.
Every replay is re-simulated and checked against its scored fingerprint.
Clean view, full screen, scrubbing and clip recording work offline.

The output includes an editable `pond.cfg`, rankings, individual trials
and four tick-zero donor saves. The first swimmer in each donor carries
the retained candidate. Pass `--preset path/to/pond.cfg` to a later batch
to set dimensions, depth, temperature in tenths of °C, light, food per
cell, starting energy, spacing and time limit.

In batch 1, the reference pair bred on all four calibration setups.
Eleven of sixteen brains tied on four births, maximum timing and maximum
feeding. The four retained brains all went 4/4 on both validation blocks.
The remaining seven tied brains were outside the four retention slots.
[Rankings](lesson1-batch1/rankings.csv) · [Trial records](lesson1-batch1/trials.csv) · [Preset and retained brains](lesson1-batch1)

**Validation of all eleven tied brains.** The seven additional brains produced
young on all 56 setups. Replaying the original four reproduced every field
and fingerprint in their 32 archived trials. All eleven scored 4/4 on
both validation blocks with maximum feeding and timing. In every pair one
swimmer reached drive 200 on tick 398 and the other on tick 399, reflecting
their staggered metabolic checks. The earlier swimmer incurred zero energy charges during
that tick. After both were ready, the pairs started 10,149 of 10,665
ticks (95.2%) outside mating reach.

The energy records cover the tick after the first swimmer became ready
through the birth tick, inclusive. The log attributes actual swimming,
calling, striking and basal upkeep charges to each swimmer. All strike
charges were zero in this batch. Food intake and energy transferred to
offspring are separate from expenditure. The per-seed records also give
each swimmer's charges while it alone was ready, and while both were
ready, using their drives at the start of each tick.

Across the eight matched seeds, slots 1, 14, 11 and 15 had the lowest
mean total expenditure for the pair. Counting swimming and calling alone
gave slots 1, 15, 11 and 8. Slot 14's pair averaged 56 ticks from first
readiness to birth, and slot 8's averaged 154.
[Per-brain means](lesson1-batch1/validation-ties-summary.csv) · [All 88 paired trials](lesson1-batch1/validation-ties-trials.csv)

```sh
python tools/compare_lesson_one_ties.py build/petrihd-nursery docs/experiments/lesson1-batch1
```

### Lesson 2 in the same pond

Slots 1, 14, 11 and 15 entered with their Lesson 1 weights and the same
reference partner (`9ec731457bd432de`). The 76,800 edible energy available
at the start of each tick was distributed over 16 cells in three reed
beds. Both swimmers start on the middle 2 × 2 bed. A 3 × 2 bed lies on
each side across three cells of open water. Each planted cell replenishes
to 4,800 edible energy per tick. The preset accepts one
`food_patch=x,y,width,height` line per bed, in map-cell coordinates.

The batch ran the same eight seeds (7321–7324 and 7331–7334) in each
layout with matching headings, candidate sides and a 960-tick limit.
All 32 uniform controls reproduced the archived Lesson 1 trials, including
their fingerprints. The reference pair bred on all four patch calibration
seeds. All four candidates bred on 8/8 patch trials and each ate on a
side bed in every trial. The reference took all its bites on the middle bed.

| Pool slot | Within mating range, uniform → patches | First readiness to birth, mean ticks | Pair energy in that interval | Whole-trial pair energy per birth |
|---:|---:|---:|---:|---:|
| 1 | 23.9% → 34.5% | 28 → 38 | 1,281 → 1,681 | 17,670 → 17,799 |
| 14 | 18.6% → 30.2% | 56 → 59.9 | 2,549 → 2,724 | 18,927 → 19,135 |
| 11 | 34.3% → 45.2% | 78 → 54 | 3,061 → 2,199 | 18,138 → 17,193 |
| 15 | 31.4% → 40.9% | 88 → 66 | 3,238 → 2,791 | 19,402 → 18,782 |

Proximity is the fraction of ticks that began with both founders alive
and strictly less than 1.25 cells apart, pooled across the eight seeds.
The summary also reports proximity specifically while both were ready.
Energy includes swimming, calling, strikes and basal upkeep. The
first-readiness window starts the tick after either swimmer reaches drive
200. Whole-trial energy includes all attempts from tick zero, divided by
the number of trials that produced a first birth. Slot 11 used the least
whole-trial energy in the patch pond. Slot 1 reached birth fastest once
either swimmer was ready.
[Paired results](lesson2-batch1/summary.csv) · [Patch trials](lesson2-batch1/trials.csv) · [Uniform controls](lesson2-batch1/controls.csv) · [Preset, calibration and four donor saves](lesson2-batch1)

```sh
cmake --build build --target petrihd-nursery
build/petrihd-nursery lesson2 build/lesson2 docs/experiments/lesson1-batch1/pond.cfg docs/experiments/lesson2-batch1/pond.cfg 1 14 11 15 --visual
```

Open `build/lesson2/nursery.html` to replay any of the 32 patch trials.
The first swimmer in each `slot-N.phdw` is the selected brain, saved at
tick zero on seed 7321. Loading the save restores the planted cells and
their replenishment.

### Selective breeding with roaming partners

Lessons 1 and 2 screened existing brains and supplied the four starting
parents. The next exercise breeds their descendants. Each round tests
both parents and offspring for births, family survival, food eaten and
energy spent. Four selected brains seed the following round in the same
pond. The simulator keeps weights fixed during each animal's lifetime.
[How selection works, saved breeders and measured limits](peer-breeding-batch1/README.md)

### Earlier development-map pilot

Petri's original nursery tested inherited brains in fresh worlds. The HD
pilot gives eight large-swimmer founders a candidate brain and lets all
56 animals live, feed and breed under the development map's usual rules.
The founding bodies, positions and starting energy match across treatments
on each seed. Offspring inherit and mutate their brains normally.

The nursery starts with sixteen candidate brains, including the hand-wired
one. It supplies variation in appetite and cruising pace, and applies four
to twelve passes of the existing weight mutation. Each candidate gets up to
three years on each of seeds 7001–7003. Four candidates become parents of
the next pool through hidden-unit crossover and mutation. The four rounds
evaluate 52 candidate slots in 156 training worlds. The random-parent
control starts with the same brains and receives the same crossover and
mutation draws, with parents retained by a random draw.

Selection compares seven totals across the three training seeds, in the
following order. The first unequal total decides which candidate ranks higher.

1. Maps where a wild-born large swimmer matured and both fish populations remained at the end.
2. Maps with a grandchild or later generation of large swimmers.
3. Maps where a wild-born large swimmer matured.
4. Maps with a large-swimmer birth.
5. Maps with a large swimmer that bred after eating from its own kill.
6. Number of large-swimmer young, capped at eight per map before summing.
7. Each map contributes 200 if a birth occurred. Otherwise it contributes the highest observed drive of the less-ready member of a compatible adult pair within 1.25 cells, sampled every 16 ticks.

The highest-ranked brain was discovered in the
initial pool and retained for all four rounds. None of the training
candidates produced young.

After selection, each treatment ran for 40 years on the same eight fresh
seeds, 7101–7108. A recruit is an offspring that reached adulthood.

| Founder treatment | Seeds with births | Young born | Adult recruits | Seeds with large swimmers at year 40 | Seeds with small fish at year 40 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Hand-wired | 0/8 | 0 | 0 | 0/8 | 5/8 |
| Nursery-selected | 0/8 | 0 | 0 | 0/8 | 7/8 |
| Random-parent control | 2/8 | 3 | 1 | 0/8 | 4/8 |

On seed 7101, a control swimmer that had eaten from its own kill became
a parent at tick 3122. Two young were born and one reached adulthood.
The third young was born on seed 7104. No grandchildren were recorded.
These releases establish that unassisted breeding and maturation are
possible on this layout. The selected family did not establish a
self-sustaining population. [All 24 release censuses](swimmer-nursery-40y.csv)

### Mixed pools, drive loss and food patches

These are three-year trials of fixed brains on a fresh block of seeds,
7201–7208. A is the pilot's selected brain (`3f1ad7902b69fe75`), B is the
random-parent control (`eb80ad9d6ccc4a3b`), and the helpers use the
hand-wired large-swimmer brain. Each seed has a pure pool for each brain
and four mixed assignments. Rotating the brains puts A and B in every
founder slot once. Bodies, homes and starting reserves are matched within
a seed. Each newborn records both parent IDs.

On the full development map, the mixed pool has two A, two B and four
helpers. In the table, a tick counts once if any two compatible grown
swimmers qualify. Observations are taken at the end of every tick before
the first birth. The drive cutoff is strictly greater than 100. Mating
reach is strictly less than 1.25 cells.

| Drive loss below half energy | Mean ticks both above 62.5% energy | Mean ticks both above 100 drive | Of those ticks, within mating reach | Trials with a birth |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 314 | 477 | 48 | 0/32 |
| 8 | 314 | 535 | 51 | 0/32 |
| 4 | 314 | 638 | 52 | 0/32 |

Across all 168 full-map trials, including the pure pools, no compatible
pair was observed with both drives at 200 or above and no large swimmer
was born. The experiment sets the loss for every animal in a world.
The ordinary world uses a loss of 16. Energy between 50% and
62.5% holds drive steady. The 25 well-fed checks needed to rise from 0 to
200 can be separated by time in that band. [Full-map trials](swimmer-pool-drive-3y.csv)

The food comparison kept only the first four large swimmers from each
lake. The stocked treatment filled empty plant slots in the lake water
within one cell horizontally and vertically of each founder with short
reeds holding 600 energy apiece. This was a one-time planting. Water
levels, grazing, birth costs and hungry drive loss followed the normal
rules. Each mixed pond contained one A, one B and two helpers.

| Four-swimmer pool | Unstocked trials with a birth | Stocked trials with a birth | Young in stocked ponds | Young reaching adulthood |
| --- | ---: | ---: | ---: | ---: |
| All A (selected) | 0/8 | 0/8 | 0 | 0 |
| All B (random-parent) | 0/8 | 8/8 | 30 | 4 |
| All hand-wired | 0/8 | 8/8 | 18 | 1 |
| Mixed, four rotations per seed | 0/32 | 32/32 | 55 | 3 |

A directly parented 3 of the 55 mixed-pool young and B directly
parented 37. Each family gets credit once per newborn if either parent
belongs to it. None of A's mixed-pool descendants matured. No pond
produced a grandchild. The four rotations are repeated assignments on
the same eight seeds. [Paired pond trials](swimmer-pond-food-3y.csv)

A separate feeding probe ran four hand-wired founders in each stocked
pond for up to one year. At each founder's four-tick decision interval,
it replayed the same end-of-tick sensory inputs, including memory, through
A, B and the hand-wired brain. Across 3,485 samples with food in the
current cell and reserves above 62.5% through 75%, A chose eat once, B
chose it 3,485 times and the hand-wired brain 878 times. Below half energy
all three chose eat on all 332 samples. These counts describe decisions
on the hand-wired fish's trajectories. [Feeding decisions by seed and energy band](swimmer-pond-feeding.csv)

To reproduce, use the donor saves that the four-round trainer writes.

```sh
python tools/compare_nursery_pools.py build/petrihd-nursery build/nursery/round-4.phdw build/nursery/random-parent.phdw --jobs 4
python tools/compare_nursery_pools.py build/petrihd-nursery build/nursery/round-4.phdw build/nursery/random-parent.phdw --jobs 4 --pond
build/petrihd-nursery feeding-probe build/nursery/round-4.phdw build/nursery/random-parent.phdw 7201
```

The four-adult food comparison motivated the two-adult Lesson 1 pond.
The original four-round selection rule and release trials remain
available through the `pilot` command.

### Reproduce the development-map pilot film

```sh
cmake --build build --target petrihd-nursery
build/petrihd-nursery pilot build/nursery 8 --visual
```

Open `build/nursery/nursery.html` as soon as it appears. It picks up new
chapters as rounds finish. Each round shows the strongest new challenger
and, when it differs, the retained family on the same seed. The final two
chapters show the selected and random-parent families released on seed
7101. Once training finishes, the HTML contains the entire replay and
can be copied by itself.

Land elevation and vegetation depict the habitat at founding. The replay
samples water depths every half day and at the first birth, so the camera
can follow a swimmer onto the floodplain. Fish positions, headings, energy,
drive, kills and births come from the simulation. Poses are sampled every
six ticks and interpolated for playback. Training chapters cover up to
2400 ticks and release chapters up to one year. A chapter ends early when
its last large swimmer dies. The 40-year census covers the full run.

Click a fish to inspect it, choose **Follow fish** for a close-up, or use
**First birth** to jump just before a recorded birth. **Clean view** (C)
hides the overlay. **Record clip** starts a 1280 × 720 recording from
the canvas and **Save clip** downloads it. Chrome and Edge use H.264 MP4
when their encoder supports it, with WebM available as a fallback. Space
pauses, F toggles full screen, and R finishes a recording.

For headless training, omit `--visual`. Each round writes a normal
tick-zero world save. To release the selected brain on a different seed,
use `build/petrihd-nursery trial build/nursery/round-4.phdw 7101 40 start.phdw`.
Open `start.phdw` with the regular windowed app to watch the release there.

