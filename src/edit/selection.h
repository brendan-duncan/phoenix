#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace fla {

class DOMElement;

/// What the user currently has selected.
///
/// Holds DOMElement rather than Element so that one model serves both the stage,
/// which selects whole elements, and the document tree, which can select the
/// edges and paths inside a shape.
///
/// Entries are borrowed, never owned. Anything that deletes document objects
/// must clear the selection first.
///
/// Free of Qt, so tools and the editing core can use it without the GUI.
class Selection
{
public:
    Selection() = default;

    Selection(const Selection&) = delete;
    Selection& operator=(const Selection&) = delete;

    /// Replaces the selection with \a element. A null element clears it.
    void select(DOMElement* element);

    /// Replaces the selection with \a elements.
    void select(const std::vector<DOMElement*>& elements);

    /// Adds \a element, keeping what is already selected. Ignores duplicates.
    void add(DOMElement* element);

    void remove(DOMElement* element);

    /// Adds \a element if it is not selected, removes it if it is. This is what
    /// shift-clicking does.
    void toggle(DOMElement* element);

    void clear();

    bool contains(const DOMElement* element) const;

    bool isEmpty() const { return _elements.empty(); }

    size_t count() const { return _elements.size(); }

    const std::vector<DOMElement*>& elements() const { return _elements; }

    /// The only selected item, or null unless exactly one thing is selected.
    /// Panels that show properties for a single object use this.
    DOMElement* single() const { return _elements.size() == 1 ? _elements.front() : nullptr; }

    /// Invoked whenever the selection changes, so views can repaint and menus
    /// can update. Never invoked when a call leaves the selection as it was.
    void setChangedCallback(std::function<void()> callback)
    {
        _changedCallback = std::move(callback);
    }

private:
    void notifyChanged() const
    {
        if (_changedCallback)
            _changedCallback();
    }

    /// Selection order is meaningful -- it is the order things were picked -- so
    /// this is a vector rather than a set.
    std::vector<DOMElement*> _elements;

    std::function<void()> _changedCallback;
};

} // namespace fla
