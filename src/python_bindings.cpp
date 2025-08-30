#include <pybind11/pybind11.h>
#include <pybind11/stl.h>       // For automatic vector/optional conversions
#include <pybind11/operators.h> // For operator overloads

#include "core/Bin.h"
#include "core/BinPacking.h"
#include "utils/Utils.h"
#include "primitives/MArea.h"
#include "primitives/Rectangle.h"
#include "primitives/MPointDouble.h"

namespace py = pybind11;

// PYBIND11_MODULE is the entry point for creating a Python module.
// The first argument is the module name (must match the target name in CMake)
// The second argument, `m`, is a py::module_ object that is the main interface.
PYBIND11_MODULE(packing_py, m) {
    m.doc() = "Python bindings for the 2D Packing Library";

    // --- Bind Primitives ---

    py::class_<MPointDouble>(m, "MPointDouble")
        .def(py::init<double, double>(), "Constructs a 2D point.", py::arg("x"), py::arg("y"))
        // The .x() and .y() methods of boost::geometry::point_xy are overloaded (const and non-const).
        // We must use lambdas to explicitly resolve the ambiguity for pybind11.
        .def_property("x",
            [](const MPointDouble &p) { return p.x(); },
            [](MPointDouble &p, double val) { p.x(val); })
        .def_property("y",
            [](const MPointDouble &p) { return p.y(); },
            [](MPointDouble &p, double val) { p.y(val); })
        .def("__repr__", [](const MPointDouble &p) {
            return "<MPointDouble (" + std::to_string(p.x()) + ", " + std::to_string(p.y()) + ")>";
        });

    py::class_<Rectangle2D>(m, "Rectangle")
        .def(py::init<>(), "Constructs an empty rectangle.")
        .def(py::init<MPointDouble, MPointDouble>(), "Constructs a rectangle from min and max corners.", py::arg("min_corner"), py::arg("max_corner"))
        .def_property_readonly("x", &RectangleUtils::getX)
        .def_property_readonly("y", &RectangleUtils::getY)
        .def_property_readonly("width", &RectangleUtils::getWidth)
        .def_property_readonly("height", &RectangleUtils::getHeight)
        .def("__repr__", [](const Rectangle2D &r) {
            return "<Rectangle x=" + std::to_string(RectangleUtils::getX(r)) +
                   " y=" + std::to_string(RectangleUtils::getY(r)) +
                   " w=" + std::to_string(RectangleUtils::getWidth(r)) +
                   " h=" + std::to_string(RectangleUtils::getHeight(r)) + ">";
        });

    py::class_<MArea>(m, "MArea")
        .def(py::init<const std::vector<MPointDouble>&, int>(), "Constructs a piece from a list of points and an ID.", py::arg("points"), py::arg("id"))
        .def("get_id", &MArea::getID, "Gets the unique ID of the piece.")
        .def("get_area", &MArea::getArea, "Gets the geometric area of the piece.")
        .def("get_rotation", &MArea::getRotation, "Gets the current rotation in degrees.")
        .def("get_bounding_box", &MArea::getBoundingBox2D, "Gets the axis-aligned bounding box.")
        .def("is_empty", &MArea::isEmpty, "Checks if the area is empty.")
        .def("__repr__", [](const MArea &a) {
            return "<MArea id=" + std::to_string(a.getID()) + " area=" + std::to_string(a.getArea()) + ">";
        });

    // --- Bind Core Classes ---

    py::class_<Bin>(m, "Bin")
        .def(py::init<const Rectangle2D&>(), "Constructs a bin with given dimensions.", py::arg("dimension"))
        .def_property_readonly("placed_pieces", &Bin::getPlacedPieces, "Returns a list of pieces placed in the bin.", py::return_value_policy::reference_internal)
        .def_property_readonly("n_placed", &Bin::getNPlaced, "Returns the number of pieces placed.")
        .def_property_readonly("occupied_area", &Bin::getOccupiedArea, "Returns the total area occupied by pieces.")
        .def_property_readonly("dimension", &Bin::getDimension, "Returns the bin's dimensions.", py::return_value_policy::reference_internal)
        .def("__repr__", [](const Bin &b) {
            return "<Bin n_placed=" + std::to_string(b.getNPlaced()) + " occupied_area=" + std::to_string(b.getOccupiedArea()) + ">";
        });

    // --- Bind Utility Functions and Structs ---

    py::class_<Utils::LoadResult>(m, "LoadResult")
        .def_readonly("bin_dimension", &Utils::LoadResult::binDimension)
        .def_readonly("pieces", &Utils::LoadResult::pieces)
        .def("__repr__", [](const Utils::LoadResult &res) {
            return "<LoadResult pieces=" + std::to_string(res.pieces.size()) + ">";
        });

    // pybind11 automatically handles the std::optional, converting std::nullopt to None.
    m.def("load_pieces", &Utils::loadPieces, "Loads pieces and bin dimensions from a file.", py::arg("file_name"));

    // --- Bind Main Packing Algorithm ---

    // The pack function takes a vector by non-const reference because it sorts it internally.
    // pybind11 will automatically convert a Python list of MArea objects to a std::vector<MArea>.
    m.def("pack", &BinPacking::pack, "Main packing algorithm. Takes a list of pieces and bin dimensions, returns a list of bins.",
          py::arg("pieces"), py::arg("bin_dimension"));
}