#include "../include/antworld.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int colonyNoKnownFoodTicks();
int colonyUnaffordableTicks();

namespace {
    constexpr int kMaxSteps = 1000;

    struct RunStats {
        int score = 0;
        int strandedDeaths = 0;
        int backtracks = 0;
        int noKnownFoodTicks = 0;
        int unaffordableTicks = 0;
    };

    /** @brief One game, with diagnostics. Splits worldStep so deaths can be seen
     * before exhausted ants are erased from the vector.
     */
    RunStats runSeed(const uint32_t seed, const int mapX, const int mapY, const int antCount) {
        std::ostringstream sink;
        std::streambuf *const previous = std::cout.rdbuf(sink.rdbuf());
        AntWorld world(seed, mapX, mapY, antCount);
        std::cout.rdbuf(previous);

        RunStats stats;
        std::vector<Coord> previousStart;
        bool over = false;
        int steps = 0;

        while (!over && steps < kMaxSteps) {
            std::vector<Coord> start;
            start.reserve(world.ants.size());
            for (const Ant &ant : world.ants) {
                start.push_back(ant.position);
            }

            world.forage();

            if (previousStart.size() == start.size()) {
                for (std::size_t i = 0; i < world.ants.size(); ++i) {
                    if (!world.ants[i].carryingFood &&
                        world.ants[i].position != start[i] &&
                        world.ants[i].position == previousStart[i]) {
                        ++stats.backtracks;
                    }
                }
            }

            for (const Ant &ant : world.ants) {
                if (ant.energy == 0 && ant.position != world.homeCoordinates) {
                    ++stats.strandedDeaths;
                }
            }

            std::vector<Coord> nextPrevious;
            for (std::size_t i = 0; i < world.ants.size(); ++i) {
                if (world.ants[i].energy > 0) {
                    nextPrevious.push_back(start[i]);
                }
            }

            world.updateWorld();
            over = world.isGameOver();
            previousStart = std::move(nextPrevious);
            ++steps;
        }

        stats.score = world.score;
        stats.noKnownFoodTicks = colonyNoKnownFoodTicks();
        stats.unaffordableTicks = colonyUnaffordableTicks();
        return stats;
    }
}

int main(int argc, char **argv) {
    int seeds = 20;
    int mapX = 15;
    int mapY = 15;
    int ants = 8;
    if (argc > 1) {
        seeds = std::stoi(argv[1]);
    }
    if (argc > 2) {
        mapX = std::stoi(argv[2]);
    }
    if (argc > 3) {
        mapY = std::stoi(argv[3]);
    }
    if (argc > 4) {
        ants = std::stoi(argv[4]);
    }

    long long scoreSum = 0;
    long long strandedSum = 0;
    long long backtrackSum = 0;
    long long noKnownSum = 0;
    long long unaffordableSum = 0;
    int scoreMin = 0;
    int scoreMax = 0;

    for (int i = 0; i < seeds; ++i) {
        const RunStats run = runSeed(1000u + static_cast<uint32_t>(i), mapX, mapY, ants);
        scoreSum += run.score;
        strandedSum += run.strandedDeaths;
        backtrackSum += run.backtracks;
        noKnownSum += run.noKnownFoodTicks;
        unaffordableSum += run.unaffordableTicks;
        if (i == 0 || run.score < scoreMin) {
            scoreMin = run.score;
        }
        if (i == 0 || run.score > scoreMax) {
            scoreMax = run.score;
        }
    }

    const double mean = seeds > 0 ? static_cast<double>(scoreSum) / seeds : 0.0;
    const double noKnownMean = seeds > 0 ? static_cast<double>(noKnownSum) / seeds : 0.0;
    const double unaffordableMean = seeds > 0 ? static_cast<double>(unaffordableSum) / seeds : 0.0;
    std::cout << "seeds=" << seeds << " map=" << mapX << "x" << mapY << " ants=" << ants << '\n';
    std::cout << "score mean=" << mean << " min=" << scoreMin << " max=" << scoreMax << '\n';
    std::cout << "stranded=" << strandedSum
              << " backtracks=" << backtrackSum
              << " no_known mean=" << noKnownMean
              << " unaffordable mean=" << unaffordableMean << '\n';
    return 0;
}
