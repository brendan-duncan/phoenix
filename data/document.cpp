#include "document.h"

namespace fla {

Document::~Document()
{
    // Clean up dynamically allocated symbols
    for (Symbol* symbol : symbols)
    {
        delete symbol;
    }
    symbols.clear();

    // Clean up dynamically allocated timelines
    for (Timeline* timeline : timelines)
    {
        delete timeline;
    }
    timelines.clear();

    // Clean up dynamically allocated folders
    for (Folder* folder : folders)
    {
        delete folder;
    }
    folders.clear();

    symbolMap.clear();
}

} // namespace fla
