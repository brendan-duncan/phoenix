#include "fla_document.h"

namespace fla {

FLADocument::~FLADocument()
{
    if (document)
    {
        delete document;
        document = nullptr;
    }
}

} // namespace fla
