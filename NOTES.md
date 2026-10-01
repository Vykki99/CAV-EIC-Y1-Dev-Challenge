# Development notes

Working log for my AntWorld solution. Written as I go, so the reasoning behind each
change is recorded somewhere other than my head.

## Problem summary

Maximise food returned home. 8 ants, a 15x15 map, 40% of cells hold food (90 items),
and each ant starts with a random energy budget in `[45, 90]`. Movement between adjacent
cells costs `1 + |height difference|`. An ant scores by standing on the home cell while
carrying food.

## Reading the framework

Before writing anything I read `antworld.cpp`, `useable_functions.cpp` and
`utility_functions.h`. Four things mainly stood out.

**1. Energy is the only real budget.** `MAX_SIMULATION_STEP_COUNT` is 1000, but the whole
colony only has about 540 energy to spend. `move()` also walks an entire path in a single
call, so ticks are close to free and energy is the thing that runs out. Every decision
reduces to "what is the cheapest round trip I can still afford?"

**2. `move()` only picks up food at the final cell of its path.** From
`useable_functions.cpp`:

```cpp
for (int i = 1; i < path.size(); ++i) { /* ... walk the whole path ... */ }

if (foodMap[this->position.first][this->position.second] == 1 && !this->carryingFood) {
    foodMap[...] = 0;
    this->carryingFood = true;
}
```

Food the ant passes over mid-path is ignored. So calling `move()` one cell at a time is
strictly better than calling it once with a distant target: every cell becomes a "final"
cell, the ant collects anything it steps on for free, and I get to re-plan each tick with
fresh sensor data.

**3. Scoring happens before the death check.** In `updateWorld()` the
`position == home && carryingFood` branch runs first, and only then does `energy == 0`
erase the ant. An ant that arrives home with exactly 0 energy still scores. That means a
"can I still get home?" guard can be exact, with no safety margin wasted.

**4. Dead ants are erased from the vector, so indices shift.** Anything I key to an ant's
index would silently attach to a different ant after a death. Rather than invent an ID
scheme, I plan to keep no per-ant state at all and re-solve every tick from current
state. Only the colony's shared map knowledge needs to persist.

## Approach

Three phases per tick, which lines up with the Search -> Decide -> Act loop in the
challenge description:

- **Observe**: every ant runs `foodScan()` into a shared colony map. Only real sensor
  readings go in, so the viewing radius is respected; the ants just pool what they see.
- **Decide**: if carrying, head home. Otherwise take the cheapest affordable round trip,
  where "affordable" means energy covers the trip out *and* the trip back.
- **Act**: one cell of movement.

Building it in small steps so I can measure each one instead of guessing.

## Baseline

Unmodified project builds clean with GCC 16.1 and CMake 4.3. All 40 framework checks in
`antworld_tests` pass. With the empty `forage()`, `dev_challenge` runs the full 1000 steps
and scores nothing.

`dev_challenge` is a poor measuring stick for anything after that. The constructor prints
each ant's starting energy, and if the 1000-step cap is hit it never prints the score at
all. `tools/benchmark.cpp` runs many seeds, swallows those energy prints, and always
reports `game.score`. Command is `benchmark [seeds] [mapX] [mapY] [ants]`, defaults
20 / 15 / 15 / 8. Seeds are 1000, 1001, ... so the number is not overfit to 12345.

### M1 — nearest known food, whole-path `move()`

20 seeds, 15x15, 8 ants:

```
score mean=32.6 min=24 max=41
stranded=120 backtracks=1 seek_misses mean=0
```

Default seed 12345 scored 33, used all 1000 steps, and still had one ant alive. That is
why the graded binary said "Game not finished" and showed no score.

**Stranded 120** is about 6 of 8 ants dying in the field every world. They walk all the
way to food with no energy reserved for the trip home.

**Backtracks 1** in 20 games: almost no vibration, which is expected while `move()` still
walks an entire path each tick. The counter ignores ants that are carrying food, so
"go out, then walk home" is not counted as oscillation.

**Seek misses 0:** `knownFood` never goes empty. At 40% food density the colony always
has some remembered cell, including ones that are already picked clean but not yet
re-scanned. A zero here means the memory never went empty, including cells that are
already picked clean.

### M3 — one cell per tick

Same 20 seeds. Each row is the colony after that commit, not a separate strategy.

```
whole path (M1)          mean=32.6  min=24 max=41  stranded=120  backtracks=1
one cell, no self-pickup mean=23.35 min=0  max=42  stranded=78   backtracks=127
standing-on-food pickup  mean=33.5  min=24 max=42  stranded=122  backtracks=193
forget food on pickup    mean=33.8  min=26 max=42  stranded=113  backtracks=201
```

The dip to 23.35 was a freeze, not a bad walk. If an ant is standing on food, that cell is distance 0, so it is always the nearest target. `stepToward` used to return without calling `move()`, and `move()` is the only thing that picks food up. On worlds where home itself had food, every ant stayed there forever (`min=0`). Calling `move()` on the ant's own cell collects it and spends no energy. The mean went back to 33.5.

Clearing `knownFood` when an ant picks up only helps ants later in the same tick. Mean 33.8. Stranded deaths are still about 6 of 8 ants per world. Backtracks rose because ants now retarget every step instead of committing to a whole path.

### M4 — round trips only

Same 20 seeds.

```
end of M3                 mean=33.8  min=26 max=42  stranded=113  backtracks=201  seek_misses mean=0
affordable food only      mean=33.2  min=24 max=42  stranded=0    backtracks=231  seek_misses mean=7105.2
step guard and walk home  mean=33.2  min=24 max=42  stranded=0    backtracks=248  seek_misses mean=7105.2
```

An ant now takes a food cell only when its energy covers the walk there and the walk home. Stranded deaths went to 0 on that change. The mean dipped by 0.6 because the old colony sometimes delivered one more item by spending its last energy and dying. Those unfinished hauls were not a reliable extra point.

The step guard and the walk-home fallback did not move the score. The food choice already refused unsafe trips, so the guard never had to block a step. Seek misses near 7100 are the idle tail: once nothing affordable is left, every ant has no target for most of the 1000 ticks. Backtracks rose slightly because some ants step back onto a cell they just left while turning toward home.

### M6 — one target per ant

Same 20 seeds.

```
before claims  mean=33.2  min=24 max=42  stranded=0  backtracks=248  seek_misses mean=7105.2
with claims    mean=34.6  min=25 max=43  stranded=0  backtracks=215  seek_misses mean=7017.9
```

Each tick now keeps a list of food cells already chosen. The next ant skips those cells, so two ants do not walk toward the same item in the same tick. The list is thrown away at the end of the tick.

The mean rose by 1.4. Stopping two ants from spending a trip on one item left that energy for a delivery instead. Backtracks fell for the same reason: fewer ants turn around after arriving at a cell someone else just emptied.

### M7 — idle ants already know the food

100 seeds, 15x15, 8 ants. The 34.6 mean from M6 was the first 20 seeds only.

```
score mean=33.6 min=24 max=43
stranded=0 backtracks=1058
no_known mean=0 unaffordable mean=7207.86
energy start mean=543.31 leftover mean=77.21
```

An idle tick is now split in two. `no_known` means the colony has no food in memory. `unaffordable` means it knows where food is and still cannot pay for the round trip. `no_known` is 0 across all 100 seeds. Every idle ant already knows where food is.

The colony starts with about 543 energy and is still holding about 77 when the game ends. That leftover is too small, on each ant, for the cheapest trip that is still open.

### Weakest ant takes the short trip

Same 100 seeds, compared one map at a time with the colony above.

```
score +23 across 100 seeds, mean +0.23
38 seeds up, 22 down, 40 unchanged
leftover energy about -3 per game
```

Ants that are already carrying still walk home first. The others are sorted by energy, lowest first, and that ant claims the cheapest trip it can finish. A nearly empty ant gets the short haul while it can still complete it, instead of a fuller ant spending that trip and leaving the empty one with nothing it can afford.

### Holding cheap food back

The colony still finishes holding about 77 energy. I tried making an ant with plenty of energy skip the cheapest trips, so a short haul would still be there once some ant was nearly empty.

On the same 100 seeds, every cutoff lost points. The gentlest one, skipping a trip cheaper than 6 when an ant still had more than 60 energy, was 27 points down across the 100 seeds. Steeper cutoffs were worse. In the steepest cases every seed scored lower. A short trip that an ant can finish is worth taking. Saving it for later mostly meant nobody took it.
