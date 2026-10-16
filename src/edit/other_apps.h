#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fla {

class ColorTransform;
class Document;
class Element;
class Timeline;
struct Filter;

namespace behaviors {
struct Applied;
}

/// Phoenix's own compositing and templates made into what any app that opens
/// FLA files shows, for File > Export > Export FLA for Other Apps. Free of
/// Qt. It changes the document it is given for good: it is for a copy being
/// written, never the document being edited.
namespace otherapps {

/// Whether \a document has anything bakeCompositing would change.
bool needsBaking(const Document& document);

/// What to draw for a bitmap: layers \a from to \a to - 1 of \a timeline
/// on \a frame (of that timeline), as an exported movie shows them; with
/// \a element, only that element of its layer, in its layer's own space
/// (no layer parent). \a margin is how far, in its pixels, the effects
/// may reach past the art.
struct Drawing
{
    const Timeline* timeline = nullptr;
    int from = 0;
    int to = 0;
    const Element* element = nullptr;
    int frame = 0;
    double margin = 0.0;
};

/// What was drawn: premultiplied ARGB, a row after another, \a scale
/// pixels to the timeline's pixel, its top left corner at \a x, \a y in
/// the timeline's space. Empty (no pixels) when nothing shows.
struct Picture
{
    int width = 0;
    int height = 0;
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
    std::vector<uint32_t> pixels;
};

/// Draws for bakeCompositing. Drawing needs Phoenix's renderer, which this
/// code has no hold of: the caller hands it in.
using Renderer = std::function<Picture(const Drawing&)>;

/// - Template values: each instance shows a copy of its symbol with its
///   words and colors written in (one copy for each different set of
///   values, nested symbols too).
/// - Track mattes: an alpha matte becomes a mask over the layer, its shape
///   hard-edged; a luma matte the same, by shape rather than light.
///   Inverted mattes cannot be carried and are left drawn plainly.
/// - Adjustment layers: the layers below go into a movie clip of their own,
///   placed on the adjustment layer with the effects as its filters and
///   color effect (see carryEffects), keyed a frame at a time where the
///   settings change. The movie clip plays on its own in other apps.
/// - Effects on instances: carried the same way, as the instance's own
///   filters (before those it had) and color effect.
///
/// An effect with nothing like it in other apps (Turbulent Displace, a
/// gradient, a wipe part way...) is drawn by \a render instead: the
/// adjustment layer and all below it, or the instance, become a bitmap on
/// each frame, the bitmaps kept in a "Baked effects" library folder and a
/// keyframe only where the picture changes. The adjustment layer's layers
/// stay in the movie clip in the library, unused. With no \a render, such
/// effects are left out.
///
/// \a notes says what could only be carried in part.
void bakeCompositing(Document& document, std::vector<std::string>& notes, const Renderer& render = Renderer());

/// The filters and color effect \a effects make on \a frame, added to
/// \a filters and \a color: Blur, Directional Blur along an axis and Lens
/// Blur with no highlight boost as Blur; Color Correction and
/// Hue/Saturation (not colorized) as Adjust Color; Glow and Outline as
/// Glow; Tint, Fill and Levels with no gamma as the color effect (when it
/// is free); a wipe at 0 as nothing and at 100 as no alpha. Returns
/// whether all of them could be; \a left gets the names of those that
/// could not.
bool carryEffects(const std::vector<behaviors::Applied>& effects, int frame, std::vector<Filter>& filters,
    ColorTransform& color, std::vector<std::string>* left = nullptr);

} // namespace otherapps

} // namespace fla
