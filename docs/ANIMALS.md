# Animals

The start of milestone 3 (bodies). Animals are in the simulation
(`core/animals.hpp`, `core/animals.cpp`): each has a genome of organs, a
body those organs add up to, a place in the world, and a metabolism. They
sense the world, decide with a brain they inherit, breed, grow up and grow
old.

Run `build/petrihd --dev-map` to watch them: the development map starts
with 48 founders. `--animals N` puts N founders into any new world. A world
without animals behaves, and fingerprints, exactly as before.

## The genome builds a body

Twenty-three genes (`AnimalGene`): size and build, pairs of legs and their
length and sprawl, fins, tail, wings, neck, gills or lungs, a plant gut and
a meat gut, fat, pigment and pattern, how fast it grows up, how many
young it has at a time, how far it sees, how keen its nose is, how far it
hears, and the pitch of its voice. `frame()` turns them into a body:

- **Mass.** The trunk is a cylinder of flesh; legs, wings, fins, neck, tail,
  guts and fat are all counted against it. Upkeep and the size of a
  mouthful follow mass to the power 3/4.
- **Speed in each medium.** Legs under the body stride further than legs out
  to its sides. Fins and a tail drive a swimmer; legs alone only paddle.
  Wings fly only if they are big enough for the mass they must lift.
- **Breath.** Lungs, gills or both. Where it cannot breathe an animal holds
  its breath for a minute of world time, then is hurt, then dies.
- **Wading.** How deep it can stand follows its legs.

There are no kinds of animal. The founders are a grazer, a six-legged
crawler, a flier, a small fish and a large swimmer, each with its
proportions varied a little.

## Senses

What an animal knows of the world (`World::senses`, `core/brain.hpp`), 59
numbers in all:

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
  neighbours and fading by an eighth four times a second. There are three:
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

There is no touch or pain yet. The wind does not carry scent. The scent of
animals does not yet say what kind of animal, which a prey animal will need
to tell a predator from a neighbour.

## The brain

Every animal has a brain of its own (`core/brain.cpp`): 59 inputs, 16
hidden units, 9 outputs, 1113 int8 weights, in integer arithmetic that is
the same on every machine. The network's arithmetic, its crossover and its
mutation are Petri's. It decides four times a second, and what comes out is
all an animal does:

- **turn** and **pace**;
- **eat**: take a mouthful of a plant in reach where it is. Any animal can
  graze what grows low (up to 1.5 m); reach only matters for browsing what
  grows taller. A mouthful grows with the body, as its needs do;
- **mate**: both animals must be ready, both must choose to, and they must
  be within a cell and a quarter of each other;
- **fly**, for one with wings big enough: up while it chooses to, down when
  it stops;
- **call**: heard for most of a second, at the pitch of its voice (a
  gene), louder from a bigger body, and costing a little energy;
- three numbers to remember.

Nothing else steers it. An animal can walk into deep water and drown, or
swim ashore and suffocate; only the edge of the world stops it.

**Inheritance.** A child's brain takes each hidden unit whole from one
parent or the other, with its incoming and outgoing weights, so a unit
stays a working part. Then each weight has one chance in 128 of a small
step.

**Founders** are not bred in a nursery yet. They start with eleven reflexes
wired by hand, and the rest of the network blank for evolution to use: turn
toward grazing in sight; turn away from ground it cannot go on; turn hard
when the way ahead is no good; wander; eat when hungry; when ready to mate,
call, and turn toward a mate in sight, a mate's scent, or a call in a voice
like its own; take to the air when there is nothing to eat in sight.

They are not wired to follow the smell of grazing. That was tried: every
founder made for the same lush places and starved there together. What to
do with that smell is left for evolution to find.

Founders are put down in families, four of a kind near each other, since
one alone on the far side of the map never finds a mate.

## The controls

`petrihd-run layout=development animals=48 brains=evolving|frozen|shuffled`
runs the world with one of:

- **evolving:** the world as meant;
- **frozen:** brains pass to the young unchanged, no mutation;
- **shuffled:** each newborn's weights are put in a random order.

Forty years on the development map, three seeds, 56 founders; grazers,
fliers and fish alive at the end:

| Seed | Evolving | Frozen | Shuffled |
| --- | --- | --- | --- |
| 1 | 381, 189 and 1465 | 323, 0 and 566 | 0, 0 and 6 |
| 2 | 612, 0 and 753 | 514, 0 and 92 | none left |
| 3 | 432, 0 and 775 | 495, 41 and 490 | none left |

What that shows, and what it does not:

- **The wiring matters.** With brains shuffled at birth every kind
  collapses on every seed.
- **Evolution may be starting to tell, for the fish.** Evolving fish end
  ahead of frozen ones on all three seeds, by between one and a half and
  eight times. Three seeds is not enough to call it: the milestone's gate
  asks for eight. Grazers show no difference.
- **Nothing holds the grazers back but hunger.** Over forty years they and
  the fish multiply until the low plants are grazed to stubble (their mean
  energy falls from about 60 to under 10). That is what herbivores without
  predators do, and it is the strongest argument for predation next.
- **Fliers mostly die out** by year forty, and crawlers within four years.

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

On the development map over the first fifteen years, with evolving brains,
grazers hold between 60 and 170, fliers between 30 and 200 and fish between
30 and 320. After that the grazers and fish keep climbing (see the
controls above). Crawlers never establish: they are slow, and lose to the
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
other, bars for energy, health and breath, what it is doing, its
length, mass, legs, wings, fins, lungs or gills, diet and speeds, and its
brain at work: a row of lamps for what it senses, a row for its hidden
units and a row for what comes out, green for more and red for less. The view
follows it from then on; drag the view, or tap it again, to let it go. Tap the panel, or anywhere
else, to close it. `--inspect ID` opens it on an animal from the start.

The plan's inspector also has a live portrait, a species name, a
family tree and a life log. None of those exist yet.

## Not yet

- Touch and pain; scent carried on the wind; scent that says what kind of
  animal made it.
- The nursery that breeds founders, and the neutral control.
- Predation and carcasses, eggs, sexes.
- Depth as a place: a swimmer keeps a fixed distance under the surface.
- Collisions. (Who is near is answered by a list of the animals in each
  cell, rebuilt every tick.)
- Cost on the Pi: every animal's body is worked out again each tick, and
  each looks over every cell in sight four times a second. Nothing is
  vectorised, and none of it has been timed there.
