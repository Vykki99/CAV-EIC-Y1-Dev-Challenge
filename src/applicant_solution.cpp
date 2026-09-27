//
// Created by dusan on 9/15/26.
//

#include "../include/antworld.h"
#include "../include/colony.h"

namespace {
    /** @brief Map knowledge shared by the whole colony. Ants hold no state of their own,
     * because dead ants are erased from AntWorld::ants and shift every index after them.
     */
    struct ColonyMemory {
        MapTemplate terrain;
        Coord home = Coord(-1, -1);
        std::vector<std::vector<bool> > knownFood;
        MapTemplate costToHome;
        int seekMisses = 0;
    };

    ColonyMemory memory;

    /** @brief Terrain and home are fixed for a world's lifetime, so they identify it.
     * Comparing the AntWorld pointer would not, since a new world can reuse a freed address.
     */
    bool isKnownWorld(const MapTemplate &terrain, const Coord home) {
        return memory.home == home && memory.terrain == terrain;
    }

    /** @brief Wipes memory and resizes the colony map to fit a new world. */
    void resetMemory(const MapTemplate &terrain, const Coord home) {
        memory.terrain = terrain;
        memory.home = home;
        memory.knownFood.assign(terrain.size(), std::vector<bool>(terrain[0].size(), false));
        memory.costToHome = computeCostField(terrain, home);
        memory.seekMisses = 0;
    }

    /** @brief Updates knownFood from every live ant's foodScan. Empty cells in radius
     * are cleared so a pickup another ant made is forgotten once someone sees it.
     */
    void observe(std::vector<Ant> &ants, MapTemplate &foodMap) {
        for (Ant &ant : ants) {
            const std::vector<Coord> visible = ant.foodScan(foodMap);
            const int radius = ant.foodRadius;
            const int rows = static_cast<int>(memory.knownFood.size());
            const int cols = static_cast<int>(memory.knownFood[0].size());

            for (int i = ant.position.first - radius; i <= ant.position.first + radius; ++i) {
                for (int j = ant.position.second - radius; j <= ant.position.second + radius; ++j) {
                    if (i < 0 || i >= rows || j < 0 || j >= cols) {
                        continue;
                    }
                    memory.knownFood[i][j] = false;
                }
            }

            for (const Coord &cell : visible) {
                memory.knownFood[cell.first][cell.second] = true;
            }
        }
    }

    /** @brief Cheapest known food this ant can reach and still walk home.
     * Returns {-1,-1} when nothing is affordable.
     */
    Coord chooseAffordableFood(const Ant &ant) {
        const MapTemplate fromAnt = computeCostField(memory.terrain, ant.position);
        const int unreached = std::numeric_limits<int>::max();
        Coord best(-1, -1);
        int bestRound = unreached;
        const int rows = static_cast<int>(memory.knownFood.size());
        const int cols = static_cast<int>(memory.knownFood[0].size());

        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                if (!memory.knownFood[i][j]) {
                    continue;
                }
                const int outCost = fromAnt[i][j];
                const int homeCost = memory.costToHome[i][j];
                if (outCost == unreached || homeCost == unreached) {
                    continue;
                }
                if (outCost > ant.energy || homeCost > ant.energy - outCost) {
                    continue;
                }
                const int round = outCost + homeCost;
                if (round < bestRound) {
                    bestRound = round;
                    best = Coord(i, j);
                }
            }
        }
        return best;
    }

    /** @brief Walks one cell toward dest, but only if the ant can still reach home
     * from the cell it would land on. Standing on dest still calls move() so food
     * there is collected at zero cost.
     */
    void stepToward(Ant &ant, const Coord dest, MapTemplate &terrain, MapTemplate &food) {
        const int homeHere = memory.costToHome[ant.position.first][ant.position.second];
        if (homeHere > ant.energy) {
            return;
        }
        if (ant.position == dest) {
            ant.move(terrain, dest, food);
            return;
        }
        const std::vector<Coord> path = shortestPath(terrain, ant.position, dest);
        if (path.size() < 2) {
            return;
        }
        const Coord next = path[1];
        const int step = 1 + std::abs(terrain[ant.position.first][ant.position.second] -
                                       terrain[next.first][next.second]);
        const int homeAfter = memory.costToHome[next.first][next.second];
        if (step > ant.energy || homeAfter > ant.energy - step) {
            return;
        }
        ant.move(terrain, next, food);
    }
}

/** @brief Runs one tick of the colony. */
void AntWorld::forage() {
    if (!isKnownWorld(this->terrainMap, this->homeCoordinates)) {
        resetMemory(this->terrainMap, this->homeCoordinates);
    }

    observe(this->ants, this->foodMap);

    for (Ant &ant : this->ants) {
        if (ant.carryingFood) {
            stepToward(ant, ant.homeCoord, this->terrainMap, this->foodMap);
            continue;
        }

        const Coord food = chooseAffordableFood(ant);
        if (food.first < 0) {
            ++memory.seekMisses;
            if (ant.position != ant.homeCoord) {
                stepToward(ant, ant.homeCoord, this->terrainMap, this->foodMap);
                if (ant.carryingFood) {
                    memory.knownFood[ant.position.first][ant.position.second] = false;
                }
            }
            continue;
        }
        stepToward(ant, food, this->terrainMap, this->foodMap);
        // This tick's observe() already ran, so forget the pickup for ants still to move.
        if (ant.carryingFood) {
            memory.knownFood[ant.position.first][ant.position.second] = false;
        }
    }
}

/** @brief Ant-ticks where a non-carrying ant had no affordable known food. */
int colonySeekMisses() {
    return memory.seekMisses;
}

/** You may insert any custom functions below **/

MapTemplate computeCostField(const MapTemplate &grid, const Coord start) {
    const int rows = static_cast<int>(grid.size());
    const int cols = static_cast<int>(grid[0].size());
    const int unreached = std::numeric_limits<int>::max();

    MapTemplate dist(rows, std::vector<int>(cols, unreached));
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    dist[start.first][start.second] = 0;
    open.push({0, start});

    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};

    while (!open.empty()) {
        const Node current = open.top();
        open.pop();
        const int cost = current.cost;
        const int r = current.pos.first;
        const int c = current.pos.second;
        if (cost != dist[r][c]) {
            continue;
        }

        for (int i = 0; i < 4; ++i) {
            const int nr = r + dr[i];
            const int nc = c + dc[i];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) {
                continue;
            }

            const int step = 1 + std::abs(grid[r][c] - grid[nr][nc]);
            const int next = cost + step;
            if (next < dist[nr][nc]) {
                dist[nr][nc] = next;
                open.push({next, {nr, nc}});
            }
        }
    }

    return dist;
}
