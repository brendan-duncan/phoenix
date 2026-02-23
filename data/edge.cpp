#include "edge.h"

namespace fla {

Path::~Path()
{
    for (fla::PathSegment* segment : segments)
    {
        delete segment;
    }
}

Edge::~Edge()
{
    for (fla::Path* path : paths)
    {
        delete path;
    }
}

} // namespace fla
