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

Bin::Bin(const Rectangle2D& dimension) : dimension(dimension) {
    freeRectangles.push_back(dimension);
}

Bin::Bin(const Bin& other) :
    dimension(other.dimension),
    placedPieces(other.placedPieces),
    freeRectangles(other.freeRectangles),
    placedPiecesRTree(other.placedPiecesRTree)
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
    std::mutex mtx;

    auto checkPlacement = [&](int i) {
        const auto& freeRect = freeRectangles[i];
        
        // Try all rotation angles (Stage 1 uses 90-degree increments)
        for (int angle : Constants::STAGE1_ROTATION_ANGLES) {
            MArea rotatedPiece = piece;
            if (angle > 0) {
                rotatedPiece.rotate(static_cast<double>(angle));
            }
            
            Rectangle2D pieceBB = rotatedPiece.getBoundingBox2D();
            
            if (RectangleUtils::fits(pieceBB, freeRect)) {
                double wastage = std::min(
                    RectangleUtils::getWidth(freeRect) - RectangleUtils::getWidth(pieceBB),
                    RectangleUtils::getHeight(freeRect) - RectangleUtils::getHeight(pieceBB)
                );
                std::lock_guard<std::mutex> lock(mtx);
                if (wastage < minWastage) {
                    minWastage = wastage;
                    bestPlacement.rectIndex = i;
                    bestPlacement.rotationAngle = angle;
                }
            }
        }
    };

    if (useParallel && !g_parallelism_disabled_for_tests && freeRectangles.size() > 250) {
#if __cpp_lib_parallel_algorithm >= 201603L
        // Use parallel algorithm if supported (C++17 and later)
        std::vector<int> indices(freeRectangles.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::for_each(std::execution::par, indices.begin(), indices.end(), checkPlacement);
#else
        // Fallback to sequential if parallel algorithms are not supported
        for (int i = static_cast<int>(freeRectangles.size()) - 1; i >= 0; --i) {
            checkPlacement(i);
        }
#endif
    } else {
        for (int i = static_cast<int>(freeRectangles.size()) - 1; i >= 0; --i) {
            checkPlacement(i);
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
        Placement placement = findWhereToPlace(piece, useParallel);

        if (placement.rectIndex != -1) {
            const Rectangle2D& freeRect = freeRectangles[placement.rectIndex];
            MArea placedPiece = piece;

            if (placement.rotationAngle > 0) {
                placedPiece.rotate(static_cast<double>(placement.rotationAngle));
            }

            placedPiece.placeInPosition(RectangleUtils::getX(freeRect), RectangleUtils::getY(freeRect));

            if (!isCollision(placedPiece)) {
                Rectangle2D pieceBB = placedPiece.getBoundingBox2D();
                computeFreeRectangles(pieceBB);
                eliminateNonMaximal();

                placedPieces.push_back(placedPiece);
                size_t newIndex = placedPieces.size() - 1;
                placedPiecesRTree.insert({pieceBB, newIndex});
                
                std::cerr << "BoundingBoxPacking: Placed piece " << newIndex << " at ("
                          << RectangleUtils::getX(pieceBB) << ", " << RectangleUtils::getY(pieceBB) << ")" << std::endl;
            } else {
                notPlacedPieces.push_back(piece);
                std::cerr << "BoundingBoxPacking: Collision detected during placement" << std::endl;
            }
        } else {
            notPlacedPieces.push_back(piece);
        }
    }
    return notPlacedPieces;
}

void Bin::computeFreeRectangles(const Rectangle2D& justPlacedPieceBB) {
    std::vector<Rectangle2D> nextFreeRectangles;
    const double epsilon = 1e-6; // Increased from 1e-9 for better floating-point robustness

    std::cerr << "computeFreeRectangles: Processing placed piece BB: ("
              << RectangleUtils::getX(justPlacedPieceBB) << ", " << RectangleUtils::getY(justPlacedPieceBB) << ") - ("
              << RectangleUtils::getMaxX(justPlacedPieceBB) << ", " << RectangleUtils::getMaxY(justPlacedPieceBB) << ")" << std::endl;
    std::cerr << "computeFreeRectangles: Current free rectangles count: " << freeRectangles.size() << std::endl;

    for (const auto& freeR : freeRectangles) {
        if (!RectangleUtils::intersects(freeR, justPlacedPieceBB)) {
            nextFreeRectangles.push_back(freeR);
            std::cerr << "computeFreeRectangles: Keeping free rectangle: ("
                      << RectangleUtils::getX(freeR) << ", " << RectangleUtils::getY(freeR) << ") - ("
                      << RectangleUtils::getMaxX(freeR) << ", " << RectangleUtils::getMaxY(freeR) << ")" << std::endl;
        } else {
            std::cerr << "computeFreeRectangles: Splitting free rectangle: ("
                      << RectangleUtils::getX(freeR) << ", " << RectangleUtils::getY(freeR) << ") - ("
                      << RectangleUtils::getMaxX(freeR) << ", " << RectangleUtils::getMaxY(freeR) << ")" << std::endl;
            
            Rectangle2D rIntersection = RectangleUtils::createIntersection(freeR, justPlacedPieceBB);
            double topHeight = RectangleUtils::getMaxY(freeR) - RectangleUtils::getMaxY(rIntersection);
            if (topHeight > epsilon) {
                Rectangle2D topRect(MPointDouble(RectangleUtils::getX(freeR), RectangleUtils::getMaxY(rIntersection)),
                                   MPointDouble(RectangleUtils::getMaxX(freeR), RectangleUtils::getMaxY(freeR)));
                nextFreeRectangles.push_back(topRect);
                std::cerr << "computeFreeRectangles: Adding top rectangle: ("
                          << RectangleUtils::getX(topRect) << ", " << RectangleUtils::getY(topRect) << ") - ("
                          << RectangleUtils::getMaxX(topRect) << ", " << RectangleUtils::getMaxY(topRect) << ")" << std::endl;
            }
            double bottomHeight = RectangleUtils::getY(rIntersection) - RectangleUtils::getY(freeR);
            if (bottomHeight > epsilon) {
                Rectangle2D bottomRect(MPointDouble(RectangleUtils::getX(freeR), RectangleUtils::getY(freeR)),
                                      MPointDouble(RectangleUtils::getMaxX(freeR), RectangleUtils::getY(rIntersection)));
                nextFreeRectangles.push_back(bottomRect);
                std::cerr << "computeFreeRectangles: Adding bottom rectangle: ("
                          << RectangleUtils::getX(bottomRect) << ", " << RectangleUtils::getY(bottomRect) << ") - ("
                          << RectangleUtils::getMaxX(bottomRect) << ", " << RectangleUtils::getMaxY(bottomRect) << ")" << std::endl;
            }
            double leftWidth = RectangleUtils::getX(rIntersection) - RectangleUtils::getX(freeR);
            if (leftWidth > epsilon) {
                Rectangle2D leftRect(MPointDouble(RectangleUtils::getX(freeR), RectangleUtils::getY(freeR)),
                                    MPointDouble(RectangleUtils::getX(rIntersection), RectangleUtils::getMaxY(freeR)));
                nextFreeRectangles.push_back(leftRect);
                std::cerr << "computeFreeRectangles: Adding left rectangle: ("
                          << RectangleUtils::getX(leftRect) << ", " << RectangleUtils::getY(leftRect) << ") - ("
                          << RectangleUtils::getMaxX(leftRect) << ", " << RectangleUtils::getMaxY(leftRect) << ")" << std::endl;
            }
            double rightWidth = RectangleUtils::getMaxX(freeR) - RectangleUtils::getMaxX(rIntersection);
            if (rightWidth > epsilon) {
                Rectangle2D rightRect(MPointDouble(RectangleUtils::getMaxX(rIntersection), RectangleUtils::getY(freeR)),
                                     MPointDouble(RectangleUtils::getMaxX(freeR), RectangleUtils::getMaxY(freeR)));
                nextFreeRectangles.push_back(rightRect);
                std::cerr << "computeFreeRectangles: Adding right rectangle: ("
                          << RectangleUtils::getX(rightRect) << ", " << RectangleUtils::getY(rightRect) << ") - ("
                          << RectangleUtils::getMaxX(rightRect) << ", " << RectangleUtils::getMaxY(rightRect) << ")" << std::endl;
            }
        }
    }
    freeRectangles = nextFreeRectangles;
    // Reduced verbosity for better performance
    // std::cerr << "computeFreeRectangles: New free rectangles count: " << freeRectangles.size() << std::endl;
}

void Bin::eliminateNonMaximal() {
    std::sort(freeRectangles.begin(), freeRectangles.end(), [](const Rectangle2D& a, const Rectangle2D& b) {
        return (RectangleUtils::getWidth(a) * RectangleUtils::getHeight(a)) > (RectangleUtils::getWidth(b) * RectangleUtils::getHeight(b));
    });

    auto& rects = freeRectangles;
    if (rects.size() < 2) return;

    auto new_end = std::remove_if(rects.begin(), rects.end(),
        [&](const Rectangle2D& r1) {
            return std::any_of(rects.begin(), rects.end(),
                [&](const Rectangle2D& r2) {
                    return (&r1 != &r2) && RectangleUtils::contains(r2, r1);
                });
        });

    rects.erase(new_end, rects.end());
}

void Bin::compress(bool useParallel) {
    if (placedPieces.empty()) {
        return;
    }

    // Use mutex to prevent concurrent compression operations
    std::lock_guard<std::mutex> lock(compressionMutex);

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
    int total_moves = 0;
    bool moved_in_iter = true;
    
    while (moved_in_iter) {
        moved_in_iter = false;

        if (vector.getY() != 0) {
            MVector u_y(0, vector.getY());
            
            // Store original position and remove from R-tree
            MArea originalPiece = pieceToMove;
            placedPiecesRTree.remove({originalPiece.getBoundingBox2D(), pieceIndex});
            
            pieceToMove.move(u_y);
            if (pieceToMove.isInside(this->dimension) && !isCollision(pieceToMove, pieceIndex)) {
                moved_in_iter = true;
                total_moves++;
                // Update R-tree with new position
                placedPiecesRTree.insert({pieceToMove.getBoundingBox2D(), pieceIndex});
            } else {
                // Restore original position and add back to R-tree
                pieceToMove = originalPiece;
                placedPiecesRTree.insert({pieceToMove.getBoundingBox2D(), pieceIndex});
            }
        }

        if (vector.getX() != 0) {
            MVector u_x(vector.getX(), 0);
            
            // Store original position and remove from R-tree
            MArea originalPiece = pieceToMove;
            placedPiecesRTree.remove({originalPiece.getBoundingBox2D(), pieceIndex});
            
            pieceToMove.move(u_x);
            if (pieceToMove.isInside(this->dimension) && !isCollision(pieceToMove, pieceIndex)) {
                moved_in_iter = true;
                total_moves++;
                // Update R-tree with new position
                placedPiecesRTree.insert({pieceToMove.getBoundingBox2D(), pieceIndex});
            } else {
                // Restore original position and add back to R-tree
                pieceToMove = originalPiece;
                placedPiecesRTree.insert({pieceToMove.getBoundingBox2D(), pieceIndex});
            }
        }
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
        for (int angle : Constants::STAGE23_ROTATION_ANGLES) {
            MArea candidate = pieceToTry;
            if (angle > 0) {
                candidate.rotate(static_cast<double>(angle));
            }

            if (auto placedPiece = dive(candidate, useParallel)) {
                placedPieces.push_back(*placedPiece);
                placedPiecesRTree.insert({placedPiece->getBoundingBox2D(), placedPieces.size() - 1});
                wasPlaced = true;
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
            placedPiecesRTree.insert({tempPiece.getBoundingBox2D(), tempIndex});
            
            compressPiece(tempIndex, MVector(0, -1.0));
            
            MArea finalPiece = placedPieces.back();
            // Remove the temporary piece from R-tree before popping
            placedPiecesRTree.remove({finalPiece.getBoundingBox2D(), tempIndex});
            placedPieces.pop_back();
            
            return finalPiece;
        }
    }
    
    MArea tempPiece = toDive;
    tempPiece.placeInPosition(binWidth - pieceWidth, binHeight - pieceHeight);
    if (!isCollision(tempPiece)) {
        size_t tempIndex = placedPieces.size();
        placedPieces.push_back(tempPiece);
        placedPiecesRTree.insert({tempPiece.getBoundingBox2D(), tempIndex});
        
        compressPiece(tempIndex, MVector(0, -1.0));
        
        MArea finalPiece = placedPieces.back();
        // Remove the temporary piece from R-tree before popping
        placedPiecesRTree.remove({finalPiece.getBoundingBox2D(), tempIndex});
        placedPieces.pop_back();
        
        return finalPiece;
    }

    return std::nullopt;
}

std::vector<MArea> Bin::placeInFreeZones(std::vector<MArea>& piecesToPlace, bool useParallel) {
    std::vector<MArea> notPlacedPieces;
    
    // Sort pieces by area (largest first)
    std::sort(piecesToPlace.begin(), piecesToPlace.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });

    // Sort free rectangles by area (largest first)
    std::vector<Rectangle2D> sortedFreeRects = freeRectangles;
    std::sort(sortedFreeRects.begin(), sortedFreeRects.end(), [](const Rectangle2D& a, const Rectangle2D& b) {
        return (RectangleUtils::getWidth(a) * RectangleUtils::getHeight(a)) >
               (RectangleUtils::getWidth(b) * RectangleUtils::getHeight(b));
    });

    for (auto& piece : piecesToPlace) {
        bool wasPlaced = false;
        
        // Try to place in each free rectangle (largest free rectangles first)
        for (const auto& freeRect : sortedFreeRects) {
            // Try all rotation angles (Stage 2 uses 10-degree increments)
            for (int angle : Constants::STAGE23_ROTATION_ANGLES) {
                MArea candidate = piece;
                if (angle > 0) {
                    candidate.rotate(static_cast<double>(angle));
                }
                
                Rectangle2D candidateBB = candidate.getBoundingBox2D();
                
                // Check if piece fits in this free rectangle
                if (RectangleUtils::fits(candidateBB, freeRect)) {
                    // Position the piece in the free rectangle
                    candidate.placeInPosition(RectangleUtils::getX(freeRect), RectangleUtils::getY(freeRect));
                    
                    // Check for collisions
                    if (!isCollision(candidate)) {
                        // Place the piece
                        placedPieces.push_back(candidate);
                        // Get the actual bounding box after placement
                        Rectangle2D actualBB = candidate.getBoundingBox2D();
                        placedPiecesRTree.insert({actualBB, placedPieces.size() - 1});
                        
                        // Update free rectangles
                        computeFreeRectangles(actualBB);
                        eliminateNonMaximal();
                        
                        wasPlaced = true;
                        break;
                    }
                }
            }
            if (wasPlaced) {
                break;
            }
        }
        
        if (!wasPlaced) {
            notPlacedPieces.push_back(piece);
        }
    }
    
    return notPlacedPieces;
}

bool Bin::moveAndReplace(size_t indexLimit) {
    bool movement = false;
    for (int i = static_cast<int>(placedPieces.size()) - 1; i >= static_cast<int>(indexLimit); --i) {
        MArea& currentArea = placedPieces[i];
        
        for (int j = 0; j < i; ++j) {
            const MArea& container = placedPieces[j];
            if (container.getFreeArea() > currentArea.getArea()) {
                Rectangle2D contBB = container.getBoundingBox2D();

                // Try all rotation angles (Stage 2 uses 10-degree increments)
                for (int angle : Constants::STAGE23_ROTATION_ANGLES) {
                    MArea candidate = currentArea;
                    if (angle > 0) {
                        candidate.rotate(static_cast<double>(angle));
                    }
                    candidate.placeInPosition(RectangleUtils::getX(contBB), RectangleUtils::getY(contBB));

                    if (auto swept = sweep(container, candidate, i)) {
                        freeRectangles.push_back(currentArea.getBoundingBox2D());
                        placedPieces[i] = *swept;
                        compressPiece(i, MVector(-1.0, -1.0));
                        computeFreeRectangles(swept->getBoundingBox2D());
                        eliminateNonMaximal();
                        movement = true;
                        goto next_piece; // Break outer loop and continue with next piece
                    }
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

bool Bin::verifyNoCollisions() const {
    bool hasCollisions = false;
    for (size_t i = 0; i < placedPieces.size(); ++i) {
        for (size_t j = i + 1; j < placedPieces.size(); ++j) {
            if (placedPieces[i].intersection(placedPieces[j])) {
                std::cerr << "Collision detected between pieces " << i << " and " << j << std::endl;
                Rectangle2D bb1 = placedPieces[i].getBoundingBox2D();
                Rectangle2D bb2 = placedPieces[j].getBoundingBox2D();
                std::cerr << "Piece " << i << " position: ("
                          << RectangleUtils::getX(bb1) << ", " << RectangleUtils::getY(bb1) << ") - ("
                          << RectangleUtils::getMaxX(bb1) << ", " << RectangleUtils::getMaxY(bb1) << ")" << std::endl;
                std::cerr << "Piece " << j << " position: ("
                          << RectangleUtils::getX(bb2) << ", " << RectangleUtils::getY(bb2) << ") - ("
                          << RectangleUtils::getMaxX(bb2) << ", " << RectangleUtils::getMaxY(bb2) << ")" << std::endl;
                hasCollisions = true;
            }
        }
    }
    return !hasCollisions;
}

void Bin::recomputeAllFreeRectangles() {
    // Clear existing free rectangles
    freeRectangles.clear();
    
    // Start with the entire bin as a free rectangle
    freeRectangles.push_back(dimension);
    
    // Subtract all placed pieces from the free space
    for (const auto& piece : placedPieces) {
        Rectangle2D pieceBB = piece.getBoundingBox2D();
        computeFreeRectangles(pieceBB);
    }
    
    // Eliminate non-maximal rectangles
    eliminateNonMaximal();
    
    // Reduced verbosity for better performance
    // std::cerr << "recomputeAllFreeRectangles: Rebuilt free rectangles for bin with "
    //           << placedPieces.size() << " pieces. New free rectangles count: "
    //           << freeRectangles.size() << std::endl;
}

// Look-ahead and backtracking implementation
Bin::BinState Bin::saveState() const {
    BinState state;
    state.placedPieces = placedPieces;
    state.freeRectangles = freeRectangles;
    state.placedPiecesRTree = placedPiecesRTree;
    return state;
}

void Bin::restoreState(const BinState& state) {
    placedPieces = state.placedPieces;
    freeRectangles = state.freeRectangles;
    placedPiecesRTree = state.placedPiecesRTree;
}

double Bin::evaluateBinUtilization() const {
    double binArea = RectangleUtils::getWidth(dimension) * RectangleUtils::getHeight(dimension);
    double occupiedArea = getOccupiedArea();
    return occupiedArea / binArea; // Utilization ratio (0.0 to 1.0)
}

Bin::PlacementResult Bin::tryPlacePiece(const MArea& piece, bool useParallel) {
    BinState originalState = saveState();
    Placement placement = findWhereToPlace(piece, useParallel);
    
    if (placement.rectIndex != -1) {
        const Rectangle2D& freeRect = freeRectangles[placement.rectIndex];
        MArea placedPiece = piece;
        
        if (placement.rotationAngle > 0) {
            placedPiece.rotate(static_cast<double>(placement.rotationAngle));
        }
        
        placedPiece.placeInPosition(RectangleUtils::getX(freeRect), RectangleUtils::getY(freeRect));
        
        if (!isCollision(placedPiece)) {
            Rectangle2D pieceBB = placedPiece.getBoundingBox2D();
            computeFreeRectangles(pieceBB);
            eliminateNonMaximal();
            
            placedPieces.push_back(placedPiece);
            size_t newIndex = placedPieces.size() - 1;
            placedPiecesRTree.insert({pieceBB, newIndex});
            
            BinState newState = saveState();
            restoreState(originalState); // Restore original state for evaluation
            
            return {true, placedPiece, newState};
        }
    }
    
    return {false, MArea(), originalState};
}

std::vector<MArea> Bin::boundingBoxPackingWithLookAhead(std::vector<MArea>& piecesToPlace, bool useParallel, int lookAheadDepth) {
    std::vector<MArea> notPlacedPieces;
    
    // Sort pieces by area (largest first)
    std::sort(piecesToPlace.begin(), piecesToPlace.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });
    
    while (!piecesToPlace.empty()) {
        BinState originalState = saveState();
        double bestUtilization = evaluateBinUtilization();
        std::vector<MArea> bestPlacementOrder;
        BinState bestState = originalState;
        
        // Get the next few pieces for look-ahead (up to lookAheadDepth or remaining pieces)
        size_t lookAheadCount = std::min(static_cast<size_t>(lookAheadDepth), piecesToPlace.size());
        std::vector<MArea> lookAheadPieces(piecesToPlace.begin(), piecesToPlace.begin() + lookAheadCount);
        
        // Try all permutations of the look-ahead pieces
        // Sort by area for permutation generation
        std::sort(lookAheadPieces.begin(), lookAheadPieces.end(), [](const MArea& a, const MArea& b) {
            return a.getArea() > b.getArea();
        });
        do {
            BinState currentState = originalState;
            restoreState(originalState);
            
            std::vector<MArea> placedInThisOrder;
            bool allPlaced = true;
            
            // Try to place all pieces in this order
            for (const auto& piece : lookAheadPieces) {
                auto result = tryPlacePiece(piece, useParallel);
                if (result.success) {
                    placedInThisOrder.push_back(result.placedPiece);
                    restoreState(result.newState);
                    currentState = result.newState;
                } else {
                    allPlaced = false;
                    break;
                }
            }
            
            if (allPlaced) {
                double currentUtilization = evaluateBinUtilization();
                if (currentUtilization > bestUtilization) {
                    bestUtilization = currentUtilization;
                    bestPlacementOrder = placedInThisOrder;
                    bestState = currentState;
                }
            }
            
            restoreState(originalState);
            
        } while (std::next_permutation(lookAheadPieces.begin(), lookAheadPieces.end(),
            [](const MArea& a, const MArea& b) { return a.getArea() > b.getArea(); }));
        
        // Apply the best found placement
        if (!bestPlacementOrder.empty()) {
            restoreState(bestState);
            
            // Remove the placed pieces from the toPlace list
            for (const auto& placedPiece : bestPlacementOrder) {
                auto it = std::find_if(piecesToPlace.begin(), piecesToPlace.end(),
                    [&placedPiece](const MArea& p) {
                        return std::abs(p.getArea() - placedPiece.getArea()) < 1e-9;
                    });
                if (it != piecesToPlace.end()) {
                    piecesToPlace.erase(it);
                }
            }
        } else {
            // Fallback to greedy placement if no better order found
            auto piece = piecesToPlace.front();
            Placement placement = findWhereToPlace(piece, useParallel);
            
            if (placement.rectIndex != -1) {
                const Rectangle2D& freeRect = freeRectangles[placement.rectIndex];
                MArea placedPiece = piece;
                
                if (placement.rotationAngle > 0) {
                    placedPiece.rotate(static_cast<double>(placement.rotationAngle));
                }
                
                placedPiece.placeInPosition(RectangleUtils::getX(freeRect), RectangleUtils::getY(freeRect));
                
                if (!isCollision(placedPiece)) {
                    Rectangle2D pieceBB = placedPiece.getBoundingBox2D();
                    computeFreeRectangles(pieceBB);
                    eliminateNonMaximal();
                    
                    placedPieces.push_back(placedPiece);
                    size_t newIndex = placedPieces.size() - 1;
                    placedPiecesRTree.insert({pieceBB, newIndex});
                    
                    piecesToPlace.erase(piecesToPlace.begin());
                } else {
                    notPlacedPieces.push_back(piece);
                    piecesToPlace.erase(piecesToPlace.begin());
                }
            } else {
                notPlacedPieces.push_back(piece);
                piecesToPlace.erase(piecesToPlace.begin());
            }
        }
    }
    
    return notPlacedPieces;
}

std::vector<MArea> Bin::placeInFreeZonesWithLookAhead(std::vector<MArea>& piecesToPlace, bool useParallel, int lookAheadDepth) {
    std::vector<MArea> notPlacedPieces;
    
    // Sort pieces by area (largest first)
    std::sort(piecesToPlace.begin(), piecesToPlace.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });
    
    while (!piecesToPlace.empty()) {
        BinState originalState = saveState();
        double bestUtilization = evaluateBinUtilization();
        std::vector<MArea> bestPlacementOrder;
        BinState bestState = originalState;
        
        // Get the next few pieces for look-ahead (up to lookAheadDepth or remaining pieces)
        size_t lookAheadCount = std::min(static_cast<size_t>(lookAheadDepth), piecesToPlace.size());
        std::vector<MArea> lookAheadPieces(piecesToPlace.begin(), piecesToPlace.begin() + lookAheadCount);
        
        // Try all permutations of the look-ahead pieces
        // Sort by area for permutation generation
        std::sort(lookAheadPieces.begin(), lookAheadPieces.end(), [](const MArea& a, const MArea& b) {
            return a.getArea() > b.getArea();
        });
        do {
            BinState currentState = originalState;
            restoreState(originalState);
            
            std::vector<MArea> placedInThisOrder;
            bool allPlaced = true;
            
            // Try to place all pieces in this order using free zone placement
            for (const auto& piece : lookAheadPieces) {
                // Create a temporary vector with just this piece for free zone placement
                std::vector<MArea> tempVec = {piece};
                auto result = placeInFreeZones(tempVec, useParallel);
                
                if (result.empty()) {
                    // Piece was placed successfully
                    placedInThisOrder.push_back(piece);
                    currentState = saveState();
                } else {
                    allPlaced = false;
                    break;
                }
            }
            
            if (allPlaced) {
                double currentUtilization = evaluateBinUtilization();
                if (currentUtilization > bestUtilization) {
                    bestUtilization = currentUtilization;
                    bestPlacementOrder = placedInThisOrder;
                    bestState = currentState;
                }
            }
            
            restoreState(originalState);
            
        } while (std::next_permutation(lookAheadPieces.begin(), lookAheadPieces.end(),
            [](const MArea& a, const MArea& b) { return a.getArea() > b.getArea(); }));
        
        // Apply the best found placement
        if (!bestPlacementOrder.empty()) {
            restoreState(bestState);
            
            // Remove the placed pieces from the toPlace list
            for (const auto& placedPiece : bestPlacementOrder) {
                auto it = std::find_if(piecesToPlace.begin(), piecesToPlace.end(),
                    [&placedPiece](const MArea& p) {
                        return std::abs(p.getArea() - placedPiece.getArea()) < 1e-9;
                    });
                if (it != piecesToPlace.end()) {
                    piecesToPlace.erase(it);
                }
            }
        } else {
            // Fallback to greedy placement if no better order found
            auto piece = piecesToPlace.front();
            
            // Try to place in free zones using the original method
            std::vector<MArea> tempVec = {piece};
            auto result = placeInFreeZones(tempVec, useParallel);
            
            if (result.empty()) {
                // Piece was placed successfully
                piecesToPlace.erase(piecesToPlace.begin());
            } else {
                notPlacedPieces.push_back(piece);
                piecesToPlace.erase(piecesToPlace.begin());
            }
        }
    }
    
    return notPlacedPieces;
}
