#pragma once

#include "../data/edge.h"

#include <string>

namespace fla {

/// Encodes edge geometry back into Flash's edge-data string, the inverse of
/// PathParser.
///
/// Coordinates are stored in twips (1/20 pixel) with 1/256 twip precision.
/// Whole twips are written in decimal, which is both shorter and what Flash
/// itself emits; anything finer uses the hex fixed-point form (`#1608.53`).
///
/// Free of Qt, so it can be tested without the GUI.
class EdgeWriter
{
public:
    /// Formats a single coordinate. The input is in pixels, as stored on Point;
    /// the output is the twip-based text the format uses.
    static std::string formatCoordinate(double pixels);

    /// Serializes every path on \a edge into one `edges` attribute value.
    ///
    /// This regenerates the string from the parsed segments. Callers holding an
    /// unmodified edge should prefer Edge::data, which is the text the file was
    /// read with and therefore round-trips exactly.
    static std::string writeEdge(const Edge& edge);

    /// Appends one path's commands to \a out.
    static void writePath(const Path& path, std::string& out);

private:
    static void appendPoint(const Point& point, std::string& out);
};

} // namespace fla
