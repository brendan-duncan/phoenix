#include "timeline.h"

Timeline::Timeline()
{}

Timeline::~Timeline()
{
    // Clean up dynamically allocated layers
    for (Layer* layer : layers)
    {
        delete layer;
    }
    layers.clear();
}
