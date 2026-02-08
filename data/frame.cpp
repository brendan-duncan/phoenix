#include "frame.h"

Frame::Frame()
{}

Frame::~Frame()
{
    for (Element* element : elements)
    {
        delete element;
    }
    elements.clear();
}
