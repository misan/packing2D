#include "BinPacking.h"
#include "core/Constants.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

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

bool placePieceGreedily(MArea& piece, std::vector<Bin>& bins) {
    double best_area_fit = std::numeric_limits<double>::max();
    int best_bin_index = -1;
    int best_free_area_index = -1;
    int best_rotation = 0;

    for (size_t i = 0; i < bins.size(); ++i) {
        // Try multiple rotation angles for better fitting
        for (int angle : Constants::ROTATION_ANGLES) {
            MArea rotated_piece = piece;
            if (angle > 0) {
                rotated_piece.rotate(static_cast<double>(angle));
            }
            
            // Also try flipping the piece for better placement
            for (int flip = 0; flip < 2; ++flip) {
                if (flip > 0) {
                    // Create a flipped version by swapping width and height through rotation
                    rotated_piece.rotate(180);
                    // Flip horizontally by placing at different position
                    // This is a simplified approach - actual flipping would need proper implementation
                }

            const auto& free_rectangles = bins[i].getFreeRectangles();
            for (size_t j = 0; j < free_rectangles.size(); ++j) {
                if (RectangleUtils::fits(rotated_piece.getBoundingBox2D(), free_rectangles[j])) {
                    if (bins[i].canPlaceWithCollisionCheck(rotated_piece, j)) {
                        double area_fit = RectangleUtils::getArea(free_rectangles[j]) - rotated_piece.getArea();
                        if (area_fit >= 0 && area_fit < best_area_fit) {
                            best_area_fit = area_fit;
                            best_bin_index = i;
                            best_free_area_index = j;
                            best_rotation = angle;
                        }
                    }
                }
            }
        }
    }

    if (best_bin_index != -1) {
        if (best_rotation > 0) {
            piece.rotate(static_cast<double>(best_rotation));
        }
        return bins[best_bin_index].addPiece(piece, best_free_area_index);
    }

    return false;
}

std::vector<Bin> slowAndSteadyPack(std::vector<MArea>& pieces, const Rectangle2D& binDimension, bool useParallel) {
    std::vector<Bin> bins;
    std::sort(pieces.begin(), pieces.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });

    for (auto& piece : pieces) {
        if (!placePieceGreedily(piece, bins)) {
            bins.emplace_back(binDimension);
            if (!placePieceGreedily(piece, bins)) {
                 std::cerr << "Error: Could not place piece " << piece.getID() << " in a new bin." << std::endl;
            }
        }
    }

    return bins;
}

} // namespace BinPacking
