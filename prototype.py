"""AntWorld scratch work - me working the problem out in python before converting it to c++.

The rules I'm working from:

  - score = food carried onto the home cell. 8 ants, 15x15 map, 40% of cells
    hold food (90 items), each ant starts with energy drawn from 20%-40% of the
    cell count, so 45-90 on a 15x15.
  - a step between two neighbouring cells costs 1 + abs(height difference).
  - there's a 1000 step cap, but the whole colony only has about 540 energy, and
    move() walks an entire path in one call. So ticks are nearly free and energy
    is the thing that actually runs out. Every decision comes back to "what is
    the cheapest round trip I can still afford?"

Three framework details that shape all of this (from reading useable_functions
and antworld before touching anything):

  1. move() walks the whole path but only picks food up on the cell it *stops*
     on. Food it passes over mid-path is ignored. So calling move() one cell at
     a time is strictly better - every cell becomes the last cell, the ant
     collects whatever it steps on for free, and I get to re-plan each tick with
     fresh scan data.
  2. update_world() scores before it checks for death, so an ant that gets home
     with exactly 0 energy still scores. That means my "can I still get home?"
     guard can be exact - no safety margin to waste.
  3. dead ants are erased from the ant list, so every index after them shifts.
     Anything I key to an ant's index would silently attach to a different ant
     after a death. Rather than invent an id scheme, keep no per-ant state at
     all and re-solve from current state every tick. Only the colony's shared
     map knowledge has to persist.

None of this is meant to be fast or final, it's just so I can watch the loop
work before I write it properly.
"""

import heapq
import random

# the framework uses INT_MAX as "no route here", this is the same idea
INF = float("inf")


# ---------------------------------------------------------------------------
# framework stand-ins
#
# The real project hands me all of this (utility_functions.h, antworld.cpp,
# useable_functions.cpp). Rough python copies here so I can actually run the
# loop; the c++ side will just call the real ones.
# ---------------------------------------------------------------------------

def step_cost(grid, a, b):
    """Energy to walk between two neighbouring cells. 1 + the climb."""
    return 1 + abs(grid[a[0]][a[1]] - grid[b[0]][b[1]])


def neighbours(grid, cell):
    """The in-bounds 4-neighbours of a cell. No diagonals, same as the
    framework's dr/dc arrays.
    """
    rows = len(grid)
    cols = len(grid[0])
    r, c = cell
    for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        nr, nc = r + dr, c + dc
        if 0 <= nr < rows and 0 <= nc < cols:
            yield (nr, nc)


def shortest_path(grid, start, goal):
    """Cheapest route from start to goal.

    in:  grid of heights, start (r, c), goal (r, c)
    out: list of cells including both ends, or [] if the goal is unreachable

    Dijkstra, not plain BFS, because steps aren't all the same price. Same job
    as shortestPath() in utility_functions.h.
    """
    dist = {start: 0}
    parent = {}
    open_set = [(0, start)]

    while open_set:
        cost, cell = heapq.heappop(open_set)
        if cost > dist.get(cell, INF):
            continue  # stale entry, already found this cell cheaper
        if cell == goal:
            break

        for nxt in neighbours(grid, cell):
            new_cost = cost + step_cost(grid, cell, nxt)
            if new_cost < dist.get(nxt, INF):
                dist[nxt] = new_cost
                parent[nxt] = cell
                heapq.heappush(open_set, (new_cost, nxt))

    if goal not in dist:
        return []

    path = [goal]
    while path[-1] != start:
        path.append(parent[path[-1]])
    path.reverse()
    return path


def path_cost(grid, path):
    """Total energy of a path. Mostly here so the tests can cross-check the
    cost field against an actual walked route.
    """
    return sum(step_cost(grid, path[i - 1], path[i]) for i in range(1, len(path)))


def generate_world_map(map_x, map_y, rng):
    """Rough stand-in for the framework's terrain generator.

    Heights 0-2, and a cell can only differ by 1 from the neighbours already
    placed, so the map comes out smooth-ish. The real one also looks at the two
    diagonals; mine only checks left and top. Close enough for a sketch - my
    scores won't match the c++ ones anyway (different RNG).
    """
    grid = [[0] * map_y for _ in range(map_x)]
    for r in range(map_x):
        for c in range(map_y):
            low, high = 0, 2
            if c > 0:
                low = max(low, grid[r][c - 1] - 1)
                high = min(high, grid[r][c - 1] + 1)
            if r > 0:
                low = max(low, grid[r - 1][c] - 1)
                high = min(high, grid[r - 1][c] + 1)
            grid[r][c] = rng.randint(low, high)
    return grid


def spread_food(map_x, map_y, food_count, rng):
    """food_count cells set to 1, picked by shuffling every index."""
    cells = [(r, c) for r in range(map_x) for c in range(map_y)]
    rng.shuffle(cells)
    grid = [[0] * map_y for _ in range(map_x)]
    for r, c in cells[:food_count]:
        grid[r][c] = 1
    return grid


class Ant:
    """Stand-in for the framework's Ant. Only the parts I actually touch."""

    def __init__(self, energy, home):
        self.energy = energy
        self.position = home
        self.home_coord = home
        self.food_radius = 3
        self.pheromone_radius = 5
        self.carrying_food = False

    def food_scan(self, food_map):
        """Every food cell within food_radius of the ant.

        in:  food_map (1 = food), not modified
        out: list of coords

        Worth noting it's a square, not a circle - the framework just loops
        position +/- radius on both axes, so the corners are in range too.
        """
        found = []
        r, c = self.position
        for i in range(r - self.food_radius, r + self.food_radius + 1):
            for j in range(c - self.food_radius, c + self.food_radius + 1):
                if i < 0 or i >= len(food_map) or j < 0 or j >= len(food_map[0]):
                    continue
                if food_map[i][j] == 1:
                    found.append((i, j))
        return found

    def pheromone_scan(self, pheromone_map):
        # Never ended up using the pheromone layer - all my ants write into one
        # shared map already, so dropping markers on the ground to talk to each
        # other is a slower version of something I get for free. Leaving the
        # stub so the shape of the class matches the framework.
        raise NotImplementedError

    def move(self, terrain, dest, food_map):
        """Walk toward dest along the cheapest route, stopping when energy runs
        out, and pick up food on whatever cell that turns out to be.

        in:  terrain, dest coord, food_map (gets modified - food is removed)
        out: the cell the ant ended on

        The pickup only happening on the final cell is the whole reason I call
        this one cell at a time from step_toward().
        """
        path = shortest_path(terrain, self.position, dest)

        for i in range(1, len(path)):
            cost = step_cost(terrain, path[i - 1], path[i])
            if cost > self.energy:
                break
            self.position = path[i]
            self.energy -= cost

        r, c = self.position
        if food_map[r][c] == 1 and not self.carrying_food:
            food_map[r][c] = 0
            self.carrying_food = True

        return self.position


class World:
    """Stand-in for AntWorld. The real one builds all three map layers in its
    constructor from a seeded mt19937, which python can't reproduce, so numbers
    out of here are only indicative. I just need the loop to run.
    """

    def __init__(self, seed, map_x=15, map_y=15, ant_count=8):
        rng = random.Random(seed)
        cells = map_x * map_y

        self.terrain_map = generate_world_map(map_x, map_y, rng)
        self.food_map = spread_food(map_x, map_y, int(cells * 0.4), rng)
        self.pheromone_map = [[0] * map_y for _ in range(map_x)]
        self.home = (rng.randrange(map_x), rng.randrange(map_y))
        self.ants = [
            Ant(rng.randint(int(cells * 0.2), int(cells * 0.4)), self.home)
            for _ in range(ant_count)
        ]
        self.score = 0

    def update_world(self):
        """Score, then clean up. Order matters (see note 2 up top): an ant that
        arrives home on its last point of energy is scored before it's removed.
        An ant that dies while carrying drops the food where it stopped.
        """
        alive = []
        for ant in self.ants:
            if ant.position == self.home and ant.carrying_food:
                self.score += 1
                ant.carrying_food = False

            if ant.energy == 0:
                if ant.carrying_food:
                    self.food_map[ant.position[0]][ant.position[1]] = 1
                continue  # erased, and this is what shifts the indices

            alive.append(ant)
        self.ants = alive

    def is_game_over(self):
        """Over when every ant is gone, or the map is empty."""
        if not self.ants:
            return True
        return not any(1 in row for row in self.food_map)


# ---------------------------------------------------------------------------
# colony memory
# ---------------------------------------------------------------------------

class ColonyMemory:
    """Everything the colony carries between ticks.

    One shared blob rather than per-ant state, because of the index shifting in
    note 3. In c++ this wants to be a struct in an anonymous namespace so it
    stays private to the solution file.
    """

    def __init__(self):
        self.terrain = []
        self.home = (-1, -1)
        self.known_food = []      # 2d bools: "someone has seen food here"
        self.cost_to_home = []    # energy from every cell back to home
        self.known_food_count = 0
        # counters below are diagnostics only, no decision reads them
        self.no_known_food_ticks = 0
        self.unaffordable_ticks = 0
        self.relay_reach = 0
        self.relay_pairs = 0
        self.dormant = False


memory = ColonyMemory()


def is_known_world(terrain, home):
    """Am I still in the world I was in last tick?

    in:  terrain grid, home coord
    out: bool

    forage() gets no "new game" callback, so I have to notice a fresh world
    myself. Can't compare the world object - in c++ a new AntWorld can land on
    a freed address and look identical. Terrain and home never change during a
    world's lifetime, so that pair identifies it.
    """
    return memory.home == home and memory.terrain == terrain


def reset_memory(terrain, markers, home):
    """Wipe memory and size the colony's maps to a new world.

    in:  terrain, marker (pheromone) layer, home coord
    out: nothing, rebuilds the global memory
    """
    memory.terrain = terrain
    memory.home = home
    memory.known_food = [[False] * len(terrain[0]) for _ in range(len(terrain))]
    memory.cost_to_home = compute_travel_cost(terrain, markers, home)
    memory.known_food_count = 0
    memory.no_known_food_ticks = 0
    memory.unaffordable_ticks = 0
    memory.relay_reach = 0
    memory.relay_pairs = 0
    memory.dormant = False


def observe(ants, food_map):
    """Pool every live ant's scan into the shared map.

    in:  list of ants, food_map (read only)
    out: nothing, updates memory.known_food and known_food_count

    Clearing the whole scanned square *before* writing the hits back is the
    important bit. If another ant already took that food, the cell comes back
    empty from the scan and has to be forgotten - otherwise ants keep walking
    to cells that were picked clean ticks ago. Only real scan results go in, so
    the viewing radius is still respected; the ants are just sharing what they
    each saw.
    """
    rows = len(memory.known_food)
    cols = len(memory.known_food[0])

    for ant in ants:
        visible = ant.food_scan(food_map)
        r, c = ant.position
        radius = ant.food_radius

        for i in range(r - radius, r + radius + 1):
            for j in range(c - radius, c + radius + 1):
                if i < 0 or i >= rows or j < 0 or j >= cols:
                    continue
                memory.known_food[i][j] = False

        for cell in visible:
            memory.known_food[cell[0]][cell[1]] = True

    memory.known_food_count = sum(row.count(True) for row in memory.known_food)


def choose_affordable_food(ant, claimed):
    """Cheapest known food this ant can reach and still walk home from.

    in:  ant, list of cells other ants already picked this tick
    out: coord, or (-1, -1) if nothing qualifies

    Rebuilding the cost field from the ant's position every single tick is
    wasteful (one dijkstra per ant per tick) but the maps are tiny and the step
    cap is never the limit, so I'm not optimising it.
    """
    from_ant = compute_cost_field(memory.terrain, ant.position)
    return choose_target(
        memory.known_food, from_ant, memory.cost_to_home, ant.energy, claimed
    )


def step_toward(ant, dest, terrain, food_map):
    """Move the ant one cell toward dest, if that's safe.

    in:  ant, dest coord, terrain, food_map (modified on pickup)
    out: nothing

    "Safe" means two things: it can afford the step, and it can still get home
    from the cell it would land on. Thanks to note 2 that check can be exact.

    Standing on dest still calls move() - it's distance 0, costs nothing, and
    move() is the only thing that picks food up. Returning early there is what
    froze the whole colony on maps where home itself had food: that cell is the
    nearest target forever, so every ant sat on it and scored nothing.
    """
    if memory.cost_to_home[ant.position[0]][ant.position[1]] > ant.energy:
        return  # already stuck, don't make it worse

    if ant.position == dest:
        ant.move(terrain, dest, food_map)
        return

    path = shortest_path(terrain, ant.position, dest)
    if len(path) < 2:
        return

    nxt = path[1]
    step = step_cost(terrain, ant.position, nxt)
    home_after = memory.cost_to_home[nxt[0]][nxt[1]]
    if step > ant.energy or home_after > ant.energy - step:
        return

    ant.move(terrain, nxt, food_map)


def measure_relay(ants):
    """Count-only check: could one ant carry food partway and a second finish?

    in:  list of ants (nothing is moved)
    out: nothing, sets memory.relay_reach / relay_pairs

    The colony keeps ending with ~75 energy spread over ants that can't pay for
    a whole round trip. But update_world() already puts food back on the map
    where a carrying ant dies, so an ant could deliberately walk out, grab food
    and die on the way back, leaving the item closer to home for someone else.

    Deliberately throwing ants away is a judgement call, so for now I'm only
    counting how often the energy would line up:
      relay_reach - how many ants could reach the nearest known food one way
      relay_pairs - 1 if the fullest ant could drop it somewhere a second ant
                    can still afford to fetch from
    Runs once, on the tick where everyone is home and idle.
    """
    nearest = INF
    for i, row in enumerate(memory.known_food):
        for j, known in enumerate(row):
            if known:
                nearest = min(nearest, memory.cost_to_home[i][j])
    if nearest == INF:
        return

    reach = 0
    best_energy = -1
    carrier = None
    for ant in ants:
        if ant.energy > nearest:
            reach += 1
            if ant.energy > best_energy:
                best_energy = ant.energy
                carrier = ant
    memory.relay_reach = reach
    if carrier is None:
        return

    # it walks out (nearest), grabs it, and gets as far back as its leftover
    # energy allows, so the food lands this far from home
    drop = 2 * nearest - best_energy
    if drop <= 0:
        return  # it could just finish the trip itself

    round_trip = 2 * drop
    for ant in ants:
        if ant is carrier:
            continue
        if ant.energy >= round_trip:
            memory.relay_pairs = 1
            return


def forage(world):
    """One tick of the colony. This is AntWorld::forage() in the c++ - it reads
    the maps and ants straight off the world, so it takes no arguments there.

    Observe -> decide -> act, which lines up with the search/decide/act loop the
    challenge description asks for.
    """
    if not is_known_world(world.terrain_map, world.home):
        reset_memory(world.terrain_map, world.pheromone_map, world.home)

    if memory.dormant:
        # Score is already final: everyone's home, nothing left is affordable.
        # The game only ends when the ants are gone or the map is empty, so the
        # run would otherwise burn all 1000 steps and the official binary would
        # print nothing at all. Clearing energy lets update_world() retire them.
        # Nobody is carrying, so this can't cost a point.
        idle = len(world.ants)
        if memory.known_food_count == 0:
            memory.no_known_food_ticks += idle
        else:
            memory.unaffordable_ticks += idle
        for ant in world.ants:
            ant.energy = 0
        return

    # --- observe
    observe(world.ants, world.food_map)

    # --- act, carriers first. They have somewhere to be and no choice to make.
    for ant in world.ants:
        if ant.carrying_food:
            step_toward(ant, ant.home_coord, world.terrain_map, world.food_map)

    # --- decide, for everyone with empty hands
    # Weakest ant goes first. A nearly-empty ant gets the short haul while it
    # can still finish it, instead of a full ant taking that trip and leaving
    # the empty one with nothing it can afford. Worth ~+0.2/game, small but it
    # was positive on more seeds than it hurt.
    seekers = [ant for ant in world.ants if not ant.carrying_food]
    seekers.sort(key=lambda a: a.energy)

    claimed = []  # one target per ant, thrown away at the end of the tick
    for ant in seekers:
        food = choose_affordable_food(ant, claimed)

        # Tried holding the cheap trips back so there'd still be a short haul
        # left once an ant was nearly empty. Every cutoff I tried lost points -
        # a trip an ant can finish is worth taking now.
        # if ant.energy > 60 and round_trip_cost(ant, food) < 6:
        #     continue

        if food[0] < 0:
            # split the idle tick so I can tell "doesn't know where food is"
            # from "knows, can't pay for it" - they'd need different fixes
            if memory.known_food_count == 0:
                memory.no_known_food_ticks += 1
            else:
                memory.unaffordable_ticks += 1
            if ant.position != ant.home_coord:
                step_toward(ant, ant.home_coord, world.terrain_map, world.food_map)
                if ant.carrying_food:
                    memory.known_food[ant.position[0]][ant.position[1]] = False
            continue

        claimed.append(food)
        step_toward(ant, food, world.terrain_map, world.food_map)
        # observe() already ran this tick, so ants later in this loop would
        # still think there's food here. Forget it now.
        if ant.carrying_food:
            memory.known_food[ant.position[0]][ant.position[1]] = False

    still_working = any(
        ant.carrying_food or ant.position != ant.home_coord for ant in world.ants
    )
    if not still_working:
        measure_relay(world.ants)
    memory.dormant = not still_working


# The benchmark wants these four numbers. Here it can just reach into memory,
# but in c++ the benchmark is a separate translation unit and memory lives in an
# anonymous namespace, so each one needs its own little free function
# (colonyNoKnownFoodTicks() and friends) declared on the benchmark side.
def colony_counters():
    return {
        "no_known": memory.no_known_food_ticks,
        "unaffordable": memory.unaffordable_ticks,
        "relay_reach": memory.relay_reach,
        "relay_pairs": memory.relay_pairs,
    }


# ---------------------------------------------------------------------------
# planning helpers
# ---------------------------------------------------------------------------

def compute_cost_field(grid, start):
    """Energy cost from start to every cell on the map.

    in:  grid of heights, start coord
    out: 2d list of costs, INF where there's no route

    Same dijkstra as shortest_path but without a goal - I don't want the route,
    just the price of every cell, and one sweep answers "can I afford this?" for
    the whole map at once. Used twice: from home (once per world) and from each
    ant (every tick).
    """
    rows = len(grid)
    cols = len(grid[0])
    dist = [[INF] * cols for _ in range(rows)]
    dist[start[0]][start[1]] = 0
    open_set = [(0, start)]

    while open_set:
        cost, cell = heapq.heappop(open_set)
        r, c = cell
        if cost != dist[r][c]:
            continue  # stale

        for nxt in neighbours(grid, cell):
            new_cost = cost + step_cost(grid, cell, nxt)
            if new_cost < dist[nxt[0]][nxt[1]]:
                dist[nxt[0]][nxt[1]] = new_cost
                heapq.heappush(open_set, (new_cost, nxt))

    return dist


def compute_travel_cost(terrain, markers, start):
    """Cost field from start, ignoring the marker layer.

    markers is in the signature because I expected to bias travel cost by
    pheromones - cheaper along a trail other ants had used. Never got there:
    the colony already shares one map, so a trail doesn't tell anyone anything
    they don't have. Keeping the parameter so I can try it without touching
    every call site. TODO: drop it if pheromones stay unused.
    """
    return compute_cost_field(terrain, start)


def choose_target(known_food, from_ant, cost_to_home, energy, claimed):
    """Cheapest food cell this ant can do as a complete round trip.

    in:  known_food (2d bools), cost field from the ant, cost field to home,
         the ant's energy, cells already claimed this tick
    out: coord, or (-1, -1) if nothing is affordable

    Affordable is both halves: energy covers the walk out *and* the walk home
    from there. Checking only the way out is what killed ~6 of 8 ants a game -
    they'd arrive at the food with nothing left and die in the field. Ties keep
    the earlier row, then the earlier column, just so the result is stable.
    """
    best = (-1, -1)
    best_round = INF

    for i, row in enumerate(known_food):
        for j, known in enumerate(row):
            if not known or (i, j) in claimed:
                continue

            out_cost = from_ant[i][j]
            home_cost = cost_to_home[i][j]
            if out_cost == INF or home_cost == INF:
                continue
            if out_cost > energy or home_cost > energy - out_cost:
                continue

            trip = out_cost + home_cost
            if trip < best_round:
                best_round = trip
                best = (i, j)

    return best


# ---------------------------------------------------------------------------
# benchmark
#
# dev_challenge itself is a poor measuring stick: the constructor prints every
# ant's starting energy, and if the 1000 step cap is hit it never prints the
# score at all. This runs a pile of seeds, keeps quiet, and always reports the
# score plus the diagnostics I care about.
# ---------------------------------------------------------------------------

MAX_STEPS = 1000


def run_seed(seed, map_x=15, map_y=15, ant_count=8):
    """One whole game, with diagnostics.

    in:  seed, map size, colony size
    out: dict of stats for that game

    Can't just call world_step() - it forages, updates and checks game over in
    one go, and update_world() erases the dead, so by the time it returns I
    can't see who died or where. Running the three parts myself lets me look at
    the ants in between.
    """
    world = World(seed, map_x, map_y, ant_count)
    stats = {
        "score": 0,
        "stranded": 0,
        "backtracks": 0,
        "start_energy": sum(ant.energy for ant in world.ants),
        "leftover": 0,
    }

    prev_start = []
    steps = 0
    over = False

    while not over and steps < MAX_STEPS:
        start = [ant.position for ant in world.ants]

        forage(world)

        # backtrack = an ant stepped off a cell and straight back onto the one
        # it was standing on the tick before, i.e. vibrating instead of making
        # progress. Carrying ants are skipped, otherwise every "walk out, walk
        # back" trip would count. NOTE: the index matching here is only valid
        # while nobody has died, so treat the number as a smell, not a fact.
        if len(prev_start) == len(start):
            for i, ant in enumerate(world.ants):
                if (not ant.carrying_food
                        and ant.position != start[i]
                        and ant.position == prev_start[i]):
                    stats["backtracks"] += 1

        # stranded = died somewhere that isn't home. has to be counted here,
        # before update_world() removes them
        for ant in world.ants:
            if ant.energy == 0 and ant.position != world.home:
                stats["stranded"] += 1

        prev_start = [start[i] for i, ant in enumerate(world.ants) if ant.energy > 0]

        world.update_world()
        over = world.is_game_over()
        steps += 1

    stats["score"] = world.score
    stats["leftover"] = sum(ant.energy for ant in world.ants)
    stats["steps"] = steps
    stats.update(colony_counters())
    return stats


def benchmark(seeds=100, map_x=15, map_y=15, ant_count=8):
    """Run seeds 1000, 1001, ... and print the aggregate.

    Starting at 1000 rather than the default 12345 so I'm not tuning to the one
    seed the graded binary happens to use.
    """
    runs = [run_seed(1000 + i, map_x, map_y, ant_count) for i in range(seeds)]
    scores = [run["score"] for run in runs]

    def mean(key):
        return sum(run[key] for run in runs) / len(runs)

    print(f"seeds={seeds} map={map_x}x{map_y} ants={ant_count}")
    print(f"score mean={mean('score'):.2f} min={min(scores)} max={max(scores)}")
    print(f"stranded={sum(r['stranded'] for r in runs)}"
          f" backtracks={sum(r['backtracks'] for r in runs)}"
          f" no_known mean={mean('no_known'):.1f}"
          f" unaffordable mean={mean('unaffordable'):.1f}")
    print(f"energy start mean={mean('start_energy'):.2f}"
          f" leftover mean={mean('leftover'):.2f}")
    print(f"relay_reach mean={mean('relay_reach'):.2f}"
          f" relay_pairs mean={mean('relay_pairs'):.2f}")


# ---------------------------------------------------------------------------
# checks
#
# Hand-built grids small enough that I can work the right answer out on paper.
# These are for my own helpers only - the framework has its own test file.
# ---------------------------------------------------------------------------

_checks = 0
_failures = 0


def check(condition, description):
    global _checks, _failures
    _checks += 1
    if not condition:
        _failures += 1
        print("FAIL:", description)


def every_cell_reached(field):
    return bool(field) and all(cost != INF for row in field for cost in row)


def test_flat_costs():
    flat = [[0, 0, 0], [0, 0, 0], [0, 0, 0]]
    field = compute_cost_field(flat, (0, 0))

    check(len(field) == 3 and len(field[0]) == 3, "flat field keeps the grid size")
    check(field[0][0] == 0, "standing still costs nothing")
    check(field[0][2] == 2, "flat row costs one per step")
    check(field[2][0] == 2, "flat column costs one per step")
    check(field[2][2] == 4, "flat corner is the manhattan length")
    check(field[2][2] == path_cost(flat, shortest_path(flat, (0, 0), (2, 2))),
          "flat field agrees with shortest_path")
    check(every_cell_reached(field), "every flat cell is reachable")


def test_elevation_costs():
    # the 5 is a wall in all but name: stepping onto it costs 1 + 5
    hills = [[0, 5, 0], [0, 0, 0]]
    field = compute_cost_field(hills, (0, 0))

    check(field[0][1] == 6, "climbing a neighbour costs one plus the height change")
    check(field[0][2] == 4, "field walks around an expensive climb")
    check(field[0][2] == path_cost(hills, shortest_path(hills, (0, 0), (0, 2))),
          "hill field agrees with shortest_path")
    check(every_cell_reached(field), "every hill cell is reachable")


def test_rectangle():
    # caught me assuming rows == cols early on, so it stays
    wide = [[0, 0, 0, 0], [0, 0, 0, 0]]
    field = compute_cost_field(wide, (1, 3))

    check(len(field) == 2 and len(field[0]) == 4, "field keeps a non-square shape")
    check(field[1][3] == 0, "start cell is zero on a shifted origin")
    check(field[0][0] == 4, "opposite corner counts each flat step")
    check(every_cell_reached(field), "a solid rectangle has no unreached cell")


def test_markers_do_not_change_cost():
    terrain = [[0, 1, 0], [0, 0, 0]]
    empty = [[0] * 3 for _ in range(2)]
    saturated = [[1] * 3 for _ in range(2)]
    saturated[0][1] = 9
    home = (1, 2)

    check(compute_travel_cost(terrain, empty, home)
          == compute_travel_cost(terrain, saturated, home),
          "a full marker layer does not change travel cost")


def test_choose_target():
    food = [
        [False, True, True],
        [True, False, False],
    ]
    from_ant = [[0, 1, 3], [2, 0, 0]]
    cost_home = [[0, 3, 1], [2, 0, 0]]

    # (0,1) and (0,2) are both 4 energy round trip, (1,0) is 4 as well
    check(choose_target(food, from_ant, cost_home, 3, []) == (-1, -1),
          "rejects a round trip the ant cannot afford")
    check(choose_target(food, from_ant, cost_home, 4, []) == (0, 1),
          "equal round trips keep the earlier row and column")
    check(choose_target(food, from_ant, cost_home, 4, [(0, 1)]) == (0, 2),
          "skips a claimed cell")
    check(choose_target(food, from_ant, cost_home, 4, [(0, 1), (0, 2), (1, 0)]) == (-1, -1),
          "returns none when every food cell is claimed")


def run_checks():
    test_flat_costs()
    test_elevation_costs()
    test_rectangle()
    test_markers_do_not_change_cost()
    test_choose_target()
    if _failures:
        print(f"{_failures} of {_checks} checks failed.")
    else:
        print(f"all {_checks} checks passed.")


if __name__ == "__main__":
    run_checks()
    # only a handful of seeds - this is slow in python (a dijkstra per ant per
    # tick). The real sweep is 100 seeds once it's in c++.
    benchmark(seeds=10)
