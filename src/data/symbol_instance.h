#pragma once

#include "element.h"
#include "point.h"
#include "color_transform.h"
#include "filter.h"
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Symbol;

class SymbolInstance : public Element
{
public:
    std::string libraryItemName;
    /// The instance name scripts address it by; movie clips and buttons.
    std::string name;
    int firstFrame = 0;
    /// The last frame a graphic plays to (the Last setting), or -1 for the
    /// symbol's end.
    int lastFrame = -1;
    SymbolType symbolType = SymbolType::Graphic;
    LoopType loopType = LoopType::PlayOnce;
    ColorTransform colorTransform;
    /// The matrix the instance had when it was read, when the file gave it
    /// a 3D center point (centerPoint3DX/Y, kept among its attributes):
    /// saving moves the point with the instance, as the FLA keeps it, by
    /// how far the instance has moved since.
    bool hasReadMatrix = false;
    Transform readMatrix;

    /// extraAttributes as a save writes them: with the 3D center point
    /// moved by however the instance has moved since it was read.
    std::vector<std::pair<std::string, std::string>> keptAttributes() const;

    /// The 3D matrix the file placed the instance with in place of a 2D
    /// one (matrix3D: sixteen numbers, column by column, its move in
    /// twips), as a clip moved in 3D keeps it; false when it has none.
    /// Such an instance keeps its transformation point in its parent's
    /// space in the file; Phoenix reads it into the symbol's, as for any
    /// other instance, and a save writes both back the file's way.
    bool matrix3D(double values[16]) const;

    /// The 2D matrix matrix3D \a values place the instance with: its x and
    /// y columns and its move, in pixels.
    static Transform transformOf3D(const double values[16]);

    /// How the instance is composited over what is under it, as the file
    /// names it: normal, layer, multiply, screen, lighten, darken,
    /// difference, add, subtract, invert, alpha, erase, overlay, hardlight.
    /// Empty is normal.
    std::string blendMode;

    /// A symbol type the reader does not model ("puppet", for a rigged
    /// asset), kept as spelled so it is written back; the instance draws as
    /// a graphic meanwhile.
    std::string otherSymbolType;

    /// Whether the file wrote a color element even with no effect in it, so
    /// a save writes it back.
    bool keepColor = false;

    /// Filter elements Phoenix does not know, as XML text, written back
    /// after the others.
    std::vector<std::string> extraFilters;

    /// The filters applied to the instance, in order.
    std::vector<Filter> filters;

    Symbol* symbol = nullptr; // Resolved symbol reference after parsing

    SymbolInstance(DOMElement* parent)
        : Element(parent)
    {}

    Element::Type elementType() const override { return Element::Type::SymbolInstance; }

    std::string domTypeName() const override { return "SymbolInstance"; }

    DOMType domType() const override { return DOMType::SymbolInstance; }

    static DOMType staticDomType() { return DOMType::SymbolInstance; }
};

} // namespace fla
