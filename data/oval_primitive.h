#include "element.h"
#include "fill_style.h"
#include "stroke_style.h"

namespace fla {

class OvalPrimitive : public Element
{
public:
    Rect rect;
    FillStyle* fillStyle = nullptr;
    StrokeStyle* strokeStyle = nullptr;

    OvalPrimitive() = default;

    ~OvalPrimitive() override;

    Type elementType() const override { return Type::Oval; }

    std::string domTypeName() const override { return "DOMOvalPrimitive"; }
};

} // namespace fla
