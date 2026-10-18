#include "binary_fla.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
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

/// Some MX 2004 documents end their strings (stream and font names) with a
/// NUL.
void trimNul(std::string& name)
{
    while (!name.empty() && name.back() == '\0')
        name.pop_back();
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
    /// Custom ease curves, one for each target (all, position, rotation,
    /// scale, color, filters): x, y pairs of in handle, point and out
    /// handle for each point.
    std::vector<std::vector<double>> eases;

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
    /// Back to where position() was, for reading a stretch again another way.
    void seek(size_t at) { _at = std::min(at, _data.size()); }

    /// The Contents version (0x129, 0x138, 0x143), for the few records whose
    /// layout follows the file rather than a schema of their own.
    uint16_t version = 0;
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
        else
        {
            // Past 0x7FFE loaded entries, the tag is 0x7FFF and a u32
            // follows: its top bit marks a class, the rest is the index.
            bool isClass = (tag & 0x8000) != 0;
            size_t index = tag & 0x7FFF;
            if (tag == 0x7FFF)
            {
                const uint32_t big = u32();
                isClass = (big & 0x80000000u) != 0;
                index = big & 0x7FFFFFFFu;
            }
            if (isClass)
            {
                if (index >= _load.size() || !_load[index].isClass)
                    fail("a bad class index");
                cls = _load[index].cls;
            }
            else
            {
                if (index >= _load.size() || _load[index].isClass || !_load[index].object)
                    fail("a bad object reference");
                // An object inside itself would make a loop of the tree.
                if (_load[index].reading)
                    fail("a reference to an object still being read");
                return _load[index].object;
            }
        }
        const auto serializer = serializers.find(cls);
        if (serializer == serializers.end())
            fail("an object of class " + cls + ", which Phoenix does not know");
        // Each level costs stack, here and in everything that walks the tree.
        if (_depth >= kMaxDepth)
            fail("objects nested too deep");
        _pool.push_back(std::make_unique<Object>());
        Object* object = _pool.back().get();
        object->cls = cls;
        const size_t loaded = _load.size();
        _load.push_back({false, cls, object, true});
        ++_depth;
        serializer->second(*this, *object);
        --_depth;
        _load[loaded].reading = false;
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
        /// Still being read: its serializer has not returned.
        bool reading = false;
    };

    static constexpr int kMaxDepth = 200;

    const std::vector<uint8_t>& _data;
    std::string _name;
    std::vector<std::unique_ptr<Object>>& _pool;
    std::vector<Loaded> _load;
    size_t _at = 0;
    int _depth = 0;
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
    if (schema >= 4)
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
        if (e.straight && shapeSchema >= 6)
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
        // Each is the cubic the edges came from (four points). In the newer
        // shapes, the quadratic points standing for it follow (each a point
        // and a u16, 1 for a control point), and a byte whose two low bits
        // each add a point (the other bits are left over from memory). Not
        // carried into XFL.
        for (int32_t i = 0; i < cubics; ++i)
        {
            r.skip(32);
            if (shapeSchema < 6)
                continue;
            const uint8_t points = r.u8();
            r.skip(static_cast<size_t>(points) * 10);
            const uint8_t more = r.u8();
            r.skip(8 * static_cast<size_t>((more & 1) + ((more >> 1) & 1)));
        }
    }
}

void readShapeBody(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.hasShape = true;
    o.matrix = r.matrix();
    readShapeData(r, o.shape, schema);
}

/// One action of a script written in normal mode: what it does and its
/// fields, in order.
struct ListedAction
{
    uint32_t code = 0;
    std::vector<uint32_t> numbers;
    std::vector<std::string> texts;
    /// Read by a guess at its fields, its code being one Phoenix does not
    /// know.
    bool unknown = false;
};

/// The fields of each action of a normal mode list: u32s and CStrings ('u'
/// and 's'). Each starts with a u32 (0); a u32 after a CString often says
/// whether it is an expression (1) or text (0). Learned from the SWFs
/// published from the same documents. [S]
const std::map<uint32_t, const char*>& listedActionFields()
{
    static const std::map<uint32_t, const char*> fields = {
        {0x00, "uussu"},      // go to: what to go by, scene, frame, play (1)
        {0x01, ""},           // play
        {0x02, ""},           // stop
        {0x04, ""},           // stop all sounds
        {0x05, "ussuuu"},     // get a URL: URL, window, method
        {0x07, "uusuusuu"},   // load a movie: URL, location, a level (1)
        {0x08, "usu"},        // tell target
        {0x09, ""},           // end of tell target
        {0x0A, "ussu"},       // if frame is loaded: scene, frame
        {0x0B, "uuu"},        // on a button event: events, key
        {0x0C, ""},           // end of the button event
        {0x0D, "uu"},         // on a clip event: events
        {0x0E, ""},           // end of the clip event
        {0x0F, "us"},         // if
        {0x10, ""},           // else
        {0x11, "us"},         // else if
        {0x12, ""},           // end of the if
        {0x13, ""},           // end of if frame is loaded
        {0x16, "usu"},        // call a frame
        {0x17, "uususu"},     // set a property: property, value, target
        {0x18, "ususu"},      // set a variable: name, value
        {0x1A, "usussssuuu"}, // start drag: target, bounds, lock, constrain
        {0x1C, "us"},         // comment
        {0x1D, ""},           // stop drag
        {0x1F, "usuu"},       // unload a movie: location, a level (1)
        {0x29, "us"},         // a statement as written
        {0x2A, ""},           // do
        {0x2B, "us"},         // its while
        {0x2C, "usss"},       // for: start, condition, step
        {0x2D, ""},           // end of the for
        {0x2E, ""},           // break
        {0x32, "usu"},        // with
        {0x33, ""},           // end of with
        {0x3E, "usss"},       // call a method: object, name, arguments
    };
    return fields;
}

void readListedFields(Reader& r, const char* layout, ListedAction& action)
{
    for (const char* field = layout; *field; ++field)
    {
        if (*field == 'u')
            action.numbers.push_back(r.u32());
        else
            action.texts.push_back(r.cstring());
    }
}

/// Whether an action read by a guess looks like one: its first field (0 or
/// 1, as every action's is) and its text (no control characters).
bool plausibleAction(const ListedAction& action)
{
    if (!action.numbers.empty() && action.numbers.front() > 1)
        return false;
    for (const std::string& text : action.texts)
        for (const char c : text)
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r')
                return false;
    return true;
}

/// Whether \a count actions Phoenix knows can be read from where \a r is.
bool listReadsOn(Reader& r, uint32_t count)
{
    const std::map<uint32_t, const char*>& fields = listedActionFields();
    try
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto found = fields.find(r.u32());
            if (found == fields.end())
                return false;
            ListedAction next;
            readListedFields(r, found->second, next);
            if (!plausibleAction(next))
                return false;
        }
    }
    catch (const ParseError&)
    {
        return false;
    }
    return true;
}

/// An action Phoenix does not know: its fields guessed, the fewest after
/// which the rest of the list reads. False when no guess does.
bool guessListedFields(Reader& r, uint32_t rest, ListedAction& action)
{
    const size_t at = r.position();
    for (size_t length = 0; length <= 8; ++length)
    {
        for (uint32_t bits = 0; bits < (1u << length); ++bits)
        {
            std::string layout;
            for (size_t i = 0; i < length; ++i)
                layout += (bits >> i) & 1 ? 's' : 'u';
            r.seek(at);
            ListedAction guess;
            guess.code = action.code;
            guess.unknown = true;
            // Fields start with a u32, as every action's do.
            if (!layout.empty() && layout[0] != 'u')
                continue;
            try
            {
                readListedFields(r, layout.c_str(), guess);
            }
            catch (const ParseError&)
            {
                continue;
            }
            if (!plausibleAction(guess))
                continue;
            const size_t after = r.position();
            if (listReadsOn(r, rest))
            {
                r.seek(after);
                action = std::move(guess);
                return true;
            }
        }
    }
    r.seek(at);
    return false;
}

/// MX and MX 2004 scripts written in normal mode are kept as a list of
/// actions rather than text: a u32 (0), a count, then each action, a u32
/// naming it and its fields. [S]
std::vector<ListedAction> readActionList(Reader& r)
{
    r.u32();
    const uint32_t count = r.u32();
    if (count > 100000)
        r.fail("a bad action count");
    const std::map<uint32_t, const char*>& fields = listedActionFields();
    std::vector<ListedAction> actions;
    for (uint32_t i = 0; i < count; ++i)
    {
        ListedAction action;
        action.code = r.u32();
        const auto found = fields.find(action.code);
        if (found != fields.end())
            readListedFields(r, found->second, action);
        else if (!guessListedFields(r, count - i - 1, action))
            r.fail("an action Phoenix does not know");
        actions.push_back(std::move(action));
    }
    return actions;
}

// ---------------------------------------------------------------------------
// Normal mode actions as ActionScript

std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out + "\"";
}

/// Slash syntax variables in an expression ("/:x", "../:x", "/a/b:x") as
/// dot syntax, which every ActionScript reads. Text in quotes is left as it
/// is, as are slashes that divide.
std::string dotSyntax(const std::string& expression)
{
    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; };
    std::string out;
    size_t i = 0;
    char quote = 0;
    while (i < expression.size())
    {
        const char c = expression[i];
        if (quote)
        {
            out += c;
            if (c == '\\' && i + 1 < expression.size())
                out += expression[++i];
            else if (c == quote)
                quote = 0;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            out += c;
            ++i;
            continue;
        }
        // A path starts where an operand may: not after a word, a closing
        // bracket or a dot.
        size_t back = out.size();
        while (back > 0 && out[back - 1] == ' ')
            --back;
        const bool operandStart = back == 0 || !(word(out[back - 1]) || out[back - 1] == ')' ||
            out[back - 1] == ']' || out[back - 1] == '.');
        if (operandStart && (c == '/' || (c == '.' && expression.compare(i, 3, "../") == 0)))
        {
            // The path: names, slashes and "..", up to a colon and a name.
            size_t end = i;
            while (end < expression.size() &&
                (word(expression[end]) || expression[end] == '/' || expression[end] == '.'))
                ++end;
            if (end < expression.size() && expression[end] == ':' && end + 1 < expression.size() &&
                word(expression[end + 1]))
            {
                const std::string path = expression.substr(i, end - i);
                std::string dotted = path[0] == '/' ? "_root" : "this";
                size_t at = path[0] == '/' ? 1 : 0;
                while (at < path.size())
                {
                    size_t slash = path.find('/', at);
                    if (slash == std::string::npos)
                        slash = path.size();
                    const std::string part = path.substr(at, slash - at);
                    if (part == "..")
                        dotted += "._parent";
                    else if (!part.empty() && part != ".")
                        dotted += "." + part;
                    at = slash + 1;
                }
                if (dotted.rfind("this.", 0) == 0)
                    dotted = dotted.substr(5);
                out += dotted + ".";
                i = end + 1;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

/// A field that is text, or an expression when \a expression is set.
std::string valueText(const std::string& text, bool expression)
{
    if (!expression)
        return quoted(text);
    return text.empty() ? std::string("\"\"") : dotSyntax(text);
}

/// A frame number written as text: a number when it is one.
std::string frameText(const std::string& frame)
{
    const bool number = !frame.empty() &&
        std::all_of(frame.begin(), frame.end(), [](char c) { return c >= '0' && c <= '9'; });
    return number ? frame : quoted(frame);
}

/// The names the player gives properties by number.
const char* propertyName(uint32_t property)
{
    static const char* const names[] = {"_x", "_y", "_xscale", "_yscale", "_currentframe", "_totalframes", "_alpha",
        "_visible", "_width", "_height", "_rotation", "_target", "_framesloaded", "_name", "_droptarget", "_url",
        "_highquality", "_focusrect", "_soundbuftime", "_quality", "_xmouse", "_ymouse"};
    return property < std::size(names) ? names[property] : nullptr;
}

/// The key a keyPress event names, as ActionScript writes it.
std::string keyText(uint32_t key)
{
    static const std::map<uint32_t, const char*> named = {{1, "<Left>"}, {2, "<Right>"}, {3, "<Home>"},
        {4, "<End>"}, {5, "<Insert>"}, {6, "<Delete>"}, {8, "<Backspace>"}, {13, "<Enter>"}, {14, "<Up>"},
        {15, "<Down>"}, {16, "<PageUp>"}, {17, "<PageDown>"}, {18, "<Tab>"}, {19, "<Escape>"}, {32, "<Space>"}};
    const auto found = named.find(key);
    if (found != named.end())
        return quoted(found->second);
    return quoted(std::string(1, static_cast<char>(key)));
}

/// A button's events: press, release, release outside, roll over, roll out,
/// drag over, drag out, then a key. [S]
std::string buttonEvents(uint32_t events, uint32_t key)
{
    static const char* const names[] = {"press", "release", "releaseOutside", "rollOver", "rollOut", "dragOver",
        "dragOut"};
    std::string out;
    for (size_t bit = 0; bit < std::size(names); ++bit)
        if (events & (1u << bit))
            out += (out.empty() ? "" : ", ") + std::string(names[bit]);
    if ((events & 0x80) || key != 0)
        out += (out.empty() ? "keyPress " : ", keyPress ") + keyText(key);
    return out.empty() ? std::string("release") : out;
}

/// A clip's events, in the order the player numbers them. [S]
std::string clipEvents(uint32_t events)
{
    static const char* const names[] = {"load", "enterFrame", "unload", "mouseMove", "mouseDown", "mouseUp",
        "keyDown", "keyUp", "data"};
    std::string out;
    for (size_t bit = 0; bit < std::size(names); ++bit)
        if (events & (1u << bit))
            out += (out.empty() ? "" : ", ") + std::string(names[bit]);
    return out.empty() ? std::string("load") : out;
}

/// A variable set to \a value: its name a reference, as typed; one in slash
/// syntax as dot syntax, or by set() when it cannot be.
std::string assignment(const std::string& name, const std::string& value)
{
    if (name.find_first_of(":/") == std::string::npos)
        return name + " = " + value + ";";
    const std::string dotted = dotSyntax(name);
    if (dotted != name && dotted.find_first_of(":/") == std::string::npos)
        return dotted + " = " + value + ";";
    return "set(" + quoted(name) + ", " + value + ");";
}

/// A statement written out as typed: given its semicolon when it has none.
std::string statementText(const std::string& text)
{
    std::string out = dotSyntax(text);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r' || out.back() == '\n'))
        out.pop_back();
    if (out.empty() || out.back() == ';' || out.back() == '}')
        return out;
    return out + ";";
}

/// A normal mode script as the ActionScript it stands for: the text the
/// script would have had in expert mode, compiled as the player compiles
/// the actions. [S]
std::string actionListScript(const std::vector<ListedAction>& actions)
{
    std::string out;
    int depth = 0;
    const auto line = [&](const std::string& text) {
        out.append(static_cast<size_t>(std::max(0, depth)), '\t');
        out += text;
        out += '\n';
    };
    const auto open = [&](const std::string& text) {
        line(text + " {");
        ++depth;
    };
    const auto close = [&]() {
        if (depth > 0)
            --depth;
        line("}");
    };
    for (const ListedAction& a : actions)
    {
        const auto number = [&a](size_t i) { return i < a.numbers.size() ? a.numbers[i] : 0u; };
        const auto text = [&a](size_t i) { return i < a.texts.size() ? a.texts[i] : std::string(); };
        if (a.unknown)
        {
            char note[96];
            std::snprintf(note, sizeof(note), "// An action Phoenix does not know (0x%02X) was here.", a.code);
            line(note);
            continue;
        }
        switch (a.code)
        {
        case 0x00:
        {
            const std::string go = number(2) ? "gotoAndPlay" : "gotoAndStop";
            const std::string scene = text(0);
            const std::string frame = text(1);
            switch (number(1))
            {
            case 3:
                line("nextFrame();");
                break;
            case 4:
                line("prevFrame();");
                break;
            case 5:
                line("nextScene();");
                break;
            case 6:
                line("prevScene();");
                break;
            default:
            {
                const std::string where = number(1) == 1 ? quoted(frame)
                    : number(1) == 2                     ? valueText(frame, true)
                                                         : frameText(frame);
                line(go + "(" + (scene.empty() ? std::string() : quoted(scene) + ", ") + where + ");");
                break;
            }
            }
            break;
        }
        case 0x01:
            line("play();");
            break;
        case 0x02:
            line("stop();");
            break;
        case 0x04:
            line("stopAllSounds();");
            break;
        case 0x05:
        {
            std::string call = "getURL(" + quoted(text(0));
            if (!text(1).empty() || number(1) != 0)
                call += ", " + quoted(text(1));
            if (number(1) == 1 || number(1) == 2)
                call += number(1) == 1 ? ", \"GET\"" : ", \"POST\"";
            line(call + ");");
            break;
        }
        case 0x07:
        {
            const std::string url = valueText(text(0), number(2) != 0);
            if (number(1) != 0)
                line("// A load action Phoenix does not know (" + std::to_string(number(1)) + ") was here.");
            else if (number(4) == 1)
                line("loadMovieNum(" + url + ", " + frameText(text(1)) + ");");
            else
                line("loadMovie(" + url + ", " + quoted(text(1)) + ");");
            break;
        }
        case 0x08:
            open("tellTarget (" + valueText(text(0), number(1) != 0) + ")");
            break;
        case 0x0A:
            open("ifFrameLoaded (" + (text(0).empty() ? std::string() : quoted(text(0)) + ", ") + frameText(text(1)) +
                ")");
            break;
        case 0x0B:
            open("on (" + buttonEvents(number(1), number(2)) + ")");
            break;
        case 0x0D:
            open("onClipEvent (" + clipEvents(number(1)) + ")");
            break;
        case 0x0F:
            open("if (" + dotSyntax(text(0)) + ")");
            break;
        case 0x10:
            close();
            open("else");
            break;
        case 0x11:
            close();
            open("else if (" + dotSyntax(text(0)) + ")");
            break;
        case 0x09:
        case 0x0C:
        case 0x0E:
        case 0x12:
        case 0x13:
        case 0x2D:
        case 0x33:
            close();
            break;
        case 0x16:
            line("call(" + valueText(text(0), number(1) != 0) + ");");
            break;
        case 0x17:
        {
            const char* property = propertyName(number(1));
            const std::string target = valueText(text(1), number(3) != 0);
            const std::string value = valueText(text(0), number(2) != 0);
            if (property)
                line(std::string("setProperty(") + target + ", " + property + ", " + value + ");");
            else
                line("// A property Phoenix does not know (" + std::to_string(number(1)) + ") was set here.");
            break;
        }
        case 0x18:
        {
            const std::string name = text(0);
            const std::string value = valueText(text(1), number(2) != 0);
            // A name given by an expression is set by set().
            line(number(1) != 0 ? "set(" + dotSyntax(name) + ", " + value + ");" : assignment(name, value));
            break;
        }
        case 0x1A:
        {
            std::string call = "startDrag(" + valueText(text(0), number(1) != 0) + (number(2) ? ", true" : ", false");
            if (number(3))
                for (size_t i = 1; i <= 4; ++i)
                    call += ", " + valueText(text(i), true);
            line(call + ");");
            break;
        }
        case 0x1C:
            line("// " + text(0));
            break;
        case 0x1D:
            line("stopDrag();");
            break;
        case 0x1F:
            line(number(1) == 1 ? "unloadMovieNum(" + frameText(text(0)) + ");"
                                : "unloadMovie(" + quoted(text(0)) + ");");
            break;
        case 0x29:
        {
            const std::string statement = statementText(text(0));
            if (!statement.empty())
                line(statement);
            break;
        }
        case 0x2A:
            open("do");
            break;
        case 0x2B:
            if (depth > 0)
                --depth;
            line("} while (" + dotSyntax(text(0)) + ");");
            break;
        case 0x2C:
            open("for (" + dotSyntax(text(0)) + "; " + dotSyntax(text(1)) + "; " + dotSyntax(text(2)) + ")");
            break;
        case 0x2E:
            line("break;");
            break;
        case 0x32:
            open("with (" + valueText(text(0), number(1) != 0) + ")");
            break;
        case 0x3E:
        {
            const std::string object = dotSyntax(text(0));
            line((object.empty() ? std::string() : object + ".") + text(1) + "(" + dotSyntax(text(2)) + ");");
            break;
        }
        default:
            break;
        }
    }
    // Blocks the list left open are closed.
    while (depth > 0)
        close();
    return out;
}

std::string readActions(Reader& r)
{
    const uint32_t version = r.u32();
    const uint32_t kind = r.u32();
    // Version 0 (MX) is the two u32s alone, kind 0 adding two more as in
    // version 1. [S]
    if (version == 0)
    {
        if (kind == 0)
            return actionListScript(readActionList(r));
        return kind == 1 ? r.cstring() : std::string();
    }
    r.u32();
    const uint32_t extra = r.u32();
    if (version >= 5)
    {
        // The fourth u32 is 0 but once (a CS4 button), where it is 1 and one
        // more u32 follows: taken as a count. [G]
        if (extra > 1000)
            r.fail("a bad action block");
        r.skip(4 * static_cast<size_t>(extra));
        r.u32();
    }
    else if (extra > 0)
    {
        // Before that the fourth u32 is 0 too, but a Flash 8 button's can be
        // 1 with one more u32 before its script, as in version 5. [G]
        if (extra > 1000)
            r.fail("a bad action block");
        r.skip(4 * static_cast<size_t>(extra));
    }
    // Version 4 (Flash 8) has the script without the u32 before it; in
    // version 1 (MX 2004) the second u32 says whether there is one (1) or
    // not (2), and 0 adds two u32s (0). [S]
    if (version >= 4 || kind == 1)
        return r.cstring();
    if (kind == 0)
        return actionListScript(readActionList(r));
    return std::string();
}

/// One Flash 5 action record.
struct Flash5Action
{
    uint8_t code = 0;
    /// The frame a go to names (0xFFFF: by an expression), or the frame
    /// if frame is loaded waits for.
    uint16_t frame = 0;
    /// The label a go to names.
    std::string label;
    /// What the action names, by where each sits: a URL and a window (0, 1),
    /// a scene (2), a value or condition (3), a second value (4), a target
    /// (5), a depth (6), a variable (7) and a location (8).
    std::string texts[9];
    /// A button's events, one a u32 each.
    uint32_t events[8] = {};
    /// The property set (-1: none).
    int32_t property = -1;
    /// Bytes after the location: which fields are expressions, and more.
    uint8_t flags[61] = {};
};

/// Flash 5 kept a frame's or button's actions as records of its own rather
/// than script text: a count, then each action: a byte (9), the action's
/// code, three CStrings, a u16 frame, a u16 (0xC180), the label (a
/// CString), three CStrings, eight u32s (a button's events), the property
/// (s32), a u16, two CStrings, five bytes, a CString and 61 bytes of flags.
/// [S]
std::vector<Flash5Action> readFlash5Actions(Reader& r)
{
    const uint32_t count = r.u32();
    if (count > 100000)
        r.fail("a bad action count");
    std::vector<Flash5Action> actions;
    for (uint32_t i = 0; i < count; ++i)
    {
        Flash5Action a;
        r.u8();
        a.code = r.u8();
        for (int k = 0; k < 3; ++k)
            a.texts[k] = r.cstring();
        a.frame = r.u16();
        r.u16();
        a.label = r.cstring();
        for (int k = 3; k < 6; ++k)
            a.texts[k] = r.cstring();
        for (uint32_t& event : a.events)
            event = r.u32();
        a.property = r.s32();
        r.u16();
        a.texts[6] = r.cstring();
        a.texts[7] = r.cstring();
        r.skip(5);
        a.texts[8] = r.cstring();
        for (uint8_t& flag : a.flags)
            flag = r.u8();
        actions.push_back(std::move(a));
    }
    return actions;
}

/// An expression in the syntax of Flash 4 (which Flash 5 kept for the
/// actions it read from Flash 4 movies) as ActionScript: = and <> compare,
/// & joins text, and functions may be capitalized.
std::string flash4Expression(const std::string& expression)
{
    static const std::map<std::string, const char*> functions = {{"getproperty", "getProperty"},
        {"random", "random"}, {"int", "int"}, {"gettimer", "getTimer"}, {"eval", "eval"}, {"true", "true"},
        {"false", "false"}, {"targetpath", "targetPath"}};
    std::string out;
    char quote = 0;
    for (size_t i = 0; i < expression.size(); ++i)
    {
        const char c = expression[i];
        if (quote)
        {
            out += c;
            if (c == '\\' && i + 1 < expression.size())
                out += expression[++i];
            else if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            out += c;
            continue;
        }
        const char before = out.empty() ? ' ' : out.back();
        const char after = i + 1 < expression.size() ? expression[i + 1] : ' ';
        if (c == '<' && after == '>')
        {
            out += "!=";
            ++i;
        }
        else if (c == '=' && before != '<' && before != '>' && before != '!' && before != '=' && after != '=')
            out += "==";
        else if (c == '&' && after != '&' && before != '&')
            out += std::string(before == ' ' ? "" : " ") + "add" + (after == ' ' ? "" : " ");
        else if (std::isalpha(static_cast<unsigned char>(c)) &&
            !(std::isalnum(static_cast<unsigned char>(before)) || before == '_' || before == '.' || before == '$'))
        {
            size_t end = i;
            while (end < expression.size() &&
                (std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_'))
                ++end;
            std::string word = expression.substr(i, end - i);
            std::string lower = word;
            for (char& l : lower)
                l = static_cast<char>(std::tolower(static_cast<unsigned char>(l)));
            const auto found = functions.find(lower);
            out += found != functions.end() ? found->second : word;
            i = end - 1;
        }
        else
            out += c;
    }
    return dotSyntax(out);
}

/// A Flash 5 action's field: text, or an expression when \a expression.
std::string flash5Value(const std::string& text, bool expression)
{
    if (!expression)
        return quoted(text);
    return text.empty() ? std::string("\"\"") : flash4Expression(text);
}

/// Flash 5's action records as the ActionScript they stand for. Codes and
/// fields were learned from the SWFs published from the same documents. [S]
std::string flash5Script(const std::vector<Flash5Action>& actions)
{
    std::string out;
    int depth = 0;
    const auto line = [&](const std::string& text) {
        out.append(static_cast<size_t>(std::max(0, depth)), '\t');
        out += text;
        out += '\n';
    };
    const auto open = [&](const std::string& text) {
        line(text + " {");
        ++depth;
    };
    const auto close = [&]() {
        if (depth > 0)
            --depth;
        line("}");
    };
    for (const Flash5Action& a : actions)
    {
        // Which fields are expressions, by the flag bytes.
        const bool valueExpression = a.flags[9] != 0;
        const bool targetExpression = a.flags[29] != 0;
        const bool depthExpression = a.flags[33] != 0;
        switch (a.code)
        {
        case 0x01:
            line("play();");
            break;
        case 0x02:
            line("stop();");
            break;
        case 0x03:
            line("nextFrame();");
            break;
        case 0x04:
            line("prevFrame();");
            break;
        case 0x08:
        case 0x0A:
        {
            // In a scene (2) when one is named; by a label, an expression
            // (frame 0xFFFF) or the frame's number.
            const std::string go = a.code == 0x0A ? "gotoAndPlay(" : "gotoAndStop(";
            const std::string scene = a.texts[2].empty() ? std::string() : quoted(a.texts[2]) + ", ";
            const std::string frame = !a.label.empty() ? quoted(a.label)
                : a.frame == 0xFFFF                    ? flash5Value(a.texts[3], true)
                                                       : std::to_string(a.frame);
            line(go + scene + frame + ");");
            break;
        }
        case 0x09:
            line("getURL(" + quoted(a.texts[0]) + (a.texts[1].empty() ? std::string() : ", " + quoted(a.texts[1])) +
                ");");
            break;
        case 0x0E:
            open("tellTarget (" + flash5Value(a.texts[5], targetExpression) + ")");
            break;
        case 0x0F:
        case 0x13:
        case 0x15:
        case 0x18:
            close();
            break;
        case 0x10:
            line("fscommand(" + quoted(a.texts[3]) + ", " + quoted(a.texts[4]) + ");");
            break;
        case 0x11:
        {
            const std::string& where = a.texts[8];
            const bool level = !where.empty() &&
                std::all_of(where.begin(), where.end(), [](char c) { return c >= '0' && c <= '9'; });
            line(level ? "loadMovieNum(" + quoted(a.texts[0]) + ", " + where + ");"
                       : "loadMovie(" + quoted(a.texts[0]) + ", " + quoted(where) + ");");
            break;
        }
        case 0x12:
            open("ifFrameLoaded (" + std::to_string(a.frame) + ")");
            break;
        case 0x14:
        {
            // Release, press, release outside, roll over, roll out, drag
            // over, drag out: the second u32 on. [S]
            static const char* const names[] = {nullptr, "release", "press", "releaseOutside", "rollOver", "rollOut",
                "dragOver", "dragOut"};
            std::string events;
            for (size_t k = 1; k < std::size(names); ++k)
                if (a.events[k])
                    events += (events.empty() ? "" : ", ") + std::string(names[k]);
            open("on (" + (events.empty() ? std::string("release") : events) + ")");
            break;
        }
        case 0x16:
            open("if (" + flash4Expression(a.texts[3]) + ")");
            break;
        case 0x17:
            close();
            open("else");
            break;
        case 0x1D:
            line(assignment(a.texts[7], flash5Value(a.texts[3], valueExpression)));
            break;
        case 0x1E:
        {
            const char* property = a.property >= 0 ? propertyName(static_cast<uint32_t>(a.property)) : nullptr;
            if (property)
                line(std::string("setProperty(") + flash5Value(a.texts[5], targetExpression) + ", " + property + ", " +
                    flash5Value(a.texts[3], valueExpression) + ");");
            else
                line("// A property Phoenix does not know (" + std::to_string(a.property) + ") was set here.");
            break;
        }
        case 0x22:
            line("startDrag(" + flash5Value(a.texts[5], targetExpression) + (a.flags[45] ? ", true" : ", false") +
                ");");
            break;
        case 0x23:
            line("duplicateMovieClip(" + flash5Value(a.texts[5], targetExpression) + ", " +
                flash5Value(a.texts[3], valueExpression) + ", " + flash5Value(a.texts[6], depthExpression) + ");");
            break;
        default:
        {
            char note[96];
            std::snprintf(note, sizeof(note), "// An action Phoenix does not know (0x%02X) was here.", a.code);
            line(note);
            break;
        }
        }
    }
    while (depth > 0)
        close();
    return out;
}

void readShape(Reader& r, Object& o)
{
    readPicObj(r, o);
    readShapeBody(r, o);
}

/// A rectangle primitive (CS3 and later): a shape holding the rectangle as
/// drawn, then a schema byte (3), a byte (1: the corners locked together?)
/// and the four corner radii (s32 twips). Written as the drawing object it
/// draws as. [S]
void readRectangle(Reader& r, Object& o)
{
    readShape(r, o);
    r.u8();
    r.u8();
    r.skip(16);
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
    if (schema >= 21)
        o.script = readActions(r);
    else
        o.script = flash5Script(readFlash5Actions(r));
    r.skip(12);
    o.morph = r.readObject(kTimeline) != nullptr;
    r.u32();
    r.readObject(kTimeline);
    r.cstring();
    // Schema 21 (MX) has two u32s here, 18 (Flash 5) none. [S]
    if (schema >= 22)
        r.skip(12);
    else if (schema >= 21)
        r.skip(8);
    if (schema >= 24)
    {
        r.u32();
        // A custom ease: a curve for each of six targets.
        if (r.u32() != 0)
        {
            for (int target = 0; target < 6; ++target)
            {
                const uint32_t points = r.u32();
                if (points > 10000)
                    r.fail("a bad ease curve");
                std::vector<double> curve;
                for (uint32_t i = 0; i < points * 6; ++i)
                {
                    const uint32_t low = r.u32();
                    const uint64_t bits = static_cast<uint64_t>(r.u32()) << 32 | low;
                    double value;
                    std::memcpy(&value, &bits, sizeof(value));
                    curve.push_back(value);
                }
                o.eases.push_back(std::move(curve));
            }
        }
    }
}

void readLayer(Reader& r, Object& o)
{
    readPicObj(r, o);
    const uint8_t schema = r.u8();
    o.name = r.cstring();
    o.current = r.u8() != 0;
    o.hidden = r.u8() != 0;
    o.locked = r.u8() != 0;
    r.u32();
    o.color = r.color();
    r.u32(); // outline mode
    r.u32(); // height
    o.layerType = r.u8();
    if (schema < 10)
    {
        // Flash 5: two bytes, then the layer it is linked to (serialized
        // inline when it comes later in the list, as a folder is), and a
        // byte. [S]
        r.skip(2);
        o.parent = r.readObject(kTimeline);
        r.u8();
        return;
    }
    o.parent = r.readObject(kTimeline);
    r.u8();
    o.autoNamed = r.u8() != 0;
    if (schema >= 13)
        r.u8(); // CS4 [S]
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
    // Schema 2 (Flash 5) has no guide list. [S]
    if (schema >= 3)
    {
        const uint32_t guides = r.u32();
        r.skip(static_cast<size_t>(guides) * 8);
    }
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
    if (schema >= 22)
    {
        // CS4: the 3D transform, a 4 x 4 matrix of floats, then 24 bytes,
        // an unset point (INT_MIN, INT_MIN) and 6 bytes. [S]
        r.skip(7 + 64 + 24 + 8 + 6);
    }
    else if (schema >= 19)
        r.skip(7);
    else if (schema >= 14)
        r.skip(3); // Flash 8
    else if (schema == 8)
        r.skip(11); // MX [S]
}

/// A bitmap on the stage: the CPicObj base, a schema byte (1 or 2), its
/// matrix (bitmap pixels to the parent, translation in twips), the number N
/// of its "Media N" and, from schema 2, a byte (0).
void readBitmapInstance(Reader& r, Object& o)
{
    readPicObj(r, o);
    const uint8_t schema = r.u8();
    o.matrix = r.matrix();
    o.number = r.u16();
    if (schema >= 2)
        r.u8();
}

/// A video on the stage: the CPicObj base, a schema byte, its matrix, its
/// frame (4 x s32 twips: xmin, xmax, ymin, ymax), a byte, two CStrings, a
/// u32 (1) and the number N of its "Media N". [S]
void readVideoInstance(Reader& r, Object& o)
{
    readPicObj(r, o);
    r.u8();
    o.matrix = r.matrix();
    for (int32_t& bound : o.bounds)
        bound = r.s32();
    r.u8();
    r.cstring();
    r.cstring();
    r.u32();
    o.number = r.u16();
}

/// An MX video on the stage: the CPicObj base, a schema byte (0), its
/// matrix and the number N of its "Media N". [S]
void readVideoInstanceMx(Reader& r, Object& o)
{
    readPicObj(r, o);
    r.u8();
    o.matrix = r.matrix();
    o.number = r.u16();
}

/// An MFC CObList: a count, then that many objects.
void readObList(Reader& r, Object& o)
{
    uint32_t count = r.u16();
    if (count == 0xFFFF)
        count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        if (Object* item = r.readObject(kTimeline))
            o.children.push_back(item);
    }
}

/// A shape hint of a shape tween: where it sits on the first keyframe and
/// on the last (s32 x, y twips each), and a u32. Not carried into XFL.
void readMorphHint(Reader& r, Object&)
{
    r.skip(20);
}

/// A shape tween's precomputed in-between shape. Phoenix tweens from the
/// keyframes itself, so this is read past rather than kept: the frame only
/// notes that it is shape tweened.
void readMorphShape(Reader& r, Object&)
{
    r.u8(); // schema (2)
    r.matrix();
    r.matrix();
    r.skip(8);
    const uint16_t segments = r.u16();
    for (uint16_t i = 0; i < segments; ++i)
        r.readObject(kTimeline);
    r.u16();
    const uint16_t fills = r.u16();
    for (uint16_t i = 0; i < fills; ++i)
    {
        r.color();
        if (r.u16() != 0)
            r.fail("a shape tween with a gradient or bitmap fill, which Phoenix cannot read yet");
    }
    const uint16_t lines = r.u16();
    r.skip(static_cast<size_t>(lines) * 10); // color, u16 width, 4 bytes
}

/// One run of a shape tween's outline: 16 bytes, its start on the first
/// keyframe and the last (s32 x, y twips each), then its curves.
void readMorphSegment(Reader& r, Object&)
{
    r.skip(16 + 16);
    const uint16_t curves = r.u16();
    for (uint16_t i = 0; i < curves; ++i)
        r.readObject(kTimeline);
}

/// One curve of a segment: control and anchor points on the first keyframe
/// and the last (s32 x, y twips each), and a u32 (1 for a straight line).
void readMorphCurve(Reader& r, Object&)
{
    r.skip(36);
}

void readSprite(Reader& r, Object& o)
{
    readInstance(r, o);
    const uint8_t schema = r.u8();
    // Schema 1 (Flash 5) is the instance name alone. [S]
    if (schema >= 2)
        o.script = readActions(r);
    const std::string name = r.cstring();
    if (!name.empty())
        o.instanceName = name;
    if (schema < 2)
        return;
    r.u32();
    r.u8();
    if (schema < 4)
        return; // MX [S]
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
    const uint8_t schema = r.u8();
    if (schema < 5)
    {
        // Flash 5: the actions and a byte. [S]
        o.script = flash5Script(readFlash5Actions(r));
        r.u8();
        return;
    }
    o.script = readActions(r);
    r.u8();
    if (schema < 6)
        return; // MX [S]
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
    const uint32_t flags = r.u32();
    if (schema >= 5)
        r.u16(); // not in Flash 5's schema 4 [S]
    r.cstring(); // instance name
    // Another CString when 0x2000 is set in either half of the flags (seen
    // twice, a CS3 and a CS4 text; never otherwise). [S]
    if (schema >= 5 && ((flags | flags >> 16) & 0x2000))
        r.cstring();
    if (schema >= 14)
        r.u8(); // CS4 [S]
    for (;;)
    {
        const uint16_t count = r.u16();
        if (count == 0)
            break;
        TextRun run;
        const uint8_t runSchema = r.u8();
        run.size = r.u16();
        const bool wide = runSchema >= 10;
        std::vector<uint8_t> a;
        if (runSchema == 15)
        {
            // CS4: the family and PostScript names, a u32 and the color,
            // then the attributes of schema 12 with its URL, target and
            // last field as CStrings rather than bytes. [S]
            run.font = r.cstring();
            const std::string face = r.cstring();
            if (!face.empty())
                run.font = face;
            r.u32();
            run.color = r.color();
            a.resize(18);
            for (uint8_t& byte : a)
                byte = r.u8();
            r.cstring();
            r.u32();
            r.cstring();
            r.skip(10);
            r.cstring();
        }
        else
        {
            run.font = wide ? r.wide8() : r.ansi8();
            trimNul(run.font);
            run.color = r.color();
            // Schema 7 holds the link's URL and target as strings: 23 bytes
            // with both empty, more where they end in a NUL. [S]
            const size_t attrs = runSchema == 4 || runSchema == 7 ? 18
                : runSchema == 5 ? 19 : runSchema == 10 ? 24 : runSchema == 12 ? 35 : 0;
            if (attrs == 0)
                r.fail("a text run of a schema Phoenix does not know");
            a.resize(attrs);
            for (uint8_t& byte : a)
                byte = r.u8();
            if (runSchema == 7)
            {
                r.ansi8();
                r.skip(3);
                r.ansi8();
            }
        }
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
    // Flash 5's schemas 4 and 5 end with the runs. [S]
    if (schema < 6)
        return;
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
    {"CObList", readObList},
    {"CMorphHintItem", readMorphHint},
    {"CPicBitmap", readBitmapInstance},
    {"CPicVideoStream", readVideoInstance},
    {"CPicVideo", readVideoInstanceMx},
    {"CPicMorphShape", readMorphShape},
    {"CMorphSegment", readMorphSegment},
    {"CMorphCurve", readMorphCurve},
    {"CPicPage", readPage},
    {"CPicLayer", readLayer},
    {"CPicFrame", readFrame},
    {"CPicShape", readShape},
    {"CPicRect", readRectangle},
    {"CPicSymbol", readInstance},
    {"CPicSprite", readSprite},
    {"CPicButton", readButton},
    {"CPicText", readText},
};

// ---------------------------------------------------------------------------
// The Contents stream

void readLinkage(Reader& r)
{
    // The low byte is the version; 0x100 is set on an exported symbol. [S]
    const uint32_t version = r.u32() & 0xFF;
    r.u8();
    r.cstring();
    r.cstring();
    // Version 3 (MX 2004) has two CStrings and the byte. [S]
    if (version >= 5)
        r.cstring();
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
    // Version 4 (Flash 5) ends here. [S]
    if (version < 5)
        return item;
    if (r.version < 0x129)
    {
        // MX and Flash 5: a u32, a byte and two CStrings (all empty in the
        // documents seen) in place of the linkage and source. [S]
        r.u32();
        r.u8();
        r.cstring();
        r.cstring();
        return item;
    }
    readLinkage(r);
    readSource(r);
    if (version >= 6)
    {
        // Flash 8 (Contents 0x138) has the byte alone; 0x13f adds the rest.
        r.u8();
        if (r.version >= 0x13f)
        {
            r.cstring();
            r.u8();
        }
    }
    return item;
}

void readDocumentPage(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.stream = schema >= 22 ? r.wide8() : r.ansi8();
    trimNul(o.stream);
    o.name = r.cstring();
    o.number = static_cast<int>(r.u32());
    o.symbolType = r.u8();
    r.cstring();
    r.u32();
    o.item = readItem(r);
    o.lastModified = r.u32();
    if (schema < 12)
        return; // Flash 5 [S]
    r.cstring();
    r.cstring();
    r.u32();
    r.u8();
    r.u32();
    r.u32();
    if (schema < 17)
    {
        r.skip(3); // MX [S]
        return;
    }
    readLinkage(r);
    readSource(r);
    if (schema >= 22)
    {
        // Schema 22 (Flash 8) is schema 24 (CS3) without a few fields, and
        // 23 (Contents 0x13f) is between them. [S]
        if (schema >= 23)
        {
            r.u8();
            r.cstring();
        }
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
        if (schema >= 24)
        {
            r.skip(16);
            r.cstring();
        }
        r.skip(schema == 23 ? 12 : 8);
        r.cstring();
        r.cstring();
        r.u32();
        r.cstring();
        if (schema >= 23)
        {
            r.u32();
            r.skip(16); // scale-9 rectangle
            if (schema >= 24)
                r.skip(schema >= 25 ? 11 : 9); // CS4 writes 25 [S]
        }
    }
    else
    {
        r.skip(34);
    }
}

/// The fields every media record starts with; returns its schema.
uint8_t readMediaCommon(Reader& r, Object& o)
{
    const uint8_t schema = r.u8();
    o.stream = schema >= 6 ? r.wide8() : r.ansi8();
    trimNul(o.stream);
    o.name = r.cstring();
    o.number = r.u16();
    o.importPath = r.cstring();
    o.lastModified = r.u32();
    o.item = readItem(r);
    return schema;
}

void readMediaBits(Reader& r, Object& o)
{
    const uint8_t schema = readMediaCommon(r, o);
    if (schema >= 5)
        r.u32(); // not in Flash 5 [S]
    r.u16();
    o.quality = r.u8();
    o.allowSmoothing = r.u8() != 0;
    r.u32();
    if (schema >= 7)
        r.u8(); // CS4 [S]
}

void readMediaSound(Reader& r, Object& o)
{
    const uint8_t schema = readMediaCommon(r, o);
    if (schema >= 5)
        r.u32(); // not in Flash 5, which ends after the u32 below [S]
    r.u8();
    o.format = r.u16();
    o.sampleCount = r.u32();
    r.u32();
    if (schema < 5)
        return;
    r.skip(11);
    if (schema >= 6)
        r.cstring();
    // Contents 0x13f and later add a u32, whatever the record's schema. [S]
    if (r.version >= 0x13f)
        r.u32();
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

/// An embedded video (CS4): the fields every media record has, then a u32
/// (1). [S] one file.
void readMediaVideo(Reader& r, Object& o)
{
    readMediaCommon(r, o);
    r.u32();
}

/// An MX video (a QuickTime or embedded one): the same, and a byte. [S]
void readMediaVideoMx(Reader& r, Object& o)
{
    readMediaVideo(r, o);
    r.u8();
}

const std::map<std::string, Reader::Serializer> kContents = {
    {"CDocumentPage", readDocumentPage},
    {"CMediaBits", readMediaBits},
    {"CMediaSound", readMediaSound},
    {"CMediaVideoStream", readMediaVideo},
    {"CMediaVideo", readMediaVideoMx},
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

/// Reads into \a c as it goes, so that what came before a failure is kept.
void readContents(Reader& r, Contents& c)
{
    // The low byte is the version; documents are found with the high byte
    // 0 or 1 and otherwise alike.
    uint16_t version = r.u16();
    if (version < 0x100)
        version |= 0x100;
    // Each version adds to the header before the scenes.
    static const std::map<uint16_t, size_t> header = {{0x11b, 5}, {0x120, 9}, {0x129, 13}, {0x138, 17},
        {0x13f, 21}, {0x143, 25}, {0x147, 29}};
    const auto known = header.find(version);
    if (known == header.end())
        r.fail("a Contents version Phoenix does not know");
    r.version = version;
    r.skip(known->second);
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
    r.skip(8);
    // The page setup: the printer's settings and its names (Windows' DEVMODE
    // and DEVNAMES), each a u16 size and that many bytes; both sizes are 0
    // unless the document was saved with a printer chosen. [S] 2 files.
    for (int block = 0; block < 2; ++block)
        r.skip(r.u16());
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
    if (version >= 0x138)
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
        // CS4 ends each list with two bytes. [S]
        r.skip(version >= 0x147 ? 2 : 1);
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
        if (m.cls == "CMediaVideoStream" || m.cls == "CMediaVideo")
        {
            // An embedded video: its stream is in a layout Phoenix does not
            // know, so the item keeps only where the video came from.
            tinyxml2::XMLElement* item = doc.NewElement("DOMVideoItem");
            item->SetAttribute("name", path.c_str());
            if (!m.item.itemId.empty())
                item->SetAttribute("itemID", m.item.itemId.c_str());
            item->SetAttribute("sourceLastImported", m.lastModified);
            if (!m.importPath.empty())
                item->SetAttribute("sourceExternalFilepath", m.importPath.c_str());
            parent->InsertEndChild(item);
            return;
        }
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
                        // Held where the XFL reader clamps frames (fla::kMaxFrames).
                        index = std::min(index + std::max(1, frame->duration), 1 << 24);
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
        spend();
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
        if ((motion || frame.morph) && !frame.eases.empty())
        {
            // XFL lists a curve's points without the first point's in
            // handle and the last one's out handle.
            static const char* const targets[6] = {"all", "position", "rotation", "scale", "color", "filters"};
            tinyxml2::XMLElement* tweens = doc.NewElement("tweens");
            for (size_t t = 0; t < frame.eases.size() && t < 6; ++t)
            {
                const std::vector<double>& curve = frame.eases[t];
                if (curve.size() < 12)
                    continue;
                tinyxml2::XMLElement* ease = doc.NewElement("CustomEase");
                ease->SetAttribute("target", targets[t]);
                for (size_t i = 2; i + 2 < curve.size(); i += 2)
                {
                    tinyxml2::XMLElement* point = doc.NewElement("Point");
                    point->SetAttribute("x", number(curve[i]).c_str());
                    point->SetAttribute("y", number(curve[i + 1]).c_str());
                    ease->InsertEndChild(point);
                }
                tweens->InsertEndChild(ease);
            }
            node->SetAttribute("hasCustomEase", "true");
            node->InsertEndChild(tweens);
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
        spend();
        if (element->cls == "CPicShape")
        {
            if (_nesting >= kMaxNesting)
                throw ParseError("groups nested too deep");
            ++_nesting;
            if (element->objFlags & 1)
            {
                parent->InsertEndChild(writeShape(doc, element->matrix, element->shape));
                for (const Object* child : element->children)
                    writeElement(doc, parent, child);
                --_nesting;
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
            --_nesting;
        }
        else if (element->cls == "CPicRect")
        {
            tinyxml2::XMLElement* shape = writeShape(doc, element->matrix, element->shape);
            shape->SetAttribute("isDrawingObject", "true");
            parent->InsertEndChild(shape);
        }
        else if (element->cls == "CPicSymbol" || element->cls == "CPicSprite" || element->cls == "CPicButton")
        {
            parent->InsertEndChild(writeInstance(doc, *element));
        }
        else if (element->cls == "CPicText")
        {
            parent->InsertEndChild(writeText(doc, *element));
        }
        else if (element->cls == "CPicVideoStream" || element->cls == "CPicVideo")
        {
            tinyxml2::XMLElement* node = doc.NewElement("DOMVideoInstance");
            const auto media = _mediaPaths.find(element->number);
            if (media != _mediaPaths.end())
                node->SetAttribute("libraryItemName", media->second.c_str());
            node->SetAttribute("frameRight", element->bounds[1] - element->bounds[0]);
            node->SetAttribute("frameBottom", element->bounds[3] - element->bounds[2]);
            writeMatrix(doc, node, element->matrix);
            parent->InsertEndChild(node);
        }
        else if (element->cls == "CPicBitmap")
        {
            tinyxml2::XMLElement* node = doc.NewElement("DOMBitmapInstance");
            const auto media = _mediaPaths.find(element->number);
            if (media != _mediaPaths.end())
                node->SetAttribute("libraryItemName", media->second.c_str());
            writeMatrix(doc, node, element->matrix);
            parent->InsertEndChild(node);
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
    // An object read once can be referred to again and again, and from
    // within others it holds: how many frames and elements are written, and
    // how deep the groups are.
    static constexpr size_t kMaxWritten = 4000000;
    static constexpr int kMaxNesting = 200;
    size_t _written = 0;
    int _nesting = 0;

    void spend()
    {
        if (++_written > kMaxWritten)
            throw ParseError("objects repeated past reason");
    }
};

} // namespace

bool convert(const std::map<std::string, std::vector<uint8_t>>& streams,
    std::map<std::string, std::vector<uint8_t>>& files, std::string& error, std::string* warning)
{
    files.clear();
    error.clear();
    const auto contentsStream = streams.find("Contents");
    if (contentsStream == streams.end())
    {
        error = "The document has no Contents stream.";
        return false;
    }
    // MFC objects carry no length, so a class Phoenix does not know (or
    // damage) ends the stream it is in: nothing after it can be found. What
    // was read before it is kept, and the rest of the document still read.
    std::string notes;
    std::vector<std::unique_ptr<Object>> pool;
    try
    {
        Contents contents;
        {
            Reader contentsReader(contentsStream->second, "Contents", pool);
            try
            {
                readContents(contentsReader, contents);
            }
            catch (const ParseError& failure)
            {
                // Without the version nothing in it can be trusted.
                if (contentsReader.version == 0)
                    throw;
                notes += std::string(failure.what()) + "\n";
            }
        }

        // The timeline streams: "Page N" and "Symbol N", or in CS4 "P N <time>"
        // and "S N <time>", as the scenes and symbols name them.
        std::set<std::string> named;
        for (const std::vector<Object*>* list : {&contents.scenes, &contents.symbols})
        {
            for (const Object* page : *list)
                named.insert(page->stream);
        }
        std::map<std::string, Object*> timelines;
        for (const auto& stream : streams)
        {
            if (!named.count(stream.first) && stream.first.compare(0, 5, "Page ") != 0 &&
                stream.first.compare(0, 7, "Symbol ") != 0)
                continue;
            const size_t first = pool.size();
            Reader reader(stream.second, stream.first, pool);
            try
            {
                if (reader.u8() != 1)
                    reader.fail("a stream that does not start with a page");
                Object* page = reader.readObject(kTimeline);
                if (!page || page->cls != "CPicPage")
                    reader.fail("a stream without a page");
                timelines[stream.first] = page;
            }
            catch (const ParseError& failure)
            {
                notes += std::string(failure.what()) + "\n";
                // The page as far as it was read: its layers before the
                // failure whole, the one it was in up to it.
                if (pool.size() > first && pool[first]->cls == "CPicPage")
                    timelines[stream.first] = pool[first].get();
            }
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
    if (warning && !notes.empty())
        *warning = "Parts of this Flash CS4 (or earlier) document could not be read and are missing:\n" + notes;
    return true;
}

} // namespace BinaryFla
