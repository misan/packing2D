#pragma once

#include "MPointDouble.h"
#include "MVector.h"
#include "Rectangle.h"
#include <vector>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>

namespace bg = boost::geometry;

using Polygon = bg::model::polygon<MPointDouble>;
using MultiPolygon = bg::model::multi_polygon<Polygon>;

/**
 * @brief Represents a piece to be placed inside a Bin.
 * This is the C++ equivalent of org.packing.primitives.MArea, which was based on java.awt.geom.Area.
 * It uses Boost.Geometry for the underlying geometric operations.
 * An MArea can be a complex shape (a polygon with holes, or multiple disjoint polygons)
 * which is represented by a Boost.Geometry multi_polygon.
 */
class MArea {
public:
    MArea();
    MArea(const std::vector<MPointDouble>& points, int id);
    MArea(const MArea& outer, const MArea& inner); // For creating holes

    int getID() const;
    double getArea() const;
    double getRotation() const;
    Rectangle2D getBoundingBox2D() const;
    double getFreeArea() const;
    size_t getVertexCount() const;

    void add(const MArea& other);
    void subtract(const MArea& other);
    void intersect(const MArea& other);
    bool intersection(const MArea& other) const;

    bool isEmpty() const;
    bool isInside(const Rectangle2D& rect) const;

    void move(const MVector& vector);
    void rotate(double degrees);
    void placeInPosition(double x, double y);

    // Comparators
    struct ByArea {
        bool operator()(const MArea& a, const MArea& b) const;
    };

private:
    MultiPolygon shape;
    int id;
    double rotation;

    void updateArea();
    double area;
};