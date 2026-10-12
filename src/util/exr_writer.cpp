#include "exr_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace exr {

namespace {

/// The file's little-endian building blocks.
struct Bytes
{
    std::vector<char> data;

    void raw(const void* source, size_t size)
    {
        const char* from = static_cast<const char*>(source);
        data.insert(data.end(), from, from + size);
    }
    void u8(uint8_t value) { raw(&value, 1); }
    void i32(int32_t value)
    {
        const uint8_t b[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 24)};
        raw(b, 4);
    }
    void u64(uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            u8(static_cast<uint8_t>(value >> (8 * i)));
    }
    void f32(float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, 4);
        i32(static_cast<int32_t>(bits));
    }
    void text(const std::string& value)
    {
        raw(value.data(), value.size());
        u8(0);
    }
    /// An attribute: its name, type, size and value.
    void attribute(const std::string& name, const std::string& type, const Bytes& value)
    {
        text(name);
        text(type);
        i32(static_cast<int32_t>(value.data.size()));
        raw(value.data.data(), value.data.size());
    }
};

} // namespace

unsigned short toHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xff) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;
    if (((bits >> 23) & 0xff) == 0xff)
        return static_cast<unsigned short>(sign | 0x7c00u | (mantissa ? 0x200u : 0u));
    if (exponent >= 31)
        return static_cast<unsigned short>(sign | 0x7c00u);
    if (exponent <= 0)
    {
        // Too small for a normal half: a denormal, or zero.
        if (exponent < -10)
            return static_cast<unsigned short>(sign);
        mantissa |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        uint32_t half = mantissa >> shift;
        if ((mantissa >> (shift - 1)) & 1u)
            ++half;
        return static_cast<unsigned short>(sign | half);
    }
    uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
    // Round to nearest.
    if (mantissa & 0x1000u)
        ++half;
    return static_cast<unsigned short>(half);
}

bool write(const std::string& path, int width, int height, std::vector<Channel> channels, std::string& error)
{
    if (width <= 0 || height <= 0 || channels.empty())
    {
        error = "Nothing to write.";
        return false;
    }
    for (const Channel& channel : channels)
        if (channel.values.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
        {
            error = "Channel " + channel.name + " is not the size of the image.";
            return false;
        }
    // The file lists, and stores, its channels in name order.
    std::sort(channels.begin(), channels.end(), [](const Channel& a, const Channel& b) { return a.name < b.name; });
    bool longNames = false;
    for (const Channel& channel : channels)
        longNames = longNames || channel.name.size() > 31;

    Bytes file;
    file.i32(20000630);
    // Version 2; long attribute and channel names flagged when used.
    file.i32(2 | (longNames ? 0x400 : 0));

    Bytes list;
    for (const Channel& channel : channels)
    {
        list.text(channel.name);
        list.i32(1); // half
        list.u8(0);  // not perceptually linear
        list.u8(0);
        list.u8(0);
        list.u8(0);
        list.i32(1);
        list.i32(1);
    }
    list.u8(0);
    file.attribute("channels", "chlist", list);
    Bytes compression;
    compression.u8(0);
    file.attribute("compression", "compression", compression);
    Bytes window;
    window.i32(0);
    window.i32(0);
    window.i32(width - 1);
    window.i32(height - 1);
    file.attribute("dataWindow", "box2i", window);
    file.attribute("displayWindow", "box2i", window);
    Bytes order;
    order.u8(0);
    file.attribute("lineOrder", "lineOrder", order);
    Bytes aspect;
    aspect.f32(1.0f);
    file.attribute("pixelAspectRatio", "float", aspect);
    Bytes center;
    center.f32(0.0f);
    center.f32(0.0f);
    file.attribute("screenWindowCenter", "v2f", center);
    Bytes screenWidth;
    screenWidth.f32(1.0f);
    file.attribute("screenWindowWidth", "float", screenWidth);
    file.u8(0);

    // The offsets of every scanline's chunk, then the chunks: each its row,
    // its size and its channels' values one after another.
    const size_t lineBytes = static_cast<size_t>(width) * 2 * channels.size();
    const uint64_t tableEnd = file.data.size() + static_cast<uint64_t>(height) * 8;
    for (int y = 0; y < height; ++y)
        file.u64(tableEnd + static_cast<uint64_t>(y) * (8 + lineBytes));
    for (int y = 0; y < height; ++y)
    {
        file.i32(y);
        file.i32(static_cast<int32_t>(lineBytes));
        for (const Channel& channel : channels)
        {
            const float* row = channel.values.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
            for (int x = 0; x < width; ++x)
            {
                const unsigned short half = toHalf(row[x]);
                file.u8(static_cast<uint8_t>(half & 0xff));
                file.u8(static_cast<uint8_t>(half >> 8));
            }
        }
    }

    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    if (!out || !out.write(file.data.data(), static_cast<std::streamsize>(file.data.size())))
    {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

} // namespace exr
