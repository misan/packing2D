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
    std::lock_guard<std::mutex> lock(collisionMutex);
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

    auto checkPlacement = [&](int i) {
        const auto& freeRect = freeRectangles[i];
        if (RectangleUtils::fits(pieceBB, freeRect)) {
            double wastage = std::min(
                RectangleUtils::getWidth(freeRect) - RectangleUtils::getWidth(pieceBB),
                RectangleUtils::getHeight(freeRect) - RectangleUtils::getHeight(pieceBB)
            );
            std::lock_guard<std::mutex> lock(mtx);
            if (wastage < minWastage) {
                minWastage = wastage;
                bestPlacement.rectIndex = i;
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
                bestPlacement.rectIndex = i;
                bestPlacement.requiresRotation = true;
            }
        }
    };

    if (useParallel && !g_parallelism_disabled_for_tests) {
        std::vector<int> indices(freeRectangles.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::for_each(std::execution::par, indices.begin(), indices.end(), checkPlacement);
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
            } else {
                notPlacedPieces.push_back(piece);
            }
        } else {
            notPlacedPieces.push_back(piece);
        }
    }
    return notPlacedPieces;
}

void Bin::computeFreeRectangles(const Rectangle2D& justPlacedPieceBB) {
    std::vector<Rectangle2D> nextFreeRectangles;
    const double epsilon = 1e-9;

    for (const auto& freeR : freeRectangles) {
        if (!RectangleUtils::intersects(freeR, justPlacedPieceBB)) {
            nextFreeRectangles.push_back(freeR);
        } else {
            Rectangle2D rIntersection = RectangleUtils::createIntersection(freeR, justPlacedPieceBB);
            double topHeight = RectangleUtils::getMaxY(freeR) - RectangleUtils::getMaxY(rIntersection);
            if (topHeight > epsilon) {
                nextFreeRectangles.emplace_back(MPointDouble(RectangleUtils::getX(freeR), RectangleUtils::getMaxY(rIntersection)), MPointDouble(RectangleUtils::getMaxX(freeR), RectangleUtils::getMaxY(freeR)));
            }
            double bottomHeight = RectangleUtils::getY(rIntersection) - RectangleUtils::getY(freeR);
            if (bottomHeight > epsilon) {
                nextFreeRectangles.emplace_back(MPointDouble(RectangleUtils::getX(freeR), RectangleUtils::getY(freeR)), MPointDouble(RectangleUtils::getMaxX(freeR), RectangleUtils::getY(rIntersection)));
            }
            double leftWidth = RectangleUtils::getX(rIntersection) - RectangleUtils::getX(freeR);
            if (leftWidth > epsilon) {
                nextFreeRectangles.emplace_back(MPointDouble(RectangleUtils::getX(freeR), RectangleUtils::getY(freeR)), MPointDouble(RectangleUtils::getX(rIntersection), RectangleUtils::getMaxY(freeR)));
            }
            double rightWidth = RectangleUtils::getMaxX(freeR) - RectangleUtils::getMaxX(rIntersection);
            if (rightWidth > epsilon) {
                nextFreeRectangles.emplace_back(MPointDouble(RectangleUtils::getMaxX(rIntersection), RectangleUtils::getY(freeR)), MPointDouble(RectangleUtils::getMaxX(freeR), RectangleUtils::getMaxY(freeR)));
            }
        }
    }
    freeRectangles = nextFreeRectangles;
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

    if (useParallel && !g_parallelism_disabled_for_tests) {
        bool moved_in_pass = true;
        while (moved_in_pass) {
            std::vector<MArea> next_placed_pieces = placedPieces;
            std::vector<bool> did_move(placedPieces.size(), false);

            std::vector<size_t> indices(placedPieces.size());
            std::iota(indices.begin(), indices.end(), 0);

            // Parallel calculation of new positions
            std::for_each(std::execution::par, indices.begin(), indices.end(),
                [&](size_t i) {
                    did_move[i] = compressPiece_parallel_helper(next_placed_pieces[i], i);
                });

            // Check if any piece moved in this pass
            if (std::any_of(did_move.begin(), did_move.end(), [](bool v){ return v; })) {
                moved_in_pass = true;
                placedPieces = next_placed_pieces;

                // Rebuild R-tree
                placedPiecesRTree.clear();
                for (size_t i = 0; i < placedPieces.size(); ++i) {
                    placedPiecesRTree.insert({placedPieces[i].getBoundingBox2D(), i});
                }
            } else {
                moved_in_pass = false;
            }
        }
    } else { // Sequential version
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
