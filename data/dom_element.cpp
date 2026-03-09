#include "dom_element.h"
#include "element.h"

namespace fla {

const Element* DOMElement::findParentElement() const
{
    const DOMElement* current = this;
    while (current)
    {
        if (current->isElementType())
            return static_cast<const Element*>(current);
        current = current->parent;
    }
    return nullptr;
}

} // namespace fla
