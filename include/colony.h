#ifndef DEV_CHALLENGE_COLONY_H
#define DEV_CHALLENGE_COLONY_H

#include <numeric>
#include <random>

#include "utility_functions.h"

/** @brief Energy cost from start to every cell. Unreached cells stay at INT_MAX. */
MapTemplate computeCostField(const MapTemplate &grid, Coord start);

/** @brief Same cost as computeCostField. markers is accepted and not read. */
MapTemplate computeTravelCost(const MapTemplate &terrain, const MapTemplate &markers, Coord start);

#endif
