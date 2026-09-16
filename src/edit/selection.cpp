#include "selection.h"

#include "../data/dom_element.h"

#include <algorithm>

namespace fla {

void Selection::select(DOMElement* element)
{
    if (!element)
    {
        clear();
        return;
    }

    // Re-selecting the only selected item changes nothing, so stay quiet rather
    // than making every click look like a selection change.
    if (_elements.size() == 1 && _elements.front() == element)
        return;

    _elements.clear();
    _elements.push_back(element);
    notifyChanged();
}

void Selection::select(const std::vector<DOMElement*>& elements)
{
    std::vector<DOMElement*> next;
    next.reserve(elements.size());
    for (DOMElement* element : elements)
    {
        if (!element)
            continue;
        if (std::find(next.begin(), next.end(), element) == next.end())
            next.push_back(element);
    }

    if (next == _elements)
        return;

    _elements = std::move(next);
    notifyChanged();
}

void Selection::add(DOMElement* element)
{
    if (!element || contains(element))
        return;

    _elements.push_back(element);
    notifyChanged();
}

void Selection::remove(DOMElement* element)
{
    const auto it = std::find(_elements.begin(), _elements.end(), element);
    if (it == _elements.end())
        return;

    _elements.erase(it);
    notifyChanged();
}

void Selection::toggle(DOMElement* element)
{
    if (!element)
        return;

    if (contains(element))
        remove(element);
    else
        add(element);
}

void Selection::clear()
{
    if (_elements.empty())
        return;

    _elements.clear();
    notifyChanged();
}

bool Selection::contains(const DOMElement* element) const
{
    return std::find(_elements.begin(), _elements.end(), element) != _elements.end();
}

} // namespace fla
