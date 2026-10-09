#include "fill_attribution.h"

#include <cmath>

namespace fla {

bool outlineContains(const std::vector<Curve>& outline, const Point& point)
{
    // Exact: each curve counts where it crosses a ray from the point.
    int crossings = 0;
    for (const Curve& curve : outline)
        crossings += curve.horizontalRayCrossings(point);
    return (crossings % 2) == 1;
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
