#pragma once

#include "MPointDouble.h"
#include <boost/geometry/geometries/box.hpp>

/**
 * @brief Represents a 2D rectangle with double precision.
 * Equivalent to java.awt.geom.Rectangle2D.Double.
 */
using Rectangle2D = bg::model::box<MPointDouble>;

/**
 * @brief Helper functions to mimic java.awt.geom.Rectangle2D.Double API.
 */
namespace RectangleUtils {
    inline double getX(const Rectangle2D& r) { return r.min_corner().x(); }
    inline double getY(const Rectangle2D& r) { return r.min_corner().y(); }
    inline double getWidth(const Rectangle2D& r) { return r.max_corner().x() - r.min_corner().x(); }
    inline double getHeight(const Rectangle2D& r) { return r.max_corner().y() - r.min_corner().y(); }
    inline double getMaxX(const Rectangle2D& r) { return r.max_corner().x(); }
    inline double getMaxY(const Rectangle2D& r) { return r.max_corner().y(); }

    inline bool intersects(const Rectangle2D& r1, const Rectangle2D& r2) {
        return bg::intersects(r1, r2);
    }

    inline bool contains(const Rectangle2D& container, const Rectangle2D& contained) {
        return bg::within(contained, container);
    }

    inline Rectangle2D createIntersection(const Rectangle2D& r1, const Rectangle2D& r2) {
        Rectangle2D intersection;
        bg::intersection(r1, r2, intersection);
        return intersection;
    }

    /**
     * @brief Takes two rectangles and check if the first fits into the second.
     */
    inline bool fits(const Rectangle2D& o1, const Rectangle2D& o2) {
        return (getHeight(o1) <= getHeight(o2) && getWidth(o1) <= getWidth(o2));
    }

    /**
     * @brief Takes two rectangles and check if the rotation of the first (90°) fits into the other.
     */
    inline bool fitsRotated(const Rectangle2D& o1, const Rectangle2D& o2) {
        return (getHeight(o1) <= getWidth(o2) && getWidth(o1) <= getHeight(o2));
    }
}