#pragma once

#include "Bin.h"
#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include <vector>

namespace BinPacking {

/**
 * @brief Main strategy for the 2D bin packing problem.
 * Equivalent to org.packing.core.BinPacking.BinPackingStrategy.
 *
 * @param pieces The pieces to be packed. The vector may be modified (sorted).
 * @param binDimension The dimensions of the bins to use.
 * @return A vector of Bins, each containing the pieces placed within it.
 */
std::vector<Bin> pack(std::vector<MArea>& pieces, const Rectangle2D& binDimension, bool useParallel);

} // namespace BinPacking

