# The nursery

Founders need brains that can feed, find each other and raise young before
evolution has had a chance to give them any. The nursery breeds those brains
in arenas, where everything is set by a dial, and raises the difficulty a
level at a time. The real world is the final exam and is never used for
training.

```sh
cmake --build build --target petrihd-nursery
build/petrihd-nursery train build/nursery          # all five kinds, every level
build/petrihd-nursery budget                       # each level's food against need, and the time a trial takes
build/petrihd --dev-map --founders build/nursery/founders.phdb
build/petrihd-run layout=development animals=56 founders=build/nursery/founders.phdb ticks=38400 every=0
```

`train` writes into its directory:

- `nursery.html`: the progress page. It reloads itself while training runs.
- `founders.phdb`: a pool of brains for each kind, from that kind's best family.
- `champions/<kind>-<level>.phdw`: each kind's best family at tick zero of its
  highest level, to open in the window with `build/petrihd --load`.
- `rounds.csv`: every round of every family.

Options: `--kinds grazer,fish`, `--levels N`, `--families N`, `--rounds N`,
`--jobs N`, `--seed N`, and `--quick` for a small run that takes seconds. The
result is the same for any number of threads.

## Arenas

An arena (`Layout::arena`) is a flat world with no weather, fire or plant
succession. Its dials are in `WorldConfig`: size, how deep its water is, how
many columns of dry ground lie along its west side, where fodder is planted
and how much a cell holds, how fast it grows back, and whether there is day
and night. Animals in it live by the ordinary rules: the same senses, brain,
movement, metabolism, breeding and predation as in any world.

The nursery builds an arena for a kind from a level's dials
(`apps/nursery_levels.hpp`). Food is scaled to the body: a level grows a set
multiple of what its founders burn at a full walk or swim, and a cell holds a
set number of that body's mouthfuls. So a level asks the same of a small
fish as of a large swimmer, and none is short of food by construction.
Founders are varied bodies of the kind, as in a real founding, put down
anywhere they can live.

## The ladder

| Level | Arena | Gate, on 3 of 4 unseen seeds unless stated |
| --- | --- | --- |
| 1 Feed | 12 × 8, food in every cell, refilled every tick, 4 founders | fed on 90% of metabolic passes, none starve, all 4 seeds |
| 2 Forage | 24 × 16, six 2 × 2 patches growing 4× what the founders burn | fed on 70%, none starve |
| 3 Pair | 20 × 14, every cell planted, 6× need, 6 founders | a birth |
| 4 Raise | 24 × 16, eight 3 × 3 patches, 5× need, for 2.2 times as long as the kind takes to grow up | two young grown up, as many alive as founded |
| 5 Lean | 36 × 24, ten patches, 2.5× need, day and night, three columns the body cannot live on, 8 founders, 3 growing times | the same, and grandchildren |

A family moves up when four of its best eight brains pass the gate.

## The score

A trial gives one brain to every founder in an arena; their young inherit it
as young do in any world. A watcher (`StepObserver`, `apps/nursery_score.hpp`)
follows the family and scores five things, each from 0 to 1, in the order of
a life:

`1·fed + 2·ready + 4·bred + 8·grown up + 16·lasting`

- **fed**: the share of the founders' metabolic passes spent above half energy;
- **ready**: the mating drive the second-readiest founder reached;
- **bred**: births, against the founding number;
- **grown up**: young born there that reached adulthood;
- **lasting**: how many are alive at the end, against twice the founding number.

A brain that cannot yet breed is still told apart by how well it stays fed,
so selection always has something to go on. Feeding saturates, so gorging
earns nothing: Petri's nursery found that scoring raw feeding bred foragers
that never mated.

## The search

Each kind has four families, bred apart and never mixed. Two start from the
hand-wired brain and two from the best of 512 random brains. Each round,
64 brains meet three new seeds; the best 16 are kept, with all their earlier
trials counting, and the rest are replaced by their young (crossover of
hidden units, then a variation coarser than the small steps a birth makes).
The best eight are tried at the gate on four seeds no brain was bred on. A
family that has not passed after 25 rounds is called stalled, and the page
says what its best brain could not yet do.

## First run

59,448 trials in 89 seconds on 24 threads
([rounds](experiments/nursery-run1/rounds.csv), [founders file](experiments/nursery-run1/founders.phdb)).

The hand-wired brain at each gate, in seeds passed out of four:

| Kind | Feed | Forage | Pair | Raise | Lean |
| --- | ---: | ---: | ---: | ---: | ---: |
| Grazer | 4 | 4 | 4 | 4 | 4 |
| Crawler | 4 | 2 | 4 | 4 | 3 |
| Flier | 4 | 0 | 4 | 1 | 0 |
| Small fish | 4 | 3 | 4 | 4 | 4 |
| Large swimmer | 4 | 4 | 4 | 4 | 4 |

- **The flier was the kind that needed training.** Its hand-wired brain
  takes to the air whenever no food is in sight and starves among patches.
  The two families started from it took 14 and 5 rounds to pass Forage and
  then passed every later gate at the first attempt.
- **Random brains can be bred to forage.** Of the ten families started from
  random brains, seven passed every level, most after 3 to 20 rounds at
  Forage. Both flier families and one fish family stalled there.
- **The large swimmer's hand-wired brain passes every gate.** Given food in
  proportion to its body it feeds, breeds and raises two generations. Its
  failure on the development map is therefore not a failure to feed or to
  mate as such.

### In the wild

Ten years on the development map, 56 founders, eight seeds the nursery never
saw (7101–7108), with the nursery's founders against the hand-wired ones
([every seed](experiments/nursery-run1/wild-10y.csv)):

| Founders | Seeds with fliers | Fliers, median | Seeds with over 100 small fish | Walkers, median | Large swimmers |
| --- | ---: | ---: | ---: | ---: | ---: |
| Hand-wired | 7 of 8 | 10 | 5 of 8 | 125 | none on any seed |
| Nursery | 8 of 8 | 220 | 6 of 8 | 140 | none on any seed |

This is one comparison, without the shuffled and random-parent controls, so
it shows the trained founders carry over to a world they were not trained
in, and no more than that yet. Walkers fell below 60 on three of the eight
seeds with the nursery's founders, and on none with the hand-wired ones.

**Large swimmers still die out on every seed.** The arenas so far do not
contain what they meet in the lake. The likely causes, not yet measured:
they share thin, slowly growing weed with small fish that breed to
hundreds; by the plant rules a cell of real weed holds only a few of their
mouthfuls, where an arena cell holds 16 to 24; and the earlier predation
runs recorded them killing 10 to 15 of the sixteen founding fish within two
years.

## What comes next

1. **Thin food**: a level with every cell planted but holding only a few
   mouthfuls and growing at the rate of real plants, so a large body has to
   keep moving to stay fed.
2. **Company**: arenas shared with other kinds carrying their own trained
   brains: competitors for all, prey for the large swimmer and the large
   swimmer for small fish.
3. **Graduation**: forty-year development-map runs against three controls
   (hand-wired, the trained brains shuffled, and a search with random
   parents), with the lake's energy budget set against what its fish burn.
   If the large swimmer passes Thin food and Company and still dies out in
   the lake, that budget will say whether the habitat can carry it at all.

## The hunting school

`build/petrihd-nursery hunt build/hunters.phdh --rounds 40 --days 30 --jobs 8`
breeds brains for varied land and water hunters, against breeding herds.
`--days` is a minimum: each arena lasts at least two juvenile growth periods
plus time to become ready to mate. Its last quarter tests whether an early
feeding or birth boom lasts.

Hunting trials measure the costs of every founder and descendant: age-adjusted
upkeep, stamina recovery, calls, growth, healing and reproduction. A newborn's
reserves transfer within the family; its tissue and parental effort are charged
once. Meals record plant energy, scavenged meat, and meat from the family's own
kills separately. The denominator grows with the family instead of remaining
the estimated needs of four founders.

A trial passes only when all of these hold:

- The family reaches the end without killing any of its own members.
- Meat from its own kills covers all measured costs, both over the whole trial
  and during its final quarter.
- Own-kill meat supplies at least 80% of absorbed food in both periods.
- At least four offspring are adults still alive at the end, replacing the
  four founders; at least eight adults are alive in total.
- The adult count has not fallen since the final quarter began.

Selection rewards surviving adult offspring and sustained adults in proportion
to how well hunting pays for them. Births alone earn little, dead young earn no
replacement credit, and grazing or scavenging cannot substitute for hunting.
Reports show food sources, actual hunting cost coverage, hit rate, adult
replacement, pass counts and the first unmet requirement of each failed trial.

A brain graduates after passing at least 75% of land trials and 75% of water
trials in both the selection arenas and the separate final exam. Up to four
graduates are written to the requested file. The best four candidates are also
saved to `FILE.candidates.phdh` for further investigation. If none graduate, the
command exits unsuccessfully and leaves any existing requested output intact.
Candidate files and previously bred pools are not evidence of passing these
new gates; existing embedded brains are not replaced by running this command.

## For whoever changes the simulation

The founders file records the shape of the network it was bred for and is
refused by a build with another. After a change to the senses, the network
or the rules animals live by, breed the pools again.

Measurement lives outside the simulation. `World::step` takes an optional
`StepObserver` and steps the same, to the bit, with or without one; tests in
`tests/nursery_test.cpp` hold it to that. The first nursery, which trained
only the large swimmer, is recorded under [experiments](experiments/README.md).
