#include "psd_import.h"

#include "zlib.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <cstdlib>

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
    uint64_t read(int count)
    {
        if (!has(static_cast<size_t>(count)))
        {
            failed = true;
            return 0;
        }
        uint64_t value = 0;
        for (int i = 0; i < count; ++i)
            value = (value << 8) | bytes[at++];
        return value;
    }
    uint8_t u8() { return static_cast<uint8_t>(read(1)); }
    uint16_t u16() { return static_cast<uint16_t>(read(2)); }
    int16_t i16() { return static_cast<int16_t>(read(2)); }
    uint32_t u32() { return static_cast<uint32_t>(read(4)); }
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
    /// Where a block of \a length bytes from here ends, held to the end of
    /// the file so that no length can wrap round to before it.
    size_t endOf(uint64_t length) const
    {
        const size_t from = std::min(at, bytes.size());
        return length <= bytes.size() - from ? from + static_cast<size_t>(length) : bytes.size();
    }
};

/// The longest side a large document may have, and the most pixels any one
/// image or layer is given memory for: past them, the size is damage.
constexpr int64_t kMaxSide = 300000;
constexpr int64_t kMaxPixels = int64_t(1) << 28;

bool sensibleSize(int64_t width, int64_t height)
{
    return width >= 0 && height >= 0 && width <= kMaxSide && height <= kMaxSide && width * height <= kMaxPixels;
}

/// What a document's pixels are, and how its lengths are written.
struct Format
{
    /// A large document (PSB) writes some lengths in eight bytes.
    bool large = false;
    /// Bits a sample: 8, 16 or 32.
    int depth = 8;
    /// 1 grayscale, 3 RGB, 4 CMYK.
    int mode = 3;

    uint64_t length(Reader& in) const { return large ? in.read(8) : in.u32(); }
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

std::vector<uint8_t> inflateAll(const uint8_t* data, size_t size, size_t expected)
{
    std::vector<uint8_t> out(expected);
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return std::vector<uint8_t>();
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    ::inflate(&stream, Z_FINISH);
    out.resize(out.size() - stream.avail_out);
    inflateEnd(&stream);
    return out;
}

/// Samples of \a depth bits as bytes: 16-bit ones by their high byte, and
/// 32-bit (linear floating point) ones brought into display gamma.
std::vector<uint8_t> toEightBit(const std::vector<uint8_t>& data, size_t count, int depth)
{
    if (depth == 8)
        return data;
    std::vector<uint8_t> out(count, 0);
    const size_t bytes = static_cast<size_t>(depth / 8);
    for (size_t i = 0; i < count && (i + 1) * bytes <= data.size(); ++i)
    {
        if (depth == 16)
        {
            out[i] = data[i * 2];
            continue;
        }
        uint32_t bits = 0;
        for (size_t k = 0; k < 4; ++k)
            bits = (bits << 8) | data[i * 4 + k];
        float value = 0.0f;
        std::memcpy(&value, &bits, 4);
        const double shown = std::pow(std::clamp(static_cast<double>(value), 0.0, 1.0), 1.0 / 2.2);
        out[i] = static_cast<uint8_t>(std::lround(shown * 255.0));
    }
    return out;
}

/// Undoes ZIP-with-prediction: each row was written as differences from
/// the sample before; 32-bit rows also had their bytes split into planes.
void unpredict(std::vector<uint8_t>& data, int width, int height, int depth)
{
    const size_t w = static_cast<size_t>(width);
    if (depth == 8)
    {
        for (int y = 0; y < height; ++y)
        {
            uint8_t* row = data.data() + static_cast<size_t>(y) * w;
            if ((static_cast<size_t>(y) + 1) * w > data.size())
                return;
            for (size_t x = 1; x < w; ++x)
                row[x] = static_cast<uint8_t>(row[x] + row[x - 1]);
        }
    }
    else if (depth == 16)
    {
        for (int y = 0; y < height; ++y)
        {
            uint8_t* row = data.data() + static_cast<size_t>(y) * w * 2;
            if ((static_cast<size_t>(y) + 1) * w * 2 > data.size())
                return;
            uint16_t previous = static_cast<uint16_t>((row[0] << 8) | row[1]);
            for (size_t x = 1; x < w; ++x)
            {
                const uint16_t value = static_cast<uint16_t>(((row[x * 2] << 8) | row[x * 2 + 1]) + previous);
                row[x * 2] = static_cast<uint8_t>(value >> 8);
                row[x * 2 + 1] = static_cast<uint8_t>(value);
                previous = value;
            }
        }
    }
    else if (depth == 32)
    {
        std::vector<uint8_t> row(w * 4);
        for (int y = 0; y < height; ++y)
        {
            uint8_t* stored = data.data() + static_cast<size_t>(y) * w * 4;
            if ((static_cast<size_t>(y) + 1) * w * 4 > data.size())
                return;
            for (size_t i = 1; i < w * 4; ++i)
                stored[i] = static_cast<uint8_t>(stored[i] + stored[i - 1]);
            // Plane k holds every sample's k-th byte.
            for (size_t x = 0; x < w; ++x)
                for (size_t k = 0; k < 4; ++k)
                    row[x * 4 + k] = stored[k * w + x];
            std::copy(row.begin(), row.end(), stored);
        }
    }
}

/// One channel's samples, raw, PackBits or ZIP, \a width by \a height, as
/// bytes. \a end is where the channel's data stops.
std::vector<uint8_t> channelData(Reader& in, size_t end, int width, int height, const Format& format)
{
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t sample = static_cast<size_t>(format.depth / 8);
    const size_t size = count * sample;
    const uint16_t compression = in.u16();
    std::vector<uint8_t> data;
    const size_t available = end > in.at && end <= in.bytes.size() ? end - in.at : 0;
    if (compression == 0)
    {
        if (available >= size)
            data.assign(in.bytes.begin() + static_cast<std::ptrdiff_t>(in.at),
                in.bytes.begin() + static_cast<std::ptrdiff_t>(in.at + size));
    }
    else if (compression == 1)
    {
        size_t packed = 0;
        for (int row = 0; row < height; ++row)
            packed += format.large ? in.u32() : in.u16();
        if (in.has(packed))
            data = unpackBits(in.bytes.data() + in.at, packed, size);
    }
    else if (compression == 2 || compression == 3)
    {
        if (available > 0)
            data = inflateAll(in.bytes.data() + in.at, available, size);
        if (data.size() == size && compression == 3)
            unpredict(data, width, height, format.depth);
    }
    in.at = end;
    if (data.size() != size)
        data.assign(size, 0);
    return toEightBit(data, count, format.depth);
}

/// Photoshop's blend modes as FLA files name them; empty for normal, and
/// for those with no like among them.
std::string blendName(const std::string& key, bool& known)
{
    static const std::map<std::string, std::string> names = {{"norm", ""}, {"pass", ""}, {"mul ", "multiply"},
        {"scrn", "screen"}, {"over", "overlay"}, {"dark", "darken"}, {"lite", "lighten"}, {"diff", "difference"},
        {"lddg", "add"}, {"fsub", "subtract"}, {"hLit", "hardlight"}};
    const auto found = names.find(key);
    known = found != names.end();
    return known ? found->second : std::string();
}

struct Channel
{
    int16_t id = 0;
    uint64_t length = 0;
};

struct Record
{
    Layer layer;
    std::vector<Channel> channels;
    /// lsct: 0 a layer, 1 or 2 a group's start (its top), 3 its end.
    uint32_t section = 0;
    int maskLeft = 0, maskTop = 0, maskWidth = 0, maskHeight = 0;
    uint8_t maskDefault = 255;
};

/// Keys whose length a large document writes in eight bytes.
bool longKey(const std::string& key)
{
    static const char* const keys[] = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph", "FMsk",
        "lnk2", "FEid", "FXid", "PxSD"};
    for (const char* name : keys)
        if (key == name)
            return true;
    return false;
}

/// The layer records and their channels from \a in up to \a end: the
/// layer info section, or an Lr16 or Lr32 block, which hold the same.
std::vector<Record> readLayers(Reader& in, size_t end, const Format& format, Image& image)
{
    std::vector<Record> records;
    const int count = std::abs(static_cast<int>(in.i16()));
    std::vector<std::string> unknownBlends;
    for (int i = 0; i < count && !in.failed && in.at < end; ++i)
    {
        Record record;
        const int top = in.i32(), left = in.i32(), bottom = in.i32(), right = in.i32();
        record.layer.left = left;
        record.layer.top = top;
        // In 64 bits, as right - left can overflow; a damaged size is left
        // empty, and the layer with it.
        const int64_t width = std::max<int64_t>(0, int64_t(right) - left);
        const int64_t height = std::max<int64_t>(0, int64_t(bottom) - top);
        if (sensibleSize(width, height))
        {
            record.layer.width = static_cast<int>(width);
            record.layer.height = static_cast<int>(height);
        }
        const uint16_t channelCount = in.u16();
        for (int c = 0; c < channelCount; ++c)
        {
            Channel channel;
            channel.id = in.i16();
            channel.length = format.length(in);
            record.channels.push_back(channel);
        }
        in.skip(4); // "8BIM"
        const std::string blendKey = in.text(4);
        bool known = true;
        record.layer.blend = blendName(blendKey, known);
        if (!known && std::find(unknownBlends.begin(), unknownBlends.end(), blendKey) == unknownBlends.end())
            unknownBlends.push_back(blendKey);
        record.layer.opacity = in.u8();
        record.layer.clipped = in.u8() != 0;
        const uint8_t flags = in.u8();
        record.layer.hidden = (flags & 0x02) != 0;
        in.u8();
        const uint32_t extra = in.u32();
        const size_t extraEnd = in.endOf(extra);
        const uint32_t maskLength = in.u32();
        const size_t maskEnd = in.endOf(maskLength);
        if (maskLength >= 18)
        {
            const int mt = in.i32(), ml = in.i32(), mb = in.i32(), mr = in.i32();
            record.maskLeft = ml;
            record.maskTop = mt;
            const int64_t maskWidth = std::max<int64_t>(0, int64_t(mr) - ml);
            const int64_t maskHeight = std::max<int64_t>(0, int64_t(mb) - mt);
            if (sensibleSize(maskWidth, maskHeight))
            {
                record.maskWidth = static_cast<int>(maskWidth);
                record.maskHeight = static_cast<int>(maskHeight);
            }
            record.maskDefault = in.u8();
            const uint8_t maskFlags = in.u8();
            // A disabled mask leaves the layer whole.
            if (maskFlags & 0x02)
                record.maskWidth = record.maskHeight = 0;
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
            const uint64_t length = format.large && longKey(key) ? in.read(8) : in.u32();
            const size_t blockEnd = in.endOf(length);
            if (key == "luni" && length >= 4)
            {
                const uint32_t units = in.u32();
                std::vector<uint16_t> name;
                for (uint32_t u = 0; u < units && in.at + 2 <= blockEnd; ++u)
                    name.push_back(in.u16());
                record.layer.name = utf8(name);
            }
            else if ((key == "lsct" || key == "lsdk") && length >= 4)
                record.section = in.u32();
            in.at = blockEnd + (length % 2);
            if (in.at > extraEnd)
                in.at = extraEnd;
        }
        in.at = extraEnd;
        records.push_back(std::move(record));
    }
    for (const std::string& key : unknownBlends)
        image.notes.push_back("A layer blend mode Phoenix has no like of (" + key + ") was drawn as Normal.");

    // Each layer's channels, in the order of the records.
    for (Record& record : records)
    {
        Layer& layer = record.layer;
        const size_t pixels = static_cast<size_t>(layer.width) * static_cast<size_t>(layer.height);
        std::map<int, std::vector<uint8_t>> planes;
        std::vector<uint8_t> mask;
        for (const Channel& channel : record.channels)
        {
            if (in.failed || in.at > in.bytes.size() || channel.length > in.bytes.size() - in.at)
            {
                in.failed = true;
                break;
            }
            const size_t channelEnd = in.at + static_cast<size_t>(channel.length);
            const bool isMask = channel.id == -2 || channel.id == -3;
            const int w = isMask ? record.maskWidth : layer.width;
            const int h = isMask ? record.maskHeight : layer.height;
            // No compression Photoshop uses packs more than about a thousand
            // samples a byte: a channel claiming more is damaged.
            const bool fits = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) <= channel.length * 1032 + 4096;
            if (record.section != 0 || w == 0 || h == 0 || channel.length < 2 || !fits)
            {
                in.at = channelEnd;
                continue;
            }
            std::vector<uint8_t> data = channelData(in, channelEnd, w, h, format);
            if (channel.id == -2)
                mask = std::move(data);
            else if (!isMask)
                planes[channel.id] = std::move(data);
        }
        if (record.section != 0 || pixels == 0)
            continue;
        uint64_t stored = 0;
        for (const Channel& channel : record.channels)
            stored += std::min<uint64_t>(channel.length, in.bytes.size());
        if (pixels > stored * 1032 + 4096)
        {
            // More pixels than its channels could hold: damaged, left out.
            layer.width = layer.height = 0;
            continue;
        }
        layer.rgba.assign(pixels * 4, 255);
        const auto plane = [&](int id) -> const std::vector<uint8_t>* {
            const auto found = planes.find(id);
            return found != planes.end() && found->second.size() == pixels ? &found->second : nullptr;
        };
        const std::vector<uint8_t>* alpha = plane(-1);
        for (size_t p = 0; p < pixels; ++p)
        {
            uint8_t* out = &layer.rgba[p * 4];
            if (format.mode == 1)
            {
                const std::vector<uint8_t>* g = plane(0);
                out[0] = out[1] = out[2] = g ? (*g)[p] : 0;
            }
            else if (format.mode == 4)
            {
                // Ink is stored inverted: 255 is none.
                const std::vector<uint8_t>* c = plane(0);
                const std::vector<uint8_t>* m = plane(1);
                const std::vector<uint8_t>* y = plane(2);
                const std::vector<uint8_t>* k = plane(3);
                const int key = k ? (*k)[p] : 255;
                out[0] = static_cast<uint8_t>((c ? (*c)[p] : 255) * key / 255);
                out[1] = static_cast<uint8_t>((m ? (*m)[p] : 255) * key / 255);
                out[2] = static_cast<uint8_t>((y ? (*y)[p] : 255) * key / 255);
            }
            else
            {
                for (int s = 0; s < 3; ++s)
                    if (const std::vector<uint8_t>* channel = plane(s))
                        out[s] = (*channel)[p];
            }
            if (alpha)
                out[3] = (*alpha)[p];
        }
        // A user mask: its pixels scale the alpha where it covers, its
        // default outside.
        if (!mask.empty())
        {
            const int w = record.maskWidth;
            const int h = record.maskHeight;
            for (int y = 0; y < layer.height; ++y)
                for (int x = 0; x < layer.width; ++x)
                {
                    const int64_t mx = int64_t(layer.left) + x - record.maskLeft;
                    const int64_t my = int64_t(layer.top) + y - record.maskTop;
                    const uint8_t value = mx >= 0 && my >= 0 && mx < w && my < h
                        ? mask[static_cast<size_t>(my) * static_cast<size_t>(w) + static_cast<size_t>(mx)]
                        : record.maskDefault;
                    uint8_t& a = layer.rgba[(static_cast<size_t>(y) * static_cast<size_t>(layer.width) + static_cast<size_t>(x)) * 4 + 3];
                    a = static_cast<uint8_t>(a * value / 255);
                }
        }
    }
    return records;
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
    Format format;
    format.depth = in.u16();
    format.mode = in.u16();
    format.large = version == 2;
    if (version != 1 && version != 2)
    {
        image.notes.push_back("The file is not a Photoshop document Phoenix reads.");
        return image;
    }
    if (!sensibleSize(image.width, image.height))
    {
        image.width = image.height = 0;
        image.notes.push_back("The document's size is damaged.");
        return image;
    }
    if ((format.depth != 8 && format.depth != 16 && format.depth != 32) ||
        (format.mode != 1 && format.mode != 3 && format.mode != 4))
    {
        image.notes.push_back("Only RGB, CMYK and grayscale documents of 8, 16 or 32 bits are read; convert it in "
                              "Photoshop first.");
        return image;
    }
    in.skip(in.u32()); // color mode data
    in.skip(in.u32()); // image resources

    // Layers, bottom first as stored. 16- and 32-bit documents keep them
    // in an Lr16 or Lr32 block after the global mask instead.
    std::vector<Record> records;
    const uint64_t layerAndMask = format.length(in);
    const size_t layerAndMaskEnd = in.endOf(layerAndMask);
    if (layerAndMask > 0 && !in.failed)
    {
        const uint64_t infoLength = format.length(in);
        const size_t infoEnd = in.endOf(infoLength);
        if (infoLength > 0)
            records = readLayers(in, infoEnd, format, image);
        in.at = infoEnd;
        // The global layer mask, then blocks of more layer information.
        in.skip(in.u32());
        while (records.empty() && !in.failed && in.at + 12 <= layerAndMaskEnd)
        {
            const std::string signature = in.text(4);
            if (signature != "8BIM" && signature != "8B64")
                break;
            const std::string key = in.text(4);
            const uint64_t length = format.large && longKey(key) ? in.read(8) : in.u32();
            const size_t end = in.endOf(length);
            if ((key == "Lr16" || key == "Lr32" || key == "Layr") && length > 2)
                records = readLayers(in, end, format, image);
            in.at = end;
            // Blocks are padded to four bytes.
            while (in.at % 4 != 0 && in.at < layerAndMaskEnd)
                ++in.at;
        }
        in.at = layerAndMaskEnd;
    }

    // Groups from the section markers: an end marker opens a group, read
    // from the bottom, and its start closes it.
    std::vector<std::vector<Layer>> stack(1);
    bool emptyLayers = false;
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
        else if (record.section == 0)
            emptyLayers = true;
    }
    while (stack.size() > 1)
    {
        std::vector<Layer> open = std::move(stack.back());
        stack.pop_back();
        stack.back().insert(stack.back().end(), open.begin(), open.end());
    }
    image.layers = std::move(stack.front());
    std::reverse(image.layers.begin(), image.layers.end());
    if (emptyLayers)
        image.notes.push_back("Adjustment and fill layers, which have no pixels of their own, were left out.");

    if (image.layers.empty() && !in.failed && in.has(2))
    {
        // No layers: the flattened image, every channel's rows counted
        // before any channel's data.
        const uint16_t compression = in.u16();
        const size_t count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        const size_t sample = static_cast<size_t>(format.depth / 8);
        const int planes = std::min<int>(channels, format.mode == 1 ? 2 : format.mode == 4 ? 5 : 4);
        std::vector<std::vector<uint8_t>> data;
        if (compression == 0)
        {
            for (int c = 0; c < planes && in.has(count * sample); ++c)
            {
                std::vector<uint8_t> plane(bytes.begin() + static_cast<std::ptrdiff_t>(in.at),
                    bytes.begin() + static_cast<std::ptrdiff_t>(in.at + count * sample));
                data.push_back(toEightBit(plane, count, format.depth));
                in.at += count * sample;
            }
        }
        else if (compression == 1)
        {
            // The format allows 56 channels; past the end of the file the
            // counts stop.
            const int counted = std::min<int>(channels, 56);
            std::vector<size_t> sizes(static_cast<size_t>(std::max(counted, planes)), 0);
            for (int c = 0; c < counted && !in.failed; ++c)
                for (int row = 0; row < image.height && !in.failed; ++row)
                    sizes[static_cast<size_t>(c)] += format.large ? in.u32() : in.u16();
            for (int c = 0; c < planes && !in.failed; ++c)
            {
                const size_t size = sizes[static_cast<size_t>(c)];
                if (!in.has(size) || count > size * 1032 + 4096)
                    break;
                data.push_back(toEightBit(unpackBits(bytes.data() + in.at, size, count * sample), count, format.depth));
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
                uint8_t* out = &layer.rgba[p * 4];
                if (format.mode == 1)
                {
                    out[0] = out[1] = out[2] = data[0][p];
                    if (data.size() > 1)
                        out[3] = data[1][p];
                }
                else if (format.mode == 4)
                {
                    const int key = data.size() > 3 ? data[3][p] : 255;
                    for (size_t c = 0; c < 3 && c < data.size(); ++c)
                        out[c] = static_cast<uint8_t>(data[c][p] * key / 255);
                    if (data.size() > 4)
                        out[3] = data[4][p];
                }
                else
                {
                    for (size_t c = 0; c < data.size(); ++c)
                        out[c] = data[c][p];
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
