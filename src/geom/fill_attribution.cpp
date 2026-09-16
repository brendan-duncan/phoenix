#include "fill_attribution.h"

#include <cmath>

namespace fla {

namespace {

/// Points sampled per curve when flattening an outline for a containment test.
constexpr int kOutlineSamples = 8;

} // namespace

bool outlineContains(const std::vector<Curve>& outline, const Point& point)
{
    std::vector<Point> flattened;
    flattened.reserve(outline.size() * kOutlineSamples);

    for (const Curve& curve : outline)
    {
        for (int i = 0; i < kOutlineSamples; ++i)
            flattened.push_back(curve.pointAt(static_cast<double>(i) / kOutlineSamples));
    }

    if (flattened.size() < 3)
        return false;

    bool inside = false;
    for (size_t i = 0, j = flattened.size() - 1; i < flattened.size(); j = i++)
    {
        const Point& a = flattened[i];
        const Point& b = flattened[j];

        if ((a.y > point.y) == (b.y > point.y))
            continue;

        const double x = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
        if (point.x < x)
            inside = !inside;
    }

    return inside;
}

void attributeFills(PlanarMap& map, const std::vector<FillRegion>& regions)
{
    for (size_t face = 0; face < map.faces().size(); ++face)
    {
        const int index = static_cast<int>(face);

        if (map.faces()[face].unbounded)
        {
            // Nothing paints the outside.
            map.setFaceFill(index, -1);
            continue;
        }

        Point inside;
        if (!map.interiorPoint(index, inside))
        {
            map.setFaceFill(index, -1);
            continue;
        }

        // Later regions paint over earlier ones, so the last one covering this
        // point decides. An outline with no fill still counts as covering: it
        // wipes what was under it rather than letting the older fill show
        // through a shape drawn on top.
        int fill = -1;
        for (const FillRegion& region : regions)
        {
            if (outlineContains(region.outline, inside))
                fill = region.fillStyle;
        }

        map.setFaceFill(index, fill);
    }
}

} // namespace fla
