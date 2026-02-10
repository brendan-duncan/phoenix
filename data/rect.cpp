#include "rect.h"
#include <algorithm>

namespace fla {

void Rect::expandToInclude(const Rect& other)
{
    topLeft.x = std::min(topLeft.x, other.topLeft.x);
    topLeft.y = std::min(topLeft.y, other.topLeft.y);
    bottomRight.x = std::max(bottomRight.x, other.bottomRight.x);
    bottomRight.y = std::max(bottomRight.y, other.bottomRight.y);
}

} // namespace fla
