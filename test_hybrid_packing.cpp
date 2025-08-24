#include "HybridBinPacking.h"
#include "src/core/BinPacking.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

// Helper function to load pieces from a file
std::vector<MArea> loadPiecesFromFile(const std::string& filename) {
    std::vector<MArea> pieces;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return pieces;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        // Parse the line to extract SVG path
        // This is a simplified version - in practice, you would need a proper SVG parser
        // For now, we'll just create a simple rectangle for each line
        
        // Extract the number at the beginning (if any)
        size_t pipePos = line.find('|');
        if (pipePos != std::string::npos) {
            std::string pathData = line.substr(pipePos + 1);
            
            // Create a simple rectangle based on the path data
            // This is just for testing - in a real implementation, you would parse the SVG path
            std::vector<MPointDouble> points = {
                MPointDouble(0, 0),
                MPointDouble(100, 0),
                MPointDouble(100, 100),
                MPointDouble(0, 100)
            };
            
            // Use the line number as ID
            int id = pieces.size();
            pieces.emplace_back(points, id);
        }
    }
    
    return pieces;
}

// Generate random pieces for testing
std::vector<MArea> generateRandomPieces(int count, int maxSize = 200) {
    std::vector<MArea> pieces;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(50, maxSize);
    std::uniform_real_distribution<> posDist(0.0, 1.0);
    
    for (int i = 0; i < count; ++i) {
        int width = sizeDist(gen);
        int height = sizeDist(gen);
        
        // Create a simple rectangle
        std::vector<MPointDouble> points = {
            MPointDouble(0, 0),
            MPointDouble(width, 0),
            MPointDouble(width, height),
            MPointDouble(0, height)
        };
        
        pieces.emplace_back(points, i);
    }
    
    return pieces;
}

// Function to calculate packing statistics
void calculateStats(const std::vector<Bin>& bins, double& utilization, int& numBins) {
    numBins = bins.size();
    double totalArea = 0.0;
    double binArea = 0.0;
    
    if (!bins.empty()) {
        const auto& firstBin = bins[0];
        const auto& dim = firstBin.getDimension();
        binArea = RectangleUtils::getWidth(dim) * RectangleUtils::getHeight(dim);
        
        for (const auto& bin : bins) {
            totalArea += bin.getOccupiedArea();
        }
        
        utilization = totalArea / (numBins * binArea);
    } else {
        utilization = 0.0;
    }
}

// Function to print results
void printResults(const std::string& algorithm, const std::vector<Bin>& bins, 
                 int64_t executionTimeMs, const HybridBinPacking::HybridPacker::Stats* hybridStats = nullptr) {
    double utilization;
    int numBins;
    calculateStats(bins, utilization, numBins);
    
    std::cout << "=== " << algorithm << " Results ===" << std::endl;
    std::cout << "Number of bins: " << numBins << std::endl;
    std::cout << "Utilization: " << std::fixed << std::setprecision(2) << (utilization * 100.0) << "%" << std::endl;
    std::cout << "Execution time: " << executionTimeMs << " ms" << std::endl;
    
    if (hybridStats) {
        std::cout << "Total iterations: " << hybridStats->totalIterations << std::endl;
        std::cout << "SA iterations: " << hybridStats->saIterations << std::endl;
        std::cout << "Local search iterations: " << hybridStats->localSearchIterations << std::endl;
        std::cout << "Best fitness: " << hybridStats->bestFitness << std::endl;
        std::cout << "Best utilization: " << std::fixed << std::setprecision(2) << (hybridStats->bestUtilization * 100.0) << "%" << std::endl;
    }
    
    std::cout << std::endl;
}

int main() {
    // Test with different piece counts
    std::vector<int> pieceCounts = {10, 25, 50, 100};
    
    // Bin dimensions
    Rectangle2D binDimension(MPointDouble(0, 0), MPointDouble(1000, 1000));
    
    for (int count : pieceCounts) {
        std::cout << "Testing with " << count << " random pieces" << std::endl;
        std::cout << "=====================================" << std::endl;
        
        // Generate random pieces
        std::vector<MArea> pieces = generateRandomPieces(count);
        std::cout << "Generated " << pieces.size() << " pieces." << std::endl;
        
        // Test original algorithm
        auto startTime = std::chrono::steady_clock::now();
        std::vector<MArea> originalPieces = pieces;  // Make a copy
        std::vector<Bin> originalBins = BinPacking::pack(originalPieces, binDimension, true);
        auto endTime = std::chrono::steady_clock::now();
        int64_t originalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        
        printResults("Original Greedy Algorithm", originalBins, originalTime);
        
        // Test hybrid algorithm with different configurations
        HybridBinPacking::HybridConfig configs[3];
        
        // Configuration 1: Balanced
        configs[0].greedyWeight = 0.7;
        configs[0].initialTemperature = 100.0;
        configs[0].coolingRate = 0.95;
        configs[0].populationSize = 10;
        configs[0].timeLimitMs = 30000;
        configs[0].saIterationsPerTemp = 50;
        
        // Configuration 2: More metaheuristic
        configs[1].greedyWeight = 0.5;
        configs[1].initialTemperature = 200.0;
        configs[1].coolingRate = 0.9;
        configs[1].populationSize = 15;
        configs[1].timeLimitMs = 30000;
        configs[1].saIterationsPerTemp = 30;
        
        // Configuration 3: Fast
        configs[2].greedyWeight = 0.8;
        configs[2].initialTemperature = 50.0;
        configs[2].coolingRate = 0.85;
        configs[2].populationSize = 5;
        configs[2].timeLimitMs = 10000;
        configs[2].saIterationsPerTemp = 20;
        
        for (int i = 0; i < 3; ++i) {
            std::string configName = "Hybrid Algorithm - Config " + std::to_string(i+1);
            
            startTime = std::chrono::steady_clock::now();
            std::vector<MArea> hybridPieces = pieces;  // Make a copy
            HybridBinPacking::HybridPacker packer(binDimension, configs[i]);
            std::vector<Bin> hybridBins = packer.pack(hybridPieces);
            endTime = std::chrono::steady_clock::now();
            int64_t hybridTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            
            printResults(configName, hybridBins, hybridTime, &packer.getStats());
        }
        
        std::cout << "=====================================" << std::endl << std::endl;
    }
    
    return 0;
}