#include "swf_writer.h"

#include "zlib.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

void putString(Bits& out, const std::string& text)
{
    out.align();
    for (const char c : text)
        if (c != '\0')
            out.bytes.push_back(static_cast<uint8_t>(c));
    out.bytes.push_back(0);
}

/// 16.16 and 8.8 fixed point.
void fixed16(Bits& out, double value)
{
    out.u32(static_cast<uint32_t>(static_cast<int32_t>(std::lround(std::clamp(value, -32768.0, 32767.0) * 65536.0))));
}

void fixed8(Bits& out, double value)
{
    out.u16(static_cast<int>(std::lround(std::clamp(value, -128.0, 127.99) * 256.0)) & 0xFFFF);
}

/// CXFORMWITHALPHA: multiply terms in 8.8, then add terms, each part only
/// when it does something.
void colorTransform(Bits& out, const ColorTransform& c)
{
    const auto multiply = [](double value) {
        return static_cast<int32_t>(std::lround(std::clamp(value, -63.0, 63.0) * 256.0));
    };
    const int32_t m[4] = {multiply(c.red), multiply(c.green), multiply(c.blue), multiply(c.alpha)};
    const int32_t a[4] = {std::clamp(c.redAdd, -255, 255), std::clamp(c.greenAdd, -255, 255),
        std::clamp(c.blueAdd, -255, 255), std::clamp(c.alphaAdd, -255, 255)};
    const bool multiplies = m[0] != 256 || m[1] != 256 || m[2] != 256 || m[3] != 256;
    const bool adds = a[0] != 0 || a[1] != 0 || a[2] != 0 || a[3] != 0;
    int bits = 1;
    if (multiplies)
        bits = std::max(bits, signedBits({m[0], m[1], m[2], m[3]}));
    if (adds)
        bits = std::max(bits, signedBits({a[0], a[1], a[2], a[3]}));
    out.put(adds ? 1u : 0u, 1);
    out.put(multiplies ? 1u : 0u, 1);
    out.put(static_cast<uint32_t>(std::min(bits, 15)), 4);
    if (multiplies)
        for (int32_t value : m)
            out.putSigned(value, bits);
    if (adds)
        for (int32_t value : a)
            out.putSigned(value, bits);
    out.align();
}

/// A FILTERLIST.
void filterList(Bits& out, const std::vector<Filter>& filters)
{
    const size_t count = std::min<size_t>(filters.size(), 255);
    out.u8(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i)
    {
        const Filter& f = filters[i];
        out.u8(static_cast<int>(f.type));
        // Inner, knockout, composite source (always), and on top and the
        // passes for a bevel, or the passes alone.
        const auto flags = [&](bool bevel) {
            const int passes = std::clamp(f.passes, 1, bevel ? 15 : 31);
            out.u8((f.inner ? 0x80 : 0) | (f.knockout ? 0x40 : 0) | 0x20 | (bevel && f.onTop ? 0x10 : 0) | passes);
        };
        switch (f.type)
        {
        case Filter::Type::DropShadow:
            rgba(out, f.color);
            fixed16(out, f.blurX);
            fixed16(out, f.blurY);
            fixed16(out, f.angle);
            fixed16(out, f.distance);
            fixed8(out, f.strength);
            flags(false);
            break;
        case Filter::Type::Blur:
            fixed16(out, f.blurX);
            fixed16(out, f.blurY);
            out.u8(std::clamp(f.passes, 1, 31) << 3);
            break;
        case Filter::Type::Glow:
            rgba(out, f.color);
            fixed16(out, f.blurX);
            fixed16(out, f.blurY);
            fixed8(out, f.strength);
            flags(false);
            break;
        case Filter::Type::Bevel:
            // The highlight first, as players read it (the format's
            // description has them the other way round).
            rgba(out, f.highlight);
            rgba(out, f.color);
            fixed16(out, f.blurX);
            fixed16(out, f.blurY);
            fixed16(out, f.angle);
            fixed16(out, f.distance);
            fixed8(out, f.strength);
            flags(true);
            break;
        case Filter::Type::GradientGlow:
        case Filter::Type::GradientBevel:
        {
            const size_t stops = std::min<size_t>(f.stops.size(), 15);
            out.u8(static_cast<int>(stops));
            for (size_t s = 0; s < stops; ++s)
                rgba(out, f.stops[s].color);
            for (size_t s = 0; s < stops; ++s)
                out.u8(static_cast<int>(std::lround(std::clamp(f.stops[s].offset, 0.0, 1.0) * 255.0)));
            fixed16(out, f.blurX);
            fixed16(out, f.blurY);
            fixed16(out, f.angle);
            fixed16(out, f.distance);
            fixed8(out, f.strength);
            flags(true);
            break;
        }
        case Filter::Type::ColorMatrix:
            for (float value : f.matrix)
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                out.u32(bits);
            }
            break;
        }
    }
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

/// A tag, its header then its body, on the end of \a out.
void appendTag(std::vector<uint8_t>& out, int code, const std::vector<uint8_t>& body)
{
    const size_t length = body.size();
    if (length < 63)
    {
        const int header = (code << 6) | static_cast<int>(length);
        out.push_back(static_cast<uint8_t>(header & 0xFF));
        out.push_back(static_cast<uint8_t>(header >> 8));
    }
    else
    {
        const int header = (code << 6) | 0x3F;
        out.push_back(static_cast<uint8_t>(header & 0xFF));
        out.push_back(static_cast<uint8_t>(header >> 8));
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<uint8_t>((length >> (8 * i)) & 0xFF));
    }
    out.insert(out.end(), body.begin(), body.end());
}

/// StartSound's body: the sound's id, then its SOUNDINFO.
std::vector<uint8_t> startSoundBody(int id, const SoundInfo& info)
{
    Bits body;
    body.u16(id);
    // Stop, no multiple, then envelope, loops, out and in point flags; what
    // they say is there follows in the order in, out, loops, envelope.
    if (info.stop)
    {
        body.u8(0x20);
        return body.bytes;
    }
    const bool looped = info.loops != 1;
    const bool in = info.inPoint > 0;
    const bool out = info.outPoint > 0;
    const size_t points = std::min<size_t>(info.envelope.size(), 255);
    body.u8((info.noMultiple ? 0x10 : 0) | (points > 0 ? 0x08 : 0) | (looped ? 0x04 : 0) | (out ? 0x02 : 0) |
        (in ? 0x01 : 0));
    if (in)
        body.u32(info.inPoint);
    if (out)
        body.u32(info.outPoint);
    if (looped)
        body.u16(info.loops <= 0 ? 0xFFFF : std::min(info.loops, 0xFFFF));
    if (points > 0)
    {
        body.u8(static_cast<int>(points));
        for (size_t i = 0; i < points; ++i)
        {
            const SoundInfo::EnvelopePoint& point = info.envelope[i];
            body.u32(point.pos44);
            body.u16(std::min<int>(point.left, 32768));
            body.u16(std::min<int>(point.right, 32768));
        }
    }
    return body.bytes;
}

/// PlaceObject2's body for character \a id at \a depth, as it is.
std::vector<uint8_t> placeBody(int id, int depth)
{
    Bits body;
    body.u8(0x02); // character
    body.u16(depth);
    body.u16(id);
    return body.bytes;
}

std::vector<uint8_t> removeBody(int depth)
{
    Bits body;
    body.u16(depth);
    return body.bytes;
}

/// PlaceObject2's body, or PlaceObject3's when \a placement has filters,
/// a blend mode or bitmap caching; \a code says which.
std::vector<uint8_t> placementBody(const Placement& p, int& code)
{
    const bool three = p.hasFilters || p.hasBlend || p.cacheAsBitmap;
    code = three ? 70 : 26;
    Bits body;
    body.u8((!p.clipActions.empty() ? 0x80 : 0) | (p.clipDepth > 0 ? 0x40 : 0) | (!p.name.empty() ? 0x20 : 0) |
        (p.ratio >= 0 ? 0x10 : 0) | (p.hasColor ? 0x08 : 0) | (p.hasMatrix ? 0x04 : 0) | (p.character > 0 ? 0x02 : 0) |
        (p.move ? 0x01 : 0));
    if (three)
        body.u8((p.cacheAsBitmap ? 0x04 : 0) | (p.hasBlend ? 0x02 : 0) | (p.hasFilters ? 0x01 : 0));
    body.u16(p.depth);
    if (p.character > 0)
        body.u16(p.character);
    if (p.hasMatrix)
        matrix(body, p.matrix, 20.0);
    if (p.hasColor)
        colorTransform(body, p.color);
    if (p.ratio >= 0)
        body.u16(std::min(p.ratio, 65535));
    if (!p.name.empty())
        putString(body, p.name);
    if (p.clipDepth > 0)
        body.u16(p.clipDepth);
    if (p.hasFilters)
        filterList(body, p.filters);
    if (p.hasBlend)
        body.u8(std::clamp(p.blend, 1, 14));
    if (p.cacheAsBitmap)
        body.u8(1);
    body.append(p.clipActions);
    return body.bytes;
}

std::vector<uint8_t> labelBody(const std::string& name, bool anchor)
{
    Bits body;
    putString(body, name);
    if (anchor)
        body.u8(1);
    return body.bytes;
}

} // namespace

std::vector<uint8_t> clipActions(const std::vector<ClipAction>& actions)
{
    if (actions.empty())
        return {};
    Bits out;
    uint32_t all = 0;
    for (const ClipAction& action : actions)
        all |= action.events;
    out.u16(0);
    out.u32(all);
    for (const ClipAction& action : actions)
    {
        const bool key = (action.events & ClipEvent::KeyPress) != 0;
        out.u32(action.events);
        out.u32(static_cast<uint32_t>(action.actions.size() + (key ? 1 : 0)));
        if (key)
            out.u8(action.key & 0xFF);
        out.append(action.actions);
    }
    out.u32(0);
    return out.bytes;
}

void Sprite::place(int id, int depth)
{
    appendTag(_tags, 26, placeBody(id, depth)); // PlaceObject2
}

void Sprite::place(const Placement& placement)
{
    int code = 26;
    const std::vector<uint8_t> body = placementBody(placement, code);
    appendTag(_tags, code, body);
}

void Sprite::frameLabel(const std::string& name, bool anchor)
{
    appendTag(_tags, 43, labelBody(name, anchor));
}

void Sprite::doAction(const std::vector<uint8_t>& actions)
{
    if (!actions.empty())
        appendTag(_tags, 12, actions);
}

void Sprite::remove(int depth)
{
    appendTag(_tags, 28, removeBody(depth)); // RemoveObject2
}

void Sprite::startSound(int id, const SoundInfo& info)
{
    appendTag(_tags, 15, startSoundBody(id, info)); // StartSound
}

void Sprite::showFrame()
{
    appendTag(_tags, 1, {});
    ++_frames;
}

Writer::Writer(int width, int height, double fps, Color background)
    : _width(std::max(1, width))
    , _height(std::max(1, height))
    , _fps(fps > 0.0 ? fps : 24.0)
{
    // FileAttributes: nothing asked of the player (until a movie says it
    // runs ActionScript 3); then the stage color.
    Bits attributes;
    attributes.u32(0);
    tag(69, attributes.bytes);
    _attributesAt = 2;
    Bits color;
    color.u8(background.r);
    color.u8(background.g);
    color.u8(background.b);
    tag(9, color.bytes);
}

void Writer::tag(int code, const std::vector<uint8_t>& body)
{
    appendTag(_tags, code, body);
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
    tag(28, removeBody(depth)); // RemoveObject2
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

int Writer::defineSound(int codec, int rate, bool stereo, uint32_t samples, const std::vector<uint8_t>& data)
{
    const int id = _nextId++;
    const int rateCode = rate >= 44100 ? 3 : rate >= 22050 ? 2 : rate >= 11025 ? 1 : 0;
    Bits body;
    body.u16(id);
    body.u8((codec << 4) | (rateCode << 2) | (1 << 1) | (stereo ? 1 : 0)); // always 16-bit
    body.u16(samples & 0xFFFF);
    body.u16((samples >> 16) & 0xFFFF);
    body.bytes.insert(body.bytes.end(), data.begin(), data.end());
    tag(14, body.bytes); // DefineSound
    return id;
}

void Writer::startSound(int id, int loops, bool noMultiple, bool stop)
{
    SoundInfo info;
    info.loops = loops;
    info.noMultiple = noMultiple;
    info.stop = stop;
    startSound(id, info);
}

void Writer::startSound(int id, const SoundInfo& info)
{
    tag(15, startSoundBody(id, info)); // StartSound
}

int Writer::defineSprite(const Sprite& sprite)
{
    const int id = _nextId++;
    Bits body;
    body.u16(id);
    body.u16(std::max(1, sprite.frames()));
    std::vector<uint8_t> tags = sprite.tags();
    if (sprite.frames() == 0)
        appendTag(tags, 1, {});
    appendTag(tags, 0, {});
    body.append(tags);
    tag(39, body.bytes); // DefineSprite
    return id;
}

int Writer::defineButton(const std::vector<ButtonRecord>& records, const std::vector<ButtonAction>& actions, bool menu)
{
    const int id = _nextId++;
    Bits body;
    body.u16(id);
    body.u8(menu ? 1 : 0);
    const size_t offsetAt = body.bytes.size();
    body.u16(0); // where the actions start, once known
    for (const ButtonRecord& record : records)
    {
        if (record.character <= 0 || (record.states & 0x0F) == 0)
            continue;
        body.u8((record.blend > 0 ? 0x20 : 0) | (!record.filters.empty() ? 0x10 : 0) | (record.states & 0x0F));
        body.u16(record.character);
        body.u16(record.depth);
        matrix(body, record.matrix, 20.0);
        colorTransform(body, record.color);
        if (!record.filters.empty())
            filterList(body, record.filters);
        if (record.blend > 0)
            body.u8(std::clamp(record.blend, 1, 14));
    }
    body.u8(0);
    if (!actions.empty())
    {
        const size_t first = body.bytes.size() - offsetAt;
        body.bytes[offsetAt] = static_cast<uint8_t>(first & 0xFF);
        body.bytes[offsetAt + 1] = static_cast<uint8_t>((first >> 8) & 0xFF);
        for (size_t i = 0; i < actions.size(); ++i)
        {
            // Each says how far on the next one is; the last, 0.
            const size_t size = i + 1 < actions.size() ? 4 + actions[i].actions.size() : 0;
            body.u16(static_cast<int>(std::min<size_t>(size, 0xFFFF)));
            body.u16(actions[i].conditions);
            body.append(actions[i].actions);
        }
    }
    tag(34, body.bytes); // DefineButton2
    return id;
}

int Writer::defineFont(const std::string& name, bool bold, bool italic)
{
    const int id = _nextId++;
    Bits body;
    body.u16(id);
    body.u8(0x04 | (italic ? 0x02 : 0) | (bold ? 0x01 : 0)); // wide codes
    body.u8(1); // Latin
    const std::string shown = name.substr(0, 255);
    body.u8(static_cast<int>(shown.size()));
    for (const char c : shown)
        body.bytes.push_back(static_cast<uint8_t>(c));
    body.u16(0); // no glyphs: the player's own font
    body.u16(2); // where the (empty) code table starts
    tag(48, body.bytes); // DefineFont2
    return id;
}

int Writer::defineEditText(const EditText& text)
{
    const int id = _nextId++;
    Bits body;
    body.u16(id);
    rect(body, twips(text.left), twips(text.right), twips(text.top), twips(text.bottom));
    const bool hasText = !text.text.empty();
    body.u8((hasText ? 0x80 : 0) | (text.wordWrap ? 0x40 : 0) | (text.multiline ? 0x20 : 0) |
        (text.password ? 0x10 : 0) | (text.readOnly ? 0x08 : 0) | 0x04 | (text.maxLength > 0 ? 0x02 : 0) |
        (text.font > 0 ? 0x01 : 0));
    body.u8((text.autoSize ? 0x40 : 0) | 0x20 | (text.noSelect ? 0x10 : 0) | (text.border ? 0x08 : 0) |
        (text.html ? 0x02 : 0) | (text.useOutlines ? 0x01 : 0));
    if (text.font > 0)
    {
        body.u16(text.font);
        body.u16(std::clamp(static_cast<int>(twips(text.size)), 1, 0xFFFF));
    }
    rgba(body, text.color);
    if (text.maxLength > 0)
        body.u16(std::min(text.maxLength, 0xFFFF));
    body.u8(std::clamp(text.align, 0, 3));
    body.u16(std::clamp(static_cast<int>(twips(text.leftMargin)), 0, 0xFFFF));
    body.u16(std::clamp(static_cast<int>(twips(text.rightMargin)), 0, 0xFFFF));
    body.u16(std::clamp(static_cast<int>(twips(text.indent)), 0, 0xFFFF));
    body.u16(std::clamp(static_cast<int>(twips(text.leading)), -32768, 32767) & 0xFFFF);
    putString(body, text.variable);
    if (hasText)
        putString(body, text.text);
    tag(37, body.bytes); // DefineEditText
    return id;
}

void Writer::defineScalingGrid(int sprite, double left, double top, double right, double bottom)
{
    Bits body;
    body.u16(sprite);
    rect(body, twips(left), twips(right), twips(top), twips(bottom));
    tag(78, body.bytes);
}

void Writer::place(const Placement& placement)
{
    int code = 26;
    const std::vector<uint8_t> body = placementBody(placement, code);
    tag(code, body);
}

void Writer::frameLabel(const std::string& name, bool anchor)
{
    tag(43, labelBody(name, anchor));
}

void Writer::doAction(const std::vector<uint8_t>& actions)
{
    if (!actions.empty())
        tag(12, actions);
}

void Writer::doInitAction(int sprite, const std::vector<uint8_t>& actions)
{
    if (actions.empty())
        return;
    Bits body;
    body.u16(sprite);
    body.append(actions);
    tag(59, body.bytes);
}

void Writer::exportAssets(const std::vector<std::pair<int, std::string>>& names)
{
    if (names.empty())
        return;
    Bits body;
    body.u16(static_cast<int>(std::min<size_t>(names.size(), 0xFFFF)));
    for (const auto& [id, name] : names)
    {
        body.u16(id);
        putString(body, name);
    }
    tag(56, body.bytes);
}

void Writer::doABC(const std::string& name, const std::vector<uint8_t>& abc, bool lazy)
{
    Bits body;
    body.u32(lazy ? 1u : 0u);
    putString(body, name);
    body.append(abc);
    tag(82, body.bytes);
}

void Writer::symbolClass(const std::vector<std::pair<int, std::string>>& classes)
{
    if (classes.empty())
        return;
    Bits body;
    body.u16(static_cast<int>(std::min<size_t>(classes.size(), 0xFFFF)));
    for (const auto& [id, name] : classes)
    {
        body.u16(id);
        putString(body, name);
    }
    tag(76, body.bytes);
}

void Writer::setActionScript3(bool on)
{
    if (_attributesAt < _tags.size())
        _tags[_attributesAt] = on ? 0x08 : 0x00;
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
