#include "edit_context.h"

namespace fla {

void EditContext::setDocument(FLADocument* document)
{
    // Commands hold pointers into the document they were recorded against, so
    // the history cannot survive a document swap. Clearing also resets the clean
    // state, which is what a freshly loaded file wants.
    _commandStack.clear();
    _document = document;
}

} // namespace fla
