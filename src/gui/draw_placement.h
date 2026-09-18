#pragma once

#include <QString>

namespace fla {
class CommandStack;
class Element;
class Frame;
class Selection;
class Shape;
}

class PhoenixView;

/// Puts a freshly drawn element into the document.
///
/// The element joins the frame as its own object and becomes the selection,
/// whichever mode is on. In object drawing mode that is the end of it.
///
/// With that mode off -- the default, as in Animate -- the drawing is not part
/// of the artwork yet. It floats above it, selected, and can be dragged around
/// as one piece. Deselecting it is what commits it, through
/// commitPendingMerge(), so the view remembers it until then.
///
/// The pen, the pencil and the shape tools all finish the same way, so they all
/// end here rather than each deciding for itself.
///
/// Takes ownership of \a element.
void placeDrawnElement(PhoenixView& view, fla::CommandStack& commandStack,
    fla::Selection& selection, fla::Frame* frame, fla::Element* element,
    const QString& gestureName, bool objectDrawing);

/// Folds a drawing that was waiting into the artwork underneath it.
///
/// Outlines cut each other where they cross, the newer fill replaces the older
/// wherever they overlap, and any seam left between two regions that ended up
/// the same colour disappears. Afterwards the drawing no longer exists as a
/// separate object: what it covered has become part of one shape, cut out of
/// whatever was below.
///
/// Returns false, leaving everything alone, when there is nothing underneath to
/// merge with or the drawing is no longer in the frame -- which is what an undo
/// while it was still waiting looks like.
bool commitPendingMerge(PhoenixView& view, fla::CommandStack& commandStack,
    fla::Selection& selection, fla::Shape* addition, fla::Frame* frame);
