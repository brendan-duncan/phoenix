#include "group.h"

namespace fla {

Group::Group()
{}

Group::~Group()
{
    for (Element* member : members)
    {
        delete member;
    }
}

} // namespace fla
