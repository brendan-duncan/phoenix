#include "layer.h"

namespace fla {

Layer::~Layer()
{
    for (Frame* frame : frames)
    {
        delete frame;
    }
}

} // namespace fla
