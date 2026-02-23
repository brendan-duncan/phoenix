#include "document.h"

namespace fla {

Document::~Document()
{
    if (symbolList)
    {
        delete symbolList;
        symbolList = nullptr;
    }

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
}

} // namespace fla
