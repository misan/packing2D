#pragma once

#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include <vector>
#include <optional>
#include <mutex>
#include <boost/geometry/index/rtree.hpp>

// Forward declaration for the test fixture to be declared as a friend.
class BinTest;


/**
 * @brief Class used to describe a Bin object.
 * Equivalent to org.packing.core.Bin.
 */
class Bin {
public:
    friend class BinTest; // Grant access to the test fixture
    /**
     * @brief Initializes this bin with the specified dimensions.
     * @param dimension The rectangle defining the bin's boundaries.
     */
    Bin(const Rectangle2D& dimension);
    Bin(const Bin& other);
    Bin& operator=(const Bin& other);

    /**
     * @brief Get the placed pieces.
     */
    const std::vector<MArea>& getPlacedPieces() const;

    /**
     * @brief Get the number of pieces placed.
     */
    size_t getNPlaced() const;

    /**
     * @brief Computes the occupied area as the sum of areas of the placed pieces.
     */
    double getOccupiedArea() const;

    /**
     * @brief Get the dimensions.
     */
    const Rectangle2D& getDimension() const;

    /**
     * @brief Computes the empty area as the total area minus the occupied area.
     */
    double getEmptyArea() const;

    /**
     * @brief Places pieces inside the bin using the maximal rectangles strategy.
     * This is the C++ version of the `boundingBoxPacking` method from the original Java code.
     * @param piecesToPlace A list of pieces to try and place in the bin. The list will be sorted.
     * @return A vector of pieces that could not be placed.
     */
    std::vector<MArea> boundingBoxPacking(std::vector<MArea>& piecesToPlace, bool useParallel);

    /**
     * @brief Places pieces inside the bin using look-ahead and backtracking for better utilization.
     * This enhanced version evaluates multiple placement orders for the next few pieces.
     * @param piecesToPlace A list of pieces to try and place in the bin. The list will be sorted.
     * @param useParallel If true, the packing will be done in parallel.
     * @param lookAheadDepth Number of pieces to consider for look-ahead (default: 5)
     * @return A vector of pieces that could not be placed.
     */
    std::vector<MArea> boundingBoxPackingWithLookAhead(std::vector<MArea>& piecesToPlace, bool useParallel, int lookAheadDepth = 5);

    /**
     * @brief Compress all placed pieces in this bin towards the lower-left corner.
     */
    void compress(bool useParallel);

    /**
     * @brief Drops pieces from the top of the bin to find a valid, non-occupied position.
     * @param piecesToDrop A list of pieces to try and place in the bin.
     * @param useParallel If true, the packing will be done in parallel.
     * @return A vector of pieces that could not be placed.
     */
    std::vector<MArea> dropPieces(const std::vector<MArea>& piecesToDrop, bool useParallel);

    /**
     * @brief Places the largest possible pending parts into free zones using global free space exploration.
     * @param piecesToPlace A list of pieces to try and place in the bin.
     * @param useParallel If true, the packing will be done in parallel.
     * @return A vector of pieces that could not be placed.
     */
    std::vector<MArea> placeInFreeZones(std::vector<MArea>& piecesToPlace, bool useParallel);

    /**
     * @brief Places pieces in free zones using look-ahead and backtracking for better utilization.
     * This enhanced version evaluates multiple placement orders for the next few pieces.
     * @param piecesToPlace A list of pieces to try and place in the bin. The list will be sorted.
     * @param useParallel If true, the packing will be done in parallel.
     * @param lookAheadDepth Number of pieces to consider for look-ahead (default: 5)
     * @return A vector of pieces that could not be placed.
     */
    std::vector<MArea> placeInFreeZonesWithLookAhead(std::vector<MArea>& piecesToPlace, bool useParallel, int lookAheadDepth = 5);

    /**
     * @brief Recomputes all free rectangles from scratch for the entire bin.
     * This is necessary when pieces have been moved through compression or other operations.
     */
    void recomputeAllFreeRectangles();

    /**
     * @brief Tries to place already placed pieces inside other placed pieces.
     * This is the C++ version of the `moveAndReplace` method from the original Java code.
     * @param indexLimit The starting index (from the end) to check for pieces to move.
     * @return True if any piece was moved, false otherwise.
     */
    bool moveAndReplace(size_t indexLimit);

    /**
     * @brief Represents a potential placement for a piece, telling the caller
     * which free rectangle to use and what rotation is needed.
     */
    struct Placement {
        int rectIndex = -1;         // Index in freeRectangles, -1 if no fit found.
        int rotationAngle = 0;      // Rotation angle in degrees (0, 10, 20, ..., 350)
    };

    /**
     * @brief Finds the best free rectangle to place a piece in based on the minimal-wastage heuristic.
     * @param piece The piece to place.
     * @param useParallel If true, the packing will be done in parallel.
     * @return A Placement struct with details of the best position.
     */
    Placement findWhereToPlace(const MArea& piece, bool useParallel);

    /**
     * @brief Verifies that no collisions exist between placed pieces.
     * @return True if no collisions found, false otherwise.
     */
    bool verifyNoCollisions() const;

private:
    // Type definitions for the R-tree.
    // It will store pairs of <Rectangle2D, size_t>, where size_t is the index
    // of the piece in the placedPieces vector.
    using RTreeValue = std::pair<Rectangle2D, size_t>;
    using RTree = boost::geometry::index::rtree<RTreeValue, boost::geometry::index::rstar<16>>;

    Rectangle2D dimension;
    std::vector<MArea> placedPieces;
    std::vector<Rectangle2D> freeRectangles;
    RTree placedPiecesRTree; // The new spatial index
    std::mutex compressionMutex; // Mutex for compression operations

    /**
     * @brief Checks if a given piece collides with any of the already placed pieces.
     * This is the core of the R-tree optimization.
     * @param piece The piece to check.
     * @param ignoredPieceIndex An optional index of a piece to ignore during the check (e.g., the piece itself if it's being moved).
     * @return True if there is a collision, false otherwise.
     */
    bool isCollision(const MArea& piece, std::optional<size_t> ignoredPieceIndex = std::nullopt);

    /**
     * @brief Divides the rectangular space where a piece was just placed.
     * @param usedFreeArea Rectangular area that contains the newly placed piece.
     * @param justPlacedPieceBB Bounding box of the newly placed piece.
     */
    void splitScheme(const Rectangle2D& usedFreeArea, const Rectangle2D& justPlacedPieceBB);

    /**
     * @brief Recalculates the free rectangular boxes in the bin after a piece has been placed.
     * @param justPlacedPieceBB Bounding box of the piece that was just added.
     */
    void computeFreeRectangles(const Rectangle2D& justPlacedPieceBB);

    /**
     * @brief Eliminates all non-maximal boxes from the empty spaces in the bin.
     */
    void eliminateNonMaximal();

    /**
     * @brief Helper to compress a single piece towards a direction, avoiding collisions.
     * @param pieceIndex The index of the piece to move in the placedPieces vector.
     * @param vector The direction of compression (e.g., (-1, -1) for bottom-left).
     * @return True if the piece was moved, false otherwise.
     */
    bool compressPiece(size_t pieceIndex, const MVector& vector);

    /**
     * @brief Helper for parallel compression. Does not modify shared state.
     * @param piece The piece to move.
     * @param pieceIndex The index of the piece.
     * @return True if the piece was moved, false otherwise.
     */
    bool compressPiece_parallel_helper(MArea& piece, size_t pieceIndex);

    /**
     * @brief Moves a piece downwards from the top of the bin, sliding horizontally to find a starting slot.
     * @param toDive The piece to drop.
     * @param useParallel If true, the search for a slot will be parallelized.
     * @return An optional containing the placed piece if successful, otherwise std::nullopt.
     */
    std::optional<MArea> dive(MArea toDive, bool useParallel);

    /**
     * @brief Sweeps a piece along the interior of a container searching for a non-overlapping position.
     * @param container The piece that contains the free space.
     * @param inside The piece to place inside the container (passed by value as it's modified).
     * @param ignoredPieceIndex The index of the piece being moved, to avoid self-collision checks.
     * @return An optional containing the placed piece if successful, otherwise std::nullopt.
     */
    std::optional<MArea> sweep(const MArea& container, MArea inside, size_t ignoredPieceIndex);

    /**
     * @brief Saves the current state of the bin for backtracking.
     * @return A state object that can be used to restore the bin.
     */
    struct BinState {
        std::vector<MArea> placedPieces;
        std::vector<Rectangle2D> freeRectangles;
        RTree placedPiecesRTree;
    };
    
    BinState saveState() const;
    
    /**
     * @brief Restores the bin to a previously saved state.
     * @param state The state to restore.
     */
    void restoreState(const BinState& state);

    /**
     * @brief Evaluates the quality of the current bin configuration.
     * @return A score representing the bin utilization (higher is better).
     */
    double evaluateBinUtilization() const;

    /**
     * @brief Tries to place a single piece and returns the resulting state.
     * @param piece The piece to place.
     * @param useParallel If true, use parallel processing.
     * @return The placement result and new state if successful.
     */
    struct PlacementResult {
        bool success;
        MArea placedPiece;
        BinState newState;
    };
    
    PlacementResult tryPlacePiece(const MArea& piece, bool useParallel);
};

namespace TestUtils {
    void disableParallelismForTests(bool disable);
}
