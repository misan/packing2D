# Technical Report: 2D Packing Library

**Author:** Gemini
**Date:** July 26, 2025

## 1. Introduction

This document provides a technical overview of the 2D Packing Library, a C++ solution for the two-dimensional irregular shape packing problem. The library's primary goal is to efficiently pack a given set of polygonal shapes (pieces) into the minimum number of rectangular containers (bins).

The library is designed to be robust, performant, and extensible. It provides a core static library, a command-line interface for direct execution, and Python bindings for easy integration into other systems.

### Key Features:
- Packs irregular 2D polygons (with holes).
- Uses a Maximal Rectangles algorithm for space management.
- Includes heuristics for piece placement and post-processing steps for compaction.
- C++ core for performance.
- Python bindings (`pybind11`) for ease of use.
- CMake-based build system for cross-platform compilation.
- Unit tests using Google Test framework.

## 2. System Architecture

The project is organized into a core library, an executable, Python bindings, and tests.

![Architecture Diagram](https://i.imgur.com/3h3M2aE.png)

### 2.1. Core Library (`packing_lib`)
This static library contains all the fundamental logic for the packing process. It is organized into three main namespaces/directories:
- `primitives`: Defines the geometric primitives used throughout the library.
  - `MPointDouble`: A 2D point with double precision.
  - `Rectangle2D`: An axis-aligned bounding box, built on `boost::geometry::model::box`.
  - `MArea`: Represents a complex 2D polygon, potentially with holes, built on `boost::geometry::model::polygon`. It handles geometry, transformations (translation, rotation), and intersection tests.
- `core`: Implements the main packing algorithm and data structures.
  - `Bin`: Represents a single container. It manages the list of placed pieces and the remaining free space.
  - `BinPacking`: The main entry point containing the `pack()` function that orchestrates the entire packing strategy.
- `utils`: Provides helper functions, primarily for loading pieces from files.

### 2.2. Executable (`packing_main`)
A command-line tool that demonstrates the library's functionality. It reads piece and bin definitions from a text file, runs the packing algorithm, and outputs the results into `Bin-X.txt` files.

### 2.3. Python Bindings (`packing_py`)
A Python module created using `pybind11`. It exposes the core C++ classes and functions (`MArea`, `Bin`, `pack`, etc.) to Python, allowing the high-performance C++ backend to be driven by Python scripts.

## 3. Algorithm and Data Structures

### 3.1. The Packing Algorithm
The primary packing strategy is implemented in `Bin::boundingBoxPacking` and orchestrated by `BinPacking::pack`. The algorithm can be summarized as follows:

1.  **Sort Pieces**: Pieces are sorted in descending order of the area of their bounding boxes. This heuristic prioritizes placing larger, more difficult pieces first.
2.  **Iterate Through Bins**: The algorithm attempts to place each piece into the first available bin that can accommodate it. If no existing bin can, a new one is created.
3.  **Maximal Rectangles**: Each `Bin` maintains a list of `freeRectangles` representing the empty space. This is the core of the Maximal Rectangles algorithm.
4.  **Piece Placement**: For each piece, the algorithm searches through all `freeRectangles` to find the best fit.
    - The placement heuristic (`findWhereToPlace`) chooses the free rectangle that results in the minimal wasted area.
    - It considers both the original orientation and a 90-degree rotation of the piece.
5.  **Splitting Free Space**: Once a piece is placed, the free rectangle it occupies is split into smaller remaining rectangles (`splitScheme`).
6.  **Pruning**: The list of free rectangles is pruned to ensure only maximal rectangles remain (`eliminateNonMaximal`), preventing a single free space from being represented by multiple overlapping smaller rectangles.
7.  **Post-processing**: After the initial packing, two optimization steps are performed:
    - `compress()`: Slides all pieces towards the bottom-left corner to compact the layout.
    - `moveAndReplace()`: Attempts to fit smaller pieces into the holes or concave regions of larger, already-placed pieces.

### 3.2. Core Data Structures
- **`MArea`**: Represents a piece as a `boost::geometry::model::polygon`. This is highly flexible, allowing for concave shapes and holes (represented as interior rings in the polygon). It provides methods for area calculation, bounding box computation, and geometric transformations.
- **`Bin`**: Manages a `std::vector<MArea>` for placed pieces and a `std::vector<Rectangle2D>` for free spaces. Its methods encapsulate the core logic of placing a piece and updating the free space representation.

## 4. API and Usage

### 4.1. C++ API
The main entry point is the `BinPacking::pack` function:
```cpp
namespace BinPacking {
    std::vector<Bin> pack(
        std::vector<MArea>& pieces,
        const Rectangle2D& binDimension
    );
}
```
- **`pieces`**: A vector of `MArea` objects to be packed. This vector is sorted in-place.
- **`binDimension`**: A `Rectangle2D` defining the size of the bins.
- **Returns**: A vector of `Bin` objects, each populated with the pieces it contains.

### 4.2. Python API
The Python bindings mirror the C++ API:
```python
import packing_py

# Load pieces from a file
load_result = packing_py.load_pieces("samples/Shapes0.txt")
if load_result:
    pieces = load_result.pieces
    bin_dimension = load_result.bin_dimension

    # Run the packing algorithm
    bins = packing_py.pack(pieces, bin_dimension)

    # Inspect the results
    print(f"Used {len(bins)} bins.")
    for i, bin_obj in enumerate(bins):
        print(f"Bin {i+1} has {bin_obj.n_placed} pieces.")
```

### 4.3. Input File Format
The `packing_main` executable and `Utils::loadPieces` function expect a specific file format:
- **Line 1**: `width height` (integer bin dimensions).
- **Line 2**: `n` (integer number of pieces).
- **Next n lines**: `x0,y0 x1,y1 ...` (space-separated coordinates for each vertex of a polygon).

## 5. Building and Testing

### 5.1. Dependencies
- **CMake** (>= 3.14)
- **Boost** (>= 1.71.0)
- **Google Test** (for tests)
- **pybind11** (for Python bindings)

### 5.2. Build Process
The project is built using standard CMake commands:
```bash
# Configure the build
cmake -B build

# Compile the code
cmake --build build

# Run tests
cd build
ctest
```

This will produce the `packing_lib` static library, the `packing_main` executable, the `packing_py` Python module, and the `packing_tests` test runner.
