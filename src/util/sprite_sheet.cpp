#include "sprite_sheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace sprites {

namespace {

int roundUpToPowerOfTwo(int value)
{
    int power = 1;
    while (power < value)
        power *= 2;
    return power;
}

/// Whether two frames' kept pixels are the same.
bool samePixels(const Frame& a, const Placed& pa, const Frame& b, const Placed& pb)
{
    if (pa.width != pb.width || pa.height != pb.height || pa.offsetX != pb.offsetX || pa.offsetY != pb.offsetY ||
        pa.sourceWidth != pb.sourceWidth || pa.sourceHeight != pb.sourceHeight)
        return false;
    for (int y = 0; y < pa.height; ++y)
    {
        const size_t rowA = (static_cast<size_t>(pa.offsetY + y) * a.width + pa.offsetX) * 4;
        const size_t rowB = (static_cast<size_t>(pb.offsetY + y) * b.width + pb.offsetX) * 4;
        if (!std::equal(a.rgba.begin() + static_cast<long long>(rowA),
                a.rgba.begin() + static_cast<long long>(rowA + static_cast<size_t>(pa.width) * 4),
                b.rgba.begin() + static_cast<long long>(rowB)))
            return false;
    }
    return true;
}

std::string escaped(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) < 0x20)
            continue;
        out += c;
    }
    return out;
}

} // namespace

void trimBounds(const Frame& frame, int& x, int& y, int& width, int& height)
{
    int left = frame.width, top = frame.height, right = -1, bottom = -1;
    for (int row = 0; row < frame.height; ++row)
    {
        for (int column = 0; column < frame.width; ++column)
        {
            if (frame.rgba[(static_cast<size_t>(row) * frame.width + column) * 4 + 3] == 0)
                continue;
            left = std::min(left, column);
            right = std::max(right, column);
            top = std::min(top, row);
            bottom = std::max(bottom, row);
        }
    }
    if (right < 0)
    {
        x = y = width = height = 0;
        return;
    }
    x = left;
    y = top;
    width = right - left + 1;
    height = bottom - top + 1;
}

Sheet layout(const std::vector<Frame>& frames, const Options& options)
{
    Sheet sheet;
    const int padding = std::max(0, options.padding);
    const int border = std::max(0, options.border);
    // What of each frame is kept, and which are copies.
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const Frame& frame = frames[i];
        Placed placed;
        placed.sourceWidth = frame.width;
        placed.sourceHeight = frame.height;
        placed.width = frame.width;
        placed.height = frame.height;
        if (options.trim)
        {
            trimBounds(frame, placed.offsetX, placed.offsetY, placed.width, placed.height);
            // A clear frame keeps one clear pixel, so it has a place.
            if (placed.width == 0)
            {
                placed.width = placed.height = 1;
            }
        }
        if (options.dedupe)
        {
            for (size_t j = 0; j < i; ++j)
            {
                if (sheet.frames[j].sameAs < 0 && samePixels(frame, placed, frames[j], sheet.frames[j]))
                {
                    placed.sameAs = static_cast<int>(j);
                    break;
                }
            }
        }
        sheet.frames.push_back(placed);
    }
    std::vector<size_t> unique;
    for (size_t i = 0; i < sheet.frames.size(); ++i)
        if (sheet.frames[i].sameAs < 0)
            unique.push_back(i);
    if (unique.empty())
        return sheet;

    int width = 0, height = 0;
    if (options.layout == Layout::Grid)
    {
        int cellWidth = 0, cellHeight = 0;
        for (size_t i : unique)
        {
            cellWidth = std::max(cellWidth, sheet.frames[i].width);
            cellHeight = std::max(cellHeight, sheet.frames[i].height);
        }
        const int count = static_cast<int>(unique.size());
        const int columns = options.columns > 0 ? std::min(options.columns, count)
                                                : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
        const int rows = (count + columns - 1) / columns;
        for (int n = 0; n < count; ++n)
        {
            Placed& placed = sheet.frames[unique[static_cast<size_t>(n)]];
            placed.x = border + (n % columns) * (cellWidth + padding);
            placed.y = border + (n / columns) * (cellHeight + padding);
        }
        width = border * 2 + columns * cellWidth + (columns - 1) * padding;
        height = border * 2 + rows * cellHeight + (rows - 1) * padding;
    }
    else if (options.layout == Layout::Strip)
    {
        int x = border;
        for (size_t i : unique)
        {
            Placed& placed = sheet.frames[i];
            placed.x = x;
            placed.y = border;
            x += placed.width + padding;
            height = std::max(height, placed.height);
        }
        width = x - padding + border;
        height += border * 2;
    }
    else
    {
        // Shelves: tallest first, each row filled left to right until the
        // next frame would pass the width.
        std::vector<size_t> order = unique;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return sheet.frames[a].height > sheet.frames[b].height;
        });
        int widest = 0;
        for (size_t i : order)
            widest = std::max(widest, sheet.frames[i].width);
        const int limit = std::max(options.maxWidth - border * 2, widest);
        int x = 0, y = 0, shelf = 0;
        for (size_t i : order)
        {
            Placed& placed = sheet.frames[i];
            if (x > 0 && x + placed.width > limit)
            {
                x = 0;
                y += shelf + padding;
                shelf = 0;
            }
            placed.x = border + x;
            placed.y = border + y;
            x += placed.width + padding;
            shelf = std::max(shelf, placed.height);
            width = std::max(width, x - padding);
        }
        height = y + shelf + border * 2;
        width += border * 2;
    }
    // Copies sit where what they copy sits.
    for (Placed& placed : sheet.frames)
    {
        if (placed.sameAs >= 0)
        {
            placed.x = sheet.frames[static_cast<size_t>(placed.sameAs)].x;
            placed.y = sheet.frames[static_cast<size_t>(placed.sameAs)].y;
        }
    }
    sheet.width = std::max(1, width);
    sheet.height = std::max(1, height);
    if (options.powerOfTwo)
    {
        sheet.width = roundUpToPowerOfTwo(sheet.width);
        sheet.height = roundUpToPowerOfTwo(sheet.height);
    }
    return sheet;
}

std::string describe(const Sheet& sheet, const std::vector<std::string>& names, const std::string& image,
    DataFormat format, double frameRate)
{
    if (format == DataFormat::None)
        return std::string();
    const bool array = format == DataFormat::JsonArray;
    std::string out = array ? "{\"frames\": [\n" : "{\"frames\": {\n";
    char buffer[512];
    for (size_t i = 0; i < sheet.frames.size(); ++i)
    {
        const Placed& p = sheet.frames[i];
        const std::string name = i < names.size() ? names[i] : std::to_string(i);
        const bool trimmed = p.offsetX != 0 || p.offsetY != 0 || p.width != p.sourceWidth || p.height != p.sourceHeight;
        std::snprintf(buffer, sizeof(buffer),
            "\t\"frame\": {\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d},\n"
            "\t\"rotated\": false,\n"
            "\t\"trimmed\": %s,\n"
            "\t\"spriteSourceSize\": {\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d},\n"
            "\t\"sourceSize\": {\"w\": %d, \"h\": %d}\n",
            p.x, p.y, p.width, p.height, trimmed ? "true" : "false", p.offsetX, p.offsetY, p.width, p.height,
            p.sourceWidth, p.sourceHeight);
        if (array)
            out += "{\n\t\"filename\": \"" + escaped(name) + "\",\n";
        else
            out += "\"" + escaped(name) + "\": {\n";
        out += buffer;
        out += i + 1 < sheet.frames.size() ? "},\n" : "}\n";
    }
    out += array ? "],\n" : "},\n";
    std::snprintf(buffer, sizeof(buffer),
        "\"meta\": {\n\t\"app\": \"Phoenix\",\n\t\"image\": \"%s\",\n\t\"format\": \"RGBA8888\",\n"
        "\t\"size\": {\"w\": %d, \"h\": %d},\n\t\"scale\": \"1\",\n\t\"frameRate\": %g\n}\n}\n",
        escaped(image).c_str(), sheet.width, sheet.height, frameRate);
    out += buffer;
    return out;
}

} // namespace sprites
