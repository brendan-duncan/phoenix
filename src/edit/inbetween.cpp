#include "inbetween.h"

#include "element_clone.h"
#include "shape_tween.h"
#include "timeline_edit.h"
#include "transform_blend.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/morph_shape.h"
#include "../data/shape.h"
#include "../data/symbol_instance.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace fla {

namespace inbetween {

Report& Report::operator+=(const Report& other)
{
    morphed += other.morphed;
    moved += other.moved;
    held += other.held;
    keyframes += other.keyframes;
    return *this;
}

double spacingAt(Spacing spacing, int step, int steps)
{
    if (steps <= 0)
        return 0.0;
    const double s = std::clamp(static_cast<double>(step) / steps, 0.0, 1.0);
    switch (spacing)
    {
    case Spacing::EaseIn:
        return s * s;
    case Spacing::EaseOut:
        return 1.0 - (1.0 - s) * (1.0 - s);
    case Spacing::Even:
        break;
    }
    return s;
}

namespace {

bool isShape(const Element* element)
{
    return element && element->elementType() == Element::Type::Shape;
}

/// The keyframe that starts at \a index, or null.
Frame* keyframeAt(Layer& layer, int index)
{
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index == index)
            return frame;
    }
    return nullptr;
}

/// The frames of an object motion tween hold one drawing moved by curves:
/// there is nothing to draw between.
bool motionSpan(const Frame& frame)
{
    return frame.otherTweenType == "motion object";
}

/// A keyframe for an inbetween at \a index, holding \a t of the way from
/// \a from's drawing to \a to's.
bool writeKeyframe(Layer& layer, int index, const Frame& from, const Frame& to, double t, Report& report)
{
    Frame* key = timeline::insertKeyframe(layer, index, true);
    if (!key || key == &from || key == &to)
        return false;
    key->tweenType = TweenType::None;
    for (Element* element : drawingBetween(from, to, t, key, report))
        key->elements.push_back(element);
    ++report.keyframes;
    return true;
}

} // namespace

std::vector<Element*> drawingBetween(const Frame& from, const Frame& to, double t, DOMElement* parent,
    Report& report)
{
    std::vector<Element*> made;
    const bool nearerFrom = t < 0.5;

    // Shapes pair in stacking order; the hints on the first key steer the
    // first pair, as they steer a shape tween's first shapes.
    std::vector<const Shape*> shapesFrom;
    std::vector<const Shape*> shapesTo;
    for (const Element* element : from.elements)
        if (isShape(element))
            shapesFrom.push_back(static_cast<const Shape*>(element));
    for (const Element* element : to.elements)
        if (isShape(element))
            shapesTo.push_back(static_cast<const Shape*>(element));
    const std::vector<ShapeHint> hints = hintsOf(from);

    // Everything else pairs as a classic tween pairs it.
    std::vector<Element*> othersFrom;
    std::vector<Element*> othersTo;
    for (Element* element : from.elements)
        if (element && !isShape(element))
            othersFrom.push_back(element);
    for (Element* element : to.elements)
        if (element && !isShape(element))
            othersTo.push_back(element);
    const std::vector<int> pairs = pairTweenElements(othersFrom, othersTo);
    std::vector<bool> pairedTo(othersTo.size(), false);
    for (int pair : pairs)
        if (pair >= 0 && pair < static_cast<int>(othersTo.size()))
            pairedTo[pair] = true;

    // The first key's stacking order, what only the second key has after.
    size_t shapeAt = 0;
    size_t otherAt = 0;
    for (const Element* element : from.elements)
    {
        if (!element)
            continue;
        if (isShape(element))
        {
            const size_t i = shapeAt++;
            if (i < shapesTo.size())
            {
                const Shape& a = *shapesFrom[i];
                const Shape& b = *shapesTo[i];
                std::unique_ptr<MorphShape> morph(
                    buildMorph(a, b, nullptr, i == 0 ? hints : std::vector<ShapeHint>()));
                std::unique_ptr<Shape> morphed(
                    morph ? morphedShape(*morph, a, b, t, a.transform, b.transform, Transform()) : nullptr);
                Element* copy = morphed ? cloneElement(*morphed, parent) : nullptr;
                if (copy)
                {
                    // The morph is built in the frame's space.
                    copy->transform = Transform();
                    made.push_back(copy);
                    ++report.morphed;
                    continue;
                }
            }
            if (nearerFrom)
            {
                if (Element* copy = cloneElement(*element, parent))
                    made.push_back(copy);
            }
            ++report.held;
            continue;
        }
        const size_t i = otherAt++;
        const int pair = i < pairs.size() ? pairs[i] : -1;
        if (pair < 0)
        {
            if (nearerFrom)
            {
                if (Element* copy = cloneElement(*element, parent))
                    made.push_back(copy);
            }
            ++report.held;
            continue;
        }
        const Element* toward = othersTo[static_cast<size_t>(pair)];
        Element* copy = cloneElement(*element, parent);
        if (!copy)
            continue;
        copy->transform = blendTransforms(element->transform, toward->transform, t);
        if (element->elementType() == Element::Type::SymbolInstance &&
            toward->elementType() == Element::Type::SymbolInstance)
        {
            auto* instance = static_cast<SymbolInstance*>(copy);
            const auto* a = static_cast<const SymbolInstance*>(element);
            const auto* b = static_cast<const SymbolInstance*>(toward);
            instance->colorTransform = blendColorTransforms(a->colorTransform, b->colorTransform, t);
            instance->filters = blendFilters(a->filters, b->filters, t);
        }
        made.push_back(copy);
        ++report.moved;
    }

    // What only the second key has: its spare shapes and unpaired others.
    size_t spareShape = 0;
    size_t spareOther = 0;
    for (const Element* element : to.elements)
    {
        if (!element)
            continue;
        bool spare = false;
        if (isShape(element))
            spare = spareShape++ >= shapesFrom.size();
        else
            spare = !pairedTo[spareOther++];
        if (!spare)
            continue;
        if (!nearerFrom)
        {
            if (Element* copy = cloneElement(*element, parent))
                made.push_back(copy);
        }
        ++report.held;
    }
    return made;
}

bool keysAround(Layer& layer, int first, int last, Frame*& from, Frame*& to)
{
    from = nullptr;
    to = nullptr;
    Frame* span = timeline::spanAt(layer, first);
    if (!span)
        return false;
    Frame* next = keyframeAt(layer, span->index + std::max(1, span->duration));
    // Standing on the last keyframe (or one selected alone with nothing
    // after it): between the one before and it.
    if (!next && span->index == first && last == first)
    {
        Frame* before = nullptr;
        for (Frame* frame : layer.frames)
            if (frame && frame != span && frame->index + std::max(1, frame->duration) == span->index)
                before = frame;
        if (before)
        {
            next = span;
            span = before;
        }
    }
    if (!next || next == span || motionSpan(*span))
        return false;
    if (span->elements.empty() && next->elements.empty())
        return false;
    from = span;
    to = next;
    return true;
}

int insertInbetween(Layer& layer, int first, int last, double t, Report& report)
{
    Frame* from = nullptr;
    Frame* to = nullptr;
    if (!keysAround(layer, first, last, from, to))
        return -1;
    t = std::clamp(t, 0.0, 1.0);
    // Keys side by side: a frame inserted after the first makes room.
    if (to->index - from->index < 2)
        timeline::insertFrames(layer, from->index, 1);
    const int gap = to->index - from->index;
    if (gap < 2)
        return -1;
    const int at = from->index + std::clamp(static_cast<int>(std::lround(gap * t)), 1, gap - 1);
    return writeKeyframe(layer, at, *from, *to, t, report) ? at : -1;
}

int fillInbetweens(Layer& layer, int first, int last, Spacing spacing, Report& report)
{
    Frame* from = nullptr;
    Frame* to = nullptr;
    if (!keysAround(layer, first, last, from, to))
        return 0;
    const int gap = to->index - from->index;
    if (gap < 2)
        return 0;
    // Every frame between is drawn now: the first key's tween has no more
    // to do.
    from->tweenType = TweenType::None;
    delete from->morphShape;
    from->morphShape = nullptr;
    from->morphSignature = 0;
    const int start = from->index;
    int made = 0;
    for (int step = 1; step < gap; ++step)
    {
        if (writeKeyframe(layer, start + step, *from, *to, spacingAt(spacing, step, gap), report))
            ++made;
    }
    return made;
}

} // namespace inbetween

} // namespace fla
