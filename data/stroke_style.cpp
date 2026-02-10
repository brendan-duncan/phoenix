#include "stroke_style.h"

namespace fla {

StrokeStyle::StrokeStyle()
    : weight(1.0)
    , fill(nullptr)
{}

StrokeStyle::~StrokeStyle()
{
    delete fill;
}

SolidStroke::SolidStroke()
    : StrokeStyle()
{}

DashedStroke::DashedStroke()
    : StrokeStyle()
{}

RaggedStroke::RaggedStroke()
    : StrokeStyle()
{}

StippleStroke::StippleStroke()
    : StrokeStyle()
{}

DottedStroke::DottedStroke()
    : StrokeStyle()
{}

} // namespace fla
