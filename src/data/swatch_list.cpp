#include "swatch_list.h"

namespace fla {

SwatchList::~SwatchList()
{
    for (Swatch* swatch : swatches)
    {
        delete swatch;
    }
}

} // namespace fla
