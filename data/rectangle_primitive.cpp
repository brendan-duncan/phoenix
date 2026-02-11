#include "rectangle_primitive.h"

namespace fla {

RectanglePrimitive::~RectanglePrimitive()
{
    delete fillStyle;
    delete strokeStyle;
}

} // namespace fla
