#include "frame.h"

namespace fla {

Frame::Frame()
{}

Frame::~Frame()
{
    if (actionScript)
    {
        delete actionScript;
        actionScript = nullptr;
    }
    for (Element* element : elements)
    {
        delete element;
    }
    elements.clear();
}

} // namespace fla
