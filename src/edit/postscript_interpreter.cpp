// A small PostScript interpreter for EPS artwork that is not Illustrator's:
// FreeHand, Photoshop, CorelDRAW and the drawing programs that write plain
// PostScript. It runs the program and keeps what it paints: paths filled
// and stroked in gray, RGB, CMYK and separation colors (through their tint
// transforms), text as text, and 8-bit images. Clipping, patterns and
// shadings are left out and said so.
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
    double lineWidth = 1.0;
    std::vector<Subpath> path;
    Subpath current;
    bool hasPoint = false;
    double px = 0, py = 0; // current point, device space
    Font font;
    /// The clipping paths in force (stage coordinates).
    std::vector<std::vector<Subpath>> clips;
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
    bool sawClip = false, sawPattern = false, sawShading = false, sawMask = false;
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
    void setColor(std::vector<double> values)
    {
        if (gs.space == "Separation" || gs.space == "DeviceN")
        {
            if (gs.tintTransform.isProc())
            {
                const size_t before = ops.size();
                for (double v : values)
                    push(Obj::real(v));
                run(gs.tintTransform);
                std::vector<double> out;
                while (ops.size() > before && top().isNumber())
                    out.insert(out.begin(), pop().num);
                ops.resize(std::min(ops.size(), before));
                setDeviceColor(out, gs.alternate);
                return;
            }
            // No transform: the tint as a shade of gray.
            gs.color = grayColor(1.0 - (values.empty() ? 1.0 : values[0]));
            return;
        }
        if (gs.space == "Pattern")
        {
            sawPattern = true;
            return;
        }
        setDeviceColor(values, gs.space);
    }

    // -- text
    double fontSize() const
    {
        const Matrix m = gs.font.matrix;
        return std::sqrt(std::fabs(m.a * m.d - m.b * m.c)) * 1000.0 / 1000.0;
    }

    /// A glyph's width, a thousand to the em: the standard metrics of the
    /// font the name is nearest (Courier, Times or Helvetica, bold or
    /// italic), for the printable ASCII; an average for the rest.
    double glyphWidth(unsigned char c) const
    {
        static const short helvetica[95] = {278, 278, 355, 556, 556, 889, 667, 222, 333, 333, 389, 584, 278, 333, 278,
            278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722,
            722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
            667, 611, 278, 278, 278, 469, 556, 222, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833,
            556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
        static const short helveticaBold[95] = {278, 333, 474, 556, 556, 889, 722, 278, 333, 333, 389, 584, 278, 333,
            278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722,
            722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944,
            667, 667, 611, 333, 278, 333, 584, 556, 278, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278,
            889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};
        static const short times[95] = {250, 333, 408, 500, 500, 833, 778, 333, 333, 333, 500, 564, 250, 333, 250, 278,
            500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444, 921, 722, 667, 667, 722,
            611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722, 556, 722, 667, 556, 611, 722, 722, 944, 722, 722,
            611, 333, 278, 333, 469, 500, 333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500,
            500, 500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541};
        static const short timesBold[95] = {250, 333, 555, 500, 500, 1000, 833, 333, 333, 333, 500, 570, 250, 333, 250,
            278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570, 570, 500, 930, 722, 667, 722,
            722, 667, 611, 778, 778, 389, 500, 778, 667, 944, 722, 778, 611, 778, 722, 556, 667, 722, 722, 1000, 722,
            722, 667, 333, 278, 333, 581, 500, 333, 500, 556, 444, 556, 444, 333, 500, 556, 278, 333, 556, 278, 833,
            556, 500, 556, 556, 444, 389, 333, 556, 500, 722, 500, 500, 444, 394, 220, 394, 520};
        static const short timesItalic[95] = {250, 333, 420, 500, 500, 833, 778, 333, 333, 333, 500, 675, 250, 333, 250,
            278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 675, 675, 675, 500, 920, 611, 611, 667,
            722, 611, 611, 722, 722, 333, 444, 667, 556, 833, 667, 722, 611, 722, 611, 500, 556, 722, 611, 833, 611,
            556, 556, 389, 278, 389, 422, 500, 333, 500, 500, 444, 500, 444, 278, 500, 500, 278, 278, 444, 278, 722,
            500, 500, 500, 500, 389, 389, 278, 500, 444, 667, 444, 444, 389, 400, 275, 400, 541};
        const std::string& name = gs.font.name;
        const auto has = [&name](const char* part) { return name.find(part) != std::string::npos; };
        if (has("Courier") || has("Mono"))
            return 600.0;
        const bool bold = has("Bold") || has("Black") || has("Heavy");
        const bool serif = has("Times") || has("Roman") || has("Serif") || has("Georgia") || has("Garamond") ||
            has("Palatino") || has("Bookman") || has("Century");
        const short* table = serif ? (bold ? timesBold : has("Italic") ? timesItalic : times)
                                   : (bold ? helveticaBold : helvetica);
        if (c >= 32 && c <= 126)
            return table[c - 32];
        return serif ? 500.0 : 556.0;
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

    /// Shows \a text, then moves on by its width and by \a extraX, \a
    /// extraY more in user space (the spacing ashow and the rest add).
    void show(const std::string& text, double extraX = 0.0, double extraY = 0.0)
    {
        if (!gs.hasPoint)
            throw PsError{"nocurrentpoint"};
        // The font's own matrix (glyph space, a thousand units to the em, to
        // user space) and the CTM.
        const Matrix toDevice = gs.ctm.after(gs.font.matrix);
        const double size = 1000.0 * std::sqrt(std::fabs(toDevice.a * toDevice.d - toDevice.b * toDevice.c));
        Text item;
        item.text = text;
        std::string family = gs.font.name;
        const auto has = [&family](const char* part) { return family.find(part) != std::string::npos; };
        item.bold = has("Bold") || has("Black") || has("Heavy");
        item.italic = has("Italic") || has("Oblique");
        const size_t dash = family.find('-');
        if (dash != std::string::npos)
            family = family.substr(0, dash);
        item.family = family;
        item.size = std::max(1.0, size);
        item.color = gs.color;
        // Text space (y down, a unit a pixel) to the stage: the direction
        // of the font's x axis, flipped to the stage's y.
        const double length = std::hypot(toDevice.a, toDevice.b);
        const double ux = length > 0 ? toDevice.a / length : 1.0;
        const double uy = length > 0 ? toDevice.b / length : 0.0;
        const Point at = stage(gs.px, gs.py);
        item.placement = Placement{ux, -uy, uy, ux, at.x, at.y};
        art.order.emplace_back(Artwork::Kind::Text, art.texts.size());
        art.texts.push_back(item);
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
    void image(int width, int height, int bits, const Matrix& matrix, std::vector<Obj> sources, int components,
        bool multiple);
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

std::vector<uint8_t> inflateBytes(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> out;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return out;
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
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
    inflateEnd(&stream);
    return out;
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
        const std::vector<uint8_t> data = rest();
        for (size_t i = 0; i < data.size();)
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
        }
    }
    else if (name == "FlateDecode")
        out->bytes = inflateBytes(rest());
    else
    {
        // DCT and the rest: the bytes as they are, for whoever reads them.
        out->bytes = rest();
    }
    return out;
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
void opSetgray(Interp& I)
{
    I.gs.space = "DeviceGray";
    I.gs.spaceComponents = 1;
    I.gs.color = Interp::grayColor(I.num());
}
void opSetrgbcolor(Interp& I)
{
    const double b = I.num(), g = I.num(), r = I.num();
    I.gs.space = "DeviceRGB";
    I.gs.spaceComponents = 3;
    I.gs.color = Interp::rgbColor(r, g, b);
}
void opSetcmykcolor(Interp& I)
{
    const double k = I.num(), y = I.num(), m = I.num(), c = I.num();
    I.gs.space = "DeviceCMYK";
    I.gs.spaceComponents = 4;
    I.gs.color = Interp::cmykColor(c, m, y, k);
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
    I.gs.space = "DeviceRGB";
    I.gs.color = Interp::rgbColor(r, g, b);
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
    std::string name;
    GState& gs = I.gs;
    gs.tintTransform = Obj();
    if (space.t == Obj::T::Name)
        name = *space.str;
    else if (space.t == Obj::T::Array && !space.arr->empty())
    {
        const Array& a = *space.arr;
        name = a[0].str ? *a[0].str : std::string();
        if ((name == "Separation" || name == "DeviceN") && a.size() >= 4)
        {
            gs.alternate = a[2].t == Obj::T::Name ? *a[2].str
                : a[2].t == Obj::T::Array && !a[2].arr->empty() && (*a[2].arr)[0].str ? *(*a[2].arr)[0].str
                                                                                       : "DeviceGray";
            gs.tintTransform = a[3];
            gs.spaceComponents = name == "DeviceN" && a[1].t == Obj::T::Array ? static_cast<int>(a[1].arr->size()) : 1;
        }
        else if (name == "ICCBased" || name == "CIEBasedABC" || name == "CalRGB")
            name = "DeviceRGB";
        else if (name == "CalGray" || name == "CIEBasedA")
            name = "DeviceGray";
        else if (name == "Indexed")
            name = "DeviceRGB";
    }
    gs.space = name;
    if (name == "DeviceRGB")
        gs.spaceComponents = 3;
    else if (name == "DeviceCMYK")
        gs.spaceComponents = 4;
    else if (name == "DeviceGray")
        gs.spaceComponents = 1;
    // The initial color of the space.
    if (name == "DeviceCMYK" || name == "DeviceRGB" || name == "DeviceGray")
        gs.color = Color{0, 0, 0, 255};
    else if (name == "Separation" || name == "DeviceN")
        I.setColor(std::vector<double>(static_cast<size_t>(std::max(1, gs.spaceComponents)), 1.0));
}
void opSetcolor(Interp& I)
{
    if (!I.ops.empty() && I.top().t == Obj::T::Dict)
    {
        I.pop();
        I.sawPattern = true;
        return;
    }
    std::vector<double> values;
    const int n = std::max(1, I.gs.spaceComponents);
    for (int i = 0; i < n && !I.ops.empty() && I.top().isNumber(); ++i)
        values.insert(values.begin(), I.num());
    I.setColor(values);
}
void opCurrentcolorspace(Interp& I) { I.push(Obj::array({Obj::name(I.gs.space, false)})); }
void opSetpattern(Interp& I)
{
    I.pop();
    I.sawPattern = true;
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
void opShfill(Interp& I)
{
    I.pop();
    I.sawShading = true;
}

// -- fonts and text

void opFindfont(Interp& I)
{
    Obj name = I.pop();
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
    I.pop();
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
    f.file = makeFilter(I, sourceOf(I, source), filter, end.t == Obj::T::String ? &end : nullptr);
    I.push(f);
}
void opReadhexstring(Interp& I)
{
    Obj buffer = textOperand(I.pop());
    Obj file = I.pop();
    Source& src = fileOperand(file);
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
    // After the operator's own white space, the data.
    if (src.program && src.at < src.bytes.size() && Scanner::white(src.bytes[src.at]))
        ++src.at;
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
        file.file->at = file.file->bytes.size();
}
void opClosefile(Interp& I) { I.pop(); }
void opBytesavailable(Interp& I)
{
    Obj file = I.pop();
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
    else if (source.t == Obj::T::File)
    {
        Source& src = *source.file;
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

void Interp::image(int width, int height, int bits, const Matrix& matrix, std::vector<Obj> sources, int components,
    bool multiple)
{
    if (width <= 0 || height <= 0 || width > 20000 || height > 20000 ||
        static_cast<size_t>(width) * static_cast<size_t>(height) > kMaxImagePixels)
        throw PsError{"rangecheck"};
    if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 12 && bits != 16)
        throw PsError{"rangecheck"};
    // Gray, RGB or CMYK; with a source for each when there are several.
    if (components < 1 || components > 4 || (multiple && sources.size() < static_cast<size_t>(components)) ||
        sources.empty())
        throw PsError{"rangecheck"};
    const size_t row = (static_cast<size_t>(width) * static_cast<size_t>(bits) *
                           static_cast<size_t>(multiple ? 1 : components) + 7) / 8;
    const size_t total = row * static_cast<size_t>(height);
    if (total > (size_t(1) << 29))
        throw PsError{"limitcheck"};
    std::vector<std::vector<uint8_t>> planes;
    for (const Obj& source : sources)
        planes.push_back(imageData(*this, source, total));
    if (bits != 8)
    {
        note(art, "Images of other than 8 bits a sample were left out.");
        return;
    }
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 255);
    for (size_t p = 0; p < static_cast<size_t>(width) * static_cast<size_t>(height); ++p)
    {
        double v[4] = {0, 0, 0, 0};
        for (int c = 0; c < components; ++c)
        {
            const std::vector<uint8_t>& plane = planes[multiple ? static_cast<size_t>(c) : 0];
            const size_t at = multiple ? p : p * static_cast<size_t>(components) + static_cast<size_t>(c);
            v[c] = at < plane.size() ? plane[at] / 255.0 : 0.0;
        }
        const Color color = components == 4 ? cmykColor(v[0], v[1], v[2], v[3])
            : components == 3              ? rgbColor(v[0], v[1], v[2])
                                           : grayColor(v[0]);
        std::copy(color.begin(), color.begin() + 3, image.rgba.begin() + static_cast<std::ptrdiff_t>(p * 4));
    }
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
    image.placement = Placement{placed.a * width, placed.b * width, placed.c * height, placed.d * height, placed.e,
        placed.f};
    art.order.emplace_back(Artwork::Kind::Image, art.images.size());
    art.images.push_back(std::move(image));
}

void opImage(Interp& I)
{
    if (I.top().t == Obj::T::Dict)
    {
        Obj d = I.pop();
        const auto get = [&d](const char* k) -> const Obj* {
            const auto f = d.dict->find(k);
            return f == d.dict->end() ? nullptr : &f->second;
        };
        const Obj* w = get("Width");
        const Obj* h = get("Height");
        const Obj* b = get("BitsPerComponent");
        const Obj* m = get("ImageMatrix");
        const Obj* source = get("DataSource");
        if (!w || !h || !b || !m || !source)
            throw PsError{"typecheck"};
        std::vector<Obj> sources;
        bool multiple = false;
        if (const Obj* many = get("MultipleDataSources"); many && many->num != 0 && source->t == Obj::T::Array)
        {
            multiple = true;
            sources = *source->arr;
        }
        else
            sources = {*source};
        I.image(Interp::toInt(w->num), Interp::toInt(h->num), Interp::toInt(b->num), matrixOf(*m), sources,
            std::max(1, I.gs.spaceComponents), multiple);
        return;
    }
    Obj source = I.pop();
    const Matrix m = matrixOf(I.pop());
    const int bits = I.integer();
    const int height = I.integer();
    const int width = I.integer();
    I.image(width, height, bits, m, {source}, 1, false);
}
void opColorimage(Interp& I)
{
    const int components = I.integer();
    if (components < 1 || components > 4)
        throw PsError{"rangecheck"};
    const bool multiple = I.boolean();
    std::vector<Obj> sources;
    for (int i = 0; i < (multiple ? components : 1); ++i)
        sources.insert(sources.begin(), I.pop());
    const Matrix m = matrixOf(I.pop());
    const int bits = I.integer();
    const int height = I.integer();
    const int width = I.integer();
    I.image(width, height, bits, m, sources, components, multiple);
}
/// The bytes of a mask \a width by \a height, which is read past and left
/// out; refused when no image could be that large.
size_t maskBytes(int width, int height)
{
    if (width <= 0 || height <= 0)
        return 0;
    if (width > 20000 || height > 20000 ||
        static_cast<size_t>(width) * static_cast<size_t>(height) > Interp::kMaxImagePixels)
        throw PsError{"rangecheck"};
    return (static_cast<size_t>(width) + 7) / 8 * static_cast<size_t>(height);
}
void opImagemask(Interp& I)
{
    I.sawMask = true;
    if (I.top().t == Obj::T::Dict)
    {
        Obj d = I.pop();
        const auto f = d.dict->find("DataSource");
        const auto w = d.dict->find("Width");
        const auto h = d.dict->find("Height");
        if (f != d.dict->end() && w != d.dict->end() && h != d.dict->end())
            imageData(I, f->second, maskBytes(Interp::toInt(w->second.num), Interp::toInt(h->second.num)));
        return;
    }
    Obj source = I.pop();
    I.pop();
    I.pop();
    const int height = I.integer();
    const int width = I.integer();
    imageData(I, source, maskBytes(width, height));
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
        {"rectclip", opRectclip}, {"shfill", opShfill}, {"findfont", opFindfont}, {"definefont", opDefinefont},
        {"scalefont", opScalefont}, {"makefont", opMakefont}, {"setfont", opSetfont},
        {"currentfont", opCurrentfont}, {"selectfont", opSelectfont}, {"show", opShow}, {"ashow", opAshow},
        {"widthshow", opWidthshow}, {"awidthshow", opAwidthshow}, {"xshow", opXshow}, {"yshow", opYshow},
        {"xyshow", opXyshow}, {"kshow", opKshow}, {"stringwidth", opStringwidth}, {"charpath", opCharpath},
        {"glyphshow", opGlyphshow}, {"currentfile", opCurrentfile}, {"filter", opFilter},
        {"readhexstring", opReadhexstring}, {"readstring", opReadstring}, {"readline", opReadline},
        {"read", opRead}, {"flushfile", opFlushfile}, {"closefile", opClosefile},
        {"bytesavailable", opBytesavailable}, {"image", opImage}, {"colorimage", opColorimage},
        {"imagemask", opImagemask}, {"currenttransfer", opEmptyProc},
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
        (*systemdict)["ISOLatin1Encoding"] = Obj::array(standard);
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
    if (interp.sawPattern)
        art.notes.push_back("Pattern fills came in as plain color.");
    if (interp.sawShading)
        art.notes.push_back("Smooth shadings were left out.");
    if (interp.sawMask)
        art.notes.push_back("Image masks were left out.");
    return art;
}

} // namespace vectors

} // namespace fla
