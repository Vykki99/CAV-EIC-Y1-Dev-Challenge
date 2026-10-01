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
        int startEnergy = 0;
        int leftoverEnergy = 0;
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
        for (const Ant &ant : world.ants) {
            stats.startEnergy += ant.energy;
        }
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
        for (const Ant &ant : world.ants) {
            stats.leftoverEnergy += ant.energy;
        }
        return stats;
    }
}

int main(int argc, char **argv) {
    int seeds = 100;
    int mapX = 15;
    int mapY = 15;
    int ants = 8;
    bool csv = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--csv") {
            csv = true;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() > 0) {
        seeds = std::stoi(positional[0]);
    }
    if (positional.size() > 1) {
        mapX = std::stoi(positional[1]);
    }
    if (positional.size() > 2) {
        mapY = std::stoi(positional[2]);
    }
    if (positional.size() > 3) {
        ants = std::stoi(positional[3]);
    }

    long long scoreSum = 0;
    long long strandedSum = 0;
    long long backtrackSum = 0;
    long long noKnownSum = 0;
    long long unaffordableSum = 0;
    long long startEnergySum = 0;
    long long leftoverSum = 0;
    int scoreMin = 0;
    int scoreMax = 0;

    if (csv) {
        std::cout << "seed,score,stranded,backtracks,no_known,unaffordable,start_energy,leftover\n";
    }

    for (int i = 0; i < seeds; ++i) {
        const uint32_t seed = 1000u + static_cast<uint32_t>(i);
        const RunStats run = runSeed(seed, mapX, mapY, ants);
        scoreSum += run.score;
        strandedSum += run.strandedDeaths;
        backtrackSum += run.backtracks;
        noKnownSum += run.noKnownFoodTicks;
        unaffordableSum += run.unaffordableTicks;
        startEnergySum += run.startEnergy;
        leftoverSum += run.leftoverEnergy;
        if (i == 0 || run.score < scoreMin) {
            scoreMin = run.score;
        }
        if (i == 0 || run.score > scoreMax) {
            scoreMax = run.score;
        }
        if (csv) {
            std::cout << seed << ',' << run.score << ',' << run.strandedDeaths << ','
                      << run.backtracks << ',' << run.noKnownFoodTicks << ','
                      << run.unaffordableTicks << ',' << run.startEnergy << ','
                      << run.leftoverEnergy << '\n';
        }
    }

    const double mean = seeds > 0 ? static_cast<double>(scoreSum) / seeds : 0.0;
    const double noKnownMean = seeds > 0 ? static_cast<double>(noKnownSum) / seeds : 0.0;
    const double unaffordableMean = seeds > 0 ? static_cast<double>(unaffordableSum) / seeds : 0.0;
    const double startMean = seeds > 0 ? static_cast<double>(startEnergySum) / seeds : 0.0;
    const double leftoverMean = seeds > 0 ? static_cast<double>(leftoverSum) / seeds : 0.0;
    std::cout << "seeds=" << seeds << " map=" << mapX << "x" << mapY << " ants=" << ants << '\n';
    std::cout << "score mean=" << mean << " min=" << scoreMin << " max=" << scoreMax << '\n';
    std::cout << "stranded=" << strandedSum
              << " backtracks=" << backtrackSum
              << " no_known mean=" << noKnownMean
              << " unaffordable mean=" << unaffordableMean << '\n';
    std::cout << "energy start mean=" << startMean << " leftover mean=" << leftoverMean << '\n';
    return 0;
}
