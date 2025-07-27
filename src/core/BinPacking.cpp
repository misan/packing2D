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
        std::vector<MArea> stillNotPlaced = currentBin.boundingBoxPacking(toPlace, useParallel);

        // Stage 2: Iteratively optimize and repack.
        if (currentBin.getNPlaced() > nPiecesBefore) {
            while (true) {
                size_t piecesInBinBeforeRepack = currentBin.getNPlaced();

                // Try to optimize the layout by moving pieces
                currentBin.moveAndReplace(nPiecesBefore);

                // Attempt to pack more pieces into the potentially new free space
                if (!stillNotPlaced.empty()) {
                    stillNotPlaced = currentBin.boundingBoxPacking(stillNotPlaced, useParallel);
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