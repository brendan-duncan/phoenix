#pragma once

#include <algorithm>
#include <functional>
#include <vector>

/// How the Tools panel lays out, free of Qt so the tests can check it:
/// items in square cells flowing down a side (rows as wide as the panel)
/// or along the top or bottom (columns as tall as it), lines between
/// sections, and the color well spanning the panel's breadth.
namespace toolboxflow {

constexpr int kMargin = 3;
/// The room a line between sections takes along the flow.
constexpr int kLineRoom = 7;
/// The most columns (or rows) the panel takes.
constexpr int kMostAcross = 8;

enum class Kind
{
    Cell,
    Line,
    /// Across the whole breadth, as long along the flow as it asks.
    Span
};

/// A rectangle in the panel.
struct Box
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const Box& other) const
    {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

/// Places \a items \a across cells of \a cell across, the first cell
/// \a start in from the panel's edge, and returns the length along the flow
/// they take, margins and all. A line ends the row (or column) and runs
/// across the panel; so does a span, whose length \a spanLength gives for
/// its breadth. \a boxes, when given, gets each item's place.
inline int place(const std::vector<Kind>& items, int across, int cell, int start, bool vertical,
    const std::function<int(int)>& spanLength, std::vector<Box>* boxes = nullptr)
{
    across = std::max(across, 1);
    const int breadth = across * cell;
    const auto box = [vertical](int along, int at, int length, int width) {
        return vertical ? Box{at, along, width, length} : Box{along, at, length, width};
    };
    if (boxes)
        boxes->clear();
    int along = kMargin;
    int index = 0;
    for (Kind kind : items)
    {
        if (kind != Kind::Cell && index > 0)
        {
            along += cell;
            index = 0;
        }
        if (kind == Kind::Line)
        {
            if (boxes)
                boxes->push_back(box(along + kLineRoom / 2, start, 1, breadth));
            along += kLineRoom;
            continue;
        }
        if (kind == Kind::Span)
        {
            const int length = spanLength ? spanLength(breadth) : cell;
            if (boxes)
                boxes->push_back(box(along, start, length, breadth));
            along += length;
            continue;
        }
        if (boxes)
            boxes->push_back(box(along, start + index * cell, cell, cell));
        if (++index == across)
        {
            index = 0;
            along += cell;
        }
    }
    if (index > 0)
        along += cell;
    return along + kMargin;
}

/// The fewest cells across, \a from or more, at which \a items fit in
/// \a room along the flow; kMostAcross when none do.
inline int acrossNeeded(const std::vector<Kind>& items, int cell, int room, int from,
    const std::function<int(int)>& spanLength)
{
    for (int across = std::max(from, 1); across < kMostAcross; ++across)
        if (place(items, across, cell, kMargin, true, spanLength) <= room)
            return across;
    return std::max(from, kMostAcross);
}

/// The color well's parts: the fill and stroke swatches, overlapping, and
/// the small Black and White and Swap buttons.
struct ColorWellParts
{
    Box fill;
    Box stroke;
    Box blackWhite;
    Box swap;
};

/// Narrower than this, down a side, the well stacks its buttons over the
/// swatches.
constexpr int kWellNarrow = 44;
/// The largest the well grows, however broad the panel.
constexpr int kWellMost = 84;
/// The small buttons' size, at most.
constexpr int kWellButton = 16;

/// The length along the flow the color well takes for \a breadth across:
/// one column down a side, a row of buttons over the swatches; else a
/// square.
inline int colorWellLength(int breadth, bool vertical)
{
    if (vertical && breadth < kWellNarrow)
        return std::min(kWellButton, breadth / 2) + 2 + breadth;
    return std::min(breadth, kWellMost);
}

/// The color well's parts in a well \a width by \a height.
inline ColorWellParts colorWellParts(int width, int height)
{
    ColorWellParts parts;
    const auto percent = [](int length, int part) { return (length * part + 50) / 100; };
    if (width < kWellNarrow && height > width)
    {
        // Narrow: Black and White at the top left, Swap at the top right,
        // and the swatches under them, the fill up and left of the stroke.
        const int button = std::min(kWellButton, width / 2);
        parts.blackWhite = {0, 0, button, button};
        parts.swap = {width - button, 0, button, button};
        const int top = button + 2;
        const int side = std::max(1, std::min(width, height - top));
        const int left = (width - side) / 2;
        const int square = percent(side, 68);
        parts.fill = {left, top, square, square};
        parts.stroke = {left + side - square, top + side - square, square, square};
        return parts;
    }
    // Square: the fill at the top left, the stroke at the bottom right,
    // Swap in the corner they leave at the top right and Black and White
    // in the one at the bottom left.
    const int side = std::max(1, std::min({width, height, kWellMost}));
    const int left = (width - side) / 2;
    const int top = (height - side) / 2;
    const int square = percent(side, 60);
    const int corner = side - square;
    const int button = std::min(kWellButton, corner);
    const int inset = (corner - button) / 2;
    parts.fill = {left, top, square, square};
    parts.stroke = {left + corner, top + corner, square, square};
    parts.swap = {left + square + inset, top + inset, button, button};
    parts.blackWhite = {left + inset, top + square + inset, button, button};
    return parts;
}

} // namespace toolboxflow
