#include "element.h"
#include "fill_style.h"
#include "stroke_style.h"

namespace fla {

class OvalPrimitive : public Element
{
public:
    Rect rect;
    double startAngle = 0.0;
    double endAngle = 0.0;
    double innerRadius = 0.0;
    FillStyle* fillStyle = nullptr;
    StrokeStyle* strokeStyle = nullptr;

    OvalPrimitive() = default;

    ~OvalPrimitive() override;

    Type elementType() const override { return Type::Oval; }

    std::string domTypeName() const override { return "DOMOvalPrimitive"; }
};

} // namespace fla
