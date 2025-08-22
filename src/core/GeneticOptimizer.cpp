
#include "utils/SignalHandler.h"
#include "core/Constants.h"
#include "core/GeneticOptimizer.h"
#include "core/BinPacking.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <chrono>
#include <numeric>
#include <iomanip> // For std::setprecision

namespace BinPacking {

// Constructor
GeneticOptimizer::GeneticOptimizer(const std::vector<MArea>& pieces, const Rectangle2D& binDimension, bool useParallel, int populationSize, int generations, double mutationRate, double crossoverRate)
    : allPieces(pieces),
      binDimension(binDimension),
      useParallel(useParallel),
      populationSize(populationSize),
      generations(generations),
      mutationRate(mutationRate),
      crossoverRate(crossoverRate) {
    rng.seed(std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

// Main GA loop
std::vector<Bin> GeneticOptimizer::run() {
    

    initializePopulation();

    double bestFitnessSoFar = -1e18; // Initialize with a very small number

    for (int gen = 0; gen < generations; ++gen) {
        if (g_interrupt_received) {
            std::cout << "\nCtrl-C detected. Finishing optimization and saving best result..." << std::endl;
            break;
        }

        evaluatePopulation();
        
        // Find the best individual in the current population
        std::sort(population.begin(), population.end());
        Individual& bestOfGeneration = population.back();

        // Print progress if fitness has improved
        if (bestOfGeneration.fitness > bestFitnessSoFar) {
            bestFitnessSoFar = bestOfGeneration.fitness;
            // Deconstruct fitness into bins and area for readability
            double area = bestOfGeneration.fitness + (RectangleUtils::getWidth(binDimension) * RectangleUtils::getHeight(binDimension) * bestOfGeneration.numBins);
            std::cout << "Generation " << gen + 1 << "/" << generations 
                      << " | New best solution! Bins: " << bestOfGeneration.numBins 
                      << ", Area: " << std::fixed << std::setprecision(2) << area 
                      << " (Fitness: " << bestFitnessSoFar << ")" << std::endl;
        } else {
            // Print a less verbose message for other generations to show it's still working
            if ((gen + 1) % 10 == 0) { // Print every 10 generations
                 std::cout << "Generation " << gen + 1 << "/" << generations 
                           << " | Current best fitness: " << bestFitnessSoFar << std::endl;
            }
        }

        selection();
        crossover();
        mutate();
    }

    std::cout << "Finished generations. Finding best solution..." << std::endl;
    evaluatePopulation();
    std::sort(population.begin(), population.end());
    GeneticOptimizer::Individual& bestIndividual = population.back();

    std::vector<MArea> bestSequence;
    bestSequence.reserve(bestIndividual.pieceIndices.size());
    for (size_t i = 0; i < bestIndividual.pieceIndices.size(); ++i) {
        MArea piece = allPieces[bestIndividual.pieceIndices[i]];
        piece.rotate(bestIndividual.rotations[i]);
        bestSequence.push_back(piece);
    }
    
    return BinPacking::pack_ordered(bestSequence, binDimension, useParallel);
}

// Fitness calculation
void GeneticOptimizer::calculateFitness(GeneticOptimizer::Individual& individual) {
    std::vector<MArea> sequence;
    sequence.reserve(individual.pieceIndices.size());
    for (size_t i = 0; i < individual.pieceIndices.size(); ++i) {
        MArea piece = allPieces[individual.pieceIndices[i]];
        piece.rotate(individual.rotations[i]);
        sequence.push_back(piece);
    }

    // Use the full packing algorithm for fitness evaluation to ensure accuracy.
    std::vector<Bin> resultBins = BinPacking::pack_ordered(sequence, binDimension, useParallel);

    if (resultBins.empty()) {
        // This case might happen if not a single piece can be placed.
        // Assign a very low fitness score.
        individual.fitness = -1e18;
        individual.numBins = 0; // No bins used
        return;
    }

    // The fitness function heavily penalizes using more bins.
    // The secondary goal is to maximize the area occupied in all bins.
    double totalOccupiedArea = 0;
    for(const auto& bin : resultBins) {
        totalOccupiedArea += bin.getOccupiedArea();
    }
    
    // The penalty for a new bin must be larger than the max possible occupied area in that bin.
    // Using the bin's own area is a safe and large penalty.
    double binArea = RectangleUtils::getWidth(binDimension) * RectangleUtils::getHeight(binDimension);
    individual.fitness = -binArea * resultBins.size() + totalOccupiedArea;
    individual.numBins = resultBins.size();

    return;
}

// Population initialization
void GeneticOptimizer::initializePopulation() {
    population.clear();
    population.reserve(populationSize);

    GeneticOptimizer::Individual greedyIndividual;
    std::vector<int> indices(allPieces.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        return allPieces[a].getArea() > allPieces[b].getArea();
    });
    greedyIndividual.pieceIndices = indices;
    greedyIndividual.rotations.assign(allPieces.size(), 0);
    population.push_back(greedyIndividual);

    for (int i = 1; i < populationSize; ++i) {
        population.push_back(createRandomIndividual());
    }
}

GeneticOptimizer::Individual GeneticOptimizer::createRandomIndividual() {
    GeneticOptimizer::Individual ind;
    ind.pieceIndices.resize(allPieces.size());
    std::iota(ind.pieceIndices.begin(), ind.pieceIndices.end(), 0);
    std::shuffle(ind.pieceIndices.begin(), ind.pieceIndices.end(), rng);

    ind.rotations.resize(allPieces.size());
    std::uniform_int_distribution<int> dist(0, Constants::ROTATION_ANGLES.size() - 1);
    for (size_t i = 0; i < allPieces.size(); ++i) {
        ind.rotations[i] = Constants::ROTATION_ANGLES[dist(rng)];
    }
    return ind;
}

// Evaluation
void GeneticOptimizer::evaluatePopulation() {
    for (auto& individual : population) {
        calculateFitness(individual);
    }
}

// Selection (Tournament)
void GeneticOptimizer::selection() {
    std::vector<GeneticOptimizer::Individual> newPopulation;
    newPopulation.reserve(populationSize);

    std::sort(population.begin(), population.end());
    newPopulation.push_back(population.back()); // Elitism

    std::uniform_int_distribution<int> dist(0, populationSize - 1);
    for (int i = 1; i < populationSize; ++i) {
        GeneticOptimizer::Individual& p1 = population[dist(rng)];
        GeneticOptimizer::Individual& p2 = population[dist(rng)];
        newPopulation.push_back(p1.fitness > p2.fitness ? p1 : p2);
    }
    population = newPopulation;
}

// Crossover (Ordered Crossover)
void GeneticOptimizer::crossover() {
    std::vector<GeneticOptimizer::Individual> new_population;
    new_population.reserve(populationSize); 

    new_population.push_back(population[0]);

    std::uniform_int_distribution<int> dist(1, populationSize - 1);
    std::uniform_real_distribution<double> r_dist(0.0, 1.0);

    while (new_population.size() < populationSize) {
        GeneticOptimizer::Individual& parent1 = population[dist(rng)];
        GeneticOptimizer::Individual& parent2 = population[dist(rng)];
        
        if (r_dist(rng) < crossoverRate) {
            new_population.push_back(crossoverIndividuals(parent1, parent2));
        } else {
            new_population.push_back(parent1);
        }
    }
    population = new_population;
}

GeneticOptimizer::Individual GeneticOptimizer::crossoverIndividuals(const GeneticOptimizer::Individual& p1, const GeneticOptimizer::Individual& p2) {
    GeneticOptimizer::Individual child;
    child.pieceIndices.assign(p1.pieceIndices.size(), -1);
    child.rotations.assign(p1.rotations.size(), 0);

    std::uniform_int_distribution<int> dist(0, p1.pieceIndices.size() - 1);
    int start = dist(rng);
    int end = dist(rng);
    if (start > end) std::swap(start, end);

    std::vector<bool> used(allPieces.size() + 1, false);

    for (int i = start; i <= end; ++i) {
        child.pieceIndices[i] = p1.pieceIndices[i];
        child.rotations[i] = p1.rotations[i];
        used[p1.pieceIndices[i]] = true;
    }

    int p2_idx = 0;
    for (int i = 0; i < child.pieceIndices.size(); ++i) {
        if (child.pieceIndices[i] == -1) {
            while (used[p2.pieceIndices[p2_idx]]) {
                p2_idx++;
            }
            child.pieceIndices[i] = p2.pieceIndices[p2_idx];
            child.rotations[i] = p2.rotations[p2_idx];
            p2_idx++;
        }
    }
    return child;
}

// Mutation
void GeneticOptimizer::mutate() {
    std::uniform_real_distribution<double> r_dist(0.0, 1.0);
    for (size_t i = 1; i < population.size(); ++i) {
        if (r_dist(rng) < mutationRate) {
            mutateIndividual(population[i]);
        }
    }
}

void GeneticOptimizer::mutateIndividual(GeneticOptimizer::Individual& individual) {
    std::uniform_int_distribution<int> pos_dist(0, individual.pieceIndices.size() - 1);
    std::uniform_int_distribution<int> rot_dist(0, Constants::ROTATION_ANGLES.size() - 1);

    if (std::uniform_int_distribution<int>(0, 1)(rng) == 0) {
        int pos1 = pos_dist(rng);
        int pos2 = pos_dist(rng);
        std::swap(individual.pieceIndices[pos1], individual.pieceIndices[pos2]);
        std::swap(individual.rotations[pos1], individual.rotations[pos2]);
    } else {
        int pos = pos_dist(rng);
        individual.rotations[pos] = Constants::ROTATION_ANGLES[rot_dist(rng)];
    }
}

} // namespace BinPacking
