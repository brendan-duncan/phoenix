#pragma once

#include <QString>

#include <memory>
#include <vector>

namespace fla {
class CommandStack;
class Element;
class Frame;
class Selection;
class Shape;
}

class PhoenixView;

/// A shape a waiting drawing cut its hole in, as it was before the cut.
struct UncutShape
{
    fla::Shape* shape = nullptr;
    std::shared_ptr<const fla::Shape> before;
};

/// Puts a freshly drawn element into the document.
///
/// The element joins the frame as its own object and becomes the selection,
/// whichever mode is on. In object drawing mode that is the end of it.
///
/// With that mode off -- the default -- the drawing is not part
/// of the artwork yet. It floats above it, selected, and can be dragged around
/// as one piece. Deselecting it is what commits it, through
/// commitPendingMerge(), so the view remembers it until then.
///
/// The pen, the pencil and the shape tools all finish the same way, so they all
/// end here rather than each deciding for itself.
///
/// Takes ownership of \a element.
/// With \a keepStrokes the drawing paints under the artwork's strokes rather
/// than burying them, both when it cuts its hole and when it is let go.
void placeDrawnElement(PhoenixView& view, fla::CommandStack& commandStack,
    fla::Selection& selection, fla::Frame* frame, fla::Element* element,
    const QString& gestureName, bool objectDrawing, bool keepStrokes = false);

/// Folds a drawing that was waiting into the artwork underneath it.
///
/// Outlines cut each other where they cross, the newer fill replaces the older
/// wherever they overlap, and any seam left between two regions that ended up
/// the same color disappears. Afterwards the drawing no longer exists as a
/// separate object: what it covered has become part of one shape, cut out of
/// whatever was below.
///
/// Returns false, leaving everything alone, when there is nothing underneath to
/// merge with or the drawing is no longer in the frame -- which is what an undo
/// while it was still waiting looks like.
///
/// \a uncut, given when nothing has been done since the drawing was placed,
/// holds the shapes it cut as they were before: the drawing is merged into
/// those instead. Merging covers whatever it lands on, so the result is the
/// same, but the hole's outline is a welded, refitted copy of the drawing's
/// own, and the two together are too nearly one curve to cut cleanly.
bool commitPendingMerge(PhoenixView& view, fla::CommandStack& commandStack,
    fla::Selection& selection, fla::Shape* addition, fla::Frame* frame,
    bool keepStrokes = false, const std::vector<UncutShape>* uncut = nullptr);
