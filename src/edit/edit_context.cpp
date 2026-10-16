#include "edit_context.h"

namespace fla {

void EditContext::setDocument(FLADocument* document)
{
    // Commands hold pointers into the document they were recorded against, so
    // the history cannot survive a document swap. Clearing also resets the clean
    // state, which is what a freshly loaded file wants.
    // The new document first: clearing tells everyone the history changed,
    // and what they look at then must not be the one going away.
    _document = document;
    _commandStack.clear();
}

} // namespace fla
