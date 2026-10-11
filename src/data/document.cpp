#include "document.h"

#include <cstdlib>
#include <string>

namespace fla {

int Document::currentSceneIndex() const
{
    if (timelines.empty())
        return -1;
    const int named = std::atoi(currentTimeline.c_str()) - 1;
    return named >= 0 && named < static_cast<int>(timelines.size()) ? named : 0;
}

Timeline* Document::currentScene() const
{
    const int index = currentSceneIndex();
    return index >= 0 ? timelines[static_cast<size_t>(index)] : nullptr;
}

void Document::setCurrentScene(int index)
{
    if (index >= 0 && index < static_cast<int>(timelines.size()))
        currentTimeline = std::to_string(index + 1);
}

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
