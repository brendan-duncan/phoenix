#include "apng_writer.h"

#include "zlib.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace apng {

namespace {

void put32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put16(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

uint32_t get32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

/// A chunk: its length, type, data and the CRC of type and data.
void chunk(std::vector<uint8_t>& out, const char type[5], const uint8_t* data, size_t size)
{
    put32(out, static_cast<uint32_t>(size));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (size)
        out.insert(out.end(), data, data + size);
    const uLong crc = crc32(crc32(0L, Z_NULL, 0), out.data() + start, static_cast<uInt>(4 + size));
    put32(out, static_cast<uint32_t>(crc));
}

uint8_t paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc)
        return static_cast<uint8_t>(a);
    return static_cast<uint8_t>(pb <= pc ? b : c);
}

/// The rows of a part of the picture, each filtered the way that leaves
/// the smallest differences (as most PNG writers choose), then deflated.
std::vector<uint8_t> pack(const std::vector<uint8_t>& rgba, int stride, int x, int y, int width, int height)
{
    const size_t row = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> raw;
    raw.reserve((row + 1) * static_cast<size_t>(height));
    std::vector<uint8_t> above(row, 0);
    std::vector<uint8_t> line(row);
    std::vector<uint8_t> best(row);
    std::vector<uint8_t> trial(row);
    for (int r = 0; r < height; ++r)
    {
        std::memcpy(line.data(), rgba.data() + (static_cast<size_t>(y + r) * stride + x) * 4, row);
        long bestSum = -1;
        uint8_t bestType = 0;
        for (uint8_t type = 0; type < 5; ++type)
        {
            long sum = 0;
            for (size_t i = 0; i < row; ++i)
            {
                const int left = i >= 4 ? line[i - 4] : 0;
                const int up = r > 0 ? above[i] : 0;
                const int corner = i >= 4 && r > 0 ? above[i - 4] : 0;
                int predicted = 0;
                switch (type)
                {
                case 1: predicted = left; break;
                case 2: predicted = up; break;
                case 3: predicted = (left + up) / 2; break;
                case 4: predicted = paeth(left, up, corner); break;
                default: break;
                }
                const uint8_t value = static_cast<uint8_t>(line[i] - predicted);
                trial[i] = value;
                sum += value < 128 ? value : 256 - value;
            }
            if (bestSum < 0 || sum < bestSum)
            {
                bestSum = sum;
                bestType = type;
                best.swap(trial);
            }
        }
        raw.push_back(bestType);
        raw.insert(raw.end(), best.begin(), best.end());
        above = line;
    }
    uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> packed(packedSize);
    if (compress2(packed.data(), &packedSize, raw.data(), static_cast<uLong>(raw.size()), 9) != Z_OK)
        return {};
    packed.resize(packedSize);
    return packed;
}

} // namespace

AnimationWriter::AnimationWriter(int width, int height, double frameRate, int plays)
    : _width(std::max(1, width)), _height(std::max(1, height)), _plays(std::max(0, plays))
{
    // A frame lasts 1 / frameRate seconds: in thousandths of a frame a
    // second where that fits the 16 bits a delay has.
    const double rate = frameRate > 0.0 ? frameRate : 24.0;
    if (rate * 1000.0 <= 65535.0)
    {
        _delayNumerator = 1000;
        _delayDenominator = static_cast<int>(std::lround(rate * 1000.0));
    }
    else
    {
        _delayNumerator = 1;
        _delayDenominator = std::min(65535, static_cast<int>(std::lround(rate)));
    }
}

bool AnimationWriter::addFrame(const std::vector<uint8_t>& rgba)
{
    const size_t size = static_cast<size_t>(_width) * _height * 4;
    if (rgba.size() < size)
        return false;
    Frame frame;
    frame.delayNumerator = _delayNumerator;
    if (_previous.empty())
    {
        frame.width = _width;
        frame.height = _height;
    }
    else
    {
        // Only what changed: the box round every pixel that differs.
        int left = _width;
        int top = _height;
        int right = -1;
        int bottom = -1;
        for (int y = 0; y < _height; ++y)
        {
            const uint8_t* now = rgba.data() + static_cast<size_t>(y) * _width * 4;
            const uint8_t* was = _previous.data() + static_cast<size_t>(y) * _width * 4;
            if (std::memcmp(now, was, static_cast<size_t>(_width) * 4) == 0)
                continue;
            top = std::min(top, y);
            bottom = y;
            for (int x = 0; x < _width; ++x)
            {
                if (std::memcmp(now + x * 4, was + x * 4, 4) != 0)
                {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
        }
        if (right < 0)
        {
            // Just like the frame before: it lasts that much longer.
            Frame& last = _frames.back();
            if (last.delayNumerator + _delayNumerator <= 65535)
            {
                last.delayNumerator += _delayNumerator;
                return true;
            }
            left = top = right = bottom = 0;
        }
        frame.x = left;
        frame.y = top;
        frame.width = right - left + 1;
        frame.height = bottom - top + 1;
    }
    frame.packed = pack(rgba, _width, frame.x, frame.y, frame.width, frame.height);
    if (frame.packed.empty())
        return false;
    _previous.assign(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(size));
    _frames.push_back(std::move(frame));
    return true;
}

std::vector<uint8_t> AnimationWriter::finish()
{
    if (_frames.empty())
        return {};
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> data;
    put32(data, static_cast<uint32_t>(_width));
    put32(data, static_cast<uint32_t>(_height));
    // 8 bits a channel, RGBA, deflate, adaptive filtering, no interlace.
    data.insert(data.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", data.data(), data.size());
    data.clear();
    put32(data, static_cast<uint32_t>(_frames.size()));
    put32(data, static_cast<uint32_t>(_plays));
    chunk(out, "acTL", data.data(), data.size());

    uint32_t sequence = 0;
    for (size_t i = 0; i < _frames.size(); ++i)
    {
        const Frame& frame = _frames[i];
        data.clear();
        put32(data, sequence++);
        put32(data, static_cast<uint32_t>(frame.width));
        put32(data, static_cast<uint32_t>(frame.height));
        put32(data, static_cast<uint32_t>(frame.x));
        put32(data, static_cast<uint32_t>(frame.y));
        put16(data, static_cast<uint32_t>(frame.delayNumerator));
        put16(data, static_cast<uint32_t>(_delayDenominator));
        // Left as drawn; the changed part replaces what was there.
        data.push_back(0);
        data.push_back(0);
        chunk(out, "fcTL", data.data(), data.size());
        if (i == 0)
        {
            chunk(out, "IDAT", frame.packed.data(), frame.packed.size());
            continue;
        }
        data.clear();
        put32(data, sequence++);
        data.insert(data.end(), frame.packed.begin(), frame.packed.end());
        chunk(out, "fdAT", data.data(), data.size());
    }
    chunk(out, "IEND", nullptr, 0);
    _frames.clear();
    _previous.clear();
    return out;
}

bool inspect(const std::vector<uint8_t>& bytes, Info& info)
{
    info = Info();
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8) != 0)
        return false;
    size_t at = 8;
    bool ended = false;
    while (at + 12 <= bytes.size())
    {
        const uint32_t length = get32(bytes.data() + at);
        if (at + 12 + static_cast<size_t>(length) > bytes.size())
            return false;
        const uint8_t* type = bytes.data() + at + 4;
        const uint8_t* data = type + 4;
        const uLong crc = crc32(crc32(0L, Z_NULL, 0), type, static_cast<uInt>(4 + length));
        if (static_cast<uint32_t>(crc) != get32(data + length))
            return false;
        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 8)
        {
            info.width = static_cast<int>(get32(data));
            info.height = static_cast<int>(get32(data + 4));
        }
        else if (std::memcmp(type, "acTL", 4) == 0 && length >= 8)
        {
            info.frames = static_cast<int>(get32(data));
            info.plays = static_cast<int>(get32(data + 4));
        }
        else if (std::memcmp(type, "fcTL", 4) == 0)
        {
            ++info.frameControls;
        }
        else if (std::memcmp(type, "IEND", 4) == 0)
        {
            ended = true;
            break;
        }
        at += 12 + length;
    }
    return ended && info.width > 0 && info.height > 0;
}

} // namespace apng
