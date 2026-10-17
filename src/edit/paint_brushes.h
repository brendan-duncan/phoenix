#pragma once

#include "brush_stroke.h"
#include "command.h"

#include "../data/stroke_style.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fla {

class Document;
class Element;

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
    /// The settings as the file keeps them (brushParam), written back as
    /// they are; withParam() changes one.
    std::string json;

    /// What tells the brush from any other wherever it is copied to (its
    /// guid), so a document takes one in only once.
    std::string guid() const;

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

    /// How a pattern brush turns a sharp corner of its stroke
    /// (cornerValue 0 to 3). The definition carries one tile, the side
    /// tile, and the corner is made from it: a tile centered on the corner
    /// and folded round it; tiles meeting at the corner, folded into it;
    /// tiles meeting at the corner, each cut along the corner's bisector
    /// like a picture frame's mitered sides; or tiles running on past the
    /// corner, overlapping there.
    enum class Corner
    {
        Centered,
        Between,
        Sliced,
        Overlap
    };
    Corner corner = Corner::Sliced;

    /// Whether an art brush's art bends round a sharp turn of its stroke
    /// rather than folding over itself on the inside of the turn
    /// (foldAdjust).
    bool foldAdjust = true;

    /// The narrowest and widest the stroke may draw where a pen's pressure,
    /// or its width profile, sets its width, and the same for the pen's
    /// tilt, as fractions of its weight (minimumWidthPercentage,
    /// maximumWidthPercentage, minimumTiltWidthPercentage,
    /// maximumTiltWidthPercentage).
    double minimumWidth = 0.01;
    double maximumWidth = 1.0;
    double minimumTiltWidth = 0.01;
    double maximumTiltWidth = 1.0;
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
/// it, colored for a stroke of \a color. With a width \a profile (the
/// stroke's, from a pen's pressure or the Width tool) the art narrows and
/// swells along the stroke as it says, within the brush's narrowest and
/// widest. The pieces are in the centerline's units, bottom first.
std::vector<PaintBrushArt::Piece> layPaintBrush(const PaintBrushDefinition& definition, const PaintBrushArt& art,
    const std::vector<PaintBrushArt::Point>& centerline, bool closed, double weight, const PaintBrushArt::Color& color,
    const std::vector<StrokeStyle::WidthMarker>& profile = {});

/// A width profile for a stroke drawn with \a definition from the pen's
/// \a samples along it: harder pressure widens it from the brush's
/// narrowest to its widest, and a pen leaning over narrows it from its
/// widest tilt width toward its narrowest (all the way over at 60 degrees).
/// Empty when nothing varies, as from a mouse.
std::vector<StrokeStyle::WidthMarker> paintBrushWidthProfile(
    const PaintBrushDefinition& definition, const std::vector<BrushSample>& samples);

// ---------------------------------------------------------------- editing

/// \a json, a JSON object, with its member \a key set to the string
/// \a value: in place when it has one, else added at the end.
std::string withJsonMember(const std::string& json, const std::string& key, const std::string& value);

/// \a definition with the setting \a key set to \a value, read anew.
PaintBrushDefinition withParam(const PaintBrushDefinition& definition, const std::string& key, const std::string& value);

/// A new brush identity, as definitions spell their guid
/// (8-4-4-4-12 upper-case hex digits).
std::string newPaintBrushGuid();

/// \a definitions as a PaintBrushDefinitions.xml, each one's settings as it
/// keeps them, laid out as FLA files have it.
std::string writePaintBrushDefinitions(const std::vector<PaintBrushDefinition>& definitions);

/// A group to file a brush under in the brush library, from its name and
/// kind: "Arrow 2.01" under Arrows, "Charcoal - Soft" under Chalk,
/// Charcoal and Pencil, a pattern of dashes under Dashed Borders.
std::string paintBrushCategory(const PaintBrushDefinition& definition);

/// \a incoming (another document's brushes, or the brush library's) added
/// to \a existing: each one \a existing has by guid is used as it is, the
/// rest come in with new indexes after the highest. \a indexes gets the
/// index in the result of each incoming brush, by its index in \a incoming.
std::vector<PaintBrushDefinition> mergedPaintBrushes(const std::vector<PaintBrushDefinition>& existing,
    const std::vector<PaintBrushDefinition>& incoming, std::map<int, int>& indexes);

/// Sets \a document's brushes to \a definitions as one undoable step: its
/// PaintBrushDefinitions.xml written anew, and the <brushdefinitions>
/// section including it added when it had none. Null when nothing
/// changes.
CommandPtr setPaintBrushDefinitions(
    Document& document, const std::vector<PaintBrushDefinition>& definitions, const std::string& name);

/// Brings \a incoming into \a document as mergedPaintBrushes does, as one
/// undoable step; null when it has them all. \a indexes as for
/// mergedPaintBrushes.
CommandPtr adoptPaintBrushes(Document& document, const std::vector<PaintBrushDefinition>& incoming,
    std::map<int, int>& indexes, const std::string& name = "Add Brushes");

/// The brushes \a element's strokes are drawn with (inside groups too), by
/// index, into \a used.
void paintBrushesUsedBy(const Element& element, std::set<int>& used);

/// Points the strokes of \a element (inside groups too) at other brushes:
/// each index found in \a indexes becomes the one it maps to.
void remapPaintBrushes(Element& element, const std::map<int, int>& indexes);

/// How many strokes of \a document, in its scenes and its symbols, are
/// drawn with each brush, by index.
std::map<int, int> paintBrushUses(const Document& document);

/// \a source's definitions of the brushes \a elements draw with, for a
/// paste or a drag to take along to another document.
std::vector<PaintBrushDefinition> paintBrushesFor(const Document& source, const std::vector<const Element*>& elements);

} // namespace fla
