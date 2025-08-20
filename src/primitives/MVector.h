#pragma once

/**
 * @brief A class that represents a geometric vector v=(x,y) in double precision.
 * Equivalent to org.packing.primitives.MVector.
 */
class MVector {
public:
    MVector(double x, double y);

    double getX() const;
    double getY() const;

    /**
     * @brief Takes this vector and changes the signs of its coordinates.
     * @return a new MVector with its coordinates inverted.
     */
    MVector inverse() const;

private:
    double x;
    double y;
};