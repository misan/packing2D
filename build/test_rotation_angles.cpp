#include "core/Bin.h"
#include "core/BinPacking.h"
#include "utils/Utils.h"
#include <chrono>
#include <iostream>
#include <vector>

// Simple utility to create a rectangular MArea
MArea createRectangle(double x, double y, double width, double height, int id) {
    std::vector<MPointDouble> points = {
        MPointDouble(x, y),
        MPointDouble(x + width, y),
        MPointDouble(x + width, y + height),
        MPointDouble(x, y + height)
    };
    return MArea(points, id);
}

int main() {
    std::cout << "=== Rotation Angles Test ===\n";
    
    Rectangle2D binDimension(MPointDouble(0, 0), MPointDouble(50, 50));
    
    // Create test pieces of various sizes
    std::vector<MArea> pieces;
    
    // Large pieces (should be Stage1-parts)
    pieces.push_back(createRectangle(0, 0, 12, 8, 1));   // Large piece 1
    pieces.push_back(createRectangle(0, 0, 10, 9, 2));   // Large piece 2
    
    // Medium/small pieces (should be Stage2-parts)
    pieces.push_back(createRectangle(0, 0, 6, 4, 3));    // Medium piece 1
    pieces.push_back(createRectangle(0, 0, 5, 3, 4));    // Medium piece 2
    pieces.push_back(createRectangle(0, 0, 4, 3, 5));    // Small piece 1
    pieces.push_back(createRectangle(0, 0, 3, 4, 6));    // Small piece 2 (different aspect ratio)
    
    std::cout << "Created " << pieces.size() << " test pieces\n";
    std::cout << "Bin dimensions: 50x50\n\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    std::vector<Bin> bins = BinPacking::pack(pieces, binDimension);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double>(endTime - startTime);
    
    std::cout << "Results:\n";
    std::cout << "Bins used: " << bins.size() << "\n";
    std::cout << "Time: " << elapsed.count() << " seconds\n\n";
    
    for (size_t i = 0; i < bins.size(); ++i) {
        const Bin& bin = bins[i];
        std::cout << "Bin " << (i + 1) << ":\n";
        std::cout << "  Pieces placed: " << bin.getNPlaced() << "\n";
        std::cout << "  Occupied area: " << bin.getOccupiedArea() << "\n";
        std::cout << "  Utilization: " << (bin.getOccupiedArea() / (50 * 50) * 100) << "%\n";
        
        // Show piece placements
        const auto& placedPieces = bin.getPlacedPieces();
        for (const auto& piece : placedPieces) {
            Rectangle2D bb = piece.getBoundingBox2D();
            std::cout << "    Piece ID " << piece.getID() 
                      << " at (" << RectangleUtils::getX(bb) << "," << RectangleUtils::getY(bb) << ")"
                      << " size " << RectangleUtils::getWidth(bb) << "x" << RectangleUtils::getHeight(bb) << "\n";
        }
        std::cout << "\n";
    }
    
    return 0;
}