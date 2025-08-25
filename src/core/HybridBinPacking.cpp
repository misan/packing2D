#include "HybridBinPacking.h"
#include "src/core/BinPacking.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <unordered_set>
#include <future>
#include <thread>
#include <cstdlib>

namespace HybridBinPacking {

HybridPacker::HybridPacker(const Rectangle2D& binDimension, const HybridConfig& config)
    : binDimension(binDimension), config(config), timeLimitReached(false) {
    // Initialize random number generator
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    rng.seed(seed);
}

std::vector<Bin> HybridPacker::pack(std::vector<MArea>& pieces) {
    // Reset stats
    stats = Stats{};
    auto startTime = std::chrono::steady_clock::now();
    timeLimitReached = false;
    
    // Store the original pieces for reconstruction
    originalPieces = pieces;
    
    // Initialize population
    std::vector<Solution> population = initializePopulation(pieces);
    
    // Find best solution in initial population
    Solution bestSolution = *std::min_element(population.begin(), population.end());
    stats.bestFitness = bestSolution.fitness;
    stats.bestUtilization = bestSolution.utilization;
    
    // Main evolution loop
    int generation = 0;
    int noImprovementCount = 0;
    
    while (!checkTimeLimit(startTime) && noImprovementCount < config.noImprovementThreshold) {
        // Select parents
        std::vector<Solution> parents = selectParents(population);
        
        // Create offspring through crossover and mutation
        std::vector<Solution> offspring;
        offspring.reserve(config.populationSize);
        
        // Keep elite solutions
        std::sort(population.begin(), population.end());
        for (int i = 0; i < config.eliteSize && i < population.size(); ++i) {
            offspring.push_back(population[i]);
        }
        
        // Generate rest of offspring
        while (offspring.size() < config.populationSize) {
            // Select two parents
            std::uniform_int_distribution<int> parentDist(0, parents.size() - 1);
            int idx1 = parentDist(rng);
            int idx2 = parentDist(rng);
            while (idx2 == idx1) {
                idx2 = parentDist(rng);
            }
            
            // Create child through crossover
            Solution child = crossover(parents[idx1], parents[idx2]);
            
            // Apply mutation
            double mutationRate = 0.1 + (0.4 * (1.0 - generation / 100.0)); // Decreasing mutation rate
            mutate(child, mutationRate);
            
            // Reconstruct bins based on new piece order
            reconstructSolution(child);
            
            // Evaluate child
            evaluateSolution(child);
            offspring.push_back(child);
        }
        
        // Replace population
        population = replacePopulation(population, offspring);
        
        // Update best solution
        Solution currentBest = *std::min_element(population.begin(), population.end());
        if (currentBest.fitness < bestSolution.fitness) {
            bestSolution = currentBest;
            stats.bestFitness = bestSolution.fitness;
            stats.bestUtilization = bestSolution.utilization;
            noImprovementCount = 0;
        } else {
            noImprovementCount++;
        }
        
        generation++;
        stats.totalIterations++;
    }
    
    // Apply simulated annealing to best solution
    if (!checkTimeLimit(startTime)) {
        Solution saSolution = simulatedAnnealing(bestSolution);
        if (saSolution.fitness < bestSolution.fitness) {
            bestSolution = saSolution;
            stats.bestFitness = bestSolution.fitness;
            stats.bestUtilization = bestSolution.utilization;
        }
    }
    
    // Apply final local search to best solution
    if (!checkTimeLimit(startTime)) {
        Solution optimizedSolution = localSearch(bestSolution);
        if (optimizedSolution.fitness < bestSolution.fitness) {
            bestSolution = optimizedSolution;
            stats.bestFitness = bestSolution.fitness;
            stats.bestUtilization = bestSolution.utilization;
        }
    }
    
    // Update execution time
    auto endTime = std::chrono::steady_clock::now();
    stats.executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    return bestSolution.bins;
}

void HybridPacker::reconstructSolution(Solution& solution) {
    // Clear existing bins
    solution.bins.clear();
    
    // Create a new set of pieces based on the piece order
    std::vector<MArea> orderedPieces;
    for (int idx : solution.pieceOrder) {
        orderedPieces.push_back(originalPieces[idx]);
    }
    
    // Use a probabilistic approach to choose the packing algorithm
    double random_val = (double)rand() / RAND_MAX;
    if (random_val < config.greedyWeight) {
        solution.bins = BinPacking::pack(orderedPieces, binDimension, config.useParallel);
    } else {
        solution.bins = BinPacking::slowAndSteadyPack(orderedPieces, binDimension, config.useParallel);
    }
}

std::vector<Solution> HybridPacker::initializePopulation(const std::vector<MArea>& pieces) {
    std::vector<Solution> population;
    population.reserve(config.populationSize);
    
    // Create solutions with different levels of randomness
    for (int i = 0; i < config.populationSize; ++i) {
        double randomness = static_cast<double>(i) / (config.populationSize - 1);
        Solution solution = generateGreedySolution(pieces, randomness);
        population.push_back(solution);
    }
    
    return population;
}

Solution HybridPacker::generateGreedySolution(const std::vector<MArea>& pieces, double randomness) {
    Solution solution;
    
    std::vector<MArea> sortedPieces = pieces;

    // Use randomness to select a sorting strategy
    int strategy = static_cast<int>(randomness * 5);
    switch (strategy) {
        case 0: // Area
            std::sort(sortedPieces.begin(), sortedPieces.end(), [](const MArea& a, const MArea& b) {
                return a.getArea() > b.getArea();
            });
            break;
        case 1: // Perimeter
            std::sort(sortedPieces.begin(), sortedPieces.end(), [](const MArea& a, const MArea& b) {
                return RectangleUtils::getWidth(a.getBoundingBox2D()) + RectangleUtils::getHeight(a.getBoundingBox2D()) >
                       RectangleUtils::getWidth(b.getBoundingBox2D()) + RectangleUtils::getHeight(b.getBoundingBox2D());
            });
            break;
        case 2: // Max dimension
            std::sort(sortedPieces.begin(), sortedPieces.end(), [](const MArea& a, const MArea& b) {
                return std::max(RectangleUtils::getWidth(a.getBoundingBox2D()), RectangleUtils::getHeight(a.getBoundingBox2D())) >
                       std::max(RectangleUtils::getWidth(b.getBoundingBox2D()), RectangleUtils::getHeight(b.getBoundingBox2D()));
            });
            break;
        case 3: // Width
            std::sort(sortedPieces.begin(), sortedPieces.end(), [](const MArea& a, const MArea& b) {
                return RectangleUtils::getWidth(a.getBoundingBox2D()) > RectangleUtils::getWidth(b.getBoundingBox2D());
            });
            break;
        case 4: // Height
            std::sort(sortedPieces.begin(), sortedPieces.end(), [](const MArea& a, const MArea& b) {
                return RectangleUtils::getHeight(a.getBoundingBox2D()) > RectangleUtils::getHeight(b.getBoundingBox2D());
            });
            break;
    }

    // Create the piece order based on the sorted pieces
    solution.pieceOrder.clear();
    for (const auto& p : sortedPieces) {
        for (size_t i = 0; i < pieces.size(); ++i) {
            if (p.getID() == pieces[i].getID()) {
                solution.pieceOrder.push_back(i);
                break;
            }
        }
    }

    // Use the existing BinPacking algorithm with modifications
    std::vector<Bin> bins = BinPacking::pack(sortedPieces, binDimension, config.useParallel);
    
    solution.bins = bins;
    evaluateSolution(solution);
    
    return solution;
}

void HybridPacker::evaluateSolution(Solution& solution) {
    // Calculate fitness based on number of bins and utilization
    double totalArea = 0.0;
    double binArea = RectangleUtils::getWidth(binDimension) * RectangleUtils::getHeight(binDimension);
    
    for (const auto& bin : solution.bins) {
        totalArea += bin.getOccupiedArea();
    }
    
    double totalBinArea = solution.bins.size() * binArea;
    solution.utilization = totalArea / totalBinArea;
    
    // Fitness function: balance between number of bins and utilization
    // Lower fitness is better
    solution.fitness = solution.bins.size() * (2.0 - solution.utilization);
}

Solution HybridPacker::simulatedAnnealing(const Solution& initialSolution) {
    Solution currentSolution = initialSolution;
    Solution bestSolution = initialSolution;
    
    double temperature = config.initialTemperature;
    int tempIterations = 0;
    
    while (temperature > 1.0 && !checkTimeLimit(std::chrono::steady_clock::now())) {
        for (int i = 0; i < config.saIterationsPerTemp && !timeLimitReached; ++i) {
            // Generate neighbor solution
            Solution neighborSolution = generateNeighbor(currentSolution);
            
            // Reconstruct bins based on new piece order
            reconstructSolution(neighborSolution);
            
            // Calculate acceptance probability
            double deltaFitness = neighborSolution.fitness - currentSolution.fitness;
            double acceptanceProb = std::exp(-deltaFitness / temperature);
            
            // Decide whether to accept neighbor
            if (deltaFitness < 0 || std::uniform_real_distribution<double>(0.0, 1.0)(rng) < acceptanceProb) {
                currentSolution = neighborSolution;
                
                // Update best solution
                if (currentSolution.fitness < bestSolution.fitness) {
                    bestSolution = currentSolution;
                }
            }
            
            stats.saIterations++;
        }
        
        // Cool down
        temperature *= config.coolingRate;
        tempIterations++;
        
        // Break if we've done enough temperature iterations
        if (tempIterations >= 10) {
            break;
        }
    }
    
    return bestSolution;
}

Solution HybridPacker::generateNeighbor(const Solution& solution) {
    Solution neighbor = solution;
    
    // Randomly select a perturbation strategy
    std::uniform_int_distribution<int> strategyDist(0, 2);
    int strategy = strategyDist(rng);
    
    switch (strategy) {
        case 0: {
            // Swap two pieces in the ordering
            if (neighbor.pieceOrder.size() >= 2) {
                std::uniform_int_distribution<size_t> idxDist(0, neighbor.pieceOrder.size() - 1);
                size_t idx1 = idxDist(rng);
                size_t idx2 = idxDist(rng);
                while (idx2 == idx1) {
                    idx2 = idxDist(rng);
                }
                std::swap(neighbor.pieceOrder[idx1], neighbor.pieceOrder[idx2]);
            }
            break;
        }
        case 1: {
            // Reverse a subsequence
            if (neighbor.pieceOrder.size() >= 3) {
                std::uniform_int_distribution<size_t> startDist(0, neighbor.pieceOrder.size() - 3);
                std::uniform_int_distribution<size_t> lenDist(2, neighbor.pieceOrder.size() / 2);
                size_t start = startDist(rng);
                size_t len = std::min(lenDist(rng), neighbor.pieceOrder.size() - start);
                std::reverse(neighbor.pieceOrder.begin() + start, neighbor.pieceOrder.begin() + start + len);
            }
            break;
        }
        case 2: {
            // Move a piece to a random position
            if (neighbor.pieceOrder.size() >= 2) {
                std::uniform_int_distribution<size_t> fromDist(0, neighbor.pieceOrder.size() - 1);
                size_t from = fromDist(rng);
                int value = neighbor.pieceOrder[from];
                neighbor.pieceOrder.erase(neighbor.pieceOrder.begin() + from);
                
                std::uniform_int_distribution<size_t> toDist(0, neighbor.pieceOrder.size());
                size_t to = toDist(rng);
                neighbor.pieceOrder.insert(neighbor.pieceOrder.begin() + to, value);
            }
            break;
        }
    }
    
    // Note: We don't evaluate the solution here because we need to reconstruct the bins first
    // The evaluation will be done after reconstruction in the calling function
    
    return neighbor;
}

Solution HybridPacker::localSearch(const Solution& solution) {
    Solution currentSolution = solution;
    Solution bestSolution = solution;
    
    int noImprovementCount = 0;
    
    for (int i = 0; i < config.maxLocalSearchIterations && noImprovementCount < config.noImprovementThreshold; ++i) {
        // Generate a set of neighbor solutions
        std::vector<Solution> neighbors;
        for (int j = 0; j < 5; ++j) {
            Solution neighbor = generateNeighbor(currentSolution);
            reconstructSolution(neighbor);
            evaluateSolution(neighbor);
            neighbors.push_back(neighbor);
        }
        
        // Find best neighbor
        Solution bestNeighbor = *std::min_element(neighbors.begin(), neighbors.end());
        
        // Update current solution if improvement found
        if (bestNeighbor.fitness < currentSolution.fitness) {
            currentSolution = bestNeighbor;
            noImprovementCount = 0;
        } else {
            noImprovementCount++;
        }
        
        // Update best solution
        if (currentSolution.fitness < bestSolution.fitness) {
            bestSolution = currentSolution;
        }
        
        stats.localSearchIterations++;
    }
    
    return bestSolution;
}

std::vector<Solution> HybridPacker::selectParents(const std::vector<Solution>& population) {
    std::vector<Solution> parents;
    parents.reserve(config.populationSize);
    
    // Tournament selection
    int tournamentSize = std::max(2, static_cast<int>(population.size() / 5));
    
    for (int i = 0; i < config.populationSize; ++i) {
        // Select tournament participants
        std::vector<Solution> tournament;
        std::sample(population.begin(), population.end(), std::back_inserter(tournament), 
                   tournamentSize, rng);
        
        // Select winner
        Solution winner = *std::min_element(tournament.begin(), tournament.end());
        parents.push_back(winner);
    }
    
    return parents;
}

Solution HybridPacker::crossover(const Solution& parent1, const Solution& parent2) {
    Solution child;
    
    // Order crossover (OX)
    if (parent1.pieceOrder.empty()) {
        return child;
    }
    
    // Select two random crossover points
    std::uniform_int_distribution<size_t> dist(0, parent1.pieceOrder.size() - 1);
    size_t start = dist(rng);
    size_t end = dist(rng);
    if (start > end) {
        std::swap(start, end);
    }
    
    // Initialize child's piece order with -1
    child.pieceOrder.resize(parent1.pieceOrder.size(), -1);
    
    // Copy segment from parent1
    for (size_t i = start; i <= end; ++i) {
        child.pieceOrder[i] = parent1.pieceOrder[i];
    }
    
    // Fill remaining positions with order from parent2
    size_t currentPos = (end + 1) % parent1.pieceOrder.size();
    for (size_t i = 0; i < parent2.pieceOrder.size(); ++i) {
        size_t pos = (end + 1 + i) % parent2.pieceOrder.size();
        int piece = parent2.pieceOrder[pos];
        
        // If piece not already in child
        if (std::find(child.pieceOrder.begin(), child.pieceOrder.end(), piece) == child.pieceOrder.end()) {
            child.pieceOrder[currentPos] = piece;
            currentPos = (currentPos + 1) % parent1.pieceOrder.size();
        }
    }
    
    // Note: We don't evaluate the solution here because we need to reconstruct the bins first
    // The evaluation will be done after reconstruction in the calling function
    
    return child;
}

void HybridPacker::mutate(Solution& solution, double mutationRate) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    
    if (dist(rng) < mutationRate) {
        // Apply swap mutation
        if (solution.pieceOrder.size() >= 2) {
            std::uniform_int_distribution<size_t> idxDist(0, solution.pieceOrder.size() - 1);
            size_t idx1 = idxDist(rng);
            size_t idx2 = idxDist(rng);
            while (idx2 == idx1) {
                idx2 = idxDist(rng);
            }
            std::swap(solution.pieceOrder[idx1], solution.pieceOrder[idx2]);
        }
    }
}

std::vector<Solution> HybridPacker::replacePopulation(
    const std::vector<Solution>& population, 
    const std::vector<Solution>& offspring) {
    
    std::vector<Solution> newPopulation;
    newPopulation.reserve(config.populationSize);
    
    // Combine population and offspring
    std::vector<Solution> combined;
    combined.reserve(population.size() + offspring.size());
    combined.insert(combined.end(), population.begin(), population.end());
    combined.insert(combined.end(), offspring.begin(), offspring.end());
    
    // Sort by fitness
    std::sort(combined.begin(), combined.end());
    
    // Select best individuals
    for (int i = 0; i < config.populationSize && i < combined.size(); ++i) {
        newPopulation.push_back(combined[i]);
    }
    
    // Fill remaining slots with random individuals if needed
    while (newPopulation.size() < config.populationSize) {
        std::uniform_int_distribution<size_t> dist(0, combined.size() - 1);
        newPopulation.push_back(combined[dist(rng)]);
    }
    
    return newPopulation;
}

bool HybridPacker::checkTimeLimit(const std::chrono::steady_clock::time_point& startTime) {
    if (config.timeLimitMs <= 0) {
        return false;
    }
    
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
    
    if (elapsedMs >= config.timeLimitMs) {
        timeLimitReached = true;
        return true;
    }
    
    return false;
}

std::vector<Bin> pack(std::vector<MArea>& pieces, const Rectangle2D& binDimension, 
                      const HybridConfig& config) {
    HybridPacker packer(binDimension, config);
    return packer.pack(pieces);
}

} // namespace HybridBinPacking
