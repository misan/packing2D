#include "core/Bin.h"
#include "core/BinPacking.h"
#include "core/GeneticOptimizer.h"
#include "core/SimulatedAnnealingOptimizer.h"
#include "utils/Utils.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

// Forward declaration for helper functions
void createOutputFiles(const std::vector<Bin>& bins);
void printUsage();

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::vector<std::string> args(argv + 1, argv + argc);
    bool useParallel = false;
    bool useGeneticAlgorithm = false;
    bool useSimulatedAnnealing = false;
    std::string fileName;

    for (const auto& arg : args) {
        if (arg == "--parallel") {
            useParallel = true;
        } else if (arg == "--ga" || arg == "--genetic") {
            useGeneticAlgorithm = true;
        } else if (arg == "--sa" || arg == "--simulated-annealing") {
            useSimulatedAnnealing = true;
        } else {
            fileName = arg;
        }
    }

    if (fileName.empty()) {
        std::cerr << "Error: No input file specified." << std::endl;
        printUsage();
        return 1;
    }

    std::cout << "Loading pieces from: " << fileName << std::endl;

    auto loadResult = Utils::loadPieces(fileName);
    if (!loadResult) {
        std::cerr << "Failed to load pieces from file." << std::endl;
        return 1;
    }

    std::cout << "Loaded " << loadResult->pieces.size() << " pieces." << std::endl;
    std::cout << "Bin dimensions: " << RectangleUtils::getWidth(loadResult->binDimension) << "x" << RectangleUtils::getHeight(loadResult->binDimension) << std::endl;

    if (useParallel) {
        std::cout << "Running in PARALLEL mode." << std::endl;
    } else {
        std::cout << "Running in SEQUENTIAL mode." << std::endl;
    }

    std::cout << "Starting packing process..." << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<Bin> bins;
    if (useGeneticAlgorithm) {
        std::cout << "Using Genetic Algorithm for optimization." << std::endl;
        BinPacking::GeneticOptimizer ga(loadResult->pieces, loadResult->binDimension, useParallel);
        bins = ga.run();
    } else if (useSimulatedAnnealing) {
        std::cout << "Using Simulated Annealing for optimization." << std::endl;
        BinPacking::SimulatedAnnealingOptimizer sa(loadResult->pieces, loadResult->binDimension, useParallel);
        bins = sa.run();
    } else {
        bins = BinPacking::pack(loadResult->pieces, loadResult->binDimension, useParallel);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

    std::cout << "Packing process finished. " << bins.size() << " bins used." << std::endl;
    std::cout << "Elapsed time: " << elapsed.count() << " seconds." << std::endl;

    std::cout << "Generating output files..." << std::endl;
    createOutputFiles(bins);

    std::cout << "DONE!!!" << std::endl;

    // return 0;
}

void createOutputFiles(const std::vector<Bin>& bins) {
    for (size_t i = 0; i < bins.size(); ++i) {
        std::string fileName = "Bin-" + std::to_string(i + 1) + ".txt";
        std::ofstream outFile(fileName);
        if (!outFile.is_open()) {
            std::cerr << "Error: Could not create output file " << fileName << std::endl;
            continue;
        }

        const auto& placedPieces = bins[i].getPlacedPieces();
        outFile << placedPieces.size() << std::endl;

        for (const auto& piece : placedPieces) {
            Rectangle2D bbox = piece.getBoundingBox2D();
            outFile << piece.getID() << " "
                    << piece.getRotation() << " "
                    << RectangleUtils::getX(bbox) << "," << RectangleUtils::getY(bbox)
                    << std::endl;
        }
        std::cout << "Generated points file for bin " << std::to_string(i + 1) << std::endl;
    }
}

void printUsage() {
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << std::endl;
    std::cout << "$ ./packing_main [--parallel] [--ga | --genetic] [--sa | --simulated-annealing] <file name>" << std::endl;
    std::cout << "  --parallel      : (Optional) Run the packing algorithm using a parallel implementation." << std::endl;
    std::cout << "  --ga, --genetic : (Optional) Use the Genetic Algorithm to find a better packing solution." << std::endl;
    std::cout << "  --sa, --simulated-annealing : (Optional) Use Simulated Annealing to find a better packing solution." << std::endl;
    std::cout << "  <file name>     : file describing pieces (see file structure specifications below)." << std::endl;
    std::cout << std::endl;
    std::cout << "The input pieces file should be structured as follows: " << std::endl;
    std::cout << "First line: 'width  height',integer bin dimensions separates by a space" << std::endl;
    std::cout << "Second line: 'number of pieces', a single integer specifying the number of pieces in this file." << std::endl;
    std::cout << "N lines: each piece contained in a single line-> 'x0,y0 x1,y1 x2,y2 ... xn,yn'.NOTE "
              << "THAT FIGURE POINTS IN DOUBLE FORMAT MUST BE SPECIFIED IN COUNTERCLOCKWISE ORDER USING THE CARTESIAN COORDINATE SYSTEM." << std::endl;
}
