#pragma once

#include "blend.h"
#include "geom.h"

#include <vector>

namespace fla {
namespace display {

class BitmapData;
class DisplayObject;
class TextField;
class TimelineArt;
struct BitmapFilter;
struct TextLine;

/// What the display engine needs from whatever renders the movie: drawing
/// display objects into pixels (BitmapData.draw), the filters it does not
/// work itself, the size and shape of timeline art, and laying out text in
/// real fonts. Set once by the renderer (setBackend); without one, draw of
/// a display object does nothing, timeline art has no size, and text is
/// laid out in a plain fixed-width approximation.
class Backend
{
public:
    virtual ~Backend() = default;

    /// BitmapData.draw of a display object: \a source with its children,
    /// drawing, filters, blend modes and masks, through \a matrix (from
    /// its own space to \a target's pixels), over \a target.
    virtual void draw(BitmapData& target, const DisplayObject& source, const Matrix& matrix,
        const ColorTransform* colorTransform, BlendMode blendMode, const Rect* clipRect, bool smoothing) = 0;
    /// BitmapData.applyFilter for the filters the core does not work
    /// (shadows, glows, bevels): false when it cannot.
    virtual bool applyFilter(BitmapData& target, const BitmapData& source, const Rect& sourceRect,
        const Point& destPoint, const BitmapFilter& filter) = 0;
    /// The box timeline art covers in its own space, and whether it is at
    /// a point there.
    virtual Rect artBounds(const TimelineArt& art, bool strokes) = 0;
    virtual bool artHits(const TimelineArt& art, double x, double y) = 0;
    /// A field's lines, in its text space (see TextLine).
    virtual std::vector<TextLine> layoutText(const TextField& field) = 0;
    /// The character index nearest (\a x, \a y) in a field's text space, as
    /// a caret goes, and the box of a character there.
    virtual int textIndexAt(const TextField& field, double x, double y) = 0;
    virtual Rect textCharBox(const TextField& field, int index) = 0;
};

Backend* backend();
void setBackend(Backend* backend);

/// Bumped whenever anything shown changes anywhere: a display object, a
/// Graphics, a BitmapData's pixels. Whatever draws compares it with the
/// one it last drew.
uint64_t displayGeneration();
void markDisplayChanged();

} // namespace display
} // namespace fla
