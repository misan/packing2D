#pragma once

#include "core/Bin.h"
#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include <vector>
#include <random>

namespace BinPacking {

class SimulatedAnnealingOptimizer {
public:
    SimulatedAnnealingOptimizer(const std::vector<MArea>& pieces, const Rectangle2D& binDimension, bool useParallel, double initialTemperature = 1000.0, double coolingRate = 0.9999, int iterations = 100000);

    std::vector<Bin> run();

private:
    struct Solution {
        std::vector<int> pieceIndices;
        std::vector<int> rotations; // In degrees
        double fitness = 0.0;
        int numBins = 0;
    };

    void initializeSolution();
    void calculateFitness(Solution& solution);
    Solution getNeighbor(const Solution& solution);
    double acceptanceProbability(double oldFitness, double newFitness, double temperature);

    const std::vector<MArea> allPieces;
    const Rectangle2D& binDimension;
    bool useParallel;

    double initialTemperature;
    double coolingRate;
    int iterations;

    Solution currentSolution;
    Solution bestSolution;

    std::mt19937 rng;
};

} // namespace BinPacking
