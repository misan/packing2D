# Python Bindings for the 2D Packing Library

This document outlines how to use the Python bindings for the C++ 2D packing library. The bindings are created using `pybind11` and allow you to interface with the core C++ library directly from Python.

## 1. Building the Python Module

The Python module is defined in `CMakeLists.txt` and built alongside the main C++ executable. To build it, follow the standard CMake build process:

```bash
# From the project root directory
mkdir build
cd build
cmake ..
cmake --build .
```

This will produce a Python module file in the `build/` directory. On Windows, this file will be named `packing_py.cp<version>-<arch>.pyd`, and on Linux/macOS, it will be `packing_py.so`.

## 2. Using the Module in Python

To use the module, you must ensure it is in Python's path. The simplest way to do this for local development is to append the `build/` directory to `sys.path`.

### Core Workflow

The typical workflow involves three main steps:
1.  **Load the data**: Use the `load_pieces` function to load piece geometry and bin dimensions from a text file.
2.  **Run the packing algorithm**: Pass the loaded data to the `pack` function.
3.  **Process the results**: Inspect the returned list of bins, which contain the placed pieces and their final positions.

### Example Usage

The following example demonstrates the complete process, from loading data to inspecting the output. For better readability, it imports the necessary classes and functions directly from the module.

```python
import sys
from packing_py import load_pieces, pack

# Add the build directory to the path
sys.path.append('./build')

# 1. Load pieces and bin dimensions from a file
# The file format is the same as the one used by the C++ executable.
problem = load_pieces("samples/S266.txt")

if not problem:
    print("Failed to load the specified file.")
    sys.exit(1)

print(f"Successfully loaded {len(problem.pieces)} pieces.")
print(f"Bin dimensions are: {problem.bin_dimension.width} x {problem.bin_dimension.height}")

# 2. Run the packing algorithm
# The function takes the list of pieces and the bin dimension.
# It returns a list of Bin objects, each populated with placed pieces.
print("\nStarting the packing process...")
bins = pack(problem.pieces, problem.bin_dimension)
print(f"Packing complete. The algorithm used {len(bins)} bins.")

# 3. Inspect the results
for i, bin_instance in enumerate(bins):
    print(f"\n--- Bin {i + 1} ---")
    print(f"  Number of pieces placed: {bin_instance.n_placed}")
    print(f"  Occupied area: {bin_instance.occupied_area:.2f}")
    
    # Iterate through the pieces placed in this bin
    for piece in bin_instance.placed_pieces:
        bbox = piece.get_bounding_box()
        print(f"    - Piece ID: {piece.get_id()}")
        print(f"      Rotation: {piece.get_rotation()}°")
        print(f"      Position (x, y): ({bbox.x:.2f}, {bbox.y:.2f})")

```

## 3. API Reference

The Python module exposes the following classes and functions:

### Functions

-   `load_pieces(file_name: str) -> LoadResult | None`
    -   Loads piece geometry and bin dimensions from a text file.
    -   **Parameters**:
        -   `file_name`: Path to the input file.
    -   **Returns**: A `LoadResult` object on success, or `None` if the file cannot be loaded.

-   `pack(pieces: list[MArea], bin_dimension: Rectangle, use_parallel: bool = False) -> list[Bin]`
    -   Runs the main packing algorithm.
    -   **Parameters**:
        -   `pieces`: A list of `MArea` objects to be packed.
        -   `bin_dimension`: A `Rectangle` object representing the bin's dimensions.
        -   `use_parallel`: If `True`, uses the C++17 parallel algorithm for sorting. Defaults to `False`.
    -   **Returns**: A list of `Bin` objects containing the placed pieces.

### Classes

#### `LoadResult`
A container for the data loaded from a file.
-   **`bin_dimension`** (`Rectangle`): The dimensions of the bin.
-   **`pieces`** (`list[MArea]`): A list of the pieces to be packed.

#### `MPointDouble`
Represents a 2D point with `x` and `y` coordinates.
-   **`x`** (`float`): The x-coordinate.
-   **`y`** (`float`): The y-coordinate.

#### `Rectangle`
Represents an axis-aligned rectangle.
-   **`x`** (`float`, read-only): The x-coordinate of the min corner.
-   **`y`** (`float`, read-only): The y-coordinate of the min corner.
-   **`width`** (`float`, read-only): The width of the rectangle.
-   **`height`** (`float`, read-only): The height of the rectangle.

#### `MArea`
Represents a single geometric piece.
-   **`get_id() -> int`**: Returns the unique ID of the piece.
-   **`get_area() -> float`**: Returns the geometric area of the piece.
-   **`get_rotation() -> float`**: Returns the final rotation of the piece in degrees.
-   **`get_bounding_box() -> Rectangle`**: Returns the axis-aligned bounding box of the piece in its final position.

#### `Bin`
Represents a single bin containing placed pieces.
-   **`placed_pieces`** (`list[MArea]`, read-only): A list of `MArea` objects placed in this bin.
-   **`n_placed`** (`int`, read-only): The number of pieces in the bin.
-   **`occupied_area`** (`float`, read-only): The total area of all pieces in the bin.
-   **`dimension`** (`Rectangle`, read-only): The dimensions of the bin.
