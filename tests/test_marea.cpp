#include <gtest/gtest.h>
#include "primitives/MArea.h"
#include "primitives/MPointDouble.h"
#include "primitives/MVector.h"
#include "primitives/Rectangle.h"

// Helper function to create a square MArea for testing.
MArea createSquare(double x, double y, double side, int id) {
    std::vector<MPointDouble> points = {
        {x, y}, {x + side, y}, {x + side, y + side}, {x, y + side}
    };
    return MArea(points, id);
}

TEST(MAreaTest, ConstructionAndProperties) {
    MArea square = createSquare(0, 0, 10, 1);
    ASSERT_EQ(square.getID(), 1);
    ASSERT_FALSE(square.isEmpty());
    // Use ASSERT_NEAR for floating-point comparisons to handle potential precision errors.
    ASSERT_NEAR(square.getArea(), 100.0, 1e-9);
}

TEST(MAreaTest, PieceWithHole) {
    MArea outer = createSquare(0, 0, 10, 2); // Area = 100
    MArea inner = createSquare(2, 2, 4, -1); // Area = 16

    MArea pieceWithHole(outer, inner);

    ASSERT_EQ(pieceWithHole.getID(), 2);
    ASSERT_NEAR(pieceWithHole.getArea(), 100.0 - 16.0, 1e-9);

    // Check that the bounding box is still based on the outer shape.
    Rectangle2D bbox = pieceWithHole.getBoundingBox2D();
    ASSERT_NEAR(RectangleUtils::getX(bbox), 0.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getY(bbox), 0.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getWidth(bbox), 10.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getHeight(bbox), 10.0, 1e-9);
}

TEST(MAreaTest, Transformations) {
    std::vector<MPointDouble> points = {{0,0}, {20,0}, {20,10}, {0,10}};
    MArea rect = MArea(points, 3);

    // Test placeInPosition
    rect.placeInPosition(50, 60);
    Rectangle2D bbox = rect.getBoundingBox2D();
    ASSERT_NEAR(RectangleUtils::getX(bbox), 50.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getY(bbox), 60.0, 1e-9);

    // Test move
    rect.move(MVector(5, 10));
    bbox = rect.getBoundingBox2D();
    ASSERT_NEAR(RectangleUtils::getX(bbox), 55.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getY(bbox), 70.0, 1e-9);

    // Test rotate
    rect.rotate(90);
    bbox = rect.getBoundingBox2D();
    ASSERT_NEAR(rect.getRotation(), 90.0, 1e-9);
    // After a 90-degree rotation, width and height of the bounding box should swap.
    ASSERT_NEAR(RectangleUtils::getWidth(bbox), 10.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getHeight(bbox), 20.0, 1e-9);
}

TEST(MAreaTest, Intersection) {
    MArea piece1 = createSquare(0, 0, 10, 4);
    MArea piece2 = createSquare(5, 5, 10, 5);     // Overlapping
    MArea piece3 = createSquare(20, 20, 10, 6);   // Not overlapping

    ASSERT_TRUE(piece1.intersection(piece2));
    ASSERT_FALSE(piece1.intersection(piece3));
    ASSERT_FALSE(piece2.intersection(piece3));
}