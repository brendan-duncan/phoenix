#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fla {

class Document;

/// One of a document's paint brushes, as its PaintBrushDefinitions.xml
/// keeps it: an index strokes refer to it by, and settings as JSON, the art
/// among them as an SVG document.
///
/// An art brush stretches its art along a stroke from end to end; a
/// pattern brush repeats its art, a tile at a time. Either way the art's
/// height runs across the stroke and is the stroke's weight.
struct PaintBrushDefinition
{
    int index = 0;
    std::string name;
    bool pattern = false;
    /// Every setting as read, by key, the art (svgData) among them.
    std::map<std::string, std::string> params;

    /// Which way the art runs along the stroke: its left end at the
    /// stroke's start (LeftToRight), its right end, its top or its bottom
    /// (artDirection 0 to 3).
    enum class Direction
    {
        LeftToRight,
        RightToLeft,
        TopToBottom,
        BottomToTop
    };
    Direction direction = Direction::LeftToRight;
    /// The art mirrored end for end (FlipAlong), or from one side of the
    /// stroke to the other (FlipAcross; an art brush's flipArt).
    bool flipAlong = false;
    bool flipAcross = false;

    /// How an art brush fits its art to the stroke's length
    /// (scaleOptionm 0 to 2): scaled whole, its height growing with its
    /// length; stretched along the stroke at the stroke's weight; or
    /// stretched only between its guides, the ends past them keeping
    /// their proportions.
    enum class Scale
    {
        Proportional,
        Stretch,
        BetweenGuides
    };
    Scale scale = Scale::Stretch;
    /// The guides, as fractions of the art's length (leftEdgePercentage,
    /// rightEdgePercentage).
    double leftGuide = 0.0;
    double rightGuide = 1.0;

    /// How a pattern brush fits its tiles to the stroke's length
    /// (adjustment 0 to 2): tiles stretched to a whole number of them,
    /// spaces added between them, or the tiles laid at their own length.
    enum class Fit
    {
        Stretch,
        AddSpace,
        Approximate
    };
    Fit fit = Fit::Stretch;
    /// The space between a pattern's tiles, as a fraction of a tile.
    double spacing = 0.0;
};

/// A brush's art, read from its SVG: closed outlines with their colors,
/// bottom first, in the SVG's units, and the box the art is laid out by
/// (the SVG's viewBox: an art brush's art end to end, a pattern's tile).
struct PaintBrushArt
{
    struct Point
    {
        double x = 0.0;
        double y = 0.0;
    };
    using Color = std::array<uint8_t, 4>;
    struct Piece
    {
        std::vector<std::vector<Point>> rings;
        /// Filled where an odd number of rings cover (fill-rule evenodd),
        /// else where they wind around.
        bool evenOdd = false;
        Color color{0, 0, 0, 255};
    };
    std::vector<Piece> pieces;
    double width = 0.0;
    double height = 0.0;
    /// Art in grays only, which takes the stroke's color: black becomes
    /// the color, white stays white and the grays between are its tints.
    /// Art in colors of its own keeps them.
    bool tinted = false;
};

/// The settings, read from a definition's brushParam: a JSON object whose
/// values are strings.
PaintBrushDefinition paintBrushDefinition(int index, const std::string& json);

/// The definitions in a PaintBrushDefinitions.xml.
std::vector<PaintBrushDefinition> readPaintBrushDefinitions(const std::string& xml);

/// \a definition's art (its svgData), its curves flattened.
PaintBrushArt paintBrushArt(const PaintBrushDefinition& definition);

/// A document's paint brushes: their definitions, and their art, read the
/// first time a stroke draws with it.
class PaintBrushLibrary
{
public:
    std::vector<PaintBrushDefinition> definitions;

    /// The definition with \a index; null when there is none.
    const PaintBrushDefinition* find(int index) const;

    /// The art of the definition with \a index; null when there is none.
    std::shared_ptr<const PaintBrushArt> art(int index) const;

private:
    mutable std::mutex _mutex;
    mutable std::map<int, std::shared_ptr<const PaintBrushArt>> _art;
};

/// \a document's paint brushes, from the PaintBrushDefinitions.xml its
/// brush definitions include; read once and kept with the document. Null
/// when it has none.
std::shared_ptr<const PaintBrushLibrary> paintBrushesOf(const Document& document);

/// A stroke drawn with a brush: \a art laid along \a centerline (a
/// polyline, closed when \a closed) at \a weight, as \a definition lays
/// it, colored for a stroke of \a color. The pieces are in the
/// centerline's units, bottom first.
std::vector<PaintBrushArt::Piece> layPaintBrush(const PaintBrushDefinition& definition, const PaintBrushArt& art,
    const std::vector<PaintBrushArt::Point>& centerline, bool closed, double weight, const PaintBrushArt::Color& color);

} // namespace fla
