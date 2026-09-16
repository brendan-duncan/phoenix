#include "draw_placement.h"

#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"

#include <memory>

namespace {

/// Whether two boxes touch at all, which is the test for whether a new shape has
/// anything to merge with.
bool overlaps(const fla::Rect& a, const fla::Rect& b)
{
    return a.topLeft.x <= b.bottomRight.x && b.topLeft.x <= a.bottomRight.x &&
           a.topLeft.y <= b.bottomRight.y && b.topLeft.y <= a.bottomRight.y;
}

/// The shape a new drawing should merge into: the topmost one in the frame it
/// touches. A drawing clear of everything has nothing to merge with and becomes
/// an object of its own.
fla::Shape* mergeTarget(fla::Frame& frame, const fla::Shape& addition)
{
    for (size_t i = frame.elements.size(); i > 0; --i)
    {
        fla::Element* element = frame.elements[i - 1];
        if (!element || element->elementType() != fla::Element::Type::Shape)
            continue;

        fla::Shape* candidate = static_cast<fla::Shape*>(element);

        // Only shapes sitting in the same coordinate space merge cleanly. One
        // that has been moved or scaled would need its geometry brought across
        // first, which is not done yet.
        const fla::Transform& t = candidate->transform;
        const bool untransformed = t.m11 == 1.0 && t.m12 == 0.0 && t.m21 == 0.0 &&
            t.m22 == 1.0 && t.tx == 0.0 && t.ty == 0.0;
        if (!untransformed)
            continue;

        if (overlaps(candidate->localBounds, addition.localBounds))
            return candidate;
    }

    return nullptr;
}

} // namespace

void placeDrawnElement(PhoenixView& view, fla::CommandStack& commandStack,
    fla::Selection& selection, fla::Frame* frame, fla::Element* element,
    const QString& gestureName, bool objectDrawing)
{
    if (!frame || !element)
    {
        delete element;
        return;
    }

    const std::string name = gestureName.toStdString();

    fla::Shape* addition = element->elementType() == fla::Element::Type::Shape
        ? static_cast<fla::Shape*>(element) : nullptr;

    fla::Shape* target = (!objectDrawing && addition)
        ? mergeTarget(*frame, *addition) : nullptr;

    if (target)
    {
        // The addition is folded into the target and is not itself added to the
        // document, so this owns it to the end of the call.
        std::unique_ptr<fla::Element> consumed(element);

        commandStack.push(fla::CommandPtr(new fla::MergeShapeCommand(
            target, *addition, name)));
        commandStack.breakMergeChain();

        // The target's geometry was rewritten, so anything cached against it is
        // stale.
        view.clearCaches();
        selection.select(target);
        view.update();
        return;
    }

    commandStack.push(fla::CommandPtr(new fla::AddElementCommand(
        frame, element, name, &selection)));
    commandStack.breakMergeChain();

    view.clearCaches();
    selection.select(element);
    view.update();
}
