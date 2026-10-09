#pragma once

#include "../data/element.h"
#include "../data/group.h"
#include "../edit/selection.h"

#include <vector>

/// The elements a transform of the selection actually moves.
///
/// A group carries no transform of its own: the file stores each member's
/// matrix in the parent's space, and the renderer draws them as they are. So
/// moving a group means moving every member, down through nested groups, while
/// the selection and the highlight stay on the group itself.
inline void collectTransformLeaves(fla::Element* element, std::vector<fla::Element*>& out)
{
    if (!element)
        return;

    if (element->elementType() == fla::Element::Type::Group)
    {
        for (fla::Element* member : static_cast<fla::Group*>(element)->members)
            collectTransformLeaves(member, out);
        return;
    }

    out.push_back(element);
}

inline std::vector<fla::Element*> transformLeaves(const fla::Selection& selection)
{
    std::vector<fla::Element*> leaves;
    for (fla::DOMElement* selected : selection.elements())
        collectTransformLeaves(dynamic_cast<fla::Element*>(selected), leaves);
    return leaves;
}

/// The selected elements that sit directly in a frame, in selection order.
inline std::vector<fla::Element*> topLevelElements(const fla::Selection& selection)
{
    std::vector<fla::Element*> elements;
    for (fla::DOMElement* selected : selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (element)
            elements.push_back(element);
    }
    return elements;
}
