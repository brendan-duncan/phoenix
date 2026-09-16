#include "test_util.h"

#include "../src/data/group.h"
#include "../src/edit/selection.h"

#include <vector>

using fla::DOMElement;
using fla::Selection;

namespace {

/// Selection only stores pointers, so any DOMElement will do as a stand-in.
/// Group is concrete and cheap to construct.
class TestElement : public fla::Group
{
public:
    TestElement()
        : fla::Group(nullptr)
    {}
};

} // namespace

TEST(selection_starts_empty)
{
    Selection selection;

    CHECK(selection.isEmpty());
    CHECK(selection.count() == 0);
    CHECK(selection.single() == nullptr);
}

TEST(selection_select_replaces)
{
    TestElement a, b;
    Selection selection;

    selection.select(&a);
    CHECK(selection.count() == 1);
    CHECK(selection.contains(&a));
    CHECK(selection.single() == &a);

    selection.select(&b);
    CHECK(selection.count() == 1);
    CHECK(!selection.contains(&a));
    CHECK(selection.contains(&b));
}

TEST(selection_select_null_clears)
{
    TestElement a;
    Selection selection;
    selection.select(&a);

    selection.select(static_cast<DOMElement*>(nullptr));

    CHECK(selection.isEmpty());
}

TEST(selection_add_accumulates_and_ignores_duplicates)
{
    TestElement a, b;
    Selection selection;

    selection.add(&a);
    selection.add(&b);
    selection.add(&a);

    CHECK(selection.count() == 2);
    CHECK(selection.contains(&a));
    CHECK(selection.contains(&b));
    // More than one thing selected, so there is no single item.
    CHECK(selection.single() == nullptr);
}

TEST(selection_keeps_pick_order)
{
    TestElement a, b, c;
    Selection selection;

    selection.add(&c);
    selection.add(&a);
    selection.add(&b);

    const std::vector<DOMElement*>& elements = selection.elements();
    CHECK(elements.size() == 3);
    if (elements.size() != 3)
        return;
    CHECK(elements[0] == &c);
    CHECK(elements[1] == &a);
    CHECK(elements[2] == &b);
}

TEST(selection_toggle_adds_then_removes)
{
    TestElement a;
    Selection selection;

    selection.toggle(&a);
    CHECK(selection.contains(&a));

    selection.toggle(&a);
    CHECK(!selection.contains(&a));
    CHECK(selection.isEmpty());
}

TEST(selection_remove_ignores_what_is_not_selected)
{
    TestElement a, b;
    Selection selection;
    selection.add(&a);

    selection.remove(&b);

    CHECK(selection.count() == 1);
    CHECK(selection.contains(&a));
}

TEST(selection_select_many_drops_duplicates_and_nulls)
{
    TestElement a, b;
    Selection selection;

    selection.select(std::vector<DOMElement*>{&a, &b, &a, nullptr});

    CHECK(selection.count() == 2);
    CHECK(selection.contains(&a));
    CHECK(selection.contains(&b));
}

TEST(selection_notifies_only_on_real_changes)
{
    TestElement a, b;
    Selection selection;
    int notifications = 0;
    selection.setChangedCallback([&notifications]() { ++notifications; });

    selection.select(&a);
    CHECK(notifications == 1);

    // Re-picking what is already the sole selection is not a change, so clicking
    // the same object twice must not churn the menus and repaint.
    selection.select(&a);
    CHECK(notifications == 1);

    selection.add(&a);
    CHECK(notifications == 1);

    selection.add(&b);
    CHECK(notifications == 2);

    selection.remove(&b);
    CHECK(notifications == 3);

    selection.clear();
    CHECK(notifications == 4);

    // Clearing an empty selection changes nothing.
    selection.clear();
    CHECK(notifications == 4);
}

TEST(selection_select_many_is_quiet_when_unchanged)
{
    TestElement a, b;
    Selection selection;
    selection.select(std::vector<DOMElement*>{&a, &b});

    int notifications = 0;
    selection.setChangedCallback([&notifications]() { ++notifications; });

    selection.select(std::vector<DOMElement*>{&a, &b});
    CHECK(notifications == 0);

    // Order is part of the selection, so a different order is a change.
    selection.select(std::vector<DOMElement*>{&b, &a});
    CHECK(notifications == 1);
}
