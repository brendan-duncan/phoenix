#include "frame.h"

namespace fla {

Frame::~Frame()
{
    if (actionScript)
    {
        delete actionScript;
        actionScript = nullptr;
    }
    if (morphShape)
    {
        delete morphShape;
        morphShape = nullptr;
    }
    for (Element* element : elements)
    {
        delete element;
    }
    elements.clear();
}

} // namespace fla
