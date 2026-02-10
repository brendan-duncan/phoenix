#include "layer.h"

namespace fla {

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

} // namespace fla
