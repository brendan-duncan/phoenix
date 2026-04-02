#include "oval_primitive.h"

namespace fla {

OvalPrimitive::~OvalPrimitive()
{
    delete fillStyle;
    delete strokeStyle;
}

} // namespace fla
