#include "swf_import.h"

#include "../data/bitmap_decoder.h"
#include "../edit/transform_blend.h"
#include "../edit/tween_ease.h"
#include "../play/display/bitmap_filter.h"
#include "../util/lzma.h"
#include "../util/swf_sound.h"

#include "tinyxml2.h"
#include "zlib.h"

#include <algorithm>
#include <map>
#include <optional>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>

namespace SwfImport {

namespace {

ImageDecoder& imageDecoder()
{
    static ImageDecoder decoder;
    return decoder;
}

// ---------------------------------------------------------------------------
// Reading

class Reader
{
public:
    Reader(const uint8_t* data, size_t size)
        : _data(data)
        , _size(size)
    {}

    bool atEnd() const { return _at >= _size; }
    const uint8_t* data() const { return _data; }
    size_t position() const { return _at; }
    size_t left() const { return _at < _size ? _size - _at : 0; }
    void seek(size_t at)
    {
        _at = std::min(at, _size);
        _bits = 0;
    }
    void skip(size_t count) { seek(_at + count); }
    void align() { _bits = 0; }

    uint8_t u8()
    {
        align();
        return _at < _size ? _data[_at++] : 0;
    }
    uint16_t u16()
    {
        const uint16_t low = u8();
        return static_cast<uint16_t>(low | (u8() << 8));
    }
    int16_t s16() { return static_cast<int16_t>(u16()); }
    uint32_t u32()
    {
        const uint32_t low = u16();
        return low | (static_cast<uint32_t>(u16()) << 16);
    }
    int32_t s32() { return static_cast<int32_t>(u32()); }
    double fixed16() { return s32() / 65536.0; }
    double fixed8() { return s16() / 256.0; }
    float f32()
    {
        const uint32_t bits = u32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    std::vector<uint8_t> bytes(size_t count)
    {
        align();
        count = std::min(count, left());
        std::vector<uint8_t> out(_data + _at, _data + _at + count);
        _at += count;
        return out;
    }
    /// EncodedU32: seven bits a byte, low first, while the top bit is set.
    uint32_t encodedU32()
    {
        uint32_t value = 0;
        for (int shift = 0; shift < 35; shift += 7)
        {
            const uint8_t byte = u8();
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80))
                break;
        }
        return value;
    }
    std::string string()
    {
        align();
        std::string out;
        while (_at < _size && _data[_at] != 0)
            out += static_cast<char>(_data[_at++]);
        if (_at < _size)
            ++_at;
        return out;
    }

    uint32_t ub(int count)
    {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i)
        {
            if (_bits == 0)
            {
                _byte = _at < _size ? _data[_at] : 0;
                ++_at;
                _bits = 8;
            }
            --_bits;
            value = (value << 1) | ((_byte >> _bits) & 1u);
        }
        return value;
    }
    int32_t sb(int count)
    {
        if (count <= 0)
            return 0;
        const uint32_t value = ub(count);
        // Past 32 bits only the low 32 are kept.
        const int bits = std::min(count, 32);
        if (value & (1u << (bits - 1)))
            return static_cast<int32_t>(value | ~((bits >= 32) ? 0u : ((1u << bits) - 1u)));
        return static_cast<int32_t>(value);
    }
    double fb(int count) { return sb(count) / 65536.0; }

private:
    const uint8_t* _data;
    size_t _size;
    size_t _at = 0;
    uint8_t _byte = 0;
    int _bits = 0;
};

struct Rect
{
    double left = 0, right = 0, top = 0, bottom = 0; // twips
};

Rect readRect(Reader& in)
{
    in.align();
    const int bits = static_cast<int>(in.ub(5));
    Rect rect;
    rect.left = in.sb(bits);
    rect.right = in.sb(bits);
    rect.top = in.sb(bits);
    rect.bottom = in.sb(bits);
    in.align();
    return rect;
}

/// x' = a x + c y + tx, in twips.
struct Matrix
{
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    bool identity() const { return a == 1 && b == 0 && c == 0 && d == 1 && tx == 0 && ty == 0; }
    void apply(double x, double y, double& ox, double& oy) const
    {
        ox = a * x + c * y + tx;
        oy = b * x + d * y + ty;
    }
    /// This after \a inner: points go through \a inner first.
    Matrix times(const Matrix& inner) const
    {
        Matrix m;
        m.a = a * inner.a + c * inner.b;
        m.b = b * inner.a + d * inner.b;
        m.c = a * inner.c + c * inner.d;
        m.d = b * inner.c + d * inner.d;
        m.tx = a * inner.tx + c * inner.ty + tx;
        m.ty = b * inner.tx + d * inner.ty + ty;
        return m;
    }
    bool operator==(const Matrix& o) const
    {
        return a == o.a && b == o.b && c == o.c && d == o.d && tx == o.tx && ty == o.ty;
    }
};

Matrix readMatrix(Reader& in)
{
    in.align();
    Matrix m;
    if (in.ub(1))
    {
        const int bits = static_cast<int>(in.ub(5));
        m.a = in.fb(bits);
        m.d = in.fb(bits);
    }
    if (in.ub(1))
    {
        const int bits = static_cast<int>(in.ub(5));
        m.b = in.fb(bits);
        m.c = in.fb(bits);
    }
    const int bits = static_cast<int>(in.ub(5));
    m.tx = in.sb(bits);
    m.ty = in.sb(bits);
    in.align();
    return m;
}

struct ColorTransform
{
    double mul[4] = {1, 1, 1, 1}; // red, green, blue, alpha
    int add[4] = {0, 0, 0, 0};

    bool identity() const
    {
        for (int i = 0; i < 4; ++i)
            if (mul[i] != 1.0 || add[i] != 0)
                return false;
        return true;
    }
    bool operator==(const ColorTransform& o) const
    {
        for (int i = 0; i < 4; ++i)
            if (mul[i] != o.mul[i] || add[i] != o.add[i])
                return false;
        return true;
    }
};

ColorTransform readColorTransform(Reader& in, bool alpha)
{
    in.align();
    ColorTransform cx;
    const bool adds = in.ub(1) != 0;
    const bool muls = in.ub(1) != 0;
    const int bits = static_cast<int>(in.ub(4));
    const int count = alpha ? 4 : 3;
    if (muls)
        for (int i = 0; i < count; ++i)
            cx.mul[i] = in.sb(bits) / 256.0;
    if (adds)
        for (int i = 0; i < count; ++i)
            cx.add[i] = in.sb(bits);
    in.align();
    return cx;
}

struct Rgba
{
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

Rgba readRgb(Reader& in, bool alpha)
{
    Rgba c;
    c.r = in.u8();
    c.g = in.u8();
    c.b = in.u8();
    c.a = alpha ? in.u8() : 255;
    return c;
}

// ---------------------------------------------------------------------------
// Shapes

struct Gradient
{
    Matrix matrix;
    int spread = 0;
    int interpolation = 0;
    struct Stop
    {
        int ratio = 0;
        Rgba color;
    };
    std::vector<Stop> stops;
    double focal = 0.0;
};

struct Fill
{
    /// 0x00 solid, 0x10 linear, 0x12 radial, 0x13 focal, 0x40-0x43 bitmap.
    int type = 0;
    Rgba color;
    Gradient gradient;
    int bitmap = 0;
    Matrix matrix;
};

struct Line
{
    double width = 20; // twips
    Rgba color;
    int startCap = 0, endCap = 0, join = 0;
    double miter = 3.0;
    bool noHScale = false, noVScale = false;
    bool hasFill = false;
    Fill fill;
};

Gradient readGradient(Reader& in, bool alpha, bool focal)
{
    Gradient g;
    g.matrix = readMatrix(in);
    in.align();
    g.spread = static_cast<int>(in.ub(2));
    g.interpolation = static_cast<int>(in.ub(2));
    const int count = static_cast<int>(in.ub(4));
    for (int i = 0; i < count; ++i)
    {
        Gradient::Stop stop;
        stop.ratio = in.u8();
        stop.color = readRgb(in, alpha);
        g.stops.push_back(stop);
    }
    if (focal)
        g.focal = in.fixed8();
    return g;
}

Fill readFill(Reader& in, int version)
{
    Fill fill;
    fill.type = in.u8();
    const bool alpha = version >= 3;
    if (fill.type == 0x00)
        fill.color = readRgb(in, alpha);
    else if (fill.type == 0x10 || fill.type == 0x12 || fill.type == 0x13)
        fill.gradient = readGradient(in, alpha, fill.type == 0x13);
    else if (fill.type >= 0x40 && fill.type <= 0x43)
    {
        fill.bitmap = in.u16();
        fill.matrix = readMatrix(in);
    }
    return fill;
}

std::vector<Fill> readFills(Reader& in, int version)
{
    size_t count = in.u8();
    if (count == 0xFF && version >= 2)
        count = in.u16();
    std::vector<Fill> fills;
    for (size_t i = 0; i < count && !in.atEnd(); ++i)
        fills.push_back(readFill(in, version));
    return fills;
}

std::vector<Line> readLines(Reader& in, int version)
{
    size_t count = in.u8();
    if (count == 0xFF)
        count = in.u16();
    std::vector<Line> lines;
    for (size_t i = 0; i < count && !in.atEnd(); ++i)
    {
        Line line;
        line.width = in.u16();
        if (version >= 4)
        {
            in.align();
            line.startCap = static_cast<int>(in.ub(2));
            line.join = static_cast<int>(in.ub(2));
            line.hasFill = in.ub(1) != 0;
            line.noHScale = in.ub(1) != 0;
            line.noVScale = in.ub(1) != 0;
            in.ub(1); // pixel hinting
            in.ub(5);
            in.ub(1); // no close
            line.endCap = static_cast<int>(in.ub(2));
            in.align();
            if (line.join == 2)
                line.miter = in.fixed8();
            if (line.hasFill)
            {
                line.fill = readFill(in, version);
                line.color = line.fill.type == 0 ? line.fill.color : Rgba{0, 0, 0, 255};
                if (line.fill.type == 0)
                    line.hasFill = false;
            }
            else
                line.color = readRgb(in, true);
        }
        else
            line.color = readRgb(in, version >= 3);
        lines.push_back(line);
    }
    return lines;
}

/// A shape record: a change of style or position, or an edge.
struct Record
{
    bool edge = false;
    // A change:
    bool move = false;
    double x = 0, y = 0;
    int fill0 = -1, fill1 = -1, line = -1; // -1 unchanged
    int styles = -1; // the new style chunk it starts, or -1
    // An edge: deltas, in twips.
    bool curve = false;
    double cdx = 0, cdy = 0, adx = 0, ady = 0;
};

/// Reads shape records up to the end record. New styles (DefineShape 2 and
/// up) add a chunk to \a fills and \a lines.
std::vector<Record> readRecords(Reader& in, int fillBits, int lineBits, int version, std::vector<std::vector<Fill>>* fills,
    std::vector<std::vector<Line>>* lines)
{
    std::vector<Record> records;
    while (!in.atEnd())
    {
        Record record;
        if (in.ub(1))
        {
            record.edge = true;
            if (in.ub(1))
            {
                const int bits = static_cast<int>(in.ub(4)) + 2;
                if (in.ub(1))
                {
                    record.adx = in.sb(bits);
                    record.ady = in.sb(bits);
                }
                else if (in.ub(1))
                    record.ady = in.sb(bits);
                else
                    record.adx = in.sb(bits);
            }
            else
            {
                record.curve = true;
                const int bits = static_cast<int>(in.ub(4)) + 2;
                record.cdx = in.sb(bits);
                record.cdy = in.sb(bits);
                record.adx = in.sb(bits);
                record.ady = in.sb(bits);
            }
            records.push_back(record);
            continue;
        }
        const bool newStyles = in.ub(1) != 0;
        const bool lineStyle = in.ub(1) != 0;
        const bool fillStyle1 = in.ub(1) != 0;
        const bool fillStyle0 = in.ub(1) != 0;
        const bool moveTo = in.ub(1) != 0;
        if (!newStyles && !lineStyle && !fillStyle1 && !fillStyle0 && !moveTo)
            break;
        if (moveTo)
        {
            const int bits = static_cast<int>(in.ub(5));
            record.move = true;
            record.x = in.sb(bits);
            record.y = in.sb(bits);
        }
        if (fillStyle0)
            record.fill0 = static_cast<int>(in.ub(fillBits));
        if (fillStyle1)
            record.fill1 = static_cast<int>(in.ub(fillBits));
        if (lineStyle)
            record.line = static_cast<int>(in.ub(lineBits));
        if (newStyles && fills && lines && version >= 2)
        {
            fills->push_back(readFills(in, version));
            lines->push_back(readLines(in, version));
            record.styles = static_cast<int>(fills->size()) - 1;
            in.align();
            fillBits = static_cast<int>(in.ub(4));
            lineBits = static_cast<int>(in.ub(4));
        }
        records.push_back(record);
    }
    in.align();
    return records;
}

/// One move, line or curve, in twips.
struct Command
{
    char kind = 'L'; // 'M', 'L' or 'Q'
    double cx = 0, cy = 0, x = 0, y = 0;
};

/// Edges that share their styles: fill on the left, fill on the right and
/// stroke, 1-based into the shape's lists, 0 for none.
struct Run
{
    int fill0 = 0, fill1 = 0, line = 0;
    std::vector<Command> commands;
    /// The style chunk the run is drawn in (new styles start another).
    int chunk = 0;
};

struct ShapeData
{
    std::vector<Fill> fills;
    std::vector<Line> lines;
    std::vector<Run> runs;
    /// Where each style chunk's fills and lines start in the lists. Flash
    /// draws a chunk's fills and then its lines before the next chunk, so
    /// a later chunk's fills cover an earlier one's lines.
    std::vector<int> fillBase, lineBase;
};

/// \a shape as one shape for each style chunk it draws in, bottom first,
/// each with only that chunk's styles; just \a shape when it has one.
std::vector<ShapeData> chunksOf(const ShapeData& shape)
{
    int last = 0;
    for (const Run& run : shape.runs)
        last = std::max(last, run.chunk);
    if (last == 0)
        return {shape};
    std::vector<ShapeData> out;
    for (int chunk = 0; chunk <= last; ++chunk)
    {
        ShapeData part;
        const auto range = [chunk](const std::vector<int>& base, size_t total) {
            const size_t from = chunk < static_cast<int>(base.size()) ? static_cast<size_t>(base[static_cast<size_t>(chunk)]) : total;
            const size_t to = chunk + 1 < static_cast<int>(base.size()) ? static_cast<size_t>(base[static_cast<size_t>(chunk) + 1]) : total;
            return std::make_pair(std::min(from, total), std::min(std::max(from, to), total));
        };
        const auto [fillFrom, fillTo] = range(shape.fillBase, shape.fills.size());
        const auto [lineFrom, lineTo] = range(shape.lineBase, shape.lines.size());
        part.fills.assign(shape.fills.begin() + static_cast<long>(fillFrom), shape.fills.begin() + static_cast<long>(fillTo));
        part.lines.assign(shape.lines.begin() + static_cast<long>(lineFrom), shape.lines.begin() + static_cast<long>(lineTo));
        const auto local = [](int index, size_t from, size_t to) {
            return index > static_cast<int>(from) && index <= static_cast<int>(to) ? index - static_cast<int>(from) : 0;
        };
        for (const Run& run : shape.runs)
        {
            if (run.chunk != chunk)
                continue;
            Run moved = run;
            moved.chunk = 0;
            moved.fill0 = local(run.fill0, fillFrom, fillTo);
            moved.fill1 = local(run.fill1, fillFrom, fillTo);
            moved.line = local(run.line, lineFrom, lineTo);
            if (moved.fill0 || moved.fill1 || moved.line)
                part.runs.push_back(std::move(moved));
        }
        if (!part.runs.empty())
            out.push_back(std::move(part));
    }
    return out;
}

/// Records as runs of edges with their styles, the style chunks flattened
/// into one list each.
ShapeData buildShape(const std::vector<Record>& records, const std::vector<std::vector<Fill>>& fillChunks,
    const std::vector<std::vector<Line>>& lineChunks)
{
    ShapeData shape;
    std::vector<int> fillBase, lineBase;
    for (const auto& chunk : fillChunks)
    {
        fillBase.push_back(static_cast<int>(shape.fills.size()));
        shape.fills.insert(shape.fills.end(), chunk.begin(), chunk.end());
    }
    for (const auto& chunk : lineChunks)
    {
        lineBase.push_back(static_cast<int>(shape.lines.size()));
        shape.lines.insert(shape.lines.end(), chunk.begin(), chunk.end());
    }
    shape.fillBase = fillBase;
    shape.lineBase = lineBase;
    int chunk = 0;
    int fill0 = 0, fill1 = 0, line = 0;
    double x = 0, y = 0;
    Run* run = nullptr;
    const auto global = [&](int local, const std::vector<int>& base, size_t total) {
        if (local <= 0 || base.empty())
            return 0;
        const int index = base[static_cast<size_t>(std::min(chunk, static_cast<int>(base.size()) - 1))] + local;
        return index <= static_cast<int>(total) ? index : 0;
    };
    for (const Record& record : records)
    {
        if (record.edge)
        {
            if (!run)
            {
                shape.runs.push_back(Run{fill0, fill1, line, {}, chunk});
                run = &shape.runs.back();
                run->commands.push_back(Command{'M', 0, 0, x, y});
            }
            Command command;
            if (record.curve)
            {
                command.kind = 'Q';
                command.cx = x + record.cdx;
                command.cy = y + record.cdy;
                x = command.cx + record.adx;
                y = command.cy + record.ady;
            }
            else
            {
                x += record.adx;
                y += record.ady;
            }
            command.x = x;
            command.y = y;
            run->commands.push_back(command);
            continue;
        }
        if (record.styles >= 0)
        {
            chunk = record.styles;
            fill0 = fill1 = line = 0;
        }
        const bool styled = record.fill0 >= 0 || record.fill1 >= 0 || record.line >= 0 || record.styles >= 0;
        if (record.fill0 >= 0)
            fill0 = global(record.fill0, fillBase, shape.fills.size());
        if (record.fill1 >= 0)
            fill1 = global(record.fill1, fillBase, shape.fills.size());
        if (record.line >= 0)
            line = global(record.line, lineBase, shape.lines.size());
        if (record.move)
        {
            x = record.x;
            y = record.y;
        }
        if (styled)
            run = nullptr;
        else if (record.move && run)
            run->commands.push_back(Command{'M', 0, 0, x, y});
    }
    // Runs with nothing to draw go.
    shape.runs.erase(std::remove_if(shape.runs.begin(), shape.runs.end(),
                         [](const Run& r) { return r.commands.size() < 2 || (!r.fill0 && !r.fill1 && !r.line); }),
        shape.runs.end());
    return shape;
}

// ---------------------------------------------------------------------------
// Characters

struct MorphData
{
    std::vector<Fill> startFills, endFills;
    std::vector<Line> startLines, endLines;
    std::vector<Record> start, end;
};

struct Font
{
    std::string name;
    bool bold = false, italic = false;
    double em = 1024.0;
    std::vector<std::vector<Record>> glyphs;
};

struct EditText
{
    Rect bounds;
    std::string text;
    std::string variable;
    int font = 0;
    double height = 240; // twips
    Rgba color;
    bool hasColor = false;
    bool readOnly = true, multiline = false, wordWrap = false, password = false, html = false, selectable = true;
    bool border = false;
    int align = 0;
    int maxLength = 0;
};

/// ActionScript 1 and 2 as the movie has it compiled: a frame's DoAction, a
/// sprite's DoInitAction (on the frame of the main timeline it comes in), a
/// movie clip's handler (the events it runs on, as PlaceObject numbers them,
/// and the key of a keyPress) or a button's (its conditions). Kept with the
/// frame or the instance as a CompiledActions element, which Test Movie
/// runs.
struct CompiledAction
{
    enum class Kind
    {
        Frame,
        Init,
        Clip,
        Button
    };
    Kind kind = Kind::Frame;
    int sprite = 0;
    uint32_t events = 0;
    int key = 0;
    std::vector<uint8_t> actions;
};

struct Placement
{
    int character = 0;
    Matrix matrix;
    ColorTransform color;
    std::string name;
    int ratio = 0;
    int clipDepth = 0;
    int blend = 0;
    /// Filters as XFL elements: name and attributes.
    struct Filter
    {
        std::string element;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<std::pair<Rgba, int>> entries;
    };
    std::vector<Filter> filters;
    /// Bumped on every change, so a span of frames is where it holds.
    long version = 0;
    /// Set anew when a character is placed rather than moved: a movie clip
    /// starts over then.
    long instance = 0;
    /// A movie clip's handlers (onClipEvent), as placed.
    std::shared_ptr<const std::vector<CompiledAction>> clipActions;
};

struct SoundUse
{
    int sound = 0;
    bool stop = false;
    bool noMultiple = false;
    int loops = 0;
};

struct TimelineData
{
    int frameCount = 0;
    std::vector<std::map<int, Placement>> frames;
    std::map<int, std::string> labels;
    std::map<int, std::vector<SoundUse>> sounds;
    std::map<int, std::string> scripts;
    /// The frames' compiled actions, in the order they come.
    std::map<int, std::vector<CompiledAction>> actions;
    // A streamed soundtrack.
    int streamCodec = -1;
    int streamRate = 0;
    bool streamStereo = false;
    bool stream16 = true;
    int streamFirst = -1, streamLast = -1;
    std::vector<uint8_t> streamData; // MP3 frames, or 16-bit PCM
    bool streamMp3 = false;
};

struct Character
{
    enum class Kind
    {
        Shape,
        Morph,
        Sprite,
        Button,
        Bitmap,
        Sound,
        EditText,
        Font,
        Video
    };
    Kind kind = Kind::Shape;
    ShapeData shape;
    MorphData morph;
    std::shared_ptr<TimelineData> timeline;
    EditText edit;
    Font font;
    // Library item, once named.
    std::string name;
    // Bitmaps and sounds: their item's attributes and data.
    int width = 0, height = 0;
    bool jpeg = false;
    std::vector<uint8_t> data;
    std::string soundFormat;
    uint32_t sampleCount = 0;
    // Videos: their frames and codec.
    int videoFrames = 0;
    int codec = 0;
    // Shapes, static text and morph shapes: their bounds, in twips.
    Rect bounds;
    // 9-slice, in twips.
    bool scale9 = false;
    Rect grid;
    std::string linkage;
    int uses = 0;
    // Buttons: their handlers, which each instance carries.
    std::vector<CompiledAction> buttonActions;
};

/// Bytes as hexadecimal text, for compiled actions in XML.
std::string hexOf(const std::vector<uint8_t>& bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const uint8_t byte : bytes)
    {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

// ---------------------------------------------------------------------------
// Writing helpers

std::string number(double value, int places = 3)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", places, value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos)
    {
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
    }
    if (text == "-0")
        text = "0";
    return text;
}

std::string hexColor(const Rgba& c)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", c.r, c.g, c.b);
    return buffer;
}

std::vector<uint8_t> printed(tinyxml2::XMLDocument& doc)
{
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    const char* text = printer.CStr();
    return std::vector<uint8_t>(text, text + std::strlen(text));
}

std::vector<uint8_t> inflate(const std::vector<uint8_t>& data, size_t expected = 0)
{
    std::vector<uint8_t> out;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return out;
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    // Deflate never grows data more than about 1032 times, so a larger
    // claimed size is only a damaged header.
    expected = std::min(expected, data.size() * 1032 + 1024);
    out.resize(expected > 0 ? expected : data.size() * 4 + 1024);
    for (;;)
    {
        stream.next_out = out.data() + stream.total_out;
        stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
        const int result = ::inflate(&stream, Z_NO_FLUSH);
        if (result == Z_STREAM_END)
            break;
        if (result != Z_OK && result != Z_BUF_ERROR)
            break;
        if (stream.avail_out == 0)
            out.resize(out.size() * 2);
        else if (stream.avail_in == 0)
            break;
    }
    out.resize(stream.total_out);
    inflateEnd(&stream);
    return out;
}

class Converter
{
public:
    std::string report;

    explicit Converter(const Options& options)
        : _options(options)
    {}

    bool run(const std::vector<uint8_t>& file, std::map<std::string, std::vector<uint8_t>>& files, std::string& error)
    {
        if (file.size() < 8 || !isSwf(file.data(), file.size()))
        {
            error = "This is not an SWF movie.";
            return false;
        }
        _version = file[3];
        std::vector<uint8_t> body;
        const uint32_t length = file[4] | (file[5] << 8) | (file[6] << 16) | (static_cast<uint32_t>(file[7]) << 24);
        if (file[0] == 'F')
            body.assign(file.begin() + 8, file.end());
        else if (file[0] == 'C')
        {
            body = inflate(std::vector<uint8_t>(file.begin() + 8, file.end()), length > 8 ? length - 8 : 0);
            if (body.empty())
            {
                error = "This SWF movie is damaged: its compressed data cannot be read.";
                return false;
            }
        }
        else
        {
            // LZMA: the compressed length, the five property bytes, then the
            // data, which decodes to the movie less its 8-byte header.
            const size_t size = length > 8 ? length - 8 : 0;
            if (file.size() < 17 || size > (size_t(1) << 30) ||
                !lzma::decode(&file[12], file.data() + 17, file.size() - 17, size, body) || body.size() != size)
            {
                error = "This SWF movie is damaged: its compressed data cannot be read.";
                return false;
            }
        }
        Reader in(body.data(), body.size());
        const Rect stage = readRect(in);
        _frameRate = in.u16() / 256.0;
        in.u16(); // frame count
        _width = static_cast<int>(std::lround((stage.right - stage.left) / 20.0));
        _height = static_cast<int>(std::lround((stage.bottom - stage.top) / 20.0));

        // A movie published with a preloader for its runtime shared
        // libraries (TLF text needs one): its own frames only load them and
        // show a loading animation, and the movie itself is embedded whole as
        // binary data, which the preloader then starts. That movie is read
        // instead.
        if (_nesting < 4)
        {
            const std::vector<uint8_t> content = preloadedContent(in);
            if (!content.empty())
            {
                Converter inner(_options);
                inner._nesting = _nesting + 1;
                if (inner.run(content, files, error))
                {
                    report = "This movie loads its runtime shared libraries before it starts; the movie it "
                             "starts was read. " + inner.report;
                    return true;
                }
            }
        }

        _main = std::make_shared<TimelineData>();
        readTags(in, *_main, true);
        finish(files);
        keepActionScript3(files);
        return true;
    }

private:
    /// The movie's ActionScript 3 as it was compiled (each DoABC tag's
    /// abcFile) and its main timeline's class: kept with the document so
    /// that Test Movie runs them, as publish settings saying so.
    std::vector<std::vector<uint8_t>> _abcFiles;
    std::string _rootClass;

    void keepActionScript3(std::map<std::string, std::vector<uint8_t>>& files)
    {
        if (_abcFiles.empty())
        {
            // ActionScript 1 and 2, kept compiled with the frames and the
            // instances: the publish settings say which, for Test Movie.
            if (_scripts == 0 || files.count("PublishSettings.xml"))
                return;
            const std::string settings = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<flash_profiles>\n"
                                         "<flash_profile version=\"1.0\" name=\"Default\" current=\"true\">\n"
                                         "<PublishFlashProperties enabled=\"true\">\n"
                                         "<ActionScriptVersion>2</ActionScriptVersion>\n"
                                         "</PublishFlashProperties>\n</flash_profile>\n</flash_profiles>\n";
            files["PublishSettings.xml"].assign(settings.begin(), settings.end());
            return;
        }
        for (size_t i = 0; i < _abcFiles.size(); ++i)
            files["bin/abc/" + std::to_string(i + 1) + ".abc"] = _abcFiles[i];
        if (files.count("PublishSettings.xml"))
            return;
        std::string settings = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<flash_profiles>\n"
                               "<flash_profile version=\"1.0\" name=\"Default\" current=\"true\">\n"
                               "<PublishFlashProperties enabled=\"true\">\n"
                               "<ActionScriptVersion>3</ActionScriptVersion>\n";
        if (!_rootClass.empty())
            settings += "<DocumentClass>" + _rootClass + "</DocumentClass>\n";
        settings += "</PublishFlashProperties>\n</flash_profile>\n</flash_profiles>\n";
        files["PublishSettings.xml"].assign(settings.begin(), settings.end());
    }
    Options _options;
    int _version = 10;
    double _frameRate = 24.0;
    int _width = 550, _height = 400;
    Rgba _background{255, 255, 255, 255};
    std::shared_ptr<TimelineData> _main;
    /// The main timeline's scenes, by first frame and name, as
    /// DefineSceneAndFrameLabelData gives them; none or one for a single
    /// scene.
    std::vector<std::pair<uint32_t, std::string>> _scenes;

    /// Frames \a first up to \a end of \a t, counted from \a first: one
    /// scene of the main timeline. A streamed soundtrack goes with the scene
    /// it starts in.
    static TimelineData sliceOf(const TimelineData& t, int first, int end)
    {
        TimelineData slice;
        first = std::clamp(first, 0, t.frameCount);
        end = std::clamp(end, first, t.frameCount);
        slice.frames.assign(t.frames.begin() + first, t.frames.begin() + end);
        slice.frameCount = end - first;
        if (slice.frameCount == 0)
        {
            slice.frames.emplace_back();
            slice.frameCount = 1;
        }
        const auto shift = [first, end](const auto& from, auto& to) {
            for (const auto& [frame, value] : from)
                if (frame >= first && frame < end)
                    to[frame - first] = value;
        };
        shift(t.labels, slice.labels);
        shift(t.sounds, slice.sounds);
        shift(t.scripts, slice.scripts);
        shift(t.actions, slice.actions);
        if (t.streamCodec >= 0 && t.streamFirst >= first && t.streamFirst < end)
        {
            slice.streamCodec = t.streamCodec;
            slice.streamRate = t.streamRate;
            slice.streamStereo = t.streamStereo;
            slice.stream16 = t.stream16;
            slice.streamFirst = t.streamFirst - first;
            slice.streamLast = t.streamLast - first;
            slice.streamData = t.streamData;
            slice.streamMp3 = t.streamMp3;
        }
        return slice;
    }
    std::map<int, Character> _characters;
    std::vector<uint8_t> _jpegTables;
    std::map<std::string, int> _unread; // tag names left out, counted
    std::map<int, std::string> _exportNames;
    long _version_counter = 0;
    int _scripts = 0;
    int _abc = 0;
    /// How many preloaders this movie was found inside.
    int _nesting = 0;
    std::set<std::string> _names;
    std::set<std::string> _folders;
    int _dataCount = 0;
    // Guards against damaged movies: placements copied into frames, sprites
    // being read and how deep, and how deep symbols are being written.
    static constexpr size_t kMaxPlacements = 8000000;
    static constexpr int kMaxNesting = 64;
    static constexpr size_t kMaxFontRecords = 4000000;
    size_t _placements = 0;
    std::set<int> _reading;
    int _symbolDepth = 0;

    void leftOut(const std::string& what) { ++_unread[what]; }

    /// The movie a runtime shared library preloader starts, when \a in (at
    /// the first tag) is one: binary data whose class, named in a
    /// SymbolClass tag, ends in "__Content__" and that holds an SWF. Empty
    /// otherwise.
    static std::vector<uint8_t> preloadedContent(Reader in)
    {
        std::map<int, std::pair<const uint8_t*, size_t>> data;
        std::set<int> content;
        while (!in.atEnd())
        {
            const uint16_t header = in.u16();
            const int code = header >> 6;
            size_t length = header & 0x3F;
            if (length == 0x3F)
                length = in.u32();
            const size_t start = in.position();
            length = std::min(length, in.left());
            if (code == 0)
                break;
            Reader tag(in.data() + start, length);
            if (code == 87 && length > 6) // DefineBinaryData: id, a reserved word, the bytes
                data[tag.u16()] = {in.data() + start + 6, length - 6};
            else if (code == 76) // SymbolClass
            {
                const int count = tag.u16();
                const std::string suffix = "__Content__";
                for (int i = 0; i < count && !tag.atEnd(); ++i)
                {
                    const int id = tag.u16();
                    const std::string name = tag.string();
                    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                        content.insert(id);
                }
            }
            in.seek(start + length);
        }
        for (const int id : content)
        {
            const auto found = data.find(id);
            if (found != data.end() && isSwf(found->second.first, found->second.second) && found->second.second >= 8)
                return std::vector<uint8_t>(found->second.first, found->second.first + found->second.second);
        }
        return {};
    }

    // -- Tags ---------------------------------------------------------------

    void readTags(Reader& in, TimelineData& timeline, bool root)
    {
        std::map<int, Placement> list;
        int frame = 0;
        bool full = false;
        while (!in.atEnd() && !full)
        {
            const uint16_t header = in.u16();
            const int code = header >> 6;
            size_t length = header & 0x3F;
            if (length == 0x3F)
                length = in.u32();
            const size_t start = in.position();
            length = std::min(length, in.left());
            if (code == 0)
                break;
            Reader tag(in.data() + start, length);
            switch (code)
            {
            case 1: // ShowFrame
                // Each frame keeps its own copy of the display list, so a
                // movie past the budget ends there.
                _placements += list.size() + 1;
                if (_placements > kMaxPlacements)
                {
                    leftOut("frames past the size limit");
                    full = true;
                    break;
                }
                timeline.frames.push_back(list);
                ++frame;
                break;
            case 4:
            case 26:
            case 70:
                place(tag, code, list);
                break;
            case 5: // RemoveObject
                tag.u16();
                list.erase(tag.u16());
                break;
            case 28: // RemoveObject2
                list.erase(tag.u16());
                break;
            case 9:
                _background = readRgb(tag, false);
                break;
            case 43: // FrameLabel
                timeline.labels[frame] = tag.string();
                break;
            case 86: // DefineSceneAndFrameLabelData: the scenes, by first frame
                if (root)
                {
                    const uint32_t scenes = std::min<uint32_t>(tag.encodedU32(), 1000);
                    for (uint32_t s = 0; s < scenes && tag.left() > 0; ++s)
                    {
                        const uint32_t offset = tag.encodedU32();
                        _scenes.emplace_back(offset, tag.string());
                    }
                    const uint32_t labels = std::min<uint32_t>(tag.encodedU32(), 100000);
                    for (uint32_t l = 0; l < labels && tag.left() > 0; ++l)
                    {
                        const uint32_t at = tag.encodedU32();
                        std::string name = tag.string();
                        timeline.labels.emplace(static_cast<int>(at), std::move(name));
                    }
                }
                break;
            case 12: // DoAction: kept as compiled, which Test Movie runs
            {
                CompiledAction action;
                action.actions = tag.bytes(tag.left());
                timeline.actions[frame].push_back(std::move(action));
                timeline.scripts[frame] += "// ActionScript compiled into the SWF (" + std::to_string(length) +
                    " bytes): it runs as compiled when the movie plays, but cannot be turned back into source.\n";
                ++_scripts;
                break;
            }
            case 59: // DoInitAction: a sprite's #initclip actions
            {
                CompiledAction action;
                action.kind = CompiledAction::Kind::Init;
                action.sprite = tag.u16();
                action.actions = tag.bytes(tag.left());
                timeline.actions[frame].push_back(std::move(action));
                timeline.scripts[frame] += "// ActionScript compiled into the SWF to set up a symbol (#initclip, " +
                    std::to_string(length) + " bytes): it runs as compiled when the movie plays.\n";
                ++_scripts;
                break;
            }
            case 72:
            case 82: // DoABC: (flags and a name, then) the abcFile
                ++_abc;
                if (code == 82)
                {
                    tag.u32();
                    tag.string();
                }
                if (tag.left() > 0)
                    _abcFiles.emplace_back(tag.data() + tag.position(), tag.data() + tag.position() + tag.left());
                break;
            case 15: // StartSound
            case 89: // StartSound2
            {
                SoundUse use;
                if (code == 89)
                {
                    const std::string className = tag.string();
                    (void)className;
                    leftOut("sounds started by class name");
                    break;
                }
                use.sound = tag.u16();
                readSoundInfo(tag, use);
                timeline.sounds[frame].push_back(use);
                break;
            }
            case 18:
            case 45: // SoundStreamHead, SoundStreamHead2
            {
                tag.u8();
                tag.align();
                timeline.streamCodec = static_cast<int>(tag.ub(4));
                const int rate = static_cast<int>(tag.ub(2));
                timeline.stream16 = tag.ub(1) != 0;
                timeline.streamStereo = tag.ub(1) != 0;
                timeline.streamRate = rate;
                timeline.streamMp3 = timeline.streamCodec == 2;
                break;
            }
            case 19: // SoundStreamBlock
                streamBlock(tag, length, timeline, frame);
                break;
            default:
                if (!define(tag, code, length))
                    ignore(code);
                break;
            }
            in.seek(start + length);
        }
        timeline.frameCount = static_cast<int>(timeline.frames.size());
        if (timeline.frameCount == 0)
        {
            timeline.frames.push_back(list);
            timeline.frameCount = 1;
        }
        (void)root;
    }

    void ignore(int code)
    {
        switch (code)
        {
        case 8: // JPEGTables, read in define
        case 24: // Protect
        case 58:
        case 64: // EnableDebugger
        case 65: // ScriptLimits
        case 66: // SetTabIndex
        case 69: // FileAttributes
        case 77: // Metadata
        case 41: // ProductInfo
        case 63: // DebugID
        case 73: // DefineFontAlignZones
        case 74: // CSMTextSettings
        case 88: // DefineFontName
        case 86: // DefineSceneAndFrameLabelData
        case 23: // DefineButtonCxform
        case 13:
        case 62: // DefineFontInfo, read in define
            return;
        case 87:
            leftOut("binary data");
            return;
        default:
            leftOut("tag " + std::to_string(code));
        }
    }

    void place(Reader& tag, int code, std::map<int, Placement>& list)
    {
        if (code == 4)
        {
            Placement p;
            p.character = tag.u16();
            const int depth = tag.u16();
            p.matrix = readMatrix(tag);
            if (tag.left() > 0)
                p.color = readColorTransform(tag, false);
            p.version = ++_version_counter;
            p.instance = p.version;
            list[depth] = p;
            return;
        }
        tag.align();
        const uint8_t flags = tag.u8();
        uint8_t flags2 = 0;
        if (code == 70)
            flags2 = tag.u8();
        const int depth = tag.u16();
        const bool move = flags & 0x01;
        const bool hasCharacter = flags & 0x02;
        const bool hasMatrix = flags & 0x04;
        const bool hasColor = flags & 0x08;
        const bool hasRatio = flags & 0x10;
        const bool hasName = flags & 0x20;
        const bool hasClip = flags & 0x40;
        const bool hasClassName = (flags2 & 0x08) || ((flags2 & 0x10) && hasCharacter);
        if (code == 70 && hasClassName)
            tag.string();
        Placement p;
        const auto found = list.find(depth);
        if (move && found != list.end())
            p = found->second;
        if (hasCharacter)
        {
            const int character = tag.u16();
            if (!move || p.character != character)
            {
                if (!move)
                    p = Placement();
                p.character = character;
                p.instance = _version_counter + 1;
            }
        }
        if (hasMatrix)
            p.matrix = readMatrix(tag);
        if (hasColor)
            p.color = readColorTransform(tag, true);
        if (hasRatio)
            p.ratio = tag.u16();
        if (hasName)
            p.name = tag.string();
        if (hasClip)
            p.clipDepth = tag.u16();
        if (code == 70)
        {
            if (flags2 & 0x01)
                p.filters = readFilters(tag);
            if (flags2 & 0x02)
                p.blend = tag.u8();
            if (flags2 & 0x04)
                tag.u8(); // cache as bitmap
            if (flags2 & 0x20)
                tag.u8(); // visible
            if (flags2 & 0x40)
                tag.u32(); // background color
        }
        if (flags & 0x80)
            p.clipActions = readClipActions(tag);
        if (p.character == 0)
            return;
        p.version = ++_version_counter;
        list[depth] = p;
    }

    /// A placement's CLIPACTIONS: its handlers, each with the events it
    /// runs on (16 bits of them before SWF 6), and a keyPress's key.
    std::shared_ptr<const std::vector<CompiledAction>> readClipActions(Reader& tag)
    {
        auto actions = std::make_shared<std::vector<CompiledAction>>();
        const bool wide = _version >= 6;
        tag.u16();
        if (wide)
            tag.u32();
        else
            tag.u16();
        while (tag.left() >= (wide ? 4u : 2u))
        {
            CompiledAction action;
            action.kind = CompiledAction::Kind::Clip;
            action.events = wide ? tag.u32() : tag.u16();
            if (action.events == 0 || tag.left() < 4)
                break;
            size_t size = std::min<size_t>(tag.u32(), tag.left());
            if (wide && (action.events & (1u << 17)) && size > 0)
            {
                action.key = tag.u8();
                --size;
            }
            action.actions = tag.bytes(size);
            actions->push_back(std::move(action));
            ++_scripts;
        }
        return actions->empty() ? nullptr : actions;
    }

    static std::string numbers(const std::vector<double>& values)
    {
        std::string out;
        for (size_t i = 0; i < values.size(); ++i)
            out += (i ? "," : "") + number(values[i], 6);
        return out;
    }

    /// The brightness, contrast, saturation and hue of Adjust Color that
    /// make color matrix  m as Phoenix draws Adjust Color, if any do.
    bool adjustColorOf(const double m[20], double& brightness, double& contrast, double& saturation, double& hue)
    {
        // A movie places the same filter frame after frame: worked out once.
        const std::vector<double> key(m, m + 20);
        const auto known = _adjustColors.find(key);
        if (known == _adjustColors.end())
        {
            std::array<double, 4> found{};
            const bool fits = fitAdjustColor(m, found[0], found[1], found[2], found[3]);
            _adjustColors[key] = fits ? std::optional<std::array<double, 4>>(found) : std::nullopt;
            return adjustColorOf(m, brightness, contrast, saturation, hue);
        }
        if (!known->second)
            return false;
        brightness = (*known->second)[0];
        contrast = (*known->second)[1];
        saturation = (*known->second)[2];
        hue = (*known->second)[3];
        return true;
    }
    std::map<std::vector<double>, std::optional<std::array<double, 4>>> _adjustColors;

    static bool fitAdjustColor(const double m[20], double& brightness, double& contrast, double& saturation,
        double& hue)
    {
        // Alpha left as it is, and color made of color alone.
        const double alphaRow[5] = {0, 0, 0, 1, 0};
        for (int c = 0; c < 5; ++c)
            if (std::fabs(m[15 + c] - alphaRow[c]) > 1e-4)
                return false;
        if (std::fabs(m[3]) > 1e-4 || std::fabs(m[8]) > 1e-4 || std::fabs(m[13]) > 1e-4)
            return false;
        // Saturation and hue keep gray gray: each row's sum is what contrast
        // scales by, and the offset what it and brightness add.
        const double scale = m[0] + m[1] + m[2];
        if (std::fabs(m[5] + m[6] + m[7] - scale) > 1e-3 || std::fabs(m[10] + m[11] + m[12] - scale) > 1e-3 ||
            std::fabs(m[9] - m[4]) > 1e-2 || std::fabs(m[14] - m[4]) > 1e-2)
            return false;
        // The contrast that scales so (whole numbers, as the panel sets it),
        // and the brightness that, scaled, leaves this offset.
        double bestScale = 1e9;
        contrast = 0.0;
        for (int c = -100; c <= 100; ++c)
        {
            const double each = fla::display::BitmapFilter::adjustColor(0.0, c, 0.0, 0.0).matrix[0];
            if (std::fabs(each - scale) < std::fabs(bestScale - scale))
            {
                bestScale = each;
                contrast = c;
            }
        }
        if (std::fabs(bestScale - scale) > 1e-3 || bestScale < 1e-6)
            return false;
        const double offset = fla::display::BitmapFilter::adjustColor(0.0, contrast, 0.0, 0.0).matrix[4];
        brightness = (m[4] - offset) / bestScale;
        if (brightness < -100.0 - 1e-2 || brightness > 100.0 + 1e-2)
            return false;
        brightness = std::round(brightness * 100.0) / 100.0;
        const auto error = [&](double s, double h) {
            const std::vector<double> made =
                fla::display::BitmapFilter::adjustColor(brightness, contrast, s, h).matrix;
            double worst = 0.0;
            for (int k = 0; k < 20; ++k)
                worst = std::max(worst, std::fabs(made[static_cast<size_t>(k)] - m[k]) / (k % 5 == 4 ? 255.0 : 1.0));
            return worst;
        };
        // Whole numbers first, as the panel sets them, then closer.
        double bestS = 0.0, bestH = 0.0, best = error(0.0, 0.0);
        for (int s = -100; s <= 100; ++s)
            for (int h = -180; h <= 180; ++h)
            {
                const double e = error(s, h);
                if (e < best)
                {
                    best = e;
                    bestS = s;
                    bestH = h;
                }
            }
        for (double step : {0.1, 0.01})
        {
            const double s0 = bestS, h0 = bestH;
            for (int i = -10; i <= 10; ++i)
                for (int j = -10; j <= 10; ++j)
                {
                    const double s = std::clamp(s0 + i * step, -100.0, 100.0);
                    const double h = std::clamp(h0 + j * step, -180.0, 180.0);
                    const double e = error(s, h);
                    if (e < best)
                    {
                        best = e;
                        bestS = s;
                        bestH = h;
                    }
                }
        }
        if (best > 1e-3)
            return false;
        saturation = bestS;
        hue = bestH;
        return true;
    }

    std::vector<Placement::Filter> readFilters(Reader& tag)
    {
        std::vector<Placement::Filter> filters;
        const int count = tag.u8();
        const double degrees = 180.0 / 3.14159265358979323846;
        for (int i = 0; i < count && !tag.atEnd(); ++i)
        {
            const int type = tag.u8();
            Placement::Filter filter;
            const auto set = [&filter](const char* name, const std::string& value) {
                filter.attributes.emplace_back(name, value);
            };
            const auto flagsOf = [&](bool bevel) {
                tag.align();
                const bool inner = tag.ub(1) != 0;
                const bool knockout = tag.ub(1) != 0;
                tag.ub(1); // composite
                bool onTop = false;
                int passes;
                if (bevel)
                {
                    onTop = tag.ub(1) != 0;
                    passes = static_cast<int>(tag.ub(4));
                }
                else
                    passes = static_cast<int>(tag.ub(5));
                tag.align();
                set("quality", std::to_string(std::max(1, std::min(3, passes))));
                if (knockout)
                    set("knockout", "true");
                return std::make_pair(inner, onTop);
            };
            switch (type)
            {
            case 0: // drop shadow
            {
                filter.element = "DropShadowFilter";
                const Rgba color = readRgb(tag, true);
                const double blurX = tag.fixed16(), blurY = tag.fixed16();
                const double angle = tag.fixed16(), distance = tag.fixed16();
                const double strength = tag.fixed8();
                set("color", hexColor(color));
                set("alpha", number(color.a / 255.0));
                set("blurX", number(blurX, 10));
                set("blurY", number(blurY, 10));
                set("angle", number(angle * degrees, 10));
                set("distance", number(distance, 10));
                set("strength", number(strength, 10));
                const auto flags = flagsOf(false);
                if (flags.first)
                    set("inner", "true");
                break;
            }
            case 1: // blur
            {
                filter.element = "BlurFilter";
                set("blurX", number(tag.fixed16(), 10));
                set("blurY", number(tag.fixed16(), 10));
                tag.align();
                set("quality", std::to_string(std::max(1, std::min(3, static_cast<int>(tag.ub(5))))));
                tag.ub(3);
                break;
            }
            case 2: // glow
            {
                filter.element = "GlowFilter";
                const Rgba color = readRgb(tag, true);
                set("color", hexColor(color));
                set("alpha", number(color.a / 255.0));
                set("blurX", number(tag.fixed16(), 10));
                set("blurY", number(tag.fixed16(), 10));
                set("strength", number(tag.fixed8(), 10));
                if (flagsOf(false).first)
                    set("inner", "true");
                break;
            }
            case 3: // bevel
            {
                filter.element = "BevelFilter";
                // The highlight comes first, whatever the format's
                // description says.
                const Rgba highlight = readRgb(tag, true);
                const Rgba shadow = readRgb(tag, true);
                set("shadowColor", hexColor(shadow));
                set("shadowAlpha", number(shadow.a / 255.0));
                set("highlightColor", hexColor(highlight));
                set("highlightAlpha", number(highlight.a / 255.0));
                set("blurX", number(tag.fixed16(), 10));
                set("blurY", number(tag.fixed16(), 10));
                set("angle", number(tag.fixed16() * degrees, 10));
                set("distance", number(tag.fixed16(), 10));
                set("strength", number(tag.fixed8(), 10));
                const auto flags = flagsOf(true);
                set("type", flags.second ? "full" : flags.first ? "inner" : "outer");
                break;
            }
            case 4: // gradient glow
            case 7: // gradient bevel
            {
                filter.element = type == 4 ? "GradientGlowFilter" : "GradientBevelFilter";
                const int colors = tag.u8();
                std::vector<Rgba> list;
                for (int c = 0; c < colors; ++c)
                    list.push_back(readRgb(tag, true));
                for (int c = 0; c < colors; ++c)
                    filter.entries.emplace_back(list[static_cast<size_t>(c)], tag.u8());
                set("blurX", number(tag.fixed16(), 10));
                set("blurY", number(tag.fixed16(), 10));
                set("angle", number(tag.fixed16() * degrees, 10));
                set("distance", number(tag.fixed16(), 10));
                set("strength", number(tag.fixed8(), 10));
                const auto flags = flagsOf(true);
                set("type", flags.second ? "full" : flags.first ? "inner" : "outer");
                break;
            }
            case 5: // convolution: Phoenix's own, on a Blur that does nothing
            {
                filter.element = "BlurFilter";
                const int x = tag.u8(), y = tag.u8();
                const double divisor = tag.f32(), bias = tag.f32();
                std::vector<double> matrix;
                for (int k = 0; k < x * y; ++k)
                    matrix.push_back(tag.f32());
                const Rgba color = readRgb(tag, true);
                const int flags = tag.u8();
                set("blurX", "0");
                set("blurY", "0");
                set("phoenixConvolution", numbers(matrix));
                set("phoenixMatrixX", std::to_string(std::min(x, 15)));
                set("phoenixMatrixY", std::to_string(std::min(y, 15)));
                set("phoenixDivisor", number(divisor, 6));
                set("phoenixBias", number(bias, 6));
                set("phoenixPreserveAlpha", flags & 0x01 ? "true" : "false");
                set("phoenixClamp", flags & 0x02 ? "true" : "false");
                set("phoenixEdgeColor", hexColor(color));
                set("phoenixEdgeAlpha", number(color.a / 255.0));
                break;
            }
            case 6: // color matrix: an Adjust Color where it is one
            {
                filter.element = "AdjustColorFilter";
                double m[20];
                for (double& value : m)
                    value = tag.f32();
                double brightness, contrast, saturation, hue;
                if (adjustColorOf(m, brightness, contrast, saturation, hue))
                {
                    set("brightness", number(brightness, 2));
                    set("contrast", number(contrast, 2));
                    set("saturation", number(saturation, 2));
                    set("hue", number(hue, 2));
                }
                else
                    set("phoenixColorMatrix", numbers(std::vector<double>(m, m + 20)));
                break;
            }
            default:
                return filters;
            }
            if (!filter.element.empty())
                filters.push_back(filter);
        }
        return filters;
    }

    void streamBlock(Reader& tag, size_t length, TimelineData& timeline, int frame)
    {
        if (timeline.streamCodec < 0)
            return;
        if (timeline.streamFirst < 0)
            timeline.streamFirst = frame;
        timeline.streamLast = frame;
        if (timeline.streamCodec == 2)
        {
            tag.u16();
            tag.u16();
            const std::vector<uint8_t> frames = tag.bytes(length - 4);
            timeline.streamData.insert(timeline.streamData.end(), frames.begin(), frames.end());
        }
        else if (timeline.streamCodec == 1)
        {
            const std::vector<uint8_t> data = tag.bytes(length);
            const int channels = timeline.streamStereo ? 2 : 1;
            const std::vector<int16_t> pcm = swf::decodeAdpcm(data, channels, adpcmCount(data, channels));
            for (int16_t sample : pcm)
            {
                timeline.streamData.push_back(static_cast<uint8_t>(sample & 0xFF));
                timeline.streamData.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
            }
        }
        else if (timeline.streamCodec == 0 || timeline.streamCodec == 3)
        {
            const std::vector<uint8_t> data = tag.bytes(length);
            timeline.streamData.insert(timeline.streamData.end(), data.begin(), data.end());
        }
        else if (frame == timeline.streamFirst)
            leftOut("streamed sound in a format Phoenix cannot read (Nellymoser or Speex)");
    }

    static size_t adpcmCount(const std::vector<uint8_t>& data, int channels)
    {
        if (data.empty())
            return 0;
        const int bits = (data[0] >> 6) + 2;
        const size_t available = data.size() * 8 - 2;
        const size_t header = 22 * static_cast<size_t>(channels);
        const size_t packet = header + static_cast<size_t>(bits * channels) * 4095;
        size_t count = (available / packet) * 4096;
        const size_t rest = available % packet;
        if (rest >= header)
            count += 1 + (rest - header) / static_cast<size_t>(bits * channels);
        return count;
    }

    /// A definition tag: true when it was one.
    /// A SOUNDINFO record: how a sound starts, stops and loops.
    static void readSoundInfo(Reader& tag, SoundUse& use)
    {
        tag.align();
        tag.ub(2);
        use.stop = tag.ub(1) != 0;
        use.noMultiple = tag.ub(1) != 0;
        const bool envelope = tag.ub(1) != 0;
        const bool loops = tag.ub(1) != 0;
        const bool outPoint = tag.ub(1) != 0;
        const bool inPoint = tag.ub(1) != 0;
        if (inPoint)
            tag.u32();
        if (outPoint)
            tag.u32();
        if (loops)
            use.loops = tag.u16();
        if (envelope)
        {
            const int points = tag.u8();
            for (int i = 0; i < points && !tag.atEnd(); ++i)
            {
                tag.u32();
                tag.u16();
                tag.u16();
            }
        }
    }

    /// A button's sounds, one for each change of state, each kept on the
    /// state frame that plays it: rolling out on Up, over on Over,
    /// pressing on Down and releasing on Hit.
    void defineButtonSound(Reader& tag)
    {
        const auto found = _characters.find(tag.u16());
        if (found == _characters.end() || found->second.kind != Character::Kind::Button || !found->second.timeline)
            return;
        for (int state = 0; state < 4 && !tag.atEnd(); ++state)
        {
            SoundUse use;
            use.sound = tag.u16();
            if (use.sound == 0)
                continue;
            readSoundInfo(tag, use);
            found->second.timeline->sounds[state].push_back(use);
        }
    }

    bool define(Reader& tag, int code, size_t length)
    {
        switch (code)
        {
        case 17:
            defineButtonSound(tag);
            return true;
        case 2:
        case 22:
        case 32:
        case 83:
            defineShape(tag, code == 2 ? 1 : code == 22 ? 2 : code == 32 ? 3 : 4);
            return true;
        case 46:
        case 84:
            defineMorph(tag, code == 84);
            return true;
        case 39:
            defineSprite(tag, length);
            return true;
        case 7:
        case 34:
            defineButton(tag, code == 34);
            return true;
        case 8:
            _jpegTables = tag.bytes(length);
            return true;
        case 6:
        case 21:
        case 35:
        case 90:
            defineJpeg(tag, code, length);
            return true;
        case 20:
        case 36:
            defineLossless(tag, code == 36, length);
            return true;
        case 14:
            defineSound(tag, length);
            return true;
        case 10:
        case 48:
        case 75:
            defineFont(tag, code, length);
            return true;
        case 13:
        case 62:
        {
            const int id = tag.u16();
            const int nameLength = tag.u8();
            const std::vector<uint8_t> bytes = tag.bytes(static_cast<size_t>(nameLength));
            const std::string name(bytes.begin(), bytes.end());
            auto found = _characters.find(id);
            if (found != _characters.end())
                found->second.font.name = name.c_str();
            return true;
        }
        case 11:
        case 33:
            defineText(tag, code == 33);
            return true;
        case 91:
        {
            // DefineFont4, for text laid out by ActionScript: its name and
            // style are kept for text that names it; its outlines (CFF) are
            // not drawn.
            Character& c = _characters[tag.u16()];
            c.kind = Character::Kind::Font;
            const uint8_t flags = tag.u8();
            c.font.italic = flags & 0x02;
            c.font.bold = flags & 0x01;
            c.font.name = tag.string();
            return true;
        }
        case 37:
            defineEditText(tag);
            return true;
        case 60: // DefineVideoStream: its size and length; the pictures are
                 // not decoded.
        {
            Character& c = _characters[tag.u16()];
            c.kind = Character::Kind::Video;
            c.videoFrames = tag.u16();
            c.width = tag.u16();
            c.height = tag.u16();
            tag.u8();
            c.codec = tag.u8();
            return true;
        }
        case 61: // VideoFrame
            return true;
        case 78: // DefineScalingGrid
        {
            const int id = tag.u16();
            const Rect grid = readRect(tag);
            auto found = _characters.find(id);
            if (found != _characters.end())
            {
                found->second.scale9 = true;
                found->second.grid = grid;
            }
            return true;
        }
        case 56: // ExportAssets
        case 76: // SymbolClass
        {
            const int count = tag.u16();
            for (int i = 0; i < count && !tag.atEnd(); ++i)
            {
                const int id = tag.u16();
                const std::string name = tag.string();
                if (id != 0)
                    _exportNames[id] = name;
                else if (code == 76)
                    _rootClass = name; // the main timeline's class
            }
            return true;
        }
        default:
            return false;
        }
    }

    void defineShape(Reader& tag, int version)
    {
        const int id = tag.u16();
        const Rect bounds = readRect(tag);
        if (version == 4)
        {
            readRect(tag);
            tag.u8();
        }
        _characters[id].bounds = bounds;
        std::vector<std::vector<Fill>> fills{readFills(tag, version)};
        std::vector<std::vector<Line>> lines{readLines(tag, version)};
        tag.align();
        const int fillBits = static_cast<int>(tag.ub(4));
        const int lineBits = static_cast<int>(tag.ub(4));
        const std::vector<Record> records = readRecords(tag, fillBits, lineBits, version, &fills, &lines);
        Character& c = _characters[id];
        c.kind = Character::Kind::Shape;
        c.shape = buildShape(records, fills, lines);
    }

    void defineMorph(Reader& tag, bool two)
    {
        const int id = tag.u16();
        // The bounds of the shape at both ends.
        Rect bounds = readRect(tag);
        const Rect end = readRect(tag);
        bounds.left = std::min(bounds.left, end.left);
        bounds.right = std::max(bounds.right, end.right);
        bounds.top = std::min(bounds.top, end.top);
        bounds.bottom = std::max(bounds.bottom, end.bottom);
        _characters[id].bounds = bounds;
        if (two)
        {
            readRect(tag);
            readRect(tag);
            tag.u8();
        }
        const uint32_t offset = tag.u32();
        const size_t endEdges = tag.position() + offset;
        Character& c = _characters[id];
        c.kind = Character::Kind::Morph;
        MorphData& m = c.morph;
        size_t count = tag.u8();
        if (count == 0xFF)
            count = tag.u16();
        for (size_t i = 0; i < count && !tag.atEnd(); ++i)
        {
            Fill start, end;
            start.type = end.type = tag.u8();
            if (start.type == 0)
            {
                start.color = readRgb(tag, true);
                end.color = readRgb(tag, true);
            }
            else if (start.type == 0x10 || start.type == 0x12 || start.type == 0x13)
            {
                start.gradient.matrix = readMatrix(tag);
                end.gradient.matrix = readMatrix(tag);
                const int stops = tag.u8() & 0x0F;
                for (int s = 0; s < stops; ++s)
                {
                    Gradient::Stop a, b;
                    a.ratio = tag.u8();
                    a.color = readRgb(tag, true);
                    b.ratio = tag.u8();
                    b.color = readRgb(tag, true);
                    start.gradient.stops.push_back(a);
                    end.gradient.stops.push_back(b);
                }
                if (start.type == 0x13)
                {
                    start.gradient.focal = tag.fixed8();
                    end.gradient.focal = tag.fixed8();
                }
            }
            else
            {
                start.bitmap = end.bitmap = tag.u16();
                start.matrix = readMatrix(tag);
                end.matrix = readMatrix(tag);
            }
            m.startFills.push_back(start);
            m.endFills.push_back(end);
        }
        count = tag.u8();
        if (count == 0xFF)
            count = tag.u16();
        for (size_t i = 0; i < count && !tag.atEnd(); ++i)
        {
            Line start, end;
            start.width = tag.u16();
            end.width = tag.u16();
            if (two)
            {
                tag.align();
                start.startCap = static_cast<int>(tag.ub(2));
                start.join = static_cast<int>(tag.ub(2));
                const bool hasFill = tag.ub(1) != 0;
                start.noHScale = tag.ub(1) != 0;
                start.noVScale = tag.ub(1) != 0;
                tag.ub(1);
                tag.ub(5);
                tag.ub(1);
                start.endCap = static_cast<int>(tag.ub(2));
                tag.align();
                if (start.join == 2)
                    start.miter = tag.fixed8();
                if (hasFill)
                {
                    // A filled stroke: its fill's start and end colors when
                    // solid, else black.
                    const int type = tag.u8();
                    if (type == 0)
                    {
                        start.color = readRgb(tag, true);
                        end.color = readRgb(tag, true);
                    }
                    else
                    {
                        leftOut("morph shape strokes with gradient fills");
                        return;
                    }
                }
                else
                {
                    start.color = readRgb(tag, true);
                    end.color = readRgb(tag, true);
                }
                end.startCap = start.startCap;
                end.endCap = start.endCap;
                end.join = start.join;
                end.miter = start.miter;
            }
            else
            {
                start.color = readRgb(tag, true);
                end.color = readRgb(tag, true);
            }
            m.startLines.push_back(start);
            m.endLines.push_back(end);
        }
        tag.align();
        int fillBits = static_cast<int>(tag.ub(4));
        int lineBits = static_cast<int>(tag.ub(4));
        m.start = readRecords(tag, fillBits, lineBits, 3, nullptr, nullptr);
        tag.seek(endEdges);
        fillBits = static_cast<int>(tag.ub(4));
        lineBits = static_cast<int>(tag.ub(4));
        m.end = readRecords(tag, fillBits, lineBits, 3, nullptr, nullptr);
    }

    void defineSprite(Reader& tag, size_t length)
    {
        const int id = tag.u16();
        tag.u16(); // frame count
        // A sprite defined again inside itself, or nested past reason, is
        // left out.
        if (_reading.count(id) || static_cast<int>(_reading.size()) >= kMaxNesting)
        {
            leftOut("sprites nested too deeply");
            return;
        }
        std::shared_ptr<TimelineData> timeline = std::make_shared<TimelineData>();
        Character& c = _characters[id];
        c.kind = Character::Kind::Sprite;
        c.timeline = timeline;
        // The sprite's tags, read as a timeline of their own.
        (void)length;
        Reader sprite(tag.data() + tag.position(), tag.left());
        _reading.insert(id);
        readTags(sprite, *timeline, false);
        _reading.erase(id);
    }

    void defineButton(Reader& tag, bool two)
    {
        const int id = tag.u16();
        int actionOffset = 0;
        if (two)
        {
            tag.u8();
            actionOffset = tag.u16();
        }
        if (_reading.count(id))
            return;
        Character& c = _characters[id];
        c.kind = Character::Kind::Button;
        c.buttonActions.clear();
        c.timeline = std::make_shared<TimelineData>();
        c.timeline->frames.resize(4);
        c.timeline->frameCount = 4;
        while (!tag.atEnd())
        {
            tag.align();
            const uint8_t flags = tag.u8();
            if (flags == 0)
                break;
            Placement p;
            p.character = tag.u16();
            const int depth = tag.u16();
            p.matrix = readMatrix(tag);
            if (two)
            {
                p.color = readColorTransform(tag, true);
                if (flags & 0x10)
                    p.filters = readFilters(tag);
                if (flags & 0x20)
                    p.blend = tag.u8();
            }
            for (int state = 0; state < 4; ++state)
                if (flags & (1 << state))
                {
                    p.version = ++_version_counter;
                    c.timeline->frames[static_cast<size_t>(state)][depth] = p;
                }
        }
        // Its handlers: DefineButton's actions run on release; DefineButton2
        // has conditions, each with its own.
        if (!two && tag.left() > 0)
        {
            CompiledAction action;
            action.kind = CompiledAction::Kind::Button;
            action.events = 0x0008; // OverDownToOverUp: release
            action.actions = tag.bytes(tag.left());
            c.buttonActions.push_back(std::move(action));
            ++_scripts;
        }
        while (two && actionOffset != 0 && tag.left() >= 4)
        {
            const size_t size = tag.u16();
            CompiledAction action;
            action.kind = CompiledAction::Kind::Button;
            action.events = tag.u16();
            action.actions = tag.bytes(size == 0 ? tag.left() : std::min(size - std::min<size_t>(size, 4), tag.left()));
            c.buttonActions.push_back(std::move(action));
            ++_scripts;
            if (size == 0)
                break;
        }
    }

    void defineJpeg(Reader& tag, int code, size_t length)
    {
        const int id = tag.u16();
        uint32_t alphaOffset = 0;
        if (code == 35 || code == 90)
            alphaOffset = tag.u32();
        if (code == 90)
            tag.u16(); // deblocking
        const size_t imageLength = alphaOffset > 0 ? alphaOffset : tag.left();
        std::vector<uint8_t> image = tag.bytes(std::min(imageLength, tag.left()));
        std::vector<uint8_t> alpha;
        if (alphaOffset > 0)
            alpha = tag.bytes(tag.left());
        (void)length;
        // Old encoders put an end and a start of image before the image.
        if (image.size() > 4 && image[0] == 0xFF && image[1] == 0xD9 && image[2] == 0xFF && image[3] == 0xD8)
            image.erase(image.begin(), image.begin() + 4);
        if (code == 6 && !_jpegTables.empty())
        {
            // Tables and image as one stream: the tables without their end,
            // the image without its start.
            std::vector<uint8_t> joined = _jpegTables;
            if (joined.size() >= 2 && joined[joined.size() - 2] == 0xFF && joined.back() == 0xD9)
                joined.resize(joined.size() - 2);
            if (joined.size() > 4 && joined[0] == 0xFF && joined[1] == 0xD9)
                joined.erase(joined.begin(), joined.begin() + 4);
            size_t from = image.size() >= 2 && image[0] == 0xFF && image[1] == 0xD8 ? 2 : 0;
            joined.insert(joined.end(), image.begin() + static_cast<std::ptrdiff_t>(from), image.end());
            image = joined;
        }
        Character& c = _characters[id];
        c.kind = Character::Kind::Bitmap;
        const bool jpeg = image.size() > 2 && image[0] == 0xFF && image[1] == 0xD8;
        if (jpeg && alpha.empty())
        {
            c.jpeg = true;
            c.data = image;
            jpegSize(image, c.width, c.height);
            return;
        }
        Image decoded;
        if (!imageDecoder() || !imageDecoder()(image, decoded) || decoded.width <= 0)
        {
            if (jpeg)
            {
                // Without a decoder, the JPEG keeps no transparency.
                c.jpeg = true;
                c.data = image;
                jpegSize(image, c.width, c.height);
                leftOut("transparency of JPEG bitmaps");
            }
            else
            {
                leftOut("PNG and GIF bitmaps");
                c.data.clear();
            }
            return;
        }
        if (jpeg && !alpha.empty())
        {
            const std::vector<uint8_t> mask =
                inflate(alpha, static_cast<size_t>(decoded.width) * static_cast<size_t>(decoded.height));
            for (size_t i = 0; i < decoded.pixels.size() && i < mask.size(); ++i)
            {
                const uint32_t p = decoded.pixels[i];
                const uint32_t a = mask[i];
                const uint32_t r = ((p >> 16) & 0xFF) * a / 255, g = ((p >> 8) & 0xFF) * a / 255, b = (p & 0xFF) * a / 255;
                decoded.pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        c.width = decoded.width;
        c.height = decoded.height;
        c.data = fla::encodeLosslessBitmap(decoded.width, decoded.height, decoded.pixels);
    }

    static void jpegSize(const std::vector<uint8_t>& jpeg, int& width, int& height)
    {
        size_t at = 2;
        while (at + 9 < jpeg.size())
        {
            if (jpeg[at] != 0xFF)
            {
                ++at;
                continue;
            }
            const uint8_t marker = jpeg[at + 1];
            if (marker == 0xD8 || marker == 0xD9 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0xFF)
            {
                at += marker == 0xFF ? 1 : 2;
                continue;
            }
            const size_t size = (jpeg[at + 2] << 8) | jpeg[at + 3];
            if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF))
            {
                height = (jpeg[at + 5] << 8) | jpeg[at + 6];
                width = (jpeg[at + 7] << 8) | jpeg[at + 8];
                return;
            }
            at += 2 + size;
        }
    }

    void defineLossless(Reader& tag, bool alpha, size_t length)
    {
        const int id = tag.u16();
        const int format = tag.u8();
        const int width = tag.u16();
        const int height = tag.u16();
        int tableSize = 0;
        if (format == 3)
            tableSize = tag.u8() + 1;
        (void)length;
        const size_t w = static_cast<size_t>(width), h = static_cast<size_t>(height);
        Character& c = _characters[id];
        c.kind = Character::Kind::Bitmap;
        // Larger than any player draws: a damaged header.
        if (width > 16383 || height > 16383 || w * h > (size_t(1) << 28))
        {
            leftOut("bitmaps too large to read");
            c.data.clear();
            return;
        }
        const std::vector<uint8_t> packed = tag.bytes(tag.left());
        std::vector<uint32_t> pixels(w * h, 0);
        c.width = width;
        c.height = height;
        if (format == 3)
        {
            const size_t entry = alpha ? 4 : 3;
            const size_t row = (w + 3) & ~static_cast<size_t>(3);
            const std::vector<uint8_t> data = inflate(packed, static_cast<size_t>(tableSize) * entry + row * h);
            std::vector<uint32_t> table(static_cast<size_t>(tableSize), 0);
            for (size_t i = 0; i < table.size() && (i + 1) * entry <= data.size(); ++i)
            {
                const uint8_t* e = &data[i * entry];
                table[i] = (static_cast<uint32_t>(alpha ? e[3] : 255) << 24) | (e[0] << 16) | (e[1] << 8) | e[2];
            }
            const size_t base = static_cast<size_t>(tableSize) * entry;
            for (size_t y = 0; y < h; ++y)
                for (size_t x = 0; x < w; ++x)
                {
                    const size_t at = base + y * row + x;
                    if (at < data.size() && data[at] < table.size())
                        pixels[y * w + x] = table[data[at]];
                }
        }
        else if (format == 4)
        {
            const size_t row = (w * 2 + 3) & ~static_cast<size_t>(3);
            const std::vector<uint8_t> data = inflate(packed, row * h);
            for (size_t y = 0; y < h; ++y)
                for (size_t x = 0; x < w; ++x)
                {
                    const size_t at = y * row + x * 2;
                    if (at + 1 >= data.size())
                        continue;
                    const uint16_t v = static_cast<uint16_t>((data[at] << 8) | data[at + 1]);
                    const uint32_t r = ((v >> 10) & 0x1F) * 255 / 31, g = ((v >> 5) & 0x1F) * 255 / 31, b = (v & 0x1F) * 255 / 31;
                    pixels[y * w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
                }
        }
        else if (format == 5)
        {
            const std::vector<uint8_t> data = inflate(packed, w * h * 4);
            for (size_t i = 0; i < w * h && i * 4 + 3 < data.size(); ++i)
            {
                const uint8_t* p = &data[i * 4];
                const uint32_t a = alpha ? p[0] : 255;
                pixels[i] = (a << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            }
        }
        c.data = fla::encodeLosslessBitmap(width, height, pixels);
    }

    void defineSound(Reader& tag, size_t length)
    {
        const int id = tag.u16();
        tag.align();
        const int codec = static_cast<int>(tag.ub(4));
        const int rate = static_cast<int>(tag.ub(2));
        const bool sixteen = tag.ub(1) != 0;
        const bool stereo = tag.ub(1) != 0;
        const uint32_t count = tag.u32();
        (void)length;
        Character& c = _characters[id];
        c.kind = Character::Kind::Sound;
        c.sampleCount = count;
        static const char* rates[4] = {"5kHz", "11kHz", "22kHz", "44kHz"};
        const std::string channels = stereo ? " Stereo" : " Mono";
        if (codec == 2)
        {
            tag.u16(); // seek samples
            c.data = tag.bytes(tag.left());
            c.soundFormat = std::string(rates[rate]) + " 16bit" + channels;
        }
        else if (codec == 1)
        {
            const std::vector<uint8_t> data = tag.bytes(tag.left());
            const std::vector<int16_t> pcm = swf::decodeAdpcm(data, stereo ? 2 : 1, count);
            for (int16_t sample : pcm)
            {
                c.data.push_back(static_cast<uint8_t>(sample & 0xFF));
                c.data.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
            }
            c.soundFormat = std::string(rates[rate]) + " 16bit" + channels;
        }
        else if (codec == 0 || codec == 3)
        {
            c.data = tag.bytes(tag.left());
            c.soundFormat = std::string(rates[rate]) + (sixteen ? " 16bit" : " 8bit") + channels;
        }
        else
        {
            leftOut("sounds in a format Phoenix cannot read (Nellymoser or Speex)");
            c.data.clear();
        }
    }

    void defineFont(Reader& tag, int code, size_t length)
    {
        const int id = tag.u16();
        Character& c = _characters[id];
        c.kind = Character::Kind::Font;
        Font& font = c.font;
        if (code == 10)
        {
            const size_t base = tag.position();
            const uint16_t first = tag.u16();
            std::vector<uint32_t> offsets{first};
            for (size_t i = 1; i < first / 2u; ++i)
                offsets.push_back(tag.u16());
            size_t records = 0;
            for (size_t i = 0; i < offsets.size(); ++i)
            {
                if (records > kMaxFontRecords)
                {
                    font.glyphs.emplace_back();
                    continue;
                }
                tag.seek(base + offsets[i]);
                tag.align();
                const int fillBits = static_cast<int>(tag.ub(4));
                const int lineBits = static_cast<int>(tag.ub(4));
                font.glyphs.push_back(readRecords(tag, fillBits, lineBits, 1, nullptr, nullptr));
                records += font.glyphs.back().size();
            }
            (void)length;
            return;
        }
        const uint8_t flags = tag.u8();
        const bool wideOffsets = flags & 0x08;
        font.italic = flags & 0x02;
        font.bold = flags & 0x01;
        tag.u8(); // language
        const int nameLength = tag.u8();
        const std::vector<uint8_t> name = tag.bytes(static_cast<size_t>(nameLength));
        font.name = std::string(name.begin(), name.end()).c_str();
        font.em = code == 75 ? 20480.0 : 1024.0;
        const int glyphs = tag.u16();
        const size_t base = tag.position();
        std::vector<uint32_t> offsets;
        for (int i = 0; i < glyphs; ++i)
            offsets.push_back(wideOffsets ? tag.u32() : tag.u16());
        size_t records = 0;
        for (int i = 0; i < glyphs; ++i)
        {
            // Offsets can point many glyphs at the same long run of records;
            // past a budget the rest are left empty.
            if (records > kMaxFontRecords)
            {
                font.glyphs.emplace_back();
                continue;
            }
            tag.seek(base + offsets[static_cast<size_t>(i)]);
            tag.align();
            const int fillBits = static_cast<int>(tag.ub(4));
            const int lineBits = static_cast<int>(tag.ub(4));
            font.glyphs.push_back(readRecords(tag, fillBits, lineBits, 1, nullptr, nullptr));
            records += font.glyphs.back().size();
        }
        if (records > kMaxFontRecords)
            leftOut("font outlines past the size limit");
    }

    void defineText(Reader& tag, bool two)
    {
        const int id = tag.u16();
        _characters[id].bounds = readRect(tag);
        const Matrix matrix = readMatrix(tag);
        const int glyphBits = tag.u8();
        const int advanceBits = tag.u8();
        Character& c = _characters[id];
        c.kind = Character::Kind::Shape;
        ShapeData& shape = c.shape;
        const Font* font = nullptr;
        Rgba color;
        double x = 0, y = 0, height = 240;
        std::map<uint32_t, int> fillOf;
        while (!tag.atEnd())
        {
            tag.align();
            const uint8_t flags = tag.u8();
            if (flags == 0)
                break;
            if (flags & 0x08)
            {
                const auto found = _characters.find(tag.u16());
                font = found != _characters.end() && found->second.kind == Character::Kind::Font ? &found->second.font
                                                                                                    : nullptr;
            }
            if (flags & 0x04)
                color = readRgb(tag, two);
            if (flags & 0x01)
                x = tag.s16();
            if (flags & 0x02)
                y = tag.s16();
            if (flags & 0x08)
                height = tag.u16();
            const int count = tag.u8();
            tag.align();
            const uint32_t key = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
            if (!fillOf.count(key))
            {
                Fill fill;
                fill.color = color;
                shape.fills.push_back(fill);
                fillOf[key] = static_cast<int>(shape.fills.size());
            }
            const int fill = fillOf[key];
            for (int g = 0; g < count; ++g)
            {
                const uint32_t index = tag.ub(glyphBits);
                const int advance = tag.sb(advanceBits);
                if (font && index < font->glyphs.size())
                {
                    const double scale = height / font->em;
                    Matrix glyph;
                    glyph.a = glyph.d = scale;
                    glyph.tx = x;
                    glyph.ty = y;
                    const Matrix place = matrix.times(glyph);
                    ShapeData outline = buildShape(font->glyphs[index], {{Fill()}}, {{}});
                    for (Run& run : outline.runs)
                    {
                        run.fill0 = run.fill0 ? fill : 0;
                        run.fill1 = run.fill1 ? fill : 0;
                        run.line = 0;
                        for (Command& command : run.commands)
                        {
                            place.apply(command.x, command.y, command.x, command.y);
                            place.apply(command.cx, command.cy, command.cx, command.cy);
                        }
                        shape.runs.push_back(run);
                    }
                }
                x += advance;
            }
        }
    }

    void defineEditText(Reader& tag)
    {
        const int id = tag.u16();
        Character& c = _characters[id];
        c.kind = Character::Kind::EditText;
        EditText& e = c.edit;
        e.bounds = readRect(tag);
        tag.align();
        const bool hasText = tag.ub(1) != 0;
        e.wordWrap = tag.ub(1) != 0;
        e.multiline = tag.ub(1) != 0;
        e.password = tag.ub(1) != 0;
        e.readOnly = tag.ub(1) != 0;
        const bool hasColor = tag.ub(1) != 0;
        const bool hasMaxLength = tag.ub(1) != 0;
        const bool hasFont = tag.ub(1) != 0;
        const bool hasFontClass = tag.ub(1) != 0;
        tag.ub(1); // auto size
        const bool hasLayout = tag.ub(1) != 0;
        e.selectable = tag.ub(1) == 0;
        e.border = tag.ub(1) != 0;
        tag.ub(1); // was static
        e.html = tag.ub(1) != 0;
        tag.ub(1); // use outlines
        tag.align();
        if (hasFont)
            e.font = tag.u16();
        if (hasFontClass)
            tag.string();
        if (hasFont || hasFontClass)
            e.height = tag.u16();
        if (hasColor)
        {
            e.color = readRgb(tag, true);
            e.hasColor = true;
        }
        if (hasMaxLength)
            e.maxLength = tag.u16();
        if (hasLayout)
        {
            e.align = tag.u8();
            tag.u16();
            tag.u16();
            tag.u16();
            tag.s16();
        }
        e.variable = tag.string();
        if (hasText)
            e.text = tag.string();
    }

    // -- Output -------------------------------------------------------------

    std::string uniqueName(std::string name)
    {
        for (char& ch : name)
            if (std::strchr("\\:*?\"<>|", ch))
                ch = '_';
        std::string candidate = name;
        for (int n = 2; _names.count(candidate); ++n)
            candidate = name + " " + std::to_string(n);
        _names.insert(candidate);
        const size_t slash = candidate.rfind('/');
        if (slash != std::string::npos)
            _folders.insert(candidate.substr(0, slash));
        return candidate;
    }

    std::string nameOf(int id)
    {
        Character& c = _characters[id];
        if (!c.name.empty())
            return c.name;
        const auto exported = _exportNames.find(id);
        if (exported != _exportNames.end() && !exported->second.empty())
        {
            c.linkage = exported->second;
            c.name = uniqueName(exported->second);
            return c.name;
        }
        const char* folder = "Shapes/Shape ";
        switch (c.kind)
        {
        case Character::Kind::Sprite: folder = "Movie Clips/Movie Clip "; break;
        case Character::Kind::Button: folder = "Buttons/Button "; break;
        case Character::Kind::Bitmap: folder = "Bitmaps/Bitmap "; break;
        case Character::Kind::Sound: folder = "Sounds/Sound "; break;
        case Character::Kind::Video: folder = "Videos/Video "; break;
        default: break;
        }
        c.name = uniqueName(folder + std::to_string(id));
        return c.name;
    }

    void countUses(const TimelineData& timeline)
    {
        std::map<int, long> last;
        for (const auto& frame : timeline.frames)
            for (const auto& [depth, p] : frame)
            {
                auto found = last.find(depth);
                if (found != last.end() && found->second == p.version)
                    continue;
                last[depth] = p.version;
                ++_characters[p.character].uses;
            }
    }

    void writeMatrix(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const Matrix& m)
    {
        if (m.identity())
            return;
        tinyxml2::XMLElement* holder = doc.NewElement("matrix");
        tinyxml2::XMLElement* matrix = doc.NewElement("Matrix");
        if (m.a != 1)
            matrix->SetAttribute("a", number(m.a, 8).c_str());
        if (m.b != 0)
            matrix->SetAttribute("b", number(m.b, 8).c_str());
        if (m.c != 0)
            matrix->SetAttribute("c", number(m.c, 8).c_str());
        if (m.d != 1)
            matrix->SetAttribute("d", number(m.d, 8).c_str());
        if (m.tx != 0)
            matrix->SetAttribute("tx", number(m.tx / 20.0, 3).c_str());
        if (m.ty != 0)
            matrix->SetAttribute("ty", number(m.ty / 20.0, 3).c_str());
        holder->InsertEndChild(matrix);
        parent->InsertEndChild(holder);
    }

    void writeColorFill(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const Rgba& color)
    {
        tinyxml2::XMLElement* solid = doc.NewElement("SolidColor");
        solid->SetAttribute("color", hexColor(color).c_str());
        if (color.a != 255)
            solid->SetAttribute("alpha", number(color.a / 255.0, 4).c_str());
        parent->InsertEndChild(solid);
    }

    /// A fill's element, placed by \a place.
    void writeFill(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const Fill& fill, const Matrix& place)
    {
        if (fill.type == 0x10 || fill.type == 0x12 || fill.type == 0x13)
        {
            tinyxml2::XMLElement* g = doc.NewElement(fill.type == 0x10 ? "LinearGradient" : "RadialGradient");
            if (fill.gradient.spread == 1)
                g->SetAttribute("spreadMethod", "reflect");
            else if (fill.gradient.spread == 2)
                g->SetAttribute("spreadMethod", "repeat");
            if (fill.gradient.interpolation == 1)
                g->SetAttribute("interpolationMethod", "linearRGB");
            if (fill.type == 0x13 && fill.gradient.focal != 0)
                g->SetAttribute("focalPointRatio", number(fill.gradient.focal, 4).c_str());
            writeMatrix(doc, g, place.times(fill.gradient.matrix));
            for (const Gradient::Stop& stop : fill.gradient.stops)
            {
                tinyxml2::XMLElement* entry = doc.NewElement("GradientEntry");
                entry->SetAttribute("color", hexColor(stop.color).c_str());
                if (stop.color.a != 255)
                    entry->SetAttribute("alpha", number(stop.color.a / 255.0, 4).c_str());
                entry->SetAttribute("ratio", number(stop.ratio / 255.0, 4).c_str());
                g->InsertEndChild(entry);
            }
            parent->InsertEndChild(g);
            return;
        }
        if (fill.type >= 0x40 && fill.type <= 0x43)
        {
            const auto found = _characters.find(fill.bitmap);
            if (found != _characters.end() && found->second.kind == Character::Kind::Bitmap && !found->second.data.empty())
            {
                tinyxml2::XMLElement* b = doc.NewElement("BitmapFill");
                b->SetAttribute("bitmapPath", nameOf(fill.bitmap).c_str());
                if (fill.type == 0x41 || fill.type == 0x43)
                    b->SetAttribute("bitmapIsClipped", "true");
                writeMatrix(doc, b, place.times(fill.matrix));
                parent->InsertEndChild(b);
                return;
            }
            writeColorFill(doc, parent, Rgba{204, 204, 204, 255});
            return;
        }
        writeColorFill(doc, parent, fill.color);
    }

    std::string edgesOf(const Run& run, const Matrix& place)
    {
        std::string text;
        for (const Command& command : run.commands)
        {
            double x, y;
            place.apply(command.x, command.y, x, y);
            if (command.kind == 'Q')
            {
                double cx, cy;
                place.apply(command.cx, command.cy, cx, cy);
                text += "[" + number(cx, 2) + " " + number(cy, 2) + " " + number(x, 2) + " " + number(y, 2);
            }
            else
                text += (command.kind == 'M' ? "!" : "|") + number(x, 2) + " " + number(y, 2);
        }
        return text;
    }

    /// A DOMShape of \a shape, its points taken through \a place; a group
    /// of them, one for each style chunk, bottom first, when it has
    /// several, so they stack as Flash draws them.
    tinyxml2::XMLElement* shapeElement(tinyxml2::XMLDocument& doc, const ShapeData& shape, const Matrix& place)
    {
        const std::vector<ShapeData> chunks = chunksOf(shape);
        if (chunks.size() == 1)
            return chunkElement(doc, chunks.front(), place);
        tinyxml2::XMLElement* group = doc.NewElement("DOMGroup");
        tinyxml2::XMLElement* members = doc.NewElement("members");
        for (const ShapeData& chunk : chunks)
            members->InsertEndChild(chunkElement(doc, chunk, place));
        group->InsertEndChild(members);
        return group;
    }

    tinyxml2::XMLElement* chunkElement(tinyxml2::XMLDocument& doc, const ShapeData& shape, const Matrix& place)
    {
        tinyxml2::XMLElement* element = doc.NewElement("DOMShape");
        const double scale = std::sqrt(std::fabs(place.a * place.d - place.b * place.c));
        if (!shape.fills.empty())
        {
            tinyxml2::XMLElement* fills = doc.NewElement("fills");
            for (size_t i = 0; i < shape.fills.size(); ++i)
            {
                tinyxml2::XMLElement* style = doc.NewElement("FillStyle");
                style->SetAttribute("index", static_cast<int>(i + 1));
                writeFill(doc, style, shape.fills[i], place);
                fills->InsertEndChild(style);
            }
            element->InsertEndChild(fills);
        }
        if (!shape.lines.empty())
        {
            tinyxml2::XMLElement* strokes = doc.NewElement("strokes");
            for (size_t i = 0; i < shape.lines.size(); ++i)
            {
                const Line& line = shape.lines[i];
                tinyxml2::XMLElement* style = doc.NewElement("StrokeStyle");
                style->SetAttribute("index", static_cast<int>(i + 1));
                tinyxml2::XMLElement* stroke = doc.NewElement("SolidStroke");
                stroke->SetAttribute("scaleMode", line.noHScale && line.noVScale ? "none"
                        : line.noHScale                                         ? "vertical"
                        : line.noVScale                                         ? "horizontal"
                                                                                : "normal");
                stroke->SetAttribute("weight", number(std::max(0.05, line.width * scale / 20.0), 3).c_str());
                if (line.startCap == 1)
                    stroke->SetAttribute("caps", "none");
                else if (line.startCap == 2)
                    stroke->SetAttribute("caps", "square");
                if (line.join == 1)
                    stroke->SetAttribute("joints", "bevel");
                else if (line.join == 2)
                {
                    stroke->SetAttribute("joints", "miter");
                    stroke->SetAttribute("miterLimit", number(line.miter, 3).c_str());
                }
                tinyxml2::XMLElement* fill = doc.NewElement("fill");
                if (line.hasFill)
                    writeFill(doc, fill, line.fill, place);
                else
                    writeColorFill(doc, fill, line.color);
                stroke->InsertEndChild(fill);
                style->InsertEndChild(stroke);
                strokes->InsertEndChild(style);
            }
            element->InsertEndChild(strokes);
        }
        tinyxml2::XMLElement* edges = doc.NewElement("edges");
        for (const Run& run : shape.runs)
        {
            tinyxml2::XMLElement* edge = doc.NewElement("Edge");
            if (run.fill0)
                edge->SetAttribute("fillStyle0", run.fill0);
            if (run.fill1)
                edge->SetAttribute("fillStyle1", run.fill1);
            if (run.line)
                edge->SetAttribute("strokeStyle", run.line);
            edge->SetAttribute("edges", edgesOf(run, place).c_str());
            edges->InsertEndChild(edge);
        }
        element->InsertEndChild(edges);
        return element;
    }

    static Rgba mix(const Rgba& a, const Rgba& b, double t)
    {
        const auto m = [t](uint8_t x, uint8_t y) {
            return static_cast<uint8_t>(std::lround(x + (y - x) * t));
        };
        return Rgba{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
    }

    static Matrix mix(const Matrix& a, const Matrix& b, double t)
    {
        Matrix m;
        m.a = a.a + (b.a - a.a) * t;
        m.b = a.b + (b.b - a.b) * t;
        m.c = a.c + (b.c - a.c) * t;
        m.d = a.d + (b.d - a.d) * t;
        m.tx = a.tx + (b.tx - a.tx) * t;
        m.ty = a.ty + (b.ty - a.ty) * t;
        return m;
    }

    /// A morph shape as it is at \a ratio (0 to 65535).
    ShapeData morphAt(const MorphData& m, int ratio)
    {
        const double t = ratio / 65535.0;
        std::vector<Fill> fills;
        for (size_t i = 0; i < m.startFills.size() && i < m.endFills.size(); ++i)
        {
            Fill f = m.startFills[i];
            const Fill& e = m.endFills[i];
            f.color = mix(f.color, e.color, t);
            f.matrix = mix(f.matrix, e.matrix, t);
            f.gradient.matrix = mix(f.gradient.matrix, e.gradient.matrix, t);
            f.gradient.focal += (e.gradient.focal - f.gradient.focal) * t;
            for (size_t s = 0; s < f.gradient.stops.size() && s < e.gradient.stops.size(); ++s)
            {
                f.gradient.stops[s].color = mix(f.gradient.stops[s].color, e.gradient.stops[s].color, t);
                f.gradient.stops[s].ratio = static_cast<int>(
                    std::lround(f.gradient.stops[s].ratio + (e.gradient.stops[s].ratio - f.gradient.stops[s].ratio) * t));
            }
            fills.push_back(f);
        }
        std::vector<Line> lines;
        for (size_t i = 0; i < m.startLines.size() && i < m.endLines.size(); ++i)
        {
            Line l = m.startLines[i];
            l.width += (m.endLines[i].width - l.width) * t;
            l.color = mix(l.color, m.endLines[i].color, t);
            lines.push_back(l);
        }
        // The start records with each position and edge moved toward the
        // end's.
        std::vector<Record> records;
        size_t e = 0;
        double sx = 0, sy = 0, ex = 0, ey = 0;
        for (const Record& s : m.start)
        {
            Record r = s;
            if (!s.edge)
            {
                if (s.move)
                {
                    sx = s.x;
                    sy = s.y;
                    while (e < m.end.size() && !m.end[e].edge && !m.end[e].move)
                        ++e;
                    if (e < m.end.size() && !m.end[e].edge)
                    {
                        ex = m.end[e].x;
                        ey = m.end[e].y;
                        ++e;
                    }
                    r.x = sx + (ex - sx) * t;
                    r.y = sy + (ey - sy) * t;
                }
                records.push_back(r);
                continue;
            }
            while (e < m.end.size() && !m.end[e].edge)
            {
                if (m.end[e].move)
                {
                    ex = m.end[e].x;
                    ey = m.end[e].y;
                }
                ++e;
            }
            Record end = e < m.end.size() ? m.end[e++] : s;
            // A straight edge against a curve becomes a curve through its
            // middle.
            const auto asCurve = [](Record& x) {
                if (x.curve)
                    return;
                x.curve = true;
                x.cdx = x.adx / 2;
                x.cdy = x.ady / 2;
                x.adx -= x.cdx;
                x.ady -= x.cdy;
            };
            Record start = s;
            if (start.curve != end.curve)
            {
                asCurve(start);
                asCurve(end);
            }
            r = start;
            r.cdx = start.cdx + (end.cdx - start.cdx) * t;
            r.cdy = start.cdy + (end.cdy - start.cdy) * t;
            r.adx = start.adx + (end.adx - start.adx) * t;
            r.ady = start.ady + (end.ady - start.ady) * t;
            sx += start.cdx + start.adx;
            sy += start.cdy + start.ady;
            ex += end.cdx + end.adx;
            ey += end.cdy + end.ady;
            records.push_back(r);
        }
        return buildShape(records, {fills}, {lines});
    }

    void writeColor(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const ColorTransform& cx)
    {
        if (cx.identity())
            return;
        tinyxml2::XMLElement* holder = doc.NewElement("color");
        tinyxml2::XMLElement* color = doc.NewElement("Color");
        const char* muls[4] = {"redMultiplier", "greenMultiplier", "blueMultiplier", "alphaMultiplier"};
        const char* adds[4] = {"redOffset", "greenOffset", "blueOffset", "alphaOffset"};
        for (int i = 0; i < 4; ++i)
        {
            if (cx.mul[i] != 1.0)
                color->SetAttribute(muls[i], number(cx.mul[i], 4).c_str());
            if (cx.add[i] != 0)
                color->SetAttribute(adds[i], cx.add[i]);
        }
        holder->InsertEndChild(color);
        parent->InsertEndChild(holder);
    }

    tinyxml2::XMLElement* editTextElement(tinyxml2::XMLDocument& doc, const Character& c, const Placement& p)
    {
        const EditText& e = c.edit;
        tinyxml2::XMLElement* text = doc.NewElement(e.readOnly ? "DOMDynamicText" : "DOMInputText");
        if (!p.name.empty())
            text->SetAttribute("instanceName", p.name.c_str());
        const double width = std::max(1.0, (e.bounds.right - e.bounds.left) / 20.0 - 4.0);
        const double height = std::max(1.0, (e.bounds.bottom - e.bounds.top) / 20.0 - 4.0);
        text->SetAttribute("width", number(width).c_str());
        text->SetAttribute("height", number(height).c_str());
        if (e.password)
            text->SetAttribute("lineType", "password");
        else if (e.multiline)
            text->SetAttribute("lineType", e.wordWrap ? "multiline" : "multiline no wrap");
        if (!e.selectable)
            text->SetAttribute("isSelectable", "false");
        if (!e.variable.empty())
            text->SetAttribute("variableName", e.variable.c_str());
        if (e.border)
            text->SetAttribute("border", "true");
        if (e.maxLength > 0)
            text->SetAttribute("maxCharacters", e.maxLength);
        Matrix offset;
        offset.tx = e.bounds.left + 40;
        offset.ty = e.bounds.top + 40;
        writeMatrix(doc, text, p.matrix.times(offset));
        writeColor(doc, text, p.color);
        std::string characters = e.text;
        if (e.html)
        {
            // The words of HTML text, its paragraphs and breaks as returns.
            std::string plain;
            bool inTag = false;
            std::string tagName;
            for (size_t i = 0; i < characters.size(); ++i)
            {
                const char ch = characters[i];
                if (ch == '<')
                {
                    inTag = true;
                    tagName.clear();
                    continue;
                }
                if (inTag)
                {
                    if (ch == '>')
                    {
                        inTag = false;
                        std::string lower = tagName;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        if ((lower.rfind("br", 0) == 0 || lower == "/p") && !plain.empty())
                            plain += '\r';
                    }
                    else
                        tagName += ch;
                    continue;
                }
                plain += ch;
            }
            while (!plain.empty() && plain.back() == '\r')
                plain.pop_back();
            const std::pair<const char*, const char*> entities[] = {
                {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&nbsp;", " "}, {"&amp;", "&"}};
            for (const auto& [from, to] : entities)
                for (size_t at = plain.find(from); at != std::string::npos; at = plain.find(from, at + 1))
                    plain.replace(at, std::strlen(from), to);
            characters = plain;
        }
        tinyxml2::XMLElement* runs = doc.NewElement("textRuns");
        tinyxml2::XMLElement* run = doc.NewElement("DOMTextRun");
        tinyxml2::XMLElement* chars = doc.NewElement("characters");
        chars->SetText(characters.c_str());
        run->InsertEndChild(chars);
        tinyxml2::XMLElement* attrsHolder = doc.NewElement("textAttrs");
        tinyxml2::XMLElement* attrs = doc.NewElement("DOMTextAttrs");
        static const char* aligns[4] = {"left", "right", "center", "justify"};
        if (e.align > 0 && e.align < 4)
            attrs->SetAttribute("alignment", aligns[e.align]);
        attrs->SetAttribute("size", number(e.height / 20.0).c_str());
        const auto font = _characters.find(e.font);
        attrs->SetAttribute("face", font != _characters.end() && !font->second.font.name.empty()
                ? font->second.font.name.c_str()
                : "Arial");
        if (e.hasColor)
        {
            attrs->SetAttribute("fillColor", hexColor(e.color).c_str());
            if (e.color.a != 255)
                attrs->SetAttribute("alpha", number(e.color.a / 255.0, 4).c_str());
        }
        attrsHolder->InsertEndChild(attrs);
        run->InsertEndChild(attrsHolder);
        runs->InsertEndChild(run);
        text->InsertEndChild(runs);
        return text;
    }

    /// A CompiledActions element holding \a actions, each as hexadecimal
    /// text: DoAction, DoInitAction (with the symbol it sets up),
    /// ClipAction (with its events and key) and ButtonAction (with its
    /// conditions). Test Movie runs them (see play::CompiledActions).
    tinyxml2::XMLElement* compiledElement(tinyxml2::XMLDocument& doc, const std::vector<CompiledAction>& actions)
    {
        tinyxml2::XMLElement* holder = doc.NewElement("CompiledActions");
        holder->SetAttribute("swfVersion", _version);
        for (const CompiledAction& action : actions)
        {
            static const char* const names[] = {"DoAction", "DoInitAction", "ClipAction", "ButtonAction"};
            tinyxml2::XMLElement* element = doc.NewElement(names[static_cast<int>(action.kind)]);
            if (action.kind == CompiledAction::Kind::Init)
            {
                const auto sprite = _characters.find(action.sprite);
                if (sprite != _characters.end() && sprite->second.kind == Character::Kind::Sprite)
                    element->SetAttribute("symbol", symbolOf(action.sprite).c_str());
            }
            else if (action.kind == CompiledAction::Kind::Clip)
            {
                element->SetAttribute("events", static_cast<unsigned>(action.events));
                if (action.key)
                    element->SetAttribute("key", action.key);
            }
            else if (action.kind == CompiledAction::Kind::Button)
                element->SetAttribute("conditions", static_cast<unsigned>(action.events));
            element->SetText(hexOf(action.actions).c_str());
            holder->InsertEndChild(element);
        }
        return holder;
    }

    tinyxml2::XMLElement* placedElement(tinyxml2::XMLDocument& doc, const Placement& p)
    {
        const auto found = _characters.find(p.character);
        if (found == _characters.end())
            return nullptr;
        Character& c = found->second;
        switch (c.kind)
        {
        case Character::Kind::Shape:
        {
            if (c.uses <= 1 && p.color.identity())
                return shapeElement(doc, c.shape, p.matrix);
            tinyxml2::XMLElement* instance = doc.NewElement("DOMSymbolInstance");
            instance->SetAttribute("libraryItemName", shapeSymbol(p.character).c_str());
            instance->SetAttribute("symbolType", "graphic");
            instance->SetAttribute("loop", "loop");
            writeMatrix(doc, instance, p.matrix);
            writeColor(doc, instance, p.color);
            return instance;
        }
        case Character::Kind::Morph:
            if (!p.color.identity())
                leftOut("color effects on morph shapes");
            return shapeElement(doc, morphAt(c.morph, p.ratio), p.matrix);
        case Character::Kind::Sprite:
        case Character::Kind::Button:
        {
            tinyxml2::XMLElement* instance = doc.NewElement("DOMSymbolInstance");
            instance->SetAttribute("libraryItemName", symbolOf(p.character).c_str());
            if (c.kind == Character::Kind::Button)
                instance->SetAttribute("symbolType", "button");
            if (!p.name.empty())
                instance->SetAttribute("name", p.name.c_str());
            static const char* blends[15] = {"", "", "layer", "multiply", "screen", "lighten", "darken", "difference",
                "add", "subtract", "invert", "alpha", "erase", "overlay", "hardlight"};
            if (p.blend >= 2 && p.blend < 15)
                instance->SetAttribute("blendMode", blends[p.blend]);
            writeMatrix(doc, instance, p.matrix);
            writeColor(doc, instance, p.color);
            if (!p.filters.empty())
            {
                tinyxml2::XMLElement* filters = doc.NewElement("filters");
                for (const Placement::Filter& f : p.filters)
                {
                    tinyxml2::XMLElement* filter = doc.NewElement(f.element.c_str());
                    for (const auto& [key, value] : f.attributes)
                        filter->SetAttribute(key.c_str(), value.c_str());
                    for (const auto& [color, ratio] : f.entries)
                    {
                        tinyxml2::XMLElement* entry = doc.NewElement("GradientEntry");
                        entry->SetAttribute("color", hexColor(color).c_str());
                        entry->SetAttribute("alpha", number(color.a / 255.0, 4).c_str());
                        entry->SetAttribute("ratio", number(ratio / 255.0, 4).c_str());
                        filter->InsertEndChild(entry);
                    }
                    filters->InsertEndChild(filter);
                }
                instance->InsertEndChild(filters);
            }
            // Its handlers, as compiled: a button's are its symbol's.
            if (c.kind == Character::Kind::Button && !c.buttonActions.empty())
                instance->InsertEndChild(compiledElement(doc, c.buttonActions));
            else if (c.kind == Character::Kind::Sprite && p.clipActions)
                instance->InsertEndChild(compiledElement(doc, *p.clipActions));
            return instance;
        }
        case Character::Kind::EditText:
            return editTextElement(doc, c, p);
        case Character::Kind::Video:
        {
            tinyxml2::XMLElement* video = doc.NewElement("DOMVideoInstance");
            video->SetAttribute("libraryItemName", nameOf(p.character).c_str());
            if (!p.name.empty())
                video->SetAttribute("name", p.name.c_str());
            video->SetAttribute("frameRight", c.width * 20);
            video->SetAttribute("frameBottom", c.height * 20);
            writeMatrix(doc, video, p.matrix);
            return video;
        }
        default:
            return nullptr;
        }
    }

    std::map<int, std::string> _symbolFiles; // id -> library name, written
    std::map<std::string, std::vector<uint8_t>>* _files = nullptr;
    std::vector<std::pair<std::string, std::string>> _includes; // href, kind

    std::string shapeSymbol(int id)
    {
        const auto done = _symbolFiles.find(id);
        if (done != _symbolFiles.end())
            return done->second;
        const std::string name = nameOf(id);
        _symbolFiles[id] = name;
        TimelineData single;
        single.frameCount = 1;
        Placement p;
        p.character = id;
        p.version = 1;
        single.frames.push_back({{1, p}});
        // Inside its symbol the shape is drawn as it is, never a symbol
        // again.
        const int uses = _characters[id].uses;
        _characters[id].uses = 1;
        writeSymbol(id, name, "graphic", single);
        _characters[id].uses = uses;
        return name;
    }

    std::string symbolOf(int id)
    {
        const auto done = _symbolFiles.find(id);
        if (done != _symbolFiles.end())
            return done->second;
        const std::string name = nameOf(id);
        _symbolFiles[id] = name;
        Character& c = _characters[id];
        std::shared_ptr<TimelineData> timeline = c.timeline;
        // Symbols nested past reason are written empty.
        if (!timeline || _symbolDepth >= kMaxNesting)
        {
            leftOut("symbols nested too deeply");
            timeline = std::make_shared<TimelineData>();
            timeline->frames.emplace_back();
            timeline->frameCount = 1;
        }
        ++_symbolDepth;
        writeSymbol(id, name, c.kind == Character::Kind::Button ? "button" : "", *timeline);
        --_symbolDepth;
        return name;
    }

    void writeSymbol(int id, const std::string& name, const char* type, const TimelineData& timeline)
    {
        tinyxml2::XMLDocument doc;
        tinyxml2::XMLElement* root = doc.NewElement("DOMSymbolItem");
        doc.InsertEndChild(root);
        root->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
        root->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
        root->SetAttribute("name", name.c_str());
        if (*type)
            root->SetAttribute("symbolType", type);
        const Character& c = _characters[id];
        if (!c.linkage.empty())
        {
            root->SetAttribute("linkageExportForAS", "true");
            // A class's name for ActionScript 3; an identifier for the
            // attachMovie and Object.registerClass of ActionScript 1 and 2.
            const bool actionScript3 = _abc > 0 || (_version >= 9 && _scripts == 0);
            root->SetAttribute(actionScript3 ? "linkageClassName" : "linkageIdentifier", c.linkage.c_str());
        }
        if (c.scale9)
        {
            root->SetAttribute("scale9", "true");
            root->SetAttribute("scaleGridLeft", number(c.grid.left / 20.0).c_str());
            root->SetAttribute("scaleGridRight", number(c.grid.right / 20.0).c_str());
            root->SetAttribute("scaleGridTop", number(c.grid.top / 20.0).c_str());
            root->SetAttribute("scaleGridBottom", number(c.grid.bottom / 20.0).c_str());
        }
        tinyxml2::XMLElement* holder = doc.NewElement("timeline");
        const std::string base = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
        holder->InsertEndChild(timelineElement(doc, base, timeline, *type && std::strcmp(type, "button") == 0));
        root->InsertEndChild(holder);
        (*_files)["LIBRARY/" + name + ".xml"] = printed(doc);
        _includes.emplace_back(name + ".xml", type);
    }

    tinyxml2::XMLElement* frameElement(tinyxml2::XMLDocument& doc, int index, int duration)
    {
        tinyxml2::XMLElement* frame = doc.NewElement("DOMFrame");
        frame->SetAttribute("index", index);
        if (duration > 1)
            frame->SetAttribute("duration", duration);
        frame->SetAttribute("keyMode", 9728);
        return frame;
    }

    tinyxml2::XMLElement* layerElement(tinyxml2::XMLDocument& doc, const std::string& name, int colorIndex)
    {
        static const char* colors[] = {"#4FFF4F", "#FF4FFF", "#4FFFFF", "#FF4F4F", "#9933CC", "#FF800A", "#4F4FFF",
            "#FFFF4F"};
        tinyxml2::XMLElement* layer = doc.NewElement("DOMLayer");
        layer->SetAttribute("name", name.c_str());
        layer->SetAttribute("color", colors[colorIndex % 8]);
        return layer;
    }

    /// A layer holding \a marks (frame to what it sets) as keyframes that
    /// run to the next mark, empty before the first.
    void markLayer(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* layers, const std::string& name, int frames,
        int colorIndex, const std::map<int, std::function<void(tinyxml2::XMLElement*)>>& marks)
    {
        tinyxml2::XMLElement* layer = layerElement(doc, name, colorIndex);
        tinyxml2::XMLElement* list = doc.NewElement("frames");
        int at = 0;
        for (auto it = marks.begin(); it != marks.end(); ++it)
        {
            if (it->first >= frames)
                break;
            if (it->first > at)
            {
                tinyxml2::XMLElement* empty = frameElement(doc, at, it->first - at);
                empty->InsertEndChild(doc.NewElement("elements"));
                list->InsertEndChild(empty);
            }
            auto next = std::next(it);
            const int end = next == marks.end() ? frames : std::min(frames, next->first);
            tinyxml2::XMLElement* frame = frameElement(doc, it->first, end - it->first);
            it->second(frame);
            frame->InsertEndChild(doc.NewElement("elements"));
            list->InsertEndChild(frame);
            at = end;
        }
        if (at < frames)
        {
            tinyxml2::XMLElement* empty = frameElement(doc, at, frames - at);
            empty->InsertEndChild(doc.NewElement("elements"));
            list->InsertEndChild(empty);
        }
        layer->InsertEndChild(list);
        layers->InsertEndChild(layer);
    }

    // -- Tweens ---------------------------------------------------------------

    /// Frames \a start up to \a end of a depth where one placement holds;
    /// none for an empty span.
    struct Span
    {
        int start = 0, end = 0;
        const Placement* placement = nullptr;
    };

    /// A classic tween's settings, as the keyframe that starts it gives them.
    struct Tween
    {
        fla::TweenRotation rotation = fla::TweenRotation::Auto;
        int turns = 0;
        int acceleration = 0;
    };

    /// The longest a recognized tween runs, and the frame checks a movie
    /// may spend recognizing them, past which its frames stay baked.
    static constexpr int kMaxTween = 1000;
    static constexpr long kTweenBudget = 50000000;
    long _tweenWork = 0;
    /// How far, in twips, a tween may put a point from where the baked
    /// frame has it: the rounding of the frame's own matrix and a little.
    static constexpr double kStray = 0.75;
    std::map<int, Rect> _bounds;

    /// The bounds of character \a id in its own space, in twips: a shape's
    /// own, a symbol's from everything it places.
    Rect boundsOf(int id, int depth = 0)
    {
        const auto done = _bounds.find(id);
        if (done != _bounds.end())
            return done->second;
        Rect box;
        const auto found = _characters.find(id);
        if (found == _characters.end() || depth > kMaxNesting)
            return box;
        const Character& c = found->second;
        if (c.kind == Character::Kind::EditText)
            box = c.edit.bounds;
        else if (c.kind == Character::Kind::Video)
            box = Rect{0, c.width * 20.0, 0, c.height * 20.0};
        else if (c.timeline && (c.kind == Character::Kind::Sprite || c.kind == Character::Kind::Button))
        {
            _bounds[id] = box; // back in itself: nothing more
            bool any = false;
            std::set<long> seen;
            for (const auto& frame : c.timeline->frames)
                for (const auto& [at, placement] : frame)
                {
                    if (placement.character == id || !seen.insert(placement.version).second)
                        continue;
                    const Rect inner = boundsOf(placement.character, depth + 1);
                    const double xs[2] = {inner.left, inner.right}, ys[2] = {inner.top, inner.bottom};
                    for (double x : xs)
                        for (double y : ys)
                        {
                            double px, py;
                            placement.matrix.apply(x, y, px, py);
                            if (!any)
                                box = Rect{px, px, py, py};
                            box.left = std::min(box.left, px);
                            box.right = std::max(box.right, px);
                            box.top = std::min(box.top, py);
                            box.bottom = std::max(box.bottom, py);
                            any = true;
                        }
                }
        }
        else
            box = c.bounds;
        _bounds[id] = box;
        return box;
    }
    int _tweens = 0;
    int _shapeTweens = 0;

    static fla::Transform transformOf(const Matrix& m) { return fla::Transform(m.a, m.b, m.c, m.d, m.tx / 20.0, m.ty / 20.0); }

    static fla::ColorTransform colorOf(const ColorTransform& cx)
    {
        fla::ColorTransform color;
        color.redMultiplier = cx.mul[0];
        color.greenMultiplier = cx.mul[1];
        color.blueMultiplier = cx.mul[2];
        color.alphaMultiplier = cx.mul[3];
        color.redOffset = cx.add[0];
        color.greenOffset = cx.add[1];
        color.blueOffset = cx.add[2];
        color.alphaOffset = cx.add[3];
        return color;
    }

    static bool sameFilters(const std::vector<Placement::Filter>& a, const std::vector<Placement::Filter>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].element != b[i].element || a[i].attributes != b[i].attributes ||
                a[i].entries.size() != b[i].entries.size())
                return false;
            for (size_t e = 0; e < a[i].entries.size(); ++e)
            {
                const Rgba& x = a[i].entries[e].first;
                const Rgba& y = b[i].entries[e].first;
                if (x.r != y.r || x.g != y.g || x.b != y.b || x.a != y.a ||
                    a[i].entries[e].second != b[i].entries[e].second)
                    return false;
            }
        }
        return true;
    }

    std::map<int, bool> _animated;

    /// Whether character \a id plays frames of its own, or holds something
    /// that does. Phoenix starts a movie clip over at each keyframe it is
    /// on, so those keep the keyframes the movie gave them: joining them
    /// into a tween would change what they show.
    bool animated(int id, int depth = 0)
    {
        const auto done = _animated.find(id);
        if (done != _animated.end())
            return done->second;
        const auto found = _characters.find(id);
        if (found == _characters.end() || !found->second.timeline ||
            (found->second.kind != Character::Kind::Sprite && found->second.kind != Character::Kind::Button))
            return false;
        // Nested past reason, or back in itself: taken to play.
        if (depth > kMaxNesting)
            return true;
        _animated[id] = true;
        const TimelineData& timeline = *found->second.timeline;
        bool plays = found->second.kind == Character::Kind::Sprite && timeline.frameCount > 1;
        for (size_t f = 0; f < timeline.frames.size() && !plays; ++f)
            for (const auto& [at, placement] : timeline.frames[f])
                if (placement.character != id && animated(placement.character, depth + 1))
                {
                    plays = true;
                    break;
                }
        _animated[id] = plays;
        return plays;
    }

    /// Whether \a a and \a b place the same instance the same way. A
    /// video's ratio is the picture it shows, which its item plays on
    /// anyway.
    bool samePlacement(const Placement& a, const Placement& b)
    {
        const auto found = _characters.find(a.character);
        const bool video = found != _characters.end() && found->second.kind == Character::Kind::Video;
        return !animated(a.character) && a.character == b.character && a.instance == b.instance && a.matrix == b.matrix &&
            a.color == b.color && a.name == b.name && (video || a.ratio == b.ratio) && a.clipDepth == b.clipDepth &&
            a.blend == b.blend && sameFilters(a.filters, b.filters);
    }

    /// Whether \a b can end a classic tween that \a a starts: the same
    /// symbol, kept on (a movie clip placed anew starts over), with only its
    /// matrix and color changed.
    bool tweensTo(const Placement& a, const Placement& b)
    {
        const auto found = _characters.find(a.character);
        if (found == _characters.end() || a.character != b.character)
            return false;
        const Character::Kind kind = found->second.kind;
        if (kind != Character::Kind::Shape && kind != Character::Kind::Sprite && kind != Character::Kind::Button)
            return false;
        if (kind != Character::Kind::Shape && (a.instance != b.instance || animated(a.character)))
            return false;
        return a.name == b.name && a.clipDepth == b.clipDepth && a.blend == b.blend &&
            sameFilters(a.filters, b.filters);
    }

    /// Whether every frame from span \a i up to span \a j shows what a
    /// classic tween between the two with \a tween's settings would, within
    /// rounding: no corner of it more than kStray twips from where the movie
    /// has it, and a step of color.
    bool tweenShows(const std::vector<Span>& spans, size_t i, size_t j, const Tween& tween)
    {
        const Placement& a = *spans[i].placement;
        const Placement& b = *spans[j].placement;
        // Where the corners of what is placed land, in twips, measures how
        // far a frame strays: a shape's bounds, else a symbol's.
        const Rect box = boundsOf(a.character);
        const double corners[4][2] = {
            {box.left, box.top}, {box.right, box.top}, {box.left, box.bottom}, {box.right, box.bottom}};
        const fla::Transform from = transformOf(a.matrix), to = transformOf(b.matrix);
        const fla::ColorTransform fromColor = colorOf(a.color), toColor = colorOf(b.color);
        const bool colored = !(a.color == b.color);
        const double duration = spans[j].start - spans[i].start;
        const std::vector<fla::Point> noCurve;
        for (size_t k = i; k < j; ++k)
        {
            const Placement& p = *spans[k].placement;
            for (int f = std::max(spans[k].start, spans[i].start + 1); f < spans[k].end; ++f)
            {
                ++_tweenWork;
                const double t = fla::easeProgress((f - spans[i].start) / duration, tween.acceleration, noCurve);
                const fla::Transform m = fla::blendTransforms(from, to, t, tween.rotation, tween.turns);
                for (const auto& corner : corners)
                {
                    const double x = corner[0], y = corner[1];
                    const double dx = (m.m11 - p.matrix.a) * x + (m.m21 - p.matrix.c) * y + m.tx * 20.0 - p.matrix.tx;
                    const double dy = (m.m12 - p.matrix.b) * x + (m.m22 - p.matrix.d) * y + m.ty * 20.0 - p.matrix.ty;
                    if (std::fabs(dx) > kStray || std::fabs(dy) > kStray)
                        return false;
                }
                if (!colored)
                {
                    if (!(p.color == a.color))
                        return false;
                    continue;
                }
                const fla::ColorTransform c = fla::blendColorTransforms(fromColor, toColor, t);
                const double muls[4] = {c.redMultiplier, c.greenMultiplier, c.blueMultiplier, c.alphaMultiplier};
                const int adds[4] = {c.redOffset, c.greenOffset, c.blueOffset, c.alphaOffset};
                for (int n = 0; n < 4; ++n)
                    if (std::fabs(muls[n] - p.color.mul[n]) > 1.5 / 256.0 || std::abs(adds[n] - p.color.add[n]) > 1)
                        return false;
            }
        }
        return true;
    }

    /// How frames fit a classic tween: they do, they come close to one (a
    /// longer one may fit), or they are nothing like one.
    enum class Fit
    {
        Fits,
        Near,
        Off
    };

    /// The settings of a classic tween from span \a i to span \a j that
    /// shows every frame between as it is: its turn worked out from the
    /// angle frame by frame, its ease from how far along each frame is.
    Fit fitTween(const std::vector<Span>& spans, size_t i, size_t j, Tween& tween)
    {
        const Placement& a = *spans[i].placement;
        const Placement& b = *spans[j].placement;
        // Ending where it starts, it must have gone somewhere between (a
        // whole turn).
        if (a.matrix == b.matrix && a.color == b.color)
        {
            bool moved = false;
            for (size_t k = i + 1; k < j && !moved; ++k)
                moved = !(spans[k].placement->matrix == a.matrix);
            if (!moved)
                return Fit::Off;
        }
        // The last tween's settings, which usually carry on.
        if (tweenShows(spans, i, j, tween))
            return Fit::Fits;

        constexpr double kTurn = 2.0 * 3.14159265358979323846;
        const auto angleOf = [](const Matrix& m) { return std::atan2(m.b, m.a); };
        const auto wrap = [kTurn](double angle) {
            angle = std::fmod(angle, kTurn);
            if (angle > kTurn / 2)
                angle -= kTurn;
            else if (angle < -kTurn / 2)
                angle += kTurn;
            return angle;
        };
        // What each frame holds, the tween's first to its last.
        std::vector<const Placement*> run;
        for (size_t k = i; k < j; ++k)
            for (int f = spans[k].start; f < spans[k].end; ++f)
                run.push_back(spans[k].placement);
        run.push_back(&b);
        // The angle turned through by each frame, and the measure that
        // changes most from one keyframe to the other, to say how far along
        // each frame is: pixels of movement, hundredths of scale or of a
        // radian, steps of color.
        std::vector<double> turned(run.size(), 0.0);
        for (size_t f = 1; f < run.size(); ++f)
            turned[f] = turned[f - 1] + wrap(angleOf(run[f]->matrix) - angleOf(run[f - 1]->matrix));
        const auto measures = [&](size_t f, double out[13]) {
            const Matrix& m = run[f]->matrix;
            const ColorTransform& cx = run[f]->color;
            out[0] = m.tx / 20.0;
            out[1] = m.ty / 20.0;
            out[2] = std::hypot(m.a, m.b) * 100.0;
            out[3] = std::hypot(m.c, m.d) * 100.0;
            out[4] = turned[f] * 100.0;
            for (int n = 0; n < 4; ++n)
            {
                out[5 + n] = cx.mul[n] * 255.0;
                out[9 + n] = cx.add[n];
            }
        };
        double first[13], last[13];
        measures(0, first);
        measures(run.size() - 1, last);
        int channel = 0;
        for (int n = 1; n < 13; ++n)
            if (std::fabs(last[n] - first[n]) > std::fabs(last[channel] - first[channel]))
                channel = n;
        const double change = last[channel] - first[channel];
        if (change == 0.0)
            return Fit::Off;
        // An ease bends progress t to t + e t (1 - t), e from -1 (in) to 1
        // (out): e by least squares.
        double sum = 0.0, weights = 0.0;
        const double duration = static_cast<double>(run.size() - 1);
        for (size_t f = 1; f + 1 < run.size(); ++f)
        {
            double values[13];
            measures(f, values);
            const double t = f / duration;
            const double w = t * (1.0 - t);
            sum += ((values[channel] - first[channel]) / change - t) * w;
            weights += w * w;
        }
        const double bend = weights > 0.0 ? sum / weights : 0.0;
        // Far off any such curve, a longer tween from here will not be one
        // either; near one, a later keyframe may fit where this one missed
        // (part of an eased tween is eased by an amount between whole
        // steps).
        const double rounding[13] = {0.1, 0.1, 0.01, 0.01, 0.01, 2, 2, 2, 2, 2, 2, 2, 2};
        const double slack = std::max(rounding[channel], 0.01 * std::fabs(change));
        if (std::fabs(bend) > 1.0 + 1e-9)
            return Fit::Off;
        for (size_t f = 1; f + 1 < run.size(); ++f)
        {
            double values[13];
            measures(f, values);
            const double t = f / duration;
            if (std::fabs(values[channel] - first[channel] - change * (t + bend * t * (1.0 - t))) > slack)
                return Fit::Off;
        }
        const int ease = static_cast<int>(std::lround(std::clamp(bend, -1.0, 1.0) * 100.0));

        // Which way it turns: the short way, or round as far as it went.
        std::vector<std::pair<fla::TweenRotation, int>> rotations = {{fla::TweenRotation::Auto, 0}};
        const double total = turned.back();
        if (std::fabs(total) > 1e-9)
        {
            const double straight = std::fmod(angleOf(b.matrix) - angleOf(a.matrix), kTurn);
            if (total > 0)
            {
                const double base = straight < 0.0 ? straight + kTurn : straight;
                const long turns = std::lround((total - base) / kTurn);
                if (turns >= 0)
                    rotations.emplace_back(fla::TweenRotation::Clockwise, static_cast<int>(turns));
            }
            else
            {
                const double base = straight > 0.0 ? straight - kTurn : straight;
                const long turns = std::lround((base - total) / kTurn);
                if (turns >= 0)
                    rotations.emplace_back(fla::TweenRotation::CounterClockwise, static_cast<int>(turns));
            }
        }
        rotations.emplace_back(fla::TweenRotation::None, 0);
        for (const auto& [rotation, turns] : rotations)
            for (int acceleration : {ease, ease - 1, ease + 1})
            {
                if (acceleration < -100 || acceleration > 100)
                    continue;
                const Tween candidate{rotation, turns, acceleration};
                if (tweenShows(spans, i, j, candidate))
                {
                    tween = candidate;
                    return Fit::Fits;
                }
            }
        return Fit::Near;
    }

    /// The span that ends the longest classic tween starting at span \a i,
    /// its settings in \a tween; \a i itself when the frames from there are
    /// no clean tween.
    size_t tweenEnd(const std::vector<Span>& spans, size_t i, Tween& tween)
    {
        const Placement* first = spans[i].placement;
        if (!first || _tweenWork > kTweenBudget)
            return i;
        size_t best = i;
        Tween trying;
        for (size_t j = i + 1; j < spans.size(); ++j)
        {
            const Placement* last = spans[j].placement;
            if (!last || !tweensTo(*first, *last) || spans[j].start - spans[i].start > kMaxTween ||
                _tweenWork > kTweenBudget)
                break;
            // A keyframe straight after another needs no tween.
            if (spans[j].start - spans[i].start < 2)
                continue;
            const Fit fit = fitTween(spans, i, j, trying);
            if (fit == Fit::Off)
                break;
            if (fit == Fit::Near)
                continue;
            // An ease or a turn has to show over more than one frame
            // between, or any three frames would make a tween.
            const bool plain = trying.acceleration == 0 && (trying.rotation == fla::TweenRotation::Auto ||
                                                               trying.rotation == fla::TweenRotation::None);
            if (spans[j].start - spans[i].start < (plain ? 2 : 3))
                continue;
            best = j;
            tween = trying;
        }
        return best;
    }

    /// Whether a shape tween can show morph \a m as the SWF does: bitmap
    /// fills that hold still, and gradients that only scale (a tween blends
    /// a turning gradient's angle, where the movie blends its matrix).
    static bool morphTweens(const MorphData& m)
    {
        for (size_t i = 0; i < m.startFills.size() && i < m.endFills.size(); ++i)
        {
            const Fill& a = m.startFills[i];
            const Fill& b = m.endFills[i];
            if (a.type >= 0x40 && !(a.matrix == b.matrix))
                return false;
            const Matrix& x = a.gradient.matrix;
            const Matrix& y = b.gradient.matrix;
            if ((a.type == 0x10 || a.type == 0x12 || a.type == 0x13) && !(x == y) &&
                (x.b != 0 || x.c != 0 || y.b != 0 || y.c != 0 || (x.a < 0) != (y.a < 0) || (x.d < 0) != (y.d < 0)))
                return false;
        }
        return true;
    }

    /// How the ratios of a morph from span \a i to span \a j fit a shape
    /// tween's progress, its ease in \a tween.
    Fit fitRatio(const std::vector<Span>& spans, size_t i, size_t j, Tween& tween)
    {
        const double from = spans[i].placement->ratio;
        const double change = spans[j].placement->ratio - from;
        if (change == 0)
            return Fit::Off;
        const double duration = spans[j].start - spans[i].start;
        const std::vector<fla::Point> noCurve;
        const auto shows = [&](int acceleration) {
            for (size_t k = i; k < j; ++k)
                for (int f = std::max(spans[k].start, spans[i].start + 1); f < spans[k].end; ++f)
                {
                    ++_tweenWork;
                    const double t = fla::easeProgress((f - spans[i].start) / duration, acceleration, noCurve);
                    if (std::fabs(from + change * t - spans[k].placement->ratio) > 2.0)
                        return false;
                }
            return true;
        };
        if (shows(tween.acceleration))
            return Fit::Fits;
        double sum = 0.0, weights = 0.0;
        for (size_t k = i; k < j; ++k)
            for (int f = std::max(spans[k].start, spans[i].start + 1); f < spans[k].end; ++f)
            {
                const double t = (f - spans[i].start) / duration;
                const double w = t * (1.0 - t);
                sum += ((spans[k].placement->ratio - from) / change - t) * w;
                weights += w * w;
            }
        const double bend = weights > 0.0 ? sum / weights : 0.0;
        if (std::fabs(bend) > 1.0 + 1e-9)
            return Fit::Off;
        const double slack = std::max(2.0, 0.01 * std::fabs(change));
        for (size_t k = i; k < j; ++k)
            for (int f = std::max(spans[k].start, spans[i].start + 1); f < spans[k].end; ++f)
            {
                const double t = (f - spans[i].start) / duration;
                if (std::fabs(from + change * (t + bend * t * (1.0 - t)) - spans[k].placement->ratio) > slack)
                    return Fit::Off;
            }
        const int ease = static_cast<int>(std::lround(std::clamp(bend, -1.0, 1.0) * 100.0));
        for (int acceleration : {ease, ease - 1, ease + 1})
            if (acceleration >= -100 && acceleration <= 100 && shows(acceleration))
            {
                tween = Tween{fla::TweenRotation::Auto, 0, acceleration};
                return Fit::Fits;
            }
        return Fit::Near;
    }

    /// The span that ends the longest shape tween starting at span \a i: a
    /// morph shape held in place while its ratio runs on, evenly or with an
    /// ease; \a i itself when there is none.
    size_t morphTweenEnd(const std::vector<Span>& spans, size_t i, Tween& tween)
    {
        const Placement* first = spans[i].placement;
        if (!first || _tweenWork > kTweenBudget)
            return i;
        const auto found = _characters.find(first->character);
        if (found == _characters.end() || found->second.kind != Character::Kind::Morph ||
            !morphTweens(found->second.morph))
            return i;
        size_t best = i;
        Tween trying;
        for (size_t j = i + 1; j < spans.size(); ++j)
        {
            const Placement* last = spans[j].placement;
            if (!last || last->character != first->character || !(last->matrix == first->matrix) ||
                !(last->color == first->color) || last->name != first->name || last->clipDepth != first->clipDepth ||
                spans[j].start - spans[i].start > kMaxTween || _tweenWork > kTweenBudget)
                break;
            if (spans[j].start - spans[i].start < 2)
                continue;
            const Fit fit = fitRatio(spans, i, j, trying);
            if (fit == Fit::Off)
                break;
            if (fit == Fit::Near || spans[j].start - spans[i].start < (trying.acceleration == 0 ? 2 : 3))
                continue;
            best = j;
            tween = trying;
        }
        return best;
    }

    /// A morph coordinate: twips over 256, as the file writes them.
    static std::string morphPoint(double x, double y)
    {
        return number(x / 256.0, 6) + ", " + number(y / 256.0, 6);
    }

    /// The MorphShape a shape tween from \a a to \a b keeps, both placed by
    /// \a place: a segment for each outline, its points from one shape to
    /// the other. Null when the two do not match point for point.
    tinyxml2::XMLElement* morphElement(tinyxml2::XMLDocument& doc, const ShapeData& a, const ShapeData& b,
        const Matrix& place)
    {
        if (a.runs.size() != b.runs.size() || a.runs.empty())
            return nullptr;
        tinyxml2::XMLElement* morph = doc.NewElement("MorphShape");
        tinyxml2::XMLElement* segments = doc.NewElement("morphSegments");
        morph->InsertEndChild(segments);
        for (size_t k = 0; k < a.runs.size(); ++k)
        {
            const Run& from = a.runs[k];
            const Run& to = b.runs[k];
            if (from.commands.size() != to.commands.size() || from.fill0 != to.fill0 || from.fill1 != to.fill1 ||
                from.line != to.line)
            {
                doc.DeleteNode(morph);
                return nullptr;
            }
            tinyxml2::XMLElement* segment = nullptr;
            double ax = 0, ay = 0, bx = 0, by = 0;
            for (size_t c = 0; c < from.commands.size(); ++c)
            {
                const Command& p = from.commands[c];
                const Command& q = to.commands[c];
                if (p.kind != q.kind || (p.kind != 'M' && !segment))
                {
                    doc.DeleteNode(morph);
                    return nullptr;
                }
                double px, py, qx, qy;
                place.apply(p.x, p.y, px, py);
                place.apply(q.x, q.y, qx, qy);
                if (p.kind == 'M')
                {
                    segment = doc.NewElement("MorphSegment");
                    segment->SetAttribute("startPointA", morphPoint(px, py).c_str());
                    segment->SetAttribute("startPointB", morphPoint(qx, qy).c_str());
                    if (from.line)
                        segment->SetAttribute("strokeIndex1", from.line);
                    if (from.fill0)
                        segment->SetAttribute("fillIndex1", from.fill0);
                    if (from.fill1)
                        segment->SetAttribute("fillIndex2", from.fill1);
                    segments->InsertEndChild(segment);
                }
                else
                {
                    // A line's control point is its middle.
                    double pcx = (ax + px) / 2, pcy = (ay + py) / 2, qcx = (bx + qx) / 2, qcy = (by + qy) / 2;
                    if (p.kind == 'Q')
                    {
                        place.apply(p.cx, p.cy, pcx, pcy);
                        place.apply(q.cx, q.cy, qcx, qcy);
                    }
                    tinyxml2::XMLElement* curve = doc.NewElement("MorphCurves");
                    curve->SetAttribute("controlPointA", morphPoint(pcx, pcy).c_str());
                    curve->SetAttribute("anchorPointA", morphPoint(px, py).c_str());
                    curve->SetAttribute("controlPointB", morphPoint(qcx, qcy).c_str());
                    curve->SetAttribute("anchorPointB", morphPoint(qx, qy).c_str());
                    if (p.kind == 'L')
                        curve->SetAttribute("isLine", "true");
                    segment->InsertEndChild(curve);
                }
                ax = px;
                ay = py;
                bx = qx;
                by = qy;
            }
        }
        return morph;
    }

    tinyxml2::XMLElement* timelineElement(tinyxml2::XMLDocument& doc, const std::string& name, const TimelineData& t,
        bool button)
    {
        tinyxml2::XMLElement* timeline = doc.NewElement("DOMTimeline");
        timeline->SetAttribute("name", name.c_str());
        tinyxml2::XMLElement* layers = doc.NewElement("layers");
        const int frames = std::max(1, t.frameCount);
        int colorIndex = 0;

        if (!t.scripts.empty() || !t.actions.empty())
        {
            std::map<int, std::function<void(tinyxml2::XMLElement*)>> marks;
            std::set<int> scripted;
            for (const auto& [frame, text] : t.scripts)
                scripted.insert(frame);
            for (const auto& [frame, actions] : t.actions)
                scripted.insert(frame);
            for (const int frame : scripted)
            {
                const auto text = t.scripts.find(frame);
                const std::string script = text == t.scripts.end() ? std::string() : text->second;
                const auto compiled = t.actions.find(frame);
                const std::vector<CompiledAction>* actions = compiled == t.actions.end() ? nullptr : &compiled->second;
                marks[frame] = [this, &doc, script, actions](tinyxml2::XMLElement* f) {
                    if (!script.empty())
                    {
                        tinyxml2::XMLElement* as = doc.NewElement("Actionscript");
                        tinyxml2::XMLElement* s = doc.NewElement("script");
                        tinyxml2::XMLText* body = doc.NewText(script.c_str());
                        body->SetCData(true);
                        s->InsertEndChild(body);
                        as->InsertEndChild(s);
                        f->InsertEndChild(as);
                    }
                    if (actions && !actions->empty())
                        f->InsertEndChild(compiledElement(doc, *actions));
                };
            }
            markLayer(doc, layers, "Actions", frames, colorIndex++, marks);
        }
        if (!t.labels.empty())
        {
            std::map<int, std::function<void(tinyxml2::XMLElement*)>> marks;
            for (const auto& [frame, label] : t.labels)
            {
                const std::string text = label;
                marks[frame] = [text](tinyxml2::XMLElement* f) {
                    f->SetAttribute("name", text.c_str());
                    f->SetAttribute("labelType", "name");
                };
            }
            markLayer(doc, layers, "Labels", frames, colorIndex++, marks);
        }
        // Event sounds, a layer for each that start on the same frame.
        size_t soundLayers = 0;
        for (const auto& [frame, uses] : t.sounds)
            soundLayers = std::max(soundLayers, uses.size());
        for (size_t k = 0; k < soundLayers; ++k)
        {
            std::map<int, std::function<void(tinyxml2::XMLElement*)>> marks;
            for (const auto& [frame, uses] : t.sounds)
            {
                if (k >= uses.size())
                    continue;
                const SoundUse use = uses[k];
                const auto found = _characters.find(use.sound);
                if (found == _characters.end() || found->second.kind != Character::Kind::Sound ||
                    found->second.data.empty())
                    continue;
                const std::string soundName = nameOf(use.sound);
                marks[frame] = [use, soundName](tinyxml2::XMLElement* f) {
                    f->SetAttribute("soundName", soundName.c_str());
                    if (use.stop)
                        f->SetAttribute("soundSync", "stop");
                    else if (use.noMultiple)
                        f->SetAttribute("soundSync", "start");
                    if (use.loops > 1)
                        f->SetAttribute("soundLoop", use.loops);
                };
            }
            if (!marks.empty())
                markLayer(doc, layers, soundLayers > 1 ? "Sound " + std::to_string(k + 1) : "Sound", frames,
                    colorIndex++, marks);
        }
        if (t.streamFirst >= 0 && !t.streamData.empty())
        {
            const std::string soundName = streamItem(t);
            std::map<int, std::function<void(tinyxml2::XMLElement*)>> marks;
            marks[t.streamFirst] = [soundName](tinyxml2::XMLElement* f) {
                f->SetAttribute("soundName", soundName.c_str());
                f->SetAttribute("soundSync", "stream");
            };
            markLayer(doc, layers, "Soundtrack", frames, colorIndex++, marks);
        }

        // A layer for each depth, top first; a clip depth makes a mask
        // layer over the depths it clips. A depth's part changes from frame
        // to frame (a mask for a while, clipped by one, or neither), so it
        // gets a layer for each part it plays, holding just those frames.
        // A role: kMask, kPlain, or the depth of the mask clipping it.
        constexpr int kMask = -1, kPlain = -2;
        std::vector<std::map<int, int>> roles(t.frames.size());
        std::map<int, std::set<int>> rolesOf;
        for (size_t f = 0; f < t.frames.size(); ++f)
        {
            const auto& frame = t.frames[f];
            for (const auto& [depth, p] : frame)
            {
                int role = p.clipDepth > 0 ? kMask : kPlain;
                if (role == kPlain)
                    for (const auto& [mask, m] : frame)
                    {
                        if (mask >= depth)
                            break;
                        if (m.clipDepth >= depth)
                        {
                            role = mask;
                            break;
                        }
                    }
                roles[f][depth] = role;
                rolesOf[depth].insert(role);
            }
        }
        std::vector<int> depths;
        for (auto it = rolesOf.rbegin(); it != rolesOf.rend(); ++it)
            depths.push_back(it->first);
        std::set<std::pair<int, int>> written;
        std::map<int, int> layerIndex;
        int index = 0;
        for (tinyxml2::XMLElement* e = layers->FirstChildElement(); e; e = e->NextSiblingElement())
            ++index;
        const auto writeDepth = [&](int depth, int role) {
            // Its placement in a frame where it plays this part.
            const auto placementAt = [&](size_t f) -> const Placement* {
                const auto found = t.frames[f].find(depth);
                return found != t.frames[f].end() && roles[f].at(depth) == role ? &found->second : nullptr;
            };
            std::string name = depthName(t, depth);
            if (rolesOf[depth].size() > 1 && role != kPlain)
                name += role == kMask ? " Mask" : " Masked";
            tinyxml2::XMLElement* layer = layerElement(doc, name, colorIndex++);
            if (role == kMask)
            {
                layer->SetAttribute("layerType", "mask");
                layer->SetAttribute("locked", "true");
            }
            else if (role >= 0 && layerIndex.count(role))
            {
                layer->SetAttribute("parentLayerIndex", layerIndex[role]);
                layer->SetAttribute("locked", "true");
            }
            // A layer of only video is hidden: Phoenix cannot draw its
            // pictures, and its stand-in box would cover what is below.
            bool video = false, other = false;
            for (size_t f = 0; f < t.frames.size(); ++f)
            {
                const Placement* placement = placementAt(f);
                if (!placement)
                    continue;
                const auto c = _characters.find(placement->character);
                (c != _characters.end() && c->second.kind == Character::Kind::Video ? video : other) = true;
            }
            if (video && !other)
                layer->SetAttribute("visible", "false");
            tinyxml2::XMLElement* list = doc.NewElement("frames");
            // The spans of frames each placement holds for.
            std::vector<Span> spans;
            for (int f = 0; f < static_cast<int>(t.frames.size()); ++f)
            {
                const Placement* placement = placementAt(static_cast<size_t>(f));
                const Placement* before = spans.empty() ? nullptr : spans.back().placement;
                // Moved to where it already is, it holds on too.
                if (!spans.empty() && ((before ? before->version : -1) == (placement ? placement->version : -1) ||
                                          (_options.recognizeTweens && before && placement &&
                                              samePlacement(*before, *placement))))
                    spans.back().end = f + 1;
                else
                    spans.push_back(Span{f, f + 1, placement});
            }
            for (size_t i = 0; i < spans.size();)
            {
                Tween tween;
                size_t next = _options.recognizeTweens ? tweenEnd(spans, i, tween) : i;
                // A morph shape whose ratio runs on: a shape tween, its
                // morph made from the shapes at either end.
                tinyxml2::XMLElement* morph = nullptr;
                if (_options.recognizeTweens && next == i)
                {
                    next = morphTweenEnd(spans, i, tween);
                    if (next > i)
                    {
                        const Placement& a = *spans[i].placement;
                        const MorphData& m = _characters[a.character].morph;
                        morph = morphElement(doc, morphAt(m, a.ratio), morphAt(m, spans[next].placement->ratio),
                            a.matrix);
                        if (!morph)
                            next = i;
                    }
                }
                const int end = next > i ? spans[next].start : spans[i].end;
                tinyxml2::XMLElement* frame = frameElement(doc, spans[i].start, end - spans[i].start);
                if (morph)
                {
                    frame->SetAttribute("tweenType", "shape");
                    frame->SetAttribute("keyMode", 17922);
                    if (tween.acceleration != 0)
                        frame->SetAttribute("acceleration", tween.acceleration);
                    frame->InsertEndChild(morph);
                    ++_shapeTweens;
                }
                else if (next > i)
                {
                    frame->SetAttribute("tweenType", "motion");
                    frame->SetAttribute("motionTweenSnap", "true");
                    frame->SetAttribute("keyMode", 22017);
                    if (tween.acceleration != 0)
                        frame->SetAttribute("acceleration", tween.acceleration);
                    if (tween.rotation == fla::TweenRotation::None)
                        frame->SetAttribute("motionTweenRotate", "none");
                    else if (tween.rotation != fla::TweenRotation::Auto)
                    {
                        frame->SetAttribute("motionTweenRotate",
                            tween.rotation == fla::TweenRotation::Clockwise ? "clockwise" : "counter-clockwise");
                        if (tween.turns > 0)
                            frame->SetAttribute("motionTweenRotateTimes", tween.turns);
                    }
                    ++_tweens;
                }
                tinyxml2::XMLElement* elements = doc.NewElement("elements");
                if (spans[i].placement)
                    if (tinyxml2::XMLElement* element = placedElement(doc, *spans[i].placement))
                        elements->InsertEndChild(element);
                frame->InsertEndChild(elements);
                list->InsertEndChild(frame);
                i = next > i ? next : i + 1;
            }
            layer->InsertEndChild(list);
            layers->InsertEndChild(layer);
            if (role == kMask)
                layerIndex[depth] = index;
            ++index;
            written.insert({depth, role});
        };
        for (int depth : depths)
            for (int role : rolesOf[depth])
            {
                if (written.count({depth, role}))
                    continue;
                // A mask goes over what it clips.
                if (role >= 0 && !written.count({role, kMask}))
                    writeDepth(role, kMask);
                writeDepth(depth, role);
            }
        if (depths.empty() && layers->NoChildren())
        {
            tinyxml2::XMLElement* layer = layerElement(doc, "Layer 1", 0);
            tinyxml2::XMLElement* list = doc.NewElement("frames");
            tinyxml2::XMLElement* frame = frameElement(doc, 0, frames);
            frame->InsertEndChild(doc.NewElement("elements"));
            list->InsertEndChild(frame);
            layer->InsertEndChild(list);
            layers->InsertEndChild(layer);
        }
        (void)button;
        timeline->InsertEndChild(layers);
        return timeline;
    }

    static std::string depthName(const TimelineData& t, int depth)
    {
        // The instance name it holds throughout, else its depth.
        std::string name;
        for (const auto& frame : t.frames)
        {
            const auto found = frame.find(depth);
            if (found == frame.end())
                continue;
            if (found->second.name.empty() || (!name.empty() && name != found->second.name))
                return "Depth " + std::to_string(depth);
            name = found->second.name;
        }
        return name.empty() ? "Depth " + std::to_string(depth) : name;
    }

    std::map<const TimelineData*, std::string> _streams;

    std::string streamItem(const TimelineData& t)
    {
        const auto done = _streams.find(&t);
        if (done != _streams.end())
            return done->second;
        static const char* rates[4] = {"5kHz", "11kHz", "22kHz", "44kHz"};
        Character sound;
        sound.kind = Character::Kind::Sound;
        sound.data = t.streamData;
        const bool sixteen = t.streamMp3 || t.streamCodec == 1 || t.stream16;
        sound.soundFormat = std::string(rates[t.streamRate & 3]) + (sixteen ? " 16bit" : " 8bit") +
            (t.streamStereo ? " Stereo" : " Mono");
        const size_t bytesPerSample = (sixteen ? 2 : 1) * (t.streamStereo ? 2 : 1);
        sound.sampleCount = t.streamMp3 ? 0 : static_cast<uint32_t>(sound.data.size() / bytesPerSample);
        if (t.streamMp3)
            for (const swf::Mp3Frame& frame : swf::mp3Frames(sound.data))
                sound.sampleCount += static_cast<uint32_t>(frame.samples);
        sound.name = uniqueName(&t == _main.get() ? "Sounds/Soundtrack" : "Sounds/Stream");
        _soundItems.push_back(sound);
        _streams[&t] = sound.name;
        return sound.name;
    }

    std::vector<Character> _soundItems;

    std::string dataName()
    {
        return "SWF " + std::to_string(++_dataCount) + ".dat";
    }

    void finish(std::map<std::string, std::vector<uint8_t>>& files)
    {
        _files = &files;
        countUses(*_main);
        for (auto& [id, c] : _characters)
            if (c.timeline)
                countUses(*c.timeline);

        tinyxml2::XMLDocument doc;
        tinyxml2::XMLElement* root = doc.NewElement("DOMDocument");
        doc.InsertEndChild(root);
        root->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
        root->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
        root->SetAttribute("backgroundColor", hexColor(_background).c_str());
        root->SetAttribute("width", std::max(1, _width));
        root->SetAttribute("height", std::max(1, _height));
        root->SetAttribute("frameRate", number(_frameRate > 0 ? _frameRate : 24.0).c_str());
        root->SetAttribute("currentTimeline", "1");
        root->SetAttribute("xflVersion", "23.0");
        root->SetAttribute("creatorInfo", "Phoenix (imported from SWF)");

        // The main timeline first: it names what it uses, which writes the
        // symbols as it goes.
        tinyxml2::XMLElement* timelines = doc.NewElement("timelines");
        if (_scenes.size() > 1)
        {
            // A scene each, as the SWF's scene list cuts the main timeline.
            std::vector<std::pair<uint32_t, std::string>> scenes = _scenes;
            std::stable_sort(scenes.begin(), scenes.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            for (size_t s = 0; s < scenes.size(); ++s)
            {
                const int first = static_cast<int>(scenes[s].first);
                const int end = s + 1 < scenes.size() ? static_cast<int>(scenes[s + 1].first) : _main->frameCount;
                if (end <= first && s + 1 < scenes.size())
                    continue;
                const TimelineData slice = sliceOf(*_main, first, end);
                const std::string name = scenes[s].second.empty() ? "Scene " + std::to_string(s + 1) : scenes[s].second;
                timelines->InsertEndChild(timelineElement(doc, name, slice, false));
            }
        }
        else
            timelines->InsertEndChild(timelineElement(doc, "Scene 1", *_main, false));
        // Symbols nothing on a timeline uses still come back, exported ones
        // above all.
        for (auto& [id, c] : _characters)
            if ((c.kind == Character::Kind::Sprite || c.kind == Character::Kind::Button) && !_symbolFiles.count(id))
                symbolOf(id);

        tinyxml2::XMLElement* folders = doc.NewElement("folders");
        std::set<std::string> allFolders;
        for (const std::string& folder : _folders)
            for (size_t at = folder.find('/'); ; at = folder.find('/', at + 1))
            {
                allFolders.insert(folder.substr(0, at));
                if (at == std::string::npos)
                    break;
            }
        for (const std::string& folder : allFolders)
        {
            tinyxml2::XMLElement* item = doc.NewElement("DOMFolderItem");
            item->SetAttribute("name", folder.c_str());
            item->SetAttribute("isExpanded", "false");
            folders->InsertEndChild(item);
        }
        if (!allFolders.empty())
            root->InsertEndChild(folders);

        tinyxml2::XMLElement* media = doc.NewElement("media");
        for (auto& [id, c] : _characters)
        {
            if (c.kind == Character::Kind::Video)
            {
                // Its size, rate and length, without its pictures.
                static const char* types[7] = {"", "", "h263 media", "screen share", "on2 vp6", "on2 vp6 alpha",
                    "screen share v2"};
                const double rate = _frameRate > 0 ? _frameRate : 24.0;
                tinyxml2::XMLElement* item = doc.NewElement("DOMVideoItem");
                item->SetAttribute("name", nameOf(id).c_str());
                if (c.codec >= 2 && c.codec <= 6)
                    item->SetAttribute("videoType", types[c.codec]);
                item->SetAttribute("fps", number(rate).c_str());
                item->SetAttribute("width", c.width);
                item->SetAttribute("height", c.height);
                item->SetAttribute("length", number(c.videoFrames / rate).c_str());
                media->InsertEndChild(item);
                leftOut("video pictures (each video is an item of its size and length)");
                continue;
            }
            if (c.data.empty())
                continue;
            if (c.kind == Character::Kind::Bitmap)
            {
                const std::string dat = dataName();
                files["bin/" + dat] = c.data;
                tinyxml2::XMLElement* item = doc.NewElement("DOMBitmapItem");
                item->SetAttribute("name", nameOf(id).c_str());
                if (!c.linkage.empty())
                {
                    // A class's name for ActionScript 3; an identifier for
                    // BitmapData.loadBitmap in ActionScript 2.
                    const bool actionScript3 = _abc > 0 || (_version >= 9 && _scripts == 0);
                    item->SetAttribute("linkageExportForAS", "true");
                    item->SetAttribute(actionScript3 ? "linkageClassName" : "linkageIdentifier", c.linkage.c_str());
                }
                item->SetAttribute("allowSmoothing", "true");
                if (!c.jpeg)
                    item->SetAttribute("originalCompressionType", "lossless");
                item->SetAttribute("href", nameOf(id).c_str());
                item->SetAttribute("bitmapDataHRef", dat.c_str());
                item->SetAttribute("frameRight", c.width * 20);
                item->SetAttribute("frameBottom", c.height * 20);
                // The format marks a JPEG's data with isJPEG.
                if (c.jpeg)
                    item->SetAttribute("isJPEG", "true");
                media->InsertEndChild(item);
            }
            else if (c.kind == Character::Kind::Sound)
                _soundItems.push_back(c), _soundItems.back().name = nameOf(id);
        }
        for (const Character& sound : _soundItems)
        {
            const std::string dat = dataName();
            files["bin/" + dat] = sound.data;
            tinyxml2::XMLElement* item = doc.NewElement("DOMSoundItem");
            item->SetAttribute("name", sound.name.c_str());
            item->SetAttribute("soundDataHRef", dat.c_str());
            item->SetAttribute("format", sound.soundFormat.c_str());
            item->SetAttribute("sampleCount", sound.sampleCount);
            media->InsertEndChild(item);
        }
        if (!media->NoChildren())
            root->InsertEndChild(media);

        tinyxml2::XMLElement* symbols = doc.NewElement("symbols");
        for (const auto& [href, type] : _includes)
        {
            tinyxml2::XMLElement* include = doc.NewElement("Include");
            include->SetAttribute("href", href.c_str());
            include->SetAttribute("loadImmediate", "false");
            symbols->InsertEndChild(include);
        }
        if (!symbols->NoChildren())
            root->InsertEndChild(symbols);
        root->InsertEndChild(timelines);
        files["DOMDocument.xml"] = printed(doc);
        writeReport();
    }

    void writeReport()
    {
        std::string text = "Imported from an SWF movie: its frames come baked, with no layer names or library "
                           "folders of the original.";
        if (_tweens > 0 || _shapeTweens > 0)
        {
            const auto count = [](int n, const char* what) {
                return std::to_string(n) + " " + what + (n == 1 ? "" : "s");
            };
            text += " Frames that change as a tween would came back as ";
            if (_tweens > 0)
                text += count(_tweens, "classic tween") + (_shapeTweens > 0 ? " and " : "");
            if (_shapeTweens > 0)
                text += count(_shapeTweens, "shape tween");
            text += "; the rest keep a keyframe wherever anything changes.";
        }
        if (_scripts > 0 || _abc > 0)
            text += " Its ActionScript was compiled and cannot be turned back into source; frames that had "
                    "scripts carry a comment saying so. Test Movie runs it as compiled.";
        if (!_unread.empty())
        {
            text += " Left out:";
            bool first = true;
            for (const auto& [what, count] : _unread)
            {
                text += (first ? " " : "; ") + what + (count > 1 ? " (" + std::to_string(count) + ")" : "");
                first = false;
            }
            text += ".";
        }
        report = text;
    }
};

} // namespace

void setImageDecoder(ImageDecoder decoder)
{
    imageDecoder() = std::move(decoder);
}

bool isSwf(const unsigned char* head, size_t size)
{
    return size >= 3 && (head[0] == 'F' || head[0] == 'C' || head[0] == 'Z') && head[1] == 'W' && head[2] == 'S';
}

bool convert(const std::vector<uint8_t>& swf, std::map<std::string, std::vector<uint8_t>>& files, std::string& error,
    std::string* report, const Options& options)
{
    Converter converter(options);
    if (!converter.run(swf, files, error))
        return false;
    if (report)
        *report = converter.report;
    return true;
}

} // namespace SwfImport
