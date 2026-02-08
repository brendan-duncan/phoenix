#include "layer.h"

Layer::Layer()
{
}

Layer::~Layer()
{
    for (Frame* frame : frames)
    {
        delete frame;
    }
}
