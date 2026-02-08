#include "group.h"

Group::Group()
{}

Group::~Group()
{
    for (Element* member : members)
    {
        delete member;
    }
}
