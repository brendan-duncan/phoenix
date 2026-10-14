#include "swf_writer.h"

#include "zlib.h"

#include <algorithm>
#include <cmath>

namespace swf {

namespace {

/// Bits written most significant first, as SWF packs its records.
struct Bits
{
    std::vector<uint8_t> bytes;
    int used = 8;

    void put(uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
        {
            if (used == 8)
            {
                bytes.push_back(0);
                used = 0;
            }
            if ((value >> i) & 1u)
                bytes.back() |= static_cast<uint8_t>(0x80 >> used);
            ++used;
        }
    }
    void putSigned(int32_t value, int count) { put(static_cast<uint32_t>(value) & ((count >= 32) ? 0xFFFFFFFFu : ((1u << count) - 1u)), count); }
    /// Byte aligned again.
    void align() { used = 8; }
    void u8(int value)
    {
        align();
        bytes.push_back(static_cast<uint8_t>(value));
    }
    void u16(int value)
    {
        u8(value & 0xFF);
        u8((value >> 8) & 0xFF);
    }
    void u32(uint32_t value)
    {
        u16(static_cast<int>(value & 0xFFFF));
        u16(static_cast<int>(value >> 16));
    }
    void append(const std::vector<uint8_t>& more)
    {
        align();
        bytes.insert(bytes.end(), more.begin(), more.end());
    }
};

/// Bits a signed value needs, sign included.
int signedBits(int32_t value)
{
    int bits = 1;
    while (bits < 32 && (value >= (1 << (bits - 1)) || value < -(1 << (bits - 1))))
        ++bits;
    return bits;
}

int signedBits(std::initializer_list<int32_t> values)
{
    int bits = 1;
    for (int32_t value : values)
        bits = std::max(bits, signedBits(value));
    return bits;
}

int32_t twips(double pixels)
{
    return static_cast<int32_t>(std::lround(pixels * 20.0));
}

void rect(Bits& out, int32_t left, int32_t right, int32_t top, int32_t bottom)
{
    const int bits = signedBits({left, right, top, bottom});
    out.put(static_cast<uint32_t>(bits), 5);
    out.putSigned(left, bits);
    out.putSigned(right, bits);
    out.putSigned(top, bits);
    out.putSigned(bottom, bits);
    out.align();
}

/// A matrix: \a m maps its source to the shape, \a scale the source's units
/// to twips' (20 for pixels; 1 for gradient squares already in twips).
void matrix(Bits& out, const Matrix& m, double scale)
{
    const auto fixed = [](double value) { return static_cast<int32_t>(std::lround(value * 65536.0)); };
    // The result is in twips; a source unit is \a scale twips smaller than
    // a pixel (gradient squares are counted in twips already, bitmaps in
    // pixels).
    const int32_t a = fixed(m.a * 20.0 / scale);
    const int32_t d = fixed(m.d * 20.0 / scale);
    const int32_t b = fixed(m.b * 20.0 / scale);
    const int32_t c = fixed(m.c * 20.0 / scale);
    const bool hasScale = a != 65536 || d != 65536;
    out.put(hasScale ? 1u : 0u, 1);
    if (hasScale)
    {
        const int bits = signedBits({a, d});
        out.put(static_cast<uint32_t>(bits), 5);
        out.putSigned(a, bits);
        out.putSigned(d, bits);
    }
    const bool hasRotate = b != 0 || c != 0;
    out.put(hasRotate ? 1u : 0u, 1);
    if (hasRotate)
    {
        const int bits = signedBits({b, c});
        out.put(static_cast<uint32_t>(bits), 5);
        out.putSigned(b, bits);
        out.putSigned(c, bits);
    }
    const int32_t tx = twips(m.tx);
    const int32_t ty = twips(m.ty);
    const int bits = (tx == 0 && ty == 0) ? 0 : signedBits({tx, ty});
    out.put(static_cast<uint32_t>(bits), 5);
    if (bits > 0)
    {
        out.putSigned(tx, bits);
        out.putSigned(ty, bits);
    }
    out.align();
}

void rgba(Bits& out, const Color& color)
{
    out.u8(color.r);
    out.u8(color.g);
    out.u8(color.b);
    out.u8(color.a);
}

std::vector<uint8_t> compress(const std::vector<uint8_t>& data)
{
    uLongf size = compressBound(static_cast<uLong>(data.size()));
    std::vector<uint8_t> out(size);
    if (compress2(out.data(), &size, data.data(), static_cast<uLong>(data.size()), 9) != Z_OK)
        return std::vector<uint8_t>();
    out.resize(size);
    return out;
}

} // namespace

Writer::Writer(int width, int height, double fps, Color background)
    : _width(std::max(1, width))
    , _height(std::max(1, height))
    , _fps(fps > 0.0 ? fps : 24.0)
{
    // FileAttributes: nothing asked of the player; then the stage color.
    Bits attributes;
    attributes.u32(0);
    tag(69, attributes.bytes);
    Bits color;
    color.u8(background.r);
    color.u8(background.g);
    color.u8(background.b);
    tag(9, color.bytes);
}

void Writer::tag(int code, const std::vector<uint8_t>& body)
{
    const size_t length = body.size();
    if (length < 63)
    {
        const int header = (code << 6) | static_cast<int>(length);
        _tags.push_back(static_cast<uint8_t>(header & 0xFF));
        _tags.push_back(static_cast<uint8_t>(header >> 8));
    }
    else
    {
        const int header = (code << 6) | 0x3F;
        _tags.push_back(static_cast<uint8_t>(header & 0xFF));
        _tags.push_back(static_cast<uint8_t>(header >> 8));
        for (int i = 0; i < 4; ++i)
            _tags.push_back(static_cast<uint8_t>((length >> (8 * i)) & 0xFF));
    }
    _tags.insert(_tags.end(), body.begin(), body.end());
}

int Writer::defineBitmap(int width, int height, const std::vector<uint8_t>& argb)
{
    if (width <= 0 || height <= 0 || argb.size() < static_cast<size_t>(width) * height * 4)
        return 0;
    const int id = _nextId++;
    Bits body;
    body.u16(id);
    body.u8(5); // 32-bit ARGB
    body.u16(width);
    body.u16(height);
    body.append(compress(argb));
    tag(36, body.bytes); // DefineBitsLossless2
    return id;
}

int Writer::defineShape(const Fill& fill, const std::vector<Segment>& outline)
{
    if (outline.size() < 2)
        return 0;
    // Bounds, from the points and controls.
    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
    for (const Segment& segment : outline)
    {
        left = std::min({left, segment.x, segment.kind == Segment::Kind::Quad ? segment.cx : segment.x});
        right = std::max({right, segment.x, segment.kind == Segment::Kind::Quad ? segment.cx : segment.x});
        top = std::min({top, segment.y, segment.kind == Segment::Kind::Quad ? segment.cy : segment.y});
        bottom = std::max({bottom, segment.y, segment.kind == Segment::Kind::Quad ? segment.cy : segment.y});
    }
    const int id = _nextId++;
    const bool focal = fill.type == Fill::Type::Radial && std::fabs(fill.focal) > 1.0e-3;
    Bits body;
    body.u16(id);
    rect(body, twips(left), twips(right), twips(top), twips(bottom));
    if (focal)
    {
        // DefineShape4's edge bounds and flags.
        rect(body, twips(left), twips(right), twips(top), twips(bottom));
        body.u8(0);
    }

    // One fill, no lines.
    body.u8(1);
    switch (fill.type)
    {
    case Fill::Type::Solid:
        body.u8(0x00);
        rgba(body, fill.color);
        break;
    case Fill::Type::Linear:
    case Fill::Type::Radial:
    {
        body.u8(fill.type == Fill::Type::Linear ? 0x10 : (focal ? 0x13 : 0x12));
        // The gradient square is 32768 twips across: the matrix takes its
        // units straight as twips.
        matrix(body, fill.matrix, 20.0);
        std::vector<Fill::Stop> stops = fill.stops;
        if (stops.size() > 15)
            stops.resize(15);
        body.put(static_cast<uint32_t>(std::clamp(fill.spread, 0, 2)), 2);
        body.put(0, 2); // normal RGB interpolation
        body.put(static_cast<uint32_t>(stops.size()), 4);
        body.align();
        for (const Fill::Stop& stop : stops)
        {
            body.u8(static_cast<int>(std::lround(std::clamp(stop.offset, 0.0, 1.0) * 255.0)));
            rgba(body, stop.color);
        }
        if (focal)
        {
            // Fixed 8.8.
            const int value = static_cast<int>(std::lround(std::clamp(fill.focal, -1.0, 1.0) * 256.0));
            body.u16(value & 0xFFFF);
        }
        break;
    }
    case Fill::Type::Bitmap:
        body.u8(fill.repeat ? (fill.smooth ? 0x40 : 0x42) : (fill.smooth ? 0x41 : 0x43));
        body.u16(fill.bitmap);
        // Bitmap pixels are twips in a fill's matrix: twenty to a pixel.
        matrix(body, fill.matrix, 1.0);
        break;
    }
    body.u8(0); // no line styles
    body.put(1, 4); // fill bits
    body.put(0, 4); // line bits

    // Records: every move starts a subpath with the fill on its left.
    int32_t x = 0, y = 0;
    bool first = true;
    for (const Segment& segment : outline)
    {
        const int32_t tx = twips(segment.x);
        const int32_t ty = twips(segment.y);
        if (segment.kind == Segment::Kind::Move)
        {
            body.put(0, 1); // a style change
            body.put(0, 1); // new styles
            body.put(0, 1); // line style
            body.put(0, 1); // fill style 1
            body.put(first ? 1u : 0u, 1); // fill style 0
            body.put(1, 1); // move to
            const int bits = signedBits({tx, ty});
            body.put(static_cast<uint32_t>(bits), 5);
            body.putSigned(tx, bits);
            body.putSigned(ty, bits);
            if (first)
                body.put(1, 1); // fill style 0 is the first fill
            first = false;
        }
        else if (segment.kind == Segment::Kind::Line)
        {
            const int32_t dx = tx - x;
            const int32_t dy = ty - y;
            if (dx == 0 && dy == 0)
                continue;
            body.put(1, 1); // an edge
            body.put(1, 1); // straight
            const int bits = std::max(2, signedBits({dx, dy}));
            body.put(static_cast<uint32_t>(bits - 2), 4);
            if (dx != 0 && dy != 0)
            {
                body.put(1, 1); // general line
                body.putSigned(dx, bits);
                body.putSigned(dy, bits);
            }
            else
            {
                body.put(0, 1);
                body.put(dx == 0 ? 1u : 0u, 1); // vertical
                body.putSigned(dx == 0 ? dy : dx, bits);
            }
        }
        else
        {
            const int32_t cx = twips(segment.cx);
            const int32_t cy = twips(segment.cy);
            const int32_t cdx = cx - x, cdy = cy - y, adx = tx - cx, ady = ty - cy;
            body.put(1, 1); // an edge
            body.put(0, 1); // curved
            const int bits = std::max(2, signedBits({cdx, cdy, adx, ady}));
            body.put(static_cast<uint32_t>(bits - 2), 4);
            body.putSigned(cdx, bits);
            body.putSigned(cdy, bits);
            body.putSigned(adx, bits);
            body.putSigned(ady, bits);
        }
        x = tx;
        y = ty;
    }
    body.put(0, 6); // the end
    body.align();
    tag(focal ? 83 : 32, body.bytes); // DefineShape4 or DefineShape3
    return id;
}

void Writer::place(int id, int depth, double alpha, Blend blend)
{
    Bits body;
    const bool fade = alpha < 0.999;
    const bool blends = blend != Blend::Normal;
    // PlaceObject2: a new character at an empty depth, with a color
    // transform when it fades; PlaceObject3 when it blends too.
    body.put(0, 1); // clip actions
    body.put(0, 1); // clip depth
    body.put(0, 1); // name
    body.put(0, 1); // ratio
    body.put(fade ? 1u : 0u, 1); // color transform
    body.put(0, 1); // matrix
    body.put(1, 1); // character
    body.put(0, 1); // move
    if (blends)
    {
        body.put(0, 1); // reserved
        body.put(0, 1); // opaque background
        body.put(0, 1); // visible
        body.put(0, 1); // image
        body.put(0, 1); // class name
        body.put(0, 1); // cache as bitmap
        body.put(1, 1); // blend mode
        body.put(0, 1); // filters
    }
    body.u16(depth);
    body.u16(id);
    if (fade)
    {
        // CXFORMWITHALPHA: multiply terms only.
        const int32_t multiply = static_cast<int32_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 256.0));
        body.put(0, 1); // add terms
        body.put(1, 1); // multiply terms
        const int bits = signedBits({256, multiply});
        body.put(static_cast<uint32_t>(bits), 4);
        body.putSigned(256, bits);
        body.putSigned(256, bits);
        body.putSigned(256, bits);
        body.putSigned(multiply, bits);
        body.align();
    }
    if (blends)
    {
        body.u8(static_cast<int>(blend));
        tag(70, body.bytes);
        return;
    }
    tag(26, body.bytes);
}

void Writer::remove(int depth)
{
    Bits body;
    body.u16(depth);
    tag(28, body.bytes); // RemoveObject2
}

void Writer::soundStream(int codec, int rate, bool stereo, int samplesPerFrame, int latency)
{
    // Rates as two-bit codes: 5.5, 11, 22 and 44 kHz.
    const int rateCode = rate >= 44100 ? 3 : rate >= 22050 ? 2 : rate >= 11025 ? 1 : 0;
    const int format = (rateCode << 2) | (1 << 1) | (stereo ? 1 : 0); // always 16-bit
    Bits body;
    body.u8(format);
    body.u8((codec << 4) | format);
    body.u16(samplesPerFrame & 0xFFFF);
    if (codec == 2)
        body.u16(latency & 0xFFFF);
    tag(45, body.bytes); // SoundStreamHead2
}

void Writer::soundBlock(const std::vector<uint8_t>& data)
{
    if (!data.empty())
        tag(19, data); // SoundStreamBlock
}

void Writer::showFrame()
{
    tag(1, {});
    ++_frames;
}

std::vector<uint8_t> Writer::finish(bool compressed)
{
    tag(0, {});
    Bits head;
    rect(head, 0, twips(_width), 0, twips(_height));
    head.u16(static_cast<int>(std::lround(_fps * 256.0)) & 0xFFFF);
    head.u16(std::max(1, _frames));
    std::vector<uint8_t> body = head.bytes;
    body.insert(body.end(), _tags.begin(), _tags.end());
    const uint32_t length = static_cast<uint32_t>(8 + body.size());
    std::vector<uint8_t> file = {static_cast<uint8_t>(compressed ? 'C' : 'F'), 'W', 'S', 10};
    for (int i = 0; i < 4; ++i)
        file.push_back(static_cast<uint8_t>((length >> (8 * i)) & 0xFF));
    if (compressed)
    {
        const std::vector<uint8_t> packed = compress(body);
        file.insert(file.end(), packed.begin(), packed.end());
    }
    else
        file.insert(file.end(), body.begin(), body.end());
    return file;
}

} // namespace swf
