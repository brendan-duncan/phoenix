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
    std::vector<Subpath> subpaths;
    Subpath current;

    Point stage(double x, double y) const { return Point(x - left, top - y); }

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
        painted.strokeWidth = std::max(0.1, width);
        art.order.emplace_back(Artwork::Kind::Path, art.paths.size());
        art.paths.push_back(std::move(painted));
    }
};

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
        _gradients = gradientDefinitions(_text);
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
            note(_art, "Images in the file were left out.");
        if (_sawGradient)
            note(_art, "Some gradient fills came in as their path's color (a gradient the file does not define).");
        if (_sawClip)
            note(_art, "Clipping masks were not applied: what they hide shows.");
        if (_sawPattern)
            note(_art, "Pattern fills came in as plain color.");
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
            ++_at;
            int depth = 1;
            while (_at < _end)
            {
                const char d = _text[_at++];
                if (d == '\\' && _at < _end)
                {
                    const char e = _text[_at++];
                    switch (e)
                    {
                    case 'n': o.text += '\n'; break;
                    case 'r': o.text += '\r'; break;
                    case 't': o.text += '\t'; break;
                    case 'b': o.text += '\b'; break;
                    case 'f': o.text += '\f'; break;
                    case '\r':
                    case '\n': break;
                    default:
                        if (e >= '0' && e <= '7')
                        {
                            int value = e - '0';
                            for (int k = 0; k < 2 && _at < _end && _text[_at] >= '0' && _text[_at] <= '7'; ++k)
                                value = value * 8 + (_text[_at++] - '0');
                            o.text += static_cast<char>(value & 0xFF);
                        }
                        else
                            o.text += e;
                    }
                    continue;
                }
                if (d == '(')
                    ++depth;
                else if (d == ')' && --depth == 0)
                    break;
                o.text += d;
            }
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

    std::optional<Color> fillColor() const { return _fill; }

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

    /// A painted path, its fill the gradient in force when there is one.
    void emit(std::vector<Subpath> paths, std::optional<Color> fill, std::optional<Color> stroke, double width,
        bool filled)
    {
        const size_t before = _art.paths.size();
        if (filled && _gradient && !_gradient->stops.empty())
            fill = _gradient->stops.front().color;
        _path.emit(std::move(paths), fill, stroke, width);
        if (_art.paths.size() > before)
            for (const std::vector<Subpath>& level : _clipLevels)
                if (!level.empty())
                    _art.paths.back().clips.push_back(level);
        if (filled && _gradient && _art.paths.size() > before)
            _art.paths.back().gradient = *_gradient;
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
            _fill = gray(number(0));
        else if (op == "G" && count >= 1)
            _stroke = gray(number(0));
        else if (op == "k" && count >= 4)
            _fill = cmyk(number(3), number(2), number(1), number(0));
        else if (op == "K" && count >= 4)
            _stroke = cmyk(number(3), number(2), number(1), number(0));
        else if (op == "Xa" || op == "XA")
        {
            if (const auto color = processColor(count))
                (op == "Xa" ? _fill : _stroke) = *color;
        }
        else if (op == "x" || op == "X" || op == "Xk" || op == "XK")
        {
            if (const auto color = customColor(op == "Xk" || op == "XK"))
                ((op == "x" || op == "Xk") ? _fill : _stroke) = *color;
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
            _inText = true;
            _textMatrix = {1, 0, 0, 1, 0, 0};
            _textX = _textY = 0.0;
        }
        else if (op == "TO")
        {
            flushText();
            _inText = false;
        }
        else if (op == "Tp" && count >= 7)
        {
            flushText();
            _textMatrix = {number(6), number(5), number(4), number(3), number(2), number(1)};
            _textX = _textY = 0.0;
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
        else if (op == "XI" || op == "XG" || op == "XF")
            _sawImage = true;
        else if (op == "Bg")
            gradientFill();
        else if (op == "BB")
            _gradient.reset();
        else if (op == "Xm" && _gradient && !_gradientPlaced && count >= 6)
        {
            place(number(5), number(4), number(3), number(2), number(1), number(0));
            _gradientPlaced = true;
        }
        else if (op == "p" || op == "P")
            _sawPattern = true;
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
        const auto found = _gradients.find(nameOperand.text);
        if (found == _gradients.end())
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
        // Unit to page: x' = a u + c v + tx; page to stage flips y.
        const double ua = a * s, ub = b * s, uc = c * s, ud = d * s;
        const double ox = a * shift + tx, oy = b * shift + ty;
        const Point origin = _path.stage(ox, oy);
        _gradient->placement = Placement{ua, -ub, uc, -ud, origin.x, origin.y};
    }

    /// Text at the text cursor, in the current font and fill color; a
    /// return starts the next line, the leading lower.
    void showText(const std::string& text)
    {
        for (const char ch : text)
        {
            if (ch == '\r' || ch == '\n')
            {
                flushText();
                _textX = 0.0;
                _textY -= _leading > 0 ? _leading : _fontSize * 1.2;
                continue;
            }
            _line += ch;
        }
    }

    void flushText()
    {
        std::string& line = _line;
        const auto flush = [&]() {
            if (line.find_first_not_of(" \t") == std::string::npos || _hidden > 0)
            {
                line.clear();
                return;
            }
            Text item;
            item.text = line;
            std::string family = _font.empty() ? std::string("Helvetica") : _font;
            const auto has = [&family](const char* part) { return family.find(part) != std::string::npos; };
            item.bold = has("Bold") || has("Black") || has("Heavy");
            item.italic = has("Italic") || has("Oblique");
            const size_t dash = family.find('-');
            if (dash != std::string::npos)
                family = family.substr(0, dash);
            item.family = family;
            const double scale = std::sqrt(std::fabs(_textMatrix[0] * _textMatrix[3] - _textMatrix[1] * _textMatrix[2]));
            item.size = std::max(1.0, _fontSize * scale);
            item.color = _fill.value_or(Color{0, 0, 0, 255});
            // Text space (y down, the baseline at the origin) to the stage:
            // the cursor, through the text matrix (y up), onto the page.
            const double a = _textMatrix[0], b = _textMatrix[1], c = _textMatrix[2], d = _textMatrix[3];
            const double px = a * _textX + c * _textY + _textMatrix[4];
            const double py = b * _textX + d * _textY + _textMatrix[5];
            const Point at = _path.stage(px, py);
            const double length = std::max(1e-9, std::hypot(a, b));
            const double heightLength = std::max(1e-9, std::hypot(c, d));
            item.placement = Placement{a / length, -b / length, -c / heightLength, d / heightLength, at.x, at.y};
            _art.order.emplace_back(Artwork::Kind::Text, _art.texts.size());
            _art.texts.push_back(item);
            // On by a rough width.
            _textX += 0.55 * _fontSize * static_cast<double>(line.size());
            line.clear();
        };
        flush();
    }

    const std::string& _text;
    Artwork& _art;
    Builder _path;
    bool _inText = false;
    std::string _line;
    std::array<double, 6> _textMatrix{1, 0, 0, 1, 0, 0};
    double _textX = 0.0, _textY = 0.0;
    std::string _font;
    double _fontSize = 12.0;
    double _leading = 0.0;
    std::map<std::string, AiGradient> _gradients;
    std::optional<Gradient> _gradient;
    bool _gradientPlaced = false;
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

