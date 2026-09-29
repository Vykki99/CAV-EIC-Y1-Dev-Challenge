#include "../include/colony.h"

#include <iostream>
#include <limits>
#include <string>

namespace {
    int failures = 0;
    int checks = 0;

    void check(bool condition, const std::string &description) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    bool everyCellReached(const MapTemplate &field) {
        const int unreached = std::numeric_limits<int>::max();
        for (const auto &row : field) {
            for (const int cost : row) {
                if (cost == unreached) {
                    return false;
                }
            }
        }
        return !field.empty();
    }

    void testFlatCosts() {
        const MapTemplate flat{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        const MapTemplate field = computeCostField(flat, {0, 0});

        check(field.size() == 3 && field[0].size() == 3, "flat field keeps the grid size");
        check(field[0][0] == 0, "standing still costs nothing");
        check(field[0][2] == 2, "flat row costs one per step");
        check(field[2][0] == 2, "flat column costs one per step");
        check(field[2][2] == 4, "flat corner is the Manhattan length");
        check(field[2][2] == calculatePathCost(flat, shortestPath(flat, {0, 0}, {2, 2})),
              "flat field matches shortestPath");
        check(everyCellReached(field), "every flat cell is reachable");
    }

    void testElevationCosts() {
        const MapTemplate hills{{0, 5, 0}, {0, 0, 0}};
        const MapTemplate field = computeCostField(hills, {0, 0});

        check(field[0][1] == 6, "climbing a neighbor costs one plus the height change");
        check(field[0][2] == 4, "field walks around an expensive climb");
        check(field[0][2] == calculatePathCost(hills, shortestPath(hills, {0, 0}, {0, 2})),
              "hill field matches shortestPath");
        check(everyCellReached(field), "every hill cell is reachable");
    }

    void testRectangle() {
        const MapTemplate wide{{0, 0, 0, 0}, {0, 0, 0, 0}};
        const MapTemplate field = computeCostField(wide, {1, 3});

        check(field.size() == 2 && field[0].size() == 4, "field keeps a non-square shape");
        check(field[1][3] == 0, "start cell is zero on a shifted origin");
        check(field[0][0] == 4, "opposite corner counts each flat step");
        check(everyCellReached(field), "a solid rectangle has no unreached cell");
    }

    void testMarkersDoNotChangeCost() {
        const MapTemplate terrain{{0, 1, 0}, {0, 0, 0}};
        const MapTemplate empty(2, std::vector<int>(3, 0));
        MapTemplate saturated(2, std::vector<int>(3, 1));
        saturated[0][1] = 9;
        const Coord home{1, 2};

        check(computeTravelCost(terrain, empty, home) == computeTravelCost(terrain, saturated, home),
              "a full marker layer does not change travel cost");
    }

    void testChooseTarget() {
        const std::vector<std::vector<bool>> food{
            {false, true, true},
            {true, false, false},
        };
        const MapTemplate fromAnt{{0, 1, 3}, {2, 0, 0}};
        const MapTemplate costHome{{0, 3, 1}, {2, 0, 0}};

        check(chooseTarget(food, fromAnt, costHome, 3, {}) == Coord(-1, -1),
              "chooseTarget rejects a round trip the ant cannot afford");
        check(chooseTarget(food, fromAnt, costHome, 4, {}) == Coord(0, 1),
              "equal round trips keep the earlier row and column");
        check(chooseTarget(food, fromAnt, costHome, 4, {Coord(0, 1)}) == Coord(0, 2),
              "chooseTarget skips a claimed cell");
        check(chooseTarget(food, fromAnt, costHome, 4, {Coord(0, 1), Coord(0, 2), Coord(1, 0)}) == Coord(-1, -1),
              "chooseTarget returns none when every food cell is claimed");
    }
}

int main() {
    testFlatCosts();
    testElevationCosts();
    testRectangle();
    testMarkersDoNotChangeCost();
    testChooseTarget();

    if (failures == 0) {
        std::cout << "All " << checks << " checks passed.\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " checks failed.\n";
    return 1;
}
