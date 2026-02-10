#include "shape.h"

namespace fla {

Shape::Shape()
{}

Shape::~Shape()
{
    for (FillStyle* fill : fills)
    {
        delete fill;
    }
    for (StrokeStyle* stroke : strokes)
    {
        delete stroke;
    }
    for (Edge* edge : edges)
    {
        delete edge;
    }
    fills.clear();
    strokes.clear();
    edges.clear();
    strokesMap.clear();
    fillsMap.clear();
}

} // namespace fla
