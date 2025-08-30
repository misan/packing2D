#include "NFPManager.h"
#include "primitives/Rectangle.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

#ifdef HAVE_CLIPPER2
using namespace Clipper2Lib;
#endif

NFPManager::NFPManager() : cacheHits(0), cacheMisses(0) {
}

NFPManager::~NFPManager() {
}

PathD NFPManager::areaToPath(const MArea& area) {
    PathD path;
    
    if (area.isEmpty()) {
        return path;
    }

    // Extract actual vertices from the MArea polygon
    std::vector<MPointDouble> vertices = area.getOuterVertices();
    
    if (vertices.empty()) {
        return path;
    }
    
    // Convert MPointDouble to Clipper2 PointD
    path.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        path.push_back(PointD(vertex.x(), vertex.y()));
    }
    
    return path;
}

MArea NFPManager::pathToArea(const PathD& path, int id) {
    if (path.empty()) {
        return MArea();
    }

    std::vector<MPointDouble> points;
    for (const auto& pt : path) {
        points.emplace_back(pt.x, pt.y);
    }
    
    return MArea(points, id);
}

PathD NFPManager::computeNFP(const MArea& pieceA, const MArea& pieceB) {
#ifdef HAVE_CLIPPER2
    // pieceA is the moving piece, pieceB is the stationary obstacle.
    // The NFP is the Minkowski sum of B and -A, where -A is A reflected through its reference point.
    // To make this independent of A's current position, we must normalize A before reflecting it.
    // The result will be the NFP in global coordinates, correctly positioned relative to B.

    // 1. Get path for stationary obstacle B in its current global position.
    PathD pathB = areaToPath(pieceB);
    if (pathB.empty()) {
        return PathD();
    }

    // 2. Get path for moving piece A and normalize it to its reference point (bottom-left of BB).
    Rectangle2D bbA = pieceA.getBoundingBox2D();
    double offsetAx = RectangleUtils::getX(bbA);
    double offsetAy = RectangleUtils::getY(bbA);
    
    PathD pathA_norm = areaToPath(pieceA);
    if (pathA_norm.empty()) {
        return PathD();
    }
    for (auto& pt : pathA_norm) {
        pt.x -= offsetAx;
        pt.y -= offsetAy;
    }

    // 3. Compute -A_norm (A_norm reflected through its reference point, which is now the origin).
    PathD negatedA_norm;
    negatedA_norm.reserve(pathA_norm.size());
    for (const auto& pt : pathA_norm) {
        negatedA_norm.push_back(PointD(-pt.x, -pt.y));
    }
    std::reverse(negatedA_norm.begin(), negatedA_norm.end());
    
    // 4. Compute the NFP by taking the Minkowski sum of the stationary obstacle B and the reflected normalized moving piece A.
    // The result is the forbidden region for A's reference point, correctly located in the bin's coordinate system.
    PathsD result = MinkowskiSum(pathB, negatedA_norm, true);
    
    // 5. Return the largest resulting polygon, which is the main NFP boundary.
    if (!result.empty()) {
        auto maxElement = std::max_element(result.begin(), result.end(),
            [](const PathD& a, const PathD& b) {
                return std::abs(Area(a)) < std::abs(Area(b));
            });
        return *maxElement;
    }
    
    return PathD();
#else
    // Fallback: Create a simple rectangular NFP approximation
    Rectangle2D bboxA = pieceA.getBoundingBox2D();
    Rectangle2D bboxB = pieceB.getBoundingBox2D();
    
    double widthA = RectangleUtils::getWidth(bboxA);
    double heightA = RectangleUtils::getHeight(bboxA);
    double widthB = RectangleUtils::getWidth(bboxB);
    double heightB = RectangleUtils::getHeight(bboxB);
    
    // Simple rectangular NFP: expand obstacle by piece dimensions
    PathD nfp;
    double x = RectangleUtils::getX(bboxB) - widthA;
    double y = RectangleUtils::getY(bboxB) - heightA;
    double maxX = RectangleUtils::getMaxX(bboxB);
    double maxY = RectangleUtils::getMaxY(bboxB);
    
    nfp.push_back(PointD(x, y));
    nfp.push_back(PointD(maxX, y));
    nfp.push_back(PointD(maxX, maxY));
    nfp.push_back(PointD(x, maxY));
    
    return nfp;
#endif
}

PathsD NFPManager::getValidPlacementRegions(const MArea& piece, 
                                           const std::vector<MArea>& obstacles,
                                           const Rectangle2D& containerBounds) {
    // Start with the Inner-Fit Polygon (IFP) - valid area within container
    PathD ifp = computeIFP(piece, containerBounds);
    PathsD validRegions;
    validRegions.push_back(ifp);
    
#ifdef HAVE_CLIPPER2
    // Subtract NFPs for each obstacle
    for (const auto& obstacle : obstacles) {
        PathD nfp = getCachedNFP(piece, obstacle);
        if (!nfp.empty()) {
            PathsD newValidRegions;
            
            // Subtract NFP from each current valid region
            for (const auto& region : validRegions) {
                PathsD subtracted = Difference(PathsD{region}, PathsD{nfp}, FillRule::NonZero);
                for (const auto& subRegion : subtracted) {
                    newValidRegions.push_back(subRegion);
                }
            }
            
            validRegions = newValidRegions;
        }
    }
#else
    // Fallback: Simple approach - if any obstacles exist, reduce valid region
    if (!obstacles.empty()) {
        // For simplicity, just return the IFP for now
        // A more sophisticated fallback could implement basic polygon subtraction
    }
#endif
    
    return validRegions;
}

bool NFPManager::isValidPlacement(const MArea& piece,
                                const PointD& position,
                                const std::vector<MArea>& obstacles,
                                const Rectangle2D& containerBounds) {
#ifdef HAVE_CLIPPER2
    // Use full NFP-based validation
    PathsD validRegions = getValidPlacementRegions(piece, obstacles, containerBounds);
    
    // Check if the position is inside any valid region
    for (const auto& region : validRegions) {
        if (PointInPolygon(position, region) != PointInPolygonResult::IsOutside) {
            return true;
        }
    }
    
    return false;
#else
    // Fallback: Direct collision checking using bounding boxes
    // Get piece dimensions from its bounding box (but ignore current position)
    Rectangle2D pieceBBox = piece.getBoundingBox2D();
    double pieceWidth = RectangleUtils::getWidth(pieceBBox);
    double pieceHeight = RectangleUtils::getHeight(pieceBBox);
    
    // Create piece bounding box at the test position
    Rectangle2D testBBox(
        MPointDouble(position.x, position.y),
        MPointDouble(position.x + pieceWidth, position.y + pieceHeight)
    );
    
    // Check container bounds
    if (!RectangleUtils::contains(containerBounds, testBBox)) {
        return false;
    }
    
    // Check collision with obstacles
    for (const auto& obstacle : obstacles) {
        Rectangle2D obstacleBBox = obstacle.getBoundingBox2D();
        if (RectangleUtils::intersects(testBBox, obstacleBBox)) {
            return false; // Collision detected
        }
    }
    
    return true; // No collisions
#endif
}

std::optional<PointD> NFPManager::findBestPlacement(const MArea& piece,
                                                   const std::vector<MArea>& obstacles,
                                                   const Rectangle2D& containerBounds) {
    PathsD validRegions = getValidPlacementRegions(piece, obstacles, containerBounds);
    
    if (validRegions.empty()) {
        return std::nullopt;
    }
    
    // Find the bottom-left-most position in the largest valid region
    auto maxRegion = std::max_element(validRegions.begin(), validRegions.end(),
        [](const PathD& a, const PathD& b) {
#ifdef HAVE_CLIPPER2
            return std::abs(Area(a)) < std::abs(Area(b));
#else
            // Fallback: Compare by number of vertices as rough area proxy
            return a.size() < b.size();
#endif
        });
    
    if (maxRegion->empty()) {
        return std::nullopt;
    }
    
    // Find the bottom-left point of the largest region
    PointD bottomLeft = (*maxRegion)[0];
    for (const auto& pt : *maxRegion) {
        if (pt.y < bottomLeft.y || (pt.y == bottomLeft.y && pt.x < bottomLeft.x)) {
            bottomLeft = pt;
        }
    }
    
    return bottomLeft;
}

double NFPManager::computeMaxCompression(const MArea& piece,
                                       const std::vector<MArea>& obstacles,
                                       const Rectangle2D& containerBounds,
                                       const PointD& direction) {
    // This is a simplified implementation
    // TODO: Implement proper offset-based compression using Clipper2
    
    // For now, return a small step size for iterative compression
    return 1.0;
}

void NFPManager::clearCache() {
    nfpCache.clear();
    cacheHits = 0;
    cacheMisses = 0;
}

NFPManager::CacheStats NFPManager::getCacheStats() const {
    return {cacheHits, cacheMisses, nfpCache.size()};
}

std::string NFPManager::generatePieceHash(const MArea& piece) const {
    // Generate a hash based on piece geometry, ignoring position and ID
    Rectangle2D bbox = piece.getBoundingBox2D();
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6)
        << RectangleUtils::getWidth(bbox) << ","
        << RectangleUtils::getHeight(bbox) << ","
        << piece.getArea() << ","
        << piece.getRotation() << ","
        << piece.getVertexCount();
    
    // Add normalized vertex information for better uniqueness
    auto vertices = piece.getOuterVertices();
    if (!vertices.empty()) {
        // Normalize to origin for position-independent hashing
        double minX = vertices[0].x(), minY = vertices[0].y();
        for (const auto& v : vertices) {
            minX = std::min(minX, v.x());
            minY = std::min(minY, v.y());
        }
        
        for (const auto& v : vertices) {
            oss << ";" << (v.x() - minX) << "," << (v.y() - minY);
        }
    }
    
    return oss.str();
}

PathD NFPManager::getCachedNFP(const MArea& pieceA, const MArea& pieceB) {
    NFPCacheKey key{generatePieceHash(pieceA), generatePieceHash(pieceB)};
    
    auto it = nfpCache.find(key);
    if (it != nfpCache.end()) {
        ++cacheHits;
        return it->second;
    }
    
    ++cacheMisses;
    PathD nfp = computeNFP(pieceA, pieceB);
    nfpCache[key] = nfp;
    return nfp;
}

PathD NFPManager::rectangleToPath(const Rectangle2D& rect) {
    PathD path;
    path.push_back(PointD(RectangleUtils::getX(rect), RectangleUtils::getY(rect)));
    path.push_back(PointD(RectangleUtils::getMaxX(rect), RectangleUtils::getY(rect)));
    path.push_back(PointD(RectangleUtils::getMaxX(rect), RectangleUtils::getMaxY(rect)));
    path.push_back(PointD(RectangleUtils::getX(rect), RectangleUtils::getMaxY(rect)));
    return path;
}

PathD NFPManager::computeIFP(const MArea& piece, const Rectangle2D& containerBounds) {
    // Inner-Fit Polygon: the region where the piece's reference point can be
    // placed such that the entire piece fits within the container
    
    Rectangle2D pieceBounds = piece.getBoundingBox2D();
    double pieceWidth = RectangleUtils::getWidth(pieceBounds);
    double pieceHeight = RectangleUtils::getHeight(pieceBounds);
    
    // The valid area is the container shrunk by the piece dimensions
    Rectangle2D ifpRect(
        MPointDouble(RectangleUtils::getX(containerBounds), 
                    RectangleUtils::getY(containerBounds)),
        MPointDouble(RectangleUtils::getMaxX(containerBounds) - pieceWidth,
                    RectangleUtils::getMaxY(containerBounds) - pieceHeight)
    );
    
    // Ensure the IFP is not degenerate
    if (RectangleUtils::getWidth(ifpRect) <= 0 || RectangleUtils::getHeight(ifpRect) <= 0) {
        return PathD(); // Empty path indicates no valid placement
    }
    
    return rectangleToPath(ifpRect);
}

// ===== CLIPPER2 POLYGON OPERATIONS FOR FREE SPACE DETECTION =====

MArea NFPManager::bufferPolygon(const MArea& area, double bufferDistance) {
#ifdef HAVE_CLIPPER2
    try {
        PathD inputPath = areaToPath(area);
        if (inputPath.empty()) {
            return area; // Return original if conversion fails
        }
        
        PathsD result = InflatePaths({inputPath}, bufferDistance, JoinType::Round, EndType::Polygon);
        
        if (result.empty()) {
            // Return empty area if buffer operation results in nothing
            std::vector<MPointDouble> emptyPoints;
            return MArea(emptyPoints, area.getID());
        }
        
        // Return the largest resulting polygon
        auto maxPath = std::max_element(result.begin(), result.end(),
            [](const PathD& a, const PathD& b) {
                return std::abs(Area(a)) < std::abs(Area(b));
            });
        
        return pathToArea(*maxPath, area.getID());
        
    } catch (const std::exception& e) {
        std::cerr << "Clipper2 buffer operation failed: " << e.what() << std::endl;
        return area; // Return original on error
    }
#else
    // Fallback: Return original area
    std::cerr << "Warning: bufferPolygon called without Clipper2 support" << std::endl;
    return area;
#endif
}

MArea NFPManager::unionPolygons(const MArea& areaA, const MArea& areaB) {
#ifdef HAVE_CLIPPER2
    try {
        PathD pathA = areaToPath(areaA);
        PathD pathB = areaToPath(areaB);
        
        if (pathA.empty() || pathB.empty()) {
            return areaA.getArea() > areaB.getArea() ? areaA : areaB;
        }
        
        PathsD result = Union({pathA}, {pathB}, FillRule::NonZero);
        
        if (result.empty()) {
            return areaA.getArea() > areaB.getArea() ? areaA : areaB;
        }
        
        // Return the largest resulting polygon
        auto maxPath = std::max_element(result.begin(), result.end(),
            [](const PathD& a, const PathD& b) {
                return std::abs(Area(a)) < std::abs(Area(b));
            });
        
        return pathToArea(*maxPath, areaA.getID());
        
    } catch (const std::exception& e) {
        std::cerr << "Clipper2 union operation failed: " << e.what() << std::endl;
        return areaA.getArea() > areaB.getArea() ? areaA : areaB;
    }
#else
    // Fallback: Return the larger area
    return areaA.getArea() > areaB.getArea() ? areaA : areaB;
#endif
}

MArea NFPManager::differencePolygons(const MArea& areaA, const MArea& areaB) {
#ifdef HAVE_CLIPPER2
    try {
        PathD pathA = areaToPath(areaA);
        PathD pathB = areaToPath(areaB);
        
        if (pathA.empty()) {
            std::vector<MPointDouble> emptyPoints;
            return MArea(emptyPoints, areaA.getID());
        }
        
        if (pathB.empty()) {
            return areaA; // No subtraction needed
        }
        
        PathsD result = Difference({pathA}, {pathB}, FillRule::NonZero);
        
        if (result.empty()) {
            std::vector<MPointDouble> emptyPoints;
            return MArea(emptyPoints, areaA.getID());
        }
        
        // Return the largest resulting polygon
        auto maxPath = std::max_element(result.begin(), result.end(),
            [](const PathD& a, const PathD& b) {
                return std::abs(Area(a)) < std::abs(Area(b));
            });
        
        return pathToArea(*maxPath, areaA.getID());
        
    } catch (const std::exception& e) {
        std::cerr << "Clipper2 difference operation failed: " << e.what() << std::endl;
        return areaA;
    }
#else
    // Fallback: Return original area
    return areaA;
#endif
}

std::vector<MArea> NFPManager::extractConnectedComponents(const MArea& complexArea) {
#ifdef HAVE_CLIPPER2
    try {
        PathD inputPath = areaToPath(complexArea);
        if (inputPath.empty()) {
            return {complexArea};
        }
        
        // For now, treat the input as a single component
        // TODO: Implement proper connected component extraction using Clipper2
        // This would involve decomposing complex polygons with holes into simple polygons
        return {complexArea};
        
    } catch (const std::exception& e) {
        std::cerr << "Connected component extraction failed: " << e.what() << std::endl;
        return {complexArea};
    }
#else
    // Fallback: Return as single component
    return {complexArea};
#endif
}