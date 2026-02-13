#include "stroke_style.h"

namespace fla {

StrokeStyle::~StrokeStyle()
{
    delete fill;
}

} // namespace fla
