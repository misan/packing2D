#include "BinPacking.h"
#include <algorithm>
#include <iostream>

namespace BinPacking {

std::vector<Bin> pack(std::vector<MArea>& pieces, const Rectangle2D& binDimension) {
    std::vector<Bin> bins;

    // Sort pieces by area, largest first.
    std::sort(pieces.begin(), pieces.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });

    std::vector<MArea> toPlace = pieces;
    size_t lastLoopUnplacedCount = 0; // For infinite loop detection

    while (!toPlace.empty()) {
        // *** INFINITE LOOP GUARD ***
        if (lastLoopUnplacedCount > 0 && toPlace.size() == lastLoopUnplacedCount) {
            std::cerr << "Error: Infinite loop detected. "
                      << "The number of unplaced pieces (" << toPlace.size()
                      << ") did not decrease. Aborting." << std::endl;
            break;
        }
        lastLoopUnplacedCount = toPlace.size();

        bins.emplace_back(binDimension, true); // Enable NFP for better free space detection
        Bin& currentBin = bins.back();
        size_t nPiecesBefore = currentBin.getNPlaced();

        // Stage 1: Initial packing using bounding boxes.
        std::vector<MArea> stillNotPlaced = currentBin.boundingBoxPacking(toPlace);

        // Stage 2: Advanced global free space placement for Stage2-parts.
        // This replaces the old moveAndReplace approach with global optimization.
        if (currentBin.getNPlaced() > nPiecesBefore && !stillNotPlaced.empty()) {
            // Use global free space detection to place remaining pieces
            // This can find and utilize gaps between Stage1-parts that were previously missed
            stillNotPlaced = currentBin.placeInGlobalFreeSpace(stillNotPlaced, false /* basic rotations */);
        }

        // Stage 3: Final gap filling with extended rotations and compression.
        currentBin.compress();
        if (!stillNotPlaced.empty()) {
            // First try traditional drop placement
            stillNotPlaced = currentBin.dropPieces(stillNotPlaced);
            
            // If pieces still remain, try global free space with extended rotations
            if (!stillNotPlaced.empty()) {
                stillNotPlaced = currentBin.placeInGlobalFreeSpace(stillNotPlaced, true /* extended rotations */);
            }
        }
        currentBin.compress();

        // If the bin is still empty, it means the largest remaining piece is too big.
        if (currentBin.getNPlaced() == nPiecesBefore) {
            std::cerr << "Warning: Could not place any of the " << toPlace.size()
                      << " remaining pieces into a new bin. The largest piece might be too big." << std::endl;
            bins.pop_back(); // Remove the unused bin.
            break;
        }

        toPlace = stillNotPlaced;
    }

    return bins;
}

} // namespace BinPacking