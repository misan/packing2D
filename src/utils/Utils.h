#pragma once

#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include <string>
#include <vector>
#include <optional>

namespace Utils {

/**
 * @brief Structure to hold the results of loading a problem file.
 */
struct LoadResult {
    Rectangle2D binDimension;
    std::vector<MArea> pieces;
};

/**
 * @brief Loads pieces from a file according to the specified format.
 *
 * @param fileName The path to the input file.
 * @return An optional LoadResult. Returns std::nullopt if the file cannot be opened or is malformed.
 */
std::optional<LoadResult> loadPieces(const std::string& fileName);

} // namespace Utils