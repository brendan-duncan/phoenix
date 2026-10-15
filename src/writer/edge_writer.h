#pragma once

#include "../data/edge.h"

#include <string>
#include <vector>

namespace fla {

/// Encodes edge geometry back into the FLA edge-data string, the inverse of
/// PathParser.
///
/// Coordinates are stored in twips (1/20 pixel) with 1/256 twip precision.
/// Whole twips are written in decimal, which is both shorter and what FLA
/// files themselves carry; anything finer uses the hex fixed-point form (`#1608.53`).
///
/// Free of Qt, so it can be tested without the GUI.
class EdgeWriter
{
public:
    /// Formats a single coordinate. The input is in pixels, as stored on Point;
    /// the output is the twip-based text the format uses.
    static std::string formatCoordinate(double pixels);

    /// Formats a MorphShape coordinate, which the format writes as twips
    /// in hex with a point before the last two digits.
    static std::string formatMorphCoordinate(double pixels);

    /// Serializes every path on \a edge into one `edges` attribute value.
    ///
    /// This regenerates the string from the parsed segments. Callers holding an
    /// unmodified edge should prefer Edge::data, which is the text the file was
    /// read with and therefore round-trips exactly.
    static std::string writeEdge(const Edge& edge);

    /// The \a edge as FLA files store it: every cubic replaced by quadratics
    /// close to it, since other apps read only quadratics from `edges`, with
    /// each cubic itself added to \a cubics as the text of a `cubics`
    /// attribute, which the FLA keeps for editing the curve again.
    static std::string writeEdgeAsAnimate(const Edge& edge, std::vector<std::string>& cubics);

    /// Appends one path's commands to \a out; with \a cubics, as
    /// writeEdgeAsAnimate does.
    static void writePath(const Path& path, std::string& out, std::vector<std::string>* cubics = nullptr);

private:
    static void appendPoint(const Point& point, std::string& out);
};

} // namespace fla
