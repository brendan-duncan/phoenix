#pragma once

#include "dom_element.h"
#include "resource.h"
#include "timeline.h"
#include <vector>
#include <string>

namespace fla {

class Symbol : public Resource
{
public:
    std::string name;
    std::string itemId;
    std::string lastModified;
    /// Path of this symbol's file inside LIBRARY/, as the document's Include
    /// referred to it. Usually the name plus ".xml", but not necessarily, so it
    /// is kept rather than derived.
    std::string href;

    /// How new instances behave by default: the symbol's own type as the
    /// library shows it.
    SymbolType symbolType = SymbolType::Graphic;

    /// 9-slice scaling guides, in the symbol's own space: the two vertical
    /// and two horizontal lines that split it into corners that never
    /// scale, edges that scale one way and a middle that scales both.
    /// Present only when the symbol has them switched on.
    bool scale9 = false;
    double scaleGridLeft = 0.0;
    double scaleGridTop = 0.0;
    double scaleGridRight = 0.0;
    double scaleGridBottom = 0.0;

    std::vector<Timeline*> timelines;

    Symbol(DOMElement* parent)
        : Resource(parent)
    {}

    ~Symbol() override;

    Type resourceType() const override { return Type::Symbol; }

    std::string domTypeName() const override { return "Symbol"; }

    DOMType domType() const override { return DOMType::Symbol; }

    static DOMType staticDomType() { return DOMType::Symbol; }
};

} // namespace fla
