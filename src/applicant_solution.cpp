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
    }
}

/** @brief Runs one tick of the colony. */
void AntWorld::forage() {
    if (!isKnownWorld(this->terrainMap, this->homeCoordinates)) {
        resetMemory(this->terrainMap, this->homeCoordinates);
    }
}

/** You may insert any custom functions below **/
