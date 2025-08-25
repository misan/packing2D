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
    
    // Main evolution loop - simplified and more efficient
    int noImprovementCount = 0;
    const int maxGenerations = 20; // Limit generations to prevent excessive computation
    
    for (int generation = 0; generation < maxGenerations && !checkTimeLimit(startTime); ++generation) {
        // Sort population by fitness
        std::sort(population.begin(), population.end());
        
        // Keep best solution
        if (population[0].fitness < bestSolution.fitness) {
            bestSolution = population[0];
            stats.bestFitness = bestSolution.fitness;
            stats.bestUtilization = bestSolution.utilization;
            noImprovementCount = 0;
        } else {
            noImprovementCount++;
        }
        
        if (noImprovementCount >= 5) {
            break;
        }
        
        // Create new population with elite individuals
        std::vector<Solution> newPopulation;
        newPopulation.reserve(config.populationSize);
        
        // Keep top eliteSize solutions
        for (int i = 0; i < config.eliteSize; ++i) {
            newPopulation.push_back(population[i]);
        }
        
        // Fill the rest with mutated versions of the best solutions
        while (newPopulation.size() < config.populationSize) {
            // Select a random elite solution to mutate
            std::uniform_int_distribution<int> eliteDist(0, config.eliteSize - 1);
            int eliteIndex = eliteDist(rng);
            
            Solution mutated = population[eliteIndex];
            // Apply stronger mutation
            mutate(mutated, 0.3);
            reconstructSolution(mutated);
            evaluateSolution(mutated);
            newPopulation.push_back(mutated);
        }
        
        population = newPopulation;
        stats.totalIterations++;
    }
    
    // Apply local search to best solution if time permits
    if (!checkTimeLimit(startTime)) {
        Solution improvedSolution = localSearch(bestSolution);
        if (improvedSolution.fitness < bestSolution.fitness) {
            bestSolution = improvedSolution;
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
    int strategy = static_cast<int>(randomness * 7); // Now 7 strategies
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
        case 5: // Aspect ratio (width/height)
            std::sort(sortedPieces.begin(), sortedPieces.end(), [](const MArea& a, const MArea& b) {
                double aspectA = RectangleUtils::getWidth(a.getBoundingBox2D()) / 
                               std::max(1.0, RectangleUtils::getHeight(a.getBoundingBox2D()));
                double aspectB = RectangleUtils::getWidth(b.getBoundingBox2D()) / 
                               std::max(1.0, RectangleUtils::getHeight(b.getBoundingBox2D()));
                return aspectA > aspectB;
            });
            break;
        case 6: // Bounding box area to actual area ratio
            std::sort(sortedPieces.begin(), sortedPieces.end(), [](const MArea& a, const MArea& b) {
                double ratioA = a.getArea() / (RectangleUtils::getWidth(a.getBoundingBox2D()) * 
                                             RectangleUtils::getHeight(a.getBoundingBox2D()));
                double ratioB = b.getArea() / (RectangleUtils::getWidth(b.getBoundingBox2D()) * 
                                             RectangleUtils::getHeight(b.getBoundingBox2D()));
                // Sort by most "compact" pieces first (higher ratio)
                return ratioA > ratioB;
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
    if (solution.bins.empty()) {
        solution.fitness = std::numeric_limits<double>::max();
        solution.utilization = 0.0;
        return;
    }
    
    // Calculate total area and bin area
    double totalArea = 0.0;
    double binArea = RectangleUtils::getWidth(binDimension) * RectangleUtils::getHeight(binDimension);
    
    // Calculate additional metrics for better evaluation
    double minUtilization = 1.0;
    double maxUtilization = 0.0;
    double sumSquaredUtilization = 0.0;
    
    for (const auto& bin : solution.bins) {
        double binOccupiedArea = bin.getOccupiedArea();
        totalArea += binOccupiedArea;
        double binUtilization = binOccupiedArea / binArea;
        
        minUtilization = std::min(minUtilization, binUtilization);
        maxUtilization = std::max(maxUtilization, binUtilization);
        sumSquaredUtilization += binUtilization * binUtilization;
    }
    
    double totalBinArea = solution.bins.size() * binArea;
    solution.utilization = totalArea / totalBinArea;
    
    // Enhanced fitness function that considers:
    // 1. Number of bins (most important)
    // 2. Average utilization
    // 3. Balance of utilization across bins (minimize variance)
    // 4. Worst-case utilization
    double utilizationVariance = (sumSquaredUtilization / solution.bins.size()) - (solution.utilization * solution.utilization);
    
    // Weighted fitness function - lower is better
    solution.fitness = (1000.0 * solution.bins.size()) +           // Primary: minimize number of bins
                      (100.0 * (1.0 - solution.utilization)) +    // Secondary: maximize average utilization
                      (50.0 * utilizationVariance) +              // Tertiary: minimize utilization variance
                      (20.0 * (1.0 - minUtilization));            // Quaternary: improve worst bin utilization
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
    Solution bestSolution = solution;
    
    // Try different perturbation strategies
    for (int i = 0; i < 10 && !checkTimeLimit(std::chrono::steady_clock::now()); ++i) {
        Solution neighbor = generateNeighbor(bestSolution);
        reconstructSolution(neighbor);
        evaluateSolution(neighbor);
        
        if (neighbor.fitness < bestSolution.fitness) {
            bestSolution = neighbor;
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
