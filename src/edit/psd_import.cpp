#include "psd_import.h"

#include <algorithm>

namespace fla {

namespace psd {

namespace {

/// Big-endian reads that stop, rather than run past, at the end.
struct Reader
{
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool failed = false;

    bool has(size_t count) const { return !failed && at <= bytes.size() && bytes.size() - at >= count; }
    uint32_t read(int count)
    {
        if (!has(static_cast<size_t>(count)))
        {
            failed = true;
            return 0;
        }
        uint32_t value = 0;
        for (int i = 0; i < count; ++i)
            value = (value << 8) | bytes[at++];
        return value;
    }
    uint8_t u8() { return static_cast<uint8_t>(read(1)); }
    uint16_t u16() { return static_cast<uint16_t>(read(2)); }
    int16_t i16() { return static_cast<int16_t>(read(2)); }
    uint32_t u32() { return read(4); }
    int32_t i32() { return static_cast<int32_t>(read(4)); }
    std::string text(size_t count)
    {
        if (!has(count))
        {
            failed = true;
            return std::string();
        }
        std::string value(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + count));
        at += count;
        return value;
    }
    void skip(size_t count)
    {
        if (!has(count))
            failed = true;
        else
            at += count;
    }
};

std::string utf8(const std::vector<uint16_t>& units)
{
    std::string out;
    for (size_t i = 0; i < units.size(); ++i)
    {
        uint32_t code = units[i];
        if (code >= 0xd800 && code < 0xdc00 && i + 1 < units.size())
            code = 0x10000 + ((code - 0xd800) << 10) + (units[++i] - 0xdc00);
        if (code == 0)
            continue;
        if (code < 0x80)
            out += static_cast<char>(code);
        else if (code < 0x800)
        {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
        else if (code < 0x10000)
        {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
        else
        {
            out += static_cast<char>(0xf0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return out;
}

struct Channel
{
    int16_t id = 0;
    uint32_t length = 0;
};

struct Record
{
    Layer layer;
    std::vector<Channel> channels;
    /// lsct: 0 a layer, 1 or 2 a group's start (its top), 3 its end.
    uint32_t section = 0;
    int maskLeft = 0, maskTop = 0, maskWidth = 0, maskHeight = 0;
};

/// One channel's pixels: raw or PackBits, \a width by \a height.
std::vector<uint8_t> channelData(Reader& in, size_t end, int width, int height)
{
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const uint16_t compression = in.u16();
    std::vector<uint8_t> pixels;
    if (compression == 0)
    {
        if (in.has(count) && in.at + count <= end)
        {
            pixels.assign(in.bytes.begin() + static_cast<std::ptrdiff_t>(in.at),
                in.bytes.begin() + static_cast<std::ptrdiff_t>(in.at + count));
        }
    }
    else if (compression == 1)
    {
        size_t packed = 0;
        for (int row = 0; row < height; ++row)
            packed += in.u16();
        if (in.has(packed))
            pixels = unpackBits(in.bytes.data() + in.at, packed, count);
    }
    in.at = end;
    if (pixels.size() != count)
        pixels.assign(count, 0);
    return pixels;
}

} // namespace

std::vector<uint8_t> unpackBits(const uint8_t* data, size_t size, size_t count)
{
    std::vector<uint8_t> out;
    out.reserve(count);
    size_t i = 0;
    while (i < size && out.size() < count)
    {
        const int8_t header = static_cast<int8_t>(data[i++]);
        if (header >= 0)
        {
            const size_t run = static_cast<size_t>(header) + 1;
            for (size_t k = 0; k < run && i < size; ++k)
                out.push_back(data[i++]);
        }
        else if (header != -128)
        {
            const size_t run = static_cast<size_t>(1 - header);
            if (i < size)
            {
                out.insert(out.end(), run, data[i]);
                ++i;
            }
        }
    }
    out.resize(count, 0);
    return out;
}

Image read(const std::vector<uint8_t>& bytes)
{
    Image image;
    Reader in{bytes};
    if (in.text(4) != "8BPS")
    {
        image.notes.push_back("The file is not a Photoshop document.");
        return image;
    }
    const uint16_t version = in.u16();
    in.skip(6);
    const uint16_t channels = in.u16();
    image.height = static_cast<int>(in.u32());
    image.width = static_cast<int>(in.u32());
    const uint16_t depth = in.u16();
    const uint16_t mode = in.u16();
    if (version != 1)
    {
        image.notes.push_back("Large documents (PSB) are not read; save it as a PSD.");
        return image;
    }
    if (depth != 8 || (mode != 3 && mode != 1))
    {
        image.notes.push_back("Only 8-bit RGB and grayscale documents are read; convert it in Photoshop first.");
        return image;
    }
    const bool gray = mode == 1;
    in.skip(in.u32()); // color mode data
    in.skip(in.u32()); // image resources

    // Layers, bottom first as stored.
    std::vector<Record> records;
    const uint32_t layerAndMask = in.u32();
    const size_t layerAndMaskEnd = in.at + layerAndMask;
    if (layerAndMask > 0 && !in.failed)
    {
        const uint32_t infoLength = in.u32();
        const size_t infoEnd = in.at + infoLength;
        if (infoLength > 0)
        {
            const int count = std::abs(static_cast<int>(in.i16()));
            for (int i = 0; i < count && !in.failed; ++i)
            {
                Record record;
                const int top = in.i32(), left = in.i32(), bottom = in.i32(), right = in.i32();
                record.layer.left = left;
                record.layer.top = top;
                record.layer.width = std::max(0, right - left);
                record.layer.height = std::max(0, bottom - top);
                const uint16_t channelCount = in.u16();
                for (int c = 0; c < channelCount; ++c)
                {
                    Channel channel;
                    channel.id = in.i16();
                    channel.length = in.u32();
                    record.channels.push_back(channel);
                }
                in.skip(8); // "8BIM" and the blend mode
                record.layer.opacity = in.u8();
                in.u8(); // clipping
                const uint8_t flags = in.u8();
                record.layer.hidden = (flags & 0x02) != 0;
                in.u8();
                const uint32_t extra = in.u32();
                const size_t extraEnd = in.at + extra;
                const uint32_t maskLength = in.u32();
                const size_t maskEnd = in.at + maskLength;
                if (maskLength >= 16)
                {
                    const int mt = in.i32(), ml = in.i32(), mb = in.i32(), mr = in.i32();
                    record.maskLeft = ml;
                    record.maskTop = mt;
                    record.maskWidth = std::max(0, mr - ml);
                    record.maskHeight = std::max(0, mb - mt);
                }
                in.at = maskEnd;
                in.skip(in.u32()); // blending ranges
                const uint8_t nameLength = in.u8();
                record.layer.name = in.text(nameLength);
                // The name and its length byte, padded to four.
                in.skip((4 - (nameLength + 1) % 4) % 4);
                while (!in.failed && in.at + 12 <= extraEnd)
                {
                    const std::string signature = in.text(4);
                    if (signature != "8BIM" && signature != "8B64")
                        break;
                    const std::string key = in.text(4);
                    const uint32_t length = in.u32();
                    const size_t end = in.at + length;
                    if (key == "luni" && length >= 4)
                    {
                        const uint32_t units = in.u32();
                        std::vector<uint16_t> name;
                        for (uint32_t u = 0; u < units && in.at + 2 <= end; ++u)
                            name.push_back(in.u16());
                        record.layer.name = utf8(name);
                    }
                    else if ((key == "lsct" || key == "lsdk") && length >= 4)
                        record.section = in.u32();
                    in.at = end + (length % 2);
                    if (in.at > extraEnd)
                        in.at = extraEnd;
                }
                in.at = extraEnd;
                records.push_back(std::move(record));
            }

            // Each layer's channels, in the order of the records.
            for (Record& record : records)
            {
                Layer& layer = record.layer;
                const size_t pixels = static_cast<size_t>(layer.width) * static_cast<size_t>(layer.height);
                if (record.section == 0)
                    layer.rgba.assign(pixels * 4, 255);
                for (const Channel& channel : record.channels)
                {
                    const size_t end = in.at + channel.length;
                    if (in.failed || end > bytes.size())
                    {
                        in.failed = true;
                        break;
                    }
                    const bool mask = channel.id == -2 || channel.id == -3;
                    const int w = mask ? record.maskWidth : layer.width;
                    const int h = mask ? record.maskHeight : layer.height;
                    if (record.section != 0 || w == 0 || h == 0 || channel.length < 2)
                    {
                        in.at = end;
                        continue;
                    }
                    const std::vector<uint8_t> data = channelData(in, end, w, h);
                    if (mask)
                    {
                        // A user mask: its pixels scale the alpha where it
                        // covers, the default color outside.
                        if (channel.id != -2)
                            continue;
                        for (int y = 0; y < layer.height; ++y)
                            for (int x = 0; x < layer.width; ++x)
                            {
                                const int mx = layer.left + x - record.maskLeft;
                                const int my = layer.top + y - record.maskTop;
                                const uint8_t value = mx >= 0 && my >= 0 && mx < w && my < h
                                    ? data[static_cast<size_t>(my) * static_cast<size_t>(w) + static_cast<size_t>(mx)]
                                    : 255;
                                uint8_t& alpha = layer.rgba[(static_cast<size_t>(y) * static_cast<size_t>(layer.width) + static_cast<size_t>(x)) * 4 + 3];
                                alpha = static_cast<uint8_t>(alpha * value / 255);
                            }
                        continue;
                    }
                    const int slot = channel.id == -1 ? 3 : channel.id;
                    if (slot < 0 || slot > 3)
                        continue;
                    for (size_t p = 0; p < pixels; ++p)
                    {
                        if (gray && slot == 0)
                            layer.rgba[p * 4] = layer.rgba[p * 4 + 1] = layer.rgba[p * 4 + 2] = data[p];
                        else if (!gray || slot == 3)
                            layer.rgba[p * 4 + static_cast<size_t>(slot)] = data[p];
                    }
                }
            }
        }
        in.at = layerAndMaskEnd;
        (void)infoEnd;
    }

    // Groups from the section markers: an end marker opens a group, read
    // from the bottom, and its start closes it.
    std::vector<std::vector<Layer>> stack(1);
    for (Record& record : records)
    {
        if (record.section == 3)
            stack.emplace_back();
        else if ((record.section == 1 || record.section == 2) && stack.size() > 1)
        {
            Layer group = std::move(record.layer);
            group.group = true;
            group.rgba.clear();
            group.children = std::move(stack.back());
            std::reverse(group.children.begin(), group.children.end());
            stack.pop_back();
            stack.back().push_back(std::move(group));
        }
        else if (record.section == 0 && record.layer.width > 0 && record.layer.height > 0)
            stack.back().push_back(std::move(record.layer));
    }
    while (stack.size() > 1)
    {
        std::vector<Layer> open = std::move(stack.back());
        stack.pop_back();
        stack.back().insert(stack.back().end(), open.begin(), open.end());
    }
    image.layers = std::move(stack.front());
    std::reverse(image.layers.begin(), image.layers.end());

    if (image.layers.empty() && !in.failed && in.has(2))
    {
        // No layers: the flattened image, every channel's rows counted
        // before any channel's data.
        const uint16_t compression = in.u16();
        const size_t count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        const int planes = std::min<int>(channels, gray ? 2 : 4);
        std::vector<std::vector<uint8_t>> data;
        if (compression == 0)
        {
            for (int c = 0; c < planes && in.has(count); ++c)
            {
                data.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(in.at), bytes.begin() + static_cast<std::ptrdiff_t>(in.at + count));
                in.at += count;
            }
        }
        else if (compression == 1)
        {
            std::vector<size_t> sizes(channels, 0);
            for (int c = 0; c < channels; ++c)
                for (int row = 0; row < image.height; ++row)
                    sizes[static_cast<size_t>(c)] += in.u16();
            for (int c = 0; c < planes && !in.failed; ++c)
            {
                const size_t size = sizes[static_cast<size_t>(c)];
                if (!in.has(size))
                    break;
                data.push_back(unpackBits(bytes.data() + in.at, size, count));
                in.at += size;
            }
        }
        if (!data.empty())
        {
            Layer layer;
            layer.name = "Background";
            layer.width = image.width;
            layer.height = image.height;
            layer.rgba.assign(count * 4, 255);
            for (size_t p = 0; p < count; ++p)
            {
                if (gray)
                {
                    layer.rgba[p * 4] = layer.rgba[p * 4 + 1] = layer.rgba[p * 4 + 2] = data[0][p];
                    if (data.size() > 1)
                        layer.rgba[p * 4 + 3] = data[1][p];
                }
                else
                {
                    for (size_t c = 0; c < data.size(); ++c)
                        layer.rgba[p * 4 + c] = data[c][p];
                }
            }
            image.layers.push_back(std::move(layer));
        }
    }
    if (in.failed && image.layers.empty())
        image.notes.push_back("The document ended before its layers did.");
    image.ok = !image.layers.empty();
    if (!image.ok && image.notes.empty())
        image.notes.push_back("The document has no pixel layers.");
    return image;
}

} // namespace psd

} // namespace fla
