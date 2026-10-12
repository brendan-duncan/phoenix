#include "combine_objects.h"

#include "shape_merge.h"
#include "shape_transform.h"

#include "../data/shape.h"

#include <memory>

namespace fla {

namespace combine {

namespace {

/// A copy of \a shape with its transform brought into its points: the shape
/// as it lies in its parent's space.
std::unique_ptr<Shape> inParent(const Shape& shape)
{
    std::unique_ptr<Shape> copy(cloneShape(shape, nullptr));
    if (copy)
        bakeTransform(*copy);
    return copy;
}

/// A copy of \a parentShape (in the parent's space) moved into the own
/// space of an element whose transform is \a transform.
std::unique_ptr<Shape> intoSpaceOf(const Shape& parentShape, const Transform& transform)
{
    std::unique_ptr<Shape> copy(cloneShape(parentShape, nullptr));
    if (!copy)
        return copy;
    const double det = transform.m11 * transform.m22 - transform.m12 * transform.m21;
    if (det == 0.0)
        return nullptr;
    warpShape(*copy, [&transform, det](const Point& p) {
        const double x = p.x - transform.tx;
        const double y = p.y - transform.ty;
        return Point((transform.m22 * x - transform.m21 * y) / det, (-transform.m12 * x + transform.m11 * y) / det);
    });
    return copy;
}

} // namespace

bool apply(Operation operation, const std::vector<Shape*>& shapes, Result& result)
{
    result = Result();
    if (shapes.size() < 2)
        return false;
    Shape* top = shapes.back();

    if (operation == Operation::Union || operation == Operation::Intersect)
    {
        std::unique_ptr<Shape> combined = inParent(operation == Operation::Union ? *shapes.front() : *top);
        if (!combined)
            return false;
        if (operation == Operation::Union)
        {
            // Each in turn over what is there, as drawing them in order would.
            for (size_t i = 1; i < shapes.size(); ++i)
            {
                std::unique_ptr<Shape> next = inParent(*shapes[i]);
                if (next)
                    ShapeMerger::merge(*combined, *next);
            }
        }
        else
        {
            // The topmost, kept to where every other one paints.
            for (size_t i = 0; i + 1 < shapes.size(); ++i)
            {
                std::unique_ptr<Shape> other = inParent(*shapes[i]);
                if (other)
                    ShapeMerger::clip(*combined, *other);
            }
        }
        if (combined->edges.empty())
            return false;
        combined->isDrawingObject = true;
        result.replacement = combined.release();
        result.removed = shapes;
        return true;
    }

    // Punch and Crop: the topmost works on each of the others in its space.
    std::unique_ptr<Shape> cutter = inParent(*top);
    if (!cutter)
        return false;
    for (size_t i = 0; i + 1 < shapes.size(); ++i)
    {
        Shape* below = shapes[i];
        std::unique_ptr<Shape> local = intoSpaceOf(*cutter, below->transform);
        std::unique_ptr<Shape> after(cloneShape(*below, nullptr));
        if (!local || !after)
            continue;
        const bool changed = operation == Operation::Punch ? ShapeMerger::subtract(*after, *local)
                                                           : ShapeMerger::clip(*after, *local);
        if (changed)
            result.rewritten.emplace_back(below, after.release());
    }
    result.removed.push_back(top);
    return true;
}

} // namespace combine

} // namespace fla
