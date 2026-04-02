#include "timeline.h"

namespace fla {

Timeline::~Timeline()
{
    // Clean up dynamically allocated layers
    for (Layer* layer : layers)
    {
        delete layer;
    }
    layers.clear();
}

} // namespace fla
