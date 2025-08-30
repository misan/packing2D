#include "MArea.h"
#include <boost/geometry/algorithms/transform.hpp>
#include <boost/geometry/strategies/transform/matrix_transformers.hpp>
#include <boost/geometry/algorithms/envelope.hpp>
#include <boost/geometry/algorithms/within.hpp>
#include <boost/geometry/algorithms/intersects.hpp>
#include <boost/geometry/algorithms/is_empty.hpp>
#include <boost/geometry/algorithms/union.hpp>
#include <boost/geometry/algorithms/difference.hpp>
#include <boost/geometry/algorithms/intersection.hpp>
#include <boost/math/constants/constants.hpp>

namespace bg = boost::geometry;
namespace bgt = boost::geometry::strategy::transform;

MArea::MArea() : id(0), rotation(0.0), area(0.0) {}

MArea::MArea(const std::vector<MPointDouble>& points, int id) : id(id), rotation(0.0) {
    if (points.empty()) {
        area = 0.0;
        return;
    }
    Polygon poly;
    bg::assign_points(poly, points);
    bg::correct(poly); // Ensure correct winding order
    shape.push_back(poly);
    updateArea();
}

MArea::MArea(const Polygon& p, int id) : id(id), rotation(0.0) {
    Polygon poly = p;
    bg::correct(poly);
    shape.push_back(poly);
    updateArea();
}

MArea::MArea(const MArea& outer, const MArea& inner) : id(outer.id), rotation(0.0) {
    bg::difference(outer.shape, inner.shape, this->shape);
    updateArea();
}

int MArea::getID() const { return id; }

double MArea::getArea() const { return area; }

double MArea::getRotation() const { return rotation; }

void MArea::updateArea() {
    this->area = bg::area(shape);
}

Rectangle2D MArea::getBoundingBox2D() const {
    Rectangle2D box;
    if (!isEmpty()) {
        bg::envelope(shape, box);
    }
    return box;
}

double MArea::getFreeArea() const {
    if (isEmpty()) {
        return 0.0;
    }
    Rectangle2D bbox = getBoundingBox2D();
    double bboxArea = RectangleUtils::getWidth(bbox) * RectangleUtils::getHeight(bbox);
    return bboxArea - this->area;
}

size_t MArea::getVertexCount() const {
    size_t count = 0;
    for (const auto& poly : shape) {
        count += bg::num_points(poly);
    }
    return count;
}

void MArea::add(const MArea& other) {
    if (other.isEmpty()) return;
    if (this->isEmpty()) {
        this->shape = other.shape;
    } else {
        MultiPolygon result;
        bg::union_(this->shape, other.shape, result);
        this->shape = result;
    }
    updateArea();
}

void MArea::subtract(const MArea& other) {
    if (other.isEmpty() || this->isEmpty()) return;
    MultiPolygon result;
    bg::difference(this->shape, other.shape, result);
    this->shape = result;
    updateArea();
}

void MArea::intersect(const MArea& other) {
    if (this->isEmpty() || other.isEmpty()) {
        this->shape.clear();
    } else {
        MultiPolygon result;
        bg::intersection(this->shape, other.shape, result);
        this->shape = result;
    }
    updateArea();
}

bool MArea::intersection(const MArea& other) const {
    if (this->isEmpty() || other.isEmpty()) {
        return false;
    }
    return bg::intersects(this->shape, other.shape);
}

bool MArea::isEmpty() const {
    return bg::is_empty(shape);
}

bool MArea::isInside(const Rectangle2D& rect) const {
    if (isEmpty()) {
        return true;
    }
    return RectangleUtils::contains(rect, getBoundingBox2D());
}

void MArea::move(const MVector& vector) {
    if (isEmpty()) return;
    bgt::translate_transformer<double, 2, 2> translate(vector.getX(), vector.getY());
    MultiPolygon result;
    bg::transform(shape, result, translate);
    shape = result;
}

void MArea::rotate(double degrees) {
    if (isEmpty()) return;

    this->rotation += degrees;
    while (this->rotation >= 360.0) this->rotation -= 360.0;
    while (this->rotation < 0.0) this->rotation += 360.0;

    Rectangle2D bbox = getBoundingBox2D();
    MPointDouble center(
        RectangleUtils::getX(bbox) + RectangleUtils::getWidth(bbox) / 2.0,
        RectangleUtils::getY(bbox) + RectangleUtils::getHeight(bbox) / 2.0
    );

    // Create a transformation that rotates around the center of the bounding box.
    // This is done by translating to the origin, rotating, and translating back.
    bgt::translate_transformer<double, 2, 2> to_origin(-center.x(), -center.y());
    bgt::translate_transformer<double, 2, 2> from_origin(center.x(), center.y());

    // Boost's rotate_transformer uses radians.
    // We need to include the header for pi.
    double radians = degrees * boost::math::constants::pi<double>() / 180.0;
    bgt::rotate_transformer<bg::radian, double, 2, 2> rotate(radians);

    // Apply transformations sequentially, using separate output variables
    // to avoid potential corruption from in-place transforms.
    MultiPolygon translated_shape;
    bg::transform(shape, translated_shape, to_origin);
    MultiPolygon rotated_shape;
    bg::transform(translated_shape, rotated_shape, rotate);
    bg::transform(rotated_shape, shape, from_origin);
}

void MArea::placeInPosition(double x, double y) {
    if (isEmpty()) return;

    Rectangle2D bbox = getBoundingBox2D();
    double currentX = RectangleUtils::getX(bbox);
    double currentY = RectangleUtils::getY(bbox);

    MVector translation_vector(x - currentX, y - currentY);
    move(translation_vector);
}

std::vector<MPointDouble> MArea::getOuterVertices() const {
    std::vector<MPointDouble> vertices;
    
    if (isEmpty() || shape.empty()) {
        return vertices;
    }
    
    // Get the first (and typically only) polygon from the multi-polygon
    const auto& polygon = shape[0];
    const auto& outerRing = polygon.outer();
    
    vertices.reserve(outerRing.size());
    for (size_t i = 0; i < outerRing.size(); ++i) {
        const auto& point = outerRing[i];
        vertices.emplace_back(point.x(), point.y());
        
        // Boost geometry often duplicates the first vertex at the end
        // Skip the last vertex if it's the same as the first
        if (i == outerRing.size() - 1 && outerRing.size() > 1) {
            const auto& firstPoint = outerRing[0];
            if (std::abs(point.x() - firstPoint.x()) < 1e-9 && 
                std::abs(point.y() - firstPoint.y()) < 1e-9) {
                vertices.pop_back(); // Remove duplicate closing vertex
            }
        }
    }
    
    return vertices;
}

const MultiPolygon& MArea::getShape() const {
    return shape;
}

bool MArea::ByArea::operator()(const MArea& a, const MArea& b) const {
    // This implements a "less than" comparison, suitable for std::sort.
    // The original Java code sorts ascending and then iterates backwards
    // to process largest pieces first. This mimics that behavior.
    return a.getArea() < b.getArea();
}
