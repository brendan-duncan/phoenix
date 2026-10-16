// A small PostScript interpreter for EPS artwork that is not Illustrator's:
// FreeHand, Photoshop, CorelDRAW and the drawing programs that write plain
// PostScript. It runs the program and keeps what it paints: paths filled
// and stroked in gray, RGB, CMYK, separation and indexed colors (through
// their tint transforms and tables), clipped; axial and radial shadings as
// gradients; tiling patterns tile by tile; images of any depth, image masks
// and images with masks of their own; and text as text, as wide as the
// fonts the file carries (or the standard fonts) make it.
//
// Written from the PostScript Language Reference (third edition); the
// shape of the operator set follows VectorCraft's clean-room EPS reader
// (MIT/Apache-2.0), which showed which operators real EPS files lean on.

#include "postscript_import.h"

#include "zlib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fla {

namespace vectors {

namespace {

struct Interp;
struct Obj;
using Array = std::vector<Obj>;
using Dict = std::map<std::string, Obj>;
using OpFn = void (*)(Interp&);

/// Where an operator reads data from: the program itself (currentfile),
/// or bytes a filter decoded.
struct Source
{
    std::vector<uint8_t> bytes;
    size_t at = 0;
    bool program = false;
    /// The JPEG a DCTDecode filter leaves for the importer to decode.
    bool dct = false;
    /// A filter not yet read: what it reads from, its name, its end string
    /// (SubFileDecode's), and how many filters lie under it.
    std::shared_ptr<Source> upstream;
    std::string filter;
    std::string end;
    bool pending = false;
    int chain = 0;
};

struct Obj
{
    enum class T : uint8_t
    {
        Null,
        Int,
        Real,
        Bool,
        Name,
        String,
        Array,
        Dict,
        Op,
        Mark,
        File,
        Save
    };
    T t = T::Null;
    bool exec = false;
    /// For an array, how deep arrays nest in it when it was made.
    uint16_t nest = 0;
    double num = 0.0;
    std::shared_ptr<std::string> str;
    std::shared_ptr<Array> arr;
    std::shared_ptr<Dict> dict;
    std::shared_ptr<Source> file;
    OpFn op = nullptr;

    static Obj integer(double v)
    {
        Obj o;
        o.t = T::Int;
        o.num = std::trunc(v);
        return o;
    }
    static Obj real(double v)
    {
        Obj o;
        o.t = T::Real;
        o.num = v;
        return o;
    }
    static Obj boolean(bool v)
    {
        Obj o;
        o.t = T::Bool;
        o.num = v ? 1.0 : 0.0;
        return o;
    }
    static Obj name(const std::string& n, bool executable)
    {
        Obj o;
        o.t = T::Name;
        o.exec = executable;
        o.str = std::make_shared<std::string>(n);
        return o;
    }
    static Obj string(std::string s)
    {
        Obj o;
        o.t = T::String;
        o.str = std::make_shared<std::string>(std::move(s));
        return o;
    }
    static Obj array(Array a, bool executable = false)
    {
        Obj o;
        o.t = T::Array;
        o.exec = executable;
        int deepest = 0;
        for (const Obj& item : a)
            if (item.t == T::Array)
                deepest = std::max<int>(deepest, item.nest);
        o.nest = static_cast<uint16_t>(std::min(deepest + 1, 0xFFFF));
        o.arr = std::make_shared<Array>(std::move(a));
        return o;
    }
    static Obj dictionary()
    {
        Obj o;
        o.t = T::Dict;
        o.dict = std::make_shared<Dict>();
        return o;
    }
    bool isNumber() const { return t == T::Int || t == T::Real; }
    bool isProc() const { return t == T::Array && exec; }
};

void note(Artwork& art, const std::string& text)
{
    if (std::find(art.notes.begin(), art.notes.end(), text) == art.notes.end())
        art.notes.push_back(text);
}

struct PsError
{
    std::string name;
};
struct ExitLoop
{
};
struct StopRun
{
};
struct QuitRun
{
};

/// x' = a x + c y + e, y' = b x + d y + f.
struct Matrix
{
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    void apply(double x, double y, double& ox, double& oy) const
    {
        ox = a * x + c * y + e;
        oy = b * x + d * y + f;
    }
    /// \a m first, then this.
    Matrix after(const Matrix& m) const
    {
        return Matrix{m.a * a + m.b * c, m.a * b + m.b * d, m.c * a + m.d * c, m.c * b + m.d * d, m.e * a + m.f * c + e,
            m.e * b + m.f * d + f};
    }
    bool invert(Matrix& out) const
    {
        const double det = a * d - b * c;
        if (std::fabs(det) < 1e-12)
            return false;
        out = Matrix{d / det, -b / det, -c / det, a / det, (c * f - d * e) / det, (b * e - a * f) / det};
        return true;
    }
    double scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
};

struct Font
{
    std::string name = "Helvetica";
    Matrix matrix{0.001, 0, 0, 0.001, 0, 0};
    /// The font's own FontMatrix scale, before scalefont: a thousandth for
    /// most fonts.
    double base = 0.001;
    std::shared_ptr<Dict> dict;
};

struct GState
{
    Matrix ctm;
    Color color{0, 0, 0, 255};
    /// The color space: "DeviceGray", "DeviceRGB", "DeviceCMYK", or a
    /// separation's with its tint transform.
    std::string space = "DeviceGray";
    int spaceComponents = 1;
    Obj tintTransform;
    std::string alternate;
    /// The space as setcolorspace was given it (a name or an array).
    Obj spaceObj = Obj::name("DeviceGray", false);
    /// A pattern fills (a dictionary makepattern made), else null.
    Obj pattern;
    /// Inside an uncolored pattern's tile: the color is the pattern's.
    bool colorLocked = false;
    double lineWidth = 1.0;
    std::vector<Subpath> path;
    Subpath current;
    bool hasPoint = false;
    double px = 0, py = 0; // current point, device space
    Font font;
    /// The clipping paths in force (stage coordinates).
    std::vector<std::vector<Subpath>> clips;
};

/// An image as image, colorimage and imagemask give it.
struct ImageSpec
{
    int width = 0, height = 0, bits = 8;
    Matrix matrix;
    std::vector<Obj> sources;
    /// Samples a pixel, in  space (null: the device space of as many).
    int components = 1;
    bool multiple = false;
    Obj space;
    std::vector<double> decode;
    /// A stencil painted in the current color (imagemask).
    bool mask = false;
    /// The samples already read, for a source shared with a mask.
    std::optional<std::vector<uint8_t>> data;
    /// An alpha a pixel from a mask (ImageType 3), its own size.
    std::vector<uint8_t> alpha;
    int alphaWidth = 0, alphaHeight = 0;
    /// The sample ranges left out (ImageType 4's MaskColor).
    std::vector<double> maskColor;
};

// ---------------------------------------------------------------- scanner

class Scanner
{
public:
    Scanner(std::shared_ptr<Source> source)
        : src(std::move(source))
    {}
    std::shared_ptr<Source> src;

    bool done() const { return src->at >= src->bytes.size(); }

    int peek() const { return done() ? -1 : src->bytes[src->at]; }

    static bool white(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0; }
    static bool delimiter(int c)
    {
        return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
            c == '%';
    }

    void skip()
    {
        while (!done())
        {
            const int c = peek();
            if (white(c))
                ++src->at;
            else if (c == '%')
            {
                while (!done() && peek() != '\r' && peek() != '\n')
                    ++src->at;
            }
            else
                break;
        }
    }

    /// The next token; false at the end.
    bool next(Obj& out, Interp& interp);

private:
    static constexpr int kMaxNesting = 256;
    int nest = 0;

    std::string word()
    {
        const size_t start = src->at;
        while (!done() && !white(peek()) && !delimiter(peek()))
            ++src->at;
        return std::string(src->bytes.begin() + static_cast<std::ptrdiff_t>(start),
            src->bytes.begin() + static_cast<std::ptrdiff_t>(src->at));
    }
    std::string literal()
    {
        std::string out;
        ++src->at;
        int depth = 1;
        while (!done())
        {
            int c = src->bytes[src->at++];
            if (c == '\\' && !done())
            {
                c = src->bytes[src->at++];
                switch (c)
                {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '\r':
                    if (peek() == '\n')
                        ++src->at;
                    break;
                case '\n': break;
                default:
                    if (c >= '0' && c <= '7')
                    {
                        int v = c - '0';
                        for (int i = 0; i < 2 && peek() >= '0' && peek() <= '7'; ++i)
                            v = v * 8 + (src->bytes[src->at++] - '0');
                        out += static_cast<char>(v & 0xFF);
                    }
                    else
                        out += static_cast<char>(c);
                }
                continue;
            }
            if (c == '(')
                ++depth;
            else if (c == ')' && --depth == 0)
                break;
            out += static_cast<char>(c);
        }
        return out;
    }
};

double toNumber(const std::string& w, bool& ok, bool& integer)
{
    ok = false;
    integer = false;
    if (w.empty())
        return 0.0;
    const size_t hash = w.find('#');
    if (hash != std::string::npos && hash > 0)
    {
        // radix#digits
        const int radix = std::atoi(w.substr(0, hash).c_str());
        if (radix < 2 || radix > 36)
            return 0.0;
        char* end = nullptr;
        const long long v = std::strtoll(w.c_str() + hash + 1, &end, radix);
        ok = end && *end == '\0';
        integer = true;
        return static_cast<double>(v);
    }
    char* end = nullptr;
    const double v = std::strtod(w.c_str(), &end);
    if (!end || *end != '\0')
        return 0.0;
    const char c = w[0];
    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.'))
        return 0.0;
    ok = true;
    integer = w.find_first_of(".eE") == std::string::npos;
    return v;
}

// ---------------------------------------------------------------- interpreter

struct Interp
{
    Artwork& art;
    double pageLeft = 0, pageTop = 0;
    std::vector<Obj> ops;
    std::vector<std::shared_ptr<Dict>> dicts;
    std::shared_ptr<Dict> systemdict, userdict, globaldict;
    GState gs;
    std::vector<GState> saved;
    std::shared_ptr<Source> program;
    std::unique_ptr<Scanner> scanner;
    long long steps = 0;
    int depth = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    bool sawShading = false;
    /// Pattern tiles drawn so far, and how deep patterns paint patterns.
    size_t tiles = 0;
    int patternDepth = 0;
    /// How deep encrypted font programs run inside others.
    int eexecDepth = 0;
    /// Charstring widths worked out, by charstring.
    mutable std::map<std::pair<const void*, size_t>, double> widthCache;
    unsigned seed = 1;

    explicit Interp(Artwork& a)
        : art(a)
    {}

    // -- stack helpers
    Obj pop()
    {
        if (ops.empty())
            throw PsError{"stackunderflow"};
        Obj o = std::move(ops.back());
        ops.pop_back();
        return o;
    }
    // Limits a damaged or hostile program meets: the operand stack, how
    // deep arrays nest (each level costs stack to free), and gsave.
    static constexpr size_t kMaxOperands = 1000000;
    static constexpr int kMaxArrayNesting = 1000;
    static constexpr size_t kMaxSaved = 1000;
    static constexpr size_t kMaxImagePixels = size_t(1) << 27;

    void push(Obj o)
    {
        if (ops.size() >= kMaxOperands)
            throw PsError{"stackoverflow"};
        if (o.t == Obj::T::Array && o.nest > kMaxArrayNesting)
            throw PsError{"limitcheck"};
        ops.push_back(std::move(o));
    }
    double num()
    {
        Obj o = pop();
        if (!o.isNumber())
            throw PsError{"typecheck"};
        return o.num;
    }
    int integer() { return toInt(num()); }
    /// \a v as an int, held to the range of one.
    static int toInt(double v)
    {
        if (!(v == v))
            return 0;
        return static_cast<int>(std::clamp(v, -2147483648.0, 2147483647.0));
    }
    bool boolean()
    {
        Obj o = pop();
        if (o.t != Obj::T::Bool)
            throw PsError{"typecheck"};
        return o.num != 0.0;
    }
    Obj& top(size_t i = 0)
    {
        if (i >= ops.size())
            throw PsError{"stackunderflow"};
        return ops[ops.size() - 1 - i];
    }

    // -- names
    const Obj* lookup(const std::string& n) const
    {
        for (auto it = dicts.rbegin(); it != dicts.rend(); ++it)
        {
            const auto found = (*it)->find(n);
            if (found != (*it)->end())
                return &found->second;
        }
        return nullptr;
    }

    std::string keyOf(const Obj& o) const
    {
        if (o.t == Obj::T::Name || o.t == Obj::T::String)
            return *o.str;
        if (o.isNumber())
        {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.12g", o.num);
            return std::string("#") + buffer;
        }
        if (o.t == Obj::T::Bool)
            return o.num != 0.0 ? "#true" : "#false";
        return "#?";
    }

    void tick()
    {
        if (++steps > 40000000)
            throw PsError{"limitcheck"};
        if ((steps & 0xFFFF) == 0 &&
            std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
            throw PsError{"timeout"};
    }

    void execute(const Obj& o)
    {
        tick();
        if (!o.exec)
        {
            push(o);
            return;
        }
        switch (o.t)
        {
        case Obj::T::Name:
        {
            const Obj* value = lookup(*o.str);
            if (!value)
                throw PsError{"undefined: " + *o.str};
            if (value->exec && (value->t == Obj::T::Array || value->t == Obj::T::Name || value->t == Obj::T::Op ||
                                   value->t == Obj::T::String))
            {
                const Obj copy = *value;
                if (copy.t == Obj::T::Name)
                {
                    // A name standing for another name counts toward the
                    // depth as a procedure does.
                    enter();
                    Leave leave{depth};
                    execute(copy);
                }
                else
                    execute(copy.t == Obj::T::Array ? runnable(copy) : copy);
            }
            else
                push(*value);
            break;
        }
        case Obj::T::Op:
            o.op(*this);
            break;
        case Obj::T::Array:
            run(o);
            break;
        case Obj::T::String:
        {
            // A string run as code.
            enter();
            Leave leave{depth};
            auto source = std::make_shared<Source>();
            source->bytes.assign(o.str->begin(), o.str->end());
            Scanner inner(source);
            Obj token;
            while (inner.next(token, *this))
                token.isProc() ? push(token) : execute(token);
            break;
        }
        default:
            push(o);
        }
    }

    /// A procedure marked to run (an executable array reached by name).
    static Obj runnable(Obj o)
    {
        o.exec = true;
        o.t = Obj::T::Array;
        Obj marker = o;
        marker.op = nullptr;
        return marker;
    }

    void enter()
    {
        if (++depth > 400)
        {
            --depth;
            throw PsError{"execstackoverflow"};
        }
    }
    struct Leave
    {
        int& d;
        ~Leave() { --d; }
    };

    void run(const Obj& proc)
    {
        if (proc.t != Obj::T::Array || !proc.arr)
            throw PsError{"typecheck"};
        // An empty procedure still counts, so a loop of one ends.
        tick();
        enter();
        Leave leave{depth};
        const std::shared_ptr<Array> body = proc.arr;
        for (const Obj& element : *body)
        {
            if (element.isProc())
                push(element);
            else
                execute(element);
        }
    }

    // -- coordinates and painting
    void transform(double x, double y, double& ox, double& oy) const { gs.ctm.apply(x, y, ox, oy); }

    Point stage(double x, double y) const { return Point(x - pageLeft, pageTop - y); }

    void finishSubpath()
    {
        if (gs.current.segments.size() > 1)
            gs.path.push_back(gs.current);
        gs.current = Subpath();
    }

    void moveTo(double x, double y)
    {
        double dx, dy;
        transform(x, y, dx, dy);
        finishSubpath();
        gs.current.segments.push_back(Segment{Segment::Kind::Move, {stage(dx, dy)}});
        gs.px = dx;
        gs.py = dy;
        gs.hasPoint = true;
    }

    void lineTo(double x, double y)
    {
        if (!gs.hasPoint)
            throw PsError{"nocurrentpoint"};
        double dx, dy;
        transform(x, y, dx, dy);
        if (gs.current.segments.empty())
            gs.current.segments.push_back(Segment{Segment::Kind::Move, {stage(gs.px, gs.py)}});
        gs.current.segments.push_back(Segment{Segment::Kind::Line, {stage(dx, dy)}});
        gs.px = dx;
        gs.py = dy;
    }

    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
    {
        if (!gs.hasPoint)
            throw PsError{"nocurrentpoint"};
        double a, b, c, d, e, f;
        transform(x1, y1, a, b);
        transform(x2, y2, c, d);
        transform(x3, y3, e, f);
        if (gs.current.segments.empty())
            gs.current.segments.push_back(Segment{Segment::Kind::Move, {stage(gs.px, gs.py)}});
        gs.current.segments.push_back(Segment{Segment::Kind::Cubic, {stage(a, b), stage(c, d), stage(e, f)}});
        gs.px = e;
        gs.py = f;
    }

    /// The current point in user space.
    void userPoint(double& x, double& y) const
    {
        Matrix inverse;
        if (!gs.ctm.invert(inverse))
        {
            x = y = 0;
            return;
        }
        inverse.apply(gs.px, gs.py, x, y);
    }

    void closePath()
    {
        if (gs.current.segments.empty())
            return;
        gs.current.closed = true;
        // A new subpath starts where this one did.
        const Point start = gs.current.segments.front().points.front();
        gs.px = start.x + pageLeft;
        gs.py = pageTop - start.y;
        finishSubpath();
    }

    void paint(bool fill, bool stroke)
    {
        finishSubpath();
        std::vector<Subpath> paths;
        paths.swap(gs.path);
        gs.hasPoint = false;
        if (paths.empty())
            return;
        if (fill && gs.pattern.t == Obj::T::Dict)
        {
            fillWithPattern(std::move(paths), stroke);
            return;
        }
        Painted painted;
        if (fill)
            for (Subpath& s : paths)
                s.closed = true;
        painted.subpaths = std::move(paths);
        if (fill)
            painted.fill = gs.color;
        if (stroke)
        {
            painted.stroke = gs.color;
            painted.strokeWidth = std::max(0.1, gs.lineWidth * gs.ctm.scale());
        }
        painted.clips = gs.clips;
        art.order.emplace_back(Artwork::Kind::Path, art.paths.size());
        art.paths.push_back(std::move(painted));
    }

    // -- color
    void setDeviceColor(const std::vector<double>& v, const std::string& space)
    {
        if (space == "DeviceRGB" && v.size() >= 3)
            gs.color = rgbColor(v[0], v[1], v[2]);
        else if (space == "DeviceCMYK" && v.size() >= 4)
            gs.color = cmykColor(v[0], v[1], v[2], v[3]);
        else if (!v.empty())
            gs.color = grayColor(v[0]);
    }

    static Color grayColor(double v)
    {
        const auto c = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
        return Color{c, c, c, 255};
    }
    static Color rgbColor(double r, double g, double b)
    {
        const auto c = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
        return Color{c(r), c(g), c(b), 255};
    }
    static Color cmykColor(double c, double m, double y, double k)
    {
        return rgbColor((1 - c) * (1 - k), (1 - m) * (1 - k), (1 - y) * (1 - k));
    }

    /// setcolor in the current space: device spaces directly, a separation
    /// through its tint transform into its alternate space.
    void setColor(const std::vector<double>& values)
    {
        if (gs.colorLocked)
            return;
        gs.pattern = Obj();
        gs.color = colorIn(gs.spaceObj, values);
    }

    /// A device space's color set by setgray and the like: the space too,
    /// unless an uncolored pattern's tile holds the color.
    void setDevice(const char* space, int components, const Color& color)
    {
        if (gs.colorLocked)
            return;
        gs.space = space;
        gs.spaceObj = Obj::name(space, false);
        gs.spaceComponents = components;
        gs.pattern = Obj();
        gs.color = color;
    }

    // -- text
    double fontSize() const
    {
        const Matrix m = gs.font.matrix;
        return std::sqrt(std::fabs(m.a * m.d - m.b * m.c)) * 1000.0 / 1000.0;
    }

    /// The name of the glyph code \a c shows in the current font: through
    /// its Encoding, else the standard one.
    std::string glyphName(unsigned char c) const
    {
        const Obj* encoding = nullptr;
        if (gs.font.dict)
        {
            const auto found = gs.font.dict->find("Encoding");
            if (found != gs.font.dict->end())
                encoding = &found->second;
        }
        if (!encoding || encoding->t != Obj::T::Array)
        {
            const auto found = systemdict->find("StandardEncoding");
            encoding = found != systemdict->end() ? &found->second : nullptr;
        }
        if (encoding && encoding->t == Obj::T::Array && encoding->arr && c < encoding->arr->size())
        {
            const Obj& name = (*encoding->arr)[c];
            if ((name.t == Obj::T::Name || name.t == Obj::T::String) && name.str)
                return *name.str;
        }
        return std::string();
    }

    /// A glyph's width in the font's own units (a thousand to the em for
    /// most): its Metrics entry, else what its charstring sets (a font the
    /// file carries), else the standard metrics of the font the name is
    /// nearest.
    double glyphWidth(unsigned char c) const
    {
        const std::string name = glyphName(c);
        if (gs.font.dict && !name.empty())
        {
            const Dict& font = *gs.font.dict;
            const auto metrics = font.find("Metrics");
            if (metrics != font.end() && metrics->second.t == Obj::T::Dict)
            {
                const auto m = metrics->second.dict->find(name);
                if (m != metrics->second.dict->end())
                {
                    if (m->second.isNumber())
                        return m->second.num;
                    // [sbx wx] or [sbx sby wx wy].
                    if (m->second.t == Obj::T::Array && m->second.arr->size() >= 2)
                        return (*m->second.arr)[m->second.arr->size() == 4 ? 2 : 1].num;
                }
            }
            const auto strings = font.find("CharStrings");
            if (strings != font.end() && strings->second.t == Obj::T::Dict)
            {
                const auto glyph = strings->second.dict->find(name);
                if (glyph != strings->second.dict->end() && glyph->second.t == Obj::T::String && glyph->second.str)
                {
                    const auto key = std::make_pair(static_cast<const void*>(glyph->second.str.get()), size_t(0));
                    const auto cached = widthCache.find(key);
                    if (cached != widthCache.end())
                        return cached->second;
                    int lenIV = 4;
                    const auto priv = font.find("Private");
                    if (priv != font.end() && priv->second.t == Obj::T::Dict)
                    {
                        const auto iv = priv->second.dict->find("lenIV");
                        if (iv != priv->second.dict->end() && iv->second.isNumber())
                            lenIV = toInt(iv->second.num);
                    }
                    const double width = charstringWidth(*glyph->second.str, lenIV);
                    widthCache[key] = width;
                    if (width >= 0.0)
                        return width;
                }
            }
        }
        const std::string text = name.empty() ? std::string() : glyphNameText(name);
        uint32_t u = c;
        if (!text.empty())
        {
            u = static_cast<unsigned char>(text[0]);
            if (u >= 0xC0 && text.size() >= 2)
            {
                const size_t n = u >= 0xF0 ? 4 : u >= 0xE0 ? 3 : 2;
                u &= n == 4 ? 0x07 : n == 3 ? 0x0F : 0x1F;
                for (size_t k = 1; k < n && k < text.size(); ++k)
                    u = (u << 6) | (static_cast<unsigned char>(text[k]) & 0x3F);
            }
        }
        // The standard metrics are a thousand to the em: in a font of other
        // units, scaled to them.
        const double units = std::fabs(gs.font.base) > 1e-12 ? 0.001 / gs.font.base : 1.0;
        return standardGlyphWidth(gs.font.name, u) * units;
    }

    /// The advance a Type 1 charstring sets with hsbw or sbw, after its
    /// encryption (and \a lenIV bytes of it) is undone; -1 when it sets none.
    static double charstringWidth(const std::string& data, int lenIV)
    {
        std::vector<int> plain;
        uint32_t r = 4330;
        for (const char ch : data)
        {
            const uint8_t cipher = static_cast<uint8_t>(ch);
            plain.push_back(cipher ^ static_cast<uint8_t>(r >> 8));
            r = ((cipher + r) * 52845u + 22719u) & 0xFFFFu;
        }
        std::vector<double> args;
        for (size_t i = lenIV < 0 ? 0 : static_cast<size_t>(lenIV); i < plain.size() && args.size() < 24;)
        {
            const int v = plain[i++];
            if (v >= 32 && v <= 246)
                args.push_back(v - 139);
            else if (v >= 247 && v <= 250 && i < plain.size())
                args.push_back((v - 247) * 256 + plain[i++] + 108);
            else if (v >= 251 && v <= 254 && i < plain.size())
                args.push_back(-(v - 251) * 256 - plain[i++] - 108);
            else if (v == 255 && i + 4 <= plain.size())
            {
                const int32_t n = static_cast<int32_t>(static_cast<uint32_t>(plain[i]) << 24 |
                    static_cast<uint32_t>(plain[i + 1]) << 16 | static_cast<uint32_t>(plain[i + 2]) << 8 |
                    static_cast<uint32_t>(plain[i + 3]));
                args.push_back(n);
                i += 4;
            }
            else if (v == 13)
                return args.size() >= 2 ? args[1] : -1.0;
            else if (v == 12 && i < plain.size() && plain[i] == 7)
                return args.size() >= 4 ? args[2] : -1.0;
            else
                return -1.0;
        }
        return -1.0;
    }

    /// The width of \a text in the current font, in user space.
    void textWidth(const std::string& text, double& wx, double& wy) const
    {
        double glyphs = 0.0;
        for (unsigned char c : text)
            glyphs += glyphWidth(c);
        wx = glyphs * gs.font.matrix.a;
        wy = glyphs * gs.font.matrix.b;
    }

    /// \a text as UTF-8: each code through the font's encoding to the
    /// character its glyph name stands for, else as Latin-1.
    std::string textOf(const std::string& text) const
    {
        std::string out;
        for (unsigned char c : text)
        {
            const std::string name = glyphName(c);
            const std::string mapped = name.empty() ? std::string() : glyphNameText(name);
            if (!mapped.empty())
                out += mapped;
            else if (c < 0x80)
                out += static_cast<char>(c);
            else
            {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    /// Shows \a text, then moves on by its width and by \a extraX, \a
    /// extraY more in user space (the spacing ashow and the rest add).
    void show(const std::string& text, double extraX = 0.0, double extraY = 0.0)
    {
        if (!gs.hasPoint)
            throw PsError{"nocurrentpoint"};
        // The font's own matrix (glyph space to user space) and the CTM;
        // the size is what one unit of a thousand-unit em comes to.
        const Matrix toDevice = gs.ctm.after(gs.font.matrix);
        const double units = std::fabs(gs.font.base) > 1e-12 ? 1.0 / std::fabs(gs.font.base) : 1000.0;
        const double size = units * std::sqrt(std::fabs(toDevice.a * toDevice.d - toDevice.b * toDevice.c));
        Text item;
        item.text = textOf(text);
        // A reencoded font's name may carry marks before it ("_Helvetica").
        std::string name = gs.font.name;
        name.erase(0, std::min(name.size(), name.find_first_not_of("_|*%")));
        splitFontName(name, item.family, item.bold, item.italic);
        item.size = std::max(1.0, size);
        item.color = gs.color;
        // Text space (y down, a unit a pixel) to the stage: the direction
        // of the font's x axis, flipped to the stage's y.
        const double length = std::hypot(toDevice.a, toDevice.b);
        const double ux = length > 0 ? toDevice.a / length : 1.0;
        const double uy = length > 0 ? toDevice.b / length : 0.0;
        const Point at = stage(gs.px, gs.py);
        item.placement = Placement{ux, -uy, uy, ux, at.x, at.y};
        if (item.text.find_first_not_of(' ') != std::string::npos)
        {
            art.order.emplace_back(Artwork::Kind::Text, art.texts.size());
            art.texts.push_back(item);
        }
        // On by the width, in user space through the CTM.
        double wx, wy;
        textWidth(text, wx, wy);
        double dx, dy, ox, oy;
        gs.ctm.apply(wx + extraX, wy + extraY, dx, dy);
        gs.ctm.apply(0, 0, ox, oy);
        gs.px += dx - ox;
        gs.py += dy - oy;
    }

    // -- files
    std::vector<uint8_t> readAll(Source& source, const std::function<bool(const Source&)>& atEnd)
    {
        std::vector<uint8_t> out;
        while (source.at < source.bytes.size() && !atEnd(source))
            out.push_back(source.bytes[source.at++]);
        return out;
    }

    void define(const char* n, OpFn f)
    {
        Obj o;
        o.t = Obj::T::Op;
        o.exec = true;
        o.op = f;
        o.str = std::make_shared<std::string>(n);
        (*systemdict)[n] = o;
    }

    void setup();
    /// Runs \a source as the program (currentfile reads it) to its end,
    /// then goes back to the program that was running.
    void runNested(const std::shared_ptr<Source>& source);
    void image(ImageSpec spec);
    void placeImage(Image image, const Matrix& matrix);
    /// What  proc leaves of numbers, run on  in; empty when it fails.
    std::vector<double> runProcedure(const Obj& proc, const std::vector<double>& in);
    /// How many numbers a color in  space takes.
    int componentsOf(const Obj& space, int depth = 0) const;
    /// The color  v names in  space, through tint transforms and
    /// lookup tables to a device's.
    Color colorIn(const Obj& space, const std::vector<double>& v, int depth = 0);
    /// A function (types 0, 2 and 3, an array of them, or a procedure) at
    ///  t.
    std::vector<double> evaluate(const Obj& function, double t, int depth = 0);
    /// An axial or radial shading as a gradient, its space onto the device
    /// by  toDevice; none for other kinds.
    std::optional<Gradient> shadingGradient(const Obj& shading, const Matrix& toDevice);
    /// Fills  area (stage coordinates) with the pattern in force.
    void fillWithPattern(std::vector<Subpath> area, bool stroke);
    /// A plain color standing for a tiling pattern.
    Color tileColor(const Obj& pattern);
};

bool Scanner::next(Obj& out, Interp& interp)
{
    skip();
    if (done())
        return false;
    const int c = peek();
    if (c == '(')
    {
        out = Obj::string(literal());
        return true;
    }
    if (c == '<')
    {
        ++src->at;
        if (peek() == '<')
        {
            ++src->at;
            out = Obj::name("<<", true);
            return true;
        }
        std::string bytes;
        if (peek() == '~')
        {
            // Base-85.
            ++src->at;
            uint32_t group = 0;
            int count = 0;
            while (!done())
            {
                const int d = src->bytes[src->at++];
                if (d == '~')
                {
                    if (peek() == '>')
                        ++src->at;
                    break;
                }
                if (white(d))
                    continue;
                if (d == 'z' && count == 0)
                {
                    bytes.append(4, '\0');
                    continue;
                }
                group = group * 85 + static_cast<uint32_t>(d - '!');
                if (++count == 5)
                {
                    for (int s = 3; s >= 0; --s)
                        bytes += static_cast<char>((group >> (s * 8)) & 0xFF);
                    group = 0;
                    count = 0;
                }
            }
            if (count > 1)
            {
                for (int k = count; k < 5; ++k)
                    group = group * 85 + 84;
                for (int s = 3; s >= 5 - count; --s)
                    bytes += static_cast<char>((group >> (s * 8)) & 0xFF);
            }
        }
        else
        {
            int high = -1;
            while (!done() && peek() != '>')
            {
                const int d = src->bytes[src->at++];
                const int v = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10
                    : d >= 'A' && d <= 'F'                                           ? d - 'A' + 10
                                                                                     : -1;
                if (v < 0)
                    continue;
                if (high < 0)
                    high = v;
                else
                {
                    bytes += static_cast<char>(high * 16 + v);
                    high = -1;
                }
            }
            if (high >= 0)
                bytes += static_cast<char>(high * 16);
            ++src->at;
        }
        out = Obj::string(bytes);
        return true;
    }
    if (c == '>')
    {
        ++src->at;
        if (peek() == '>')
            ++src->at;
        out = Obj::name(">>", true);
        return true;
    }
    if (c == '[' || c == ']')
    {
        ++src->at;
        out = Obj::name(std::string(1, static_cast<char>(c)), true);
        return true;
    }
    if (c == '{')
    {
        // Each brace is read a level deeper.
        if (nest >= kMaxNesting)
            throw PsError{"limitcheck"};
        ++nest;
        struct Leave
        {
            int& d;
            ~Leave() { --d; }
        } leave{nest};
        ++src->at;
        Array body;
        for (;;)
        {
            skip();
            if (done())
                break;
            if (peek() == '}')
            {
                ++src->at;
                break;
            }
            Obj item;
            if (!next(item, interp))
                break;
            body.push_back(item);
        }
        out = Obj::array(std::move(body), true);
        return true;
    }
    if (c == '}' || c == ')')
    {
        ++src->at;
        out = Obj::name("", false);
        return true;
    }
    if (c == '/')
    {
        ++src->at;
        bool immediate = false;
        if (peek() == '/')
        {
            immediate = true;
            ++src->at;
        }
        const std::string n = word();
        if (immediate)
        {
            const Obj* value = interp.lookup(n);
            out = value ? *value : Obj::name(n, false);
            return true;
        }
        out = Obj::name(n, false);
        return true;
    }
    const std::string w = word();
    if (w.empty())
    {
        ++src->at;
        out = Obj::name("", false);
        return true;
    }
    // The white space ending the token is part of it: data an operator
    // reads from the program starts past it (a CR LF counts as one).
    if (!done() && white(peek()))
    {
        const bool crlf = peek() == '\r' && src->at + 1 < src->bytes.size() && src->bytes[src->at + 1] == '\n';
        src->at += crlf ? 2 : 1;
    }
    bool ok = false, integer = false;
    const double v = toNumber(w, ok, integer);
    if (ok)
    {
        out = integer && std::fabs(v) < 2147483647.0 ? Obj::integer(v) : Obj::real(v);
        return true;
    }
    out = Obj::name(w, true);
    return true;
}

Matrix matrixOf(const Obj& o)
{
    if (o.t != Obj::T::Array || o.arr->size() < 6)
        throw PsError{"typecheck"};
    const Array& a = *o.arr;
    return Matrix{a[0].num, a[1].num, a[2].num, a[3].num, a[4].num, a[5].num};
}

Obj matrixObj(const Matrix& m)
{
    return Obj::array({Obj::real(m.a), Obj::real(m.b), Obj::real(m.c), Obj::real(m.d), Obj::real(m.e), Obj::real(m.f)});
}

/// The value of entry \a key of dictionary \a d; null when it has none.
const Obj* entry(const Obj& d, const char* key)
{
    if (d.t != Obj::T::Dict || !d.dict)
        return nullptr;
    const auto found = d.dict->find(key);
    return found == d.dict->end() ? nullptr : &found->second;
}

/// The numbers of an array entry; empty when it is not one.
std::vector<double> numbersIn(const Obj* o)
{
    std::vector<double> out;
    if (o && o->t == Obj::T::Array && o->arr)
        for (const Obj& item : *o->arr)
            out.push_back(item.isNumber() ? item.num : 0.0);
    return out;
}

/// The matrix operand an operator may take last: true when there was one.
bool popMatrix(Interp& I, Matrix& m)
{
    if (!I.ops.empty() && I.top().t == Obj::T::Array && I.top().arr->size() == 6 && !I.top().exec)
    {
        m = matrixOf(I.pop());
        return true;
    }
    return false;
}

std::string cvs(const Obj& o)
{
    switch (o.t)
    {
    case Obj::T::Int: return std::to_string(static_cast<long long>(o.num));
    case Obj::T::Real:
    {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%g", o.num);
        return buffer;
    }
    case Obj::T::Bool: return o.num != 0.0 ? "true" : "false";
    case Obj::T::Name:
    case Obj::T::String: return *o.str;
    case Obj::T::Op: return o.str ? *o.str : "--op--";
    default: return "--nostringval--";
    }
}

bool equal(const Obj& a, const Obj& b)
{
    if (a.isNumber() && b.isNumber())
        return a.num == b.num;
    if ((a.t == Obj::T::Name || a.t == Obj::T::String) && (b.t == Obj::T::Name || b.t == Obj::T::String))
        return *a.str == *b.str;
    if (a.t != b.t)
        return false;
    switch (a.t)
    {
    case Obj::T::Bool: return a.num == b.num;
    case Obj::T::Null:
    case Obj::T::Mark: return true;
    case Obj::T::Array: return a.arr == b.arr;
    case Obj::T::Dict: return a.dict == b.dict;
    case Obj::T::Op: return a.op == b.op;
    default: return false;
    }
}

/// zlib data from \a data on, inflated; \a used gets how many bytes the
/// stream took, so what follows it can be read.
std::vector<uint8_t> inflateBytes(const uint8_t* data, size_t size, size_t* used = nullptr)
{
    std::vector<uint8_t> out;
    z_stream stream{};
    if (used)
        *used = size;
    if (inflateInit(&stream) != Z_OK)
        return out;
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(std::min<size_t>(size, 0xFFFFFFFFu));
    uint8_t buffer[65536];
    for (;;)
    {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        const int status = ::inflate(&stream, Z_NO_FLUSH);
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
        // Past a quarter of a gigabyte the data is damage, or a bomb.
        if (status != Z_OK || (stream.avail_in == 0 && stream.avail_out != 0) || out.size() > (size_t(1) << 28))
            break;
    }
    if (used)
        *used = static_cast<size_t>(stream.total_in);
    inflateEnd(&stream);
    return out;
}

/// Where the JPEG starting at \a at in \a bytes ends: past its end of
/// image mark, else the end of the bytes.
size_t jpegEnd(const std::vector<uint8_t>& bytes, size_t at)
{
    size_t i = at;
    if (i + 1 >= bytes.size() || bytes[i] != 0xFF || bytes[i + 1] != 0xD8)
        return bytes.size();
    i += 2;
    // Marker segments up to the scan, then the scan's data up to a marker
    // that is not a restart.
    while (i + 3 < bytes.size())
    {
        if (bytes[i] != 0xFF)
        {
            ++i;
            continue;
        }
        const uint8_t marker = bytes[i + 1];
        if (marker == 0xD9)
            return i + 2;
        if (marker == 0xFF || marker == 0x00 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01)
        {
            ++i;
            continue;
        }
        const size_t length = static_cast<size_t>(bytes[i + 2]) << 8 | bytes[i + 3];
        i += 2 + length;
        if (marker == 0xDA)
        {
            while (i + 1 < bytes.size())
            {
                if (bytes[i] == 0xFF && bytes[i + 1] != 0x00 && !(bytes[i + 1] >= 0xD0 && bytes[i + 1] <= 0xD7))
                    break;
                ++i;
            }
        }
    }
    return bytes.size();
}

/// What a filter reads from \a source, decoded. Text filters stop at their
/// end marks; others take all their source holds.
std::shared_ptr<Source> makeFilter(Interp& I, const std::shared_ptr<Source>& source, const std::string& name,
    const Obj* parameters)
{
    auto out = std::make_shared<Source>();
    Source& in = *source;
    const auto rest = [&in]() {
        std::vector<uint8_t> all(in.bytes.begin() + static_cast<std::ptrdiff_t>(std::min(in.at, in.bytes.size())),
            in.bytes.end());
        in.at = in.bytes.size();
        return all;
    };
    if (name == "ASCIIHexDecode")
    {
        int high = -1;
        while (in.at < in.bytes.size())
        {
            const int d = in.bytes[in.at++];
            if (d == '>')
                break;
            const int v = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10
                : d >= 'A' && d <= 'F'                                           ? d - 'A' + 10
                                                                                 : -1;
            if (v < 0)
                continue;
            if (high < 0)
                high = v;
            else
            {
                out->bytes.push_back(static_cast<uint8_t>(high * 16 + v));
                high = -1;
            }
        }
    }
    else if (name == "ASCII85Decode")
    {
        uint32_t group = 0;
        int count = 0;
        while (in.at < in.bytes.size())
        {
            const int d = in.bytes[in.at++];
            if (d == '~')
            {
                if (in.at < in.bytes.size() && in.bytes[in.at] == '>')
                    ++in.at;
                break;
            }
            if (Scanner::white(d))
                continue;
            if (d == 'z' && count == 0)
            {
                out->bytes.insert(out->bytes.end(), 4, 0);
                continue;
            }
            if (d < '!' || d > 'u')
                continue;
            group = group * 85 + static_cast<uint32_t>(d - '!');
            if (++count == 5)
            {
                for (int s = 3; s >= 0; --s)
                    out->bytes.push_back(static_cast<uint8_t>((group >> (s * 8)) & 0xFF));
                group = 0;
                count = 0;
            }
        }
        if (count > 1)
        {
            for (int k = count; k < 5; ++k)
                group = group * 85 + 84;
            for (int s = 3; s >= 5 - count; --s)
                out->bytes.push_back(static_cast<uint8_t>((group >> (s * 8)) & 0xFF));
        }
    }
    else if (name == "SubFileDecode")
    {
        // count and an end string: the data up to the count'th end string
        // (with count 0, the first).
        std::string end;
        int count = 0;
        if (parameters && parameters->t == Obj::T::String)
            end = *parameters->str;
        (void)I;
        const auto& bytes = in.bytes;
        const std::string haystack(bytes.begin() + static_cast<std::ptrdiff_t>(in.at), bytes.end());
        size_t stop = std::string::npos;
        if (!end.empty())
        {
            size_t from = 0;
            for (int k = 0; k <= count; ++k)
            {
                stop = haystack.find(end, from);
                if (stop == std::string::npos)
                    break;
                from = stop + end.size();
            }
        }
        const size_t take = stop == std::string::npos ? haystack.size() : stop;
        out->bytes.assign(haystack.begin(), haystack.begin() + static_cast<std::ptrdiff_t>(take));
        in.at += stop == std::string::npos ? take : take + end.size();
    }
    else if (name == "RunLengthDecode")
    {
        // Up to its end mark, so the program reads on past it.
        const std::vector<uint8_t>& data = in.bytes;
        size_t i = std::min(in.at, data.size());
        while (i < data.size())
        {
            const int length = data[i++];
            if (length == 128)
                break;
            if (length < 128)
            {
                const size_t n = std::min<size_t>(static_cast<size_t>(length) + 1, data.size() - i);
                out->bytes.insert(out->bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(i),
                    data.begin() + static_cast<std::ptrdiff_t>(i + n));
                i += n;
            }
            else if (i < data.size())
                out->bytes.insert(out->bytes.end(), static_cast<size_t>(257 - length), data[i++]);
            if (out->bytes.size() > (size_t(1) << 28))
                break;
        }
        in.at = i;
    }
    else if (name == "FlateDecode")
    {
        const size_t start = std::min(in.at, in.bytes.size());
        size_t used = 0;
        out->bytes = inflateBytes(in.bytes.data() + start, in.bytes.size() - start, &used);
        in.at = start + used;
    }
    else if (name == "DCTDecode")
    {
        // The JPEG as it is, for the importer to decode; the program reads
        // on past its end.
        const size_t start = std::min(in.at, in.bytes.size());
        const size_t end = jpegEnd(in.bytes, start);
        out->bytes.assign(in.bytes.begin() + static_cast<std::ptrdiff_t>(start),
            in.bytes.begin() + static_cast<std::ptrdiff_t>(end));
        in.at = end;
        out->dct = true;
    }
    else
    {
        // The rest: the bytes as they are, for whoever reads them.
        out->bytes = rest();
    }
    return out;
}

/// A filter's data, read the first time anything asks for it: from where
/// its source stands then, so it sees what follows the operator reading it
/// (the image operator, for one, its data after it in the program).
void ready(Interp& I, Source& s)
{
    if (!s.pending || !s.upstream)
        return;
    s.pending = false;
    Source& in = *s.upstream;
    ready(I, in);
    const Obj end = Obj::string(s.end);
    const std::shared_ptr<Source> decoded = makeFilter(I, s.upstream, s.filter, s.end.empty() ? nullptr : &end);
    s.bytes = std::move(decoded->bytes);
    s.dct = decoded->dct;
    s.at = 0;
}

// ---------------------------------------------------------------- operators

void opPop(Interp& I) { I.pop(); }
void opExch(Interp& I)
{
    std::swap(I.top(0), I.top(1));
}
void opDup(Interp& I) { I.push(I.top()); }
void opCopy(Interp& I)
{
    if (I.top().t == Obj::T::Int)
    {
        const int n = I.integer();
        if (n < 0 || static_cast<size_t>(n) > I.ops.size())
            throw PsError{"rangecheck"};
        const size_t start = I.ops.size() - static_cast<size_t>(n);
        for (int i = 0; i < n; ++i)
            I.push(I.ops[start + static_cast<size_t>(i)]);
        return;
    }
    Obj to = I.pop();
    Obj from = I.pop();
    if (to.t == Obj::T::Array && from.t == Obj::T::Array)
    {
        for (size_t i = 0; i < from.arr->size() && i < to.arr->size(); ++i)
            (*to.arr)[i] = (*from.arr)[i];
        Obj part = Obj::array(Array(to.arr->begin(), to.arr->begin() + static_cast<std::ptrdiff_t>(
                                                                      std::min(from.arr->size(), to.arr->size()))));
        I.push(part);
    }
    else if (to.t == Obj::T::String && from.t == Obj::T::String)
    {
        to.str->replace(0, std::min(from.str->size(), to.str->size()), from.str->substr(0, to.str->size()));
        I.push(Obj::string(to.str->substr(0, from.str->size())));
    }
    else if (to.t == Obj::T::Dict && from.t == Obj::T::Dict)
    {
        for (const auto& [k, v] : *from.dict)
            (*to.dict)[k] = v;
        I.push(to);
    }
    else
        throw PsError{"typecheck"};
}
void opIndex(Interp& I)
{
    const int n = I.integer();
    I.push(I.top(static_cast<size_t>(std::max(0, n))));
}
void opRoll(Interp& I)
{
    int j = I.integer();
    const int n = I.integer();
    if (n < 0 || static_cast<size_t>(n) > I.ops.size())
        throw PsError{"rangecheck"};
    if (n == 0)
        return;
    j = ((j % n) + n) % n;
    std::rotate(I.ops.end() - n, I.ops.end() - j, I.ops.end());
}
void opClear(Interp& I) { I.ops.clear(); }
void opCount(Interp& I) { I.push(Obj::integer(static_cast<double>(I.ops.size()))); }
void opMark(Interp& I)
{
    Obj m;
    m.t = Obj::T::Mark;
    I.push(m);
}
size_t markIndex(Interp& I)
{
    for (size_t i = I.ops.size(); i-- > 0;)
        if (I.ops[i].t == Obj::T::Mark)
            return i;
    throw PsError{"unmatchedmark"};
}
void opClearToMark(Interp& I) { I.ops.resize(markIndex(I)); }
void opCountToMark(Interp& I) { I.push(Obj::integer(static_cast<double>(I.ops.size() - markIndex(I) - 1))); }

void arith(Interp& I, double (*f)(double, double), bool keepInt)
{
    Obj b = I.pop();
    Obj a = I.pop();
    if (!a.isNumber() || !b.isNumber())
        throw PsError{"typecheck"};
    const double v = f(a.num, b.num);
    I.push(keepInt && a.t == Obj::T::Int && b.t == Obj::T::Int && std::fabs(v) < 2147483647.0 ? Obj::integer(v)
                                                                                             : Obj::real(v));
}
void opAdd(Interp& I) { arith(I, [](double a, double b) { return a + b; }, true); }
void opSub(Interp& I) { arith(I, [](double a, double b) { return a - b; }, true); }
void opMul(Interp& I) { arith(I, [](double a, double b) { return a * b; }, true); }
void opDiv(Interp& I)
{
    const double b = I.num();
    const double a = I.num();
    if (b == 0.0)
        throw PsError{"undefinedresult"};
    I.push(Obj::real(a / b));
}
void opIdiv(Interp& I)
{
    // In 64 bits, so the most negative int over -1 does not trap.
    const long long b = I.integer();
    const long long a = I.integer();
    if (b == 0)
        throw PsError{"undefinedresult"};
    I.push(Obj::integer(static_cast<double>(a / b)));
}
void opMod(Interp& I)
{
    const long long b = I.integer();
    const long long a = I.integer();
    if (b == 0)
        throw PsError{"undefinedresult"};
    I.push(Obj::integer(static_cast<double>(a % b)));
}
void unary(Interp& I, double (*f)(double), bool keepInt)
{
    Obj a = I.pop();
    if (!a.isNumber())
        throw PsError{"typecheck"};
    I.push(keepInt && a.t == Obj::T::Int ? Obj::integer(f(a.num)) : Obj::real(f(a.num)));
}
void opAbs(Interp& I) { unary(I, [](double v) { return std::fabs(v); }, true); }
void opNeg(Interp& I) { unary(I, [](double v) { return -v; }, true); }
void opCeiling(Interp& I) { unary(I, [](double v) { return std::ceil(v); }, true); }
void opFloor(Interp& I) { unary(I, [](double v) { return std::floor(v); }, true); }
void opRound(Interp& I) { unary(I, [](double v) { return std::floor(v + 0.5); }, true); }
void opTruncate(Interp& I) { unary(I, [](double v) { return std::trunc(v); }, true); }
void opSqrt(Interp& I) { unary(I, [](double v) { return std::sqrt(std::max(0.0, v)); }, false); }
void opSin(Interp& I) { unary(I, [](double v) { return std::sin(v * 3.14159265358979323846 / 180.0); }, false); }
void opCos(Interp& I) { unary(I, [](double v) { return std::cos(v * 3.14159265358979323846 / 180.0); }, false); }
void opExp(Interp& I)
{
    const double e = I.num();
    const double b = I.num();
    I.push(Obj::real(std::pow(b, e)));
}
void opLn(Interp& I) { unary(I, [](double v) { return std::log(std::max(1e-300, v)); }, false); }
void opLog(Interp& I) { unary(I, [](double v) { return std::log10(std::max(1e-300, v)); }, false); }
void opAtan(Interp& I)
{
    const double den = I.num();
    const double n = I.num();
    double angle = std::atan2(n, den) * 180.0 / 3.14159265358979323846;
    if (angle < 0)
        angle += 360.0;
    I.push(Obj::real(angle));
}
void opCvi(Interp& I)
{
    Obj o = I.pop();
    if (o.t == Obj::T::String)
        I.push(Obj::integer(std::atof(o.str->c_str())));
    else
        I.push(Obj::integer(o.num));
}
void opCvr(Interp& I)
{
    Obj o = I.pop();
    I.push(Obj::real(o.t == Obj::T::String ? std::atof(o.str->c_str()) : o.num));
}
void opRand(Interp& I)
{
    I.seed = I.seed * 1103515245u + 12345u;
    I.push(Obj::integer(static_cast<double>((I.seed >> 1) & 0x7FFFFFFF)));
}
void opSrand(Interp& I) { I.seed = static_cast<unsigned>(I.integer()); }
void opRrand(Interp& I) { I.push(Obj::integer(static_cast<double>(I.seed))); }

void compare(Interp& I, bool (*f)(double, double), bool (*s)(const std::string&, const std::string&))
{
    Obj b = I.pop();
    Obj a = I.pop();
    if (a.isNumber() && b.isNumber())
        I.push(Obj::boolean(f(a.num, b.num)));
    else if (a.t == Obj::T::String && b.t == Obj::T::String)
        I.push(Obj::boolean(s(*a.str, *b.str)));
    else
        throw PsError{"typecheck"};
}
void opEq(Interp& I)
{
    Obj b = I.pop();
    Obj a = I.pop();
    I.push(Obj::boolean(equal(a, b)));
}
void opNe(Interp& I)
{
    Obj b = I.pop();
    Obj a = I.pop();
    I.push(Obj::boolean(!equal(a, b)));
}
void opGt(Interp& I) { compare(I, [](double a, double b) { return a > b; }, [](const std::string& a, const std::string& b) { return a > b; }); }
void opGe(Interp& I) { compare(I, [](double a, double b) { return a >= b; }, [](const std::string& a, const std::string& b) { return a >= b; }); }
void opLt(Interp& I) { compare(I, [](double a, double b) { return a < b; }, [](const std::string& a, const std::string& b) { return a < b; }); }
void opLe(Interp& I) { compare(I, [](double a, double b) { return a <= b; }, [](const std::string& a, const std::string& b) { return a <= b; }); }
void logic(Interp& I, int which)
{
    Obj b = I.pop();
    Obj a = I.pop();
    if (a.t == Obj::T::Bool && b.t == Obj::T::Bool)
    {
        const bool x = a.num != 0.0, y = b.num != 0.0;
        I.push(Obj::boolean(which == 0 ? (x && y) : which == 1 ? (x || y) : (x != y)));
    }
    else
    {
        const long long x = static_cast<long long>(a.num), y = static_cast<long long>(b.num);
        I.push(Obj::integer(static_cast<double>(which == 0 ? (x & y) : which == 1 ? (x | y) : (x ^ y))));
    }
}
void opAnd(Interp& I) { logic(I, 0); }
void opOr(Interp& I) { logic(I, 1); }
void opXor(Interp& I) { logic(I, 2); }
void opNot(Interp& I)
{
    Obj a = I.pop();
    if (a.t == Obj::T::Bool)
        I.push(Obj::boolean(a.num == 0.0));
    else
        I.push(Obj::integer(static_cast<double>(~static_cast<long long>(a.num))));
}
void opBitshift(Interp& I)
{
    const int shift = I.integer();
    const long long v = static_cast<long long>(I.num());
    I.push(Obj::integer(static_cast<double>(shift >= 0 ? (v << shift) : (v >> -shift))));
}
void opTrue(Interp& I) { I.push(Obj::boolean(true)); }
void opFalse(Interp& I) { I.push(Obj::boolean(false)); }
void opNull(Interp& I) { I.push(Obj()); }

void opType(Interp& I)
{
    Obj o = I.pop();
    static const char* names[] = {"nulltype", "integertype", "realtype", "booleantype", "nametype", "stringtype",
        "arraytype", "dicttype", "operatortype", "marktype", "filetype", "savetype"};
    I.push(Obj::name(names[static_cast<int>(o.t)], true));
}
void opCvn(Interp& I)
{
    Obj o = I.pop();
    I.push(Obj::name(o.str ? *o.str : std::string(), o.exec));
}
void opCvs(Interp& I)
{
    Obj buffer = I.pop();
    Obj o = I.pop();
    const std::string text = cvs(o);
    if (buffer.t == Obj::T::String)
        buffer.str->replace(0, std::min(text.size(), buffer.str->size()), text.substr(0, buffer.str->size()));
    I.push(Obj::string(text));
}
void opCvx(Interp& I) { I.top().exec = true; }
void opCvlit(Interp& I) { I.top().exec = false; }
void opXcheck(Interp& I)
{
    Obj o = I.pop();
    I.push(Obj::boolean(o.exec));
}
void opNoop(Interp&) {}
void opTrueResult(Interp& I)
{
    I.pop();
    I.push(Obj::boolean(true));
}

void opExec(Interp& I)
{
    Obj o = I.pop();
    if (o.t == Obj::T::Array && o.exec)
        I.run(o);
    else
    {
        // exec of exec of exec... counts toward the depth too.
        I.enter();
        Interp::Leave leave{I.depth};
        I.execute(o);
    }
}
void opIf(Interp& I)
{
    Obj proc = I.pop();
    if (I.boolean())
        I.run(proc);
}
void opIfelse(Interp& I)
{
    Obj no = I.pop();
    Obj yes = I.pop();
    I.run(I.boolean() ? yes : no);
}
void opFor(Interp& I)
{
    Obj proc = I.pop();
    Obj limit = I.pop();
    Obj step = I.pop();
    Obj init = I.pop();
    const bool integer = init.t == Obj::T::Int && step.t == Obj::T::Int;
    try
    {
        for (double v = init.num; step.num >= 0 ? v <= limit.num : v >= limit.num; v += step.num)
        {
            // A step too small to move the count would never end.
            if (step.num == 0 || v + step.num == v)
                throw PsError{"limitcheck"};
            I.push(integer ? Obj::integer(v) : Obj::real(v));
            I.run(proc);
        }
    }
    catch (const ExitLoop&)
    {
    }
}
void opRepeat(Interp& I)
{
    Obj proc = I.pop();
    const int n = I.integer();
    try
    {
        for (int i = 0; i < n; ++i)
            I.run(proc);
    }
    catch (const ExitLoop&)
    {
    }
}
void opLoop(Interp& I)
{
    Obj proc = I.pop();
    try
    {
        for (;;)
            I.run(proc);
    }
    catch (const ExitLoop&)
    {
    }
}
void opExit(Interp&) { throw ExitLoop{}; }
void opStop(Interp&) { throw StopRun{}; }
void opStopped(Interp& I)
{
    Obj proc = I.pop();
    const size_t dictDepth = I.dicts.size();
    try
    {
        I.enter();
        Interp::Leave leave{I.depth};
        I.execute(proc.t == Obj::T::Array ? Interp::runnable(proc) : proc);
        I.push(Obj::boolean(false));
    }
    catch (const StopRun&)
    {
        I.dicts.resize(std::max<size_t>(3, std::min(dictDepth, I.dicts.size())));
        I.push(Obj::boolean(true));
    }
    catch (const PsError&)
    {
        I.dicts.resize(std::max<size_t>(3, std::min(dictDepth, I.dicts.size())));
        I.push(Obj::boolean(true));
    }
}
void opQuit(Interp&) { throw QuitRun{}; }
void opForall(Interp& I)
{
    Obj proc = I.pop();
    Obj over = I.pop();
    try
    {
        if (over.t == Obj::T::Array)
        {
            const std::shared_ptr<Array> items = over.arr;
            for (size_t i = 0; i < items->size(); ++i)
            {
                I.push((*items)[i]);
                I.run(proc);
            }
        }
        else if (over.t == Obj::T::String)
        {
            const std::string text = *over.str;
            for (const char c : text)
            {
                I.push(Obj::integer(static_cast<unsigned char>(c)));
                I.run(proc);
            }
        }
        else if (over.t == Obj::T::Dict)
        {
            const Dict copy = *over.dict;
            for (const auto& [k, v] : copy)
            {
                I.push(k[0] == '#' ? Obj::integer(std::atof(k.c_str() + 1)) : Obj::name(k, false));
                I.push(v);
                I.run(proc);
            }
        }
        else
            throw PsError{"typecheck"};
    }
    catch (const ExitLoop&)
    {
    }
}

void opDict(Interp& I)
{
    I.pop();
    I.push(Obj::dictionary());
}
void opBegin(Interp& I)
{
    Obj d = I.pop();
    if (d.t != Obj::T::Dict)
        throw PsError{"typecheck"};
    I.dicts.push_back(d.dict);
}
void opEnd(Interp& I)
{
    if (I.dicts.size() > 3)
        I.dicts.pop_back();
}
void opDef(Interp& I)
{
    Obj value = I.pop();
    Obj key = I.pop();
    (*I.dicts.back())[I.keyOf(key)] = value;
}
void opLoad(Interp& I)
{
    Obj key = I.pop();
    const Obj* value = I.lookup(I.keyOf(key));
    if (!value)
        throw PsError{"undefined: " + I.keyOf(key)};
    I.push(*value);
}
void opStore(Interp& I)
{
    Obj value = I.pop();
    Obj key = I.pop();
    const std::string k = I.keyOf(key);
    for (auto it = I.dicts.rbegin(); it != I.dicts.rend(); ++it)
        if ((*it)->count(k))
        {
            (**it)[k] = value;
            return;
        }
    (*I.dicts.back())[k] = value;
}
void opKnown(Interp& I)
{
    Obj key = I.pop();
    Obj d = I.pop();
    if (d.t != Obj::T::Dict)
        throw PsError{"typecheck"};
    I.push(Obj::boolean(d.dict->count(I.keyOf(key)) > 0));
}
void opWhere(Interp& I)
{
    Obj key = I.pop();
    const std::string k = I.keyOf(key);
    for (auto it = I.dicts.rbegin(); it != I.dicts.rend(); ++it)
        if ((*it)->count(k))
        {
            Obj d;
            d.t = Obj::T::Dict;
            d.dict = *it;
            I.push(d);
            I.push(Obj::boolean(true));
            return;
        }
    I.push(Obj::boolean(false));
}
void opCurrentdict(Interp& I)
{
    Obj d;
    d.t = Obj::T::Dict;
    d.dict = I.dicts.back();
    I.push(d);
}
void pushDict(Interp& I, const std::shared_ptr<Dict>& dict)
{
    Obj d;
    d.t = Obj::T::Dict;
    d.dict = dict;
    I.push(d);
}
void opUserdict(Interp& I) { pushDict(I, I.userdict); }
void opSystemdict(Interp& I) { pushDict(I, I.systemdict); }
void opGlobaldict(Interp& I) { pushDict(I, I.globaldict); }
void opCountdictstack(Interp& I) { I.push(Obj::integer(static_cast<double>(I.dicts.size()))); }
void opUndef(Interp& I)
{
    Obj key = I.pop();
    Obj d = I.pop();
    if (d.t == Obj::T::Dict)
        d.dict->erase(I.keyOf(key));
}
void opDictBegin(Interp& I) { opMark(I); }
void opDictEnd(Interp& I)
{
    const size_t mark = markIndex(I);
    Obj d = Obj::dictionary();
    for (size_t i = mark + 1; i + 1 < I.ops.size(); i += 2)
        (*d.dict)[I.keyOf(I.ops[i])] = I.ops[i + 1];
    I.ops.resize(mark);
    I.push(d);
}
void opArrayBegin(Interp& I) { opMark(I); }
void opArrayEnd(Interp& I)
{
    const size_t mark = markIndex(I);
    Array items(I.ops.begin() + static_cast<std::ptrdiff_t>(mark + 1), I.ops.end());
    I.ops.resize(mark);
    I.push(Obj::array(std::move(items)));
}
void opArray(Interp& I)
{
    const int n = I.integer();
    if (n < 0 || n > 10000000)
        throw PsError{"rangecheck"};
    I.push(Obj::array(Array(static_cast<size_t>(n))));
}
void opString(Interp& I)
{
    const int n = I.integer();
    if (n < 0 || n > 100000000)
        throw PsError{"rangecheck"};
    I.push(Obj::string(std::string(static_cast<size_t>(n), '\0')));
}
void opLength(Interp& I)
{
    Obj o = I.pop();
    switch (o.t)
    {
    case Obj::T::Array: I.push(Obj::integer(static_cast<double>(o.arr->size()))); break;
    case Obj::T::String:
    case Obj::T::Name: I.push(Obj::integer(static_cast<double>(o.str->size()))); break;
    case Obj::T::Dict: I.push(Obj::integer(static_cast<double>(o.dict->size()))); break;
    default: throw PsError{"typecheck"};
    }
}
void opMaxlength(Interp& I)
{
    Obj o = I.pop();
    I.push(Obj::integer(o.t == Obj::T::Dict ? static_cast<double>(o.dict->size() + 100) : 0));
}
void opGet(Interp& I)
{
    Obj key = I.pop();
    Obj o = I.pop();
    if (o.t == Obj::T::Array)
    {
        const int i = Interp::toInt(key.num);
        if (i < 0 || static_cast<size_t>(i) >= o.arr->size())
            throw PsError{"rangecheck"};
        I.push((*o.arr)[static_cast<size_t>(i)]);
    }
    else if (o.t == Obj::T::String)
    {
        const int i = Interp::toInt(key.num);
        if (i < 0 || static_cast<size_t>(i) >= o.str->size())
            throw PsError{"rangecheck"};
        I.push(Obj::integer(static_cast<unsigned char>((*o.str)[static_cast<size_t>(i)])));
    }
    else if (o.t == Obj::T::Dict)
    {
        const auto found = o.dict->find(I.keyOf(key));
        if (found == o.dict->end())
            throw PsError{"undefined: " + I.keyOf(key)};
        I.push(found->second);
    }
    else
        throw PsError{"typecheck"};
}
void opPut(Interp& I)
{
    Obj value = I.pop();
    Obj key = I.pop();
    Obj o = I.pop();
    if (o.t == Obj::T::Array)
    {
        const int i = Interp::toInt(key.num);
        if (i < 0 || static_cast<size_t>(i) >= o.arr->size())
            throw PsError{"rangecheck"};
        (*o.arr)[static_cast<size_t>(i)] = value;
    }
    else if (o.t == Obj::T::String)
    {
        const int i = Interp::toInt(key.num);
        if (i < 0 || static_cast<size_t>(i) >= o.str->size())
            throw PsError{"rangecheck"};
        (*o.str)[static_cast<size_t>(i)] = static_cast<char>(static_cast<int>(value.num));
    }
    else if (o.t == Obj::T::Dict)
        (*o.dict)[I.keyOf(key)] = value;
    else
        throw PsError{"typecheck"};
}
void opGetinterval(Interp& I)
{
    const int count = I.integer();
    const int index = I.integer();
    Obj o = I.pop();
    if (index < 0 || count < 0)
        throw PsError{"rangecheck"};
    if (o.t == Obj::T::Array)
    {
        if (static_cast<size_t>(index) + static_cast<size_t>(count) > o.arr->size())
            throw PsError{"rangecheck"};
        Obj part = Obj::array(Array(o.arr->begin() + index, o.arr->begin() + index + count), o.exec);
        I.push(part);
    }
    else if (o.t == Obj::T::String)
    {
        if (static_cast<size_t>(index) + static_cast<size_t>(count) > o.str->size())
            throw PsError{"rangecheck"};
        I.push(Obj::string(o.str->substr(static_cast<size_t>(index), static_cast<size_t>(count))));
    }
    else
        throw PsError{"typecheck"};
}
void opPutinterval(Interp& I)
{
    Obj from = I.pop();
    const int index = I.integer();
    Obj to = I.pop();
    if (index < 0)
        throw PsError{"rangecheck"};
    if (to.t == Obj::T::Array && from.t == Obj::T::Array)
    {
        for (size_t i = 0; i < from.arr->size() && static_cast<size_t>(index) + i < to.arr->size(); ++i)
            (*to.arr)[static_cast<size_t>(index) + i] = (*from.arr)[i];
    }
    else if (to.t == Obj::T::String && from.t == Obj::T::String)
    {
        for (size_t i = 0; i < from.str->size() && static_cast<size_t>(index) + i < to.str->size(); ++i)
            (*to.str)[static_cast<size_t>(index) + i] = (*from.str)[i];
    }
    else
        throw PsError{"typecheck"};
}
void opAload(Interp& I)
{
    Obj a = I.pop();
    if (a.t != Obj::T::Array)
        throw PsError{"typecheck"};
    for (const Obj& item : *a.arr)
        I.push(item);
    I.push(a);
}
void opAstore(Interp& I)
{
    Obj a = I.pop();
    if (a.t != Obj::T::Array)
        throw PsError{"typecheck"};
    for (size_t i = a.arr->size(); i-- > 0;)
        (*a.arr)[i] = I.pop();
    I.push(a);
}
/// Throws unless \a o is a string (or a name, read as one).
const Obj& textOperand(const Obj& o)
{
    if ((o.t != Obj::T::String && o.t != Obj::T::Name) || !o.str)
        throw PsError{"typecheck"};
    return o;
}
/// Throws unless \a o is a file.
Source& fileOperand(const Obj& o)
{
    if (o.t != Obj::T::File || !o.file)
        throw PsError{"typecheck"};
    return *o.file;
}
void opSearch(Interp& I)
{
    Obj seek = textOperand(I.pop());
    Obj s = textOperand(I.pop());
    const size_t at = s.str->find(*seek.str);
    if (at == std::string::npos)
    {
        I.push(s);
        I.push(Obj::boolean(false));
        return;
    }
    I.push(Obj::string(s.str->substr(at + seek.str->size())));
    I.push(Obj::string(*seek.str));
    I.push(Obj::string(s.str->substr(0, at)));
    I.push(Obj::boolean(true));
}
void opAnchorsearch(Interp& I)
{
    Obj seek = textOperand(I.pop());
    Obj s = textOperand(I.pop());
    if (s.str->compare(0, seek.str->size(), *seek.str) == 0)
    {
        I.push(Obj::string(s.str->substr(seek.str->size())));
        I.push(Obj::string(*seek.str));
        I.push(Obj::boolean(true));
    }
    else
    {
        I.push(s);
        I.push(Obj::boolean(false));
    }
}
void opBind(Interp&) {}
void opSave(Interp& I)
{
    if (I.saved.size() >= Interp::kMaxSaved)
        throw PsError{"limitcheck"};
    I.saved.push_back(I.gs);
    Obj s;
    s.t = Obj::T::Save;
    I.push(s);
}
void opRestore(Interp& I)
{
    I.pop();
    if (!I.saved.empty())
    {
        I.gs = I.saved.back();
        I.saved.pop_back();
    }
}
void opVersion(Interp& I) { I.push(Obj::string("3010")); }
void opLanguagelevel(Interp& I) { I.push(Obj::integer(2)); }
void opProduct(Interp& I) { I.push(Obj::string("Phoenix")); }
void opUsertime(Interp& I)
{
    I.push(Obj::integer(static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - I.started).count())));
}
void opVmstatus(Interp& I)
{
    I.push(Obj::integer(0));
    I.push(Obj::integer(0));
    I.push(Obj::integer(100000000));
}
void opCurrentglobal(Interp& I) { I.push(Obj::boolean(false)); }
void opPopOne(Interp& I) { I.pop(); }
void opPopTwo(Interp& I)
{
    I.pop();
    I.pop();
}
void opEmptyProc(Interp& I) { I.push(Obj::array({}, true)); }
void opFourProcs(Interp& I)
{
    for (int i = 0; i < 4; ++i)
        opEmptyProc(I);
}
void opCurrentscreen(Interp& I)
{
    I.push(Obj::real(60));
    I.push(Obj::real(45));
    opEmptyProc(I);
}
void opZero(Interp& I) { I.push(Obj::integer(0)); }
void opOne(Interp& I) { I.push(Obj::real(1)); }
void opTen(Interp& I) { I.push(Obj::real(10)); }
void opFalseResult(Interp& I) { I.push(Obj::boolean(false)); }
void opCurrentdash(Interp& I)
{
    I.push(Obj::array({}));
    I.push(Obj::integer(0));
}
void opCurrenthalftone(Interp& I)
{
    Obj d = Obj::dictionary();
    (*d.dict)["HalftoneType"] = Obj::integer(1);
    (*d.dict)["Frequency"] = Obj::real(60);
    (*d.dict)["Angle"] = Obj::real(45);
    (*d.dict)["SpotFunction"] = Obj::array({}, true);
    I.push(d);
}
void opSetscreen(Interp& I)
{
    I.pop();
    I.pop();
    I.pop();
}
void opSetcachedevice(Interp& I)
{
    for (int i = 0; i < 6; ++i)
        I.pop();
}
void opSetcharwidth(Interp& I)
{
    I.pop();
    I.pop();
}

void opCurrentpagedevice(Interp& I)
{
    Obj d = Obj::dictionary();
    (*d.dict)["PageSize"] = Obj::array({Obj::real(612), Obj::real(792)});
    I.push(d);
}

// -- graphics state

void opGsave(Interp& I)
{
    if (I.saved.size() >= Interp::kMaxSaved)
        throw PsError{"limitcheck"};
    I.saved.push_back(I.gs);
}
void opGrestore(Interp& I)
{
    if (!I.saved.empty())
    {
        I.gs = I.saved.back();
        I.saved.pop_back();
    }
}
void opGrestoreall(Interp& I)
{
    if (!I.saved.empty())
    {
        I.gs = I.saved.front();
        I.saved.clear();
    }
}
void opInitgraphics(Interp& I)
{
    I.gs = GState();
}
void opSetlinewidth(Interp& I) { I.gs.lineWidth = I.num(); }
void opCurrentlinewidth(Interp& I) { I.push(Obj::real(I.gs.lineWidth)); }
void opSetdash(Interp& I)
{
    I.pop();
    I.pop();
}
void opSetgray(Interp& I) { I.setDevice("DeviceGray", 1, Interp::grayColor(I.num())); }
void opSetrgbcolor(Interp& I)
{
    const double b = I.num(), g = I.num(), r = I.num();
    I.setDevice("DeviceRGB", 3, Interp::rgbColor(r, g, b));
}
void opSetcmykcolor(Interp& I)
{
    const double k = I.num(), y = I.num(), m = I.num(), c = I.num();
    I.setDevice("DeviceCMYK", 4, Interp::cmykColor(c, m, y, k));
}
void opSethsbcolor(Interp& I)
{
    const double v = I.num(), s = I.num(), h = I.num();
    const double i = std::floor(h * 6.0), f = h * 6.0 - i;
    const double p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    double r = v, g = t, b = p;
    switch (static_cast<int>(i) % 6)
    {
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    default: break;
    }
    I.setDevice("DeviceRGB", 3, Interp::rgbColor(r, g, b));
}
void opCurrentgray(Interp& I)
{
    const Color c = I.gs.color;
    I.push(Obj::real((0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]) / 255.0));
}
void opCurrentrgbcolor(Interp& I)
{
    const Color c = I.gs.color;
    for (int i = 0; i < 3; ++i)
        I.push(Obj::real(c[static_cast<size_t>(i)] / 255.0));
}
void opCurrentcmykcolor(Interp& I)
{
    const Color c = I.gs.color;
    const double r = c[0] / 255.0, g = c[1] / 255.0, b = c[2] / 255.0;
    const double k = 1.0 - std::max({r, g, b});
    const double d = std::max(1e-9, 1.0 - k);
    I.push(Obj::real((1 - r - k) / d));
    I.push(Obj::real((1 - g - k) / d));
    I.push(Obj::real((1 - b - k) / d));
    I.push(Obj::real(k));
}
void opSetcolorspace(Interp& I)
{
    Obj space = I.pop();
    GState& gs = I.gs;
    if (gs.colorLocked)
        return;
    std::string name;
    if ((space.t == Obj::T::Name || space.t == Obj::T::String) && space.str)
        name = *space.str;
    else if (space.t == Obj::T::Array && space.arr && !space.arr->empty() && (*space.arr)[0].str)
        name = *(*space.arr)[0].str;
    else
        throw PsError{"typecheck"};
    gs.space = name;
    gs.spaceObj = space;
    gs.spaceComponents = I.componentsOf(space);
    gs.pattern = Obj();
    // The space's initial color: black, a full tint, the first index.
    if (name == "Separation" || name == "DeviceN")
        gs.color = I.colorIn(space, std::vector<double>(static_cast<size_t>(std::max(1, gs.spaceComponents)), 1.0));
    else if (name == "DeviceCMYK")
        gs.color = Color{0, 0, 0, 255};
    else if (name != "Pattern")
        gs.color = I.colorIn(space, std::vector<double>(static_cast<size_t>(std::max(1, gs.spaceComponents)), 0.0));
}
void opSetcolor(Interp& I)
{
    if (!I.ops.empty() && I.top().t == Obj::T::Dict)
    {
        // A pattern; an uncolored one's color first, in the space under it.
        Obj pattern = I.pop();
        const Obj space = I.gs.spaceObj;
        if (space.t == Obj::T::Array && space.arr && space.arr->size() >= 2)
        {
            std::vector<double> values;
            const int n = std::max(1, I.componentsOf((*space.arr)[1]));
            for (int i = 0; i < n && !I.ops.empty() && I.top().isNumber(); ++i)
                values.insert(values.begin(), I.num());
            if (!I.gs.colorLocked)
                I.gs.color = I.colorIn((*space.arr)[1], values);
        }
        if (!I.gs.colorLocked)
            I.gs.pattern = pattern;
        return;
    }
    std::vector<double> values;
    const int n = std::max(1, I.gs.spaceComponents);
    for (int i = 0; i < n && !I.ops.empty() && I.top().isNumber(); ++i)
        values.insert(values.begin(), I.num());
    I.setColor(values);
}
void opCurrentcolorspace(Interp& I)
{
    const Obj& space = I.gs.spaceObj;
    I.push(space.t == Obj::T::Array ? space : Obj::array({Obj::name(I.gs.space, false)}));
}
void opSetpattern(Interp& I)
{
    Obj pattern = I.pop();
    if (pattern.t != Obj::T::Dict)
        throw PsError{"typecheck"};
    if (I.gs.colorLocked)
        return;
    // An uncolored pattern paints in the color already set.
    const Obj* paintType = entry(pattern, "PaintType");
    if (!(paintType && Interp::toInt(paintType->num) == 2))
    {
        I.gs.space = "Pattern";
        I.gs.spaceObj = Obj::name("Pattern", false);
        I.gs.spaceComponents = 0;
    }
    I.gs.pattern = pattern;
}

// -- matrices

void opMatrix(Interp& I) { I.push(matrixObj(Matrix())); }
void opIdentmatrix(Interp& I)
{
    Obj m = I.pop();
    I.push(matrixObj(Matrix()));
    (void)m;
}
void opCurrentmatrix(Interp& I)
{
    I.pop();
    I.push(matrixObj(I.gs.ctm));
}
void opSetmatrix(Interp& I) { I.gs.ctm = matrixOf(I.pop()); }
void opInitmatrix(Interp& I) { I.gs.ctm = Matrix(); }
void opDefaultmatrix(Interp& I)
{
    I.pop();
    I.push(matrixObj(Matrix()));
}
void opTranslate(Interp& I)
{
    Matrix m;
    const bool given = popMatrix(I, m);
    const double ty = I.num(), tx = I.num();
    const Matrix t{1, 0, 0, 1, tx, ty};
    if (given)
        I.push(matrixObj(t));
    else
        I.gs.ctm = I.gs.ctm.after(t);
}
void opScale(Interp& I)
{
    Matrix m;
    const bool given = popMatrix(I, m);
    const double sy = I.num(), sx = I.num();
    const Matrix s{sx, 0, 0, sy, 0, 0};
    if (given)
        I.push(matrixObj(s));
    else
        I.gs.ctm = I.gs.ctm.after(s);
}
void opRotate(Interp& I)
{
    Matrix m;
    const bool given = popMatrix(I, m);
    const double angle = I.num() * 3.14159265358979323846 / 180.0;
    const Matrix r{std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), 0, 0};
    if (given)
        I.push(matrixObj(r));
    else
        I.gs.ctm = I.gs.ctm.after(r);
}
void opConcat(Interp& I) { I.gs.ctm = I.gs.ctm.after(matrixOf(I.pop())); }
void opConcatmatrix(Interp& I)
{
    I.pop();
    const Matrix b = matrixOf(I.pop());
    const Matrix a = matrixOf(I.pop());
    I.push(matrixObj(b.after(a)));
}
void opInvertmatrix(Interp& I)
{
    I.pop();
    Matrix inverse;
    if (!matrixOf(I.pop()).invert(inverse))
        throw PsError{"undefinedresult"};
    I.push(matrixObj(inverse));
}
void pointOp(Interp& I, bool inverse, bool delta)
{
    Matrix m = I.gs.ctm;
    popMatrix(I, m);
    const double y = I.num(), x = I.num();
    if (inverse && !m.invert(m))
        throw PsError{"undefinedresult"};
    if (delta)
        m.e = m.f = 0;
    double ox, oy;
    m.apply(x, y, ox, oy);
    I.push(Obj::real(ox));
    I.push(Obj::real(oy));
}
void opTransform(Interp& I) { pointOp(I, false, false); }
void opItransform(Interp& I) { pointOp(I, true, false); }
void opDtransform(Interp& I) { pointOp(I, false, true); }
void opIdtransform(Interp& I) { pointOp(I, true, true); }

// -- paths

void opNewpath(Interp& I)
{
    I.gs.path.clear();
    I.gs.current = Subpath();
    I.gs.hasPoint = false;
}
void opMoveto(Interp& I)
{
    const double y = I.num(), x = I.num();
    I.moveTo(x, y);
}
void opRmoveto(Interp& I)
{
    const double dy = I.num(), dx = I.num();
    double x, y;
    I.userPoint(x, y);
    I.moveTo(x + dx, y + dy);
}
void opLineto(Interp& I)
{
    const double y = I.num(), x = I.num();
    I.lineTo(x, y);
}
void opRlineto(Interp& I)
{
    const double dy = I.num(), dx = I.num();
    double x, y;
    I.userPoint(x, y);
    I.lineTo(x + dx, y + dy);
}
void opCurveto(Interp& I)
{
    const double y3 = I.num(), x3 = I.num(), y2 = I.num(), x2 = I.num(), y1 = I.num(), x1 = I.num();
    I.curveTo(x1, y1, x2, y2, x3, y3);
}
void opRcurveto(Interp& I)
{
    const double y3 = I.num(), x3 = I.num(), y2 = I.num(), x2 = I.num(), y1 = I.num(), x1 = I.num();
    double x, y;
    I.userPoint(x, y);
    I.curveTo(x + x1, y + y1, x + x2, y + y2, x + x3, y + y3);
}
void arcOp(Interp& I, bool clockwise)
{
    double a2 = I.num(), a1 = I.num();
    const double r = I.num(), cy = I.num(), cx = I.num();
    const double pi = 3.14159265358979323846;
    if (!std::isfinite(a1) || !std::isfinite(a2) || !std::isfinite(a2 - a1))
        throw PsError{"undefinedresult"};
    // The second angle brought within a turn of the first, in the arc's
    // direction; past ten turns an arc only draws over itself.
    double sweep360 = a2 - a1;
    if (!clockwise && sweep360 < 0)
    {
        sweep360 = std::fmod(sweep360, 360.0);
        if (sweep360 < 0)
            sweep360 += 360.0;
    }
    else if (clockwise && sweep360 > 0)
    {
        sweep360 = std::fmod(sweep360, 360.0);
        if (sweep360 > 0)
            sweep360 -= 360.0;
    }
    a2 = a1 + std::clamp(sweep360, -3600.0, 3600.0);
    const double start = a1 * pi / 180.0;
    const double sweep = (a2 - a1) * pi / 180.0;
    const double sx = cx + r * std::cos(start), sy = cy + r * std::sin(start);
    if (I.gs.hasPoint)
        I.lineTo(sx, sy);
    else
        I.moveTo(sx, sy);
    const int pieces = std::max(1, static_cast<int>(std::ceil(std::fabs(sweep) / (pi / 2.0) - 1e-9)));
    const double step = sweep / pieces;
    const double k = 4.0 / 3.0 * std::tan(step / 4.0);
    double angle = start;
    for (int i = 0; i < pieces; ++i)
    {
        const double a = angle, b = angle + step;
        const double x0 = cx + r * std::cos(a), y0 = cy + r * std::sin(a);
        const double x3 = cx + r * std::cos(b), y3 = cy + r * std::sin(b);
        I.curveTo(x0 - k * r * std::sin(a), y0 + k * r * std::cos(a), x3 + k * r * std::sin(b), y3 - k * r * std::cos(b), x3, y3);
        angle = b;
    }
}
void opArc(Interp& I) { arcOp(I, false); }
void opArcn(Interp& I) { arcOp(I, true); }
void opArct(Interp& I)
{
    const double r = I.num(), y2 = I.num(), x2 = I.num(), y1 = I.num(), x1 = I.num();
    (void)r;
    (void)x2;
    (void)y2;
    I.lineTo(x1, y1);
}
void opArcto(Interp& I)
{
    opArct(I);
    for (int i = 0; i < 4; ++i)
        I.push(Obj::real(0));
}
void opClosepath(Interp& I) { I.closePath(); }
void opCurrentpoint(Interp& I)
{
    if (!I.gs.hasPoint)
        throw PsError{"nocurrentpoint"};
    double x, y;
    I.userPoint(x, y);
    I.push(Obj::real(x));
    I.push(Obj::real(y));
}
void opPathbbox(Interp& I)
{
    double l = 1e300, b = 1e300, r = -1e300, t = -1e300;
    std::vector<Subpath> all = I.gs.path;
    all.push_back(I.gs.current);
    for (const Subpath& s : all)
        for (const Segment& seg : s.segments)
            for (const Point& p : seg.points)
            {
                const double x = p.x + I.pageLeft, y = I.pageTop - p.y;
                l = std::min(l, x);
                r = std::max(r, x);
                b = std::min(b, y);
                t = std::max(t, y);
            }
    if (l > r)
        l = b = r = t = 0;
    I.push(Obj::real(l));
    I.push(Obj::real(b));
    I.push(Obj::real(r));
    I.push(Obj::real(t));
}
void opFill(Interp& I) { I.paint(true, false); }
void opStroke(Interp& I) { I.paint(false, true); }
void opClip(Interp& I)
{
    // The path clips what follows, and stays for the program to paint or
    // clear.
    std::vector<Subpath> area = I.gs.path;
    if (I.gs.current.segments.size() > 1)
        area.push_back(I.gs.current);
    for (Subpath& s : area)
        s.closed = true;
    if (!area.empty())
        I.gs.clips.push_back(area);
}
void opInitclip(Interp& I) { I.gs.clips.clear(); }
void opClippath(Interp& I)
{
    opNewpath(I);
    I.moveTo(0, 0);
    I.lineTo(612, 0);
    I.lineTo(612, 792);
    I.lineTo(0, 792);
    I.closePath();
}
void rectOp(Interp& I, bool fill)
{
    std::vector<double> numbers;
    if (I.top().t == Obj::T::Array)
    {
        for (const Obj& o : *I.pop().arr)
            numbers.push_back(o.num);
    }
    else
    {
        const double h = I.num(), w = I.num(), y = I.num(), x = I.num();
        numbers = {x, y, w, h};
    }
    I.gs.path.clear();
    I.gs.current = Subpath();
    for (size_t i = 0; i + 3 < numbers.size(); i += 4)
    {
        const double x = numbers[i], y = numbers[i + 1], w = numbers[i + 2], h = numbers[i + 3];
        I.moveTo(x, y);
        I.lineTo(x + w, y);
        I.lineTo(x + w, y + h);
        I.lineTo(x, y + h);
        I.closePath();
    }
    I.paint(fill, !fill);
}
void opRectfill(Interp& I) { rectOp(I, true); }
void opRectstroke(Interp& I)
{
    Matrix m;
    popMatrix(I, m);
    rectOp(I, false);
}
void opRectclip(Interp& I)
{
    std::vector<double> numbers;
    if (I.top().t == Obj::T::Array)
    {
        for (const Obj& o : *I.pop().arr)
            numbers.push_back(o.num);
    }
    else
    {
        const double h = I.num(), w = I.num(), y = I.num(), x = I.num();
        numbers = {x, y, w, h};
    }
    const std::vector<Subpath> keep = I.gs.path;
    const Subpath current = I.gs.current;
    I.gs.path.clear();
    I.gs.current = Subpath();
    for (size_t i = 0; i + 3 < numbers.size(); i += 4)
    {
        const double x = numbers[i], y = numbers[i + 1], w = numbers[i + 2], h = numbers[i + 3];
        I.moveTo(x, y);
        I.lineTo(x + w, y);
        I.lineTo(x + w, y + h);
        I.lineTo(x, y + h);
        I.closePath();
    }
    opClip(I);
    I.gs.path.clear();
    I.gs.current = Subpath();
    I.gs.hasPoint = false;
    (void)keep;
    (void)current;
}
// -- color spaces, functions, shadings and patterns

std::vector<double> Interp::runProcedure(const Obj& proc, const std::vector<double>& in)
{
    const size_t before = ops.size();
    for (double v : in)
        push(Obj::real(v));
    std::vector<double> out;
    try
    {
        run(proc);
    }
    catch (const PsError&)
    {
        ops.resize(std::min(ops.size(), before));
        return out;
    }
    while (ops.size() > before && top().isNumber())
        out.insert(out.begin(), pop().num);
    ops.resize(std::min(ops.size(), before));
    return out;
}

int Interp::componentsOf(const Obj& space, int depth) const
{
    std::string name;
    const Array* a = nullptr;
    if ((space.t == Obj::T::Name || space.t == Obj::T::String) && space.str)
        name = *space.str;
    else if (space.t == Obj::T::Array && space.arr && !space.arr->empty() && (*space.arr)[0].str)
    {
        name = *(*space.arr)[0].str;
        a = space.arr.get();
    }
    if (name == "DeviceRGB" || name == "CalRGB" || name == "CIEBasedABC" || name == "Lab")
        return 3;
    if (name == "DeviceCMYK")
        return 4;
    if (name == "DeviceN" && a && a->size() >= 2 && (*a)[1].t == Obj::T::Array)
        return static_cast<int>(std::clamp<size_t>((*a)[1].arr->size(), 1, 32));
    if (name == "ICCBased" && a && a->size() >= 2)
    {
        if (const Obj* n = entry((*a)[1], "N"))
            return std::clamp(toInt(n->num), 1, 4);
        return 3;
    }
    if (name == "Pattern" && a && a->size() >= 2 && depth < 8)
        return componentsOf((*a)[1], depth + 1);
    if (name == "Pattern")
        return 0;
    return 1;
}

Color Interp::colorIn(const Obj& space, const std::vector<double>& v, int depth)
{
    const auto at = [&v](size_t i) { return i < v.size() ? v[i] : 0.0; };
    std::string name;
    const Array* a = nullptr;
    if ((space.t == Obj::T::Name || space.t == Obj::T::String) && space.str)
        name = *space.str;
    else if (space.t == Obj::T::Array && space.arr && !space.arr->empty() && (*space.arr)[0].str)
    {
        name = *(*space.arr)[0].str;
        a = space.arr.get();
    }
    if (depth > 8)
        name.clear();
    if (name == "DeviceRGB" || name == "CalRGB" || name == "CIEBasedABC")
        return rgbColor(at(0), at(1), at(2));
    if (name == "DeviceCMYK")
        return cmykColor(at(0), at(1), at(2), at(3));
    if (name == "DeviceGray" || name == "CalGray" || name == "CIEBasedA")
        return grayColor(at(0));
    if (name == "Lab")
        return grayColor(at(0) / 100.0);
    if (name == "ICCBased")
    {
        const int n = componentsOf(space);
        return n == 4 ? cmykColor(at(0), at(1), at(2), at(3)) : n == 3 ? rgbColor(at(0), at(1), at(2)) : grayColor(at(0));
    }
    if ((name == "Separation" || name == "DeviceN") && a && a->size() >= 4)
    {
        // Through the tint transform into the alternate space; with none,
        // the tint as a shade of gray.
        if ((*a)[3].isProc())
        {
            const std::vector<double> out = runProcedure((*a)[3], v);
            if (!out.empty())
                return colorIn((*a)[2], out, depth + 1);
        }
        return grayColor(1.0 - at(0));
    }
    if (name == "Indexed" && a && a->size() >= 4)
    {
        const Obj& base = (*a)[1];
        const int hival = std::max(0, toInt((*a)[2].num));
        const int index = std::clamp(toInt(std::floor(at(0) + 0.5)), 0, hival);
        const int n = std::max(1, componentsOf(base, depth + 1));
        std::vector<double> values;
        const Obj& lookup = (*a)[3];
        if (lookup.t == Obj::T::String && lookup.str)
        {
            for (int c = 0; c < n; ++c)
            {
                const size_t k = static_cast<size_t>(index) * static_cast<size_t>(n) + static_cast<size_t>(c);
                values.push_back(k < lookup.str->size() ? static_cast<unsigned char>((*lookup.str)[k]) / 255.0 : 0.0);
            }
        }
        else if (lookup.isProc())
            values = runProcedure(lookup, {static_cast<double>(index)});
        return colorIn(base, values, depth + 1);
    }
    return v.size() >= 4 ? cmykColor(at(0), at(1), at(2), at(3))
        : v.size() == 3  ? rgbColor(at(0), at(1), at(2))
                         : grayColor(at(0));
}

std::vector<double> Interp::evaluate(const Obj& function, double t, int depth)
{
    std::vector<double> out;
    if (depth > 8)
        return out;
    if (function.t == Obj::T::Array && !function.exec && function.arr)
    {
        // One function a component.
        for (const Obj& part : *function.arr)
        {
            const std::vector<double> one = evaluate(part, t, depth + 1);
            out.push_back(one.empty() ? 0.0 : one[0]);
        }
        return out;
    }
    if (function.isProc())
        return runProcedure(function, {t});
    const Obj* typeEntry = entry(function, "FunctionType");
    if (!typeEntry)
        return out;
    const int type = toInt(typeEntry->num);
    std::vector<double> domain = numbersIn(entry(function, "Domain"));
    if (domain.size() < 2)
        domain = {0.0, 1.0};
    t = std::clamp(t, std::min(domain[0], domain[1]), std::max(domain[0], domain[1]));
    const std::vector<double> range = numbersIn(entry(function, "Range"));
    const auto clipped = [&range](std::vector<double> values) {
        for (size_t i = 0; i < values.size() && 2 * i + 1 < range.size(); ++i)
            values[i] = std::clamp(values[i], range[2 * i], range[2 * i + 1]);
        return values;
    };
    if (type == 2)
    {
        std::vector<double> c0 = numbersIn(entry(function, "C0"));
        std::vector<double> c1 = numbersIn(entry(function, "C1"));
        if (c0.empty())
            c0 = {0.0};
        if (c1.empty())
            c1 = {1.0};
        const Obj* nEntry = entry(function, "N");
        const double n = nEntry ? nEntry->num : 1.0;
        const double x = std::pow(std::max(0.0, t), n);
        for (size_t i = 0; i < std::min(c0.size(), c1.size()); ++i)
            out.push_back(c0[i] + x * (c1[i] - c0[i]));
        return clipped(out);
    }
    if (type == 3)
    {
        const Obj* functions = entry(function, "Functions");
        if (!functions || functions->t != Obj::T::Array || functions->arr->empty())
            return out;
        const std::vector<double> bounds = numbersIn(entry(function, "Bounds"));
        const std::vector<double> encode = numbersIn(entry(function, "Encode"));
        const size_t k = functions->arr->size();
        size_t i = 0;
        while (i < bounds.size() && i + 1 < k && t >= bounds[i])
            ++i;
        const double low = i == 0 ? domain[0] : bounds[i - 1];
        const double high = i < bounds.size() ? bounds[i] : domain[1];
        const double e0 = 2 * i < encode.size() ? encode[2 * i] : 0.0;
        const double e1 = 2 * i + 1 < encode.size() ? encode[2 * i + 1] : 1.0;
        const double u = high != low ? e0 + (t - low) * (e1 - e0) / (high - low) : e0;
        return clipped(evaluate((*functions->arr)[i], u, depth + 1));
    }
    if (type == 0)
    {
        // Sampled, one input: interpolated between the two nearest samples.
        const std::vector<double> size = numbersIn(entry(function, "Size"));
        const Obj* bitsEntry = entry(function, "BitsPerSample");
        const Obj* source = entry(function, "DataSource");
        if (size.empty() || !bitsEntry || !source || range.size() < 2)
            return out;
        const int bits = toInt(bitsEntry->num);
        const int count = std::clamp(toInt(size[0]), 1, 65536);
        if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 12 && bits != 16 && bits != 24 && bits != 32)
            return out;
        const size_t outputs = range.size() / 2;
        std::vector<uint8_t> data;
        if (source->t == Obj::T::String && source->str)
            data.assign(source->str->begin(), source->str->end());
        else if (source->t == Obj::T::File && source->file)
        {
            ready(*this, *source->file);
            data = source->file->bytes;
        }
        std::vector<double> encode = numbersIn(entry(function, "Encode"));
        if (encode.size() < 2)
            encode = {0.0, static_cast<double>(count - 1)};
        std::vector<double> decode = numbersIn(entry(function, "Decode"));
        if (decode.size() < 2 * outputs)
            decode = range;
        const double e = encode[0] + (t - domain[0]) * (encode[1] - encode[0]) / (domain[1] != domain[0] ? domain[1] - domain[0] : 1.0);
        const double position = std::clamp(e, 0.0, static_cast<double>(count - 1));
        const int i0 = static_cast<int>(std::floor(position));
        const int i1 = std::min(count - 1, i0 + 1);
        const double f = position - i0;
        const double top = bits >= 32 ? 4294967295.0 : static_cast<double>((uint64_t(1) << bits) - 1);
        const auto sample = [&](int index, size_t c) {
            const size_t bit = (static_cast<size_t>(index) * outputs + c) * static_cast<size_t>(bits);
            uint64_t value = 0;
            for (int b = 0; b < bits; ++b)
            {
                const size_t at = bit + static_cast<size_t>(b);
                const int on = at / 8 < data.size() ? (data[at / 8] >> (7 - static_cast<int>(at % 8))) & 1 : 0;
                value = (value << 1) | static_cast<uint64_t>(on);
            }
            return static_cast<double>(value) / top;
        };
        for (size_t c = 0; c < outputs; ++c)
        {
            const double s = sample(i0, c) * (1 - f) + sample(i1, c) * f;
            out.push_back(decode[2 * c] + s * (decode[2 * c + 1] - decode[2 * c]));
        }
        return clipped(out);
    }
    return out;
}

/// \a stops brought to at most fifteen (what an FLA gradient holds),
/// dropping first those its neighbors draw anyway.
std::vector<Gradient::Stop> fewerStops(std::vector<Gradient::Stop> stops)
{
    const auto apart = [](const Gradient::Stop& a, const Gradient::Stop& b, const Gradient::Stop& c) {
        // How far b's color is from the line between a's and c's.
        const double f = c.offset > a.offset ? (b.offset - a.offset) / (c.offset - a.offset) : 0.0;
        double worst = 0.0;
        for (size_t k = 0; k < 4; ++k)
            worst = std::max(worst, std::fabs(a.color[k] + f * (c.color[k] - a.color[k]) - b.color[k]));
        return worst;
    };
    for (double tolerance = 1.5;; tolerance *= 2.0)
    {
        bool removed = true;
        while (removed && stops.size() > 2)
        {
            removed = false;
            size_t best = 0;
            double least = 1e300;
            for (size_t i = 1; i + 1 < stops.size(); ++i)
            {
                const double d = apart(stops[i - 1], stops[i], stops[i + 1]);
                if (d < least)
                {
                    least = d;
                    best = i;
                }
            }
            if (best > 0 && (least <= tolerance || stops.size() > 15))
            {
                stops.erase(stops.begin() + static_cast<std::ptrdiff_t>(best));
                removed = true;
            }
        }
        if (stops.size() <= 15 || tolerance > 1000.0)
            return stops;
    }
}

std::optional<Gradient> Interp::shadingGradient(const Obj& shading, const Matrix& toDevice)
{
    const Obj* typeEntry = entry(shading, "ShadingType");
    const Obj* function = entry(shading, "Function");
    if (!typeEntry || !function)
        return std::nullopt;
    const int type = toInt(typeEntry->num);
    const std::vector<double> coords = numbersIn(entry(shading, "Coords"));
    if ((type != 2 || coords.size() < 4) && (type != 3 || coords.size() < 6))
        return std::nullopt;
    Obj space = Obj::name("DeviceGray", false);
    if (const Obj* s = entry(shading, "ColorSpace"))
        space = *s;
    std::vector<double> domain = numbersIn(entry(shading, "Domain"));
    if (domain.size() < 2)
        domain = {0.0, 1.0};
    // The function sampled along the ramp, at its stitches too, then thinned.
    std::vector<double> at;
    for (int i = 0; i <= 32; ++i)
        at.push_back(i / 32.0);
    if (const Obj* bounds = entry(*function, "Bounds"))
        for (double b : numbersIn(bounds))
            if (domain[1] != domain[0])
                for (double e : {-1e-6, 1e-6})
                    at.push_back(std::clamp((b - domain[0]) / (domain[1] - domain[0]) + e, 0.0, 1.0));
    std::sort(at.begin(), at.end());
    Gradient gradient;
    for (double u : at)
    {
        const std::vector<double> values = evaluate(*function, domain[0] + u * (domain[1] - domain[0]));
        if (values.empty())
            return std::nullopt;
        gradient.stops.push_back(Gradient::Stop{u, colorIn(space, values)});
    }
    gradient.stops = fewerStops(std::move(gradient.stops));
    const Matrix toStage = Matrix{1, 0, 0, -1, -pageLeft, pageTop}.after(toDevice);
    const double square = 1638.4;
    Matrix local;
    if (type == 2)
    {
        // The square's x along the axis, its y across it.
        const double x0 = coords[0], y0 = coords[1], dx = coords[2] - x0, dy = coords[3] - y0;
        local = Matrix{dx / square, dy / square, -dy / square, dx / square, x0 + dx / 2, y0 + dy / 2};
    }
    else
    {
        // On the outer circle, turned toward the inner one's center, the
        // colors starting out at the inner circle.
        const double radius = coords[5];
        if (!(radius > 0.0))
            return std::nullopt;
        const double ix = coords[0], iy = coords[1], ox = coords[3], oy = coords[4];
        const double toward = std::hypot(ix - ox, iy - oy);
        const double angle = toward > 1e-9 ? std::atan2(iy - oy, ix - ox) : 0.0;
        const double k = radius / (square / 2.0);
        local = Matrix{std::cos(angle) * k, std::sin(angle) * k, -std::sin(angle) * k, std::cos(angle) * k, ox, oy};
        gradient.radial = true;
        gradient.focal = std::clamp(toward / radius, 0.0, 0.99);
        const double start = std::clamp(coords[2] / radius, 0.0, 0.99);
        for (Gradient::Stop& stop : gradient.stops)
            stop.offset = start + stop.offset * (1.0 - start);
    }
    const Matrix placed = toStage.after(local);
    gradient.placement = Placement{placed.a, placed.b, placed.c, placed.d, placed.e, placed.f};
    return gradient;
}

/// A pattern's space onto the device: what makepattern kept with it.
Matrix patternMatrix(const Obj& pattern)
{
    if (const Obj* m = entry(pattern, "#PatternMatrix"); m && m->t == Obj::T::Array && m->arr->size() >= 6)
        return matrixOf(*m);
    return Matrix();
}

void Interp::fillWithPattern(std::vector<Subpath> area, bool stroke)
{
    const Obj pattern = gs.pattern;
    const Obj* typeEntry = entry(pattern, "PatternType");
    const int type = typeEntry ? toInt(typeEntry->num) : 1;
    for (Subpath& s : area)
        s.closed = true;
    const auto plain = [&](std::optional<Gradient> gradient, Color color) {
        Painted painted;
        painted.subpaths = area;
        painted.fill = gradient && !gradient->stops.empty() ? gradient->stops.front().color : color;
        painted.gradient = std::move(gradient);
        if (stroke)
        {
            painted.stroke = gs.color;
            painted.strokeWidth = std::max(0.1, gs.lineWidth * gs.ctm.scale());
        }
        painted.clips = gs.clips;
        art.order.emplace_back(Artwork::Kind::Path, art.paths.size());
        art.paths.push_back(std::move(painted));
    };
    if (type == 2)
    {
        const Obj* shading = entry(pattern, "Shading");
        std::optional<Gradient> gradient = shading ? shadingGradient(*shading, patternMatrix(pattern)) : std::nullopt;
        if (!gradient)
            note(art, "Some shadings are of kinds Phoenix does not read and came in as plain color.");
        plain(gradient, gs.color);
        return;
    }
    // A tiling pattern: its cell painted wherever the area reaches, each
    // tile cut to the area.
    const std::vector<double> box = numbersIn(entry(pattern, "BBox"));
    const Obj* xEntry = entry(pattern, "XStep");
    const Obj* yEntry = entry(pattern, "YStep");
    const Obj* proc = entry(pattern, "PaintProc");
    const double xs = xEntry ? std::fabs(xEntry->num) : 0.0, ys = yEntry ? std::fabs(yEntry->num) : 0.0;
    const Matrix toDevice = patternMatrix(pattern);
    const Matrix toStage = Matrix{1, 0, 0, -1, -pageLeft, pageTop}.after(toDevice);
    Matrix inverse;
    long i0 = 0, i1 = -1, j0 = 0, j1 = -1;
    if (box.size() >= 4 && proc && proc->isProc() && xs > 1e-9 && ys > 1e-9 && toStage.invert(inverse))
    {
        double left = 1e300, top = 1e300, right = -1e300, bottom = -1e300;
        for (const Subpath& s : area)
            for (const Segment& seg : s.segments)
                for (const Point& pt : seg.points)
                {
                    left = std::min(left, pt.x);
                    right = std::max(right, pt.x);
                    top = std::min(top, pt.y);
                    bottom = std::max(bottom, pt.y);
                }
        double px0 = 1e300, py0 = 1e300, px1 = -1e300, py1 = -1e300;
        for (const Point& corner : {Point(left, top), Point(right, top), Point(left, bottom), Point(right, bottom)})
        {
            double qx, qy;
            inverse.apply(corner.x, corner.y, qx, qy);
            px0 = std::min(px0, qx);
            px1 = std::max(px1, qx);
            py0 = std::min(py0, qy);
            py1 = std::max(py1, qy);
        }
        const double limit = 1e6;
        if (std::fabs(px0) < limit * xs && std::fabs(px1) < limit * xs && std::fabs(py0) < limit * ys &&
            std::fabs(py1) < limit * ys)
        {
            i0 = static_cast<long>(std::floor((px0 - std::max(box[0], box[2])) / xs));
            i1 = static_cast<long>(std::ceil((px1 - std::min(box[0], box[2])) / xs));
            j0 = static_cast<long>(std::floor((py0 - std::max(box[1], box[3])) / ys));
            j1 = static_cast<long>(std::ceil((py1 - std::min(box[1], box[3])) / ys));
        }
    }
    const long count = (i1 - i0 + 1) * (j1 - j0 + 1);
    if (i1 < i0 || j1 < j0 || count > 2500 || tiles + static_cast<size_t>(count) > 20000 || patternDepth >= 4)
    {
        if (i1 >= i0 && j1 >= j0)
            note(art, "Pattern fills too fine to draw tile by tile came in as plain colors.");
        plain(std::nullopt, tileColor(pattern));
        return;
    }
    tiles += static_cast<size_t>(count);
    const Obj* paintType = entry(pattern, "PaintType");
    const bool uncolored = paintType && toInt(paintType->num) == 2;
    const GState outer = gs;
    const size_t stackDepth = ops.size();
    ++patternDepth;
    try
    {
        for (long i = i0; i <= i1; ++i)
            for (long j = j0; j <= j1; ++j)
            {
                gs = outer;
                gs.ctm = toDevice.after(Matrix{1, 0, 0, 1, i * xs, j * ys});
                gs.clips.push_back(area);
                gs.path.clear();
                gs.current = Subpath();
                gs.hasPoint = false;
                gs.pattern = Obj();
                if (!uncolored)
                {
                    gs.color = Color{0, 0, 0, 255};
                    gs.space = "DeviceGray";
                    gs.spaceObj = Obj::name("DeviceGray", false);
                    gs.spaceComponents = 1;
                }
                gs.colorLocked = uncolored;
                push(pattern);
                run(*proc);
                ops.resize(std::min(ops.size(), stackDepth));
            }
    }
    catch (...)
    {
        --patternDepth;
        gs = outer;
        ops.resize(std::min(ops.size(), stackDepth));
        throw;
    }
    --patternDepth;
    gs = outer;
    if (stroke)
    {
        Painted outline;
        outline.subpaths = area;
        outline.stroke = gs.color;
        outline.strokeWidth = std::max(0.1, gs.lineWidth * gs.ctm.scale());
        outline.clips = gs.clips;
        art.order.emplace_back(Artwork::Kind::Path, art.paths.size());
        art.paths.push_back(std::move(outline));
    }
}

Color Interp::tileColor(const Obj& pattern)
{
    // One tile painted aside: the color of the first thing it fills.
    const Obj* paintType = entry(pattern, "PaintType");
    if (paintType && toInt(paintType->num) == 2)
        return gs.color;
    const Obj* proc = entry(pattern, "PaintProc");
    if (!proc || !proc->isProc() || patternDepth >= 4)
        return gs.color;
    const size_t paths = art.paths.size(), images = art.images.size(), texts = art.texts.size(),
                 order = art.order.size(), notes = art.notes.size();
    const GState outer = gs;
    const size_t stackDepth = ops.size();
    Color found = gs.color;
    ++patternDepth;
    try
    {
        gs.pattern = Obj();
        gs.ctm = patternMatrix(pattern);
        gs.path.clear();
        gs.current = Subpath();
        gs.hasPoint = false;
        push(pattern);
        run(*proc);
    }
    catch (const PsError&)
    {
    }
    --patternDepth;
    for (size_t k = paths; k < art.paths.size(); ++k)
        if (art.paths[k].fill || art.paths[k].stroke)
        {
            found = art.paths[k].fill ? *art.paths[k].fill : *art.paths[k].stroke;
            break;
        }
    art.paths.resize(paths);
    art.images.resize(images);
    art.texts.resize(texts);
    art.order.resize(order);
    art.notes.resize(notes);
    gs = outer;
    ops.resize(std::min(ops.size(), stackDepth));
    return found;
}

void opShfill(Interp& I)
{
    Obj shading = I.pop();
    if (shading.t != Obj::T::Dict)
        throw PsError{"typecheck"};
    // The area: what clips it, else the shading's box, else the page.
    std::vector<Subpath> area;
    std::vector<std::vector<Subpath>> clips = I.gs.clips;
    if (!clips.empty())
    {
        area = clips.back();
        clips.pop_back();
    }
    else
    {
        std::vector<double> box = numbersIn(entry(shading, "BBox"));
        Subpath rect;
        const auto corner = [&](double x, double y, bool user) {
            double dx = x, dy = y;
            if (user)
                I.gs.ctm.apply(x, y, dx, dy);
            return I.stage(dx, dy);
        };
        const bool user = box.size() >= 4;
        if (!user)
            box = {I.pageLeft, I.pageTop - I.art.height, I.pageLeft + I.art.width, I.pageTop};
        rect.segments.push_back(Segment{Segment::Kind::Move, {corner(box[0], box[1], user)}});
        rect.segments.push_back(Segment{Segment::Kind::Line, {corner(box[2], box[1], user)}});
        rect.segments.push_back(Segment{Segment::Kind::Line, {corner(box[2], box[3], user)}});
        rect.segments.push_back(Segment{Segment::Kind::Line, {corner(box[0], box[3], user)}});
        rect.closed = true;
        area.push_back(rect);
    }
    std::optional<Gradient> gradient = I.shadingGradient(shading, I.gs.ctm);
    Painted painted;
    painted.subpaths = std::move(area);
    painted.clips = std::move(clips);
    if (gradient)
    {
        painted.fill = gradient->stops.front().color;
        painted.gradient = std::move(gradient);
    }
    else
    {
        // Meshes and function shadings: their background, else left out.
        const std::vector<double> background = numbersIn(entry(shading, "Background"));
        Obj space = Obj::name("DeviceGray", false);
        if (const Obj* s = entry(shading, "ColorSpace"))
            space = *s;
        if (background.empty())
        {
            I.sawShading = true;
            return;
        }
        painted.fill = I.colorIn(space, background);
        I.sawShading = true;
    }
    I.art.order.emplace_back(Artwork::Kind::Path, I.art.paths.size());
    I.art.paths.push_back(std::move(painted));
}

void opMakepattern(Interp& I)
{
    const Matrix m = matrixOf(I.pop());
    Obj pattern = I.pop();
    if (pattern.t != Obj::T::Dict)
        throw PsError{"typecheck"};
    Obj copy = Obj::dictionary();
    *copy.dict = *pattern.dict;
    (*copy.dict)["#PatternMatrix"] = matrixObj(I.gs.ctm.after(m));
    I.push(copy);
}

// -- fonts and text

/// The fonts definefont made, by name.
Dict& fontDirectory(Interp& I)
{
    Obj& directory = (*I.systemdict)["FontDirectory"];
    if (directory.t != Obj::T::Dict)
        directory = Obj::dictionary();
    return *directory.dict;
}
void opFindfont(Interp& I)
{
    Obj name = I.pop();
    // A font the program defined, else one standing for a standard font.
    const Dict& directory = fontDirectory(I);
    const auto defined = directory.find(I.keyOf(name));
    if (defined != directory.end() && defined->second.t == Obj::T::Dict)
    {
        I.push(defined->second);
        return;
    }
    Obj font = Obj::dictionary();
    (*font.dict)["FontName"] = Obj::name(name.str ? *name.str : "Helvetica", false);
    (*font.dict)["FontMatrix"] = matrixObj(Matrix{0.001, 0, 0, 0.001, 0, 0});
    (*font.dict)["FontType"] = Obj::integer(1);
    if (const Obj* standard = I.lookup("StandardEncoding"))
        (*font.dict)["Encoding"] = *standard;
    (*font.dict)["FontBBox"] = Obj::array({Obj::integer(0), Obj::integer(-200), Obj::integer(1000), Obj::integer(800)});
    (*font.dict)["CharStrings"] = Obj::dictionary();
    (*font.dict)["PaintType"] = Obj::integer(0);
    I.push(font);
}
void opDefinefont(Interp& I)
{
    Obj font = I.pop();
    Obj key = I.pop();
    if (font.t != Obj::T::Dict)
        throw PsError{"typecheck"};
    if (!font.dict->count("FontName") && (key.t == Obj::T::Name || key.t == Obj::T::String))
        (*font.dict)["FontName"] = Obj::name(*key.str, false);
    Dict& directory = fontDirectory(I);
    if (directory.size() < 10000)
        directory[I.keyOf(key)] = font;
    I.push(font);
}
Obj scaledFont(const Obj& font, const Matrix& by)
{
    Obj copy = Obj::dictionary();
    if (font.t == Obj::T::Dict)
        *copy.dict = *font.dict;
    Matrix base{0.001, 0, 0, 0.001, 0, 0};
    const auto found = copy.dict->find("FontMatrix");
    if (found != copy.dict->end() && found->second.t == Obj::T::Array && found->second.arr->size() == 6)
        base = matrixOf(found->second);
    // The font's own units, kept through scaling for its widths.
    if (!copy.dict->count("#UnitScale"))
        (*copy.dict)["#UnitScale"] = Obj::real(base.a);
    (*copy.dict)["FontMatrix"] = matrixObj(by.after(base));
    return copy;
}
void opScalefont(Interp& I)
{
    const double s = I.num();
    Obj font = I.pop();
    I.push(scaledFont(font, Matrix{s, 0, 0, s, 0, 0}));
}
void opMakefont(Interp& I)
{
    const Matrix m = matrixOf(I.pop());
    Obj font = I.pop();
    I.push(scaledFont(font, m));
}
void opSetfont(Interp& I)
{
    Obj font = I.pop();
    if (font.t != Obj::T::Dict)
        throw PsError{"typecheck"};
    const auto name = font.dict->find("FontName");
    I.gs.font.name = name != font.dict->end() && name->second.str ? *name->second.str : "Helvetica";
    const auto matrix = font.dict->find("FontMatrix");
    I.gs.font.matrix = matrix != font.dict->end() && matrix->second.t == Obj::T::Array && matrix->second.arr->size() == 6
        ? matrixOf(matrix->second)
        : Matrix{0.001, 0, 0, 0.001, 0, 0};
    const auto unit = font.dict->find("#UnitScale");
    I.gs.font.base = unit != font.dict->end() && unit->second.isNumber() ? unit->second.num : I.gs.font.matrix.a;
    I.gs.font.dict = font.dict;
}
void opCurrentfont(Interp& I)
{
    Obj font = Obj::dictionary();
    if (I.gs.font.dict)
        *font.dict = *I.gs.font.dict;
    (*font.dict)["FontName"] = Obj::name(I.gs.font.name, false);
    (*font.dict)["FontMatrix"] = matrixObj(I.gs.font.matrix);
    I.push(font);
}
void opSelectfont(Interp& I)
{
    Obj size = I.pop();
    Obj name = I.pop();
    I.push(name);
    opFindfont(I);
    if (size.t == Obj::T::Array)
    {
        I.push(size);
        opMakefont(I);
    }
    else
    {
        I.push(size);
        opScalefont(I);
    }
    opSetfont(I);
}
void opShow(Interp& I)
{
    Obj s = I.pop();
    if (s.t != Obj::T::String)
        throw PsError{"typecheck"};
    I.show(*s.str);
}
/// The spacing awidthshow adds to \a text: \a ax, \a ay after every
/// character and \a cx, \a cy more after each \a c.
void spacedShow(Interp& I, const std::string& text, double ax, double ay, int c, double cx, double cy)
{
    const auto count = static_cast<double>(text.size());
    const auto matches = static_cast<double>(c < 0 ? 0 : std::count(text.begin(), text.end(), static_cast<char>(c)));
    I.show(text, ax * count + cx * matches, ay * count + cy * matches);
}
void opAshow(Interp& I)
{
    Obj s = I.pop();
    const double ay = I.num(), ax = I.num();
    if (s.t != Obj::T::String)
        throw PsError{"typecheck"};
    spacedShow(I, *s.str, ax, ay, -1, 0, 0);
}
void opWidthshow(Interp& I)
{
    Obj s = I.pop();
    const int c = static_cast<int>(I.num());
    const double cy = I.num(), cx = I.num();
    if (s.t != Obj::T::String)
        throw PsError{"typecheck"};
    spacedShow(I, *s.str, 0, 0, c, cx, cy);
}
void opAwidthshow(Interp& I)
{
    Obj s = I.pop();
    const double ay = I.num(), ax = I.num();
    const int c = static_cast<int>(I.num());
    const double cy = I.num(), cx = I.num();
    if (s.t != Obj::T::String)
        throw PsError{"typecheck"};
    spacedShow(I, *s.str, ax, ay, c, cx, cy);
}
/// xshow, yshow and xyshow: each glyph moves on by the numbers given
/// rather than by its width; the text lands where the first glyph does,
/// and the point ends past them all.
void displacedShow(Interp& I, bool xs, bool ys)
{
    Obj numbers = I.pop();
    Obj s = I.pop();
    if (s.t != Obj::T::String)
        throw PsError{"typecheck"};
    double wx, wy;
    I.textWidth(*s.str, wx, wy);
    double sx = 0.0, sy = 0.0;
    if (numbers.t == Obj::T::Array)
    {
        const Array& list = *numbers.arr;
        const size_t step = xs && ys ? 2 : 1;
        for (size_t k = 0; k + step <= list.size(); k += step)
        {
            const double first = list[k].isNumber() ? list[k].num : 0.0;
            if (xs)
                sx += first;
            if (ys)
                sy += step == 2 ? (list[k + 1].isNumber() ? list[k + 1].num : 0.0) : first;
        }
    }
    I.show(*s.str, sx - wx, sy - wy);
}
void opXshow(Interp& I) { displacedShow(I, true, false); }
void opYshow(Interp& I) { displacedShow(I, false, true); }
void opXyshow(Interp& I) { displacedShow(I, true, true); }
void opKshow(Interp& I)
{
    Obj s = textOperand(I.pop());
    I.pop();
    I.show(*s.str);
}
void opStringwidth(Interp& I)
{
    // The standard metrics of the nearest font.
    Obj s = I.pop();
    double wx = 0, wy = 0;
    if (s.str)
        I.textWidth(*s.str, wx, wy);
    I.push(Obj::real(wx));
    I.push(Obj::real(wy));
}
void opCharpath(Interp& I)
{
    I.pop();
    I.pop();
}
void opGlyphshow(Interp& I) { I.pop(); }

// -- files

void opCurrentfile(Interp& I)
{
    Obj f;
    f.t = Obj::T::File;
    f.file = I.program;
    I.push(f);
}
std::shared_ptr<Source> sourceOf(Interp& I, const Obj& o)
{
    if (o.t == Obj::T::File)
        return o.file;
    if (o.t == Obj::T::String)
    {
        auto s = std::make_shared<Source>();
        s->bytes.assign(o.str->begin(), o.str->end());
        return s;
    }
    (void)I;
    throw PsError{"typecheck"};
}
void opFilter(Interp& I)
{
    Obj name = I.pop();
    const std::string filter = name.str ? *name.str : std::string();
    // SubFileDecode takes a count and an end string; others may take a
    // dictionary of parameters.
    Obj end;
    if (filter == "SubFileDecode")
    {
        end = I.pop();
        I.pop();
    }
    else if (!I.ops.empty() && I.top().t == Obj::T::Dict)
        I.pop();
    Obj source = I.pop();
    Obj f;
    f.t = Obj::T::File;
    f.file = std::make_shared<Source>();
    f.file->upstream = sourceOf(I, source);
    f.file->filter = filter;
    f.file->end = end.t == Obj::T::String && end.str ? *end.str : std::string();
    f.file->pending = true;
    f.file->chain = f.file->upstream->chain + 1;
    // Filters on filters past reason.
    if (f.file->chain > 64)
        throw PsError{"limitcheck"};
    I.push(f);
}
void opReadhexstring(Interp& I)
{
    Obj buffer = textOperand(I.pop());
    Obj file = I.pop();
    Source& src = fileOperand(file);
    ready(I, src);
    std::string& out = *buffer.str;
    size_t filled = 0;
    int high = -1;
    while (filled < out.size() && src.at < src.bytes.size())
    {
        const int d = src.bytes[src.at++];
        const int v = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10
            : d >= 'A' && d <= 'F'                                           ? d - 'A' + 10
                                                                             : -1;
        if (v < 0)
            continue;
        if (high < 0)
            high = v;
        else
        {
            out[filled++] = static_cast<char>(high * 16 + v);
            high = -1;
        }
    }
    I.push(Obj::string(out.substr(0, filled)));
    I.push(Obj::boolean(filled == out.size()));
}
void opReadstring(Interp& I)
{
    Obj buffer = textOperand(I.pop());
    Obj file = I.pop();
    Source& src = fileOperand(file);
    ready(I, src);
    const size_t n = std::min(buffer.str->size(), src.bytes.size() - std::min(src.at, src.bytes.size()));
    std::string out(src.bytes.begin() + static_cast<std::ptrdiff_t>(src.at),
        src.bytes.begin() + static_cast<std::ptrdiff_t>(src.at + n));
    src.at += n;
    buffer.str->replace(0, n, out);
    I.push(Obj::string(out));
    I.push(Obj::boolean(n == buffer.str->size()));
}
void opReadline(Interp& I)
{
    Obj buffer = textOperand(I.pop());
    Obj file = I.pop();
    Source& src = fileOperand(file);
    ready(I, src);
    std::string out;
    bool found = false;
    while (src.at < src.bytes.size())
    {
        const char c = static_cast<char>(src.bytes[src.at++]);
        if (c == '\n' || c == '\r')
        {
            if (c == '\r' && src.at < src.bytes.size() && src.bytes[src.at] == '\n')
                ++src.at;
            found = true;
            break;
        }
        out += c;
    }
    I.push(Obj::string(out));
    I.push(Obj::boolean(found));
}
void opRead(Interp& I)
{
    Obj file = I.pop();
    Source& src = fileOperand(file);
    ready(I, src);
    if (src.at < src.bytes.size())
    {
        I.push(Obj::integer(src.bytes[src.at++]));
        I.push(Obj::boolean(true));
    }
    else
        I.push(Obj::boolean(false));
}
void opFlushfile(Interp& I)
{
    Obj file = I.pop();
    if (file.t == Obj::T::File && file.file && !file.file->program)
    {
        ready(I, *file.file);
        file.file->at = file.file->bytes.size();
    }
}
void opClosefile(Interp& I)
{
    // Closing the program being run (an encrypted font's end) ends it.
    Obj file = I.pop();
    if (file.t == Obj::T::File && file.file && file.file == I.program && I.eexecDepth > 0)
        file.file->at = file.file->bytes.size();
}
void opEexec(Interp& I)
{
    // An encrypted font program: hex or binary from where the file stands
    // to the zeros that close it, decrypted and run as the program.
    Obj file = I.pop();
    std::shared_ptr<Source> src = sourceOf(I, file);
    ready(I, *src);
    if (I.eexecDepth >= 4)
        throw PsError{"limitcheck"};
    const std::vector<uint8_t>& bytes = src->bytes;
    size_t at = std::min(src->at, bytes.size());
    while (at < bytes.size() && Scanner::white(bytes[at]))
        ++at;
    const auto hexDigit = [](int d) {
        return d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10 : d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;
    };
    bool hex = true;
    for (size_t k = at, n = 0; n < 4; ++k)
    {
        if (k >= bytes.size() || hexDigit(bytes[k]) < 0)
        {
            hex = false;
            break;
        }
        ++n;
    }
    // The end: a run of 64 zeros (white space between them aside).
    size_t end = bytes.size();
    size_t run = 0, runStart = at;
    for (size_t k = at; k < bytes.size(); ++k)
    {
        const uint8_t b = bytes[k];
        if (b == '0')
        {
            if (run++ == 0)
                runStart = k;
            if (run >= 64)
            {
                end = runStart;
                break;
            }
        }
        else if (!(b == '\r' || b == '\n') || run == 0)
            run = 0;
    }
    auto plain = std::make_shared<Source>();
    plain->program = true;
    uint32_t r = 55665;
    const auto decrypt = [&](uint8_t cipher) {
        plain->bytes.push_back(cipher ^ static_cast<uint8_t>(r >> 8));
        r = ((cipher + r) * 52845u + 22719u) & 0xFFFFu;
    };
    int high = -1;
    for (size_t k = at; k < end; ++k)
    {
        if (!hex)
        {
            decrypt(bytes[k]);
            continue;
        }
        const int v = hexDigit(bytes[k]);
        if (v < 0)
            continue;
        if (high < 0)
            high = v;
        else
        {
            decrypt(static_cast<uint8_t>(high * 16 + v));
            high = -1;
        }
    }
    // The first four bytes are random.
    plain->bytes.erase(plain->bytes.begin(), plain->bytes.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(4, plain->bytes.size())));
    src->at = end;
    I.runNested(plain);
}
void opBytesavailable(Interp& I)
{
    Obj file = I.pop();
    if (file.t == Obj::T::File && file.file)
        ready(I, *file.file);
    I.push(Obj::integer(file.file ? static_cast<double>(file.file->bytes.size() - std::min(file.file->at, file.file->bytes.size())) : 0));
}

// -- images

/// Bytes from an image's data source: a string (again and again), a
/// procedure (called until enough has come), or a file.
std::vector<uint8_t> imageData(Interp& I, const Obj& source, size_t wanted)
{
    std::vector<uint8_t> out;
    if (source.t == Obj::T::String)
    {
        if (source.str->empty())
            return out;
        while (out.size() < wanted)
            out.insert(out.end(), source.str->begin(), source.str->end());
    }
    else if (source.t == Obj::T::File && source.file)
    {
        Source& src = *source.file;
        ready(I, src);
        const size_t n = std::min(wanted, src.bytes.size() - std::min(src.at, src.bytes.size()));
        out.assign(src.bytes.begin() + static_cast<std::ptrdiff_t>(src.at),
            src.bytes.begin() + static_cast<std::ptrdiff_t>(src.at + n));
        src.at += n;
    }
    else if (source.isProc())
    {
        int empty = 0;
        while (out.size() < wanted && empty < 4)
        {
            const size_t before = I.ops.size();
            I.run(source);
            if (I.ops.size() <= before)
                break;
            Obj piece = I.pop();
            I.ops.resize(std::min(I.ops.size(), before));
            if (piece.t != Obj::T::String || piece.str->empty())
            {
                ++empty;
                continue;
            }
            out.insert(out.end(), piece.str->begin(), piece.str->end());
        }
    }
    out.resize(std::min(out.size(), wanted));
    return out;
}

/// Samples of \a bits each from \a data, \a count of them from the bit
/// \a at on, as values from 0 to 2^bits - 1.
uint32_t sampleAt(const std::vector<uint8_t>& data, size_t bit, int bits)
{
    if (bits == 8)
        return bit / 8 < data.size() ? data[bit / 8] : 0;
    if (bits == 16)
        return bit / 8 + 1 < data.size() ? (static_cast<uint32_t>(data[bit / 8]) << 8 | data[bit / 8 + 1]) : 0;
    uint32_t v = 0;
    for (int k = 0; k < bits; ++k, ++bit)
    {
        const size_t byte = bit / 8;
        const int on = byte < data.size() ? (data[byte] >> (7 - static_cast<int>(bit % 8))) & 1 : 0;
        v = (v << 1) | static_cast<uint32_t>(on);
    }
    return v;
}

void Interp::image(ImageSpec spec)
{
    const int width = spec.width, height = spec.height, bits = spec.bits;
    if (width <= 0 || height <= 0 || width > 20000 || height > 20000 ||
        static_cast<size_t>(width) * static_cast<size_t>(height) > kMaxImagePixels)
        throw PsError{"rangecheck"};
    if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 12 && bits != 16)
        throw PsError{"rangecheck"};
    // Gray, RGB or CMYK (or a space of as many); with a source for each when
    // there are several.
    const int components = spec.mask ? 1 : spec.components;
    if (components < 1 || components > 4 || (spec.multiple && spec.sources.size() < static_cast<size_t>(components)) ||
        spec.sources.empty())
        throw PsError{"rangecheck"};
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (const Obj& source : spec.sources)
        if (source.t == Obj::T::File && source.file)
            ready(*this, *source.file);
    // A JPEG (a DCTDecode filter): the file as it is, for the importer.
    if (!spec.multiple && !spec.mask && spec.sources[0].t == Obj::T::File && spec.sources[0].file &&
        spec.sources[0].file->dct)
    {
        Source& src = *spec.sources[0].file;
        Image image;
        image.encoded.assign(src.bytes.begin() + static_cast<std::ptrdiff_t>(std::min(src.at, src.bytes.size())),
            src.bytes.end());
        src.at = src.bytes.size();
        image.width = width;
        image.height = height;
        // CMYK read as ink unless Decode inverts it.
        image.inkCmyk = components == 4 && !(spec.decode.size() >= 2 && spec.decode[0] > spec.decode[1]);
        placeImage(std::move(image), spec.matrix);
        return;
    }
    const size_t samplesPerRow = static_cast<size_t>(width) * static_cast<size_t>(spec.multiple ? 1 : components);
    const size_t row = (samplesPerRow * static_cast<size_t>(bits) + 7) / 8;
    const size_t total = row * static_cast<size_t>(height);
    if (total > (size_t(1) << 29))
        throw PsError{"limitcheck"};
    std::vector<std::vector<uint8_t>> planes;
    const bool procedures = spec.multiple &&
        std::any_of(spec.sources.begin(), spec.sources.end(), [](const Obj& o) { return o.isProc(); });
    if (spec.data)
        planes.push_back(std::move(*spec.data));
    else if (procedures)
    {
        // Procedures that read the same file take turns, a piece each.
        planes.resize(static_cast<size_t>(components));
        for (int rounds = 0; rounds < (1 << 24); ++rounds)
        {
            bool short_ = false, grew = false;
            for (size_t c = 0; c < planes.size(); ++c)
            {
                if (planes[c].size() >= total)
                    continue;
                short_ = true;
                const size_t before = ops.size();
                run(spec.sources[c]);
                if (ops.size() > before && top().t == Obj::T::String && top().str && !top().str->empty())
                {
                    const std::string& piece = *top().str;
                    planes[c].insert(planes[c].end(), piece.begin(),
                        piece.begin() + static_cast<std::ptrdiff_t>(std::min(piece.size(), total - planes[c].size())));
                    grew = true;
                }
                ops.resize(std::min(ops.size(), before));
            }
            if (!short_ || !grew)
                break;
        }
    }
    else
        for (const Obj& source : spec.sources)
            planes.push_back(imageData(*this, source, total));
    // Each sample to its value: by the Decode array, else 0 to 1 (an index
    // keeps its number).
    const double top = static_cast<double>((uint64_t(1) << bits) - 1);
    std::vector<double> decode = spec.decode;
    const bool indexed = spec.space.t == Obj::T::Array && spec.space.arr && !spec.space.arr->empty() &&
        (*spec.space.arr)[0].str && *(*spec.space.arr)[0].str == "Indexed";
    for (int c = static_cast<int>(decode.size()) / 2; c < components; ++c)
    {
        decode.push_back(0.0);
        decode.push_back(indexed ? top : 1.0);
    }
    // Colors through a table when there are few samples and the space is
    // not a device's (a separation's tint transform, an index).
    const bool device = spec.space.t == Obj::T::Null ||
        (spec.space.t == Obj::T::Name && spec.space.str &&
            (*spec.space.str == "DeviceGray" || *spec.space.str == "DeviceRGB" || *spec.space.str == "DeviceCMYK"));
    std::vector<Color> table;
    if (!spec.mask && !device && components == 1 && bits <= 8)
        for (uint32_t s = 0; s <= static_cast<uint32_t>(top); ++s)
            table.push_back(colorIn(spec.space, {decode[0] + s * (decode[1] - decode[0]) / top}));
    else if (!spec.mask && !device && pixels > (size_t(1) << 20))
    {
        note(art, "An image in a color space too slow to convert came in gray.");
        spec.space = Obj();
    }
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(pixels * 4, 255);
    std::vector<double> v(static_cast<size_t>(components));
    for (size_t p = 0; p < pixels; ++p)
    {
        const size_t y = p / static_cast<size_t>(width);
        const size_t x = p % static_cast<size_t>(width);
        uint32_t first = 0;
        for (int c = 0; c < components; ++c)
        {
            const std::vector<uint8_t>& plane = planes[spec.multiple ? static_cast<size_t>(c) : 0];
            const size_t index = spec.multiple ? x : x * static_cast<size_t>(components) + static_cast<size_t>(c);
            const uint32_t s = sampleAt(plane, y * row * 8 + index * static_cast<size_t>(bits), bits);
            if (c == 0)
                first = s;
            v[static_cast<size_t>(c)] = decode[2 * static_cast<size_t>(c)] +
                s * (decode[2 * static_cast<size_t>(c) + 1] - decode[2 * static_cast<size_t>(c)]) / top;
        }
        uint8_t* out = image.rgba.data() + p * 4;
        if (spec.mask)
        {
            // A stencil: the current color where the sample paints.
            const bool paints = v[0] < 0.5;
            std::copy(gs.color.begin(), gs.color.begin() + 3, out);
            out[3] = paints ? 255 : 0;
            continue;
        }
        Color color;
        if (!table.empty())
            color = table[std::min<size_t>(first, table.size() - 1)];
        else if (device)
            color = components == 4 ? cmykColor(v[0], v[1], v[2], v[3])
                : components == 3   ? rgbColor(v[0], v[1], v[2])
                                    : grayColor(v[0]);
        else
            color = colorIn(spec.space, v);
        std::copy(color.begin(), color.begin() + 3, out);
        // Colors the mask color range leaves out (ImageType 4).
        if (!spec.maskColor.empty())
        {
            bool inside = true;
            for (int c = 0; c < components && inside; ++c)
            {
                const std::vector<uint8_t>& plane = planes[spec.multiple ? static_cast<size_t>(c) : 0];
                const size_t index = spec.multiple ? x : x * static_cast<size_t>(components) + static_cast<size_t>(c);
                const double s = sampleAt(plane, y * row * 8 + index * static_cast<size_t>(bits), bits);
                const size_t k = static_cast<size_t>(c);
                if (spec.maskColor.size() >= 2 * static_cast<size_t>(components))
                    inside = s >= spec.maskColor[2 * k] && s <= spec.maskColor[2 * k + 1];
                else
                    inside = k < spec.maskColor.size() && s == spec.maskColor[k];
            }
            if (inside)
                out[3] = 0;
        }
    }
    // An alpha from a mask of its own size (ImageType 3), stretched over
    // the image.
    if (!spec.alpha.empty() && spec.alphaWidth > 0 && spec.alphaHeight > 0)
        for (size_t p = 0; p < pixels; ++p)
        {
            const size_t y = p / static_cast<size_t>(width) * static_cast<size_t>(spec.alphaHeight) /
                static_cast<size_t>(height);
            const size_t x = p % static_cast<size_t>(width) * static_cast<size_t>(spec.alphaWidth) /
                static_cast<size_t>(width);
            const size_t at = y * static_cast<size_t>(spec.alphaWidth) + x;
            image.rgba[p * 4 + 3] = at < spec.alpha.size() ? std::min(image.rgba[p * 4 + 3], spec.alpha[at]) : 0;
        }
    placeImage(std::move(image), spec.matrix);
}

void Interp::placeImage(Image image, const Matrix& matrix)
{
    // Image space to user space is the inverse of the image's matrix; on
    // to the device, then to the stage.
    Matrix inverse;
    if (!matrix.invert(inverse))
        return;
    const Matrix device = gs.ctm.after(inverse);
    const Matrix toStage{1, 0, 0, -1, -pageLeft, pageTop};
    const Matrix placed = toStage.after(device);
    // The importer places the unit square (its rows top down): pixel space
    // scaled by the image's size.
    const double w = image.width, h = image.height;
    image.placement = Placement{placed.a * w, placed.b * w, placed.c * h, placed.d * h, placed.e, placed.f};
    art.order.emplace_back(Artwork::Kind::Image, art.images.size());
    art.images.push_back(std::move(image));
}

/// A mask's alpha, a byte a sample (255 where it paints), read from its
/// dictionary's own source or from \a data.
std::vector<uint8_t> maskAlpha(Interp& I, const Obj& mask, int width, int height, const std::vector<uint8_t>* data)
{
    std::vector<uint8_t> alpha;
    const Obj* bitsEntry = entry(mask, "BitsPerComponent");
    const int bits = bitsEntry ? Interp::toInt(bitsEntry->num) : 1;
    if (width <= 0 || height <= 0 || static_cast<size_t>(width) * static_cast<size_t>(height) > Interp::kMaxImagePixels ||
        (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16))
        return alpha;
    const size_t row = (static_cast<size_t>(width) * static_cast<size_t>(bits) + 7) / 8;
    std::vector<uint8_t> bytes;
    if (data)
        bytes = *data;
    else if (const Obj* source = entry(mask, "DataSource"))
        bytes = imageData(I, *source, row * static_cast<size_t>(height));
    // Decode [0 1] (the default) paints where samples are 0.
    const std::vector<double> decode = numbersIn(entry(mask, "Decode"));
    const bool inverted = decode.size() >= 2 && decode[0] > decode[1];
    const double top = static_cast<double>((uint64_t(1) << bits) - 1);
    alpha.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t y = 0; y < static_cast<size_t>(height); ++y)
        for (size_t x = 0; x < static_cast<size_t>(width); ++x)
        {
            const double s = sampleAt(bytes, y * row * 8 + x * static_cast<size_t>(bits), bits) / top;
            alpha[y * static_cast<size_t>(width) + x] = (inverted ? s >= 0.5 : s < 0.5) ? 255 : 0;
        }
    return alpha;
}

/// An image dictionary's own spec: its size, samples and matrix, in the
/// current color space.
ImageSpec imageSpec(Interp& I, const Obj& d)
{
    const Obj* w = entry(d, "Width");
    const Obj* h = entry(d, "Height");
    const Obj* b = entry(d, "BitsPerComponent");
    const Obj* m = entry(d, "ImageMatrix");
    const Obj* source = entry(d, "DataSource");
    if (!w || !h || !b || !m || !source)
        throw PsError{"typecheck"};
    ImageSpec spec;
    spec.width = Interp::toInt(w->num);
    spec.height = Interp::toInt(h->num);
    spec.bits = Interp::toInt(b->num);
    spec.matrix = matrixOf(*m);
    spec.components = std::max(1, I.gs.spaceComponents);
    spec.space = I.gs.spaceObj;
    spec.decode = numbersIn(entry(d, "Decode"));
    if (const Obj* many = entry(d, "MultipleDataSources"); many && many->num != 0 && source->t == Obj::T::Array)
    {
        spec.multiple = true;
        spec.sources = *source->arr;
    }
    else
        spec.sources = {*source};
    return spec;
}

void opImage(Interp& I)
{
    if (I.top().t == Obj::T::Dict)
    {
        Obj d = I.pop();
        const Obj* type = entry(d, "ImageType");
        const int imageType = type ? Interp::toInt(type->num) : 1;
        if (imageType == 3)
        {
            // An image with a mask: interleaved sample by sample, row by
            // row, or from sources of their own.
            const Obj* dataDict = entry(d, "DataDict");
            const Obj* maskDict = entry(d, "MaskDict");
            if (!dataDict || !maskDict)
                throw PsError{"typecheck"};
            const Obj* interleave = entry(d, "InterleaveType");
            const int kind = interleave ? Interp::toInt(interleave->num) : 3;
            const Obj* mw = entry(*maskDict, "Width");
            const Obj* mh = entry(*maskDict, "Height");
            ImageSpec spec = imageSpec(I, *dataDict);
            const int maskWidth = mw ? Interp::toInt(mw->num) : spec.width;
            const int maskHeight = mh ? Interp::toInt(mh->num) : spec.height;
            if (kind == 1 && !spec.multiple && spec.bits == 8 && spec.width > 0 && spec.height > 0 &&
                static_cast<size_t>(spec.width) * static_cast<size_t>(spec.height) <= Interp::kMaxImagePixels)
            {
                // Each pixel's mask sample first, then its color.
                const size_t pixels = static_cast<size_t>(spec.width) * static_cast<size_t>(spec.height);
                const size_t per = static_cast<size_t>(spec.components) + 1;
                const std::vector<uint8_t> all = imageData(I, spec.sources[0], pixels * per);
                std::vector<uint8_t> color(pixels * (per - 1));
                spec.alpha.resize(pixels);
                for (size_t p = 0; p < pixels; ++p)
                {
                    const size_t at = p * per;
                    spec.alpha[p] = at < all.size() ? all[at] : 0;
                    for (size_t c = 1; c < per; ++c)
                        color[p * (per - 1) + c - 1] = at + c < all.size() ? all[at + c] : 0;
                }
                // A sample of the mask paints where it is not 0, as an alpha.
                const std::vector<double> decode = numbersIn(entry(*maskDict, "Decode"));
                if (!(decode.size() >= 2 && decode[0] > decode[1]))
                    for (uint8_t& a : spec.alpha)
                        a = static_cast<uint8_t>(255 - a);
                spec.alphaWidth = spec.width;
                spec.alphaHeight = spec.height;
                spec.data = std::move(color);
                I.image(std::move(spec));
                return;
            }
            if (kind == 3)
            {
                spec.alpha = maskAlpha(I, *maskDict, maskWidth, maskHeight, nullptr);
                spec.alphaWidth = maskWidth;
                spec.alphaHeight = maskHeight;
                I.image(std::move(spec));
                return;
            }
            note(I.art, "Images with masks interleaved row by row came in without their masks.");
            I.image(std::move(spec));
            return;
        }
        ImageSpec spec = imageSpec(I, d);
        if (imageType == 4)
            spec.maskColor = numbersIn(entry(d, "MaskColor"));
        I.image(std::move(spec));
        return;
    }
    Obj source = I.pop();
    ImageSpec spec;
    spec.matrix = matrixOf(I.pop());
    spec.bits = I.integer();
    spec.height = I.integer();
    spec.width = I.integer();
    spec.sources = {source};
    spec.components = 1;
    I.image(std::move(spec));
}
void opColorimage(Interp& I)
{
    const int components = I.integer();
    if (components < 1 || components > 4)
        throw PsError{"rangecheck"};
    const bool multiple = I.boolean();
    ImageSpec spec;
    for (int i = 0; i < (multiple ? components : 1); ++i)
        spec.sources.insert(spec.sources.begin(), I.pop());
    spec.matrix = matrixOf(I.pop());
    spec.bits = I.integer();
    spec.height = I.integer();
    spec.width = I.integer();
    spec.components = components;
    spec.multiple = multiple;
    I.image(std::move(spec));
}
void opImagemask(Interp& I)
{
    ImageSpec spec;
    spec.mask = true;
    spec.bits = 1;
    if (I.top().t == Obj::T::Dict)
    {
        Obj d = I.pop();
        const Obj* w = entry(d, "Width");
        const Obj* h = entry(d, "Height");
        const Obj* m = entry(d, "ImageMatrix");
        const Obj* source = entry(d, "DataSource");
        if (!w || !h || !m || !source)
            throw PsError{"typecheck"};
        spec.width = Interp::toInt(w->num);
        spec.height = Interp::toInt(h->num);
        spec.matrix = matrixOf(*m);
        spec.sources = {*source};
        // Decode [1 0] paints the samples that are 1.
        spec.decode = numbersIn(entry(d, "Decode"));
        I.image(std::move(spec));
        return;
    }
    Obj source = I.pop();
    spec.matrix = matrixOf(I.pop());
    const bool polarity = I.boolean();
    spec.height = I.integer();
    spec.width = I.integer();
    spec.sources = {source};
    // With polarity true the samples that are 1 paint.
    if (polarity)
        spec.decode = {1.0, 0.0};
    I.image(std::move(spec));
}

void Interp::runNested(const std::shared_ptr<Source>& source)
{
    std::shared_ptr<Source> outerProgram = program;
    std::unique_ptr<Scanner> outerScanner = std::move(scanner);
    struct Restore
    {
        Interp& I;
        std::shared_ptr<Source>& program;
        std::unique_ptr<Scanner>& scanner;
        ~Restore()
        {
            I.program = program;
            I.scanner = std::move(scanner);
            --I.eexecDepth;
        }
    } restore{*this, outerProgram, outerScanner};
    program = source;
    scanner = std::make_unique<Scanner>(source);
    ++eexecDepth;
    try
    {
        Obj token;
        while (scanner->next(token, *this))
        {
            if (token.isProc())
                push(token);
            else
                execute(token);
        }
    }
    catch (const PsError&)
    {
        // A font that fails to load leaves the rest of the program to run.
        note(art, "A font the file carries could not be read; its text came in with standard widths.");
    }
}

void Interp::setup()
{
    systemdict = std::make_shared<Dict>();
    globaldict = std::make_shared<Dict>();
    userdict = std::make_shared<Dict>();
    dicts = {systemdict, globaldict, userdict};
    const std::pair<const char*, OpFn> table[] = {
        {"pop", opPop}, {"exch", opExch}, {"dup", opDup}, {"copy", opCopy}, {"index", opIndex}, {"roll", opRoll},
        {"clear", opClear}, {"count", opCount}, {"mark", opMark}, {"cleartomark", opClearToMark},
        {"counttomark", opCountToMark}, {"add", opAdd}, {"sub", opSub}, {"mul", opMul}, {"div", opDiv},
        {"idiv", opIdiv}, {"mod", opMod}, {"abs", opAbs}, {"neg", opNeg}, {"ceiling", opCeiling}, {"floor", opFloor},
        {"round", opRound}, {"truncate", opTruncate}, {"sqrt", opSqrt}, {"sin", opSin}, {"cos", opCos},
        {"exp", opExp}, {"ln", opLn}, {"log", opLog}, {"atan", opAtan}, {"cvi", opCvi}, {"cvr", opCvr},
        {"rand", opRand}, {"srand", opSrand}, {"rrand", opRrand}, {"eq", opEq}, {"ne", opNe}, {"gt", opGt},
        {"ge", opGe}, {"lt", opLt}, {"le", opLe}, {"and", opAnd}, {"or", opOr}, {"xor", opXor}, {"not", opNot},
        {"bitshift", opBitshift}, {"true", opTrue}, {"false", opFalse}, {"null", opNull}, {"type", opType},
        {"cvn", opCvn}, {"cvs", opCvs}, {"cvx", opCvx}, {"cvlit", opCvlit}, {"xcheck", opXcheck},
        {"readonly", opNoop}, {"executeonly", opNoop}, {"noaccess", opNoop}, {"rcheck", opTrueResult},
        {"wcheck", opTrueResult}, {"exec", opExec}, {"if", opIf}, {"ifelse", opIfelse}, {"for", opFor},
        {"repeat", opRepeat}, {"loop", opLoop}, {"exit", opExit}, {"stop", opStop}, {"stopped", opStopped},
        {"quit", opQuit}, {"forall", opForall}, {"dict", opDict}, {"begin", opBegin}, {"end", opEnd},
        {"def", opDef}, {"load", opLoad}, {"store", opStore}, {"known", opKnown}, {"where", opWhere},
        {"currentdict", opCurrentdict}, {"userdict", opUserdict}, {"systemdict", opSystemdict},
        {"globaldict", opGlobaldict}, {"statusdict", opUserdict}, {"countdictstack", opCountdictstack},
        {"undef", opUndef}, {"<<", opDictBegin}, {">>", opDictEnd}, {"[", opArrayBegin}, {"]", opArrayEnd},
        {"array", opArray}, {"packedarray", opArrayEnd}, {"string", opString}, {"length", opLength},
        {"maxlength", opMaxlength}, {"get", opGet}, {"put", opPut}, {"getinterval", opGetinterval},
        {"putinterval", opPutinterval}, {"aload", opAload}, {"astore", opAstore}, {"search", opSearch},
        {"anchorsearch", opAnchorsearch}, {"bind", opBind}, {"save", opSave}, {"restore", opRestore},
        {"version", opVersion}, {"languagelevel", opLanguagelevel}, {"product", opProduct},
        {"usertime", opUsertime}, {"realtime", opUsertime}, {"vmstatus", opVmstatus},
        {"currentglobal", opCurrentglobal}, {"setglobal", opPopOne}, {"setpacking", opPopOne},
        {"currentpacking", opCurrentglobal}, {"setpagedevice", opPopOne},
        {"currentpagedevice", opCurrentpagedevice}, {"showpage", opNoop}, {"copypage", opNoop},
        {"erasepage", opNoop}, {"print", opPopOne}, {"=", opPopOne}, {"==", opPopOne}, {"pstack", opNoop},
        {"stack", opNoop}, {"flush", opNoop}, {"gsave", opGsave}, {"grestore", opGrestore},
        {"grestoreall", opGrestoreall}, {"initgraphics", opInitgraphics}, {"setlinewidth", opSetlinewidth},
        {"currentlinewidth", opCurrentlinewidth}, {"setlinecap", opPopOne}, {"setlinejoin", opPopOne},
        {"setmiterlimit", opPopOne}, {"setdash", opSetdash}, {"setflat", opPopOne}, {"setstrokeadjust", opPopOne},
        {"setoverprint", opPopOne}, {"setsmoothness", opPopOne}, {"setcolorrendering", opPopOne},
        {"setrenderingintent", opPopOne}, {"settransfer", opPopOne}, {"setblackgeneration", opPopOne},
        {"setundercolorremoval", opPopOne}, {"sethalftone", opPopOne}, {"setcolortransfer", opNoop},
        {"setgray", opSetgray}, {"setrgbcolor", opSetrgbcolor}, {"setcmykcolor", opSetcmykcolor},
        {"sethsbcolor", opSethsbcolor}, {"currentgray", opCurrentgray}, {"currentrgbcolor", opCurrentrgbcolor},
        {"currentcmykcolor", opCurrentcmykcolor}, {"setcolorspace", opSetcolorspace}, {"setcolor", opSetcolor},
        {"currentcolorspace", opCurrentcolorspace}, {"setpattern", opSetpattern}, {"matrix", opMatrix},
        {"identmatrix", opIdentmatrix}, {"currentmatrix", opCurrentmatrix}, {"setmatrix", opSetmatrix},
        {"initmatrix", opInitmatrix}, {"defaultmatrix", opDefaultmatrix}, {"translate", opTranslate},
        {"scale", opScale}, {"rotate", opRotate}, {"concat", opConcat}, {"concatmatrix", opConcatmatrix},
        {"invertmatrix", opInvertmatrix}, {"transform", opTransform}, {"itransform", opItransform},
        {"dtransform", opDtransform}, {"idtransform", opIdtransform}, {"newpath", opNewpath},
        {"moveto", opMoveto}, {"rmoveto", opRmoveto}, {"lineto", opLineto}, {"rlineto", opRlineto},
        {"curveto", opCurveto}, {"rcurveto", opRcurveto}, {"arc", opArc}, {"arcn", opArcn}, {"arct", opArct},
        {"arcto", opArcto}, {"closepath", opClosepath}, {"currentpoint", opCurrentpoint}, {"pathbbox", opPathbbox},
        {"flattenpath", opNoop}, {"reversepath", opNoop}, {"strokepath", opNoop}, {"fill", opFill},
        {"eofill", opFill}, {"stroke", opStroke}, {"clip", opClip}, {"eoclip", opClip}, {"initclip", opInitclip},
        {"clippath", opClippath}, {"rectfill", opRectfill}, {"rectstroke", opRectstroke},
        {"rectclip", opRectclip}, {"shfill", opShfill}, {"makepattern", opMakepattern}, {"findfont", opFindfont}, {"definefont", opDefinefont},
        {"scalefont", opScalefont}, {"makefont", opMakefont}, {"setfont", opSetfont},
        {"currentfont", opCurrentfont}, {"selectfont", opSelectfont}, {"show", opShow}, {"ashow", opAshow},
        {"widthshow", opWidthshow}, {"awidthshow", opAwidthshow}, {"xshow", opXshow}, {"yshow", opYshow},
        {"xyshow", opXyshow}, {"kshow", opKshow}, {"stringwidth", opStringwidth}, {"charpath", opCharpath},
        {"glyphshow", opGlyphshow}, {"currentfile", opCurrentfile}, {"filter", opFilter},
        {"readhexstring", opReadhexstring}, {"readstring", opReadstring}, {"readline", opReadline},
        {"read", opRead}, {"flushfile", opFlushfile}, {"closefile", opClosefile},
        {"bytesavailable", opBytesavailable}, {"image", opImage}, {"colorimage", opColorimage},
        {"imagemask", opImagemask}, {"eexec", opEexec}, {"currenttransfer", opEmptyProc},
        {"currentblackgeneration", opEmptyProc}, {"currentundercolorremoval", opEmptyProc},
        {"currentcolortransfer", opFourProcs}, {"currentscreen", opCurrentscreen},
        {"currenthalftone", opCurrenthalftone}, {"setscreen", opSetscreen}, {"setcolorscreen", opClear},
        {"currentflat", opOne}, {"currentlinecap", opZero}, {"currentlinejoin", opZero},
        {"currentmiterlimit", opTen}, {"currentdash", opCurrentdash}, {"currentoverprint", opFalseResult},
        {"currentsmoothness", opOne}, {"currentstrokeadjust", opFalseResult},
        {"currentcolorrendering", opCurrenthalftone}, {"setcachedevice", opSetcachedevice},
        {"setcachedevice2", opSetcachedevice}, {"setcharwidth", opSetcharwidth}, {"cachestatus", opZero},
        {"setcachelimit", opPopOne}, {"setucacheparams", opClear}, {"setvmthreshold", opPopOne},
        {"vmreclaim", opPopOne}, {"setsystemparams", opPopOne}, {"setuserparams", opPopOne},
        {"echo", opPopOne}, {"prompt", opNoop}, {"letter", opNoop}, {"note", opNoop}, {"legal", opNoop},
        {"a4", opNoop},
    };
    for (const auto& [n, f] : table)
        define(n, f);
    // The encodings fonts are reencoded from: the standard names for the
    // printable ASCII, the rest .notdef.
    {
        Array standard(256, Obj::name(".notdef", false));
        static const char* const names[] = {"space", "exclam", "quotedbl", "numbersign", "dollar", "percent",
            "ampersand", "quoteright", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period",
            "slash", "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "colon",
            "semicolon", "less", "equal", "greater", "question", "at"};
        for (int i = 0; i < 33; ++i)
            standard[static_cast<size_t>(32 + i)] = Obj::name(names[i], false);
        for (int c = 'A'; c <= 'Z'; ++c)
            standard[static_cast<size_t>(c)] = Obj::name(std::string(1, static_cast<char>(c)), false);
        for (int c = 'a'; c <= 'z'; ++c)
            standard[static_cast<size_t>(c)] = Obj::name(std::string(1, static_cast<char>(c)), false);
        static const char* const more[] = {"bracketleft", "backslash", "bracketright", "asciicircum", "underscore",
            "quoteleft"};
        for (int i = 0; i < 6; ++i)
            standard[static_cast<size_t>(91 + i)] = Obj::name(more[i], false);
        static const char* const end[] = {"braceleft", "bar", "braceright", "asciitilde"};
        for (int i = 0; i < 4; ++i)
            standard[static_cast<size_t>(123 + i)] = Obj::name(end[i], false);
        (*systemdict)["StandardEncoding"] = Obj::array(standard);
        // ISO Latin-1: the same printable ASCII (but quoteright and
        // quoteleft), then the Latin-1 letters and signs.
        Array latin = standard;
        static const char* const high[] = {"space", "exclamdown", "cent", "sterling", "currency", "yen", "brokenbar",
            "section", "dieresis", "copyright", "ordfeminine", "guillemotleft", "logicalnot", "hyphen", "registered",
            "macron", "degree", "plusminus", "twosuperior", "threesuperior", "acute", "mu", "paragraph",
            "periodcentered", "cedilla", "onesuperior", "ordmasculine", "guillemotright", "onequarter", "onehalf",
            "threequarters", "questiondown", "Agrave", "Aacute", "Acircumflex", "Atilde", "Adieresis", "Aring", "AE",
            "Ccedilla", "Egrave", "Eacute", "Ecircumflex", "Edieresis", "Igrave", "Iacute", "Icircumflex", "Idieresis",
            "Eth", "Ntilde", "Ograve", "Oacute", "Ocircumflex", "Otilde", "Odieresis", "multiply", "Oslash", "Ugrave",
            "Uacute", "Ucircumflex", "Udieresis", "Yacute", "Thorn", "germandbls", "agrave", "aacute", "acircumflex",
            "atilde", "adieresis", "aring", "ae", "ccedilla", "egrave", "eacute", "ecircumflex", "edieresis", "igrave",
            "iacute", "icircumflex", "idieresis", "eth", "ntilde", "ograve", "oacute", "ocircumflex", "otilde",
            "odieresis", "divide", "oslash", "ugrave", "uacute", "ucircumflex", "udieresis", "yacute", "thorn",
            "ydieresis"};
        for (size_t i = 0; i < 96; ++i)
            latin[0xA0 + i] = Obj::name(high[i], false);
        latin[0x60] = Obj::name("quoteleft", false);
        (*systemdict)["ISOLatin1Encoding"] = Obj::array(latin);
    }
    // Fonts and the error machinery programs look for.
    (*systemdict)["FontDirectory"] = Obj::dictionary();
    (*systemdict)["SharedFontDirectory"] = Obj::dictionary();
    (*systemdict)["errordict"] = Obj::dictionary();
    (*systemdict)["$error"] = Obj::dictionary();
}

} // namespace

Artwork readPostScript(const std::string& text, const double box[4])
{
    Artwork art;
    art.width = box[2] - box[0];
    art.height = box[3] - box[1];
    Interp interp(art);
    interp.pageLeft = box[0];
    interp.pageTop = box[3];
    interp.setup();
    interp.program = std::make_shared<Source>();
    interp.program->bytes.assign(text.begin(), text.end());
    interp.program->program = true;
    interp.scanner = std::make_unique<Scanner>(interp.program);
    std::string stoppedAt;
    try
    {
        Obj token;
        while (interp.scanner->next(token, interp))
        {
            if (token.isProc())
                interp.push(token);
            else
                interp.execute(token);
        }
    }
    catch (const PsError& error)
    {
        stoppedAt = error.name;
    }
    catch (const ExitLoop&)
    {
    }
    catch (const StopRun&)
    {
        stoppedAt = "stop";
    }
    catch (const QuitRun&)
    {
    }
    if (!stoppedAt.empty())
        art.notes.push_back(art.paths.empty() && art.images.empty()
                ? "The PostScript could not be run (" + stoppedAt + ")."
                : "The PostScript stopped part way (" + stoppedAt + "): what it drew until then came in.");
    if (interp.sawShading)
        art.notes.push_back("Smooth shadings other than axial and radial ones (meshes) came in as plain color or were "
                            "left out.");
    return art;
}

} // namespace vectors

} // namespace fla
