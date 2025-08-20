#include "Utils.h"
#include "primitives/MPointDouble.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <vector>

namespace Utils {

// Helper to split a string by a delimiter
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::optional<LoadResult> loadPieces(const std::string& fileName) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << fileName << std::endl;
        return std::nullopt;
    }

    LoadResult result;
    double binWidth, binHeight;
    if (!(file >> binWidth >> binHeight)) {
        std::cerr << "Error: Could not read bin dimensions." << std::endl;
        return std::nullopt;
    }
    result.binDimension = Rectangle2D(MPointDouble(0, 0), MPointDouble(binWidth, binHeight));

    int numPieces;
    if (!(file >> numPieces)) {
        std::cerr << "Error: Could not read number of pieces." << std::endl;
        return std::nullopt;
    }

    std::string line;
    std::getline(file, line); // Consume the rest of the line after numPieces

    int pieceCount = 0;
    while (pieceCount < numPieces && std::getline(file, line)) {
        if (line.empty() || line.find_first_not_of(" \t\n\v\f\r") == std::string::npos) continue;

        std::stringstream lineStream(line);
        std::string firstToken;
        lineStream >> firstToken;

        if (firstToken == "@") { // This line defines a hole
            if (result.pieces.empty()) {
                std::cerr << "Error: Hole definition '@' found before any piece was defined." << std::endl;
                return std::nullopt;
            }

            std::vector<MPointDouble> holePoints;
            std::string pointStr;
            while (lineStream >> pointStr) {
                auto coords = split(pointStr, ',');
                if (coords.size() != 2) {
                    std::cerr << "Error: Malformed point string '" << pointStr << "'" << std::endl;
                    return std::nullopt;
                }
                holePoints.emplace_back(std::stod(coords[0]), std::stod(coords[1]));
            }

            MArea& outerPiece = result.pieces.back();
            MArea innerHole(holePoints, -1); // ID for hole is not important
            
            MArea pieceWithHole(outerPiece, innerHole);
            pieceWithHole.placeInPosition(0, 0);
            result.pieces.back() = pieceWithHole;

        } else { // This line defines a new piece
            std::set<MPointDouble, MPointDoubleCompare> uniquePoints;
            std::vector<MPointDouble> points;
            
            lineStream.clear();
            lineStream.seekg(0);

            std::string pointStr;
            while (lineStream >> pointStr) {
                auto coords = split(pointStr, ',');
                if (coords.size() != 2) {
                    std::cerr << "Error: Malformed point string '" << pointStr << "'" << std::endl;
                    return std::nullopt;
                }
                MPointDouble p(std::stod(coords[0]), std::stod(coords[1]));
                if (uniquePoints.find(p) == uniquePoints.end()) {
                    uniquePoints.insert(p);
                    points.push_back(p);
                }
            }
            
            if (!points.empty()) {
                result.pieces.emplace_back(points, pieceCount + 1);
                pieceCount++;
            }
        }
    }

    if (pieceCount != numPieces) {
        std::cerr << "Warning: Expected " << numPieces << " pieces, but found " << pieceCount << "." << std::endl;
    }

    return result;
}

} // namespace Utils