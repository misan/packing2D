# Technical Deep Dive: 2D Packing Algorithms

**Author:** Gemini
**Date:** July 26, 2025
**Audience:** Software Developers

## 1. Overview

This document provides a detailed explanation of the algorithms used in the 2D Packing Library. It is intended for developers who need to understand, maintain, or extend the core packing logic. The strategy is a multi-stage process centered around the **Maximal Rectangles** heuristic, enhanced with several post-processing and optimization steps.

The entire process is orchestrated by the `BinPacking::pack` function, which manages a list of bins and the pieces that still need to be placed. The core logic for placing pieces within a single bin resides in the `Bin` class.

**Source Files:**
-   `src/core/BinPacking.cpp`: High-level strategy.
-   `src/core/Bin.cpp`: Low-level implementation of placement, space management, and optimization.
-   `src/core/Bin.h`: Interface for the `Bin` class.

---

## 2. High-Level Packing Strategy (`BinPacking::pack`)

The `pack` function orchestrates the entire process by iterating through pieces and bins.

1.  **Initial Sort**: All pieces to be packed are first sorted in descending order by their area. This is a common heuristic that prioritizes placing the largest, most awkward pieces first, which tends to leave more manageable, smaller spaces for the remaining pieces.

2.  **Bin Iteration**: The algorithm proceeds in a loop, processing one bin at a time until all pieces are placed.
    ```cpp
    while (!toPlace.empty()) {
        // 1. Create a new, empty bin
        bins.emplace_back(binDimension);
        Bin& currentBin = bins.back();

        // 2. Run the multi-stage packing process on this bin
        stillNotPlaced = runMultiStagePacking(currentBin, toPlace);

        // 3. Update the list of pieces to place for the next bin
        toPlace = stillNotPlaced;
    }
    ```

3.  **Multi-Stage Bin Packing**: For each bin, a complex, multi-stage process is invoked to fill it as densely as possible. This is the heart of the algorithm and is detailed below.

---

## 3. The Maximal Rectangles Algorithm

The core of the placement logic is the Maximal Rectangles method. Instead of tracking complex polygonal free areas, the bin maintains a list of non-overlapping, maximal empty rectangles (`freeRectangles`). A rectangle is "maximal" if it cannot be expanded in any direction without overlapping a placed piece.

### 3.1. Stage 1: Initial Bounding Box Packing (`Bin::boundingBoxPacking`)

This is the main loop for placing pieces into a bin.

1.  **Find a Candidate Position (`Bin::findWhereToPlace`)**:
    -   For a given piece, the algorithm iterates through all current `freeRectangles`.
    -   It checks if the piece's **bounding box** fits into the free rectangle, both in its original orientation and rotated by 90 degrees.
    -   **Heuristic**: It selects the placement that minimizes "wastage." Wastage is defined as the smaller of the remaining width or height in the free rectangle (`min(free.w - piece.w, free.h - piece.h)`). This heuristic prefers to leave more "squarish" and usable leftover spaces.
    -   The function returns a `Placement` struct containing the index of the best `freeRectangles` and whether a rotation is needed.

2.  **Place the Piece**:
    -   If a valid placement is found, a copy of the piece is created, rotated if necessary, and translated to the bottom-left corner of the chosen free rectangle.
    -   A crucial **collision check** (`!placedPiece.intersection(totalPlacedArea)`) ensures the actual geometry of the piece (not just its bounding box) doesn't overlap with any other piece already in the bin.

3.  **Update Free Space (`Bin::computeFreeRectangles`)**:
    -   This is the most critical step. The algorithm iterates through the `freeRectangles` list.
    -   If a free rectangle does not intersect the newly placed piece's bounding box, it is kept as is.
    -   If it *does* intersect, it is "shattered." The original free rectangle is removed and replaced by up to four new, smaller rectangles that represent the remaining space to the top, bottom, left, and right of the intersection area.

    *ASCII Art Example of `computeFreeRectangles`*:
    ```
    Before:                  After placing Piece P:
    +-------------------+    +-------------------+
    |                   |    |       Top         |
    |                   |    +---------+---------+
    |    Free Rect      |    |  Left   | P       |
    |                   |    +---------+---------+
    |                   |    |      Bottom       |
    +-------------------+    +-------------------+
    ```
    *(Note: The actual implementation is more general and handles all intersection cases)*

4.  **Prune Non-Maximal Rectangles (`Bin::eliminateNonMaximal`)**:
    -   The `computeFreeRectangles` step can create redundant rectangles (i.e., one free rectangle that is fully contained within another).
    -   This function cleans up the list by iterating through all pairs of rectangles and removing any rectangle that is completely contained inside another. This keeps the list of free spaces minimal and efficient.

### 3.2. Stage 2: Iterative Improvement (`moveAndReplace`)

After the initial packing, the layout can often be improved. `moveAndReplace` is a powerful optimization that tries to fit smaller pieces into the empty internal areas (holes) of larger pieces.

1.  **Iteration**: It iterates backwards through the placed pieces (from smallest to largest).
2.  **Target**: For each `currentArea` (a smaller piece), it iterates through all the larger pieces (`container`) already placed.
3.  **Check Feasibility**: It checks if the `container` piece has enough "free area" (the area of its bounding box minus its actual geometric area) to potentially hold `currentArea`.
4.  **Sweep Search (`Bin::sweep`)**:
    -   If feasible, it initiates a "sweep" search. A copy of `currentArea` is placed at the bottom-left of the `container`'s bounding box.
    -   It then systematically moves (`sweeps`) the piece horizontally and vertically in small increments (`dx`, `dy`) within the container's bounding box.
    -   At each step, it performs a rigorous check: the piece must not intersect the `container`'s geometry, must not intersect any *other* piece in the bin, and must remain within the bin's boundaries.
    -   If a valid position is found, the sweep is successful.
5.  **Update State**: If the sweep succeeds, the old bounding box of the moved piece is added back to the `freeRectangles` list, and the piece is updated to its new position. The free space is then re-calculated.

### 3.3. Stage 3: Compaction and Final Pass

1.  **Gravity (`Bin::compress`)**:
    -   This function simulates gravity by pulling all pieces towards the bottom-left corner (`-1, -1`).
    -   It iterates through each placed piece, temporarily removing it from the collision set.
    -   It then moves the piece one unit at a time, first vertically then horizontally, until it collides with another piece or the bin wall.
    -   This process is repeated until no piece can be moved further, resulting in a more compact layout.

2.  **Final Drop (`Bin::dropPieces`)**:
    -   After compaction, new gaps might have opened up. The algorithm attempts one final pass with any remaining unplaced pieces.
    -   The `dropPieces` function uses a "dive" strategy. It takes a piece, places it at the top of the bin, and slides it horizontally, looking for an open "slot."
    -   Once a slot is found where the piece doesn't collide with anything, it's dropped vertically (using the `compress` logic with a `(0, -1)` vector) until it rests on another piece or the bin floor.

## 4. Summary of the Multi-Stage Process in `BinPacking::pack`

For each new bin, the sequence is:

1.  **`boundingBoxPacking`**: Run the initial Maximal Rectangles placement for all remaining pieces.
2.  **`while(true)` loop**:
    a. **`moveAndReplace`**: Attempt to repack pieces internally to create larger free areas.
    b. **`boundingBoxPacking`**: Try to fit more pieces from the unplaced list into any new gaps.
    c. **Break**: If a full pass of (a) and (b) results in no new pieces being placed and no existing pieces being moved, the bin's layout is considered stable, and the loop terminates.
3.  **`compress`**: Run the gravity simulation to compact the final layout.
4.  **`dropPieces`**: Make one last attempt to drop any remaining pieces from the top.
5.  **`compress`**: A final compaction pass to settle any newly dropped pieces.

This comprehensive, multi-stage approach ensures a high-quality packing result by combining a fast initial placement heuristic with powerful post-processing and optimization steps.
