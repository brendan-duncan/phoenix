#pragma once

#include "../data/point.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class Shape;

/// Vector artwork from other applications, read into drawing objects:
/// SVG files, and Illustrator files through the PDF page they carry. Each
/// filled or stroked path becomes a shape of its own, in drawing order.
/// Free of Qt.
namespace vectors {

/// One piece of a path: a move, a line or a cubic curve to a point.
struct Segment
{
    enum class Kind
    {
        Move,
        Line,
        Cubic
    };
    Kind kind = Kind::Line;
    /// For a cubic, the two controls and the end; else the end.
    std::vector<Point> points;
};

struct Subpath
{
    std::vector<Segment> segments;
    bool closed = false;
};

using Color = std::array<uint8_t, 4>;

/// A path as painted: its subpaths, in stage pixels, and what paints it.
struct Painted
{
    std::vector<Subpath> subpaths;
    std::optional<Color> fill;
    std::optional<Color> stroke;
    double strokeWidth = 1.0;
};

/// The artwork read, and the size of the page or canvas it was on.
struct Artwork
{
    std::vector<Painted> paths;
    double width = 0.0;
    double height = 0.0;
    /// What could not be read, in words, for the person importing.
    std::vector<std::string> notes;
};

/// An SVG document's paths: path, rect, circle, ellipse, line, polyline
/// and polygon, in groups with transforms, with fill and stroke from
/// attributes or style.
Artwork readSvg(const std::string& text);

/// An Illustrator file's artwork, from the PDF page it carries (or a PDF):
/// the page's paths with their fill and stroke colors. False notes when
/// the file has no PDF page (an Illustrator 8 or older file).
Artwork readPdf(const std::vector<uint8_t>& bytes);

/// A drawing object for a path: its fill on the inside of each closed
/// subpath (holes by nesting), its stroke along every edge. Null for a
/// path with nothing to paint.
Shape* toShape(const Painted& painted);

/// Arcs as SVG gives them, from \a from to \a to, as cubic curves.
std::vector<Segment> arcToCubics(const Point& from, double rx, double ry, double rotation, bool large, bool sweep,
    const Point& to);

} // namespace vectors

} // namespace fla
