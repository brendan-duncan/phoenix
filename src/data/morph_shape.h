#pragma once

#include "dom_element.h"
#include "morph_segment.h"
#include <string>
#include <vector>

namespace fla {

class MorphShape : public DOMElement
{
public:
    std::vector<MorphSegment*> segments;

    /// The shape hints (morphHintsList) as read, written back as they came.
    std::string hintsXml;

    MorphShape(fla::DOMElement* parent)
        : DOMElement(parent)
    {}

    ~MorphShape() override
    {
        for (auto* segment : segments)
        {
            delete segment;
        }
    }

    std::string domTypeName() const override { return "MorphShape"; }

    DOMType domType() const override { return DOMType::MorphShape; }

    static DOMType staticDomType() { return DOMType::MorphShape; }
};

} // namespace fla
