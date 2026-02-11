#include "element.h"
#include "fill_style.h"
#include "stroke_style.h"

namespace fla {

class RectanglePrimitive : public Element
{
public:
    Rect rect;
    FillStyle* fillStyle = nullptr;
    StrokeStyle* strokeStyle = nullptr;

    RectanglePrimitive() = default;

    ~RectanglePrimitive() override;

    Type elementType() const override { return Type::Rectangle; }

    std::string domTypeName() const override { return "DOMRectanglePrimitive"; }
};

}
