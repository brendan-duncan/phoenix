#include "test_util.h"

#include "../src/gui/toolbox_flow.h"

using toolboxflow::Box;
using toolboxflow::Kind;

namespace {

/// A panel like Phoenix's: tools in sections, then the color well, a line
/// and Object Drawing.
std::vector<Kind> panel(int tools)
{
    std::vector<Kind> items;
    for (int i = 0; i < tools; ++i)
    {
        if (i > 0 && i % 6 == 0)
            items.push_back(Kind::Line);
        items.push_back(Kind::Cell);
    }
    items.push_back(Kind::Line);
    items.push_back(Kind::Span);
    items.push_back(Kind::Line);
    items.push_back(Kind::Cell);
    return items;
}

int wellLength(int breadth)
{
    return toolboxflow::colorWellLength(breadth, true);
}

} // namespace

TEST(toolbox_flow_cells_fill_rows)
{
    // Five cells two across: three rows, the last half full.
    const std::vector<Kind> items(5, Kind::Cell);
    std::vector<Box> boxes;
    const int length = toolboxflow::place(items, 2, 30, 3, true, nullptr, &boxes);
    CHECK(length == 3 + 3 * 30 + 3);
    CHECK(boxes.size() == 5);
    CHECK((boxes[1] == Box{33, 3, 30, 30}));
    CHECK((boxes[4] == Box{3, 63, 30, 30}));
}

TEST(toolbox_flow_line_ends_row)
{
    const std::vector<Kind> items{Kind::Cell, Kind::Line, Kind::Cell};
    std::vector<Box> boxes;
    const int length = toolboxflow::place(items, 2, 30, 3, true, nullptr, &boxes);
    CHECK(length == 3 + 30 + toolboxflow::kLineRoom + 30 + 3);
    // The line runs across both columns, in the middle of its room.
    CHECK((boxes[1] == Box{3, 33 + toolboxflow::kLineRoom / 2, 60, 1}));
    CHECK((boxes[2] == Box{3, 33 + toolboxflow::kLineRoom, 30, 30}));
}

TEST(toolbox_flow_span_takes_breadth)
{
    // A span after a half-full row starts a row of its own and spans every
    // column, as long as it asks for that breadth.
    const std::vector<Kind> items{Kind::Cell, Kind::Span, Kind::Cell};
    std::vector<Box> vertical;
    toolboxflow::place(items, 2, 30, 3, true, wellLength, &vertical);
    CHECK((vertical[1] == Box{3, 33, 60, wellLength(60)}));
    CHECK((vertical[2] == Box{3, 33 + wellLength(60), 30, 30}));
    // Along the top, the same span stands across the rows.
    std::vector<Box> horizontal;
    const auto squareLength = [](int breadth) { return toolboxflow::colorWellLength(breadth, false); };
    toolboxflow::place(items, 1, 30, 3, false, squareLength, &horizontal);
    CHECK((horizontal[1] == Box{33, 3, 30, 30}));
    CHECK((horizontal[2] == Box{63, 3, 30, 30}));
}

TEST(toolbox_flow_columns_needed_for_height)
{
    const std::vector<Kind> items = panel(24);
    const int cell = 30;
    const int one = toolboxflow::place(items, 1, cell, 3, true, wellLength);
    const int two = toolboxflow::place(items, 2, cell, 3, true, wellLength);
    CHECK(two < one);
    // Tall enough, the columns chosen stay; a little short, one more.
    CHECK(toolboxflow::acrossNeeded(items, cell, one, 1, wellLength) == 1);
    CHECK(toolboxflow::acrossNeeded(items, cell, one - 1, 1, wellLength) == 2);
    CHECK(toolboxflow::acrossNeeded(items, cell, two, 1, wellLength) == 2);
    // Never fewer than chosen, however tall.
    CHECK(toolboxflow::acrossNeeded(items, cell, 10000, 3, wellLength) == 3);
    // Hopelessly short: as many as it takes, at most.
    CHECK(toolboxflow::acrossNeeded(items, cell, 50, 1, wellLength) == toolboxflow::kMostAcross);
}

TEST(toolbox_flow_color_well_narrow)
{
    // One column: the buttons in a row on top, the swatches under them,
    // the fill up and left of the stroke and overlapping it.
    const int length = toolboxflow::colorWellLength(30, true);
    CHECK(length == 15 + 2 + 30);
    const toolboxflow::ColorWellParts parts = toolboxflow::colorWellParts(30, length);
    CHECK((parts.blackWhite == Box{0, 0, 15, 15}));
    CHECK((parts.swap == Box{15, 0, 15, 15}));
    CHECK(parts.fill.y >= parts.blackWhite.height);
    CHECK(parts.fill.x < parts.stroke.x && parts.fill.y < parts.stroke.y);
    CHECK(parts.stroke.x < parts.fill.x + parts.fill.width);
    CHECK(parts.stroke.x + parts.stroke.width <= 30);
    CHECK(parts.stroke.y + parts.stroke.height <= length);
}

TEST(toolbox_flow_color_well_wide)
{
    // Two columns: a square, the swatches about 60% of it, the buttons in
    // the corners they leave.
    CHECK(toolboxflow::colorWellLength(60, true) == 60);
    const toolboxflow::ColorWellParts parts = toolboxflow::colorWellParts(60, 60);
    CHECK((parts.fill == Box{0, 0, 36, 36}));
    CHECK((parts.stroke == Box{24, 24, 36, 36}));
    CHECK(parts.swap.x >= 36 && parts.swap.y + parts.swap.height <= 24);
    CHECK(parts.blackWhite.x + parts.blackWhite.width <= 24 && parts.blackWhite.y >= 36);
    // Broad panels keep it a modest size, in the middle.
    CHECK(toolboxflow::colorWellLength(200, true) == toolboxflow::kWellMost);
    const toolboxflow::ColorWellParts broad = toolboxflow::colorWellParts(200, toolboxflow::kWellMost);
    CHECK(broad.fill.x == (200 - toolboxflow::kWellMost) / 2);
    // Along the top, a square block as tall as the bar.
    CHECK(toolboxflow::colorWellLength(36, false) == 36);
    const toolboxflow::ColorWellParts bar = toolboxflow::colorWellParts(36, 36);
    CHECK(bar.fill.x == 0 && bar.stroke.x + bar.stroke.width == 36);
}
