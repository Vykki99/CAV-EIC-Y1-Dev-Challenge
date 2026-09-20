//
// Created by dusan on 9/15/26.
//

#include "../include/antworld.h"

namespace {
    /** @brief Map knowledge shared by the whole colony. Ants hold no state of their own,
     * because dead ants are erased from AntWorld::ants and shift every index after them.
     */
    struct ColonyMemory {
        MapTemplate terrain;
        Coord home = Coord(-1, -1);
        std::vector<std::vector<bool> > knownFood;
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

    /** @brief Closest known food by Manhattan distance. Returns {-1,-1} if none. */
    Coord nearestKnownFood(const Coord from) {
        Coord best(-1, -1);
        int bestDist = std::numeric_limits<int>::max();
        const int rows = static_cast<int>(memory.knownFood.size());
        const int cols = static_cast<int>(memory.knownFood[0].size());

        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                if (!memory.knownFood[i][j]) {
                    continue;
                }
                const int dist = std::abs(from.first - i) + std::abs(from.second - j);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = Coord(i, j);
                }
            }
        }
        return best;
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
            ant.returnHome(this->terrainMap, this->foodMap);
            continue;
        }

        const Coord food = nearestKnownFood(ant.position);
        if (food.first < 0) {
            ++memory.seekMisses;
            continue;
        }
        ant.move(this->terrainMap, food, this->foodMap);
    }
}

/** @brief Ant-ticks this world where a non-carrying ant had no known food to chase. */
int colonySeekMisses() {
    return memory.seekMisses;
}

/** You may insert any custom functions below **/
