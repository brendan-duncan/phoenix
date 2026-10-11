#include "binary_fla.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>

namespace BinaryFla {

namespace {

constexpr int32_t kIntMin = INT32_MIN;

struct ParseError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Text

/// Windows-1252 bytes 0x80 to 0x9F, as Unicode.
const uint16_t kCp1252High[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
    0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};

void appendUtf8(std::string& out, uint32_t c)
{
    if (c < 0x80)
        out += static_cast<char>(c);
    else if (c < 0x800)
    {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string fromAnsi(const uint8_t* bytes, size_t count)
{
    std::string out;
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t b = bytes[i];
        appendUtf8(out, b >= 0x80 && b < 0xA0 ? kCp1252High[b - 0x80] : b);
    }
    return out;
}

std::string fromUtf16(const uint8_t* bytes, size_t units)
{
    std::string out;
    for (size_t i = 0; i < units; ++i)
    {
        uint32_t c = static_cast<uint32_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < units)
        {
            const uint32_t low = static_cast<uint32_t>(bytes[2 * i + 2] | (bytes[2 * i + 3] << 8));
            if (low >= 0xDC00 && low < 0xE000)
            {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        appendUtf8(out, c);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The model read from the streams

struct Color
{
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Matrix
{
    double a = 1, b = 0, c = 0, d = 1;
    int32_t tx = 0, ty = 0; // twips
};

struct Fill
{
    enum class Type
    {
        Solid,
        Linear,
        Radial,
        Bitmap
    };
    Type type = Type::Solid;
    Color color;
    Matrix matrix;
    std::vector<std::pair<uint8_t, Color>> stops;
    int media = 0;
    bool clipped = false;
    bool smoothed = true;
};

struct Stroke
{
    Color color;
    int width = 0; // twips
    bool hasFill = false;
    Fill fill;
};

struct EdgeRecord
{
    int64_t fx, fy, cx, cy, tx, ty; // 1/256 twip
    bool straight = true;
    bool move = false;
    int line = 0, fill0 = 0, fill1 = 0;
};

struct ShapeData
{
    std::vector<Fill> fills;
    std::vector<Stroke> strokes;
    std::vector<EdgeRecord> edges;
};

struct TextRun
{
    int size = 0; // twips
    std::string font;
    Color color;
    bool bold = false;
    bool italic = false;
    int alignment = 0;
    int lineSpacing = 0;
    int letterSpacing = 0;
    std::string text;
};

struct ColorEffect
{
    double multiply[4] = {1, 1, 1, 1}; // alpha, red, green, blue
    int add[4] = {0, 0, 0, 0};
};

struct ItemHeader
{
    std::string folder;
    std::string itemId;
};

/// Any object a stream holds: one struct for every class, using the fields
/// its class has.
struct Object
{
    std::string cls;

    // CPicObj
    uint8_t objFlags = 0;
    std::vector<Object*> children;
    bool hasPoint = false;
    int32_t pointX = 0, pointY = 0;

    // CPicShape and CPicFrame
    bool hasShape = false;
    Matrix matrix;
    ShapeData shape;

    // CPicFrame
    int duration = 1;
    int keyMode = 0;
    int ease = 0;
    int sound = 0;
    int soundSync = 0;
    int soundLoop = 0;
    uint32_t soundIn = 0;
    int32_t soundOut = 0;
    std::string label;
    std::string script;
    bool morph = false;

    // CPicLayer
    std::string name;
    bool current = false, hidden = false, locked = false, autoNamed = true;
    Color color;
    int layerType = 0;
    Object* parent = nullptr;

    // Instances
    int firstFrame = 0;
    int loop = 0;
    ColorEffect effect;
    int colorMode = 0;
    int colorPercent = 0;
    Color tint;
    uint32_t symbol = 0;
    std::string instanceName;

    // CPicText
    int32_t bounds[4] = {0, 0, 0, 0};
    std::vector<TextRun> runs;

    // Contents records
    std::string stream;
    int number = 0;
    int symbolType = 0;
    ItemHeader item;
    uint32_t lastModified = 0;
    int quality = 0;
    bool allowSmoothing = false;
    int format = 0;
    uint32_t sampleCount = 0;
    std::string importPath;
};

// ---------------------------------------------------------------------------
// MFC CArchive reading

class Reader
{
public:
    Reader(const std::vector<uint8_t>& data, const std::string& name, std::vector<std::unique_ptr<Object>>& pool)
        : _data(data)
        , _name(name)
        , _pool(pool)
    {
        _load.push_back({});
    }

    size_t position() const { return _at; }
    size_t size() const { return _data.size(); }

    [[noreturn]] void fail(const std::string& what) const
    {
        char at[32];
        std::snprintf(at, sizeof(at), " at 0x%zx", _at);
        throw ParseError(_name + ": " + what + at);
    }

    void need(size_t n) const
    {
        if (_at + n > _data.size())
            fail("read past the end");
    }

    uint8_t u8()
    {
        need(1);
        return _data[_at++];
    }

    uint16_t u16()
    {
        need(2);
        const uint16_t v = static_cast<uint16_t>(_data[_at] | (_data[_at + 1] << 8));
        _at += 2;
        return v;
    }

    int16_t s16() { return static_cast<int16_t>(u16()); }

    uint32_t u32()
    {
        need(4);
        const uint32_t v = static_cast<uint32_t>(_data[_at]) | (static_cast<uint32_t>(_data[_at + 1]) << 8) |
            (static_cast<uint32_t>(_data[_at + 2]) << 16) | (static_cast<uint32_t>(_data[_at + 3]) << 24);
        _at += 4;
        return v;
    }

    int32_t s32() { return static_cast<int32_t>(u32()); }

    void skip(size_t n)
    {
        need(n);
        _at += n;
    }

    /// An MFC CString: an ANSI or (marked FF FE FF) UTF-16 string with a
    /// length of one, three or seven bytes.
    std::string cstring()
    {
        bool unicode = false;
        uint32_t n = u8();
        if (n == 0xFF)
        {
            n = u16();
            if (n == 0xFFFE)
            {
                unicode = true;
                n = u8();
                if (n == 0xFF)
                {
                    n = u16();
                    if (n == 0xFFFF)
                        n = u32();
                }
            }
            else if (n == 0xFFFF)
            {
                n = u32();
            }
        }
        if (unicode)
        {
            need(2 * static_cast<size_t>(n));
            const std::string s = fromUtf16(&_data[_at], n);
            _at += 2 * static_cast<size_t>(n);
            return s;
        }
        need(n);
        const std::string s = fromAnsi(n ? &_data[_at] : nullptr, n);
        _at += n;
        return s;
    }

    /// A count byte and that many UTF-16 units.
    std::string wide8()
    {
        const uint8_t n = u8();
        need(2 * static_cast<size_t>(n));
        const std::string s = fromUtf16(n ? &_data[_at] : nullptr, n);
        _at += 2 * static_cast<size_t>(n);
        return s;
    }

    /// A count byte and that many ANSI bytes.
    std::string ansi8()
    {
        const uint8_t n = u8();
        need(n);
        const std::string s = fromAnsi(n ? &_data[_at] : nullptr, n);
        _at += n;
        return s;
    }

    Color color()
    {
        Color c;
        c.r = u8();
        c.g = u8();
        c.b = u8();
        c.a = u8();
        return c;
    }

    Matrix matrix()
    {
        Matrix m;
        m.a = s32() / 65536.0;
        m.b = s32() / 65536.0;
        m.c = s32() / 65536.0;
        m.d = s32() / 65536.0;
        m.tx = s32();
        m.ty = s32();
        return m;
    }

    std::string itemId()
    {
        const uint32_t time = u32();
        const uint32_t counter = u32();
        if (time == 0 && counter == 0)
            return std::string();
        char text[24];
        std::snprintf(text, sizeof(text), "%08x-%08x", time, counter);
        return text;
    }

    using Serializer = void (*)(Reader&, Object&);

    /// CArchive::ReadObject: null, a new object of a class declared here or
    /// earlier, or one already read.
    Object* readObject(const std::map<std::string, Serializer>& serializers)
    {
        const uint16_t tag = u16();
        if (tag == 0)
            return nullptr;
        std::string cls;
        if (tag == 0xFFFF)
        {
            u16(); // the class schema
            const uint16_t length = u16();
            need(length);
            cls.assign(reinterpret_cast<const char*>(&_data[_at]), length);
            _at += length;
            _load.push_back({true, cls, nullptr});
        }
        else if (tag == 0x7FFF)
        {
            fail("a 32-bit class tag");
        }
        else if (tag & 0x8000)
        {
            const size_t index = tag & 0x7FFF;
            if (index >= _load.size() || !_load[index].isClass)
                fail("a bad class index");
            cls = _load[index].cls;
        }
        else
        {
            if (tag >= _load.size() || _load[tag].isClass || !_load[tag].object)
                fail("a bad object reference");
            return _load[tag].object;
        }
        const auto serializer = serializers.find(cls);
        if (serializer == serializers.end())
            fail("an object of class " + cls + ", which Phoenix does not know");
        _pool.push_back(std::make_unique<Object>());
        Object* object = _pool.back().get();
        object->cls = cls;
        _load.push_back({false, cls, object});
        serializer->second(*this, *object);
        return object;
    }

    /// Whether the next object is of class \a cls (without reading it).
    bool nextIs(const std::string& cls) const
    {
        if (_at + 2 > _data.size())
            return false;
        const uint16_t tag = static_cast<uint16_t>(_data[_at] | (_data[_at + 1] << 8));
        if (tag == 0xFFFF && _at + 6 <= _data.size())
        {
            const uint16_t length = static_cast<uint16_t>(_data[_at + 4] | (_data[_at + 5] << 8));
            return _at + 6 + length <= _data.size() &&
                std::string(reinterpret_cast<const char*>(&_data[_at + 6]), length) == cls;
        }
        if ((tag & 0x8000) && tag != 0xFFFF)
        {
            const size_t index = tag & 0x7FFF;
            return index < _load.size() && _load[index].isClass && _load[index].cls == cls;
        }
        return false;
    }

private:
    struct Loaded
    {
        bool isClass = false;
        std::string cls;
        Object* object = nullptr;
    };

    const std::vector<uint8_t>& _data;
    std::string _name;
    std::vector<std::unique_ptr<Object>>& _pool;
    std::vector<Loaded> _load;
    size_t _at = 0;
};

// ---------------------------------------------------------------------------
// Timeline streams: Page N, Symbol N

extern const std::map<std::string, Reader::Serializer> kTimeline;

void readPicObj(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.objFlags = r.u8();
    while (Object* child = r.readObject(kTimeline))
        o.children.push_back(child);
    o.pointX = r.s32();
    o.pointY = r.s32();
    o.hasPoint = !(o.pointX == kIntMin && o.pointY == kIntMin);
    if (schema >= 5)
        r.skip(2);
}

Fill readFill(Reader& r, int shapeSchema)
{
    Fill f;
    f.color = r.color();
    const uint16_t kind = r.u16();
    const int type = kind & 0xFF;
    if (type == 0x00)
    {
        f.type = Fill::Type::Solid;
    }
    else if (type == 0x10 || type == 0x12 || type == 0x13)
    {
        f.type = type == 0x10 ? Fill::Type::Linear : Fill::Type::Radial;
        f.matrix = r.matrix();
        const int stops = r.u8();
        if (shapeSchema >= 3)
            r.skip(8);
        for (int i = 0; i < stops; ++i)
        {
            const uint8_t ratio = r.u8();
            f.stops.emplace_back(ratio, r.color());
        }
    }
    else if (type >= 0x40 && type <= 0x43)
    {
        f.type = Fill::Type::Bitmap;
        f.clipped = (type & 1) != 0;
        f.smoothed = (type & 2) == 0;
        f.matrix = r.matrix();
        f.media = r.u16();
    }
    else
    {
        r.fail("an unknown fill kind");
    }
    return f;
}

Stroke readStroke(Reader& r, int shapeSchema)
{
    Stroke s;
    s.color = r.color();
    s.width = r.u16();
    r.skip(4);
    if (shapeSchema >= 3)
    {
        r.skip(6);
        s.fill = readFill(r, shapeSchema);
        s.hasFill = true;
    }
    return s;
}

void readShapeData(Reader& r, ShapeData& shape, int shapeSchema)
{
    const uint8_t dataSchema = r.u8();
    r.u32(); // an edge count hint
    const uint16_t fills = r.u16();
    for (uint16_t i = 0; i < fills; ++i)
        shape.fills.push_back(readFill(r, shapeSchema));
    const uint16_t strokes = r.u16();
    for (uint16_t i = 0; i < strokes; ++i)
        shape.strokes.push_back(readStroke(r, shapeSchema));

    int64_t x = 0;
    int64_t y = 0;
    int styles[3] = {0, 0, 0};
    for (;;)
    {
        const uint8_t f = r.u8();
        if (f == 0)
            break;
        if (f & 0x40)
        {
            for (int& style : styles)
                style = (f & 0x80) ? r.u8() : (r.u16() & 0x7FFF);
        }
        int64_t d[3][2];
        for (int k = 0; k < 3; ++k)
        {
            switch ((f >> (2 * k)) & 3)
            {
            case 0:
                d[k][0] = d[k][1] = 0;
                break;
            case 1:
                d[k][0] = r.s16();
                d[k][1] = r.s16();
                break;
            case 2:
                d[k][0] = r.s32();
                d[k][1] = r.s32();
                break;
            default:
                d[k][0] = static_cast<int64_t>(r.s16()) * 128;
                d[k][1] = static_cast<int64_t>(r.s16()) * 128;
                break;
            }
        }
        EdgeRecord e;
        e.fx = x + d[0][0];
        e.fy = y + d[0][1];
        e.cx = e.fx + d[1][0];
        e.cy = e.fy + d[1][1];
        e.tx = e.fx + d[2][0];
        e.ty = e.fy + d[2][1];
        e.straight = (f & 0x0C) == 0;
        if (e.straight && shapeSchema >= 3)
            r.u8();
        e.move = d[0][0] != 0 || d[0][1] != 0 || shape.edges.empty();
        e.line = styles[0];
        e.fill0 = styles[1];
        e.fill1 = styles[2];
        shape.edges.push_back(e);
        x = e.tx;
        y = e.ty;
    }
    if (dataSchema >= 5)
    {
        const int32_t cubics = r.s32();
        if (cubics < 0 || cubics > 1000000)
            r.fail("a bad cubic count");
        r.skip(static_cast<size_t>(cubics) * 32);
    }
}

void readShapeBody(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.hasShape = true;
    o.matrix = r.matrix();
    readShapeData(r, o.shape, schema);
}

std::string readActions(Reader& r)
{
    const uint32_t version = r.u32();
    r.skip(12);
    if (version >= 5)
    {
        r.u32();
        return r.cstring();
    }
    return std::string();
}

void readShape(Reader& r, Object& o)
{
    readPicObj(r, o);
    readShapeBody(r, o);
}

void readFrame(Reader& r, Object& o)
{
    readPicObj(r, o);
    readShapeBody(r, o);
    const uint8_t schema = r.u8();
    o.duration = r.u16();
    o.keyMode = r.u16();
    o.ease = r.s16();
    o.sound = r.u16();
    const uint16_t envelope = r.u16();
    r.skip(static_cast<size_t>(envelope) * 8);
    o.soundLoop = r.u16();
    o.soundSync = r.u8();
    o.soundIn = r.u32();
    o.soundOut = r.s32();
    r.u16(); // sound zoom
    o.label = r.cstring();
    o.script = readActions(r);
    r.skip(12);
    o.morph = r.readObject(kTimeline) != nullptr;
    r.u32();
    r.readObject(kTimeline);
    r.cstring();
    r.skip(12);
    if (schema >= 24)
        r.skip(8);
}

void readLayer(Reader& r, Object& o)
{
    readPicObj(r, o);
    r.u8(); // layer schema
    o.name = r.cstring();
    o.current = r.u8() != 0;
    o.hidden = r.u8() != 0;
    o.locked = r.u8() != 0;
    r.u32();
    o.color = r.color();
    r.u32(); // outline mode
    r.u32(); // height
    o.layerType = r.u8();
    o.parent = r.readObject(kTimeline);
    r.u8();
    o.autoNamed = r.u8() != 0;
}

void readPage(Reader& r, Object& o)
{
    readPicObj(r, o);
    const uint8_t schema = r.u8();
    r.u16();
    if (schema >= 5)
        r.u16();
    if (schema >= 7)
        r.u32();
    const uint32_t guides = r.u32();
    r.skip(static_cast<size_t>(guides) * 8);
}

void readInstance(Reader& r, Object& o)
{
    readPicObj(r, o);
    const uint8_t schema = r.u8();
    o.matrix = r.matrix();
    o.firstFrame = r.u16();
    o.loop = r.u16();
    r.u8();
    for (int i = 0; i < 4; ++i)
    {
        o.effect.multiply[i] = r.u16() / 256.0;
        o.effect.add[i] = r.s16();
    }
    o.colorMode = r.u16();
    o.colorPercent = r.s16();
    o.tint = r.color();
    o.instanceName = r.cstring();
    o.symbol = r.u32();
    if (schema >= 19)
        r.skip(7);
}

void readSprite(Reader& r, Object& o)
{
    readInstance(r, o);
    const uint8_t schema = r.u8();
    o.script = readActions(r);
    const std::string name = r.cstring();
    if (!name.empty())
        o.instanceName = name;
    r.u32();
    r.u8();
    r.skip(12);
    if (schema >= 8)
    {
        r.u8();
        r.u32();
        r.cstring(); // component metadata
    }
}

void readButton(Reader& r, Object& o)
{
    readInstance(r, o);
    r.u8();
    o.script = readActions(r);
    r.u8();
    const std::string name = r.cstring();
    if (!name.empty())
        o.instanceName = name;
    r.u32();
}

void readText(Reader& r, Object& o)
{
    readPicObj(r, o);
    const uint8_t schema = r.u8();
    o.matrix = r.matrix();
    for (int32_t& bound : o.bounds)
        bound = r.s32();
    r.u8();
    r.u8();
    r.u32();
    r.u16();
    r.cstring();
    for (;;)
    {
        const uint16_t count = r.u16();
        if (count == 0)
            break;
        TextRun run;
        const uint8_t runSchema = r.u8();
        run.size = r.u16();
        const bool wide = runSchema >= 10;
        run.font = wide ? r.wide8() : r.ansi8();
        run.color = r.color();
        const size_t attrs = runSchema == 7 ? 23 : runSchema == 12 ? 35 : 0;
        if (attrs == 0)
            r.fail("a text run of a schema Phoenix does not know");
        std::vector<uint8_t> a(attrs);
        for (uint8_t& byte : a)
            byte = r.u8();
        run.bold = a[2] != 0;
        run.italic = a[3] != 0;
        run.alignment = a[7];
        run.lineSpacing = static_cast<int16_t>(a[8] | (a[9] << 8));
        run.letterSpacing = static_cast<int16_t>(a[16] | (a[17] << 8));
        if (wide)
        {
            std::vector<uint8_t> bytes(2 * static_cast<size_t>(count));
            for (uint8_t& byte : bytes)
                byte = r.u8();
            run.text = fromUtf16(bytes.data(), count);
        }
        else
        {
            std::vector<uint8_t> bytes(count);
            for (uint8_t& byte : bytes)
                byte = r.u8();
            run.text = fromAnsi(bytes.data(), count);
        }
        o.runs.push_back(run);
    }
    r.cstring();
    r.u32();
    r.u32();
    if (schema >= 11)
        r.cstring();
    if (schema >= 12)
        r.cstring();
    if (schema >= 13)
    {
        if (r.u8())
            r.fail("text filters, which Phoenix cannot read yet");
        r.u16();
    }
}

const std::map<std::string, Reader::Serializer> kTimeline = {
    {"CPicPage", readPage},
    {"CPicLayer", readLayer},
    {"CPicFrame", readFrame},
    {"CPicShape", readShape},
    {"CPicSymbol", readInstance},
    {"CPicSprite", readSprite},
    {"CPicButton", readButton},
    {"CPicText", readText},
};

// ---------------------------------------------------------------------------
// The Contents stream

void readLinkage(Reader& r)
{
    const uint32_t version = r.u32();
    r.u8();
    r.cstring();
    r.cstring();
    r.cstring();
    if (version >= 7)
        r.u8();
    r.u32();
}

void readSource(Reader& r)
{
    r.cstring();
    r.cstring();
    r.skip(12);
    r.itemId();
}

ItemHeader readItem(Reader& r)
{
    ItemHeader item;
    const uint32_t version = r.u32();
    r.u32();
    item.folder = r.itemId();
    r.u32();
    item.itemId = r.itemId();
    r.u32();
    readLinkage(r);
    readSource(r);
    if (version >= 6)
    {
        r.u8();
        r.cstring();
        r.u8();
    }
    return item;
}

void readDocumentPage(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.stream = schema >= 24 ? r.wide8() : r.ansi8();
    o.name = r.cstring();
    o.number = static_cast<int>(r.u32());
    o.symbolType = r.u8();
    r.cstring();
    r.u32();
    o.item = readItem(r);
    o.lastModified = r.u32();
    r.cstring();
    r.cstring();
    r.u32();
    r.u8();
    r.u32();
    r.u32();
    readLinkage(r);
    readSource(r);
    if (schema >= 24)
    {
        r.u8();
        r.cstring();
        r.u32();
        r.u8();
        r.cstring();
        r.skip(12);
        r.cstring();
        for (int i = 0; i < 3; ++i)
        {
            r.u8();
            r.cstring();
            r.skip(8);
        }
        r.cstring();
        r.u32();
        r.u8();
        r.u32();
        r.cstring();
        r.skip(16);
        r.cstring();
        r.skip(8);
        r.cstring();
        r.cstring();
        r.u32();
        r.cstring();
        r.u32();
        r.skip(16); // scale-9 rectangle
        r.skip(9);
    }
    else
    {
        r.skip(34);
    }
}

void readMediaCommon(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.stream = schema >= 6 ? r.wide8() : r.ansi8();
    o.name = r.cstring();
    o.number = r.u16();
    o.importPath = r.cstring();
    o.lastModified = r.u32();
    o.item = readItem(r);
}

void readMediaBits(Reader& r, Object& o)
{
    readMediaCommon(r, o);
    r.u32();
    r.u16();
    o.quality = r.u8();
    o.allowSmoothing = r.u8() != 0;
    r.u32();
}

void readMediaSound(Reader& r, Object& o)
{
    readMediaCommon(r, o);
    r.u32();
    r.u8();
    o.format = r.u16();
    o.sampleCount = r.u32();
    r.u32();
    r.skip(11);
}

void readColorDef(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.color = r.color();
    const uint16_t kind = r.u16();
    if (kind & 0xF0)
    {
        r.matrix();
        const int stops = r.u8();
        if (schema >= 4)
            r.skip(8);
        r.skip(static_cast<size_t>(stops) * 5);
    }
    r.skip(6);
}

void readQtAudio(Reader& r, Object&)
{
    r.u32();
}

const std::map<std::string, Reader::Serializer> kContents = {
    {"CDocumentPage", readDocumentPage},
    {"CMediaBits", readMediaBits},
    {"CMediaSound", readMediaSound},
    {"CColorDef", readColorDef},
    {"CQTAudioSettings", readQtAudio},
};

struct Folder
{
    std::string name;
    ItemHeader item;
};

struct Contents
{
    std::vector<Object*> scenes;
    std::vector<Object*> symbols;
    std::vector<Object*> media;
    std::vector<Folder> folders;
    double width = 550;
    double height = 400;
    double frameRate = 12;
    Color background{255, 255, 255, 255};
};

std::vector<Object*> readList(Reader& r)
{
    std::vector<Object*> list;
    while (Object* object = r.readObject(kContents))
        list.push_back(object);
    return list;
}

Contents readContents(Reader& r)
{
    Contents c;
    const uint16_t version = r.u16();
    if (version != 0x129 && version != 0x143)
        r.fail("a Contents version Phoenix does not know");
    r.skip(version >= 0x143 ? 25 : 13);
    c.scenes = readList(r);
    r.skip(6);
    c.symbols = readList(r);
    r.u16();
    c.media = readList(r);
    r.u16();

    r.u8();
    r.u16();
    int32_t rect[4];
    for (int32_t& v : rect)
        v = r.s32();
    c.width = (rect[1] - rect[0]) / 20.0;
    c.height = (rect[3] - rect[2]) / 20.0;
    r.skip(2 + 2 + 1 + 2 + 2);
    r.skip(16); // grid and snap spacing
    r.skip(12);
    c.background = r.color();
    r.color(); // grid color
    c.frameRate = r.u32() / 65536.0;
    r.skip(7);

    const auto pairs = [&r]() {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n; ++i)
        {
            r.cstring();
            r.cstring();
        }
    };
    pairs();
    if (version >= 0x143)
    {
        const uint32_t profiles = r.u32();
        for (uint32_t i = 0; i < profiles; ++i)
            pairs();
    }
    r.u32();
    r.cstring();
    r.cstring();
    r.u32();
    r.u16();
    for (int list = 0; list < 2; ++list)
    {
        const uint16_t swatches = r.u16();
        for (uint16_t i = 0; i < swatches; ++i)
            r.readObject(kContents);
        r.u8();
    }
    r.skip(8 + 16 + 24);
    const uint32_t folders = r.u32();
    for (uint32_t i = 0; i < folders; ++i)
    {
        Folder folder;
        r.u32();
        folder.name = r.cstring();
        folder.item = readItem(r);
        c.folders.push_back(folder);
    }
    // The rest (named settings and the save history) says nothing Phoenix
    // shows.
    return c;
}

// ---------------------------------------------------------------------------
// Writing XFL

std::string hexColor(const Color& c)
{
    char text[8];
    std::snprintf(text, sizeof(text), "#%02X%02X%02X", c.r, c.g, c.b);
    return text;
}

std::string number(double v)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%.10g", v);
    return text;
}

/// A coordinate in 1/256 twips as an XFL edge number, in twips.
std::string edgeNumber(int64_t v)
{
    if (v % 256 == 0)
        return std::to_string(v / 256);
    char text[64];
    std::snprintf(text, sizeof(text), "%.8f", v / 256.0);
    std::string s = text;
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

class Writer
{
public:
    Writer(const Contents& contents, const std::map<std::string, Object*>& timelines,
        const std::map<std::string, std::vector<uint8_t>>& streams)
        : _contents(contents)
        , _timelines(timelines)
        , _streams(streams)
    {
        for (const Folder& folder : contents.folders)
            _folders[folder.item.itemId] = &folder;
        for (const Object* symbol : contents.symbols)
            _symbolPaths[symbol->number] = libraryPath(symbol->item.folder, symbol->name);
        for (const Object* media : contents.media)
            _mediaPaths[media->number] = libraryPath(media->item.folder, media->name);
    }

    void write(std::map<std::string, std::vector<uint8_t>>& files)
    {
        tinyxml2::XMLDocument doc;
        tinyxml2::XMLElement* root = doc.NewElement("DOMDocument");
        doc.InsertEndChild(root);
        root->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
        root->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
        root->SetAttribute("width", number(_contents.width).c_str());
        root->SetAttribute("height", number(_contents.height).c_str());
        root->SetAttribute("frameRate", number(_contents.frameRate).c_str());
        root->SetAttribute("backgroundColor", hexColor(_contents.background).c_str());

        if (!_contents.folders.empty())
        {
            tinyxml2::XMLElement* folders = doc.NewElement("folders");
            for (const Folder& folder : _contents.folders)
            {
                tinyxml2::XMLElement* item = doc.NewElement("DOMFolderItem");
                item->SetAttribute("name", libraryPath(folder.item.folder, folder.name).c_str());
                if (!folder.item.itemId.empty())
                    item->SetAttribute("itemID", folder.item.itemId.c_str());
                folders->InsertEndChild(item);
            }
            root->InsertEndChild(folders);
        }

        if (!_contents.media.empty())
        {
            tinyxml2::XMLElement* media = doc.NewElement("media");
            for (const Object* m : _contents.media)
                writeMedia(doc, media, *m, files);
            root->InsertEndChild(media);
        }

        if (!_contents.symbols.empty())
        {
            tinyxml2::XMLElement* symbols = doc.NewElement("symbols");
            for (const Object* symbol : _contents.symbols)
            {
                const std::string path = _symbolPaths[symbol->number];
                tinyxml2::XMLElement* include = doc.NewElement("Include");
                include->SetAttribute("href", (path + ".xml").c_str());
                if (!symbol->item.itemId.empty())
                    include->SetAttribute("itemID", symbol->item.itemId.c_str());
                symbols->InsertEndChild(include);
                files["LIBRARY/" + path + ".xml"] = symbolFile(*symbol, path);
            }
            root->InsertEndChild(symbols);
        }

        tinyxml2::XMLElement* timelines = doc.NewElement("timelines");
        for (const Object* scene : _contents.scenes)
        {
            const auto found = _timelines.find(scene->stream);
            timelines->InsertEndChild(timeline(doc, scene->name, found != _timelines.end() ? found->second : nullptr));
        }
        root->InsertEndChild(timelines);
        files["DOMDocument.xml"] = print(doc);
    }

private:
    static std::vector<uint8_t> print(tinyxml2::XMLDocument& doc)
    {
        tinyxml2::XMLPrinter printer;
        doc.Print(&printer);
        const char* text = printer.CStr();
        return std::vector<uint8_t>(text, text + std::strlen(text));
    }

    std::string libraryPath(const std::string& folder, const std::string& name) const
    {
        std::vector<std::string> parts;
        std::set<std::string> seen;
        for (std::string id = folder; !id.empty() && seen.insert(id).second;)
        {
            const auto found = _folders.find(id);
            if (found == _folders.end())
                break;
            parts.push_back(found->second->name);
            id = found->second->item.folder;
        }
        std::string path;
        for (auto part = parts.rbegin(); part != parts.rend(); ++part)
            path += *part + "/";
        return path + name;
    }

    void writeMedia(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const Object& m,
        std::map<std::string, std::vector<uint8_t>>& files)
    {
        const std::string path = _mediaPaths[m.number];
        const std::string dat = m.stream + ".dat";
        const auto data = _streams.find(m.stream);
        if (data != _streams.end())
            files["bin/" + dat] = data->second;
        if (m.cls == "CMediaBits")
        {
            tinyxml2::XMLElement* item = doc.NewElement("DOMBitmapItem");
            item->SetAttribute("name", path.c_str());
            if (!m.item.itemId.empty())
                item->SetAttribute("itemID", m.item.itemId.c_str());
            item->SetAttribute("sourceLastImported", m.lastModified);
            if (m.allowSmoothing)
                item->SetAttribute("allowSmoothing", "true");
            const bool jpeg = data != _streams.end() && data->second.size() > 2 && data->second[0] == 0xFF &&
                data->second[1] == 0xD8;
            item->SetAttribute("originalCompressionType", jpeg ? "photo" : "lossless");
            item->SetAttribute("quality", m.quality);
            item->SetAttribute("href", path.c_str());
            item->SetAttribute("bitmapDataHRef", dat.c_str());
            // A lossless bitmap's header gives its size in twips.
            if (!jpeg && data != _streams.end() && data->second.size() >= 26 && data->second[0] == 0x03)
            {
                const auto s32 = [&](size_t at) {
                    const std::vector<uint8_t>& b = data->second;
                    return static_cast<int32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
                };
                item->SetAttribute("frameRight", s32(12));
                item->SetAttribute("frameBottom", s32(20));
            }
            parent->InsertEndChild(item);
        }
        else
        {
            tinyxml2::XMLElement* item = doc.NewElement("DOMSoundItem");
            item->SetAttribute("name", path.c_str());
            if (!m.item.itemId.empty())
                item->SetAttribute("itemID", m.item.itemId.c_str());
            item->SetAttribute("sourceLastImported", m.lastModified);
            item->SetAttribute("soundDataHRef", dat.c_str());
            static const char* rates[4] = {"5kHz", "11kHz", "22kHz", "44kHz"};
            const std::string format = std::string(rates[(m.format >> 2) & 3]) + ((m.format & 2) ? " 16bit" : " 8bit") +
                ((m.format & 1) ? " Stereo" : " Mono");
            item->SetAttribute("format", format.c_str());
            item->SetAttribute("sampleCount", m.sampleCount);
            parent->InsertEndChild(item);
        }
    }

    std::vector<uint8_t> symbolFile(const Object& symbol, const std::string& path)
    {
        tinyxml2::XMLDocument doc;
        tinyxml2::XMLElement* root = doc.NewElement("DOMSymbolItem");
        doc.InsertEndChild(root);
        root->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
        root->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
        root->SetAttribute("name", path.c_str());
        if (!symbol.item.itemId.empty())
            root->SetAttribute("itemID", symbol.item.itemId.c_str());
        if (symbol.symbolType == 0)
            root->SetAttribute("symbolType", "graphic");
        else if (symbol.symbolType == 1)
            root->SetAttribute("symbolType", "button");
        root->SetAttribute("lastModified", symbol.lastModified);
        tinyxml2::XMLElement* holder = doc.NewElement("timeline");
        const auto found = _timelines.find(symbol.stream);
        const std::string name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
        holder->InsertEndChild(timeline(doc, name, found != _timelines.end() ? found->second : nullptr));
        root->InsertEndChild(holder);
        return print(doc);
    }

    tinyxml2::XMLElement* timeline(tinyxml2::XMLDocument& doc, const std::string& name, const Object* page)
    {
        tinyxml2::XMLElement* timeline = doc.NewElement("DOMTimeline");
        timeline->SetAttribute("name", name.c_str());
        tinyxml2::XMLElement* layers = doc.NewElement("layers");
        if (page)
        {
            // Stored bottom first; XFL lists the top layer first.
            std::vector<const Object*> order(page->children.rbegin(), page->children.rend());
            for (const Object* layer : order)
            {
                if (!layer || layer->cls != "CPicLayer")
                    continue;
                tinyxml2::XMLElement* node = doc.NewElement("DOMLayer");
                node->SetAttribute("name", layer->name.c_str());
                node->SetAttribute("color", hexColor(layer->color).c_str());
                if (layer->layerType == 1)
                    node->SetAttribute("layerType", "guide");
                else if (layer->layerType == 3)
                    node->SetAttribute("layerType", "folder");
                if (layer->parent)
                {
                    const auto parent = std::find(order.begin(), order.end(), layer->parent);
                    if (parent != order.end())
                        node->SetAttribute("parentLayerIndex", static_cast<int>(parent - order.begin()));
                }
                if (layer->current)
                    node->SetAttribute("current", "true");
                if (layer->locked)
                    node->SetAttribute("locked", "true");
                if (!layer->autoNamed)
                    node->SetAttribute("autoNamed", "false");
                if (layer->hidden)
                    node->SetAttribute("visible", "false");
                if (layer->layerType != 3)
                {
                    tinyxml2::XMLElement* frames = doc.NewElement("frames");
                    int index = 0;
                    for (const Object* frame : layer->children)
                    {
                        if (!frame || frame->cls != "CPicFrame")
                            continue;
                        frames->InsertEndChild(writeFrame(doc, *frame, index));
                        index += std::max(1, frame->duration);
                    }
                    node->InsertEndChild(frames);
                }
                layers->InsertEndChild(node);
            }
        }
        timeline->InsertEndChild(layers);
        return timeline;
    }

    tinyxml2::XMLElement* writeFrame(tinyxml2::XMLDocument& doc, const Object& frame, int index)
    {
        tinyxml2::XMLElement* node = doc.NewElement("DOMFrame");
        node->SetAttribute("index", index);
        if (frame.duration > 1)
            node->SetAttribute("duration", frame.duration);
        const bool motion = (frame.keyMode & 1) != 0;
        if (frame.morph)
            node->SetAttribute("tweenType", "shape");
        else if (motion)
            node->SetAttribute("tweenType", "motion");
        if ((motion || frame.morph) && frame.ease != 0)
            node->SetAttribute("acceleration", frame.ease);
        node->SetAttribute("keyMode", frame.keyMode | (motion ? 0x4000 : 0x2000));
        if (!frame.label.empty())
            node->SetAttribute("name", frame.label.c_str());
        if (frame.sound)
        {
            const auto sound = _mediaPaths.find(frame.sound);
            if (sound != _mediaPaths.end())
            {
                node->SetAttribute("soundName", sound->second.c_str());
                static const char* syncs[4] = {nullptr, "start", "stop", "stream"};
                if (frame.soundSync > 0 && frame.soundSync < 4)
                    node->SetAttribute("soundSync", syncs[frame.soundSync]);
                if (frame.soundLoop > 1)
                    node->SetAttribute("soundLoop", frame.soundLoop);
                if (frame.soundIn)
                    node->SetAttribute("inPoint44", frame.soundIn);
                if (frame.soundOut > 0 && frame.soundOut != 0x3FFFFFFF)
                    node->SetAttribute("outPoint44", frame.soundOut);
            }
        }
        if (!frame.script.empty())
        {
            tinyxml2::XMLElement* actions = doc.NewElement("Actionscript");
            tinyxml2::XMLElement* script = doc.NewElement("script");
            tinyxml2::XMLText* text = doc.NewText(frame.script.c_str());
            text->SetCData(true);
            script->InsertEndChild(text);
            actions->InsertEndChild(script);
            node->InsertEndChild(actions);
        }
        tinyxml2::XMLElement* elements = doc.NewElement("elements");
        // The frame's own drawing first, then what is placed on it.
        if (!frame.shape.edges.empty())
            elements->InsertEndChild(writeShape(doc, frame.matrix, frame.shape));
        for (const Object* element : frame.children)
            writeElement(doc, elements, element);
        node->InsertEndChild(elements);
        return node;
    }

    void writeElement(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const Object* element)
    {
        if (!element)
            return;
        if (element->cls == "CPicShape")
        {
            if (element->objFlags & 1)
            {
                parent->InsertEndChild(writeShape(doc, element->matrix, element->shape));
                for (const Object* child : element->children)
                    writeElement(doc, parent, child);
                return;
            }
            tinyxml2::XMLElement* group = doc.NewElement("DOMGroup");
            writeMatrix(doc, group, element->matrix);
            tinyxml2::XMLElement* members = doc.NewElement("members");
            if (!element->shape.edges.empty())
                members->InsertEndChild(writeShape(doc, element->matrix, element->shape));
            for (const Object* child : element->children)
                writeElement(doc, members, child);
            group->InsertEndChild(members);
            parent->InsertEndChild(group);
        }
        else if (element->cls == "CPicSymbol" || element->cls == "CPicSprite" || element->cls == "CPicButton")
        {
            parent->InsertEndChild(writeInstance(doc, *element));
        }
        else if (element->cls == "CPicText")
        {
            parent->InsertEndChild(writeText(doc, *element));
        }
    }

    static void writeMatrix(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const Matrix& m,
        const char* name = "matrix")
    {
        tinyxml2::XMLElement* holder = doc.NewElement(name);
        tinyxml2::XMLElement* matrix = doc.NewElement("Matrix");
        if (m.a != 1.0)
            matrix->SetAttribute("a", number(m.a).c_str());
        if (m.b != 0.0)
            matrix->SetAttribute("b", number(m.b).c_str());
        if (m.c != 0.0)
            matrix->SetAttribute("c", number(m.c).c_str());
        if (m.d != 1.0)
            matrix->SetAttribute("d", number(m.d).c_str());
        if (m.tx != 0)
            matrix->SetAttribute("tx", number(m.tx / 20.0).c_str());
        if (m.ty != 0)
            matrix->SetAttribute("ty", number(m.ty / 20.0).c_str());
        holder->InsertEndChild(matrix);
        parent->InsertEndChild(holder);
    }

    static void setColor(tinyxml2::XMLElement* node, const Color& c, const char* name = "color")
    {
        node->SetAttribute(name, hexColor(c).c_str());
        if (c.a != 255)
            node->SetAttribute("alpha", number(c.a / 255.0).c_str());
    }

    tinyxml2::XMLElement* writeFill(tinyxml2::XMLDocument& doc, const Fill& fill)
    {
        switch (fill.type)
        {
        case Fill::Type::Solid:
        {
            tinyxml2::XMLElement* solid = doc.NewElement("SolidColor");
            setColor(solid, fill.color);
            return solid;
        }
        case Fill::Type::Linear:
        case Fill::Type::Radial:
        {
            tinyxml2::XMLElement* gradient =
                doc.NewElement(fill.type == Fill::Type::Linear ? "LinearGradient" : "RadialGradient");
            writeMatrix(doc, gradient, fill.matrix);
            for (const auto& stop : fill.stops)
            {
                tinyxml2::XMLElement* entry = doc.NewElement("GradientEntry");
                setColor(entry, stop.second);
                entry->SetAttribute("ratio", number(stop.first / 255.0).c_str());
                gradient->InsertEndChild(entry);
            }
            return gradient;
        }
        case Fill::Type::Bitmap:
        {
            tinyxml2::XMLElement* bitmap = doc.NewElement("BitmapFill");
            const auto media = _mediaPaths.find(fill.media);
            if (media != _mediaPaths.end())
                bitmap->SetAttribute("bitmapPath", media->second.c_str());
            if (!fill.clipped)
                bitmap->SetAttribute("bitmapIsClipped", "false");
            else
                bitmap->SetAttribute("bitmapIsClipped", "true");
            writeMatrix(doc, bitmap, fill.matrix);
            return bitmap;
        }
        }
        return doc.NewElement("SolidColor");
    }

    tinyxml2::XMLElement* writeShape(tinyxml2::XMLDocument& doc, const Matrix& matrix, const ShapeData& shape)
    {
        tinyxml2::XMLElement* node = doc.NewElement("DOMShape");
        writeMatrix(doc, node, matrix);
        if (!shape.fills.empty())
        {
            tinyxml2::XMLElement* fills = doc.NewElement("fills");
            for (size_t i = 0; i < shape.fills.size(); ++i)
            {
                tinyxml2::XMLElement* style = doc.NewElement("FillStyle");
                style->SetAttribute("index", static_cast<int>(i + 1));
                style->InsertEndChild(writeFill(doc, shape.fills[i]));
                fills->InsertEndChild(style);
            }
            node->InsertEndChild(fills);
        }
        if (!shape.strokes.empty())
        {
            tinyxml2::XMLElement* strokes = doc.NewElement("strokes");
            for (size_t i = 0; i < shape.strokes.size(); ++i)
            {
                const Stroke& stroke = shape.strokes[i];
                tinyxml2::XMLElement* style = doc.NewElement("StrokeStyle");
                style->SetAttribute("index", static_cast<int>(i + 1));
                tinyxml2::XMLElement* solid = doc.NewElement("SolidStroke");
                solid->SetAttribute("scaleMode", "normal");
                solid->SetAttribute("weight", number(stroke.width / 20.0).c_str());
                tinyxml2::XMLElement* fill = doc.NewElement("fill");
                if (stroke.hasFill && stroke.fill.type != Fill::Type::Solid)
                {
                    fill->InsertEndChild(writeFill(doc, stroke.fill));
                }
                else
                {
                    tinyxml2::XMLElement* color = doc.NewElement("SolidColor");
                    setColor(color, stroke.color);
                    fill->InsertEndChild(color);
                }
                solid->InsertEndChild(fill);
                style->InsertEndChild(solid);
                strokes->InsertEndChild(style);
            }
            node->InsertEndChild(strokes);
        }
        if (!shape.edges.empty())
        {
            // One Edge per run of pieces with the same styles.
            tinyxml2::XMLElement* edges = doc.NewElement("edges");
            size_t start = 0;
            while (start < shape.edges.size())
            {
                const EdgeRecord& first = shape.edges[start];
                size_t end = start;
                std::string text;
                bool atStart = true;
                int64_t lastX = 0, lastY = 0;
                while (end < shape.edges.size() && shape.edges[end].fill0 == first.fill0 &&
                    shape.edges[end].fill1 == first.fill1 && shape.edges[end].line == first.line)
                {
                    const EdgeRecord& e = shape.edges[end];
                    if (atStart || e.move || e.fx != lastX || e.fy != lastY)
                        text += "!" + edgeNumber(e.fx) + " " + edgeNumber(e.fy);
                    if (e.straight)
                        text += "|" + edgeNumber(e.tx) + " " + edgeNumber(e.ty);
                    else
                        text += "[" + edgeNumber(e.cx) + " " + edgeNumber(e.cy) + " " + edgeNumber(e.tx) + " " +
                            edgeNumber(e.ty);
                    lastX = e.tx;
                    lastY = e.ty;
                    atStart = false;
                    ++end;
                }
                tinyxml2::XMLElement* edge = doc.NewElement("Edge");
                if (first.fill0)
                    edge->SetAttribute("fillStyle0", first.fill0);
                if (first.fill1)
                    edge->SetAttribute("fillStyle1", first.fill1);
                if (first.line)
                    edge->SetAttribute("strokeStyle", first.line);
                edge->SetAttribute("edges", text.c_str());
                edges->InsertEndChild(edge);
                start = end;
            }
            node->InsertEndChild(edges);
        }
        return node;
    }

    tinyxml2::XMLElement* writeInstance(tinyxml2::XMLDocument& doc, const Object& o)
    {
        tinyxml2::XMLElement* node = doc.NewElement("DOMSymbolInstance");
        const auto path = _symbolPaths.find(static_cast<int>(o.symbol));
        if (path != _symbolPaths.end())
            node->SetAttribute("libraryItemName", path->second.c_str());
        if (!o.instanceName.empty() && o.cls != "CPicSymbol")
            node->SetAttribute("name", o.instanceName.c_str());
        if (o.cls == "CPicSymbol")
        {
            node->SetAttribute("symbolType", "graphic");
            if (o.firstFrame > 0)
                node->SetAttribute("firstFrame", o.firstFrame);
            node->SetAttribute("loop", o.loop == 2 ? "single frame" : o.loop == 1 ? "play once" : "loop");
        }
        else if (o.cls == "CPicButton")
        {
            node->SetAttribute("symbolType", "button");
        }
        writeMatrix(doc, node, o.matrix);
        if (o.hasPoint)
        {
            // Stored where it lands in the parent; XFL keeps it in the
            // instance's own space.
            const double det = o.matrix.a * o.matrix.d - o.matrix.b * o.matrix.c;
            if (std::fabs(det) > 1.0e-12)
            {
                const double x = (o.pointX - o.matrix.tx) / 20.0;
                const double y = (o.pointY - o.matrix.ty) / 20.0;
                const double lx = (o.matrix.d * x - o.matrix.c * y) / det;
                const double ly = (-o.matrix.b * x + o.matrix.a * y) / det;
                tinyxml2::XMLElement* holder = doc.NewElement("transformationPoint");
                tinyxml2::XMLElement* point = doc.NewElement("Point");
                if (std::fabs(lx) > 1.0e-9)
                    point->SetAttribute("x", number(std::round(lx * 1000.0) / 1000.0).c_str());
                if (std::fabs(ly) > 1.0e-9)
                    point->SetAttribute("y", number(std::round(ly * 1000.0) / 1000.0).c_str());
                holder->InsertEndChild(point);
                node->InsertEndChild(holder);
            }
        }
        writeColor(doc, node, o);
        if (!o.script.empty())
        {
            tinyxml2::XMLElement* actions = doc.NewElement("Actionscript");
            tinyxml2::XMLElement* script = doc.NewElement("script");
            tinyxml2::XMLText* text = doc.NewText(o.script.c_str());
            text->SetCData(true);
            script->InsertEndChild(text);
            actions->InsertEndChild(script);
            node->InsertEndChild(actions);
        }
        return node;
    }

    static void writeColor(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const Object& o)
    {
        const ColorEffect& e = o.effect;
        const bool identity = e.multiply[0] == 1 && e.multiply[1] == 1 && e.multiply[2] == 1 &&
            e.multiply[3] == 1 && !e.add[0] && !e.add[1] && !e.add[2] && !e.add[3];
        if (identity && o.colorMode == 0)
            return;
        tinyxml2::XMLElement* holder = doc.NewElement("color");
        tinyxml2::XMLElement* color = doc.NewElement("Color");
        switch (o.colorMode)
        {
        case 1:
            color->SetAttribute("brightness", number(o.colorPercent / 100.0).c_str());
            break;
        case 2:
            color->SetAttribute("tintMultiplier", number(o.colorPercent / 100.0).c_str());
            color->SetAttribute("tintColor", hexColor(o.tint).c_str());
            break;
        case 4:
            color->SetAttribute("alphaMultiplier", number(e.multiply[0]).c_str());
            break;
        default:
        {
            static const char* channels[4] = {"alpha", "red", "green", "blue"};
            for (int i = 0; i < 4; ++i)
            {
                if (e.multiply[i] != 1.0)
                    color->SetAttribute((std::string(channels[i]) + "Multiplier").c_str(), number(e.multiply[i]).c_str());
                if (e.add[i] != 0)
                    color->SetAttribute((std::string(channels[i]) + "Offset").c_str(), e.add[i]);
            }
            break;
        }
        }
        holder->InsertEndChild(color);
        parent->InsertEndChild(holder);
    }

    tinyxml2::XMLElement* writeText(tinyxml2::XMLDocument& doc, const Object& o)
    {
        tinyxml2::XMLElement* node = doc.NewElement("DOMStaticText");
        node->SetAttribute("fontRenderingMode", "standard");
        node->SetAttribute("left", number(o.bounds[0] / 20.0).c_str());
        node->SetAttribute("width", number((o.bounds[1] - o.bounds[0]) / 20.0).c_str());
        node->SetAttribute("height", number((o.bounds[3] - o.bounds[2]) / 20.0).c_str());
        node->SetAttribute("isSelectable", "false");
        writeMatrix(doc, node, o.matrix);
        tinyxml2::XMLElement* runs = doc.NewElement("textRuns");
        for (const TextRun& run : o.runs)
        {
            tinyxml2::XMLElement* node = doc.NewElement("DOMTextRun");
            tinyxml2::XMLElement* characters = doc.NewElement("characters");
            characters->SetText(run.text.c_str());
            node->InsertEndChild(characters);
            tinyxml2::XMLElement* holder = doc.NewElement("textAttrs");
            tinyxml2::XMLElement* attrs = doc.NewElement("DOMTextAttrs");
            static const char* alignments[4] = {"left", "right", "center", "justify"};
            if (run.alignment > 0 && run.alignment < 4)
                attrs->SetAttribute("alignment", alignments[run.alignment]);
            attrs->SetAttribute("aliasText", "false");
            attrs->SetAttribute("size", number(run.size / 20.0).c_str());
            attrs->SetAttribute("lineSpacing", number(run.lineSpacing / 20.0).c_str());
            if (run.letterSpacing)
                attrs->SetAttribute("letterSpacing", number(run.letterSpacing / 20.0).c_str());
            if (run.bold)
                attrs->SetAttribute("bold", "true");
            if (run.italic)
                attrs->SetAttribute("italic", "true");
            attrs->SetAttribute("face", run.font.c_str());
            setColor(attrs, run.color, "fillColor");
            holder->InsertEndChild(attrs);
            node->InsertEndChild(holder);
            runs->InsertEndChild(node);
        }
        node->InsertEndChild(runs);
        return node;
    }

    const Contents& _contents;
    const std::map<std::string, Object*>& _timelines;
    const std::map<std::string, std::vector<uint8_t>>& _streams;
    std::map<std::string, const Folder*> _folders;
    std::map<int, std::string> _symbolPaths;
    std::map<int, std::string> _mediaPaths;
};

} // namespace

bool convert(const std::map<std::string, std::vector<uint8_t>>& streams,
    std::map<std::string, std::vector<uint8_t>>& files, std::string& error)
{
    files.clear();
    error.clear();
    const auto contentsStream = streams.find("Contents");
    if (contentsStream == streams.end())
    {
        error = "The document has no Contents stream.";
        return false;
    }
    std::vector<std::unique_ptr<Object>> pool;
    try
    {
        Reader contentsReader(contentsStream->second, "Contents", pool);
        const Contents contents = readContents(contentsReader);

        std::map<std::string, Object*> timelines;
        for (const auto& stream : streams)
        {
            if (stream.first.compare(0, 5, "Page ") != 0 && stream.first.compare(0, 7, "Symbol ") != 0)
                continue;
            Reader reader(stream.second, stream.first, pool);
            if (reader.u8() != 1)
                reader.fail("a stream that does not start with a page");
            Object* page = reader.readObject(kTimeline);
            if (!page || page->cls != "CPicPage")
                reader.fail("a stream without a page");
            timelines[stream.first] = page;
        }

        Writer writer(contents, timelines, streams);
        writer.write(files);
    }
    catch (const ParseError& failure)
    {
        error = std::string("Phoenix could not read this Flash CS4 (or earlier) document: ") + failure.what();
        files.clear();
        return false;
    }
    return true;
}

} // namespace BinaryFla
