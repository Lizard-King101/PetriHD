# Animals

The start of milestone 3 (bodies). Animals are in the simulation
(`core/animals.hpp`, `core/animals.cpp`): each has a genome of organs, a
body those organs add up to, a place in the world, and a metabolism. They
sense the world, decide with a brain they inherit, breed, grow up and grow
old.

Run `build/petrihd --dev-map` to watch them. A new world starts with fifty
bodies nobody designed (`core/founders.cpp`): ten families of five
relatives, each drawn at random for a diet and a place to live, six animals
of each. Those built to live on meat are held back. A pair of each arrives
in year 8, once there is something to hunt, with brains bred in the
nursery's hunting school. `--classic` starts with the five designed
founders instead. A world without animals behaves, and fingerprints,
exactly as before.

## The genome builds a body

Twenty-six genes (`AnimalGene`): size and build, pairs of legs and their
length and sprawl, fins, tail, wings, neck, gills or lungs, a plant gut and
a meat gut, fat, pigment and pattern, how fast it grows up, how many
young it has at a time, how far it sees, how keen its nose is, how far it
hears, the pitch of its voice, and how hard its mouth, the front of its
head and its back are. `frame()` turns them into a body:

- **Mass.** The trunk is a cylinder of flesh; legs, wings, fins, neck, tail,
  guts and fat are all counted against it. Upkeep and the size of a
  mouthful follow mass to the power 3/4.
- **Speed in each medium.** Legs under the body stride further than legs out
  to its sides. Fins and a tail drive a swimmer; legs alone only paddle.
  Wings fly only if they are big enough for the mass they must lift.
- **Breath.** Lungs, gills or both. Where it cannot breathe an animal holds
  its breath for a minute of world time, then is hurt, then dies.
- **Wading.** How deep it can stand follows its legs.

There are no kinds of animal. The names on screen (`core/taxonomy.cpp`)
are made up from what each body is, and are not part of the simulation.

## Satiety, energy and sleep

An animal has two reserves.

- **Satiety** (`Animal::energy`) is its food: slow to fill and slow to
  empty. Upkeep, growth, healing and young are paid from it, and it starves
  when it runs out.
- **Energy** (`Animal::stamina`, its wind) is what moving spends. A lean
  body holds more of it than a fat one. Walking draws on it slowly, and a
  body with none left can only trudge. Above a cruise it sprints, up to
  twice the speed its frame gives, and spends it fast. Every tick in the
  air costs some: a flier needs a quarter of its wind to take off and comes
  down when it is spent. A lunge takes a little.
- Energy comes back out of satiety, each metabolic pass, while the animal
  is on the ground or in the water and not sprinting. That refill is what
  moving costs in food; moving costs nothing else.
- **Sleep** is an output of the brain. One lying still does not move or
  strike and sees half as far. It burns a quarter less, gets its energy
  back twice as fast at half the price, and heals twice as fast.
- **Healing.** A fed, breathing animal closes its wounds slowly, and pays
  for it.

It can still drown: a body that cannot stop where it is (in the air, or a
lung-breather without fins in deep water) keeps moving on nothing.

## Hard tissue

Three genes harden the mouth, the front of the head and the back. Hardness
grows in with the body, and it is heavy: there is far more back than mouth
to cover. Nothing says what a hard part is for. A strike does more with a
harder mouth and less against a harder part, and which part it lands on
follows from where the blow comes from as the struck body faces. A mouth
softer than what it bites is wounded by it. Teeth, horns and armour are
what selection makes of that.

## Senses

The brain receives 64 inputs from `World::senses`.

- **Sight**, in five sectors about its heading; behind is unseen. How far
  it sees is its eyes gene, 3 to 10 cells, and eyes have mass and upkeep.
  Each sector reports:
  - grazing its body can get at, nearer counting for more (meadow grass is
    nothing to a fish, nor waterweed in deep water to a walker);
  - how near the nearest animal is, its size against its own, and how like
    its own its genome is;
  - ground its body cannot go on, and the edge of the world;
  - when it is ready to mate: how near the nearest animal is that is ready
    too and near enough alike.
- **Ahead:** the cell its next steps reach, seen alone: its grazing, and
  whether its body can go there.
- **Itself:** energy, health, breath, how far it is from full grown, its
  drive to mate, whether it is on land, in water or in the air, the grazing
  where it is, the daylight, a number that is different every time, and
  three numbers its brain told itself last time.

- **Smell**, for one with a nose (a gene; none below 32, then keener).
  Scents lie on a grid of nodes four cells across, spreading to their
  neighbours and fading by an eighth every four ticks. The three scents are
  grazing, animals, and animals ready to mate. For each, an animal smells
  how strong it is where it stands, and how much stronger it is ahead than
  behind and to its right than to its left, which is what tells it where
  to turn. The scent of a mate carries what kind of body made it, and
  registers only when the animal is itself ready and the body is like its
  own.
- **Hearing**, for one with ears (a gene; it sets how far, 4 to 36 cells).
  Of the calls it can hear just now it knows the loudest: how loud, how
  like its own voice the pitch is, and, if it is in a voice like its own,
  where it is coming from.

At its mouth the animal senses the length of the nearest body it can
strike relative to its own. A hit produces a pain strength and two signed
directions, ahead and right. The bearing stays fixed in the world as the
animal turns. Strength falls by six per tick from a maximum of 127.
Fear is a separate drive in the design plan and is still unimplemented.
The wind does not carry scent. The scent of
animals does not yet say what kind of animal, which a prey animal will need
to tell a predator from a neighbour.

## The brain

Every animal has a brain of its own (`core/brain.cpp`) with 64 inputs, 16
hidden units, 11 outputs and 1227 int8 weights. Its integer arithmetic is
the same on every machine. The network's arithmetic, its crossover and its
mutation are Petri's. Each animal decides every four ticks, or every 0.4
seconds at normal speed. Its outputs control these actions.

- **turn** and **pace**;
- **Eat.** A meat gut can take a mouthful from a carcass in reach.
  A plant gut can graze when it finds no edible meat. Any grazer can
  graze what grows low (up to 1.5 m); reach only matters for browsing what
  grows taller. A mouthful grows with the body, as its needs do;
- **Attack.** Try a strike with the mouth. A miss also costs energy.
- **mate**: both animals must be ready, both must choose to, and they must
  be within a cell and a quarter of each other;
- **fly**, for one with wings big enough: up while it chooses to, down when
  it stops;
- **call**: heard for most of a second, at the pitch of its voice (a
  gene), louder from a bigger body, and costing a little energy;
- three numbers to remember.

Nothing else steers it. An animal can walk into deep water and drown, or
swim ashore and suffocate; only the edge of the world stops it.

### Brain channels

Indices below are zero-based. The three memory outputs feed back as signed
bytes on the next decision. Each animal keeps its weights for its lifetime.

| Input indices | Values in order |
| --- | --- |
| 0–29 | Five sectors, far left, left, ahead, right, far right. Six values per sector in order of food, nearest animal, its relative size, its genetic similarity, hazard, nearest compatible ready adult. |
| 30–34 | Own energy, health, breath, youth, mating drive (raw drive divided by two). |
| 35–37 | On land, in water, in air. |
| 38–42 | Food here, food ahead, hazard ahead, daylight, noise. |
| 43–45 | Previous memory outputs 0, 1, 2. |
| 46–54 | Plant scent (46–48), animal scent (49–51), ready-mate scent (52–54). Each gives local strength, the forward gradient and the rightward gradient. |
| 55–58 | Loudness of the loudest call, voice similarity, forward and rightward bearing. |
| 59–63 | Relative size of the animal at the mouth (127 for none), pain strength, forward and rightward pain bearing, remaining stamina. |

Outputs 0–10 are turn, pace, eat, mate, fly, call, memory 0, memory 1,
memory 2, attack and rest. Positive outputs activate actions; attack also
requires mouth contact. Turn and pace use the output magnitude.

The five ready-neighbor inputs are 5, 11, 17, 23 and 29. A fully grown
observer needs at least 200 drive. A visible adult within 240 genome
difference points then registers if its drive is at least 200, even if its
mate output is off. The nearest animal and nearest ready adult in a sector
can be different individuals. Mate scent has the same observer threshold.
It reflects recent emissions and similarity of legs, fins/gills and wings.
A ready observer can still smell a fish that has just lost readiness.

**Inheritance.** A child's brain takes each hidden unit whole from one
parent or the other, with its incoming and outgoing weights, so a unit
stays a working part. Then each weight has one chance in 128 of a small
step.

The **founders** start with reflexes wired by hand. They turn toward
visible grazing, turn away from impassable ground, wander and eat when
hungry. When ready to mate they call and turn toward a visible mate, a
mate's scent or a call like their own. Those that can fly take to the air
when they see no food.
New founders turn away from the side that was hit and increase their
pace while pain lasts. The large swimmer also starts with a weak turn
toward smaller bodies and a strike response to hunger and a smaller
animal at its mouth. These are inherited weights. Every body can express
the attack output.

The nearest-animal channels describe bodies in the same medium and within
reachable height; ready mates retain their own independent sight channels.
The mouth-size input uses 127 for no contact, distinguishes equal-sized contact
as zero, and caps larger valid targets at 126. An attack can be armed only on
perceived mouth contact. The lunge lands two ticks after a decision, so a prey
animal that moves out of reach or out of the forward strike cone can still cause a
miss, spending the attacker's stamina. Empty mouths do not repeatedly arm attacks.
Network dimensions and save layouts are unchanged; pools should be bred again
against the revised senses, attack timing and hunting graduation rules.

They are not wired to follow the smell of grazing. That was tried: every
founder made for the same lush places and starved there together. What to
do with that smell is left for evolution to find.

Founders are put down in families, four of a kind near each other, since
one alone on the far side of the map never finds a mate.

## The controls

All four modes start with the same founders on a given seed. Bodies come
from the two animals that mate. The mode controls which brain a newborn gets.

| Mode | Brain inheritance |
| --- | --- |
| `evolving` | Recombine the parents' brains and mutate the weights. |
| `frozen` | Recombine the parents' brains with mutation switched off. |
| `shuffled` | Recombine the parents' brains and shuffle the resulting weights. |
| `neutral` | Draw two distinct brain donors at random from the animals alive at the start of the tick, then use the same crossover and mutation as in evolving mode. |

In the neutral control, juveniles can donate and siblings get separate
draws. This tests whether inheriting the successful breeders' wiring
matters. Survival still affects which brains are available to donate.

`build/petrihd-run layout=development animals=56 seed=1 brains=neutral ticks=153600 every=0`
prints the population at year 40, including a zero count if it died out.
A save records its brain mode. Version 13 also stores the current forage
field so a resumed animal senses the same food between surveys. Passing
`brains=neutral` on reload switches inheritance for subsequent births.
To watch this control in the window, run `build/petrihd --dev-map --brains neutral`.

`python tools/compare_brains.py build/petrihd-run --jobs 4` runs all four
modes across seeds 1 through 8 and writes the final censuses as CSV.

Before adding predation, at 40 years (153,600 ticks) with 56 founders per seed, the
counts below are **walkers / fliers / swimmers**. Walkers include crawlers.

| Seed | Evolving | Frozen | Shuffled | Neutral |
| --- | --- | --- | --- | --- |
| 1 | 258 / 62 / 561 | 321 / 0 / 355 | 0 / 0 / 4 | 315 / 0 / 606 |
| 2 | 373 / 0 / 299 | 408 / 0 / 56 | 0 / 0 / 0 | 309 / 0 / 146 |
| 3 | 398 / 0 / 523 | 429 / 27 / 235 | 0 / 0 / 0 | 433 / 0 / 154 |
| 4 | 420 / 0 / 678 | 293 / 140 / 258 | 0 / 0 / 0 | 383 / 0 / 515 |
| 5 | 372 / 0 / 358 | 340 / 0 / 130 | 0 / 0 / 0 | 316 / 0 / 312 |
| 6 | 293 / 5 / 584 | 215 / 79 / 112 | 0 / 0 / 0 | 330 / 0 / 0 |
| 7 | 459 / 0 / 664 | 310 / 0 / 0 | 0 / 0 / 1 | 387 / 0 / 1 |
| 8 | 330 / 97 / 226 | 199 / 482 / 163 | 0 / 0 / 0 | 382 / 0 / 355 |

All eight evolving populations survived. Each had more animals at year 40
than its shuffled counterpart. Six shuffled populations went extinct. The
other two ended with four swimmers on seed 1 and one swimmer on seed 7.

Evolving populations finished larger than neutral on six seeds and larger
than frozen on seven. Evolving brains produced more surviving swimmers than
frozen brains on all eight seeds. On seed 8, 482 fliers survived with
frozen brains, bringing that population to 844 animals. The evolving world
on that seed ended with 653.

The earlier three-seed table gave different counts. A clean build of
`fed75d7` reproduced the new seed 1 evolving census of 881 animals and
its fingerprint, `db1bfb557117b349`, at tick 153,600.

## Predation on the development map

A strike can reach forward up to one cell once every eight ticks. Reach
depends on both body lengths. The two animals must occupy the same medium
and be close enough in height. Relative length sets the damage. A lethal
strike leaves a carcass at the victim's position with the killer's ID.
Starvation and suffocation also leave carcasses. Each contains the body's
remaining reserves and tissue energy. Feeding removes a finite mouthful
and credits the energy the eater actually absorbed. The rest decays into
litter, including after the last animal dies.

The census follows the maternal founder lines of the large swimmers and
small fish. A hunter qualifies as a breeder when it first produces young
after eating from an animal it killed itself. Either parent can qualify.
Each newborn counts once. The generation count measures consecutive
qualifying parents and offspring. The census records scavenged energy
separately. Plant food is available throughout the study.

Version 14 saves retain carcasses, the bearing and strength of pain, and
the hunting history. Version 12 and 13 brains keep every learned weight
and bias. Their new connections start at zero. Earlier saves lack founder
cohort records, so their existing animals remain outside the fish census.

### Forty-year results

Each development map started with 56 founders, including eight large
swimmers and sixteen small fish. Each seed ran for 153,600 ticks in all
four brain modes. In the evolving worlds, 51 of the 64 large swimmers ate
from their own kills. None produced young. The last large swimmer died
before the end of year two on every seed.

| Seed | Large swimmers that ate their own kill | Small fish killed by large swimmers | Small fish at year 40 | Small fish at year 40 before predation |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6 | 15 | 0 | 561 |
| 2 | 7 | 13 | 373 | 299 |
| 3 | 7 | 12 | 0 | 523 |
| 4 | 6 | 13 | 0 | 678 |
| 5 | 7 | 13 | 0 | 358 |
| 6 | 6 | 10 | 723 | 584 |
| 7 | 6 | 14 | 0 | 664 |
| 8 | 6 | 13 | 546 | 226 |

On the same eight seeds, the earlier grazing build also produced zero
large-swimmer young. Its year-40 fingerprints matched the original
evolving runs. During the predation runs, five large swimmers reached the
mating drive threshold of 200 at some point. Only one was observed at or
above that threshold after eating its own kill.

Across the 32 runs, large swimmers ate from their own kills on every map
and no qualifying hunter produced young. Small fish survived to year 40
on three evolving seeds, one frozen seed, one shuffled seed and two neutral
seeds. [Full year-40 census for all four brain modes](experiments/swimmer-predation-40y.csv)

### Where large-swimmer mating stopped

We replayed the eight evolving worlds from founding until the last large
swimmer died. The trace recorded each adult and all genetically compatible
adults at every tick, then checked them again immediately after each
swimmer's energy and mating drive updated. On these seeds every compatible
partner was another large swimmer. All eight replays matched the normal
runner's world fingerprint at extinction.

Every one of the 64 founders came within mating range of a compatible
adult before its own energy first fell below half on a metabolic pass.
The median first encounter was tick 27. The median first drop below half
was tick 411. Only five founders reached a mating drive of 200, and no
two compatible adults were ever ready at the same time, even at a
distance. Four of the five spent time in range of a partner while ready.
The mate action was active on 180 of the 185 passes when a swimmer was ready.

| Seed | Distinct pairs within 1.25 cells (of 28) | Adults ever ready (of 8) | Starvation deaths | Last large swimmer died at tick |
| --- | ---: | ---: | ---: | ---: |
| 1 | 28 | 2 | 5 | 6726 |
| 2 | 21 | 0 | 6 | 2084 |
| 3 | 27 | 0 | 6 | 1734 |
| 4 | 27 | 1 | 8 | 1810 |
| 5 | 26 | 0 | 8 | 1576 |
| 6 | 26 | 1 | 7 | 2008 |
| 7 | 26 | 0 | 7 | 1705 |
| 8 | 25 | 1 | 5 | 1741 |

**Seed 6 came closest.** Swimmer #56 was ready from tick 392 through
647. It spent 252 of those 256 ticks within reach of #35, whose drive
held at 192. At tick 381, #35 had 63.0% of its energy capacity and
gained eight drive points to reach 192. By its next metabolic pass it
had 62.0%, below the 62.5% needed for another increase. At tick 653 its
energy fell to 49.2% and its drive dropped to 176.

![Mating drive, energy reserves and time within range for large swimmers 35 and 56 on seed 6](images/swimmer_mating_seed6.png)

Across the eight seeds, 59 swimmers exhausted their reserves at least
once. Starvation caused 52 deaths, attacks caused seven and suffocation
caused five. The immediate obstacle to breeding was sustaining energy
long enough for two adults' readiness to overlap.
[Per-swimmer lifetimes and mating checks](experiments/swimmer-mating-lifetimes.csv)

The trace tool that produced this (`petrihd-swimmer-trace`) compiled its
observation points into a private copy of the simulation. It was removed
when the step observer replaced such hooks (`StepObserver` in
`core/world.hpp`); it is in the repository's history at commit `cb33c70`.

### The nursery

Founder brains are bred in arenas before a world starts: see
[the nursery](NURSERY.md). The first nursery trained only the large swimmer,
in a two-fish pond. [Its lessons, breeding course and development-map pilot](experiments/README.md)
are kept as a record, with their data.

## Breeding and life history

- **The drive to mate** rises while a grown animal is well fed, and hunger
  holds it down, so nothing breeds its way out of starving.
- **Mates.** Two can breed only if their genomes are near enough alike
  (`genetic_distance` no more than `breeding_distance`). Finding each other
  and choosing to is the brain's business. Bodies that have
  drifted apart cannot breed: nothing names a species.
- **Young.** The one that found the other bears them: one to four, by its
  investment gene. Each takes every gene from one parent or the other, with
  one gene in eight changed a little. What a newborn starts with comes out
  of its mother, who keeps enough to live on.
- **Growing up.** A newborn is the adult's body at three eighths of its
  length, and so a twentieth of its mass. It grows while it is fed, as fast
  as its maturity gene allows, and can breed when full grown.
- **Growing old.** Upkeep rises with age, so an old animal needs more and
  more to stay alive. It is gentle: where nothing competes with them, some
  founders are still alive after fifteen years, which is too long.

There are no sexes, no eggs, no choice of mate and no care of young yet.

## What it shows so far

In the grazing runs before predation, over the first fifteen years with evolving brains,
grazers hold between 60 and 170, fliers between 30 and 200 and fish between
30 and 320. After that the grazers and fish keep climbing (see the
controls above). In the earlier grazing runs, mean energy in low plants
fell from about 60 to under 10 as the herds grew.
Crawlers never establish: they are slow, and lose to the
grazers for the same grass.

Founders that breathe water start in a lake: nothing grows in the sea for
them yet.

## On the map

Below 32 px per cell, where the view is the bird's-eye map, every animal is
a dot in its own colour on the art's pixel grid, bigger for a bigger body
and as the view closes in, with a dark rim. One in the air is paler and has
no rim. Dots can be tapped like the animals themselves.

## The inspector

Tap an animal to inspect it: it is marked on the screen, and a panel shows
what its organs make it, its organs drawn from the side to scale with each
other, bars for satiety, energy, health and breath, what it is doing, its
length, mass, legs, wings, fins, lungs or gills, diet and speeds, and its
brain at work: a row of lamps for what it senses, a row for its hidden
units and a row for what comes out, green for more and red for less. The view
follows it from then on; drag the view, or tap it again, to let it go. Tap the panel, or anywhere
else, to close it. `--inspect ID` opens it on an animal from the start.

The plan's inspector also has a live portrait, a species name, a
family tree and a life log. None of those exist yet.

## Not yet

- Touch along the sides, a persistent fear drive, windborne scents and
  scents that distinguish the kind of animal that made them.
- Nursery trials for other founder body plans.
- Eggs and sexes.
- Depth as a place: a swimmer keeps a fixed distance under the surface.
- Collisions. (Who is near is answered by a list of the animals in each
  cell, rebuilt every tick.)
- Timing on the Pi. The simulation rebuilds each animal's body every tick,
  scans the cells in sight every four ticks and evaluates the network with
  portable integer loops.
