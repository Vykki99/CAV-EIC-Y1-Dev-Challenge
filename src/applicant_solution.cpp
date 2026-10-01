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
        int knownFoodCount = 0;
        int noKnownFoodTicks = 0;
        int unaffordableTicks = 0;
        bool dormant = false;
    };

    ColonyMemory memory;

    /** @brief Terrain and home are fixed for a world's lifetime, so they identify it.
     * Comparing the AntWorld pointer would not, since a new world can reuse a freed address.
     */
    bool isKnownWorld(const MapTemplate &terrain, const Coord home) {
        return memory.home == home && memory.terrain == terrain;
    }

    /** @brief Wipes memory and resizes the colony map to fit a new world. */
    void resetMemory(const MapTemplate &terrain, const MapTemplate &markers, const Coord home) {
        memory.terrain = terrain;
        memory.home = home;
        memory.knownFood.assign(terrain.size(), std::vector<bool>(terrain[0].size(), false));
        memory.costToHome = computeTravelCost(terrain, markers, home);
        memory.knownFoodCount = 0;
        memory.noKnownFoodTicks = 0;
        memory.unaffordableTicks = 0;
        memory.dormant = false;
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

        int known = 0;
        for (const auto &row : memory.knownFood) {
            for (const bool cell : row) {
                if (cell) {
                    ++known;
                }
            }
        }
        memory.knownFoodCount = known;
    }

    /** @brief Cheapest known food this ant can reach and still walk home. */
    Coord chooseAffordableFood(const Ant &ant, const std::vector<Coord> &claimed) {
        const MapTemplate fromAnt = computeCostField(memory.terrain, ant.position);
        return chooseTarget(memory.knownFood, fromAnt, memory.costToHome, ant.energy, claimed);
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
        resetMemory(this->terrainMap, this->pheromoneMap, this->homeCoordinates);
    }

    if (memory.dormant) {
        const int idle = static_cast<int>(this->ants.size());
        if (memory.knownFoodCount == 0) {
            memory.noKnownFoodTicks += idle;
        } else {
            memory.unaffordableTicks += idle;
        }
        return;
    }

    observe(this->ants, this->foodMap);

    for (Ant &ant : this->ants) {
        if (ant.carryingFood) {
            stepToward(ant, ant.homeCoord, this->terrainMap, this->foodMap);
        }
    }

    std::vector<std::size_t> seekers;
    for (std::size_t i = 0; i < this->ants.size(); ++i) {
        if (!this->ants[i].carryingFood) {
            seekers.push_back(i);
        }
    }
    std::sort(seekers.begin(), seekers.end(), [&](const std::size_t a, const std::size_t b) {
        if (this->ants[a].energy != this->ants[b].energy) {
            return this->ants[a].energy < this->ants[b].energy;
        }
        return a < b;
    });

    std::vector<Coord> claimed;
    for (const std::size_t index : seekers) {
        Ant &ant = this->ants[index];
        const Coord food = chooseAffordableFood(ant, claimed);
        if (food.first < 0) {
            if (memory.knownFoodCount == 0) {
                ++memory.noKnownFoodTicks;
            } else {
                ++memory.unaffordableTicks;
            }
            if (ant.position != ant.homeCoord) {
                stepToward(ant, ant.homeCoord, this->terrainMap, this->foodMap);
                if (ant.carryingFood) {
                    memory.knownFood[ant.position.first][ant.position.second] = false;
                }
            }
            continue;
        }
        claimed.push_back(food);
        stepToward(ant, food, this->terrainMap, this->foodMap);
        // This tick's observe() already ran, so forget the pickup for ants still to move.
        if (ant.carryingFood) {
            memory.knownFood[ant.position.first][ant.position.second] = false;
        }
    }

    bool stillWorking = false;
    for (const Ant &ant : this->ants) {
        if (ant.carryingFood || ant.position != ant.homeCoord) {
            stillWorking = true;
            break;
        }
    }
    memory.dormant = !stillWorking;
}

/** @brief Ant-ticks where a non-carrying ant knew of no food. */
int colonyNoKnownFoodTicks() {
    return memory.noKnownFoodTicks;
}

/** @brief Ant-ticks where known food existed but none of it was affordable. */
int colonyUnaffordableTicks() {
    return memory.unaffordableTicks;
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

MapTemplate computeTravelCost(const MapTemplate &terrain, const MapTemplate &markers, const Coord start) {
    (void)markers;
    return computeCostField(terrain, start);
}

Coord chooseTarget(
    const std::vector<std::vector<bool>> &knownFood,
    const MapTemplate &fromAnt,
    const MapTemplate &costToHome,
    const int energy,
    const std::vector<Coord> &claimed) {
    const int unreached = std::numeric_limits<int>::max();
    Coord best(-1, -1);
    int bestRound = unreached;
    const int rows = static_cast<int>(knownFood.size());
    const int cols = rows > 0 ? static_cast<int>(knownFood[0].size()) : 0;

    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            if (!knownFood[i][j]) {
                continue;
            }
            bool taken = false;
            for (const Coord &cell : claimed) {
                if (cell.first == i && cell.second == j) {
                    taken = true;
                    break;
                }
            }
            if (taken) {
                continue;
            }
            const int outCost = fromAnt[i][j];
            const int homeCost = costToHome[i][j];
            if (outCost == unreached || homeCost == unreached) {
                continue;
            }
            if (outCost > energy || homeCost > energy - outCost) {
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
