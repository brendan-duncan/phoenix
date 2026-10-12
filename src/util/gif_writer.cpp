#include "gif_writer.h"

#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>

namespace gif {

namespace {

struct Color
{
    uint8_t r, g, b;
};

/// A box of colors in median cut: the distinct colors (with how many pixels
/// each has) between first and last of the shared list.
struct Box
{
    size_t first;
    size_t last;
};

struct Entry
{
    Color color;
    uint32_t count;
};

/// Median cut over the distinct colors: the box with the widest channel
/// (weighted by pixels) is split at its median until there are \a slots.
std::vector<Color> palette(std::vector<Entry>& entries, size_t slots)
{
    std::vector<Box> boxes{{0, entries.size()}};
    const auto rangeOf = [&entries](const Box& box, int& channel) {
        uint8_t lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
        for (size_t i = box.first; i < box.last; ++i)
        {
            const uint8_t c[3] = {entries[i].color.r, entries[i].color.g, entries[i].color.b};
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], c[k]);
                hi[k] = std::max(hi[k], c[k]);
            }
        }
        int widest = 0;
        channel = 0;
        for (int k = 0; k < 3; ++k)
        {
            if (hi[k] - lo[k] > widest)
            {
                widest = hi[k] - lo[k];
                channel = k;
            }
        }
        return widest;
    };
    while (boxes.size() < slots)
    {
        // The box most worth splitting: wide, and with many pixels.
        size_t pick = boxes.size();
        double best = 0.0;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            if (boxes[i].last - boxes[i].first < 2)
                continue;
            int channel = 0;
            const int width = rangeOf(boxes[i], channel);
            uint64_t pixels = 0;
            for (size_t k = boxes[i].first; k < boxes[i].last; ++k)
                pixels += entries[k].count;
            const double score = static_cast<double>(width) * static_cast<double>(pixels);
            if (width > 0 && score > best)
            {
                best = score;
                pick = i;
            }
        }
        if (pick == boxes.size())
            break;
        Box box = boxes[pick];
        int channel = 0;
        rangeOf(box, channel);
        std::sort(entries.begin() + static_cast<std::ptrdiff_t>(box.first),
            entries.begin() + static_cast<std::ptrdiff_t>(box.last), [channel](const Entry& a, const Entry& b) {
                const uint8_t av = channel == 0 ? a.color.r : channel == 1 ? a.color.g : a.color.b;
                const uint8_t bv = channel == 0 ? b.color.r : channel == 1 ? b.color.g : b.color.b;
                return av < bv;
            });
        // Split where half the box's pixels fall on each side.
        uint64_t total = 0;
        for (size_t k = box.first; k < box.last; ++k)
            total += entries[k].count;
        uint64_t running = 0;
        size_t split = box.first + 1;
        for (size_t k = box.first; k < box.last - 1; ++k)
        {
            running += entries[k].count;
            split = k + 1;
            if (running * 2 >= total)
                break;
        }
        boxes[pick] = {box.first, split};
        boxes.push_back({split, box.last});
    }
    std::vector<Color> colors;
    for (const Box& box : boxes)
    {
        uint64_t r = 0, g = 0, b = 0, n = 0;
        for (size_t k = box.first; k < box.last; ++k)
        {
            r += static_cast<uint64_t>(entries[k].color.r) * entries[k].count;
            g += static_cast<uint64_t>(entries[k].color.g) * entries[k].count;
            b += static_cast<uint64_t>(entries[k].color.b) * entries[k].count;
            n += entries[k].count;
        }
        if (n == 0)
            n = 1;
        colors.push_back({static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n)});
    }
    return colors;
}

/// Bits written least significant first, packed into sub-blocks of at most
/// 255 bytes, as GIF's LZW data is.
class BitWriter
{
public:
    explicit BitWriter(std::vector<uint8_t>& out)
        : _out(out)
    {}
    void write(uint32_t code, int bits)
    {
        _buffer |= code << _count;
        _count += bits;
        while (_count >= 8)
        {
            byte(static_cast<uint8_t>(_buffer & 0xFF));
            _buffer >>= 8;
            _count -= 8;
        }
    }
    void finish()
    {
        if (_count > 0)
            byte(static_cast<uint8_t>(_buffer & 0xFF));
        flushBlock();
        _out.push_back(0); // block terminator
    }

private:
    void byte(uint8_t value)
    {
        _block.push_back(value);
        if (_block.size() == 255)
            flushBlock();
    }
    void flushBlock()
    {
        if (_block.empty())
            return;
        _out.push_back(static_cast<uint8_t>(_block.size()));
        _out.insert(_out.end(), _block.begin(), _block.end());
        _block.clear();
    }
    std::vector<uint8_t>& _out;
    std::vector<uint8_t> _block;
    uint32_t _buffer = 0;
    int _count = 0;
};

void lzw(const std::vector<uint8_t>& indices, int minCodeSize, std::vector<uint8_t>& out)
{
    out.push_back(static_cast<uint8_t>(minCodeSize));
    BitWriter bits(out);
    const uint32_t clear = 1u << minCodeSize;
    const uint32_t end = clear + 1;
    std::unordered_map<uint32_t, uint32_t> table;
    uint32_t next = end + 1;
    int size = minCodeSize + 1;
    const auto reset = [&]() {
        table.clear();
        next = end + 1;
        size = minCodeSize + 1;
    };
    bits.write(clear, size);
    if (indices.empty())
    {
        bits.write(end, size);
        bits.finish();
        return;
    }
    uint32_t prefix = indices[0];
    for (size_t i = 1; i < indices.size(); ++i)
    {
        const uint32_t key = (prefix << 8) | indices[i];
        const auto found = table.find(key);
        if (found != table.end())
        {
            prefix = found->second;
            continue;
        }
        bits.write(prefix, size);
        if (next < 4096)
        {
            table[key] = next++;
            if (next > (1u << size) && size < 12)
                ++size;
        }
        else
        {
            bits.write(clear, size);
            reset();
        }
        prefix = indices[i];
    }
    bits.write(prefix, size);
    bits.write(end, size);
    bits.finish();
}

void put16(std::vector<uint8_t>& out, int value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

} // namespace

std::vector<uint8_t> encode(int width, int height, const std::vector<uint8_t>& rgba, bool transparent)
{
    std::vector<uint8_t> out;
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535 ||
        rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
        return out;
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);

    // The distinct colors, counted; transparent pixels set aside.
    std::map<uint32_t, uint32_t> counts;
    bool anyClear = false;
    for (size_t i = 0; i < pixels; ++i)
    {
        const uint8_t* p = &rgba[i * 4];
        if (transparent && p[3] < 128)
        {
            anyClear = true;
            continue;
        }
        ++counts[(static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2]];
    }
    std::vector<Entry> entries;
    entries.reserve(counts.size());
    for (const auto& entry : counts)
        entries.push_back({{static_cast<uint8_t>(entry.first >> 16), static_cast<uint8_t>(entry.first >> 8),
                              static_cast<uint8_t>(entry.first)}, entry.second});
    const size_t slots = anyClear ? 255 : 256;
    std::vector<Color> colors = entries.empty() ? std::vector<Color>{{0, 0, 0}} : palette(entries, slots);
    const int clearIndex = anyClear ? static_cast<int>(colors.size()) : -1;
    if (anyClear)
        colors.push_back({0, 0, 0});
    int bitsPerColor = 1;
    while ((1 << bitsPerColor) < static_cast<int>(colors.size()))
        ++bitsPerColor;
    colors.resize(static_cast<size_t>(1) << bitsPerColor, {0, 0, 0});

    // Each pixel's nearest palette color, remembered by color.
    std::unordered_map<uint32_t, uint8_t> nearest;
    std::vector<uint8_t> indices(pixels);
    const size_t used = anyClear ? static_cast<size_t>(clearIndex) : colors.size();
    for (size_t i = 0; i < pixels; ++i)
    {
        const uint8_t* p = &rgba[i * 4];
        if (anyClear && p[3] < 128)
        {
            indices[i] = static_cast<uint8_t>(clearIndex);
            continue;
        }
        const uint32_t key = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        const auto found = nearest.find(key);
        if (found != nearest.end())
        {
            indices[i] = found->second;
            continue;
        }
        int best = 0;
        int bestDistance = 1 << 30;
        for (size_t c = 0; c < used; ++c)
        {
            const int dr = p[0] - colors[c].r, dg = p[1] - colors[c].g, db = p[2] - colors[c].b;
            const int distance = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = static_cast<int>(c);
            }
        }
        nearest[key] = static_cast<uint8_t>(best);
        indices[i] = static_cast<uint8_t>(best);
    }

    // Header, screen with the global palette, the transparency, the image.
    const char* signature = "GIF89a";
    out.insert(out.end(), signature, signature + 6);
    put16(out, width);
    put16(out, height);
    out.push_back(static_cast<uint8_t>(0x80 | ((bitsPerColor - 1) << 4) | (bitsPerColor - 1)));
    out.push_back(0); // background color index
    out.push_back(0); // pixel aspect
    for (const Color& color : colors)
    {
        out.push_back(color.r);
        out.push_back(color.g);
        out.push_back(color.b);
    }
    if (anyClear)
    {
        const uint8_t control[] = {0x21, 0xF9, 4, 0x01, 0, 0, static_cast<uint8_t>(clearIndex), 0};
        out.insert(out.end(), control, control + sizeof(control));
    }
    out.push_back(0x2C);
    put16(out, 0);
    put16(out, 0);
    put16(out, width);
    put16(out, height);
    out.push_back(0); // no local palette, not interlaced
    lzw(indices, std::max(2, bitsPerColor), out);
    out.push_back(0x3B);
    return out;
}

} // namespace gif
