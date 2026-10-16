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

/// A placement: x' = a x + c y + tx, y' = b x + d y + ty.
struct Placement
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;
};

/// A gradient fill, as FLA files keep one: its stops, and the placement of the
/// gradient's square (x and y from -819.2 to 819.2, the color running along
/// x for a linear one and out from the middle for a radial one) on the
/// stage.
struct Gradient
{
    struct Stop
    {
        double offset = 0.0;
        Color color{0, 0, 0, 255};
    };
    bool radial = false;
    std::vector<Stop> stops;
    Placement placement;
    /// Past its ends: the end colors (Pad), mirrored (Reflect) or again
    /// (Repeat).
    enum class Spread
    {
        Pad,
        Reflect,
        Repeat
    };
    Spread spread = Spread::Pad;
    /// A radial gradient's focal point, along its x axis: -1 to 1, 0 at
    /// the middle.
    double focal = 0.0;
};

/// A path as painted: its subpaths, in stage pixels, and what paints it.
struct Painted
{
    std::vector<Subpath> subpaths;
    std::optional<Color> fill;
    /// A gradient fill, over \a fill, which stands for it where a gradient
    /// cannot go.
    std::optional<Gradient> gradient;
    std::optional<Color> stroke;
    double strokeWidth = 1.0;
    /// The clipping paths in force, each an area (its subpaths, filled by
    /// nesting) the paint is cut to; empty when nothing clips it.
    std::vector<std::vector<Subpath>> clips;
};

/// An image in the artwork: its file's bytes (PNG or JPEG) or its pixels,
/// and the placement of its pixels on the stage.
struct Image
{
    /// The encoded file, as embedded; or empty, with \a rgba.
    std::vector<uint8_t> encoded;
    /// A CMYK JPEG whose samples are amounts of ink, as PostScript and PDF
    /// read them unless a Decode array inverts them; JPEG files keep CMYK
    /// inverted, and decoders read it so.
    bool inkCmyk = false;
    /// Straight RGBA, row by row, when there is no file.
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    /// Where an SVG found it outside itself, relative to the SVG.
    std::string href;
    /// Pixel space to the stage.
    Placement placement;
};

/// A line of text: what it says, its font, and where its baseline starts.
struct Text
{
    std::string text;
    std::string family;
    double size = 16.0;
    bool bold = false;
    bool italic = false;
    Color color{0, 0, 0, 255};
    /// The text's own space, in which its baseline starts at the origin, to
    /// the stage.
    Placement placement;
};

/// The artwork read, and the size of the page or canvas it was on.
struct Artwork
{
    std::vector<Painted> paths;
    std::vector<Image> images;
    std::vector<Text> texts;
    /// Everything in drawing order, bottom first: which list and where in
    /// it.
    enum class Kind
    {
        Path,
        Image,
        Text
    };
    std::vector<std::pair<Kind, size_t>> order;
    double width = 0.0;
    double height = 0.0;
    /// What could not be read, in words, for the person importing.
    std::vector<std::string> notes;
};

/// An SVG document's artwork: path, rect, circle, ellipse, line, polyline
/// and polygon, in groups with transforms and through use, with fill and
/// stroke from attributes, style or the style sheet's classes, gradients
/// among them; images, embedded or beside it; and text.
Artwork readSvg(const std::string& text);

/// An Illustrator file's artwork, from the PDF page it carries (or a PDF):
/// the page's paths with their colors, gradients and opacity, the groups
/// and placed art in it, and its images. False notes when the file has no
/// PDF page (an Illustrator 8 or older file).
Artwork readPdf(const std::vector<uint8_t>& bytes);

/// Base64 decoded, skipping what is not base64.
std::vector<uint8_t> fromBase64(const std::string& text);

/// A drawing object for a path: its fill on the inside of each closed
/// subpath (holes by nesting), its stroke along every edge. Null for a
/// path with nothing to paint.
Shape* toShape(const Painted& painted);

/// Arcs as SVG gives them, from \a from to \a to, as cubic curves.
std::vector<Segment> arcToCubics(const Point& from, double rx, double ry, double rotation, bool large, bool sweep,
    const Point& to);

} // namespace vectors

} // namespace fla
