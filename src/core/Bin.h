#pragma once

#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include "NFPManager.h"
#include <vector>
#include <optional>
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
     * @param useNFP Enable NFP-based collision detection (default: false for backward compatibility).
     */
    Bin(const Rectangle2D& dimension, bool useNFP = false);
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
    std::vector<MArea> boundingBoxPacking(std::vector<MArea>& piecesToPlace);

    /**
     * @brief Compress all placed pieces in this bin towards the lower-left corner.
     */
    void compress();

    /**
     * @brief Drops pieces from the top of the bin to find a valid, non-occupied position.
     * @param piecesToDrop A list of pieces to try and place in the bin.
     * @return A vector of pieces that could not be placed.
     */
    std::vector<MArea> dropPieces(const std::vector<MArea>& piecesToDrop);

    /**
     * @brief Tries to place already placed pieces inside other placed pieces.
     * This is the C++ version of the `moveAndReplace` method from the original Java code.
     * @param indexLimit The starting index (from the end) to check for pieces to move.
     * @return True if any piece was moved, false otherwise.
     */
    bool moveAndReplace(size_t indexLimit);

    /**
     * @brief Represents a potential placement for a piece, telling the caller
     * which free rectangle to use and if a rotation is needed.
     */
    struct Placement {
        int rectIndex = -1;         // Index in freeRectangles, -1 if no fit found.
        bool requiresRotation = false; // Whether the piece needs to be rotated 90 degrees.
    };

    /**
     * @brief Finds the best free rectangle to place a piece in based on the minimal-wastage heuristic.
     * @param piece The piece to place.
     * @return A Placement struct with details of the best position.
     */
    Placement findWhereToPlace(const MArea& piece);

    /**
     * @brief Add a piece directly to the bin (for testing purposes).
     * @param piece The piece to add.
     */
    void addPieceForTesting(const MArea& piece);

    /**
     * @brief Public interface to test collision detection methods.
     * @param piece The piece to check.
     * @return True if there is a collision, false otherwise.
     */
    bool testCollision(const MArea& piece) { return isCollision(piece); }

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
    NFPManager nfpManager; // NFP-based collision detection and placement
    bool useNFPCollisionDetection; // Flag to enable NFP-based collision detection

    /**
     * @brief Checks if a given piece collides with any of the already placed pieces.
     * This is the core of the R-tree optimization.
     * @param piece The piece to check.
     * @param ignoredPieceIndex An optional index of a piece to ignore during the check (e.g., the piece itself if it's being moved).
     * @return True if there is a collision, false otherwise.
     */
    bool isCollision(const MArea& piece, std::optional<size_t> ignoredPieceIndex = std::nullopt);

    /**
     * @brief NFP-based collision checking - more efficient replacement for isCollision.
     * @param piece The piece to check at its current position.
     * @param ignoredPieceIndex An optional index of a piece to ignore during the check.
     * @return True if there is a collision, false otherwise.
     */
    bool isCollisionNFP(const MArea& piece, std::optional<size_t> ignoredPieceIndex = std::nullopt);

    /**
     * @brief NFP-based placement validation for a piece at a specific position.
     * @param piece The piece to place.
     * @param position The position to test.
     * @param ignoredPieceIndex An optional index of a piece to ignore during the check.
     * @return True if placement is valid (no collision), false otherwise.
     */
    bool isValidPlacementNFP(const MArea& piece, const PointD& position, std::optional<size_t> ignoredPieceIndex = std::nullopt);

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
     * @brief Moves a piece downwards from the top of the bin, sliding horizontally to find a starting slot.
     * @param toDive The piece to drop.
     * @return An optional containing the placed piece if successful, otherwise std::nullopt.
     */
    std::optional<MArea> dive(MArea toDive);

    /**
     * @brief Sweeps a piece along the interior of a container searching for a non-overlapping position.
     * @param container The piece that contains the free space.
     * @param inside The piece to place inside the container (passed by value as it's modified).
     * @param ignoredPieceIndex The index of the piece being moved, to avoid self-collision checks.
     * @return An optional containing the placed piece if successful, otherwise std::nullopt.
     */
    std::optional<MArea> sweep(const MArea& container, MArea inside, size_t ignoredPieceIndex);

public:
    /**
     * @brief Represents a sophisticated free space island with principal component analysis.
     */
    struct FreeSpaceIsland {
        MArea shape;                // The geometric shape of the free region
        PointD centroid;            // True centroid of the polygon
        double majorAxisLength;     // Length along principal axis (max extent)
        double minorAxisLength;     // Length along minor axis (min extent) 
        double principalAngle;      // Angle of the major axis (in degrees)
        double robustness;          // Thickness = minorAxisLength
        double area;                // Area of the region
        double aspectRatio;         // majorAxisLength / minorAxisLength
        
        FreeSpaceIsland(const MArea& s) : shape(s), area(s.getArea()) {
            computePrincipalAxes();
        }
        
    private:
        void computePrincipalAxes();
    };

    /**
     * @brief Detects sophisticated free space islands using adaptive negative buffering.
     * This enables Stage2-parts placement in gaps between Stage1-parts.
     * @return Vector of free space islands available for placement.
     */
    std::vector<FreeSpaceIsland> detectAdaptiveFreeSpaceIslands();

    /**
     * @brief Represents optimal placement info for global free space placement.
     */
    struct GlobalPlacement {
        size_t regionIndex;           // Index in the freeRegions vector
        PointD position;              // Position to place the piece
        double rotationAngle;         // Rotation angle to apply
        double wastedArea;            // Estimated wasted area after placement
    };

    /**
     * @brief Advanced Stage2-parts placement using global free space detection.
     * Replaces the limited moveAndReplace approach with global optimization.
     * @param piecesToPlace Remaining pieces to be placed (Stage2-parts).
     * @param extendedRotations If true, use more rotation angles for better fitting.
     * @return Vector of pieces that could not be placed.
     */
    std::vector<MArea> placeInGlobalFreeSpace(std::vector<MArea>& piecesToPlace, bool extendedRotations = false);

    /**
     * @brief Finds the best free space island and position for a given piece using PCA-based placement.
     * @param piece The piece to place.
     * @param islands Available free space islands.
     * @param extendedRotations Whether to try extended rotation angles.
     * @return Optional containing placement info if successful.
     */
    std::optional<GlobalPlacement> findBestIslandPlacement(
        const MArea& piece, 
        const std::vector<FreeSpaceIsland>& islands,
        bool extendedRotations = false
    );

private:
    /**
     * @brief Computes the union of all placed pieces as a single complex polygon.
     * @return MArea representing the occupied space in the bin.
     */
    MArea computePlacedPiecesUnion();

    /**
     * @brief Decomposes a complex free region into simpler placeable areas.
     * @param complexRegion The complex free region to decompose.
     * @return Vector of simpler regions suitable for piece placement.
     */
    std::vector<MArea> decomposeFreeRegion(const MArea& complexRegion);
};

