#pragma once

#include "command.h"
#include "drawing_style.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class DOMElement;
class Document;
class FillStyle;
class Shape;

/// The Swatches panel's colors: the document's own swatch list, which
/// the FLA saves in the file, and the default palette for a document
/// that has none.
namespace swatches {

using Color = std::array<uint8_t, 4>;

/// The default palette: a ramp of grays and the primaries, then the
/// 216 web-safe colors.
std::vector<Color> defaults();

/// The solid colors of the document's first swatch list; empty when it has
/// none.
std::vector<Color> of(const Document& document);

/// The document's first swatch list as text, and whether it has one; and
/// its extended swatch lists, where the reference app keeps the gradient
/// swatches.
struct State
{
    bool exists = false;
    std::string xml;
    std::vector<Color> colors;
    bool extended = false;
    std::string extendedXml;
};
State stateOf(const Document& document);

/// \a state with \a color added at the end; with no list yet, a new one
/// named Swatches.
State withAdded(const State& state, const Color& color);

/// \a state without its solid swatch \a index. Swatches it does not model
/// (gradients) stay as they were.
State withRemoved(const State& state, size_t index);

/// \a state with its solid swatches replaced by \a colors, in order;
/// gradients stay. An empty list still exists.
State withColors(const State& state, const std::vector<Color>& colors);

/// The colors in hue order, grays first by lightness, as Sort by Color
/// lays them out.
std::vector<Color> sortedByColor(std::vector<Color> colors);

/// Color tables: an Adobe color table (.act, 256 RGB triplets and an
/// optional count) and a GIMP palette (.gpl, lines of red, green, blue).
std::vector<Color> readAct(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> writeAct(const std::vector<Color>& colors);
std::vector<Color> readGpl(const std::string& text);
std::string writeGpl(const std::vector<Color>& colors, const std::string& name);

/// A gradient swatch: linear or radial, its stops, and how it spreads and
/// blends, as the drawing style keeps a gradient.
struct Gradient
{
    bool radial = false;
    std::vector<DrawingStyle::Stop> stops;
    std::string spread;
    bool linearRGB = false;
};

/// The gradients a document with none of its own shows, as the reference
/// app's default swatches have them: white to black, linear and radial;
/// white to red, green and blue, radial; two color ramps; and a soft gray.
std::vector<Gradient> defaultGradients();

/// The gradient swatches in swatch list text (LinearGradientSwatchItem and
/// RadialGradientSwatchItem with their GradientEntry stops), in order.
std::vector<Gradient> gradientsIn(const std::string& xml);

/// The document's gradient swatches: those in its swatch list, then those
/// in its extended swatch lists, where the reference app keeps them. Empty
/// when it has none.
std::vector<Gradient> gradientsOf(const Document& document);
std::vector<Gradient> gradientsOf(const State& state);

/// The swatches a document shows: its own colors and gradients, or the
/// defaults for either it has none of.
std::vector<Color> shownColors(const Document* document);
std::vector<Gradient> shownGradients(const Document* document);

/// \a gradient as a swatch list item: a LinearGradientSwatchItem or
/// RadialGradientSwatchItem with a GradientEntry for each stop.
std::string itemOf(const Gradient& gradient);

/// \a state with \a gradient added at the end of its extended swatch
/// lists, made when it has none. A document showing the default gradients
/// keeps them, now its own, ahead of the new one.
State withGradientAdded(const State& state, const Gradient& gradient);

/// \a state without its gradient swatch \a index, counted as gradientsOf
/// counts them; the defaults become the document's own first when it has
/// none.
State withGradientRemoved(const State& state, size_t index);

/// \a gradient as the fill new art is drawn with: the style's fill kind,
/// stops, spread and blending, and its fill switched on.
void useGradient(DrawingStyle& style, const Gradient& gradient);

/// A new fill of \a gradient, unplaced: DrawingStyle::fitFill or fitFills
/// puts it over the art. The caller owns it.
FillStyle* gradientFill(const Gradient& gradient, DOMElement* parent);

/// Paints every fill of \a shape with \a gradient, each fitted to the
/// art it fills, or with \a color, replacing gradients and bitmaps too.
void paintFills(Shape& shape, const Gradient& gradient);
void paintFills(Shape& shape, const Color& color);
/// The same for the strokes: a gradient fitted to each stroke's lines.
void paintStrokes(Shape& shape, const Gradient& gradient);
void paintStrokes(Shape& shape, const Color& color);

/// \a shape without its fills, or with \a strokes without its strokes, as
/// No Color leaves it; null when nothing would be left, since a shape
/// keeps a fill or a stroke. The caller owns it.
Shape* withoutPart(const Shape& shape, bool strokes);

} // namespace swatches

/// Puts the document's swatch list in one state or the other.
class SetSwatchesCommand : public Command
{
public:
    SetSwatchesCommand(Document* document, swatches::State before, swatches::State after, const std::string& name);
    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

private:
    void apply(const swatches::State& state);

    Document* _document;
    swatches::State _before;
    swatches::State _after;
    std::string _name;
};

} // namespace fla
