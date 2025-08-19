import packing_py
import sys

# Add the build directory to the path so Python can find the module
# This is a simple way for testing; for distribution, you'd install the module.
sys.path.append('./build')

print("--- Testing Python Bindings ---")

# 1. Load the problem from the same file format
problem = packing_py.load_pieces("samples/S24.txt")

if not problem:
    print("Failed to load file.")
else:
    print(f"Loaded {len(problem.pieces)} pieces from Python.")
    print(f"Bin dimensions: {problem.bin_dimension.width}x{problem.bin_dimension.height}")

    # 2. Run the packing algorithm
    print("\nStarting packing via Python...")
    bins = packing_py.pack(problem.pieces, problem.bin_dimension)
    print(f"Packing finished. Used {len(bins)} bins.")

    # 3. Inspect the results
    for i, bin_instance in enumerate(bins):
        print(f"\n--- Bin {i+1} ---")
        print(f"  Pieces placed: {bin_instance.n_placed}")
        print(f"  Occupied area: {bin_instance.occupied_area:.2f}")
        for piece in bin_instance.placed_pieces:
            bbox = piece.get_bounding_box()
            print(f"    - Piece ID: {piece.get_id()}, Rotation: {piece.get_rotation()}°, Position: ({bbox.x:.2f}, {bbox.y:.2f})")
