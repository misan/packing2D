cmake_minimum_required(VERSION 3.14)
project(Packing2D_CPP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++ -D_LIBCPP_ENABLE_PARALLEL_ALGORITHMS -march=native -mtune=native -O3")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native")
endif()

# --- Dependencies ---
# Find Boost, which is required for geometric operations.
# On Debian/Ubuntu: sudo apt-get install libboost-all-dev
# On macOS (with Homebrew): brew install boost
# Use modern "Config" mode, which is more reliable and what CMake recommends.
find_package(Boost 1.71.0 CONFIG REQUIRED)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found. Please install Boost.")
endif()

# Add TBB for C++17 parallel algorithms
# On Debian/Ubuntu: sudo apt-get install libtbb-dev
find_package(TBB REQUIRED)

# Add local Clipper2 for No-Fit Polygon (NFP) operations
set(CLIPPER2_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/clipper2")
if(EXISTS "${CLIPPER2_LOCAL_DIR}/Clipper2Lib/include/clipper2/clipper.h")
    message(STATUS "Found local Clipper2 at: ${CLIPPER2_LOCAL_DIR}")
    set(CLIPPER2_FOUND TRUE)
    
    # Build Clipper2 as static library
    add_library(clipper2_local STATIC
        ${CLIPPER2_LOCAL_DIR}/Clipper2Lib/src/clipper.engine.cpp
        ${CLIPPER2_LOCAL_DIR}/Clipper2Lib/src/clipper.offset.cpp
        ${CLIPPER2_LOCAL_DIR}/Clipper2Lib/src/clipper.rectclip.cpp
    )
    
    target_include_directories(clipper2_local PUBLIC
        ${CLIPPER2_LOCAL_DIR}/Clipper2Lib/include
    )
    
    set(CLIPPER2_INCLUDE_DIRS ${CLIPPER2_LOCAL_DIR}/Clipper2Lib/include)
    set(CLIPPER2_LIBRARIES clipper2_local)
    
    add_definitions(-DHAVE_CLIPPER2)
    message(STATUS "Clipper2 NFP functionality enabled")
else()
    message(WARNING "Local Clipper2 not found at ${CLIPPER2_LOCAL_DIR}. Using fallback NFP implementation.")
    set(CLIPPER2_FOUND FALSE)
    set(CLIPPER2_INCLUDE_DIRS "")
    set(CLIPPER2_LIBRARIES "")
endif()

# --- Library ---
# This creates the static library `packing_lib` containing the core logic.
add_library(packing_lib STATIC
    src/primitives/MArea.cpp
    src/primitives/MVector.cpp
    src/utils/Utils.cpp
    src/core/Bin.cpp
    src/core/BinPacking.cpp
    src/core/NFPManager.cpp
)
set_target_properties(packing_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(packing_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src # Add our own src dir to includes
    ${CLIPPER2_INCLUDE_DIRS}
)
# Link to the Boost::headers target. This is the modern way to add include paths.
target_link_libraries(packing_lib PUBLIC Boost::headers TBB::tbb ${CLIPPER2_LIBRARIES})

# --- Executable ---
# This creates the main executable to run the packing process from the command line.
add_executable(packing_main src/main.cpp)
target_link_libraries(packing_main PRIVATE packing_lib TBB::tbb)

# --- NFP Demo Executable ---
add_executable(nfp_demo demo_nfp_performance.cpp)
target_link_libraries(nfp_demo PRIVATE packing_lib TBB::tbb)

# --- NFP Integration Test Executable ---
add_executable(nfp_integration_test test_nfp_integration.cpp)
target_link_libraries(nfp_integration_test PRIVATE packing_lib TBB::tbb)

# --- Rotation Angles Test Executable ---
add_executable(rotation_test build/test_rotation_angles.cpp)
target_link_libraries(rotation_test PRIVATE packing_lib TBB::tbb)


# --- Python Bindings (Future Work) ---
# To create Python bindings using pybind11, we configure the following section.
# It requires pybind11 to be installed (e.g., via `pip install pybind11`).
# Use the modern FindPython module to avoid deprecation warnings.
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

# This creates the Python module `packing_py.so` (or .pyd on Windows)
pybind11_add_module(packing_py src/python_bindings.cpp) # All C++ source should be in src/

# Link the Python module against our static library
target_link_libraries(packing_py PRIVATE packing_lib)

message(STATUS "Project configured. Build with 'cmake --build .'")

# --- Unit Testing ---
# To run tests:
# 1. Install Google Test (see prerequisites in comments)
# 2. Configure with CMake: `cmake ..`
# 3. Build: `cmake --build .`
# 4. Run tests from build directory: `ctest` or `./packing_tests`
enable_testing()

# Use modern "Config" mode for GTest as well.
find_package(GTest CONFIG REQUIRED)

add_executable(packing_tests
    tests/test_marea.cpp
    tests/test_bin.cpp
    tests/test_nfp.cpp)
target_link_libraries(packing_tests PRIVATE packing_lib GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(packing_tests)
