#pragma once

#include <array>

/**
 * @brief Constants variables used along the application.
 * Equivalent to org.packing.core.Constants.
 */
namespace Constants {

/**
 * @brief Division factor when trying different positions to drop the piece along
 * the X axes. Increasing this factor will produce smaller horizontal steps.
 */
constexpr int DIVE_HORIZONTAL_DISPLACEMENT_FACTOR = 3;

/**
 * @brief Division factor for the displacement of pieces.
 * Increasing this factor will produce smaller horizontal steps.
 */
constexpr int DX_SWEEP_FACTOR = 10;

/**
 * @brief Division factor for the displacement of pieces.
 * Increasing this factor will produce smaller vertical steps.
 */
constexpr int DY_SWEEP_FACTOR = 2;

/**
 * @brief The vector of angles to try when placing a piece.
 */
constexpr std::array<int, 8> ROTATION_ANGLES = {0, 45, 90, 135, 180, 225, 270, 315};

} // namespace Constants