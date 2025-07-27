# Presentation: 2D Packing Library

This presentation provides a high-level overview of the 2D Packing Library, its architecture, and its capabilities.

---

## Slide 1: Title Slide

**2D Irregular Shape Packing Library**

A high-performance C++ library with Python bindings for solving the 2D bin packing problem.

---

## Slide 2: The Problem

**What is 2D Bin Packing?**

- **Goal**: Fit a collection of 2D shapes (pieces) into the smallest possible number of containers (bins).
- **Challenge**: The shapes can be irregular (non-rectangular), making optimal placement computationally difficult (NP-hard).
- **Applications**:
  - Manufacturing (cutting materials like sheet metal, wood, glass)
  - Logistics (loading pallets, shipping containers)
  - Fashion (nesting patterns on fabric)

---

## Slide 3: Our Solution: Key Features

- **High-Performance Core**: Written in modern C++ (17) for speed and efficiency.
- **Irregular Shapes**: Packs any 2D polygon, including concave shapes and those with holes.
- **Python Friendly**: `pybind11` provides a simple, powerful Python API.
- **Cross-Platform**: CMake build system supports Linux, macOS, and Windows.
- **Proven Algorithm**: Implements the well-regarded "Maximal Rectangles" heuristic.
- **Extensible**: Designed with a modular structure for future improvements.

---

## Slide 4: Architecture Overview

A layered architecture separates concerns and promotes code reuse.

- **C++ Core Library (`packing_lib`)**:
  - **Primitives**: `MArea`, `Rectangle2D` (based on Boost.Geometry)
  - **Core Logic**: `Bin`, `BinPacking`
  - **Utilities**: File I/O
- **Interfaces**:
  - **C++ Executable (`packing_main`)**: For command-line usage.
  - **Python Module (`packing_py`)**: For scripting and integration.
- **Testing**:
  - **`packing_tests`**: Unit tests using the Google Test framework.

---

## Slide 5: The "Maximal Rectangles" Algorithm

How does it work?

1.  **Track Empty Space**: The available area in a bin is represented as a list of the largest possible rectangles.
2.  **Select a Piece**: Prioritize larger pieces first.
3.  **Find a Home**: Find the free rectangle where the piece fits best (e.g., leaving the least wasted space). Consider rotating the piece.
4.  **Place and Split**: Place the piece. The free rectangle it occupied is now split into smaller leftover rectangles.
5.  **Prune**: Clean up the list of free rectangles to remove any that are now contained within another.
6.  **Repeat**: Continue until all pieces are placed.

---

## Slide 6: Visualizing the Algorithm

![Maximal Rectangles Visualization](https://i.imgur.com/R2JcCIa.png)

1.  Initial empty bin (one large free rectangle).
2.  Place Piece A. The free space is split.
3.  Place Piece B. A different free rectangle is used and split.
4.  The process continues, intelligently filling the space.

---

## Slide 7: Post-Processing for Better Results

After the initial packing, we run optimization passes:

- **`compress()`**:
  - Simulates "gravity," pulling all pieces towards the bottom-left corner.
  - This closes unnecessary gaps left by the placement algorithm.
- **`moveAndReplace()`**:
  - A clever trick: it tries to fit smaller, already-placed pieces inside the empty spaces or "holes" of larger pieces.
  - This significantly improves material utilization.

---

## Slide 8: How to Use It (Python Example)

The Python API makes the library accessible.

```python
import packing_py

# 1. Load pieces from a file
# This returns the pieces and the bin dimensions
load_result = packing_py.load_pieces("samples/Shapes0.txt")
pieces = load_result.pieces
bin_dim = load_result.bin_dimension

# 2. Run the packing algorithm
# The C++ core does the heavy lifting
bins = packing_py.pack(pieces, bin_dim)

# 3. Use the results
print(f"Packed {len(pieces)} pieces into {len(bins)} bins.")

for i, bin_obj in enumerate(bins):
    print(f"Bin {i+1}:")
    for piece in bin_obj.placed_pieces:
        bbox = piece.get_bounding_box()
        print(f"  - Piece {piece.get_id()} at ({bbox.x}, {bbox.y})")
```

---

## Slide 9: Building and Testing

- **Dependencies**: CMake, Boost, Google Test, pybind11
- **Simple Build Process**:
  ```bash
  # Configure
  cmake -B build

  # Build
  cmake --build build

  # Test
  cd build && ctest
  ```
- **Outputs**:
  - `libpacking_lib.a` (Static Library)
  - `packing_main` (Executable)
  - `packing_py.so` (Python Module)

---

## Slide 10: Conclusion & Next Steps

- **Summary**: We have a powerful, efficient, and easy-to-use 2D packing library.
- **Future Work**:
  - Performance profiling and optimization.
  - Exploring alternative placement heuristics (e.g., Best-Fit, Skyline).
  - Adding support for more complex constraints (e.g., piece orientation).
  - GUI for visualization.

**Thank you!**

**Questions?**
