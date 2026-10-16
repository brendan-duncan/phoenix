#include "postscript_import.h"

#include "pdf_objects.h"

#include "zlib.h"
#include "zstd.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace fla {

namespace vectors {

namespace {

// ---------------------------------------------------------------- shared

Color gray(double v, double alpha = 1.0)
{
    const auto c = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    return Color{c, c, c, static_cast<uint8_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0))};
}

Color rgb(double r, double g, double b)
{
    const auto c = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return Color{c(r), c(g), c(b), 255};
}

Color cmyk(double c, double m, double y, double k)
{
    return rgb((1.0 - c) * (1.0 - k), (1.0 - m) * (1.0 - k), (1.0 - y) * (1.0 - k));
}

std::vector<uint8_t> inflateAll(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> out;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return out;
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    uint8_t buffer[65536];
    for (;;)
    {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        const int status = ::inflate(&stream, Z_NO_FLUSH);
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
        // Past a quarter of a gigabyte the data is damage, or a bomb.
        if (status != Z_OK || out.size() > (size_t(1) << 28))
            break;
        if (stream.avail_in == 0 && stream.avail_out != 0)
            break;
    }
    inflateEnd(&stream);
    return out;
}

/// Zstandard frames, one after another, decompressed: as much as could be
/// read.
std::vector<uint8_t> zstdAll(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> out;
    ZSTD_DStream* stream = ZSTD_createDStream();
    if (!stream)
        return out;
    // A window past 128 MB is more than any real file asks for.
    ZSTD_DCtx_setParameter(stream, ZSTD_d_windowLogMax, 27);
    ZSTD_inBuffer in{data, size, 0};
    std::vector<uint8_t> buffer(ZSTD_DStreamOutSize());
    for (;;)
    {
        const size_t before = in.pos;
        ZSTD_outBuffer piece{buffer.data(), buffer.size(), 0};
        const size_t status = ZSTD_decompressStream(stream, &piece, &in);
        if (ZSTD_isError(status))
            break;
        out.insert(out.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(piece.pos));
        // Past a quarter of a gigabyte the data is damage, or a bomb.
        if (out.size() > (size_t(1) << 28))
            break;
        // Done once the input is used and nothing more is held back; stuck
        // on bytes after the last frame that are not one.
        if ((in.pos == in.size && piece.pos < piece.size) || (piece.pos == 0 && in.pos == before))
            break;
    }
    ZSTD_freeDStream(stream);
    return out;
}

/// The PostScript of an EPS with a binary header (a TIFF or metafile
/// preview beside it), or the bytes as they are.
std::string postScriptOf(const std::vector<uint8_t>& bytes)
{
    if (bytes.size() >= 30 && bytes[0] == 0xC5 && bytes[1] == 0xD0 && bytes[2] == 0xD3 && bytes[3] == 0xC6)
    {
        const auto u32 = [&bytes](size_t at) {
            return static_cast<size_t>(bytes[at]) | (static_cast<size_t>(bytes[at + 1]) << 8) |
                (static_cast<size_t>(bytes[at + 2]) << 16) | (static_cast<size_t>(bytes[at + 3]) << 24);
        };
        const size_t start = u32(4);
        const size_t length = u32(8);
        if (start < bytes.size())
            return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), start + length)));
    }
    return std::string(bytes.begin(), bytes.end());
}

/// The value of a DSC comment ("%%BoundingBox:"), the first one that is
/// not "(atend)", else the last.
std::string comment(const std::string& text, const char* key)
{
    std::string last;
    size_t at = 0;
    const size_t header = std::min(text.size(), static_cast<size_t>(1) << 20);
    while ((at = text.find(key, at)) != std::string::npos)
    {
        const size_t start = at + std::strlen(key);
        const size_t end = text.find_first_of("\r\n", start);
        std::string value = text.substr(start, (end == std::string::npos ? text.size() : end) - start);
        value.erase(0, value.find_first_not_of(" \t:"));
        if (value.find("atend") == std::string::npos && !value.empty() && at < header)
            return value;
        if (value.find("atend") == std::string::npos)
            last = value;
        at = start;
    }
    return last;
}

/// The page: HiResBoundingBox, else BoundingBox, else US Letter.
void boundingBox(const std::string& text, double box[4])
{
    box[0] = 0.0;
    box[1] = 0.0;
    box[2] = 612.0;
    box[3] = 792.0;
    for (const char* key : {"%%HiResBoundingBox", "%%BoundingBox"})
    {
        const std::string value = comment(text, key);
        double v[4];
        if (!value.empty() && std::sscanf(value.c_str(), "%lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3]) == 4 &&
            v[2] > v[0] && v[3] > v[1])
        {
            for (int i = 0; i < 4; ++i)
                box[i] = v[i];
            return;
        }
    }
}

bool isIllustrator(const std::string& text)
{
    const size_t header = std::min(text.size(), static_cast<size_t>(65536));
    const std::string head = text.substr(0, header);
    return head.find("%AI5_FileFormat") != std::string::npos || head.find("%AI3_") != std::string::npos ||
        head.find("Creator: Adobe Illustrator") != std::string::npos;
}

std::vector<uint8_t> ascii85(const std::string& text)
{
    std::vector<uint8_t> out;
    uint32_t group = 0;
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '~')
            break;
        if (std::isspace(c))
            continue;
        if (c == 'z' && count == 0)
        {
            out.insert(out.end(), 4, 0);
            continue;
        }
        if (c < '!' || c > 'u')
            continue;
        group = group * 85 + (c - '!');
        if (++count == 5)
        {
            for (int s = 3; s >= 0; --s)
                out.push_back(static_cast<uint8_t>((group >> (s * 8)) & 0xFF));
            group = 0;
            count = 0;
        }
    }
    if (count > 1)
    {
        for (int k = count; k < 5; ++k)
            group = group * 85 + 84;
        for (int s = 3; s >= 5 - count; --s)
            out.push_back(static_cast<uint8_t>((group >> (s * 8)) & 0xFF));
    }
    return out;
}

/// The native Illustrator data an Illustrator 9 (or later) EPS keeps after
/// its printing PostScript, between %AI9_PrivateDataBegin and its end:
/// as text, or after %AI9_DataStream as base-85 lines (each behind a "%"
/// so printers skip it) of zlib data. Empty when the file has none.
std::string illustratorPrivateText(const std::string& text)
{
    const size_t begin = text.find("%AI9_PrivateDataBegin");
    if (begin == std::string::npos)
        return {};
    size_t end = text.find("%AI9_PrivateDataEnd", begin);
    if (end == std::string::npos)
        end = text.size();
    const size_t stream = text.find("%AI9_DataStream", begin);
    if (stream == std::string::npos || stream > end)
        return text.substr(begin, end - begin);
    std::string encoded;
    size_t at = text.find_first_of("\r\n", stream);
    while (at != std::string::npos && at < end)
    {
        while (at < end && (text[at] == '\r' || text[at] == '\n'))
            ++at;
        const size_t lineEnd = std::min(end, text.find_first_of("\r\n", at));
        // Each line behind its "%", base-85 (which may itself start "%").
        if (at < lineEnd && text[at] == '%')
            encoded.append(text, at + 1, lineEnd - at - 1);
        else if (at < lineEnd)
            break;
        at = lineEnd;
    }
    const std::vector<uint8_t> packed = ascii85(encoded);
    const std::vector<uint8_t> data = inflateAll(packed.data(), packed.size());
    return std::string(data.begin(), data.end());
}

void note(Artwork& art, const std::string& text)
{
    if (std::find(art.notes.begin(), art.notes.end(), text) == art.notes.end())
        art.notes.push_back(text);
}

/// Paths in PostScript space, taken to the stage: y down from the page's
/// top, x from its left.
struct Builder
{
    Artwork& art;
    double left = 0.0;
    double top = 0.0;
    /// The art's own space onto the page: a pattern tile's, else none.
    std::array<double, 6> m{1, 0, 0, 1, 0, 0};
    std::vector<Subpath> subpaths;
    Subpath current;

    Point stage(double x, double y) const
    {
        return Point(m[0] * x + m[2] * y + m[4] - left, top - (m[1] * x + m[3] * y + m[5]));
    }
    /// A direction in the art's space as one on the stage.
    Point vector(double x, double y) const { return Point(m[0] * x + m[2] * y, -(m[1] * x + m[3] * y)); }
    double scale() const { return std::sqrt(std::fabs(m[0] * m[3] - m[1] * m[2])); }

    void finishSubpath()
    {
        if (current.segments.size() > 1)
            subpaths.push_back(current);
        current = Subpath();
    }
    void moveTo(const Point& p)
    {
        finishSubpath();
        current.segments.push_back(Segment{Segment::Kind::Move, {p}});
    }
    void lineTo(const Point& p)
    {
        if (current.segments.empty())
            current.segments.push_back(Segment{Segment::Kind::Move, {p}});
        else
            current.segments.push_back(Segment{Segment::Kind::Line, {p}});
    }
    void cubicTo(const Point& a, const Point& b, const Point& p)
    {
        if (current.segments.empty())
            current.segments.push_back(Segment{Segment::Kind::Move, {a}});
        current.segments.push_back(Segment{Segment::Kind::Cubic, {a, b, p}});
    }
    void close()
    {
        if (!current.segments.empty())
            current.closed = true;
    }
    std::vector<Subpath> take(bool closeAll)
    {
        finishSubpath();
        std::vector<Subpath> out;
        out.swap(subpaths);
        if (closeAll)
            for (Subpath& s : out)
                s.closed = true;
        return out;
    }
    void emit(std::vector<Subpath> paths, std::optional<Color> fill, std::optional<Color> stroke, double width)
    {
        if (paths.empty() || (!fill && !stroke))
            return;
        Painted painted;
        painted.subpaths = std::move(paths);
        painted.fill = fill;
        painted.stroke = stroke;
        painted.strokeWidth = std::max(0.1, width * scale());
        art.order.emplace_back(Artwork::Kind::Path, art.paths.size());
        art.paths.push_back(std::move(painted));
    }
};

/// Latin-1 text as UTF-8.
std::string latin1(const std::string& text)
{
    std::string out;
    for (const unsigned char c : text)
    {
        if (c < 0x80)
            out += static_cast<char>(c);
        else
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

/// Points along \a subpaths, curves cut into short lines.
std::vector<Point> flattened(const std::vector<Subpath>& subpaths)
{
    std::vector<Point> out;
    for (const Subpath& s : subpaths)
        for (const Segment& seg : s.segments)
        {
            if (seg.kind != Segment::Kind::Cubic || out.empty() || seg.points.size() < 3)
            {
                if (!seg.points.empty())
                    out.push_back(seg.points.back());
                continue;
            }
            const Point p0 = out.back();
            for (int k = 1; k <= 16; ++k)
            {
                const double t = k / 16.0, u = 1.0 - t;
                const Point& a = seg.points[0];
                const Point& b = seg.points[1];
                const Point& c = seg.points[2];
                out.emplace_back(u * u * u * p0.x + 3 * u * u * t * a.x + 3 * u * t * t * b.x + t * t * t * c.x,
                    u * u * u * p0.y + 3 * u * u * t * a.y + 3 * u * t * t * b.y + t * t * t * c.y);
            }
        }
    return out;
}

/// Text set along a path on the stage, a glyph a piece, each turned to the
/// path's direction where it sits, from \a start along it.
void textOnPath(Artwork& art, const Text& style, const std::string& text, const std::vector<Point>& path, double start)
{
    if (path.size() < 2)
        return;
    double along = start;
    size_t i = 0;
    double walked = 0.0;
    for (size_t k = 0; k < text.size();)
    {
        // One character (UTF-8).
        const unsigned char lead = static_cast<unsigned char>(text[k]);
        const size_t n = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        const std::string glyph = text.substr(k, n);
        uint32_t u = lead;
        if (n > 1)
        {
            u &= n == 4 ? 0x07 : n == 3 ? 0x0F : 0x1F;
            for (size_t j = 1; j < n && k + j < text.size(); ++j)
                u = (u << 6) | (static_cast<unsigned char>(text[k + j]) & 0x3F);
        }
        k += n;
        const double width = standardGlyphWidth(style.family + (style.bold ? "-Bold" : ""), u) / 1000.0 * style.size;
        // The segment the glyph starts on.
        while (i + 1 < path.size())
        {
            const double length = std::hypot(path[i + 1].x - path[i].x, path[i + 1].y - path[i].y);
            if (walked + length >= along || i + 2 == path.size())
                break;
            walked += length;
            ++i;
        }
        const Point& a = path[i];
        const Point& b = path[i + 1];
        const double length = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
        const double f = (along - walked) / length;
        if (f > 1.0 + 1e-6 && i + 2 == path.size())
            break;
        const double ux = (b.x - a.x) / length, uy = (b.y - a.y) / length;
        if (glyph != " ")
        {
            Text item = style;
            item.text = glyph;
            item.placement = Placement{ux, uy, -uy, ux, a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
            art.order.emplace_back(Artwork::Kind::Text, art.texts.size());
            art.texts.push_back(item);
        }
        along += width;
    }
}

// ---------------------------------------------------------------- Illustrator

/// A gradient Illustrator defines once and fills paths with by name.
struct AiGradient
{
    bool radial = false;
    std::vector<Gradient::Stop> stops;
};

/// Numbers and strings of one line of Illustrator text, in order.
std::vector<std::string> words(const std::string& line)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size())
    {
        if (std::isspace(static_cast<unsigned char>(line[i])))
        {
            ++i;
            continue;
        }
        if (line[i] == '(')
        {
            std::string text = "(";
            int depth = 1;
            ++i;
            while (i < line.size() && depth > 0)
            {
                if (line[i] == '\\' && i + 1 < line.size())
                {
                    // As the reader decodes them, so names match.
                    const char e = line[i + 1];
                    i += 2;
                    switch (e)
                    {
                    case 'n': text += '\n'; break;
                    case 'r': text += '\r'; break;
                    case 't': text += '\t'; break;
                    case 'b': text += '\b'; break;
                    case 'f': text += '\f'; break;
                    default:
                        if (e >= '0' && e <= '7')
                        {
                            int value = e - '0';
                            for (int k = 0; k < 2 && i < line.size() && line[i] >= '0' && line[i] <= '7'; ++k)
                                value = value * 8 + (line[i++] - '0');
                            text += static_cast<char>(value & 0xFF);
                        }
                        else
                            text += e;
                    }
                    continue;
                }
                if (line[i] == '(')
                    ++depth;
                else if (line[i] == ')' && --depth == 0)
                {
                    ++i;
                    break;
                }
                text += line[i++];
            }
            out.push_back(text);
            continue;
        }
        const size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        out.push_back(line.substr(start, i - start));
    }
    return out;
}

/// A gradient stop's color from its words: the color's numbers (and a
/// custom color's name and tint), then its style. Style 0 is gray, 1 CMYK,
/// 2 CMYK and RGB, 3 a custom CMYK color, 4 a custom RGB color.
std::optional<Color> stopColor(const std::vector<std::string>& w, size_t styleAt)
{
    const auto n = [&w](size_t i) { return i < w.size() ? std::atof(w[i].c_str()) : 0.0; };
    const int style = static_cast<int>(n(styleAt));
    if (style == 0 && styleAt >= 1)
        return gray(n(styleAt - 1));
    if (style == 1 && styleAt >= 4)
        return cmyk(n(styleAt - 4), n(styleAt - 3), n(styleAt - 2), n(styleAt - 1));
    if (style == 2 && styleAt >= 7)
        return rgb(n(styleAt - 3), n(styleAt - 2), n(styleAt - 1));
    if ((style == 3 || style == 4) && styleAt >= 6)
    {
        // ... (name) tint style: the tint counts down from the full color.
        const double strength = 1.0 - std::clamp(n(styleAt - 1), 0.0, 1.0);
        if (style == 4)
        {
            const double r = n(styleAt - 5), g = n(styleAt - 4), b = n(styleAt - 3);
            return rgb(1 - (1 - r) * strength, 1 - (1 - g) * strength, 1 - (1 - b) * strength);
        }
        if (styleAt >= 6)
            return cmyk(n(styleAt - 6) * strength, n(styleAt - 5) * strength, n(styleAt - 4) * strength,
                n(styleAt - 3) * strength);
    }
    return std::nullopt;
}

/// The gradients a file defines, by name, from their definitions'
/// "%_BS" (or "%_ ... Bs") stop lines.
std::map<std::string, AiGradient> gradientDefinitions(const std::string& text)
{
    std::map<std::string, AiGradient> out;
    size_t at = 0;
    while ((at = text.find("%AI5_BeginGradient", at)) != std::string::npos)
    {
        const size_t end = text.find("%AI5_EndGradient", at);
        if (end == std::string::npos)
            break;
        const std::string block = text.substr(at, end - at);
        at = end;
        std::string name;
        AiGradient gradient;
        std::vector<std::pair<double, Color>> commentStops, lineStops;
        size_t lineStart = 0;
        while (lineStart < block.size())
        {
            size_t lineEnd = block.find_first_of("\r\n", lineStart);
            if (lineEnd == std::string::npos)
                lineEnd = block.size();
            std::string line = block.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 1;
            std::vector<std::string> w = words(line);
            if (w.size() >= 4 && w.back() == "Bd")
            {
                name = w[0].size() > 1 ? w[0].substr(1) : std::string();
                gradient.radial = std::atoi(w[1].c_str()) == 1;
                continue;
            }
            std::vector<std::pair<double, Color>>* into = nullptr;
            const size_t mark = line.find("%_BS");
            const size_t markOld = line.find("%_Bs");
            if (mark != std::string::npos || markOld != std::string::npos)
            {
                w = words(line.substr(0, mark != std::string::npos ? mark : markOld));
                into = &commentStops;
            }
            else if (line.rfind("%_", 0) == 0 && !w.empty() && w.back() == "Bs")
            {
                w = words(line.substr(2));
                w.pop_back();
                into = &lineStops;
            }
            if (!into || w.size() < 4)
                continue;
            // ... style midpoint rampPoint
            const double ramp = std::atof(w[w.size() - 1].c_str());
            if (const auto color = stopColor(w, w.size() - 3))
                into->emplace_back(ramp, *color);
        }
        std::vector<std::pair<double, Color>>& stops = commentStops.empty() ? lineStops : commentStops;
        std::sort(stops.begin(), stops.end(),
            [](const std::pair<double, Color>& a, const std::pair<double, Color>& b) { return a.first < b.first; });
        for (const auto& [ramp, color] : stops)
            gradient.stops.push_back(Gradient::Stop{std::clamp(ramp / 100.0, 0.0, 1.0), color});
        if (!name.empty() && !gradient.stops.empty())
            out[name] = gradient;
    }
    return out;
}

// ---------------------------------------------------------------- text engine

/// A value of the text engine's data (Illustrator 11 and later keep their
/// text so): dictionaries, arrays, numbers, names and strings.
struct TeValue
{
    enum class Kind
    {
        Null,
        Number,
        Bool,
        String,
        Name,
        Array,
        Dict
    };
    Kind kind = Kind::Null;
    double number = 0.0;
    /// A string's bytes (UTF-16 ones as UTF-16), or a name without its
    /// slash.
    std::string text;
    std::vector<TeValue> items;
    std::vector<std::pair<std::string, TeValue>> entries;

    const TeValue* get(const char* key) const
    {
        for (const auto& [k, v] : entries)
            if (k == key)
                return &v;
        return nullptr;
    }
    /// The value along \a path of keys; null when one is missing.
    const TeValue* at(std::initializer_list<const char*> path) const
    {
        const TeValue* v = this;
        for (const char* key : path)
            if (!v || !(v = v->get(key)))
                return nullptr;
        return v;
    }
    double num(const char* key, double fallback) const
    {
        const TeValue* v = get(key);
        return v && v->kind == Kind::Number ? v->number : fallback;
    }
    std::vector<double> numbers() const
    {
        std::vector<double> out;
        for (const TeValue& item : items)
            out.push_back(item.kind == Kind::Number ? item.number : 0.0);
        return out;
    }
};

/// Reads the text engine's data, held to what a real file needs: nesting,
/// how many values, and the time that takes.
class TeParser
{
public:
    explicit TeParser(const std::string& data)
        : _data(data)
    {}

    TeValue document()
    {
        TeValue top;
        top.kind = TeValue::Kind::Dict;
        while (skip())
        {
            TeValue key = value(0);
            if (key.kind != TeValue::Kind::Name || !skip())
                break;
            top.entries.emplace_back(key.text, value(0));
        }
        return top;
    }

private:
    bool skip()
    {
        while (_at < _data.size())
        {
            const char c = _data[_at];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0')
                ++_at;
            else if (c == '%')
                while (_at < _data.size() && _data[_at] != '\r' && _data[_at] != '\n')
                    ++_at;
            else
                return true;
        }
        return false;
    }

    TeValue value(int depth)
    {
        TeValue v;
        if (!skip() || ++_count > 4000000 || depth > 64)
        {
            _at = _data.size();
            return v;
        }
        const char c = _data[_at];
        if (c == '<' && _at + 1 < _data.size() && _data[_at + 1] == '<')
        {
            _at += 2;
            v.kind = TeValue::Kind::Dict;
            while (skip())
            {
                if (_data.compare(_at, 2, ">>") == 0)
                {
                    _at += 2;
                    break;
                }
                TeValue key = value(depth + 1);
                if (key.kind != TeValue::Kind::Name)
                    continue;
                v.entries.emplace_back(key.text, value(depth + 1));
            }
            return v;
        }
        if (c == '[')
        {
            ++_at;
            v.kind = TeValue::Kind::Array;
            while (skip())
            {
                if (_data[_at] == ']')
                {
                    ++_at;
                    break;
                }
                v.items.push_back(value(depth + 1));
            }
            return v;
        }
        if (c == '(')
        {
            ++_at;
            v.kind = TeValue::Kind::String;
            int nest = 1;
            while (_at < _data.size())
            {
                char d = _data[_at++];
                if (d == '\\' && _at < _data.size())
                {
                    d = _data[_at++];
                    switch (d)
                    {
                    case 'n': d = '\n'; break;
                    case 'r': d = '\r'; break;
                    case 't': d = '\t'; break;
                    case 'b': d = '\b'; break;
                    case 'f': d = '\f'; break;
                    default: break;
                    }
                    v.text += d;
                    continue;
                }
                if (d == '(')
                    ++nest;
                else if (d == ')' && --nest == 0)
                    break;
                v.text += d;
            }
            return v;
        }
        if (c == '/')
        {
            ++_at;
            v.kind = TeValue::Kind::Name;
            const size_t start = _at;
            while (_at < _data.size() && std::strchr(" \t\r\n/[]()<>", _data[_at]) == nullptr)
                ++_at;
            v.text = _data.substr(start, _at - start);
            return v;
        }
        const size_t start = _at;
        while (_at < _data.size() && std::strchr(" \t\r\n/[]()<>", _data[_at]) == nullptr)
            ++_at;
        if (_at == start)
        {
            ++_at;
            return v;
        }
        const std::string word = _data.substr(start, _at - start);
        if (word == "true" || word == "false")
        {
            v.kind = TeValue::Kind::Bool;
            v.number = word == "true" ? 1.0 : 0.0;
            return v;
        }
        char* end = nullptr;
        const double n = std::strtod(word.c_str(), &end);
        if (end && *end == '\0')
        {
            v.kind = TeValue::Kind::Number;
            v.number = std::isfinite(n) ? n : 0.0;
        }
        return v;
    }

    const std::string& _data;
    size_t _at = 0;
    size_t _count = 0;
};

/// A text engine string as UTF-16 units: big-endian after its byte order
/// mark, else its bytes.
std::u16string utf16(const std::string& bytes)
{
    std::u16string out;
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE && static_cast<unsigned char>(bytes[1]) == 0xFF)
    {
        for (size_t i = 2; i + 1 < bytes.size(); i += 2)
            out += static_cast<char16_t>(static_cast<unsigned char>(bytes[i]) << 8 | static_cast<unsigned char>(bytes[i + 1]));
        return out;
    }
    for (const unsigned char c : bytes)
        out += static_cast<char16_t>(c);
    return out;
}

std::string utf8(const std::u16string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        uint32_t c = text[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
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
    return out;
}

/// Illustrator 11's text: its stories, the frames they flow through and
/// the lines the text engine laid them in, read from the document's
/// AI11TextDocument data.
struct TextDocument
{
    struct Style
    {
        std::string font = "Helvetica";
        double size = 12.0;
        double scale = 1.0;
        Color color{0, 0, 0, 255};
    };
    struct Frame
    {
        int type = 0; // point, area, along a path
        std::array<double, 6> matrix{1, 0, 0, 1, 0, 0};
        std::vector<double> points;
    };
    struct Story
    {
        std::u16string text;
        /// Style runs: where each starts and its style.
        std::vector<std::pair<size_t, Style>> styles;
        /// Tab stops (position, type) by paragraph start.
        std::vector<std::pair<size_t, std::vector<std::pair<double, int>>>> tabs;
        std::vector<int> frames;
        /// Each line laid out: its frame, the character it starts at and how
        /// many, and where on the frame it starts (frame space, y down).
        struct Line
        {
            int frame = -1;
            size_t first = 0, count = 0;
            double x = 0.0, y = 0.0;
            double frameX = 0.0;
        };
        std::vector<Line> lines;

        const Style& styleAt(size_t index) const
        {
            static const Style plain;
            const Style* found = &plain;
            for (const auto& [start, style] : styles)
                if (start <= index)
                    found = &style;
            return *found;
        }
    };
    std::vector<std::string> fonts;
    std::vector<Frame> frames;
    std::vector<Story> stories;
    double originX = 0.0, originY = 0.0;
    bool read = false;
};

/// The style a style sheet's features give, over \a base.
TextDocument::Style styleOf(const TeValue* features, TextDocument::Style base, const std::vector<std::string>& fonts)
{
    if (!features)
        return base;
    if (const TeValue* font = features->get("Font"); font && font->kind == TeValue::Kind::Number)
    {
        const size_t index = static_cast<size_t>(std::max(0.0, font->number));
        if (index < fonts.size())
            base.font = fonts[index];
    }
    base.size = features->num("FontSize", base.size);
    base.scale = features->num("HorizontalScale", base.scale);
    if (const TeValue* color = features->at({"FillColor", "Color"}))
    {
        const std::vector<double> v = color->get("Values") ? color->get("Values")->numbers() : std::vector<double>();
        const int type = static_cast<int>(color->num("Type", 1));
        // A, then RGB or CMYK; or gray.
        if (type == 1 && v.size() >= 4)
            base.color = rgb(v[1], v[2], v[3]);
        else if (type == 2 && v.size() >= 5)
            base.color = cmyk(v[1], v[2], v[3], v[4]);
        else if (v.size() == 2)
            base.color = gray(v[1]);
    }
    return base;
}

void readTextDocument(const std::string& native, TextDocument& doc)
{
    // The data: base-85 lines, each behind a "%", after its key.
    const size_t key = native.find("/AI11TextDocument");
    if (key == std::string::npos)
        return;
    const size_t end = native.find("~>", key);
    if (end == std::string::npos)
        return;
    std::string encoded;
    size_t at = native.find_first_of("\r\n", key);
    while (at != std::string::npos && at < end)
    {
        while (at < end && (native[at] == '\r' || native[at] == '\n'))
            ++at;
        const size_t lineEnd = std::min(end, native.find_first_of("\r\n", at));
        if (at < lineEnd && native[at] == '%')
            encoded.append(native, at + 1, lineEnd - at - 1);
        else if (at < lineEnd)
            encoded.append(native, at, lineEnd - at);
        at = lineEnd;
    }
    std::vector<uint8_t> bytes = ascii85(encoded);
    const std::string head = native.substr(key, std::min<size_t>(80, end - key));
    if (head.find("FlateDecode") != std::string::npos)
        bytes = inflateAll(bytes.data(), bytes.size());
    // Where the ruler's origin was: the document's text sits from it.
    const size_t ruler = native.find("/RulerOrigin", end);
    if (ruler != std::string::npos && ruler - end < 400)
    {
        const size_t lineStart = native.find_last_of("\r\n", ruler);
        double x = 0.0, y = 0.0;
        if (std::sscanf(native.c_str() + (lineStart == std::string::npos ? end + 2 : lineStart + 1), "%lf %lf", &x, &y) == 2)
        {
            doc.originX = x;
            doc.originY = y;
        }
    }
    const std::string data(bytes.begin(), bytes.end());
    TeParser parser(data);
    const TeValue root = parser.document();
    const TeValue* resources = root.get("DocumentResources");
    if (!resources)
        return;
    if (const TeValue* fonts = resources->at({"FontSet", "Resources"}))
        for (const TeValue& font : fonts->items)
        {
            const TeValue* name = font.at({"Resource", "Identifier", "Name"});
            doc.fonts.push_back(name ? utf8(utf16(name->text)) : std::string("Helvetica"));
        }
    std::vector<TextDocument::Style> sheets;
    if (const TeValue* set = resources->at({"StyleSheetSet", "Resources"}))
        for (const TeValue& sheet : set->items)
            sheets.push_back(styleOf(sheet.at({"Resource", "Features"}), TextDocument::Style(), doc.fonts));
    if (const TeValue* frames = resources->at({"TextFrameSet", "Resources"}))
        for (const TeValue& item : frames->items)
        {
            TextDocument::Frame frame;
            if (const TeValue* r = item.get("Resource"))
            {
                if (const TeValue* points = r->at({"Bezier", "Points"}))
                    frame.points = points->numbers();
                if (const TeValue* data = r->get("Data"))
                {
                    frame.type = static_cast<int>(data->num("Type", 0));
                    if (const TeValue* m = data->get("FrameMatrix"); m && m->items.size() >= 6)
                    {
                        const std::vector<double> n = m->numbers();
                        std::copy(n.begin(), n.begin() + 6, frame.matrix.begin());
                    }
                }
            }
            doc.frames.push_back(frame);
        }
    const TeValue* objects = root.at({"DocumentObjects", "TextObjects"});
    if (!objects)
        return;
    for (const TeValue& object : objects->items)
    {
        TextDocument::Story story;
        if (const TeValue* text = object.at({"Model", "Text"}))
            story.text = utf16(text->text);
        size_t start = 0;
        if (const TeValue* runs = object.at({"Model", "StyleRun", "RunArray"}))
            for (const TeValue& run : runs->items)
            {
                const TeValue* sheet = run.at({"RunData", "StyleSheet"});
                TextDocument::Style base;
                if (sheet)
                {
                    const double parent = sheet->num("Parent", -1);
                    if (parent >= 0 && parent < static_cast<double>(sheets.size()))
                        base = sheets[static_cast<size_t>(parent)];
                }
                story.styles.emplace_back(start, styleOf(sheet ? sheet->get("Features") : nullptr, base, doc.fonts));
                start += static_cast<size_t>(std::max(0.0, run.num("Length", 0)));
            }
        start = 0;
        if (const TeValue* runs = object.at({"Model", "ParagraphRun", "RunArray"}))
            for (const TeValue& run : runs->items)
            {
                std::vector<std::pair<double, int>> stops;
                if (const TeValue* list = run.at({"RunData", "ParagraphSheet", "Features", "TabStops", "TabStops"}))
                    for (const TeValue& stop : list->items)
                        stops.emplace_back(stop.num("TabAdvance", 0), static_cast<int>(stop.num("TabType", 0)));
                story.tabs.emplace_back(start, stops);
                start += static_cast<size_t>(std::max(0.0, run.num("Length", 0)));
            }
        if (const TeValue* frames = object.at({"View", "Frames"}))
            for (const TeValue& frame : frames->items)
                story.frames.push_back(static_cast<int>(frame.num("Resource", -1)));
        // The lines, from the strikes the engine drew: frame, then rows,
        // lines and segments, each placed from the one above, their
        // characters one after another through the story.
        size_t next = 0;
        std::function<void(const TeValue&, double, double, int, double, int)> walk =
            [&](const TeValue& node, double x, double y, int frame, double frameX, int depth) {
                if (depth > 32)
                    return;
                if (const TeValue* origin = node.at({"Transform", "Origin"}); origin && origin->items.size() >= 2)
                {
                    x += origin->items[0].number;
                    y += origin->items[1].number;
                }
                const TeValue* tag = node.get("StreamTag");
                const std::string kind = tag ? tag->text : std::string();
                if (kind == "FrameStrike")
                {
                    frame = static_cast<int>(node.num("Frame", -1));
                    frameX = x;
                }
                if (kind == "Segment")
                {
                    TextDocument::Story::Line line;
                    line.frame = frame;
                    line.first = next;
                    if (const TeValue* mapping = node.get("Mapping"))
                        line.count = static_cast<size_t>(std::clamp(mapping->num("CharacterCount", 0), 0.0, 1e9));
                    next += line.count;
                    line.x = x;
                    line.y = y;
                    line.frameX = frameX;
                    if (story.lines.size() < 100000)
                        story.lines.push_back(line);
                    return;
                }
                if (const TeValue* children = node.get("Children"))
                    for (const TeValue& child : children->items)
                        walk(child, x, y, frame, frameX, depth + 1);
            };
        if (const TeValue* strikes = object.at({"View", "Strikes"}))
            for (const TeValue& strike : strikes->items)
                walk(strike, 0.0, 0.0, -1, 0.0, 0);
        doc.stories.push_back(std::move(story));
    }
    doc.read = true;
}

/// A pattern Illustrator defines once and fills paths with by name: its
/// tile's box and the art that paints it, in its own format.
struct AiPattern
{
    double box[4] = {0, 0, 0, 0};
    std::string art;
};

/// A string literal from \a at in \a text (at its "("), escapes undone;
/// \a at ends past it.
std::string literalAt(const std::string& text, size_t& at, size_t end)
{
    std::string out;
    ++at;
    int depth = 1;
    while (at < end)
    {
        const char d = text[at++];
        if (d == '\\' && at < end)
        {
            const char e = text[at++];
            switch (e)
            {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\r':
            case '\n': break;
            default:
                if (e >= '0' && e <= '7')
                {
                    int value = e - '0';
                    for (int k = 0; k < 2 && at < end && text[at] >= '0' && text[at] <= '7'; ++k)
                        value = value * 8 + (text[at++] - '0');
                    out += static_cast<char>(value & 0xFF);
                }
                else
                    out += e;
            }
            continue;
        }
        if (d == '(')
            ++depth;
        else if (d == ')' && --depth == 0)
            break;
        out += d;
    }
    return out;
}

/// The patterns a file defines, by name: "(name) llx lly urx ury [ ... ] E"
/// between %AI3_BeginPattern and %AI3_EndPattern, the tile's art in the
/// strings inside (each behind @ or &).
std::map<std::string, AiPattern> patternDefinitions(const std::string& text)
{
    std::map<std::string, AiPattern> out;
    size_t at = 0;
    while ((at = text.find("%AI3_BeginPattern", at)) != std::string::npos && out.size() < 10000)
    {
        const size_t end = text.find("%AI3_EndPattern", at);
        if (end == std::string::npos)
            break;
        // The header line: not a comment, a "[" ending it.
        size_t line = text.find_first_of("\r\n", at);
        AiPattern pattern;
        std::string name;
        while (line != std::string::npos && line < end)
        {
            while (line < end && (text[line] == '\r' || text[line] == '\n'))
                ++line;
            const size_t lineEnd = std::min(end, text.find_first_of("\r\n", line));
            if (line < lineEnd && text[line] != '%')
            {
                const std::vector<std::string> w = words(text.substr(line, lineEnd - line));
                if (w.size() >= 6 && !w[0].empty() && w[0][0] == '(' && w.back() == "[")
                {
                    name = w[0].substr(1);
                    for (int k = 0; k < 4; ++k)
                        pattern.box[k] = std::atof(w[1 + static_cast<size_t>(k)].c_str());
                    at = lineEnd;
                    break;
                }
            }
            line = lineEnd;
        }
        if (name.empty())
        {
            at = end + 1;
            continue;
        }
        // The tile's art: each string in turn.
        while (at < end)
        {
            const char c = text[at];
            if (c == '%')
            {
                while (at < end && text[at] != '\r' && text[at] != '\n')
                    ++at;
                continue;
            }
            if (c == '(')
            {
                pattern.art += literalAt(text, at, end);
                pattern.art += '\r';
                continue;
            }
            if (c == ']')
                break;
            ++at;
        }
        out[name] = std::move(pattern);
        at = end + 1;
    }
    return out;
}

/// What all the readers of one file share: what it defines, and the
/// budgets that keep patterns from growing past reason.
struct AiShared
{
    std::map<std::string, AiGradient> gradients;
    std::map<std::string, AiPattern> patterns;
    std::map<std::string, Color> patternColors;
    TextDocument text;
    /// Text written on a Mac, in Mac OS Roman.
    bool macRoman = false;
    size_t tiles = 0;
};

/// Illustrator's native format, read by its operators. Comments are
/// skipped, the "%_" lines among them: those hold the editable form of
/// art (a brush stroke's path, an effect's source) whose printed form
/// follows in plain lines, which are what is drawn.
class IllustratorReader
{
public:
    IllustratorReader(const std::string& text, Artwork& art)
        : _text(text)
        , _art(art)
        , _path{art}
    {}

    void run()
    {
        double box[4];
        boundingBox(_text, box);
        _path.left = box[0];
        _path.top = box[3];
        _art.width = box[2] - box[0];
        _art.height = box[3] - box[1];
        _ownShared.gradients = gradientDefinitions(_text);
        _ownShared.patterns = patternDefinitions(_text);
        readTextDocument(_text, _ownShared.text);
        const std::string head = _text.substr(0, std::min<size_t>(_text.size(), 65536));
        _ownShared.macRoman = head.find("%AI3_DocumentPreview: Macintosh") != std::string::npos ||
            head.find("%%Creator: Adobe Illustrator(TM) 1") != std::string::npos;
        // The art, past the prolog and setup (which define procedures,
        // patterns and symbols rather than draw).
        size_t at = _text.find("%%EndSetup");
        if (at == std::string::npos)
            at = _text.find("%%EndProlog");
        _at = at == std::string::npos ? 0 : at;
        const size_t trailer = _text.find("%%Trailer", _at);
        _end = trailer == std::string::npos ? _text.size() : trailer;
        while (token())
        {
        }
        if (_sawText)
            note(_art, "Some text was left out.");
        if (_sawImage)
            note(_art, "Some images in the file could not be read and were left out.");
        if (_sawGradient)
            note(_art, "Some gradient fills came in as their path's color (a gradient the file does not define).");
        if (_sawClip)
            note(_art, "Clipping masks were not applied: what they hide shows.");
        if (_sawPattern)
            note(_art, "Some pattern fills came in as plain color (too fine to draw tile by tile).");
    }

    /// Paints a pattern's tile: \a art in the pattern's own space, onto the
    /// page by \a toPage, cut to \a clips (stage coordinates).
    static void runTile(const std::string& art, Artwork& into, const Builder& outer, const std::array<double, 6>& toPage,
        std::vector<std::vector<Subpath>> clips, AiShared& shared, int depth)
    {
        IllustratorReader tile(art, into);
        tile._path.left = outer.left;
        tile._path.top = outer.top;
        tile._path.m = toPage;
        tile._outerClips = std::move(clips);
        tile._shared = &shared;
        tile._depth = depth;
        tile._at = 0;
        tile._end = art.size();
        while (tile.token())
        {
        }
    }

private:
    struct Operand
    {
        enum class Kind
        {
            Number,
            String,
            Array
        };
        Kind kind = Kind::Number;
        double number = 0.0;
        std::string text;
        std::vector<double> numbers;
    };

    AiShared& shared() { return _shared ? *_shared : _ownShared; }

    double number(size_t fromTop) const
    {
        if (fromTop >= _stack.size())
            return 0.0;
        const Operand& o = _stack[_stack.size() - 1 - fromTop];
        return o.kind == Operand::Kind::Number ? o.number : 0.0;
    }
    /// How many numbers lie on top of the stack, up to the first non-number.
    size_t numbersOnTop() const
    {
        size_t n = 0;
        while (n < _stack.size() && _stack[_stack.size() - 1 - n].kind == Operand::Kind::Number)
            ++n;
        return n;
    }

    bool token()
    {
        while (_at < _end)
        {
            const char c = _text[_at];
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++_at;
                continue;
            }
            if (c == '%')
            {
                while (_at < _end && _text[_at] != '\r' && _text[_at] != '\n')
                    ++_at;
                continue;
            }
            break;
        }
        if (_at >= _end)
            return false;
        const char c = _text[_at];
        if (c == '(')
        {
            Operand o;
            o.kind = Operand::Kind::String;
            o.text = literalAt(_text, _at, _end);
            _stack.push_back(o);
            return true;
        }
        if (c == '<')
        {
            // Hex data (an image's, a thumbnail's): skipped as one string.
            const size_t close = _text.find('>', _at);
            _at = close == std::string::npos ? _end : close + 1;
            Operand o;
            o.kind = Operand::Kind::String;
            _stack.push_back(o);
            return true;
        }
        if (c == '[')
        {
            ++_at;
            Operand o;
            o.kind = Operand::Kind::Array;
            while (_at < _end && _text[_at] != ']')
            {
                char* end = nullptr;
                const double v = std::strtod(_text.c_str() + _at, &end);
                if (end && end != _text.c_str() + _at)
                {
                    o.numbers.push_back(v);
                    _at = static_cast<size_t>(end - _text.c_str());
                }
                else
                    ++_at;
            }
            ++_at;
            _stack.push_back(o);
            return true;
        }
        if (c == '{')
        {
            int depth = 0;
            while (_at < _end)
            {
                if (_text[_at] == '{')
                    ++depth;
                else if (_text[_at] == '}' && --depth == 0)
                {
                    ++_at;
                    break;
                }
                ++_at;
            }
            _stack.clear();
            return true;
        }
        if (c == '/')
        {
            const size_t start = ++_at;
            while (_at < _end && !std::isspace(static_cast<unsigned char>(_text[_at])) &&
                   std::strchr("()<>[]{}/%", _text[_at]) == nullptr)
                ++_at;
            Operand o;
            o.kind = Operand::Kind::String;
            o.text = _text.substr(start, _at - start);
            _stack.push_back(o);
            return true;
        }
        const size_t start = _at;
        while (_at < _end && !std::isspace(static_cast<unsigned char>(_text[_at])) &&
               std::strchr("()<>[]{}/%", _text[_at]) == nullptr)
            ++_at;
        if (_at == start)
        {
            ++_at;
            return true;
        }
        const std::string word = _text.substr(start, _at - start);
        char* end = nullptr;
        const double v = std::strtod(word.c_str(), &end);
        if (end && *end == '\0' && (std::isdigit(static_cast<unsigned char>(word[0])) || word[0] == '-' ||
                                       word[0] == '.' || word[0] == '+'))
        {
            Operand o;
            o.number = v;
            _stack.push_back(o);
            return true;
        }
        operate(word);
        return true;
    }

    void paint(bool fill, bool stroke, bool closes)
    {
        if (closes)
            _path.close();
        std::vector<Subpath> paths = _path.take(false);
        if (_clipping)
        {
            // A clipping path: not drawn, but what follows in its group is
            // cut to it.
            _clipping = false;
            if (!_clipLevels.empty())
            {
                for (Subpath& s : paths)
                    s.closed = true;
                _clipLevels.back().insert(_clipLevels.back().end(), paths.begin(), paths.end());
            }
            return;
        }
        // The path text runs along, or an area text's frame: not drawn.
        if (_inText)
        {
            _textPath.insert(_textPath.end(), paths.begin(), paths.end());
            return;
        }
        if (_hidden > 0)
            return;
        if (fill)
            for (Subpath& s : paths)
                s.closed = true;
        if (_compound)
        {
            _compoundPaths.insert(_compoundPaths.end(), paths.begin(), paths.end());
            if (fill || stroke)
            {
                _compoundFill = fill ? _fill : std::nullopt;
                _compoundStroke = stroke ? _stroke : std::nullopt;
                _compoundWidth = _width;
            }
            return;
        }
        emit(std::move(paths), fill ? _fill : std::nullopt, stroke ? _stroke : std::nullopt, _width, fill);
    }

    /// The clips in force: the tile's, then the groups'.
    std::vector<std::vector<Subpath>> clips() const
    {
        std::vector<std::vector<Subpath>> out = _outerClips;
        for (const std::vector<Subpath>& level : _clipLevels)
            if (!level.empty())
                out.push_back(level);
        return out;
    }

    /// A painted path, its fill the gradient or pattern in force when there
    /// is one.
    void emit(std::vector<Subpath> paths, std::optional<Color> fill, std::optional<Color> stroke, double width,
        bool filled)
    {
        if (filled && _fillPattern && !paths.empty() && fillWithPattern(paths))
        {
            if (!stroke)
                return;
            fill.reset();
            filled = false;
        }
        const size_t before = _art.paths.size();
        if (filled && _gradient && !_gradient->stops.empty())
            fill = _gradient->stops.front().color;
        _path.emit(std::move(paths), fill, stroke, width);
        if (_art.paths.size() > before)
            _art.paths.back().clips = clips();
        if (filled && _gradient && _art.paths.size() > before)
            _art.paths.back().gradient = *_gradient;
    }

    /// The tiles of the pattern in force over \a area; false (to fill it
    /// with a plain color) when there are too many or it is unknown.
    bool fillWithPattern(std::vector<Subpath> area)
    {
        AiShared& all = shared();
        const auto found = all.patterns.find(_fillPattern->name);
        if (found == all.patterns.end())
            return false;
        const AiPattern& pattern = found->second;
        for (Subpath& s : area)
            s.closed = true;
        const double w = pattern.box[2] - pattern.box[0], h = pattern.box[3] - pattern.box[1];
        // The pattern's space onto the stage: its matrix, the page's.
        const std::array<double, 6>& p = _fillPattern->matrix;
        const std::array<double, 6>& m = _path.m;
        const std::array<double, 6> toPage{m[0] * p[0] + m[2] * p[1], m[1] * p[0] + m[3] * p[1],
            m[0] * p[2] + m[2] * p[3], m[1] * p[2] + m[3] * p[3], m[0] * p[4] + m[2] * p[5] + m[4],
            m[1] * p[4] + m[3] * p[5] + m[5]};
        // The area's box in the pattern's space: stage to page, page to
        // pattern.
        const double det = toPage[0] * toPage[3] - toPage[1] * toPage[2];
        if (std::fabs(det) < 1e-12 || !(w > 1e-6) || !(h > 1e-6) || _depth >= 4)
            return false;
        double i0 = 1e300, i1 = -1e300, j0 = 1e300, j1 = -1e300;
        for (const Subpath& s : area)
            for (const Segment& seg : s.segments)
                for (const Point& pt : seg.points)
                {
                    const double px = pt.x + _path.left, py = _path.top - pt.y;
                    const double dx = px - toPage[4], dy = py - toPage[5];
                    const double u = (toPage[3] * dx - toPage[2] * dy) / det;
                    const double v = (-toPage[1] * dx + toPage[0] * dy) / det;
                    i0 = std::min(i0, (u - pattern.box[2]) / w);
                    i1 = std::max(i1, (u - pattern.box[0]) / w);
                    j0 = std::min(j0, (v - pattern.box[3]) / h);
                    j1 = std::max(j1, (v - pattern.box[1]) / h);
                }
        if (!(i1 - i0 < 1e6) || !(j1 - j0 < 1e6))
            return false;
        const long a0 = static_cast<long>(std::floor(i0)), a1 = static_cast<long>(std::ceil(i1));
        const long b0 = static_cast<long>(std::floor(j0)), b1 = static_cast<long>(std::ceil(j1));
        const long count = (a1 - a0 + 1) * (b1 - b0 + 1);
        if (count <= 0 || count > 2500 || all.tiles + static_cast<size_t>(count) > 20000)
        {
            _sawPattern = true;
            return false;
        }
        all.tiles += static_cast<size_t>(count);
        std::vector<std::vector<Subpath>> cut = clips();
        cut.push_back(area);
        for (long i = a0; i <= a1; ++i)
            for (long j = b0; j <= b1; ++j)
            {
                // The tile at (i, j): its art moved there, then placed.
                const double tx = i * w, ty = j * h;
                const std::array<double, 6> tile{toPage[0], toPage[1], toPage[2], toPage[3],
                    toPage[0] * tx + toPage[2] * ty + toPage[4], toPage[1] * tx + toPage[3] * ty + toPage[5]};
                runTile(pattern.art, _art, _path, tile, cut, all, _depth + 1);
            }
        return true;
    }

    /// A plain color standing for a pattern: the first color its tile
    /// fills with.
    std::optional<Color> patternColor(const std::string& name)
    {
        AiShared& all = shared();
        const auto cached = all.patternColors.find(name);
        if (cached != all.patternColors.end())
            return cached->second;
        const auto found = all.patterns.find(name);
        if (found == all.patterns.end() || _depth >= 4)
            return std::nullopt;
        Artwork scratch;
        runTile(found->second.art, scratch, _path, {1, 0, 0, 1, 0, 0}, {}, all, _depth + 1);
        Color color{0, 0, 0, 255};
        for (const Painted& p : scratch.paths)
            if (p.fill || p.stroke)
            {
                color = p.fill ? *p.fill : *p.stroke;
                break;
            }
        all.patternColors[name] = color;
        return color;
    }

    /// "(name) px py sx sy angle rf r k ka [matrix] p": the pattern and how
    /// it is placed.
    void patternFill(bool forFill)
    {
        std::string name;
        std::array<double, 6> matrix{1, 0, 0, 1, 0, 0};
        bool placed = false;
        for (const Operand& o : _stack)
        {
            if (o.kind == Operand::Kind::String && name.empty())
                name = o.text;
            if (o.kind == Operand::Kind::Array && o.numbers.size() >= 6)
            {
                std::copy(o.numbers.begin(), o.numbers.begin() + 6, matrix.begin());
                placed = true;
            }
        }
        if (name.empty())
            return;
        if (!placed)
        {
            // From the offset and scale alone.
            const size_t n = numbersOnTop();
            if (n >= 4)
                matrix = {number(n - 3), 0, 0, number(n - 4), number(n - 1), number(n - 2)};
        }
        const std::optional<Color> plain = patternColor(name);
        if (forFill)
        {
            _fillPattern = PatternUse{name, matrix};
            if (plain)
                _fill = *plain;
        }
        else if (plain)
            _stroke = *plain;
    }

    /// A color from the numbers on the stack: 3 RGB, 4 CMYK, 7 CMYK then
    /// RGB (Illustrator 9's Xa), the RGB counting.
    std::optional<Color> processColor(size_t count) const
    {
        if (count >= 7)
            return rgb(number(2), number(1), number(0));
        if (count >= 4)
            return cmyk(number(3), number(2), number(1), number(0));
        if (count == 3)
            return rgb(number(2), number(1), number(0));
        if (count == 1)
            return gray(number(0));
        return std::nullopt;
    }

    /// A custom color: "c m y k (name) tint x", or Xk's "... (name) tint
    /// type" with type 1 for RGB.
    std::optional<Color> customColor(bool typed) const
    {
        // From the top: [type] tint (name) then the color's numbers.
        size_t at = typed ? 1 : 0;
        const double tint = number(at);
        ++at;
        if (at >= _stack.size() || _stack[_stack.size() - 1 - at].kind != Operand::Kind::String)
            return std::nullopt;
        ++at;
        const bool isRgb = typed && number(0) == 1.0;
        // Tint 0 is the full color, 1 none (Illustrator counts it down).
        const double strength = 1.0 - std::clamp(tint, 0.0, 1.0);
        if (isRgb)
        {
            const double r = number(at + 2), g = number(at + 1), b = number(at);
            return rgb(1.0 - (1.0 - r) * strength, 1.0 - (1.0 - g) * strength, 1.0 - (1.0 - b) * strength);
        }
        const double c = number(at + 3), m = number(at + 2), y = number(at + 1), k = number(at);
        return cmyk(c * strength, m * strength, y * strength, k * strength);
    }

    void setFill(const std::optional<Color>& color)
    {
        if (!color)
            return;
        _fill = *color;
        _fillPattern.reset();
    }

    void operate(const std::string& op)
    {
        const size_t count = numbersOnTop();
        const auto point = [this](size_t fromTop) { return _path.stage(number(fromTop + 1), number(fromTop)); };
        if (op == "m")
        {
            _last = point(0);
            _path.moveTo(_last);
        }
        else if (op == "l" || op == "L")
        {
            _last = point(0);
            _path.lineTo(_last);
        }
        else if (op == "c" || op == "C")
        {
            const Point a = point(4), b = point(2), p = point(0);
            _path.cubicTo(a, b, p);
            _last = p;
        }
        else if (op == "v" || op == "V")
        {
            const Point b = point(2), p = point(0);
            _path.cubicTo(_last, b, p);
            _last = p;
        }
        else if (op == "y" || op == "Y")
        {
            const Point a = point(2), p = point(0);
            _path.cubicTo(a, p, p);
            _last = p;
        }
        else if (op == "h" || op == "H")
            _path.close();
        else if (op == "f")
            paint(true, false, true);
        else if (op == "F")
            paint(true, false, false);
        else if (op == "s")
            paint(false, true, true);
        else if (op == "S")
            paint(false, true, false);
        else if (op == "b")
            paint(true, true, true);
        else if (op == "B")
            paint(true, true, false);
        else if (op == "n" || op == "N")
            paint(false, false, op == "n");
        else if (op == "W")
            _clipping = true;
        else if (op == "q")
            _clipLevels.emplace_back();
        else if (op == "Q")
        {
            if (!_clipLevels.empty())
                _clipLevels.pop_back();
        }
        else if (op == "*u")
        {
            _compound = true;
            _compoundPaths.clear();
            _compoundFill.reset();
            _compoundStroke.reset();
        }
        else if (op == "*U")
        {
            _compound = false;
            if (_hidden == 0)
                emit(std::move(_compoundPaths), _compoundFill, _compoundStroke, _compoundWidth, _compoundFill.has_value());
            _compoundPaths.clear();
        }
        else if (op == "g" && count >= 1)
            setFill(gray(number(0)));
        else if (op == "G" && count >= 1)
            _stroke = gray(number(0));
        else if (op == "k" && count >= 4)
            setFill(cmyk(number(3), number(2), number(1), number(0)));
        else if (op == "K" && count >= 4)
            _stroke = cmyk(number(3), number(2), number(1), number(0));
        else if (op == "Xa" || op == "XA")
        {
            if (const auto color = processColor(count))
            {
                if (op == "Xa")
                    setFill(color);
                else
                    _stroke = *color;
            }
        }
        else if (op == "x" || op == "X" || op == "Xk" || op == "XK")
        {
            if (const auto color = customColor(op == "Xk" || op == "XK"))
            {
                if (op == "x" || op == "Xk")
                    setFill(color);
                else
                    _stroke = *color;
            }
        }
        else if (op == "w" && count >= 1)
            _width = number(0);
        else if (op == "Lb")
        {
            // visible preview enabled printing dimmed ... : a hidden layer's
            // art is left out.
            const size_t args = numbersOnTop();
            const bool visible = args >= 1 ? _stack[_stack.size() - args].number != 0.0 : true;
            _layers.push_back(!visible);
            if (!visible)
                ++_hidden;
        }
        else if (op == "LB")
        {
            if (!_layers.empty())
            {
                if (_layers.back())
                    --_hidden;
                _layers.pop_back();
            }
        }
        else if (op == "To")
        {
            // 0 point text, 1 text in an area, 2 text along a path.
            _inText = true;
            _textType = count >= 1 ? static_cast<int>(number(0)) : 0;
            _textMatrix = {1, 0, 0, 1, 0, 0};
            _textX = _textY = _lineX = _lineY = 0.0;
            _textPath.clear();
            _pathText.clear();
            _breakPending = false;
        }
        else if (op == "TO")
        {
            flushText();
            if (_textType == 2)
                flushPathText();
            _inText = false;
            _textPath.clear();
        }
        else if (op == "Tp" && count >= 7)
        {
            flushText();
            _textMatrix = {number(6), number(5), number(4), number(3), number(2), number(1)};
            _textX = _textY = _lineX = _lineY = 0.0;
            _breakPending = false;
        }
        else if (op == "TP")
        {
            // The area's or the path's outline is in; the text follows.
            _path.take(false);
        }
        else if (op == "Td" && count >= 2)
        {
            flushText();
            _lineX += number(1);
            _lineY += number(0);
            _textX = _lineX;
            _textY = _lineY;
            _breakPending = false;
        }
        else if (op == "T*")
        {
            flushText();
            nextLine();
        }
        else if (op == "Tf" && _stack.size() >= 2)
        {
            // "/_Name size [leading ascent] Tf"
            for (size_t i = 0; i < _stack.size(); ++i)
                if (_stack[i].kind == Operand::Kind::String)
                {
                    _font = _stack[i].text;
                    if (!_font.empty() && _font[0] == '_')
                        _font.erase(0, 1);
                    if (i + 1 < _stack.size() && _stack[i + 1].kind == Operand::Kind::Number)
                        _fontSize = _stack[i + 1].number;
                    break;
                }
        }
        else if (op == "Tl" && count >= 1)
            _leading = number(count - 1);
        else if ((op == "Tx" || op == "Tj" || op == "TX") && !_stack.empty() &&
                 _stack.back().kind == Operand::Kind::String)
            showText(_stack.back().text);
        else if (op == "XG")
        {
            // A linked file's path, for the image that follows.
            for (const Operand& o : _stack)
                if (o.kind == Operand::Kind::String)
                {
                    _linked = o.text;
                    break;
                }
        }
        else if (op == "XI")
            raster();
        else if (op == "XF")
            _linked.clear();
        else if (op == "Bg")
        {
            _fillPattern.reset();
            gradientFill();
        }
        else if (op == "BB")
            _gradient.reset();
        else if (op == "Xm" && _gradient && !_gradientPlaced && count >= 6)
        {
            place(number(5), number(4), number(3), number(2), number(1), number(0));
            _gradientPlaced = true;
        }
        else if (op == "p" || op == "P")
            patternFill(op == "p");
        else if (op == ":")
        {
            // A dictionary of art: its name first.
            const std::string name =
                !_stack.empty() && _stack.back().kind == Operand::Kind::String ? _stack.back().text : std::string();
            if (_dictionaries.size() < 64)
                _dictionaries.push_back(name);
            if (name == "AI11Text")
            {
                _aiStory = _aiFrame = 0;
                _textPath.clear();
            }
        }
        else if (op == "," && _stack.size() >= 2 && !_dictionaries.empty() && _dictionaries.back() == "AI11Text")
        {
            const Operand& key = _stack.back();
            if (key.kind == Operand::Kind::String && key.text == "StoryIndex")
                _aiStory = static_cast<int>(number(1));
            else if (key.kind == Operand::Kind::String && key.text == "FrameIndex")
                _aiFrame = static_cast<int>(number(1));
        }
        else if (op == ";")
        {
            if (!_dictionaries.empty())
            {
                const std::string name = _dictionaries.back();
                _dictionaries.pop_back();
                if (name == "AI11Text")
                    textFrame(_aiStory, _aiFrame);
            }
        }
        else if ((op == "X=" || op == "X+") && _dictionaries.size() >= 2 && _dictionaries.back() == "Art" &&
                 _dictionaries[_dictionaries.size() - 2] == "AI11Text")
        {
            // A text object's outline (its area, or the path it runs along):
            // kept for the text, not drawn.
            _inText = op == "X=";
        }
        _stack.clear();
    }

    /// "flag (name) x y angle length a b c d tx ty Bg": the named gradient
    /// from its origin along the angle for the length, through the matrix.
    void gradientFill()
    {
        // From the top: tx ty d c b a length angle y x (name) flag.
        if (_stack.size() < 11)
            return;
        const Operand& nameOperand = _stack[_stack.size() - 11];
        if (nameOperand.kind != Operand::Kind::String)
            return;
        const auto found = shared().gradients.find(nameOperand.text);
        if (found == shared().gradients.end())
        {
            _sawGradient = true;
            return;
        }
        // Until an Xm places it exactly: from its origin along its angle
        // for its length, in the page's space.
        const double length = number(6), angle = number(7) * 3.14159265358979323846 / 180.0;
        const double y = number(8), x = number(9);
        Gradient gradient;
        gradient.stops = found->second.stops;
        gradient.radial = found->second.radial;
        _gradient = gradient;
        place(length * std::cos(angle), length * std::sin(angle), -length * std::sin(angle), length * std::cos(angle), x, y);
        _gradientPlaced = false;
    }

    /// The gradient's unit vector (its ramp from 0 to 1 along x) through
    /// [a b c d tx ty] into the page, then onto the stage as the gradient
    /// square's placement.
    void place(double a, double b, double c, double d, double tx, double ty)
    {
        if (!_gradient)
            return;
        const double square = 1638.4;
        // Square to unit space: a linear ramp across [-819.2, 819.2] runs
        // 0 to 1; a radial one's edge is at 819.2.
        const double s = _gradient->radial ? 2.0 / square : 1.0 / square;
        const double shift = _gradient->radial ? 0.0 : 0.5;
        // Unit to page: x' = a u + c v + tx; on to the stage.
        const Point ux = _path.vector(a * s, b * s), uy = _path.vector(c * s, d * s);
        const Point origin = _path.stage(a * shift + tx, b * shift + ty);
        _gradient->placement = Placement{ux.x, ux.y, uy.x, uy.y, origin.x, origin.y};
    }

    /// An image Illustrator keeps in its own format: "[a b c d tx ty] llx
    /// lly urx ury width height bits type alpha reserved encoding mask XI",
    /// its samples after (hex, binary, or base-85 JPEG lines behind "%"),
    /// any alpha after those.
    void raster()
    {
        const std::string linked = _linked;
        _linked.clear();
        const Operand* matrixOperand = nullptr;
        std::vector<double> n;
        for (const Operand& o : _stack)
        {
            if (o.kind == Operand::Kind::Array && !matrixOperand && o.numbers.size() >= 6)
                matrixOperand = &o;
            else if (o.kind == Operand::Kind::Number && matrixOperand)
                n.push_back(o.number);
        }
        if (!matrixOperand || n.size() < 12)
        {
            _sawImage = true;
            return;
        }
        const std::vector<double>& m = matrixOperand->numbers;
        const int width = static_cast<int>(n[4]), height = static_cast<int>(n[5]), bits = static_cast<int>(n[6]);
        const int type = static_cast<int>(n[7]), alphas = std::clamp(static_cast<int>(n[8]), 0, 4);
        const int encoding = static_cast<int>(n[10]) % 256, compression = static_cast<int>(n[10]) / 256;
        const bool mask = n[11] == 1.0;
        const int channels = mask || bits == 1 ? 1 : type == 4 ? 4 : type == 3 ? 3 : 1;
        if (width <= 0 || height <= 0 || width > 20000 || height > 20000 ||
            static_cast<size_t>(width) * static_cast<size_t>(height) > (size_t(1) << 27) || (bits != 1 && bits != 8))
        {
            _sawImage = true;
            return;
        }
        // The data starts on the next line.
        if (_at < _end && _text[_at] == '\r')
            ++_at;
        if (_at < _end && _text[_at] == '\n')
            ++_at;
        const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t row = (static_cast<size_t>(width) * static_cast<size_t>(bits) * static_cast<size_t>(channels) + 7) / 8;
        const size_t colorBytes = row * static_cast<size_t>(height);
        const size_t wanted = colorBytes + pixels * static_cast<size_t>(alphas);
        Image image;
        image.width = width;
        image.height = height;
        std::vector<uint8_t> data;
        if (compression != 0 || encoding == 2)
        {
            // Base-85 lines behind "%", up to its end mark.
            std::string encoded;
            while (_at < _end)
            {
                const size_t lineEnd = std::min(_end, _text.find_first_of("\r\n", _at));
                if (_text[_at] != '%')
                    break;
                encoded.append(_text, _at + 1, lineEnd - _at - 1);
                _at = lineEnd;
                while (_at < _end && (_text[_at] == '\r' || _text[_at] == '\n'))
                    ++_at;
                if (encoded.find("~>") != std::string::npos)
                    break;
            }
            data = ascii85(encoded);
            if (compression != 0)
            {
                // A JPEG, its CMYK ink as PostScript reads it.
                image.encoded = std::move(data);
                image.inkCmyk = channels == 4;
                placeRaster(std::move(image), m);
                return;
            }
        }
        else if (encoding == 0)
        {
            int high = -1;
            while (_at < _end && data.size() < wanted)
            {
                const int d = static_cast<unsigned char>(_text[_at++]);
                const int v = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10
                    : d >= 'A' && d <= 'F'                                           ? d - 'A' + 10
                                                                                     : -1;
                if (v < 0)
                    continue;
                if (high < 0)
                    high = v;
                else
                {
                    data.push_back(static_cast<uint8_t>(high * 16 + v));
                    high = -1;
                }
            }
        }
        else
        {
            const size_t take = std::min(wanted, _end - std::min(_at, _end));
            data.assign(_text.begin() + static_cast<std::ptrdiff_t>(_at),
                _text.begin() + static_cast<std::ptrdiff_t>(_at + take));
            _at += take;
        }
        if (data.size() < colorBytes)
        {
            if (!linked.empty())
            {
                // Nothing embedded: the linked file, where it was.
                image.href = linked;
                placeRaster(std::move(image), m);
                return;
            }
            _sawImage = true;
            return;
        }
        image.rgba.assign(pixels * 4, 255);
        for (size_t p = 0; p < pixels; ++p)
        {
            const size_t y = p / static_cast<size_t>(width), x = p % static_cast<size_t>(width);
            uint8_t* out = image.rgba.data() + p * 4;
            if (bits == 1)
            {
                const uint8_t byte = data[y * row + x / 8];
                const bool on = (byte >> (7 - x % 8)) & 1;
                if (mask)
                {
                    // A stencil in the fill color, painted where the bit is 0.
                    const Color c = _fill.value_or(Color{0, 0, 0, 255});
                    std::copy(c.begin(), c.begin() + 3, out);
                    out[3] = on ? 0 : 255;
                }
                else
                    out[0] = out[1] = out[2] = on ? 255 : 0;
                continue;
            }
            const uint8_t* s = data.data() + y * row + x * static_cast<size_t>(channels);
            Color c;
            if (channels == 4)
                c = cmyk(s[0] / 255.0, s[1] / 255.0, s[2] / 255.0, s[3] / 255.0);
            else if (channels == 3)
                c = Color{s[0], s[1], s[2], 255};
            else if (mask)
            {
                c = _fill.value_or(Color{0, 0, 0, 255});
                c[3] = static_cast<uint8_t>(255 - s[0]);
            }
            else
                c = Color{s[0], s[0], s[0], 255};
            std::copy(c.begin(), c.end(), out);
            if (alphas > 0 && colorBytes + p < data.size())
                out[3] = data[colorBytes + p];
        }
        placeRaster(std::move(image), m);
    }

    /// An image's pixels (rows top down) placed by its matrix: pixel (x, y)
    /// lands at [a b c d tx ty] (x, -y) on the page.
    void placeRaster(Image image, const std::vector<double>& m)
    {
        if (_hidden > 0)
            return;
        const double w = image.width, h = image.height;
        const Point ux = _path.vector(m[0] * w, m[1] * w), uy = _path.vector(-m[2] * h, -m[3] * h);
        const Point origin = _path.stage(m[4], m[5]);
        image.placement = Placement{ux.x, ux.y, uy.x, uy.y, origin.x, origin.y};
        _art.order.emplace_back(Artwork::Kind::Image, _art.images.size());
        _art.images.push_back(std::move(image));
    }

    /// The style of text now: font, size and fill.
    Text textStyle(double scale) const
    {
        Text item;
        std::string name = _font.empty() ? std::string("Helvetica") : _font;
        splitFontName(name, item.family, item.bold, item.italic);
        item.size = std::max(1.0, _fontSize * scale);
        item.color = _fill.value_or(Color{0, 0, 0, 255});
        return item;
    }

    /// How wide \a text (Latin-1) is in the current font, in text space.
    double widthOf(const std::string& text) const
    {
        double units = 0.0;
        for (const unsigned char c : text)
            units += standardGlyphWidth(_font, c);
        return units / 1000.0 * _fontSize;
    }

    void nextLine()
    {
        _lineY -= _leading > 0 ? _leading : _fontSize * 1.2;
        _textX = _lineX;
        _textY = _lineY;
        _breakPending = false;
    }

    /// Text at the text cursor, in the current font and fill color; a
    /// return ends the line, the next one lower by the leading unless a Td
    /// places it first.
    void showText(const std::string& text)
    {
        if (_textType == 2)
        {
            // Along a path: all of it, laid out at the end.
            for (const char ch : text)
                if (ch != '\r' && ch != '\n')
                    _pathText += ch;
            return;
        }
        for (const char ch : text)
        {
            if (ch == '\r' || ch == '\n')
            {
                flushText();
                _breakPending = true;
                continue;
            }
            if (_breakPending)
                nextLine();
            _line += ch;
        }
    }

    void flushText()
    {
        std::string& line = _line;
        if (line.find_first_not_of(" \t") == std::string::npos || _hidden > 0)
        {
            line.clear();
            return;
        }
        const double a = _textMatrix[0], b = _textMatrix[1], c = _textMatrix[2], d = _textMatrix[3];
        const double scale = std::sqrt(std::fabs(a * d - b * c)) * _path.scale();
        Text item = textStyle(scale);
        const std::string shown = line.substr(0, line.find_last_not_of(" \t") + 1);
        item.text = shared().macRoman ? fromMacRoman(shown) : latin1(shown);
        // Text space (y down, the baseline at the origin) to the stage:
        // the cursor, through the text matrix (y up), onto the page.
        const double px = a * _textX + c * _textY + _textMatrix[4];
        const double py = b * _textX + d * _textY + _textMatrix[5];
        const Point at = _path.stage(px, py);
        const Point across = _path.vector(a, b), down = _path.vector(-c, -d);
        const double length = std::max(1e-9, std::hypot(across.x, across.y));
        const double heightLength = std::max(1e-9, std::hypot(down.x, down.y));
        item.placement = Placement{across.x / length, across.y / length, down.x / heightLength, down.y / heightLength,
            at.x, at.y};
        _art.order.emplace_back(Artwork::Kind::Text, _art.texts.size());
        _art.texts.push_back(item);
        // On by the line's width.
        _textX += widthOf(line);
        line.clear();
    }

    /// Text along a path (2 To): its glyphs on the path from the point
    /// nearest where Tp starts it.
    void flushPathText()
    {
        const std::string text = _pathText;
        _pathText.clear();
        if (text.find_first_not_of(" \t") == std::string::npos || _hidden > 0)
            return;
        const std::vector<Point> path = flattened(_textPath);
        if (path.size() < 2)
            return;
        const Point start = _path.stage(_textMatrix[4], _textMatrix[5]);
        double along = 0.0, walked = 0.0, best = 1e300;
        for (size_t i = 0; i < path.size(); ++i)
        {
            if (i > 0)
                walked += std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
            const double d = std::hypot(path[i].x - start.x, path[i].y - start.y);
            if (d < best)
            {
                best = d;
                along = walked;
            }
        }
        const double a = _textMatrix[0], b = _textMatrix[1], c = _textMatrix[2], d = _textMatrix[3];
        textOnPath(_art, textStyle(std::sqrt(std::fabs(a * d - b * c)) * _path.scale()), shared().macRoman ? fromMacRoman(text) : latin1(text), path, along);
    }

    /// An Illustrator 11 text object: story \a story's lines in its frame
    /// \a frameIndex, from where the text engine laid them out.
    void textFrame(int story, int frameIndex)
    {
        const TextDocument& doc = shared().text;
        std::vector<Subpath> confining;
        confining.swap(_textPath);
        _inText = false;
        if (!doc.read || story < 0 || static_cast<size_t>(story) >= doc.stories.size() || _hidden > 0)
        {
            _sawText = _sawText || !doc.read;
            return;
        }
        const TextDocument::Story& s = doc.stories[static_cast<size_t>(story)];
        const int resource = frameIndex >= 0 && static_cast<size_t>(frameIndex) < s.frames.size()
            ? s.frames[static_cast<size_t>(frameIndex)]
            : -1;
        const TextDocument::Frame frame = resource >= 0 && static_cast<size_t>(resource) < doc.frames.size()
            ? doc.frames[static_cast<size_t>(resource)]
            : TextDocument::Frame();
        const std::array<double, 6>& fm = frame.matrix;
        // Frame space (y down) to the page: its matrix, then from the
        // ruler's origin, y up.
        const auto toPage = [&](double x, double y) {
            const double dx = fm[0] * x + fm[2] * y + fm[4], dy = fm[1] * x + fm[3] * y + fm[5];
            return std::make_pair(dx - doc.originX, doc.originY - dy);
        };
        const Point across = _path.vector(fm[0], -fm[1]), down = _path.vector(fm[2], -fm[3]);
        const double al = std::max(1e-9, std::hypot(across.x, across.y)), dl = std::max(1e-9, std::hypot(down.x, down.y));
        const double scale = std::sqrt(std::fabs(fm[0] * fm[3] - fm[1] * fm[2])) * _path.scale();
        const auto textOf = [&s](size_t first, size_t count) {
            std::u16string part = s.text.substr(std::min(first, s.text.size()), count);
            // Line and paragraph ends, and the engine's own marks.
            while (!part.empty() && (part.back() == u'\r' || part.back() == u'\n' || part.back() == 3 || part.back() == 0))
                part.pop_back();
            for (char16_t& c : part)
                if (c < 0x20 && c != u'\t')
                    c = u' ';
            return part;
        };
        const auto styled = [&](const TextDocument::Style& style) {
            Text item;
            splitFontName(style.font, item.family, item.bold, item.italic);
            item.size = std::max(1.0, style.size * scale);
            item.color = style.color;
            return item;
        };
        if (frame.type == 2)
        {
            // Along its path: the frame's own, or the art's outline of it.
            std::vector<Point> path = flattened(confining);
            if (path.size() < 2)
            {
                path.clear();
                for (size_t k = 0; k + 1 < frame.points.size(); k += 2)
                {
                    const auto [x, y] = toPage(frame.points[k], frame.points[k + 1]);
                    path.push_back(_path.stage(x, y));
                }
            }
            const std::u16string text = textOf(0, s.text.size());
            textOnPath(_art, styled(s.styleAt(0)), utf8(text), path, 0.0);
            return;
        }
        std::vector<TextDocument::Story::Line> lines;
        for (const TextDocument::Story::Line& line : s.lines)
            if (resource < 0 || line.frame == resource)
                lines.push_back(line);
        if (lines.empty() && !s.text.empty())
        {
            // Never laid out: a paragraph a line from the frame's corner,
            // the first a size below it in an area.
            double x = 0.0, y = 0.0;
            if (frame.points.size() >= 2)
            {
                x = frame.points[0];
                y = frame.points[1];
                for (size_t k = 0; k + 1 < frame.points.size(); k += 2)
                {
                    x = std::min(x, frame.points[k]);
                    y = std::min(y, frame.points[k + 1]);
                }
            }
            const TextDocument::Style& style = s.styleAt(0);
            if (frame.type == 1)
                y += style.size;
            size_t from = 0;
            while (from < s.text.size() && lines.size() < 1000)
            {
                const size_t end = std::min(s.text.find(u'\r', from), s.text.size());
                TextDocument::Story::Line line;
                line.first = from;
                line.count = end - from;
                line.x = line.frameX = x;
                line.y = y;
                lines.push_back(line);
                y += style.size * 1.2;
                from = end + 1;
            }
        }
        for (const TextDocument::Story::Line& line : lines)
        {
            const std::u16string text = textOf(line.first, line.count);
            if (text.empty())
                continue;
            const TextDocument::Style& style = s.styleAt(line.first);
            std::vector<std::pair<double, int>> stops;
            for (const auto& [start, list] : s.tabs)
                if (start <= line.first)
                    stops = list;
            // Pieces between tabs, each from the tab stop it reaches.
            double pen = line.x;
            size_t from = 0;
            for (int piece = 0; from <= text.size() && piece < 64; ++piece)
            {
                const size_t tab = std::min(text.find(u'\t', from), text.size());
                const std::u16string part = text.substr(from, tab - from);
                double width = 0.0;
                for (const char16_t c : part)
                    width += standardGlyphWidth(style.font, c) / 1000.0 * style.size * style.scale;
                if (piece > 0)
                {
                    const double at = pen - line.frameX;
                    double stop = (std::floor(at / 36.0) + 1.0) * 36.0;
                    int kind = 0;
                    for (const auto& [position, type] : stops)
                        if (position > at + 1e-6)
                        {
                            stop = position;
                            kind = type;
                            break;
                        }
                    pen = line.frameX + stop - (kind == 1 ? width / 2.0 : kind >= 2 ? width : 0.0);
                }
                if (part.find_first_not_of(u' ') != std::u16string::npos)
                {
                    Text item = styled(style);
                    item.text = utf8(part);
                    const auto [x, y] = toPage(pen, line.y);
                    const Point at = _path.stage(x, y);
                    item.placement = Placement{across.x / al, across.y / al, down.x / dl, down.y / dl, at.x, at.y};
                    _art.order.emplace_back(Artwork::Kind::Text, _art.texts.size());
                    _art.texts.push_back(item);
                }
                pen += width;
                from = tab + 1;
            }
        }
    }

    const std::string& _text;
    Artwork& _art;
    Builder _path;
    AiShared _ownShared;
    AiShared* _shared = nullptr;
    int _depth = 0;
    std::vector<std::vector<Subpath>> _outerClips;
    bool _inText = false;
    int _textType = 0;
    std::string _line;
    std::array<double, 6> _textMatrix{1, 0, 0, 1, 0, 0};
    double _textX = 0.0, _textY = 0.0, _lineX = 0.0, _lineY = 0.0;
    bool _breakPending = false;
    std::vector<Subpath> _textPath;
    std::string _pathText;
    std::string _font;
    double _fontSize = 12.0;
    double _leading = 0.0;
    std::vector<std::string> _dictionaries;
    int _aiStory = 0, _aiFrame = 0;
    std::optional<Gradient> _gradient;
    bool _gradientPlaced = false;
    struct PatternUse
    {
        std::string name;
        std::array<double, 6> matrix;
    };
    std::optional<PatternUse> _fillPattern;
    std::string _linked;
    size_t _at = 0;
    size_t _end = 0;
    std::vector<Operand> _stack;
    Point _last;
    std::optional<Color> _fill = Color{0, 0, 0, 255};
    std::optional<Color> _stroke = Color{0, 0, 0, 255};
    double _width = 1.0;
    bool _compound = false;
    std::vector<Subpath> _compoundPaths;
    std::optional<Color> _compoundFill;
    std::optional<Color> _compoundStroke;
    double _compoundWidth = 1.0;
    bool _clipping = false;
    std::vector<std::vector<Subpath>> _clipLevels;
    std::vector<bool> _layers;
    int _hidden = 0;
    bool _sawText = false, _sawImage = false, _sawGradient = false, _sawClip = false, _sawPattern = false;
};

} // namespace

bool looksLikePostScript(const std::vector<uint8_t>& bytes)
{
    if (bytes.size() >= 4 && bytes[0] == 0xC5 && bytes[1] == 0xD0 && bytes[2] == 0xD3 && bytes[3] == 0xC6)
        return true;
    return bytes.size() >= 2 && bytes[0] == '%' && bytes[1] == '!';
}

Artwork readIllustrator(const std::string& text)
{
    Artwork art;
    IllustratorReader reader(text, art);
    reader.run();
    return art;
}

std::vector<uint8_t> illustratorPrivateData(pdf::File& file, const pdf::Object& page)
{
    std::vector<uint8_t> data;
    const pdf::Object* pieces = page.get("PieceInfo");
    if (!pieces)
        return data;
    const pdf::Object& info = file.resolve(*pieces);
    const pdf::Object* illustrator = info.get("Illustrator");
    if (!illustrator)
        return data;
    const pdf::Object* privateEntry = file.resolve(*illustrator).get("Private");
    if (!privateEntry)
        return data;
    const pdf::Object& privateData = file.resolve(*privateEntry);
    std::vector<std::pair<int, const pdf::Object*>> blocks;
    const std::string prefix = "AIPrivateData";
    for (const auto& [key, value] : privateData.entries)
        if (key.compare(0, prefix.size(), prefix) == 0)
            blocks.emplace_back(std::atoi(key.c_str() + prefix.size()), &value);
    std::sort(blocks.begin(), blocks.end(),
        [](const std::pair<int, const pdf::Object*>& a, const std::pair<int, const pdf::Object*>& b) {
            return a.first < b.first;
        });
    for (const auto& [index, value] : blocks)
    {
        const pdf::Object& stream = file.resolve(*value);
        if (stream.type != pdf::Object::Type::Stream)
            continue;
        const std::vector<uint8_t> part = file.decoded(stream);
        data.insert(data.end(), part.begin(), part.end());
    }
    return data;
}

Artwork readIllustratorPrivateData(const std::vector<uint8_t>& data)
{
    const std::string text(data.begin(), data.end());
    const char* const compressed = "%AI12_CompressedData";
    const size_t mark = text.find(compressed);
    if (mark != std::string::npos)
    {
        const size_t start = mark + std::strlen(compressed);
        const std::vector<uint8_t> body = inflateAll(data.data() + start, data.size() - start);
        if (body.empty())
        {
            Artwork art;
            note(art, "The file's Illustrator data could not be decompressed.");
            return art;
        }
        return readIllustrator(std::string(body.begin(), body.end()));
    }
    const char* const zstandard = "%AI24_ZStandard_Data";
    const size_t zmark = text.find(zstandard);
    if (zmark != std::string::npos)
    {
        // The first frame, a line break or so past the mark.
        size_t start = zmark + std::strlen(zstandard);
        for (size_t k = start; k < std::min(data.size(), start + 16) && k + 4 <= data.size(); ++k)
            if (data[k] == 0x28 && data[k + 1] == 0xB5 && data[k + 2] == 0x2F && data[k + 3] == 0xFD)
            {
                start = k;
                break;
            }
        const std::vector<uint8_t> body = zstdAll(data.data() + start, data.size() - start);
        if (body.empty())
        {
            Artwork art;
            note(art, "The file's Illustrator data could not be decompressed.");
            return art;
        }
        return readIllustrator(std::string(body.begin(), body.end()));
    }
    return readIllustrator(text);
}

Artwork readPostScriptArt(const std::vector<uint8_t>& bytes)
{
    const std::string text = postScriptOf(bytes);
    if (isIllustrator(text))
    {
        // An Illustrator 9 or later EPS draws through Adobe's printing
        // procedures; its own art is in the private data it keeps.
        const std::string own = illustratorPrivateText(text);
        if (!own.empty())
        {
            Artwork art = readIllustrator(own);
            if (!art.paths.empty())
                return art;
        }
        Artwork art = readIllustrator(text);
        if (!art.paths.empty() || text.find("%AI5_BeginLayer") != std::string::npos)
            return art;
    }
    double box[4];
    boundingBox(text, box);
    return readPostScript(text, box);
}

} // namespace vectors

} // namespace fla

