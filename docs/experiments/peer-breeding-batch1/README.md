# Selective breeding with roaming partners

This batch started with the Lesson 2 donors from slots 1, 14, 11 and 15.
Both swimmers in each pair controlled their own feeding, swimming and mate
decisions. The starting pond, bodies, energy, drives, light, temperature
and three reed beds came from Lesson 2. Habitat difficulty increased with
the time between food refills. Every pond started with 76,800 raw food units.

## What one generation does

1. Cross all six possible pairs of the four parents. Each cross gets up to four seed setups to produce two distinct offspring brains through actual births. The simulator supplies the inherited and mutated weights. Keep the four parents eligible alongside their children.
2. Test every eligible brain on the same eight seed setups for that generation. Rotate roaming partners from the parent group, exclude self-pairings, and balance the candidate's starting side. Put each brain in a standardized healthy adult body for these trials. Each trial ends at the first birth or 960 ticks.
3. Give each candidate two additional family trials with different partners and seeds. The young grow up with their own inherited bodies and brains. Observe the first grandchild, extinction, or three simulated years. A grandchild has at least one parent born to the two founders. The family records include both parent IDs and the count of first-generation parents.
4. Rank candidates by successful first births, families with a grandchild, families with adult offspring, then first births leaving both parents at full health with at least one-third reserves. When grandchildren are absent, offspring mating drive gives partial credit. When first births are absent, use nearby mating drive and fed checks. Among comparable outcomes, raw plant food removed and gross energy charged to both founders each contribute one rank. Lower combined ranks win. Carry four distinct brains forward.

Each food setting gets four generations. Afterward, test all six pairings
of the incoming and selected groups on the same eight new seeds. Every
pair must breed on at least 7/8. Eight separate family setups must produce
grandchildren on at least 3/4 in each four-seed block. If both groups pass,
higher birth and family reliability comes first. At equal reliability, a
replacement must reduce energy or raw food per birth by at least 1% while
keeping the other cost within 1% of the incumbent. The preceding checkpoint
supplies the parents after a failed step.

## What changed

Fourteen tested food settings produced **56 selection rounds** and **666
offspring entries**. Four rounds at the initial food setting moved food
per birth from 14,184 to 8,612 and energy from 22,010 to 21,061 on the
matched stage checks. Both groups bred on 48/48 trials and produced
grandchildren on 8/8 family setups.

Food gaps doubled from one tick through the first missed step at 1,024.
Smaller steps then tested 768, 896 and 832 ticks. The stage checks passed
at 768. A separate comparison froze the selected group and original group
across the same food settings and seeds. It used two new panels, each
with 48 pair trials and eight family trials. The table pools the two panels.

| Ticks between refills | Founder families with grandchildren | Selected families with grandchildren | Selected group passed both panels |
|---:|---:|---:|:---|
| 1 | 16/16 | 16/16 | Yes |
| 512 | 16/16 | 16/16 | Yes |
| 704 | 16/16 | 15/16 | Yes |
| 768 | 14/16 | 12/16 | No |
| 832 | 13/16 | 4/16 | No |
| 1,024 | 11/16 | 2/16 | No |

At 704 ticks, both groups bred on 96/96 pair trials. The original four
used 14,325 food and 22,479 energy per birth. The selected four used
8,552 food and 20,980 energy. The selected parents finished their first
birth at 60.6% reserves on average, compared with 77.9% for the founders.
The selected group ran leaner as well as spending less energy. The
original families reproduced across generations more often
as food gaps grew. The 704-tick checkpoint passed both audit panels at
every sampled interval up to and including that value.

[Checkpoint manifest](confirmed/brains.csv) · [Stage and audit results](stages.csv) · [Family pedigrees](families.csv) · [Selection in every generation](generations.csv) · [Offspring and their parents](lineage.csv) · [Pair trials](trials.csv)

## Run the batch

```sh
cmake --build build --target petrihd-nursery
build/petrihd-nursery curriculum build/peer-course docs/experiments/lesson2-batch1/pond.cfg docs/experiments/lesson2-batch1/slot-1.phdw docs/experiments/lesson2-batch1/slot-14.phdw docs/experiments/lesson2-batch1/slot-11.phdw docs/experiments/lesson2-batch1/slot-15.phdw --visual --seed-base 300000
```

Open `build/peer-course/nursery.html` to watch representative first-birth
and family trials. Family clips can jump to the first adult offspring and
the first grandchild. Frame data come from the scored simulation and are
checked against its final fingerprint.

The four files listed in [the manifest](confirmed/brains.csv) are native
tick-zero worlds. The first swimmer carries the named brain. A subsequent
batch can start from these four donors and their saved pond preset. A new
seed base, for example 1,300,000, gives it separate selection and audit panels.
