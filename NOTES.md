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

Unmodified project builds clean with GCC 16.1 and CMake 4.3 (Ninja). All 40 framework
checks in `antworld_tests` pass. With the empty `forage()`, `dev_challenge` runs the full
1000 steps and scores nothing, which is the number to beat.
