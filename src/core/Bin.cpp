#include "Bin.h"
#include "primitives/Rectangle.h" // For RectangleUtils
#include "core/Constants.h"
#include <limits>
#include <algorithm>
#include <numeric>
#include <vector>


Bin::Bin(const Rectangle2D& dimension, bool useNFP) : dimension(dimension), useNFPCollisionDetection(useNFP) {
    freeRectangles.push_back(dimension);
}

Bin::Bin(const Bin& other) :
    dimension(other.dimension),
    placedPieces(other.placedPieces),
    freeRectangles(other.freeRectangles),
    placedPiecesRTree(other.placedPiecesRTree),
    useNFPCollisionDetection(other.useNFPCollisionDetection)
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
    useNFPCollisionDetection = other.useNFPCollisionDetection;
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
    // Switch between NFP-based and R-tree based collision detection
    if (useNFPCollisionDetection) {
        return isCollisionNFP(piece, ignoredPieceIndex);
    }
    
    // Original R-tree based collision detection
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

bool Bin::isCollisionNFP(const MArea& piece, std::optional<size_t> ignoredPieceIndex) {
    // Create obstacles list, excluding ignored piece if specified
    std::vector<MArea> obstacles;
    obstacles.reserve(placedPieces.size());
    
    for (size_t i = 0; i < placedPieces.size(); ++i) {
        if (!ignoredPieceIndex || i != *ignoredPieceIndex) {
            obstacles.push_back(placedPieces[i]);
        }
    }
    
    // Get the piece's current position
    Rectangle2D pieceBB = piece.getBoundingBox2D();
    PointD currentPos(RectangleUtils::getX(pieceBB), RectangleUtils::getY(pieceBB));
    
    // Use NFP to check if current position is valid
    return !nfpManager.isValidPlacement(piece, currentPos, obstacles, dimension);
}

bool Bin::isValidPlacementNFP(const MArea& piece, const PointD& position, std::optional<size_t> ignoredPieceIndex) {
    // Create obstacles list, excluding ignored piece if specified
    std::vector<MArea> obstacles;
    obstacles.reserve(placedPieces.size());
    
    for (size_t i = 0; i < placedPieces.size(); ++i) {
        if (!ignoredPieceIndex || i != *ignoredPieceIndex) {
            obstacles.push_back(placedPieces[i]);
        }
    }
    
    // Use NFP to check if position is valid
    return nfpManager.isValidPlacement(piece, position, obstacles, dimension);
}

void Bin::addPieceForTesting(const MArea& piece) {
    placedPieces.push_back(piece);
    Rectangle2D pieceBB = piece.getBoundingBox2D();
    placedPiecesRTree.insert({pieceBB, placedPieces.size() - 1});
}


Bin::Placement Bin::findWhereToPlace(const MArea& piece) {
    Placement bestPlacement;
    double minWastage = std::numeric_limits<double>::max();
    Rectangle2D pieceBB = piece.getBoundingBox2D();

    // Simplified sequential processing - no parallel overhead
    for (int i = static_cast<int>(freeRectangles.size()) - 1; i >= 0; --i) {
        const auto& freeRect = freeRectangles[i];
        
        // Check normal orientation
        if (RectangleUtils::fits(pieceBB, freeRect)) {
            double wastage = std::min(
                RectangleUtils::getWidth(freeRect) - RectangleUtils::getWidth(pieceBB),
                RectangleUtils::getHeight(freeRect) - RectangleUtils::getHeight(pieceBB)
            );
            if (wastage < minWastage) {
                minWastage = wastage;
                bestPlacement.rectIndex = i;
                bestPlacement.requiresRotation = false;
            }
        }

        // Check rotated orientation
        if (RectangleUtils::fitsRotated(pieceBB, freeRect)) {
            double wastage = std::min(
                RectangleUtils::getWidth(freeRect) - RectangleUtils::getHeight(pieceBB),
                RectangleUtils::getHeight(freeRect) - RectangleUtils::getWidth(pieceBB)
            );
            if (wastage < minWastage) {
                minWastage = wastage;
                bestPlacement.rectIndex = i;
                bestPlacement.requiresRotation = true;
            }
        }
    }

    return bestPlacement;
}

std::vector<MArea> Bin::boundingBoxPacking(std::vector<MArea>& piecesToPlace) {
    std::vector<MArea> notPlacedPieces;

    std::sort(piecesToPlace.begin(), piecesToPlace.end(), [](const MArea& a, const MArea& b) {
        return a.getArea() > b.getArea();
    });

    for (const auto& piece : piecesToPlace) {
        Placement placement = findWhereToPlace(piece);

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

void Bin::compress() {
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
            if (pieceToMove.isInside(this->dimension) && !isCollision(pieceToMove, pieceIndex)) {
                moved_in_iter = true;
                total_moves++;
            } else {
                pieceToMove.move(u_y.inverse());
            }
        }

        if (vector.getX() != 0) {
            MVector u_x(vector.getX(), 0);
            pieceToMove.move(u_x);
            if (pieceToMove.isInside(this->dimension) && !isCollision(pieceToMove, pieceIndex)) {
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


std::vector<MArea> Bin::dropPieces(const std::vector<MArea>& piecesToDrop) {
    std::vector<MArea> unplacedPieces;

    for (const auto& pieceToTry : piecesToDrop) {
        bool wasPlaced = false;
        for (int angle : Constants::ROTATION_ANGLES) {
            MArea candidate = pieceToTry;
            if (angle > 0) {
                candidate.rotate(static_cast<double>(angle));
            }

            if (auto placedPiece = dive(candidate)) {
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

std::optional<MArea> Bin::dive(MArea toDive) {
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

// ===== SOPHISTICATED FREE SPACE ISLAND DETECTION =====

void Bin::FreeSpaceIsland::computePrincipalAxes() {
    std::vector<MPointDouble> vertices = shape.getOuterVertices();
    if (vertices.empty()) {
        // Fallback for empty shape
        majorAxisLength = minorAxisLength = robustness = 0.0;
        principalAngle = 0.0;
        aspectRatio = 1.0;
        centroid = PointD(0, 0);
        return;
    }
    
    // Compute true centroid
    double cx = 0.0, cy = 0.0;
    for (const auto& vertex : vertices) {
        cx += vertex.x();
        cy += vertex.y();
    }
    cx /= vertices.size();
    cy /= vertices.size();
    centroid = PointD(cx, cy);
    
    // Compute covariance matrix for Principal Component Analysis
    double xx = 0, xy = 0, yy = 0;
    for (const auto& vertex : vertices) {
        double dx = vertex.x() - cx;
        double dy = vertex.y() - cy;
        xx += dx * dx;
        xy += dx * dy;
        yy += dy * dy;
    }
    xx /= vertices.size();
    xy /= vertices.size();
    yy /= vertices.size();
    
    // Find eigenvalues and eigenvectors of covariance matrix
    double trace = xx + yy;
    double det = xx * yy - xy * xy;
    
    if (det < 1e-10) {
        // Degenerate case - use bounding box approach
        Rectangle2D bb = shape.getBoundingBox2D();
        majorAxisLength = std::max(RectangleUtils::getWidth(bb), RectangleUtils::getHeight(bb));
        minorAxisLength = std::min(RectangleUtils::getWidth(bb), RectangleUtils::getHeight(bb));
        principalAngle = (RectangleUtils::getWidth(bb) > RectangleUtils::getHeight(bb)) ? 0.0 : 90.0;
    } else {
        double lambda1 = (trace + sqrt(trace * trace - 4 * det)) / 2;  // Major eigenvalue
        double lambda2 = (trace - sqrt(trace * trace - 4 * det)) / 2;  // Minor eigenvalue
        
        // Principal axis angle (major eigenvector direction)
        if (std::abs(xy) > 1e-9) {
            principalAngle = atan2(lambda1 - xx, xy) * 180.0 / M_PI;
        } else {
            principalAngle = (xx > yy) ? 0.0 : 90.0;  // Axis-aligned case
        }
        
        // Project all vertices onto principal axes to find actual extents
        double maxProj = -std::numeric_limits<double>::max();
        double minProj = std::numeric_limits<double>::max();
        double maxPerpProj = -std::numeric_limits<double>::max();
        double minPerpProj = std::numeric_limits<double>::max();
        
        double cosAngle = cos(principalAngle * M_PI / 180.0);
        double sinAngle = sin(principalAngle * M_PI / 180.0);
        
        for (const auto& vertex : vertices) {
            double dx = vertex.x() - cx;
            double dy = vertex.y() - cy;
            
            // Project onto major axis
            double projMajor = dx * cosAngle + dy * sinAngle;
            maxProj = std::max(maxProj, projMajor);
            minProj = std::min(minProj, projMajor);
            
            // Project onto minor axis (perpendicular)
            double projMinor = -dx * sinAngle + dy * cosAngle;
            maxPerpProj = std::max(maxPerpProj, projMinor);
            minPerpProj = std::min(minPerpProj, projMinor);
        }
        
        majorAxisLength = maxProj - minProj;
        minorAxisLength = maxPerpProj - minPerpProj;
    }
    
    robustness = minorAxisLength;  // Thickness = minor axis length
    aspectRatio = (minorAxisLength > 1e-9) ? majorAxisLength / minorAxisLength : 1000.0;  // Very high ratio for degenerate
}

std::vector<Bin::FreeSpaceIsland> Bin::detectAdaptiveFreeSpaceIslands() {
    std::vector<FreeSpaceIsland> islands;

    MArea binArea({
        MPointDouble(RectangleUtils::getX(dimension), RectangleUtils::getY(dimension)),
        MPointDouble(RectangleUtils::getMaxX(dimension), RectangleUtils::getY(dimension)),
        MPointDouble(RectangleUtils::getMaxX(dimension), RectangleUtils::getMaxY(dimension)),
        MPointDouble(RectangleUtils::getX(dimension), RectangleUtils::getMaxY(dimension))
    }, -1);

    if (placedPieces.empty()) {
        islands.emplace_back(binArea);
        return islands;
    }

    try {
        // Compute the union of all placed pieces to define the occupied region.
        MArea occupiedRegion = computePlacedPiecesUnion();

        // Subtract the occupied region from the total bin area to get the free space.
        MultiPolygon freePolygons;
        boost::geometry::difference(binArea.getShape(), occupiedRegion.getShape(), freePolygons);

        // Each polygon in the resulting multi-polygon is a disconnected free space island.
        for (const auto& poly : freePolygons) {
            if (boost::geometry::area(poly) > 1.0) { // Ignore tiny sliver polygons
                islands.emplace_back(MArea(poly, -1));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in adaptive island detection: " << e.what() << std::endl;
        // Fallback to empty islands
    }

    return islands;
}

MArea Bin::computePlacedPiecesUnion() {
    if (placedPieces.empty()) {
        return MArea();
    }

    MArea result = placedPieces[0];
    for (size_t i = 1; i < placedPieces.size(); ++i) {
        result.add(placedPieces[i]);
    }
    return result;
}

std::vector<MArea> Bin::placeInGlobalFreeSpace(std::vector<MArea>& piecesToPlace, bool extendedRotations) {
    std::vector<MArea> unplacedPieces;
    
    if (piecesToPlace.empty()) {
        return unplacedPieces;
    }
    
    // Sort pieces by area (largest first) for better placement order
    std::sort(piecesToPlace.begin(), piecesToPlace.end(), 
        [](const MArea& a, const MArea& b) {
            return a.getArea() > b.getArea();
        });
    
    for (const auto& piece : piecesToPlace) {
        // Detect current free space islands (updated after each placement)
        std::vector<FreeSpaceIsland> islands = detectAdaptiveFreeSpaceIslands();
        
        // Find best placement for this piece using sophisticated island-based approach
        auto placement = findBestIslandPlacement(piece, islands, extendedRotations);
        
        if (placement.has_value()) {
            // Place the piece
            MArea placedPiece = piece;
            
            // Apply rotation if needed
            if (std::abs(placement->rotationAngle) > 1e-6) {
                placedPiece.rotate(placement->rotationAngle);
            }
            
            // Move to position
            placedPiece.placeInPosition(placement->position.x, placement->position.y);
            
            // Verify no collision (safety check)
            if (!isCollision(placedPiece)) {
                placedPieces.push_back(placedPiece);
                placedPiecesRTree.insert({placedPiece.getBoundingBox2D(), placedPieces.size() - 1});
            } else {
                unplacedPieces.push_back(piece);
            }
        } else {
            unplacedPieces.push_back(piece);
        }
    }
    
    return unplacedPieces;
}

std::optional<Bin::GlobalPlacement> Bin::findBestIslandPlacement(
    const MArea& piece, 
    const std::vector<FreeSpaceIsland>& islands,
    bool extendedRotations) {
    
    if (islands.empty()) {
        return std::nullopt;
    }
    
    std::optional<GlobalPlacement> bestPlacement;
    double bestScore = -1e9;

    auto sortedIslands = islands;
    std::sort(sortedIslands.begin(), sortedIslands.end(), 
        [](const FreeSpaceIsland& a, const FreeSpaceIsland& b) {
            return a.area > b.area;
        });
    
    for (size_t islandIdx = 0; islandIdx < sortedIslands.size(); ++islandIdx) {
        const auto& island = sortedIslands[islandIdx];
        
        if (piece.getArea() > island.area * 1.1) {
            continue;
        }
        
        std::vector<double> rotationAngles = {0.0, 90.0, 180.0, 270.0};
        if (extendedRotations) {
            for (int angle = 30; angle < 360; angle += 30) { // Coarser steps for performance
                rotationAngles.push_back(static_cast<double>(angle));
            }
        }
        
        for (double angle : rotationAngles) {
            MArea rotatedPiece = piece;
            if (std::abs(angle) > 1e-6) {
                rotatedPiece.rotate(angle);
            }
            
            Rectangle2D rotatedBB = rotatedPiece.getBoundingBox2D();
            Rectangle2D islandBB = island.shape.getBoundingBox2D();

            // --- Grid Search for Placement ---
            double dx = std::max(5.0, RectangleUtils::getWidth(rotatedBB) / 4.0);
            double dy = std::max(5.0, RectangleUtils::getHeight(rotatedBB) / 4.0);

            for (double y = islandBB.min_corner().y(); y + RectangleUtils::getHeight(rotatedBB) <= islandBB.max_corner().y() + 1e-9; y += dy) {
                for (double x = islandBB.min_corner().x(); x + RectangleUtils::getWidth(rotatedBB) <= islandBB.max_corner().x() + 1e-9; x += dx) {
                    
                    MArea candidate = rotatedPiece;
                    candidate.placeInPosition(x, y);

                    if (boost::geometry::within(candidate.getShape(), island.shape.getShape())) {
                        double score = -y * 1000 - x; // Prioritize bottom-left
                        if (score > bestScore) {
                            bestScore = score;
                            bestPlacement = GlobalPlacement{
                                islandIdx,
                                PointD(x, y),
                                angle,
                                island.area - candidate.getArea()
                            };
                        }
                    }
                }
            }
            // --- End of Grid Search ---
        }
    }
    
    return bestPlacement;
}

std::vector<MArea> Bin::decomposeFreeRegion(const MArea& complexRegion) {
    // For now, return the region as-is
    // TODO: Implement sophisticated polygon decomposition for complex regions
    return {complexRegion};
}
