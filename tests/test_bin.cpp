#include <gtest/gtest.h>
#include "core/Bin.h"
#include "primitives/MArea.h"
#include "primitives/MPointDouble.h"
#include "primitives/Rectangle.h"
#include <memory>

// Helper functions to create simple MArea objects for testing.
namespace {
    MArea createSquare(double x, double y, double side, int id) {
        std::vector<MPointDouble> points = {
            {x, y}, {x + side, y}, {x + side, y + side}, {x, y + side}
        };
        return MArea(points, id);
    }

    MArea createRect(double x, double y, double w, double h, int id) {
        std::vector<MPointDouble> points = {
            {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}
        };
        return MArea(points, id);
    }
}

class BinTest : public ::testing::Test {
protected:
    Rectangle2D binDimension;
    std::unique_ptr<Bin> testBin;

    void SetUp() override {
        binDimension = Rectangle2D(MPointDouble(0, 0), MPointDouble(100, 100));
        testBin = std::make_unique<Bin>(binDimension);
    }
};

TEST_F(BinTest, Construction) {
    ASSERT_EQ(testBin->getNPlaced(), 0);
    ASSERT_NEAR(testBin->getOccupiedArea(), 0.0, 1e-9);
    ASSERT_EQ(RectangleUtils::getWidth(testBin->getDimension()), 100);
    ASSERT_EQ(RectangleUtils::getHeight(testBin->getDimension()), 100);
}

TEST_F(BinTest, BoundingBoxPacking_SinglePiece) {
    std::vector<MArea> pieces = { createSquare(0, 0, 20, 1) };
    std::vector<MArea> unplaced = testBin->boundingBoxPacking(pieces);

    ASSERT_EQ(testBin->getNPlaced(), 1);
    ASSERT_TRUE(unplaced.empty());
    const auto& placed = testBin->getPlacedPieces()[0];
    ASSERT_EQ(placed.getID(), 1);
    
    Rectangle2D bbox = placed.getBoundingBox2D();
    // Should be placed at the bottom-left corner
    ASSERT_NEAR(RectangleUtils::getX(bbox), 0.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getY(bbox), 0.0, 1e-9);
}

TEST_F(BinTest, BoundingBoxPacking_PieceTooLarge) {
    std::vector<MArea> pieces = { createSquare(0, 0, 120, 1) };
    std::vector<MArea> unplaced = testBin->boundingBoxPacking(pieces);

    ASSERT_EQ(testBin->getNPlaced(), 0);
    ASSERT_EQ(unplaced.size(), 1);
    ASSERT_EQ(unplaced[0].getID(), 1);
}

TEST_F(BinTest, BoundingBoxPacking_MultiplePieces) {
    std::vector<MArea> pieces = {
        createSquare(0, 0, 30, 1),
        createSquare(0, 0, 30, 2)
    };
    std::vector<MArea> unplaced = testBin->boundingBoxPacking(pieces);

    ASSERT_EQ(testBin->getNPlaced(), 2);
    ASSERT_TRUE(unplaced.empty());
}

TEST_F(BinTest, Compress) {
    // Place a piece manually away from the origin to test compression.
    // We use dropPieces to get it into the bin, which places it at (50, 0).
    MArea piece = createSquare(0, 0, 20, 1);
    piece.placeInPosition(50, 50); // Set an initial high position
    std::vector<MArea> toDrop = { piece };
    testBin->dropPieces(toDrop);
    
    ASSERT_EQ(testBin->getNPlaced(), 1);
    Rectangle2D bbox_before = testBin->getPlacedPieces()[0].getBoundingBox2D();
    ASSERT_NEAR(RectangleUtils::getX(bbox_before), 50.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getY(bbox_before), 0.0, 1e-9);

    // Now, test the compress method, which should slide it from (50,0) to (0,0).
    testBin->compress();

    Rectangle2D bbox_after = testBin->getPlacedPieces()[0].getBoundingBox2D();
    ASSERT_NEAR(RectangleUtils::getX(bbox_after), 0.0, 1e-9);
    ASSERT_NEAR(RectangleUtils::getY(bbox_after), 0.0, 1e-9);
}

TEST_F(BinTest, DropPieces_Stacking) {
    MArea piece1 = createRect(0, 0, 20, 30, 1);
    testBin->dropPieces({piece1});
    ASSERT_EQ(testBin->getNPlaced(), 1);
    Rectangle2D bbox1 = testBin->getPlacedPieces()[0].getBoundingBox2D();
    ASSERT_NEAR(RectangleUtils::getY(bbox1), 0.0, 1e-9);

    // Now drop a second piece. It should land on top of the first.
    MArea piece2 = createRect(0, 0, 20, 30, 2);
    testBin->dropPieces({piece2});
    ASSERT_EQ(testBin->getNPlaced(), 2);
    Rectangle2D bbox2 = testBin->getPlacedPieces()[1].getBoundingBox2D();
    ASSERT_NEAR(RectangleUtils::getY(bbox2), RectangleUtils::getMaxY(bbox1), 1e-9);
}