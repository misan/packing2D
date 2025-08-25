#pragma once

#include "src/core/Bin.h"
#include "src/primitives/MArea.h"
#include "src/primitives/Rectangle.h"
#include <vector>
#include <random>
#include <chrono>
#include <atomic>

namespace HybridBinPacking {

/**
 * @brief Configuration parameters for the hybrid algorithm
 */
struct HybridConfig {
    // Simulated Annealing parameters
    double initialTemperature = 100.0;
    double coolingRate = 0.95;
    int saIterationsPerTemp = 50;
    
    // Local Search parameters
    int maxLocalSearchIterations = 100;
    int noImprovementThreshold = 20;
    
    // Hybrid parameters
    double greedyWeight = 0.7;  // Weight for greedy component in hybrid evaluation
    int populationSize = 10;     // Size of population for hybrid approach
    int eliteSize = 2;           // Number of elite solutions to preserve
    
    // Performance parameters
    bool useParallel = true;
    int timeLimitMs = 30000;     // Time limit in milliseconds
};

/**
 * @brief Represents a solution in the hybrid algorithm
 */
struct Solution {
    std::vector<Bin> bins;
    double fitness;  // Lower is better
    double utilization;  // Higher is better
    std::vector<int> pieceOrder;  // Order in which pieces were placed
    
    Solution() : fitness(std::numeric_limits<double>::max()), utilization(0.0) {}
    
    bool operator<(const Solution& other) const {
        return fitness < other.fitness;
    }
};

/**
 * @brief Hybrid bin packing algorithm combining greedy with metaheuristic techniques
 */
class HybridPacker {
public:
    HybridPacker(const Rectangle2D& binDimension, const HybridConfig& config = HybridConfig());
    
    /**
     * @brief Pack pieces using hybrid approach
     * @param pieces The pieces to be packed
     * @return A vector of Bins, each containing the pieces placed within it
     */
    std::vector<Bin> pack(std::vector<MArea>& pieces);
    
    /**
     * @brief Get statistics about the last packing run
     */
    struct Stats {
        int totalIterations;
        int saIterations;
        int localSearchIterations;
        double bestFitness;
        double bestUtilization;
        int64_t executionTimeMs;
    };
    
    const Stats& getStats() const { return stats; }
    
private:
    Rectangle2D binDimension;
    HybridConfig config;
    std::mt19937 rng;
    Stats stats;
    std::atomic<bool> timeLimitReached;
    std::vector<MArea> originalPieces;  // Store reference to original pieces
    
    /**
     * @brief Initialize population with diverse solutions
     */
    std::vector<Solution> initializePopulation(const std::vector<MArea>& pieces);
    
    /**
     * @brief Generate initial solution using greedy approach with randomization
     */
    Solution generateGreedySolution(const std::vector<MArea>& pieces, double randomness = 0.0);
    
    /**
     * @brief Evaluate a solution and calculate fitness
     */
    void evaluateSolution(Solution& solution);
    
    /**
     * @brief Reconstruct solution bins based on piece order
     */
    void reconstructSolution(Solution& solution);
    
    /**
     * @brief Simulated Annealing local search
     */
    Solution simulatedAnnealing(const Solution& initialSolution);
    
    /**
     * @brief Generate neighbor solution for SA
     */
    Solution generateNeighbor(const Solution& solution);
    
    /**
     * @brief Local search with guided perturbations
     */
    Solution localSearch(const Solution& solution);
    
    /**
     * @brief Select parents for crossover (tournament selection)
     */
    std::vector<Solution> selectParents(const std::vector<Solution>& population);
    
    /**
     * @brief Crossover operation between two solutions
     */
    Solution crossover(const Solution& parent1, const Solution& parent2);
    
    /**
     * @brief Mutate a solution
     */
    void mutate(Solution& solution, double mutationRate);
    
    /**
     * @brief Replace population using elitism and diversity
     */
    std::vector<Solution> replacePopulation(
        const std::vector<Solution>& population, 
        const std::vector<Solution>& offspring);
    
    /**
     * @brief Check if time limit has been reached
     */
    bool checkTimeLimit(const std::chrono::steady_clock::time_point& startTime);
};

/**
 * @brief Main strategy for the 2D bin packing problem using hybrid approach.
 * 
 * @param pieces The pieces to be packed. The vector may be modified (sorted).
 * @param binDimension The dimensions of the bins to use.
 * @param config Configuration for the hybrid algorithm.
 * @return A vector of Bins, each containing the pieces placed within it.
 */
std::vector<Bin> pack(std::vector<MArea>& pieces, const Rectangle2D& binDimension, 
                      const HybridConfig& config = HybridConfig());

} // namespace HybridBinPacking
