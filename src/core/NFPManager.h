#pragma once

#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>

#ifdef HAVE_CLIPPER2
#include <clipper2/clipper.h>
using namespace Clipper2Lib;
#else
// Fallback definitions when Clipper2 is not available
struct PointD {
    double x, y;
    PointD(double x = 0, double y = 0) : x(x), y(y) {}
};
using PathD = std::vector<PointD>;
using PathsD = std::vector<PathD>;
#endif

/**
 * @brief Manages No-Fit Polygon (NFP) computations using Clipper2 for high-performance collision-free placement.
 * 
 * This class replaces the current collision detection system with NFP-based approach:
 * - Instead of asking "does A collide with B?", we ask "where can A be placed without colliding with B?"
 * - Uses Clipper2's MinkowskiSum to compute NFPs efficiently
 * - Caches NFPs for piece-type pairs to avoid recomputation
 * - Provides direct access to valid placement regions
 */
class NFPManager {
public:
    NFPManager();
    ~NFPManager();

    /**
     * @brief Convert MArea polygon to Clipper2 Path format.
     * @param area The MArea to convert.
     * @return Clipper2 Path representation.
     */
    static PathD areaToPath(const MArea& area);

    /**
     * @brief Convert Clipper2 Path back to MArea polygon.
     * @param path The Clipper2 Path to convert.
     * @param id Piece ID for the resulting MArea.
     * @return MArea representation.
     */
    static MArea pathToArea(const PathD& path, int id = 0);

    /**
     * @brief Compute No-Fit Polygon between two pieces.
     * The NFP represents all positions where the reference point of piece A
     * can be placed such that A does not overlap with B.
     * 
     * @param pieceA The piece to be placed.
     * @param pieceB The obstacle piece.
     * @return PathD representing the NFP boundary.
     */
    PathD computeNFP(const MArea& pieceA, const MArea& pieceB);

    /**
     * @brief Apply buffer (offset) operation to a polygon.
     * Positive values expand, negative values shrink the polygon.
     * 
     * @param area The polygon to buffer.
     * @param bufferDistance The buffer distance (negative for shrinking).
     * @return Buffered polygon as MArea.
     */
    static MArea bufferPolygon(const MArea& area, double bufferDistance);

    /**
     * @brief Compute union of two polygons.
     * 
     * @param areaA First polygon.
     * @param areaB Second polygon.
     * @return Union of the two polygons as MArea.
     */
    static MArea unionPolygons(const MArea& areaA, const MArea& areaB);

    /**
     * @brief Compute difference of two polygons (A - B).
     * 
     * @param areaA Base polygon.
     * @param areaB Polygon to subtract.
     * @return Difference as MArea.
     */
    static MArea differencePolygons(const MArea& areaA, const MArea& areaB);

    /**
     * @brief Extract connected components from a complex polygon.
     * 
     * @param complexArea Polygon that may contain multiple disconnected regions.
     * @return Vector of individual connected components.
     */
    static std::vector<MArea> extractConnectedComponents(const MArea& complexArea);

    /**
     * @brief Get valid placement positions for a piece given a set of obstacles.
     * This is the core replacement for collision detection.
     * 
     * @param piece The piece to place.
     * @param obstacles Vector of already placed pieces.
     * @param containerBounds The bin boundaries.
     * @return PathsD representing valid placement regions (can be multiple disjoint areas).
     */
    PathsD getValidPlacementRegions(const MArea& piece, 
                                   const std::vector<MArea>& obstacles,
                                   const Rectangle2D& containerBounds);

    /**
     * @brief Check if a specific position is valid for placing a piece.
     * This replaces the isCollision() function calls.
     * 
     * @param piece The piece to check.
     * @param position The position to test (x, y).
     * @param obstacles Vector of obstacle pieces.
     * @param containerBounds The bin boundaries.
     * @return True if position is valid (no collision), false otherwise.
     */
    bool isValidPlacement(const MArea& piece,
                         const PointD& position,
                         const std::vector<MArea>& obstacles,
                         const Rectangle2D& containerBounds);

    /**
     * @brief Find the best placement position using minimal wastage heuristic.
     * This enhances the current findWhereToPlace() functionality.
     * 
     * @param piece The piece to place.
     * @param obstacles Vector of obstacle pieces.
     * @param containerBounds The bin boundaries.
     * @return Optional point representing the best position, or nullopt if no valid position.
     */
    std::optional<PointD> findBestPlacement(const MArea& piece,
                                           const std::vector<MArea>& obstacles,
                                           const Rectangle2D& containerBounds);

    /**
     * @brief Compute optimal compression movement using MinkowskiSum offset.
     * This replaces the iterative compression algorithm.
     * 
     * @param piece The piece to compress.
     * @param obstacles Vector of obstacle pieces.
     * @param containerBounds The bin boundaries.
     * @param direction Compression direction vector.
     * @return The maximum safe movement distance.
     */
    double computeMaxCompression(const MArea& piece,
                               const std::vector<MArea>& obstacles,
                               const Rectangle2D& containerBounds,
                               const PointD& direction);

    /**
     * @brief Clear the NFP cache (useful for testing or memory management).
     */
    void clearCache();

    /**
     * @brief Get cache statistics for performance monitoring.
     */
    struct CacheStats {
        size_t hits;
        size_t misses;
        size_t totalEntries;
    };
    CacheStats getCacheStats() const;

private:
    // Cache key for NFP pairs (combines piece geometries)
    struct NFPCacheKey {
        std::string pieceAHash;
        std::string pieceBHash;
        
        bool operator==(const NFPCacheKey& other) const {
            return pieceAHash == other.pieceAHash && pieceBHash == other.pieceBHash;
        }
    };

    // Hash function for cache key
    struct NFPCacheKeyHash {
        std::size_t operator()(const NFPCacheKey& key) const {
            return std::hash<std::string>()(key.pieceAHash + key.pieceBHash);
        }
    };

    // NFP cache to avoid recomputing identical piece pairs
    std::unordered_map<NFPCacheKey, PathD, NFPCacheKeyHash> nfpCache;
    
    // Cache statistics
    mutable size_t cacheHits;
    mutable size_t cacheMisses;

    /**
     * @brief Generate a hash string for a piece geometry (for caching).
     */
    std::string generatePieceHash(const MArea& piece) const;

    /**
     * @brief Get cached NFP or compute if not found.
     */
    PathD getCachedNFP(const MArea& pieceA, const MArea& pieceB);

    /**
     * @brief Convert Rectangle2D to Clipper2 PathD.
     */
    static PathD rectangleToPath(const Rectangle2D& rect);

    /**
     * @brief Compute inner-fit polygon (IFP) for container bounds.
     */
    PathD computeIFP(const MArea& piece, const Rectangle2D& containerBounds);
};