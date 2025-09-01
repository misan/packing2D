#include "BinPacking.h"
#include <algorithm>
#include <iostream>

namespace BinPacking {

std::vector<Bin> pack(std::vector<MArea>& pieces, const Rectangle2D& binDimension, bool useParallel) {
    std::vector<Bin> bins;

    // Sort pieces by area, largest first.
    std::sort(pieces.begin(), pieces.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });

    std::vector<MArea> toPlace = pieces;
    std::cerr << "BinPacking: Initial toPlace size: " << toPlace.size() << std::endl;
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

        bins.emplace_back(binDimension);
        Bin& currentBin = bins.back();
        size_t nPiecesBefore = currentBin.getNPlaced();

        // Stage 1: Initial packing using bounding boxes.
        std::cerr << "BinPacking: Starting bin " << bins.size() << " with " << toPlace.size() << " pieces to place" << std::endl;
        std::vector<MArea> stillNotPlaced = currentBin.boundingBoxPackingWithLookAhead(toPlace, useParallel, 5);
        std::cerr << "BinPacking: After boundingBoxPacking, " << stillNotPlaced.size() << " pieces not placed" << std::endl;

        // Stage 2: Iteratively optimize and repack using global free space exploration.
        if (currentBin.getNPlaced() > nPiecesBefore) {
            while (true) {
                size_t piecesInBinBeforeRepack = currentBin.getNPlaced();

                // Try to optimize the layout by moving pieces
                currentBin.moveAndReplace(nPiecesBefore);

                // Attempt to pack more pieces into the potentially new free space
                // using global free space exploration (largest pieces first in free zones)
                if (!stillNotPlaced.empty()) {
                    stillNotPlaced = currentBin.placeInFreeZonesWithLookAhead(stillNotPlaced, useParallel, 5);
                }

                // The loop is stable and should terminate if no new pieces were added.
                // The `moveAndReplace` might shuffle pieces internally, but if it doesn't
                // create space for new pieces, we are not making progress.
                if (currentBin.getNPlaced() == piecesInBinBeforeRepack) {
                    break;
                }
            }
        }

        // Stage 3: Final compression and drop pass to fill any remaining gaps.
        currentBin.compress(useParallel);
        if (!stillNotPlaced.empty()) {
            stillNotPlaced = currentBin.dropPieces(stillNotPlaced, useParallel);
        }
        currentBin.compress(useParallel);
        
        // Verify no collisions exist after all operations
        if (!currentBin.verifyNoCollisions()) {
            std::cerr << "Warning: Collisions detected in bin after packing operations!" << std::endl;
        }

        // If the bin is still empty, it means the largest remaining piece is too big.
        if (currentBin.getNPlaced() == nPiecesBefore) {
            std::cerr << "Warning: Could not place any of the " << toPlace.size()
                      << " remaining pieces into a new bin. The largest piece might be too big." << std::endl;
            bins.pop_back(); // Remove the unused bin.
            break;
        }

        toPlace = stillNotPlaced;
        std::cerr << "BinPacking: Next toPlace size: " << toPlace.size() << std::endl;

        // Stage 4: Global optimization - after each new bin is created, try to place
        // remaining pieces in free spaces of ALL existing bins (not just the current one)
        if (!toPlace.empty() && bins.size() > 1) {
            std::cerr << "BinPacking: Starting iterative global optimization with " << toPlace.size()
                      << " pieces remaining and " << bins.size() << " bins" << std::endl;
            
            // Sort remaining pieces by area (largest first)
            std::sort(toPlace.begin(), toPlace.end(), [](const MArea& a, const MArea& b) {
                return a.getArea() > b.getArea();
            });

            // Try to place each remaining piece in any existing bin
            std::vector<MArea> globallyNotPlaced;
            for (auto& piece : toPlace) {
                bool wasPlaced = false;
                
                // Try all bins (from first to last)
                for (auto& bin : bins) {
                    // First recompute free rectangles for this bin to ensure they're accurate
                    bin.recomputeAllFreeRectangles();
                    
                    // Try to place in this bin's free space
                    std::vector<MArea> tempVec = {piece};
                    auto result = bin.placeInFreeZones(tempVec, useParallel);
                    
                    if (result.empty()) {
                        // Piece was placed successfully
                        wasPlaced = true;
                        bin.compress(useParallel); // Re-compress after placement
                        break;
                    }
                }
                
                if (!wasPlaced) {
                    globallyNotPlaced.push_back(piece);
                }
            }
            
            toPlace = globallyNotPlaced;
            std::cerr << "BinPacking: After iterative global optimization, " << toPlace.size() << " pieces still not placed" << std::endl;
        }
    }

    // Stage 4: Global optimization - try to place remaining pieces in free spaces of all existing bins
    if (!toPlace.empty() && !bins.empty()) {
        std::cerr << "BinPacking: Starting global optimization with " << toPlace.size()
                  << " pieces remaining and " << bins.size() << " bins" << std::endl;
        
        // Sort remaining pieces by area (largest first)
        std::sort(toPlace.begin(), toPlace.end(), [](const MArea& a, const MArea& b) {
            return a.getArea() > b.getArea();
        });

        // Try to place each remaining piece in any existing bin
        std::vector<MArea> globallyNotPlaced;
        for (auto& piece : toPlace) {
            bool wasPlaced = false;
            
            // Try all bins (from first to last)
            for (auto& bin : bins) {
                // Try to place in this bin's free space
                std::vector<MArea> tempVec = {piece};
                auto result = bin.placeInFreeZones(tempVec, useParallel);
                
                if (result.empty()) {
                    // Piece was placed successfully
                    wasPlaced = true;
                    bin.compress(useParallel); // Re-compress after placement
                    break;
                }
            }
            
            if (!wasPlaced) {
                globallyNotPlaced.push_back(piece);
            }
        }
        
        toPlace = globallyNotPlaced;
        std::cerr << "BinPacking: After global optimization, " << toPlace.size() << " pieces still not placed" << std::endl;
    }

    return bins;
}

} // namespace BinPacking