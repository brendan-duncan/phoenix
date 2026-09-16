#pragma once

#include <QString>

namespace fla {
class CommandStack;
class Element;
class Frame;
class Selection;
}

class PhoenixView;

/// Puts a freshly drawn element into the document.
///
/// In object drawing mode the element joins the frame as its own object. With
/// that mode off -- which is the default, as in Animate -- a shape merges into
/// whatever it was drawn across: outlines cut each other, the newer fill
/// replaces the older, and the two become one object.
///
/// The pen, the pencil and the shape tools all finish the same way, so they all
/// end here rather than each deciding for itself.
///
/// Takes ownership of \a element: it is either handed to the document or, when
/// merged, consumed and deleted.
void placeDrawnElement(PhoenixView& view, fla::CommandStack& commandStack,
    fla::Selection& selection, fla::Frame* frame, fla::Element* element,
    const QString& gestureName, bool objectDrawing);
