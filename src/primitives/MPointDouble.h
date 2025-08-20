#pragma once

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

namespace bg = boost::geometry;

/**
 * @brief Represents a 2D point with double precision.
 * Equivalent to java.awt.geom.Point2D.Double and org.packing.primitives.MPointDouble.
 */
using MPointDouble = bg::model::d2::point_xy<double>;

/**
 * @brief Custom less-than operator for MPointDouble to use it in std::set.
 */
struct MPointDoubleCompare {
    bool operator()(const MPointDouble& a, const MPointDouble& b) const {
        if (a.x() < b.x()) return true;
        if (a.x() > b.x()) return false;
        return a.y() < b.y();
    }
};

/**
 * @brief Custom equality operator for MPointDouble.
 */
inline bool operator==(const MPointDouble& a, const MPointDouble& b) {
    return a.x() == b.x() && a.y() == b.y();
}