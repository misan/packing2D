#include "Bin.h"
#include "primitives/Rectangle.h" // For RectangleUtils
#include "core/Constants.h"
#include <limits>
#include <algorithm>
#include <numeric>
#include <vector>
#include <execution>
#include <mutex>

static bool g_parallelism_disabled_for_tests = false;

namespace TestUtils {
    void disableParallelismForTests(bool disable) {
        g_parallelism_disabled_for_tests = disable;
    }
}

Bin::Bin(const Rectangle2D& dimension) : dimension(dimension), jointSpacesDirty(true) {
    freeRectangles.push_back(dimension);
}

Bin::Bin(const Bin& other) :
    dimension(other.dimension),
    placedPieces(other.placedPieces),
    freeRectangles(other.freeRectangles),
    placedPiecesRTree(other.placedPiecesRTree),
    jointFreeSpaces(other.jointFreeSpaces),
    jointSpacesDirty(other.jointSpacesDirty)
    // collisionMutex is not copied, a new one is default-initialized.
{
}

Bin& Bin::operator=(const Bin& other) {
    if (this == &other) {
        return *this;
    }
    dimension = other.dimension;
    placedPieces = other.placedPieces;
    freeRectangles = other.freeRectangles;
    placedPiecesRTree = other.placedPiecesRTree;
    jointFreeSpaces = other.jointFreeSpaces;
    jointSpacesDirty = other.jointSpacesDirty;
    // collisionMutex is not copied.
    return *this;
}

const std::vector<MArea>& Bin::getPlacedPieces() const {
    return placedPieces;
}

size_t Bin::getNPlaced() const {
    return placedPieces.size();
}

const Rectangle2D& Bin::getDimension() const {
    return dimension;
}

double Bin::getOccupiedArea() const {
    return std::accumulate(placedPieces.begin(), placedPieces.end(), 0.0,
        [](double sum, const MArea& piece) {
            return sum + piece.getArea();
        });
}

double Bin::getEmptyArea() const {
    double binArea = RectangleUtils::getWidth(dimension) * RectangleUtils::getHeight(dimension);
    return binArea - getOccupiedArea();
}

bool Bin::isCollision(const MArea& piece, std::optional<size_t> ignoredPieceIndex) {
    // 1. Broad phase: Query the R-tree to find pieces whose bounding boxes intersect with the new piece's bounding box.
    std::vector<RTreeValue> candidates;
    placedPiecesRTree.query(boost::geometry::index::intersects(piece.getBoundingBox2D()), std::back_inserter(candidates));

    if (candidates.empty()) {
        return false; // No overlapping bounding boxes means no collision.
    }

    // 2. Narrow phase: For the few candidates found, perform a precise geometric intersection test.
    for (const auto& candidate : candidates) {
        size_t candidateIndex = candidate.second;

        // If we are moving a piece, we must ignore a collision with itself.
        if (ignoredPieceIndex && candidateIndex == *ignoredPieceIndex) {
            continue;
        }

        if (piece.intersection(placedPieces[candidateIndex])) {
            return true; // Found a definite collision.
        }
    }

    return false; // No precise collisions found.
}


Bin::Placement Bin::findWhereToPlace(const MArea& piece, bool useParallel) {
    Placement bestPlacement;
    double minWastage = std::numeric_limits<double>::max();
    Rectangle2D pieceBB = piece.getBoundingBox2D();
    std::mutex mtx;

    // Optimization: Pre-filter free rectangles to only consider those that could potentially fit the piece
    // This is especially useful when there are many small free rectangles
    double pieceArea = RectangleUtils::getWidth(pieceBB) * RectangleUtils::getHeight(pieceBB);
    std::vector<size_t> candidateIndices;
    candidateIndices.reserve(freeRectangles.size());
    
    for (size_t i = 0; i < freeRectangles.size(); ++i) {
        const auto& freeRect = freeRectangles[i];
        double freeRectArea = RectangleUtils::getWidth(freeRect) * RectangleUtils::getHeight(freeRect);
        
        // Quick area check - if the free rectangle is smaller than the piece, skip it
        if (freeRectArea < pieceArea * 0.9) { // Allow a small margin for rotation
            continue;
        }
        
        // More detailed check for dimensions
        if (RectangleUtils::getWidth(freeRect) >= RectangleUtils::getWidth(pieceBB) &&
            RectangleUtils::getHeight(freeRect) >= RectangleUtils::getHeight(pieceBB)) {
            candidateIndices.push_back(i);
        } else if (RectangleUtils::getWidth(freeRect) >= RectangleUtils::getHeight(pieceBB) &&
                  RectangleUtils::getHeight(freeRect) >= RectangleUtils::getWidth(pieceBB)) {
            candidateIndices.push_back(i);
        }
    }

    auto checkPlacement = [&](size_t i) {
        const auto& freeRect = freeRectangles[i];
        if (RectangleUtils::fits(pieceBB, freeRect)) {
            double wastage = std::min(
                RectangleUtils::getWidth(freeRect) - RectangleUtils::getWidth(pieceBB),
                RectangleUtils::getHeight(freeRect) - RectangleUtils::getHeight(pieceBB)
            );
            std::lock_guard<std::mutex> lock(mtx);
            if (wastage < minWastage) {
                minWastage = wastage;
                bestPlacement.rectIndex = static_cast<int>(i);
                bestPlacement.requiresRotation = false;
            }
        }

        if (RectangleUtils::fitsRotated(pieceBB, freeRect)) {
            double wastage = std::min(
                RectangleUtils::getWidth(freeRect) - RectangleUtils::getHeight(pieceBB),
                RectangleUtils::getHeight(freeRect) - RectangleUtils::getWidth(pieceBB)
            );
            std::lock_guard<std::mutex> lock(mtx);
            if (wastage < minWastage) {
                minWastage = wastage;
                bestPlacement.rectIndex = static_cast<int>(i);
                bestPlacement.requiresRotation = true;
            }
        }
    };

    if (useParallel && !g_parallelism_disabled_for_tests && candidateIndices.size() > 250) {
#if __cpp_lib_parallel_algorithm >= 201603L
        // Use parallel algorithm if supported (C++17 and later)
        std::for_each(std::execution::par, candidateIndices.begin(), candidateIndices.end(), checkPlacement);
#else
        // Fallback to sequential if parallel algorithms are not supported
        for (auto it = candidateIndices.rbegin(); it != candidateIndices.rend(); ++it) {
            checkPlacement(*it);
        }
#endif
    } else {
        // Iterate in reverse order to prioritize newer free rectangles
        for (auto it = candidateIndices.rbegin(); it != candidateIndices.rend(); ++it) {
            checkPlacement(*it);
        }
    }

    return bestPlacement;
}

std::vector<MArea> Bin::boundingBoxPacking(std::vector<MArea>& piecesToPlace, bool useParallel) {
    std::vector<MArea> notPlacedPieces;

    std::sort(piecesToPlace.begin(), piecesToPlace.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });

    for (const auto& piece : piecesToPlace) {
        bool piecePlaced = false;
        
        // First, try the traditional rectangular placement
        Placement placement = findWhereToPlace(piece, useParallel);

        if (placement.rectIndex != -1) {
            const Rectangle2D& freeRect = freeRectangles[placement.rectIndex];
            MArea placedPiece = piece;

            if (placement.requiresRotation) {
                placedPiece.rotate(90);
            }

            placedPiece.placeInPosition(RectangleUtils::getX(freeRect), RectangleUtils::getY(freeRect));

            if (!isCollision(placedPiece)) {
                Rectangle2D pieceBB = placedPiece.getBoundingBox2D();
                computeFreeRectangles(pieceBB);
                eliminateNonMaximal();

                placedPieces.push_back(placedPiece);
                placedPiecesRTree.insert({pieceBB, placedPieces.size() - 1});
                piecePlaced = true;
                
                // Mark joint spaces as dirty since we've added a new piece
                jointSpacesDirty = true;
            }
        }
        
        // If rectangular placement failed, try placing in joint free spaces
        if (!piecePlaced) {
            if (auto placedPiece = tryPlaceInJointSpace(piece, useParallel)) {
                piecePlaced = true;
            }
        }
        
        if (!piecePlaced) {
            notPlacedPieces.push_back(piece);
        }
    }
    return notPlacedPieces;
}

void Bin::computeFreeRectangles(const Rectangle2D& justPlacedPieceBB) {
    std::vector<Rectangle2D> nextFreeRectangles;
    const double epsilon = 1e-9;
    
    // Reserve space to avoid reallocations
    nextFreeRectangles.reserve(freeRectangles.size() * 2); // Worst case: each rectangle splits into 4

    for (const auto& freeR : freeRectangles) {
        if (!RectangleUtils::intersects(freeR, justPlacedPieceBB)) {
            nextFreeRectangles.push_back(freeR);
        } else {
            Rectangle2D rIntersection = RectangleUtils::createIntersection(freeR, justPlacedPieceBB);
            
            // Pre-calculate coordinates to avoid repeated function calls
            double freeR_x = RectangleUtils::getX(freeR);
            double freeR_y = RectangleUtils::getY(freeR);
            double freeR_maxX = RectangleUtils::getMaxX(freeR);
            double freeR_maxY = RectangleUtils::getMaxY(freeR);
            double rIntersection_x = RectangleUtils::getX(rIntersection);
            double rIntersection_y = RectangleUtils::getY(rIntersection);
            double rIntersection_maxX = RectangleUtils::getMaxX(rIntersection);
            double rIntersection_maxY = RectangleUtils::getMaxY(rIntersection);
            
            // Create rectangles only if they have significant area
            double topHeight = freeR_maxY - rIntersection_maxY;
            if (topHeight > epsilon) {
                double topWidth = freeR_maxX - freeR_x;
                if (topWidth * topHeight > epsilon) { // Only add if area is significant
                    nextFreeRectangles.emplace_back(MPointDouble(freeR_x, rIntersection_maxY), MPointDouble(freeR_maxX, freeR_maxY));
                }
            }
            
            double bottomHeight = rIntersection_y - freeR_y;
            if (bottomHeight > epsilon) {
                double bottomWidth = freeR_maxX - freeR_x;
                if (bottomWidth * bottomHeight > epsilon) { // Only add if area is significant
                    nextFreeRectangles.emplace_back(MPointDouble(freeR_x, freeR_y), MPointDouble(freeR_maxX, rIntersection_y));
                }
            }
            
            double leftWidth = rIntersection_x - freeR_x;
            if (leftWidth > epsilon) {
                double leftHeight = freeR_maxY - freeR_y;
                if (leftWidth * leftHeight > epsilon) { // Only add if area is significant
                    nextFreeRectangles.emplace_back(MPointDouble(freeR_x, freeR_y), MPointDouble(rIntersection_x, freeR_maxY));
                }
            }
            
            double rightWidth = freeR_maxX - rIntersection_maxX;
            if (rightWidth > epsilon) {
                double rightHeight = freeR_maxY - freeR_y;
                if (rightWidth * rightHeight > epsilon) { // Only add if area is significant
                    nextFreeRectangles.emplace_back(MPointDouble(rIntersection_maxX, freeR_y), MPointDouble(freeR_maxX, freeR_maxY));
                }
            }
        }
    }
    
    // Use move assignment to avoid copying
    freeRectangles = std::move(nextFreeRectangles);
    
    // Mark joint spaces as dirty since free rectangles have changed
    jointSpacesDirty = true;
}

void Bin::eliminateNonMaximal() {
    auto& rects = freeRectangles;
    if (rects.size() < 2) return;

    // Sort by area in descending order - largest rectangles first
    std::sort(rects.begin(), rects.end(), [](const Rectangle2D& a, const Rectangle2D& b) {
        double areaA = RectangleUtils::getWidth(a) * RectangleUtils::getHeight(a);
        double areaB = RectangleUtils::getWidth(b) * RectangleUtils::getHeight(b);
        return areaA > areaB;
    });

    // Use a more efficient algorithm: only check smaller rectangles against larger ones
    std::vector<bool> toRemove(rects.size(), false);
    
    for (size_t i = 0; i < rects.size(); ++i) {
        if (toRemove[i]) continue;
        
        for (size_t j = i + 1; j < rects.size(); ++j) {
            if (toRemove[j]) continue;
            
            // Since rects are sorted by area (descending), rects[i] is larger than rects[j]
            // So we only need to check if rects[i] contains rects[j]
            if (RectangleUtils::contains(rects[i], rects[j])) {
                toRemove[j] = true;
            }
        }
    }
    
    // Remove marked rectangles
    size_t write_idx = 0;
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!toRemove[i]) {
            if (write_idx != i) {
                rects[write_idx] = rects[i];
            }
            write_idx++;
        }
    }
    
    rects.resize(write_idx);
}

void Bin::compress(bool useParallel) {
    if (placedPieces.empty()) {
        return;
    }

    bool moved_in_pass = true;
    while (moved_in_pass) {
        moved_in_pass = false;
        for (size_t i = 0; i < placedPieces.size(); ++i) {
            if (compressPiece(i, MVector(-1.0, -1.0))) {
                moved_in_pass = true;
            }
        }
    }
}

bool Bin::compressPiece(size_t pieceIndex, const MVector& vector) {
    if (vector.getX() == 0 && vector.getY() == 0) {
        return false;
    }

    MArea& pieceToMove = placedPieces[pieceIndex];
    RTreeValue rtreeEntry = {pieceToMove.getBoundingBox2D(), pieceIndex};

    // Temporarily remove the piece from the R-tree to avoid self-collision.
    placedPiecesRTree.remove(rtreeEntry);

    int total_moves = 0;
    bool moved_in_iter = true;
    while (moved_in_iter) {
        moved_in_iter = false;

        if (vector.getY() != 0) {
            MVector u_y(0, vector.getY());
            pieceToMove.move(u_y);
            if (pieceToMove.isInside(this->dimension) && !isCollision(pieceToMove)) {
                moved_in_iter = true;
                total_moves++;
            } else {
                pieceToMove.move(u_y.inverse());
            }
        }

        if (vector.getX() != 0) {
            MVector u_x(vector.getX(), 0);
            pieceToMove.move(u_x);
            if (pieceToMove.isInside(this->dimension) && !isCollision(pieceToMove)) {
                moved_in_iter = true;
                total_moves++;
            } else {
                pieceToMove.move(u_x.inverse());
            }
        }
    }

    // Add the piece back to the R-tree in its new final position.
    placedPiecesRTree.insert({pieceToMove.getBoundingBox2D(), pieceIndex});

    // If the piece was moved, mark joint spaces as dirty
    if (total_moves > 0) {
        jointSpacesDirty = true;
    }

    return total_moves > 0;
}

bool Bin::compressPiece_parallel_helper(MArea& piece, size_t pieceIndex) {
    const MVector vector(-1.0, -1.0);
    int total_moves = 0;
    bool moved_in_iter = true;
    while (moved_in_iter) {
        moved_in_iter = false;

        if (vector.getY() != 0) {
            MVector u_y(0, vector.getY());
            piece.move(u_y);
            if (piece.isInside(this->dimension) && !isCollision(piece, pieceIndex)) {
                moved_in_iter = true;
                total_moves++;
            } else {
                piece.move(u_y.inverse());
            }
        }

        if (vector.getX() != 0) {
            MVector u_x(vector.getX(), 0);
            piece.move(u_x);
            if (piece.isInside(this->dimension) && !isCollision(piece, pieceIndex)) {
                moved_in_iter = true;
                total_moves++;
            } else {
                piece.move(u_x.inverse());
            }
        }
    }
    return total_moves > 0;
}

std::vector<MArea> Bin::dropPieces(const std::vector<MArea>& piecesToDrop, bool useParallel) {
    std::vector<MArea> unplacedPieces;

    for (const auto& pieceToTry : piecesToDrop) {
        bool wasPlaced = false;
        for (int angle : Constants::ROTATION_ANGLES) {
            MArea candidate = pieceToTry;
            if (angle > 0) {
                candidate.rotate(static_cast<double>(angle));
            }

            if (auto placedPiece = dive(candidate, useParallel)) {
                placedPieces.push_back(*placedPiece);
                placedPiecesRTree.insert({placedPiece->getBoundingBox2D(), placedPieces.size() - 1});
                wasPlaced = true;
                // Mark joint spaces as dirty since we've added a new piece
                jointSpacesDirty = true;
                break;
            }
        }
        if (!wasPlaced) {
            unplacedPieces.push_back(pieceToTry);
        }
    }
    return unplacedPieces;
}

std::optional<MArea> Bin::dive(MArea toDive, bool useParallel) {
    Rectangle2D pieceBB = toDive.getBoundingBox2D();
    double pieceWidth = RectangleUtils::getWidth(pieceBB);
    double pieceHeight = RectangleUtils::getHeight(pieceBB);
    double binWidth = RectangleUtils::getWidth(this->dimension);
    double binHeight = RectangleUtils::getHeight(this->dimension);

    if (pieceWidth > binWidth || pieceHeight > binHeight) {
        return std::nullopt;
    }

    double dx = pieceWidth / Constants::DIVE_HORIZONTAL_DISPLACEMENT_FACTOR;
    if (dx < 1e-9) dx = 1.0;

    for (double initialX = 0; initialX + pieceWidth <= binWidth + 1e-9; initialX += dx) {
        MArea tempPiece = toDive;
        tempPiece.placeInPosition(initialX, binHeight - pieceHeight);

        if (!isCollision(tempPiece)) {
            size_t tempIndex = placedPieces.size();
            placedPieces.push_back(tempPiece);
            compressPiece(tempIndex, MVector(0, -1.0));
            MArea finalPiece = placedPieces.back();
            placedPieces.pop_back();
            return finalPiece;
        }
    }
    
    MArea tempPiece = toDive;
    tempPiece.placeInPosition(binWidth - pieceWidth, binHeight - pieceHeight);
    if (!isCollision(tempPiece)) {
        size_t tempIndex = placedPieces.size();
        placedPieces.push_back(tempPiece);
        compressPiece(tempIndex, MVector(0, -1.0));
        MArea finalPiece = placedPieces.back();
        placedPieces.pop_back();
        return finalPiece;
    }

    return std::nullopt;
}

bool Bin::moveAndReplace(size_t indexLimit) {
    bool movement = false;
    for (int i = static_cast<int>(placedPieces.size()) - 1; i >= static_cast<int>(indexLimit); --i) {
        MArea& currentArea = placedPieces[i];
        
        for (int j = 0; j < i; ++j) {
            const MArea& container = placedPieces[j];
            if (container.getFreeArea() > currentArea.getArea()) {
                Rectangle2D contBB = container.getBoundingBox2D();

                // Try without rotation
                MArea candidate = currentArea;
                candidate.placeInPosition(RectangleUtils::getX(contBB), RectangleUtils::getY(contBB));

                if (auto swept = sweep(container, candidate, i)) {
                    freeRectangles.push_back(currentArea.getBoundingBox2D());
                    placedPieces[i] = *swept;
                    compressPiece(i, MVector(-1.0, -1.0));
                    computeFreeRectangles(swept->getBoundingBox2D());
                    eliminateNonMaximal();
                    movement = true;
                    // Mark joint spaces as dirty since we've moved a piece
                    jointSpacesDirty = true;
                    goto next_piece; // Break outer loop and continue with next piece
                }

                // Try with rotation
                candidate = currentArea;
                candidate.rotate(90);
                candidate.placeInPosition(RectangleUtils::getX(contBB), RectangleUtils::getY(contBB));
                if (auto swept = sweep(container, candidate, i)) {
                    freeRectangles.push_back(currentArea.getBoundingBox2D());
                    placedPieces[i] = *swept;
                    compressPiece(i, MVector(-1.0, -1.0));
                    computeFreeRectangles(swept->getBoundingBox2D());
                    eliminateNonMaximal();
                    movement = true;
                    // Mark joint spaces as dirty since we've moved a piece
                    jointSpacesDirty = true;
                    goto next_piece;
                }
            }
        }
        next_piece:;
    }
    return movement;
}

std::optional<MArea> Bin::sweep(const MArea& container, MArea inside, size_t ignoredPieceIndex) {
    if (!inside.intersection(container) && !isCollision(inside, ignoredPieceIndex)) {
        return inside;
    }

    Rectangle2D containerBB = container.getBoundingBox2D();
    Rectangle2D insideBB_orig = inside.getBoundingBox2D();

    double dx_factor = Constants::DX_SWEEP_FACTOR;
    double dy_factor = Constants::DY_SWEEP_FACTOR;

    if (inside.getVertexCount() > 100) {
        dx_factor = 2;
        dy_factor = 1;
    }

    double dx = RectangleUtils::getWidth(insideBB_orig) / dx_factor;
    double dy = RectangleUtils::getHeight(insideBB_orig) / dy_factor;
    if (dx < 1e-9) dx = 1.0;
    if (dy < 1e-9) dy = 1.0;

    double startX = RectangleUtils::getX(containerBB);
    double startY = RectangleUtils::getY(containerBB);
    double endX = RectangleUtils::getMaxX(containerBB);
    double endY = RectangleUtils::getMaxY(containerBB);

    for (double y = startY; y + RectangleUtils::getHeight(insideBB_orig) <= endY + 1e-9; y += dy) {
        for (double x = startX; x + RectangleUtils::getWidth(insideBB_orig) <= endX + 1e-9; x += dx) {
            inside.placeInPosition(x, y);
            if (inside.isInside(this->dimension) && !inside.intersection(container) && !isCollision(inside, ignoredPieceIndex)) {
                return inside;
            }
        }
    }

    return std::nullopt;
}

MArea Bin::computeTotalFreeSpace() const {
    if (placedPieces.empty()) {
        // Create an MArea representing the entire bin
        std::vector<MPointDouble> binPoints = {
            MPointDouble(RectangleUtils::getX(dimension), RectangleUtils::getY(dimension)),
            MPointDouble(RectangleUtils::getMaxX(dimension), RectangleUtils::getY(dimension)),
            MPointDouble(RectangleUtils::getMaxX(dimension), RectangleUtils::getMaxY(dimension)),
            MPointDouble(RectangleUtils::getX(dimension), RectangleUtils::getMaxY(dimension))
        };
        return MArea(binPoints, 0);
    }

    // Start with the bin as the initial free space
    std::vector<MPointDouble> binPoints = {
        MPointDouble(RectangleUtils::getX(dimension), RectangleUtils::getY(dimension)),
        MPointDouble(RectangleUtils::getMaxX(dimension), RectangleUtils::getY(dimension)),
        MPointDouble(RectangleUtils::getMaxX(dimension), RectangleUtils::getMaxY(dimension)),
        MPointDouble(RectangleUtils::getX(dimension), RectangleUtils::getMaxY(dimension))
    };
    MArea totalFreeSpace(binPoints, 0);

    // Reduced optimization: Only skip extremely small pieces that have negligible impact
    // This ensures we maintain accuracy in joint space detection
    double totalArea = RectangleUtils::getWidth(dimension) * RectangleUtils::getHeight(dimension);
    double minSignificantArea = totalArea * 0.0001; // Reduced threshold from 0.1% to 0.01%
    
    // Subtract significant placed pieces from the free space
    for (const auto& piece : placedPieces) {
        if (piece.getArea() >= minSignificantArea) {
            totalFreeSpace.subtract(piece);
        }
    }

    return totalFreeSpace;
}

std::vector<MArea> Bin::decomposeFreeSpace(const MArea& freeSpace) const {
    std::vector<MArea> decomposedSpaces;
    
    if (freeSpace.isEmpty()) {
        return decomposedSpaces;
    }

    // Get the bounding box of the free space
    Rectangle2D bbox = freeSpace.getBoundingBox2D();
    
    // Create a rectangular area representing the bounding box
    std::vector<MPointDouble> bboxPoints = {
        MPointDouble(RectangleUtils::getX(bbox), RectangleUtils::getY(bbox)),
        MPointDouble(RectangleUtils::getMaxX(bbox), RectangleUtils::getY(bbox)),
        MPointDouble(RectangleUtils::getMaxX(bbox), RectangleUtils::getMaxY(bbox)),
        MPointDouble(RectangleUtils::getX(bbox), RectangleUtils::getMaxY(bbox))
    };
    MArea bboxArea(bboxPoints, 0);
    
    // Subtract the actual free space from its bounding box to get the "occupied" parts
    MArea occupiedParts = bboxArea;
    occupiedParts.subtract(freeSpace);
    
    // If the free space is already rectangular, return it as is
    if (occupiedParts.isEmpty()) {
        decomposedSpaces.push_back(freeSpace);
        return decomposedSpaces;
    }
    
    // For complex free spaces, we'll use a simple decomposition strategy:
    // Divide the space into smaller rectangular regions based on the vertices
    // of the occupied parts
    
    // This is a simplified approach - in a more sophisticated implementation,
    // we might use more advanced decomposition algorithms
    
    // For now, we'll return the original free space as a single region
    // The actual placement logic will handle fitting pieces into this space
    decomposedSpaces.push_back(freeSpace);
    
    return decomposedSpaces;
}

std::vector<MArea> Bin::detectJointFreeSpaces() {
    if (!jointSpacesDirty) {
        return jointFreeSpaces;
    }

    std::vector<MArea> jointSpaces;
    
    if (placedPieces.size() < 2) {
        // Need at least 2 pieces to form joint spaces
        jointSpacesDirty = false;
        jointFreeSpaces = jointSpaces;
        return jointSpaces;
    }

    // Only compute joint spaces if we have a significant number of pieces
    // This is an expensive operation, so we limit its frequency
    static size_t lastPieceCount = 0;
    if (placedPieces.size() - lastPieceCount < 3 && !jointFreeSpaces.empty()) {
        // If we haven't added many pieces since last computation, reuse existing spaces
        // Reduced threshold from 5 to 3 to be more responsive
        jointSpacesDirty = false;
        return jointFreeSpaces;
    }
    lastPieceCount = placedPieces.size();

    // Compute the total free space only if necessary
    MArea totalFreeSpace = computeTotalFreeSpace();
    
    if (totalFreeSpace.isEmpty()) {
        jointSpacesDirty = false;
        jointFreeSpaces = jointSpaces;
        return jointSpaces;
    }

    // Decompose the free space into simpler regions
    std::vector<MArea> decomposedSpaces = decomposeFreeSpace(totalFreeSpace);
    
    // Filter out spaces that are too small to be useful
    // Use a dynamic threshold based on the average piece size, but make it less aggressive
    double avgPieceArea = 0.0;
    if (!placedPieces.empty()) {
        avgPieceArea = std::accumulate(placedPieces.begin(), placedPieces.end(), 0.0,
            [](double sum, const MArea& piece) { return sum + piece.getArea(); }) / placedPieces.size();
    }
    // Reduced threshold from 10% to 5% of average piece area to capture more joint spaces
    const double minAreaThreshold = std::max(5.0, avgPieceArea * 0.05);
    
    // Reserve space to avoid reallocations
    jointSpaces.reserve(decomposedSpaces.size());
    
    for (const auto& space : decomposedSpaces) {
        if (space.getArea() >= minAreaThreshold) {
            jointSpaces.push_back(space);
        }
    }
    
    // Use move assignment to avoid copying
    jointSpacesDirty = false;
    jointFreeSpaces = std::move(jointSpaces);
    return jointFreeSpaces;
}

std::optional<MArea> Bin::checkPlacementInJointSpace(const MArea& piece, const MArea& jointSpace, bool useParallel) {
    if (jointSpace.isEmpty() || piece.isEmpty()) {
        return std::nullopt;
    }

    // Get the bounding box of the joint space
    Rectangle2D jointSpaceBBox = jointSpace.getBoundingBox2D();
    Rectangle2D pieceBBox = piece.getBoundingBox2D();
    
    // Quick check: if the piece's bounding box doesn't fit in the joint space's bounding box,
    // it definitely won't fit in the joint space
    if (!RectangleUtils::fits(pieceBBox, jointSpaceBBox) && !RectangleUtils::fitsRotated(pieceBBox, jointSpaceBBox)) {
        return std::nullopt;
    }

    // Try to place the piece at different positions within the joint space
    // We'll use a grid-based approach to sample potential positions
    
    // Use the original step size to maintain packing efficiency
    const double stepSize = std::min(RectangleUtils::getWidth(pieceBBox), RectangleUtils::getHeight(pieceBBox)) / 4.0;
    const double minX = RectangleUtils::getX(jointSpaceBBox);
    const double maxX = RectangleUtils::getMaxX(jointSpaceBBox) - RectangleUtils::getWidth(pieceBBox);
    const double minY = RectangleUtils::getY(jointSpaceBBox);
    const double maxY = RectangleUtils::getMaxY(jointSpaceBBox) - RectangleUtils::getHeight(pieceBBox);
    
    // Pre-calculate bounds to avoid repeated function calls
    const double pieceArea = piece.getArea();
    const double jointSpaceArea = jointSpace.getArea();
    
    // Try without rotation first
    for (double x = minX; x <= maxX; x += stepSize) {
        for (double y = minY; y <= maxY; y += stepSize) {
            MArea candidate = piece;
            candidate.placeInPosition(x, y);
            
            // Quick bounding box check first
            if (!candidate.isInside(dimension)) {
                continue;
            }
            
            // More expensive collision check only if bounding box check passes
            if (isCollision(candidate)) {
                continue;
            }
            
            // Check if the candidate is contained within the joint space
            MArea tempJointSpace = jointSpace;
            tempJointSpace.subtract(candidate);
            if (tempJointSpace.getArea() < jointSpaceArea - pieceArea + 1e-6) {
                return candidate;
            }
        }
    }
    
    // Try with rotation
    MArea rotatedPiece = piece;
    rotatedPiece.rotate(90);
    Rectangle2D rotatedBBox = rotatedPiece.getBoundingBox2D();
    
    const double minXRot = RectangleUtils::getX(jointSpaceBBox);
    const double maxXRot = RectangleUtils::getMaxX(jointSpaceBBox) - RectangleUtils::getWidth(rotatedBBox);
    const double minYRot = RectangleUtils::getY(jointSpaceBBox);
    const double maxYRot = RectangleUtils::getMaxY(jointSpaceBBox) - RectangleUtils::getHeight(rotatedBBox);
    
    for (double x = minXRot; x <= maxXRot; x += stepSize) {
        for (double y = minYRot; y <= maxYRot; y += stepSize) {
            MArea candidate = piece;
            candidate.rotate(90);
            candidate.placeInPosition(x, y);
            
            // Quick bounding box check first
            if (!candidate.isInside(dimension)) {
                continue;
            }
            
            // More expensive collision check only if bounding box check passes
            if (isCollision(candidate)) {
                continue;
            }
            
            // Check if the candidate is contained within the joint space
            MArea tempJointSpace = jointSpace;
            tempJointSpace.subtract(candidate);
            if (tempJointSpace.getArea() < jointSpaceArea - pieceArea + 1e-6) {
                return candidate;
            }
        }
    }
    
    return std::nullopt;
}

std::optional<MArea> Bin::tryPlaceInJointSpace(const MArea& piece, bool useParallel) {
    // Detect joint free spaces
    std::vector<MArea> jointSpaces = detectJointFreeSpaces();
    
    // Try to place the piece in each joint space
    for (const auto& jointSpace : jointSpaces) {
        if (auto placedPiece = checkPlacementInJointSpace(piece, jointSpace, useParallel)) {
            // If successful, update the bin state
            placedPieces.push_back(*placedPiece);
            placedPiecesRTree.insert({placedPiece->getBoundingBox2D(), placedPieces.size() - 1});
            
            // Mark joint spaces as dirty since we've added a new piece
            jointSpacesDirty = true;
            
            // Update the rectangular free spaces as well
            computeFreeRectangles(placedPiece->getBoundingBox2D());
            eliminateNonMaximal();
            
            return placedPiece;
        }
    }
    
    return std::nullopt;
}

void Bin::splitScheme(const Rectangle2D& usedFreeArea, const Rectangle2D& justPlacedPieceBB) {
    // This function is declared but appears to be unused in the provided code.
    // Providing a stub implementation to resolve potential linker errors.
}