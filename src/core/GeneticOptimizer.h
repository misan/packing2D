#pragma once

#include "core/Bin.h"
#include "core/BinPacking.h"
#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include <vector>
#include <random>

namespace BinPacking {

class GeneticOptimizer {
public:
    GeneticOptimizer(const std::vector<MArea>& pieces, const Rectangle2D& binDimension, bool useParallel, int populationSize = 50, int generations = 100, double mutationRate = 0.05, double crossoverRate = 0.7);

    std::vector<Bin> run();

private:
    struct Individual {
        std::vector<int> pieceIndices;
        std::vector<int> rotations; // In degrees
        double fitness = 0.0;
        int numBins = 0;

        bool operator<(const Individual& other) const {
            return fitness < other.fitness;
        }
    };

    void initializePopulation();
    void evaluatePopulation();
    void selection();
    void crossover();
    void mutate();
    void calculateFitness(Individual& individual);

    Individual createRandomIndividual();
    Individual crossoverIndividuals(const Individual& parent1, const Individual& parent2);
    void mutateIndividual(Individual& individual);


    const std::vector<MArea> allPieces;
    const Rectangle2D& binDimension;
    bool useParallel;

    int populationSize;
    int generations;
    double mutationRate;
    double crossoverRate;

    std::vector<Individual> population;
    std::mt19937 rng;
};

} // namespace BinPacking
