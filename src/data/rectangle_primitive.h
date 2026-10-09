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

    /// Corner radii, in pixels. A negative radius is an inward curve.
    double topLeftRadius = 0.0;
    double topRightRadius = 0.0;
    double bottomLeftRadius = 0.0;
    double bottomRightRadius = 0.0;

    RectanglePrimitive(DOMElement* parent)
        : Element(parent)
    {}

    ~RectanglePrimitive() override;

    Type elementType() const override { return Type::Rectangle; }

    std::string domTypeName() const override { return "DOMRectanglePrimitive"; }

    DOMType domType() const override { return DOMType::RectanglePrimitive; }

    static DOMType staticDomType() { return DOMType::RectanglePrimitive; }
};

}
