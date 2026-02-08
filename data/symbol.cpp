#include "symbol.h"

Symbol::Symbol()
{}

Symbol::~Symbol()
{
    for (Timeline* timeline : timelines)
    {
        delete timeline;
    }
    timelines.clear();
}
