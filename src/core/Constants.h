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
 * @brief The vector of angles to try when placing a piece in Stage 1.
 * Using 0, 90, 180, and 270 degrees for initial placement.
 */
constexpr std::array<int, 4> STAGE1_ROTATION_ANGLES = {0, 90, 180, 270};

/**
 * @brief The vector of angles to try when placing a piece in Stages 2 and 3.
 * Using 5-degree increments for better fitting opportunities.
 */
constexpr std::array<int, 72> STAGE23_ROTATION_ANGLES = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90,
    95, 100, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 165, 170, 175, 180,
    185, 190, 195, 200, 205, 210, 215, 220, 225, 230, 235, 240, 245, 250, 255, 260, 265, 270, 275,
    280, 285, 290, 295, 300, 305, 310, 315, 320, 325, 330, 335, 340, 345, 350, 355
};

} // namespace Constants