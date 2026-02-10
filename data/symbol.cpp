#include "symbol.h"

namespace fla {

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

} // namespace fla
