#include "stroke_style.h"

namespace fla {

StrokeStyle::StrokeStyle()
    : stroke(nullptr)
{}

StrokeStyle::~StrokeStyle()
{
    delete stroke;
}

Stroke::Stroke()
    : weight(1.0)
    , fill(nullptr)
{}

Stroke::~Stroke()
{
    delete fill;
}

SolidStroke::SolidStroke()
    : Stroke()
{}

DashedStroke::DashedStroke()
    : Stroke()
{}

RaggedStroke::RaggedStroke()
    : Stroke()
{}

StippleStroke::StippleStroke()
    : Stroke()
{}

DottedStroke::DottedStroke()
    : Stroke()
{}

} // namespace fla
