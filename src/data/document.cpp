#include "document.h"

#include "frame.h"
#include "group.h"
#include "layer.h"
#include "symbol.h"
#include "symbol_list.h"
#include "timeline.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace fla {

int Document::currentSceneIndex() const
{
    if (timelines.empty())
        return -1;
    const int named = std::atoi(currentTimeline.c_str()) - 1;
    return named >= 0 && named < static_cast<int>(timelines.size()) ? named : 0;
}

std::unordered_set<const DOMElement*> Document::reachable() const
{
    std::unordered_set<const DOMElement*> found;
    std::vector<const Element*> pending;
    const auto timeline = [&](const Timeline* timeline) {
        if (!timeline || !found.insert(timeline).second)
            return;
        for (const Layer* layer : timeline->layers)
        {
            if (!layer)
                continue;
            found.insert(layer);
            for (const Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                found.insert(frame);
                pending.insert(pending.end(), frame->elements.begin(), frame->elements.end());
            }
        }
    };
    for (const Timeline* scene : timelines)
        timeline(scene);
    if (symbolList)
        for (const Symbol* symbol : symbolList->symbols)
            if (symbol && found.insert(symbol).second)
                for (const Timeline* t : symbol->timelines)
                    timeline(t);
    while (!pending.empty())
    {
        const Element* element = pending.back();
        pending.pop_back();
        if (!element || !found.insert(element).second)
            continue;
        if (element->elementType() == Element::Type::Group)
        {
            const Group* group = static_cast<const Group*>(element);
            pending.insert(pending.end(), group->members.begin(), group->members.end());
        }
    }
    return found;
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
