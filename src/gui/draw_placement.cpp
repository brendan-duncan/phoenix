#include "draw_placement.h"

#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/shape_transform.h"

#include <memory>

namespace {

/// Every shape below  addition that it overlaps, nearest first.
///
/// Only shapes below count. A drawing joins the artwork it was laid over, never
/// something stacked on top of it, so the search starts at the addition's own
/// place in the z-order and works down.
///
/// Bounds come from the view rather than from the shape's own `localBounds`,
/// which is recorded when a shape is built and not kept up to date through a
/// merge. The view computes them from the geometry, in document space, so a
/// shape that has been dragged is measured where it actually sits.
std::vector<fla::Shape*> shapesUnder(PhoenixView& view, fla::Frame& frame,
    fla::Shape& addition)
{
    std::vector<fla::Shape*> found;

    const QRectF additionBounds = view.elementBounds(&addition);
    if (!additionBounds.isValid())
        return found;

    size_t additionIndex = frame.elements.size();
    for (size_t i = 0; i < frame.elements.size(); ++i)
    {
        if (frame.elements[i] == &addition)
        {
            additionIndex = i;
            break;
        }
    }

    for (size_t i = additionIndex; i > 0; --i)
    {
        fla::Element* element = frame.elements[i - 1];
        if (!element || element->elementType() != fla::Element::Type::Shape)
            continue;

        fla::Shape* candidate = static_cast<fla::Shape*>(element);

        const QRectF candidateBounds = view.elementBounds(candidate);
        if (candidateBounds.isValid() && candidateBounds.intersects(additionBounds))
            found.push_back(candidate);
    }

    return found;
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

    // Anything still waiting from a previous drawing is committed first, so it
    // merges into the artwork that was under it rather than into the drawing
    // about to be added above it.
    view.flushPendingMerge();

    const std::string name = gestureName.toStdString();

    // Adding the drawing and cutting what it covers are one action to undo, so
    // they go on the history together.
    commandStack.beginMacro(name);

    commandStack.push(fla::CommandPtr(new fla::AddElementCommand(
        frame, element, name, &selection)));

    // The element is in the frame now, so it can be measured against what it
    // landed on.
    view.clearCaches();

    if (!objectDrawing && element->elementType() == fla::Element::Type::Shape)
    {
        fla::Shape* addition = static_cast<fla::Shape*>(element);

        // Drawing over artwork destroys what was under it, there and then. The
        // drawing sits exactly over the hole it just made, so nothing looks
        // different until it is moved -- and then the hole is simply uncovered
        // rather than appearing out of nowhere.
        for (fla::Shape* target : shapesUnder(view, *frame, *addition))
        {
            // Cutting reads raw edge coordinates, so a target that carries its
            // position in a transform has to be brought down into its geometry.
            fla::bakeTransform(*target);

            commandStack.push(fla::CommandPtr(new fla::SubtractShapeCommand(
                target, *addition, name)));
        }

        // It is still its own object, selected and draggable as one piece, until
        // it is let go. Letting go drops it back into the artwork wherever it
        // now sits.
        view.setPendingMerge(addition, frame);
    }

    commandStack.endMacro();
    commandStack.breakMergeChain();

    view.clearCaches();
    selection.select(element);
    view.update();
}

bool commitPendingMerge(PhoenixView& view, fla::CommandStack& commandStack,
    fla::Selection& selection, fla::Shape* addition, fla::Frame* frame)
{
    if (!addition || !frame)
        return false;

    // The drawing may have been undone, or removed some other way, while it was
    // still waiting to merge.
    bool present = false;
    for (const fla::Element* element : frame->elements)
        present = present || element == addition;
    if (!present)
        return false;

    // Everything it now sits on, nearest first.
    const std::vector<fla::Shape*> below = shapesUnder(view, *frame, *addition);
    if (below.empty())
        return false;

    // Merging reads raw edge coordinates, so everything involved has to be in
    // the same space first. A shape that was dragged carries the move in its
    // transform, and what it landed on may carry one of its own.
    fla::bakeTransform(*addition);
    for (fla::Shape* target : below)
        fla::bakeTransform(*target);

    // One undo step for the whole drop.
    commandStack.beginMacro("Merge Shape");

    // It joins the nearest thing below it, and destroys what it covers of
    // everything further down. Merging already replaces what it lands on, so
    // only the rest need cutting.
    commandStack.push(fla::CommandPtr(new fla::MergeShapeCommand(
        below.front(), *addition, "Merge Shape")));

    for (size_t i = 1; i < below.size(); ++i)
    {
        commandStack.push(fla::CommandPtr(new fla::SubtractShapeCommand(
            below[i], *addition, "Merge Shape")));
    }

    commandStack.push(fla::CommandPtr(new fla::RemoveElementCommand(
        frame, addition, "Merge Shape", &selection)));
    commandStack.endMacro();
    commandStack.breakMergeChain();

    // The target's geometry was rewritten, so anything cached against it, and
    // against the drawing that is now gone, is stale.
    view.clearCaches();
    view.update();
    return true;
}
