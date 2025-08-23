#include "utils/SignalHandler.h"
#include "core/Constants.h"
#include "core/SimulatedAnnealingOptimizer.h"
#include "core/BinPacking.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <chrono>
#include <numeric>
#include <iomanip> // For std::setprecision

namespace BinPacking {

// Constructor
SimulatedAnnealingOptimizer::SimulatedAnnealingOptimizer(const std::vector<MArea>& pieces, const Rectangle2D& binDimension, bool useParallel, double initialTemperature, double coolingRate, int iterations)
    : allPieces(pieces),
      binDimension(binDimension),
      useParallel(useParallel),
      initialTemperature(initialTemperature),
      coolingRate(coolingRate),
      iterations(iterations) {
    setup_signal_handler();
    rng.seed(std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

// Main SA loop
std::vector<Bin> SimulatedAnnealingOptimizer::run() {
    initializeSolution();
    calculateFitness(currentSolution);
    bestSolution = currentSolution;

    double temperature = initialTemperature;

    for (int i = 0; i < iterations; ++i) {
        if (g_interrupt_received) {
            std::cout << "\nCtrl-C detected. Finishing optimization and saving best result..." << std::endl;
            break;
        }

        Solution neighbor = getNeighbor(currentSolution);
        calculateFitness(neighbor);

        if (neighbor.fitness > currentSolution.fitness) {
            currentSolution = neighbor;
            if (currentSolution.fitness > bestSolution.fitness) {
                bestSolution = currentSolution;
                double area = bestSolution.fitness + (RectangleUtils::getWidth(binDimension) * RectangleUtils::getHeight(binDimension) * bestSolution.numBins);
                std::cout << "Iteration " << i + 1 << "/" << iterations
                          << " | New best solution! Bins: " << bestSolution.numBins
                          << ", Area: " << std::fixed << std::setprecision(2) << area
                          << " (Fitness: " << bestSolution.fitness << ")" << std::endl;
            }
        } else {
            if (acceptanceProbability(currentSolution.fitness, neighbor.fitness, temperature) > std::uniform_real_distribution<double>(0.0, 1.0)(rng)) {
                currentSolution = neighbor;
            }
        }

        temperature *= coolingRate;
        if ((i + 1) % 1000 == 0) { // Print every 1000 iterations
            calculateFitness(currentSolution);
            std::cout << "Iteration " << i + 1 << "/" << iterations
                      << " | Current Bins: " << currentSolution.numBins
                      << " | Current fitness: " << currentSolution.fitness
                      << " | Temperature: " << temperature << std::endl;
        }
    }

    std::cout << "Finished iterations." << std::endl;

    // Run the full packing algorithm on the best solution found
    std::vector<MArea> bestSequence;
    bestSequence.reserve(bestSolution.pieceIndices.size());
    for (size_t j = 0; j < bestSolution.pieceIndices.size(); ++j) {
        MArea piece = allPieces[bestSolution.pieceIndices[j]];
        piece.rotate(bestSolution.rotations[j]);
        bestSequence.push_back(piece);
    }

    return BinPacking::pack_ordered(bestSequence, binDimension, useParallel);
}

// Fitness calculation
void SimulatedAnnealingOptimizer::calculateFitness(Solution& solution) {
    std::vector<MArea> sequence;
    sequence.reserve(solution.pieceIndices.size());
    for (size_t i = 0; i < solution.pieceIndices.size(); ++i) {
        MArea piece = allPieces[solution.pieceIndices[i]];
        piece.rotate(solution.rotations[i]);
        sequence.push_back(piece);
    }

    std::vector<Bin> resultBins = BinPacking::pack_fast(sequence, binDimension);

    if (resultBins.empty()) {
        solution.fitness = -1e18;
        solution.numBins = 0;
        return;
    }

    double totalOccupiedArea = 0;
    for (const auto& bin : resultBins) {
        totalOccupiedArea += bin.getOccupiedArea();
    }

    double binArea = RectangleUtils::getWidth(binDimension) * RectangleUtils::getHeight(binDimension);
    solution.fitness = -binArea * resultBins.size() + totalOccupiedArea;
    solution.numBins = resultBins.size();
}

// Population initialization
void SimulatedAnnealingOptimizer::initializeSolution() {
    // Generate a few random initial solutions and pick the best one
    const int numInitialSolutions = 10;
    Solution bestInitialSolution;
    bestInitialSolution.fitness = -1e18; // Initialize with a very low fitness

    for (int i = 0; i < numInitialSolutions; ++i) {
        Solution newSolution;
        newSolution.pieceIndices.resize(allPieces.size());
        std::iota(newSolution.pieceIndices.begin(), newSolution.pieceIndices.end(), 0);
        std::shuffle(newSolution.pieceIndices.begin(), newSolution.pieceIndices.end(), rng);

        newSolution.rotations.resize(allPieces.size());
        std::uniform_int_distribution<int> dist(0, Constants::ROTATION_ANGLES.size() - 1);
        for (size_t j = 0; j < allPieces.size(); ++j) {
            newSolution.rotations[j] = Constants::ROTATION_ANGLES[dist(rng)];
        }

        calculateFitness(newSolution);

        if (newSolution.fitness > bestInitialSolution.fitness) {
            bestInitialSolution = newSolution;
        }
    }
    currentSolution = bestInitialSolution;
}

SimulatedAnnealingOptimizer::Solution SimulatedAnnealingOptimizer::getNeighbor(const Solution& solution) {
    Solution neighbor = solution;
    std::uniform_int_distribution<int> pos_dist(0, neighbor.pieceIndices.size() - 1);
    std::uniform_int_distribution<int> rot_dist(0, Constants::ROTATION_ANGLES.size() - 1);
    std::uniform_int_distribution<int> move_type_dist(0, 2);

    int move_type = move_type_dist(rng);

    if (move_type == 0) { // Swap two pieces
        int pos1 = pos_dist(rng);
        int pos2 = pos_dist(rng);
        std::swap(neighbor.pieceIndices[pos1], neighbor.pieceIndices[pos2]);
        std::swap(neighbor.rotations[pos1], neighbor.rotations[pos2]);
    } else if (move_type == 1) { // Change rotation of a piece
        int pos = pos_dist(rng);
        neighbor.rotations[pos] = Constants::ROTATION_ANGLES[rot_dist(rng)];
    } else { // Move a block of pieces
        int block_size = std::uniform_int_distribution<int>(1, neighbor.pieceIndices.size() / 4)(rng);
        int start_pos = std::uniform_int_distribution<int>(0, neighbor.pieceIndices.size() - block_size)(rng);
        int new_pos = std::uniform_int_distribution<int>(0, neighbor.pieceIndices.size() - block_size)(rng);

        std::vector<int> block_indices(neighbor.pieceIndices.begin() + start_pos, neighbor.pieceIndices.begin() + start_pos + block_size);
        std::vector<int> block_rotations(neighbor.rotations.begin() + start_pos, neighbor.rotations.begin() + start_pos + block_size);

        neighbor.pieceIndices.erase(neighbor.pieceIndices.begin() + start_pos, neighbor.pieceIndices.begin() + start_pos + block_size);
        neighbor.rotations.erase(neighbor.rotations.begin() + start_pos, neighbor.rotations.begin() + start_pos + block_size);

        neighbor.pieceIndices.insert(neighbor.pieceIndices.begin() + new_pos, block_indices.begin(), block_indices.end());
        neighbor.rotations.insert(neighbor.rotations.begin() + new_pos, block_rotations.begin(), block_rotations.end());
    }

    return neighbor;
}

double SimulatedAnnealingOptimizer::acceptanceProbability(double oldFitness, double newFitness, double temperature) {
    if (newFitness > oldFitness) {
        return 1.0;
    }
    return exp((newFitness - oldFitness) / temperature);
}

} // namespace BinPacking
