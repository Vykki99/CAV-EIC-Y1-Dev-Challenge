#include "../include/antworld.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {
    constexpr int kMaxSteps = 1000;

    /** @brief Silences AntWorld's constructor energy prints, then runs one game. */
    int runSeed(const uint32_t seed, const int mapX, const int mapY, const int antCount) {
        std::ostringstream sink;
        std::streambuf *const previous = std::cout.rdbuf(sink.rdbuf());
        AntWorld world(seed, mapX, mapY, antCount);
        std::cout.rdbuf(previous);

        bool over = false;
        int steps = 0;
        while (!over && steps < kMaxSteps) {
            over = world.worldStep();
            ++steps;
        }
        return world.score;
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
    int scoreMin = 0;
    int scoreMax = 0;

    for (int i = 0; i < seeds; ++i) {
        const int score = runSeed(1000u + static_cast<uint32_t>(i), mapX, mapY, ants);
        scoreSum += score;
        if (i == 0 || score < scoreMin) {
            scoreMin = score;
        }
        if (i == 0 || score > scoreMax) {
            scoreMax = score;
        }
    }

    const double mean = seeds > 0 ? static_cast<double>(scoreSum) / seeds : 0.0;
    std::cout << "seeds=" << seeds << " map=" << mapX << "x" << mapY << " ants=" << ants << '\n';
    std::cout << "score mean=" << mean << " min=" << scoreMin << " max=" << scoreMax << '\n';
    return 0;
}
