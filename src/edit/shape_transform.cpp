#include "shape_transform.h"

#include "../data/shape.h"

namespace fla {

namespace {

bool isIdentity(const Transform& t)
{
    return t.m11 == 1.0 && t.m12 == 0.0 && t.m21 == 0.0 && t.m22 == 1.0 &&
        t.tx == 0.0 && t.ty == 0.0;
}

} // namespace

void bakeTransform(Shape& shape)
{
    const Transform t = shape.transform;
    if (isIdentity(t))
        return;

    for (Edge* edge : shape.edges)
    {
        if (!edge)
            continue;

        for (Path* path : edge->paths)
        {
            if (!path)
                continue;

            for (PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;

                for (Point& point : segment->points)
                    point.transform(t);
            }
        }

        // The writer prefers the text an edge was read with, so leaving it in
        // place would save the geometry this just moved.
        edge->data.clear();
    }

    // Bounds are kept in the shape's own space, which is what just changed.
    shape.localBounds.transform(t);
    shape.bounds = shape.localBounds;

    shape.transformationPoint = shape.transformationPoint.transformed(t);

    shape.transform.reset();
}

} // namespace fla
