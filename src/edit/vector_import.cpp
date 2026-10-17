#include "vector_import.h"

#include "../data/edge.h"
#include "../data/linear_gradient.h"
#include "../data/radial_gradient.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"

#include "pdf_objects.h"
#include "postscript_import.h"

#include "tinyxml2.h"
#include "zlib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>

namespace fla {

namespace vectors {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// An affine map: x' = a x + c y + e, y' = b x + d y + f.
struct Affine
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;

    Point map(const Point& p) const { return Point(a * p.x + c * p.y + e, b * p.x + d * p.y + f); }
    /// This one after \a first.
    Affine after(const Affine& first) const
    {
        Affine r;
        r.a = a * first.a + c * first.b;
        r.b = b * first.a + d * first.b;
        r.c = a * first.c + c * first.d;
        r.d = b * first.c + d * first.d;
        r.e = a * first.e + c * first.f + e;
        r.f = b * first.e + d * first.f + f;
        return r;
    }
    double scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
};

Affine toAffine(const Placement& p)
{
    return Affine{p.a, p.b, p.c, p.d, p.tx, p.ty};
}

Placement toPlacement(const Affine& m)
{
    return Placement{m.a, m.b, m.c, m.d, m.e, m.f};
}

void transform(Painted& painted, const Affine& m)
{
    for (Subpath& subpath : painted.subpaths)
        for (Segment& segment : subpath.segments)
            for (Point& point : segment.points)
                point = m.map(point);
    painted.strokeWidth *= m.scale();
    if (painted.gradient)
        painted.gradient->placement = toPlacement(m.after(toAffine(painted.gradient->placement)));
}

/// The FLA gradient square is 1638.4 pixels across.
constexpr double kGradientSquare = 1638.4;

/// The placement of a linear gradient's square running from \a from to \a
/// to: its x along the line, its y across it.
Placement linearPlacement(const Point& from, const Point& to)
{
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    return Placement{dx / kGradientSquare, dy / kGradientSquare, -dy / kGradientSquare, dx / kGradientSquare,
        (from.x + to.x) / 2.0, (from.y + to.y) / 2.0};
}

/// A radial gradient's square on a circle of \a radius around \a center,
/// its x turned toward \a toward when that is not the center.
Placement radialPlacement(const Point& center, double radius, double angle)
{
    const double k = radius / (kGradientSquare / 2.0);
    const double cs = std::cos(angle) * k;
    const double sn = std::sin(angle) * k;
    return Placement{cs, sn, -sn, cs, center.x, center.y};
}

/// Remembers what was added, in drawing order.
void addPath(Artwork& artwork, Painted painted)
{
    artwork.order.emplace_back(Artwork::Kind::Path, artwork.paths.size());
    artwork.paths.push_back(std::move(painted));
}

// ---------------------------------------------------------------- numbers

/// Reads numbers from SVG and PDF text: "1.5.5" is two, "-1-2" is two,
/// exponents allowed.
struct Scanner
{
    const char* at;
    const char* end;

    void skip()
    {
        while (at < end && (std::isspace(static_cast<unsigned char>(*at)) || *at == ','))
            ++at;
    }
    bool number(double& value)
    {
        skip();
        if (at >= end)
            return false;
        const char* start = at;
        const char* p = at;
        if (*p == '+' || *p == '-')
            ++p;
        bool digits = false;
        while (p < end && std::isdigit(static_cast<unsigned char>(*p)))
        {
            ++p;
            digits = true;
        }
        if (p < end && *p == '.')
        {
            ++p;
            while (p < end && std::isdigit(static_cast<unsigned char>(*p)))
            {
                ++p;
                digits = true;
            }
        }
        if (!digits)
            return false;
        if (p < end && (*p == 'e' || *p == 'E'))
        {
            const char* q = p + 1;
            if (q < end && (*q == '+' || *q == '-'))
                ++q;
            if (q < end && std::isdigit(static_cast<unsigned char>(*q)))
            {
                p = q;
                while (p < end && std::isdigit(static_cast<unsigned char>(*p)))
                    ++p;
            }
        }
        value = std::strtod(std::string(start, p).c_str(), nullptr);
        at = p;
        return true;
    }
    /// An arc's flags may be written run together: "011".
    bool flag(bool& value)
    {
        skip();
        if (at < end && (*at == '0' || *at == '1'))
        {
            value = *at == '1';
            ++at;
            return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------- colors

std::optional<Color> parseColor(std::string text, std::optional<Color> current)
{
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }),
        text.end());
    std::transform(text.begin(), text.end(), text.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
    if (text.empty() || text == "none" || text == "transparent")
        return std::nullopt;
    if (text == "currentcolor")
        return current ? current : std::optional<Color>(Color{0, 0, 0, 255});
    if (text[0] == '#')
    {
        const std::string hex = text.substr(1);
        const auto digit = [&hex](size_t i) { return static_cast<uint8_t>(std::strtoul(hex.substr(i, 1).c_str(), nullptr, 16)); };
        if (hex.size() == 3)
            return Color{static_cast<uint8_t>(digit(0) * 17), static_cast<uint8_t>(digit(1) * 17), static_cast<uint8_t>(digit(2) * 17), 255};
        if (hex.size() >= 6)
        {
            const unsigned long value = std::strtoul(hex.substr(0, 6).c_str(), nullptr, 16);
            return Color{static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value), 255};
        }
        return std::nullopt;
    }
    if (text.rfind("rgb(", 0) == 0 || text.rfind("rgba(", 0) == 0)
    {
        const size_t open = text.find('(');
        Color color{0, 0, 0, 255};
        std::string inside = text.substr(open + 1, text.find(')') - open - 1);
        size_t i = 0;
        size_t start = 0;
        while (i < 4 && start <= inside.size())
        {
            size_t comma = inside.find(',', start);
            if (comma == std::string::npos)
                comma = inside.size();
            const std::string part = inside.substr(start, comma - start);
            if (part.empty())
                break;
            double value = std::atof(part.c_str());
            if (i == 3)
                value = (part.back() == '%' ? value / 100.0 : value) * 255.0;
            else if (part.back() == '%')
                value = value * 2.55;
            color[i] = static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
            ++i;
            start = comma + 1;
        }
        return color;
    }
    static const std::map<std::string, Color> named = {
        {"black", {0, 0, 0, 255}}, {"white", {255, 255, 255, 255}}, {"red", {255, 0, 0, 255}},
        {"green", {0, 128, 0, 255}}, {"lime", {0, 255, 0, 255}}, {"blue", {0, 0, 255, 255}},
        {"yellow", {255, 255, 0, 255}}, {"cyan", {0, 255, 255, 255}}, {"aqua", {0, 255, 255, 255}},
        {"magenta", {255, 0, 255, 255}}, {"fuchsia", {255, 0, 255, 255}}, {"gray", {128, 128, 128, 255}},
        {"grey", {128, 128, 128, 255}}, {"silver", {192, 192, 192, 255}}, {"maroon", {128, 0, 0, 255}},
        {"olive", {128, 128, 0, 255}}, {"navy", {0, 0, 128, 255}}, {"teal", {0, 128, 128, 255}},
        {"purple", {128, 0, 128, 255}}, {"orange", {255, 165, 0, 255}}, {"pink", {255, 192, 203, 255}},
        {"brown", {165, 42, 42, 255}}, {"gold", {255, 215, 0, 255}}, {"darkgray", {169, 169, 169, 255}},
        {"lightgray", {211, 211, 211, 255}}, {"tomato", {255, 99, 71, 255}}, {"coral", {255, 127, 80, 255}},
        {"salmon", {250, 128, 114, 255}}, {"crimson", {220, 20, 60, 255}}, {"indigo", {75, 0, 130, 255}},
        {"violet", {238, 130, 238, 255}}, {"khaki", {240, 230, 140, 255}}, {"beige", {245, 245, 220, 255}},
        {"ivory", {255, 255, 240, 255}}, {"tan", {210, 180, 140, 255}}, {"chocolate", {210, 105, 30, 255}},
        {"skyblue", {135, 206, 235, 255}}, {"steelblue", {70, 130, 180, 255}}, {"royalblue", {65, 105, 225, 255}},
        {"darkblue", {0, 0, 139, 255}}, {"darkgreen", {0, 100, 0, 255}}, {"darkred", {139, 0, 0, 255}},
        {"forestgreen", {34, 139, 34, 255}}, {"seagreen", {46, 139, 87, 255}}, {"turquoise", {64, 224, 208, 255}},
        {"orchid", {218, 112, 214, 255}}, {"plum", {221, 160, 221, 255}}, {"slategray", {112, 128, 144, 255}},
        {"dimgray", {105, 105, 105, 255}}, {"whitesmoke", {245, 245, 245, 255}}, {"lavender", {230, 230, 250, 255}}};
    const auto found = named.find(text);
    if (found != named.end())
        return found->second;
    return std::nullopt;
}

// ---------------------------------------------------------------- arcs

void addArcCubics(std::vector<Segment>& out, const Point& from, double rx, double ry, double rotation, bool large,
    bool sweep, const Point& to)
{
    if (std::hypot(to.x - from.x, to.y - from.y) < 1.0e-9)
        return;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx < 1.0e-9 || ry < 1.0e-9)
    {
        out.push_back({Segment::Kind::Line, {to}});
        return;
    }
    // The SVG arc implementation notes: from endpoints to a center.
    const double phi = rotation * kPi / 180.0;
    const double cosPhi = std::cos(phi);
    const double sinPhi = std::sin(phi);
    const double dx = (from.x - to.x) / 2.0;
    const double dy = (from.y - to.y) / 2.0;
    const double x1 = cosPhi * dx + sinPhi * dy;
    const double y1 = -sinPhi * dx + cosPhi * dy;
    const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1.0)
    {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double factor = denominator > 0.0 ? std::sqrt(std::max(0.0, numerator / denominator)) : 0.0;
    if (large == sweep)
        factor = -factor;
    const double cx1 = factor * rx * y1 / ry;
    const double cy1 = -factor * ry * x1 / rx;
    const double cx = cosPhi * cx1 - sinPhi * cy1 + (from.x + to.x) / 2.0;
    const double cy = sinPhi * cx1 + cosPhi * cy1 + (from.y + to.y) / 2.0;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const double theta = angle(1.0, 0.0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    double delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sweep && delta > 0.0)
        delta -= 2.0 * kPi;
    else if (sweep && delta < 0.0)
        delta += 2.0 * kPi;

    // Pieces of at most a quarter turn, each a cubic.
    const int pieces = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / (kPi / 2.0) - 1.0e-9)));
    const double step = delta / pieces;
    const double k = 4.0 / 3.0 * std::tan(step / 4.0);
    const auto point = [&](double t) {
        return Point(cx + rx * std::cos(t) * cosPhi - ry * std::sin(t) * sinPhi,
            cy + rx * std::cos(t) * sinPhi + ry * std::sin(t) * cosPhi);
    };
    const auto tangent = [&](double t) {
        return Point(-rx * std::sin(t) * cosPhi - ry * std::cos(t) * sinPhi,
            -rx * std::sin(t) * sinPhi + ry * std::cos(t) * cosPhi);
    };
    for (int i = 0; i < pieces; ++i)
    {
        const double t0 = theta + step * i;
        const double t1 = t0 + step;
        const Point p0 = point(t0);
        const Point p1 = i + 1 == pieces ? to : point(t1);
        const Point d0 = tangent(t0);
        const Point d1 = tangent(t1);
        out.push_back({Segment::Kind::Cubic,
            {Point(p0.x + k * d0.x, p0.y + k * d0.y), Point(p1.x - k * d1.x, p1.y - k * d1.y), p1}});
    }
}

// ---------------------------------------------------------------- SVG

/// An SVG path's data as subpaths.
std::vector<Subpath> parsePathData(const std::string& data)
{
    std::vector<Subpath> subpaths;
    Scanner scan{data.data(), data.data() + data.size()};
    Point current(0.0, 0.0);
    Point start(0.0, 0.0);
    Point lastControl(0.0, 0.0);
    char lastCommand = 0;
    char command = 0;
    const auto startSubpath = [&](const Point& at) {
        if (!subpaths.empty() && subpaths.back().segments.size() <= 1)
            subpaths.pop_back();
        subpaths.push_back(Subpath());
        subpaths.back().segments.push_back({Segment::Kind::Move, {at}});
        start = at;
    };
    const auto ensure = [&]() {
        if (subpaths.empty() || subpaths.back().closed)
            startSubpath(current);
    };
    while (true)
    {
        scan.skip();
        if (scan.at >= scan.end)
            break;
        if (std::isalpha(static_cast<unsigned char>(*scan.at)))
            command = *scan.at++;
        else if (!command)
            break;
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char upper = static_cast<char>(std::toupper(command));
        const auto offset = [&](Point p) {
            if (relative)
            {
                p.x += current.x;
                p.y += current.y;
            }
            return p;
        };
        double x = 0, y = 0;
        bool ok = true;
        switch (upper)
        {
        case 'M':
            if (!scan.number(x) || !scan.number(y))
            {
                ok = false;
                break;
            }
            current = offset(Point(x, y));
            startSubpath(current);
            // Pairs after the first are lines.
            command = relative ? 'l' : 'L';
            break;
        case 'L':
            if (!scan.number(x) || !scan.number(y))
            {
                ok = false;
                break;
            }
            ensure();
            current = offset(Point(x, y));
            subpaths.back().segments.push_back({Segment::Kind::Line, {current}});
            break;
        case 'H':
            if (!scan.number(x))
            {
                ok = false;
                break;
            }
            ensure();
            current = Point(relative ? current.x + x : x, current.y);
            subpaths.back().segments.push_back({Segment::Kind::Line, {current}});
            break;
        case 'V':
            if (!scan.number(y))
            {
                ok = false;
                break;
            }
            ensure();
            current = Point(current.x, relative ? current.y + y : y);
            subpaths.back().segments.push_back({Segment::Kind::Line, {current}});
            break;
        case 'C':
        case 'S':
        case 'Q':
        case 'T':
        {
            Point c1, c2, end;
            double v[4];
            if (upper == 'C')
            {
                double w[6];
                for (double& value : w)
                    ok = ok && scan.number(value);
                if (!ok)
                    break;
                c1 = offset(Point(w[0], w[1]));
                c2 = offset(Point(w[2], w[3]));
                end = offset(Point(w[4], w[5]));
            }
            else if (upper == 'S')
            {
                for (double& value : v)
                    ok = ok && scan.number(value);
                if (!ok)
                    break;
                const bool smooth = lastCommand == 'C' || lastCommand == 'S';
                c1 = smooth ? Point(2 * current.x - lastControl.x, 2 * current.y - lastControl.y) : current;
                c2 = offset(Point(v[0], v[1]));
                end = offset(Point(v[2], v[3]));
            }
            else
            {
                Point q;
                if (upper == 'Q')
                {
                    for (int i = 0; i < 4; ++i)
                        ok = ok && scan.number(v[i]);
                    if (!ok)
                        break;
                    q = offset(Point(v[0], v[1]));
                    end = offset(Point(v[2], v[3]));
                }
                else
                {
                    ok = scan.number(v[0]) && scan.number(v[1]);
                    if (!ok)
                        break;
                    const bool smooth = lastCommand == 'Q' || lastCommand == 'T';
                    q = smooth ? Point(2 * current.x - lastControl.x, 2 * current.y - lastControl.y) : current;
                    end = offset(Point(v[0], v[1]));
                }
                // A quadratic raised to a cubic.
                c1 = Point(current.x + 2.0 / 3.0 * (q.x - current.x), current.y + 2.0 / 3.0 * (q.y - current.y));
                c2 = Point(end.x + 2.0 / 3.0 * (q.x - end.x), end.y + 2.0 / 3.0 * (q.y - end.y));
                lastControl = q;
            }
            ensure();
            subpaths.back().segments.push_back({Segment::Kind::Cubic, {c1, c2, end}});
            if (upper == 'C' || upper == 'S')
                lastControl = c2;
            current = end;
            break;
        }
        case 'A':
        {
            double rx = 0, ry = 0, rotation = 0;
            bool large = false, sweep = false;
            ok = scan.number(rx) && scan.number(ry) && scan.number(rotation) && scan.flag(large) && scan.flag(sweep) &&
                scan.number(x) && scan.number(y);
            if (!ok)
                break;
            ensure();
            const Point end = offset(Point(x, y));
            addArcCubics(subpaths.back().segments, current, rx, ry, rotation, large, sweep, end);
            current = end;
            break;
        }
        case 'Z':
            if (!subpaths.empty())
            {
                subpaths.back().closed = true;
                current = start;
            }
            break;
        default:
            ok = false;
            break;
        }
        if (!ok)
            break;
        lastCommand = upper;
    }
    if (!subpaths.empty() && subpaths.back().segments.size() <= 1)
        subpaths.pop_back();
    return subpaths;
}

Affine parseTransform(const std::string& text)
{
    Affine result;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t open = text.find('(', at);
        if (open == std::string::npos)
            break;
        const size_t close = text.find(')', open);
        if (close == std::string::npos)
            break;
        std::string name = text.substr(at, open - at);
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) || c == ','; }),
            name.end());
        const std::string args = text.substr(open + 1, close - open - 1);
        Scanner scan{args.data(), args.data() + args.size()};
        std::vector<double> v;
        double value = 0;
        while (scan.number(value))
            v.push_back(value);
        Affine m;
        if (name == "matrix" && v.size() >= 6)
            m = Affine{v[0], v[1], v[2], v[3], v[4], v[5]};
        else if (name == "translate" && !v.empty())
            m.e = v[0], m.f = v.size() > 1 ? v[1] : 0.0;
        else if (name == "scale" && !v.empty())
            m.a = v[0], m.d = v.size() > 1 ? v[1] : v[0];
        else if (name == "rotate" && !v.empty())
        {
            const double t = v[0] * kPi / 180.0;
            Affine r{std::cos(t), std::sin(t), -std::sin(t), std::cos(t), 0.0, 0.0};
            if (v.size() >= 3)
            {
                Affine to{1, 0, 0, 1, v[1], v[2]};
                Affine back{1, 0, 0, 1, -v[1], -v[2]};
                r = to.after(r.after(back));
            }
            m = r;
        }
        else if (name == "skewX" && !v.empty())
            m.c = std::tan(v[0] * kPi / 180.0);
        else if (name == "skewY" && !v.empty())
            m.b = std::tan(v[0] * kPi / 180.0);
        result = result.after(m);
        at = close + 1;
    }
    return result;
}

/// A length with its unit, in pixels.
double length(const char* text, double fallback)
{
    if (!text)
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text)
        return fallback;
    const std::string unit = end ? std::string(end) : std::string();
    if (unit.rfind("pt", 0) == 0)
        return value * 96.0 / 72.0;
    if (unit.rfind("in", 0) == 0)
        return value * 96.0;
    if (unit.rfind("cm", 0) == 0)
        return value * 96.0 / 2.54;
    if (unit.rfind("mm", 0) == 0)
        return value * 96.0 / 25.4;
    if (unit.rfind("%", 0) == 0)
        return fallback;
    return value;
}

struct SvgStyle
{
    std::optional<Color> fill = Color{0, 0, 0, 255};
    std::optional<Color> stroke;
    double strokeWidth = 1.0;
    double opacity = 1.0;
    double fillOpacity = 1.0;
    double strokeOpacity = 1.0;
    bool evenOdd = false;
    std::optional<Color> color;
    bool hidden = false;
    /// A fill or stroke given as url(#id): the gradient's id.
    std::string fillUrl;
    std::string strokeUrl;
    std::string family = "Arial";
    double fontSize = 16.0;
    bool bold = false;
    bool italic = false;
};

/// What the document says once and elements refer to: elements by id, and
/// the style sheet's rules by class and by element name.
struct SvgDefs
{
    std::map<std::string, const tinyxml2::XMLElement*> ids;
    std::map<std::string, std::string> classRules;
    std::map<std::string, std::string> typeRules;
};

/// The document being read.
const SvgDefs* gDefs = nullptr;
/// Elements <use> can repeat many times over: how many have been walked,
/// against a budget, and the ones being walked through a <use>.
size_t gSvgWalked = 0;
constexpr size_t kMaxSvgElements = 1000000;
std::vector<const tinyxml2::XMLElement*> gSvgUsing;

std::string trimmed(std::string s)
{
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

/// "key: value; ..." as pairs.
void declarationsOf(const std::string& text, std::vector<std::pair<std::string, std::string>>& out)
{
    size_t at = 0;
    while (at < text.size())
    {
        size_t semi = text.find(';', at);
        if (semi == std::string::npos)
            semi = text.size();
        const std::string declaration = text.substr(at, semi - at);
        const size_t colon = declaration.find(':');
        if (colon != std::string::npos)
            out.emplace_back(trimmed(declaration.substr(0, colon)), trimmed(declaration.substr(colon + 1)));
        at = semi + 1;
    }
}

/// The id a "url(#id)" names, or empty.
std::string urlId(const std::string& value)
{
    const size_t url = value.find("url(");
    if (url == std::string::npos)
        return std::string();
    const size_t hash = value.find('#', url);
    if (hash == std::string::npos)
        return std::string();
    const size_t close = value.find(')', hash);
    std::string id = value.substr(hash + 1, close == std::string::npos ? std::string::npos : close - hash - 1);
    id.erase(std::remove(id.begin(), id.end(), '"'), id.end());
    id.erase(std::remove(id.begin(), id.end(), '\''), id.end());
    return trimmed(id);
}

std::string localName(const tinyxml2::XMLElement& element)
{
    std::string name = element.Name();
    const size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

void applyStyle(const tinyxml2::XMLElement& element, SvgStyle& style)
{
    // Attributes, then the style sheet's rules, then the style attribute's
    // declarations over them.
    std::vector<std::pair<std::string, std::string>> declarations;
    for (const char* name : {"color", "fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity",
             "fill-rule", "display", "visibility", "font-family", "font-size", "font-weight", "font-style"})
        if (const char* value = element.Attribute(name))
            declarations.emplace_back(name, value);
    if (gDefs)
    {
        const auto type = gDefs->typeRules.find(localName(element));
        if (type != gDefs->typeRules.end())
            declarationsOf(type->second, declarations);
        if (const char* classes = element.Attribute("class"))
        {
            const std::string list = classes;
            size_t at = 0;
            while (at < list.size())
            {
                size_t space = list.find_first_of(" \t", at);
                if (space == std::string::npos)
                    space = list.size();
                const auto rule = gDefs->classRules.find(list.substr(at, space - at));
                if (rule != gDefs->classRules.end())
                    declarationsOf(rule->second, declarations);
                at = space + 1;
            }
        }
    }
    if (const char* css = element.Attribute("style"))
        declarationsOf(css, declarations);
    for (const auto& [key, value] : declarations)
    {
        if (key == "color")
            style.color = parseColor(value, style.color);
        else if (key == "fill")
        {
            style.fillUrl = urlId(value);
            style.fill = style.fillUrl.empty() ? parseColor(value, style.color) : std::optional<Color>(Color{0, 0, 0, 255});
        }
        else if (key == "stroke")
        {
            style.strokeUrl = urlId(value);
            style.stroke = style.strokeUrl.empty() ? parseColor(value, style.color) : std::optional<Color>(Color{0, 0, 0, 255});
        }
        else if (key == "font-family")
        {
            std::string family = value.substr(0, value.find(','));
            family.erase(std::remove(family.begin(), family.end(), '"'), family.end());
            family.erase(std::remove(family.begin(), family.end(), '\''), family.end());
            family = trimmed(family);
            if (family == "sans-serif")
                family = "Arial";
            else if (family == "serif")
                family = "Times New Roman";
            else if (family == "monospace")
                family = "Courier New";
            if (!family.empty())
                style.family = family;
        }
        else if (key == "font-size")
            style.fontSize = length(value.c_str(), style.fontSize);
        else if (key == "font-weight")
            style.bold = value == "bold" || value == "bolder" || std::atoi(value.c_str()) >= 600;
        else if (key == "font-style")
            style.italic = value == "italic" || value == "oblique";
        else if (key == "stroke-width")
            style.strokeWidth = length(value.c_str(), style.strokeWidth);
        else if (key == "opacity")
            style.opacity *= std::atof(value.c_str());
        else if (key == "fill-opacity")
            style.fillOpacity = std::atof(value.c_str());
        else if (key == "stroke-opacity")
            style.strokeOpacity = std::atof(value.c_str());
        else if (key == "fill-rule")
            style.evenOdd = value == "evenodd";
        else if ((key == "display" && value == "none") || (key == "visibility" && value == "hidden"))
            style.hidden = true;
    }
}

std::vector<Point> pointsOf(const char* text)
{
    std::vector<Point> points;
    if (!text)
        return points;
    Scanner scan{text, text + std::strlen(text)};
    double x = 0, y = 0;
    while (scan.number(x) && scan.number(y))
        points.emplace_back(x, y);
    return points;
}

/// An ellipse as four cubics.
Subpath ellipse(double cx, double cy, double rx, double ry)
{
    constexpr double k = 0.5522847498;
    Subpath subpath;
    subpath.closed = true;
    subpath.segments.push_back({Segment::Kind::Move, {Point(cx + rx, cy)}});
    subpath.segments.push_back({Segment::Kind::Cubic, {Point(cx + rx, cy + k * ry), Point(cx + k * rx, cy + ry), Point(cx, cy + ry)}});
    subpath.segments.push_back({Segment::Kind::Cubic, {Point(cx - k * rx, cy + ry), Point(cx - rx, cy + k * ry), Point(cx - rx, cy)}});
    subpath.segments.push_back({Segment::Kind::Cubic, {Point(cx - rx, cy - k * ry), Point(cx - k * rx, cy - ry), Point(cx, cy - ry)}});
    subpath.segments.push_back({Segment::Kind::Cubic, {Point(cx + k * rx, cy - ry), Point(cx + rx, cy - k * ry), Point(cx + rx, cy)}});
    return subpath;
}

/// An attribute of a gradient, or of the ones it takes after through href.
const char* gradientAttribute(const tinyxml2::XMLElement* element, const char* name)
{
    for (int depth = 0; element && depth < 8; ++depth)
    {
        if (const char* value = element->Attribute(name))
            return value;
        const char* href = element->Attribute("xlink:href");
        if (!href)
            href = element->Attribute("href");
        if (!href || href[0] != '#' || !gDefs)
            break;
        const auto found = gDefs->ids.find(href + 1);
        element = found != gDefs->ids.end() ? found->second : nullptr;
    }
    return nullptr;
}

/// A coordinate of a gradient: a number, or a percentage of \a whole.
double gradientNumber(const char* text, double fallback, bool fraction)
{
    if (!text)
        return fallback;
    const double value = std::atof(text);
    if (std::strchr(text, '%'))
        return fraction ? value / 100.0 : value / 100.0;
    return value;
}

/// The gradient \a id names, for a path whose bounds are \a box in its
/// own space; its colors faded by \a alpha.
std::optional<Gradient> svgGradient(const std::string& id, double left, double top, double width, double height,
    double alpha)
{
    if (!gDefs)
        return std::nullopt;
    const auto found = gDefs->ids.find(id);
    if (found == gDefs->ids.end())
        return std::nullopt;
    const tinyxml2::XMLElement* element = found->second;
    const std::string kind = localName(*element);
    if (kind != "linearGradient" && kind != "radialGradient")
        return std::nullopt;
    Gradient gradient;
    gradient.radial = kind == "radialGradient";
    // The stops: its own, or those of the gradient it takes after.
    const tinyxml2::XMLElement* stopsFrom = element;
    for (int depth = 0; stopsFrom && depth < 8; ++depth)
    {
        if (stopsFrom->FirstChildElement())
            break;
        const char* href = stopsFrom->Attribute("xlink:href");
        if (!href)
            href = stopsFrom->Attribute("href");
        const auto next = href && href[0] == '#' ? gDefs->ids.find(href + 1) : gDefs->ids.end();
        stopsFrom = next != gDefs->ids.end() ? next->second : nullptr;
    }
    double last = 0.0;
    for (const tinyxml2::XMLElement* stop = stopsFrom ? stopsFrom->FirstChildElement() : nullptr; stop;
         stop = stop->NextSiblingElement())
    {
        if (localName(*stop) != "stop")
            continue;
        std::vector<std::pair<std::string, std::string>> declarations;
        if (const char* color = stop->Attribute("stop-color"))
            declarations.emplace_back("stop-color", color);
        if (const char* opacity = stop->Attribute("stop-opacity"))
            declarations.emplace_back("stop-opacity", opacity);
        if (const char* css = stop->Attribute("style"))
            declarationsOf(css, declarations);
        Gradient::Stop entry;
        entry.offset = std::clamp(gradientNumber(stop->Attribute("offset"), 0.0, true), 0.0, 1.0);
        entry.offset = std::max(entry.offset, last);
        last = entry.offset;
        double opacity = 1.0;
        for (const auto& [key, value] : declarations)
        {
            if (key == "stop-color")
                if (const std::optional<Color> color = parseColor(value, std::nullopt))
                    entry.color = *color;
            if (key == "stop-opacity")
                opacity = std::atof(value.c_str());
        }
        entry.color[3] = static_cast<uint8_t>(std::clamp(std::lround(255.0 * opacity * alpha), 0L, 255L));
        gradient.stops.push_back(entry);
    }
    if (gradient.stops.empty())
        return std::nullopt;
    if (const char* spread = gradientAttribute(element, "spreadMethod"))
        gradient.spread = std::string(spread) == "reflect" ? Gradient::Spread::Reflect
            : std::string(spread) == "repeat"              ? Gradient::Spread::Repeat
                                                           : Gradient::Spread::Pad;
    // Its space: the path's bounds unless it says user space, then its own
    // transform.
    const char* units = gradientAttribute(element, "gradientUnits");
    const bool userSpace = units && std::string(units) == "userSpaceOnUse";
    Affine space;
    if (!userSpace)
        space = Affine{width, 0.0, 0.0, height, left, top};
    if (const char* transformText = gradientAttribute(element, "gradientTransform"))
        space = space.after(parseTransform(transformText));
    Placement local;
    if (gradient.radial)
    {
        const double cx = gradientNumber(gradientAttribute(element, "cx"), 0.5, true);
        const double cy = gradientNumber(gradientAttribute(element, "cy"), 0.5, true);
        const double r = gradientNumber(gradientAttribute(element, "r"), 0.5, true);
        const double fx = gradientNumber(gradientAttribute(element, "fx"), cx, true);
        const double fy = gradientNumber(gradientAttribute(element, "fy"), cy, true);
        const double toward = std::hypot(fx - cx, fy - cy);
        local = radialPlacement(Point(cx, cy), r, toward > 1.0e-9 ? std::atan2(fy - cy, fx - cx) : 0.0);
        gradient.focal = r > 0.0 ? std::clamp(toward / r, 0.0, 0.99) : 0.0;
    }
    else
    {
        const Point from(gradientNumber(gradientAttribute(element, "x1"), 0.0, true),
            gradientNumber(gradientAttribute(element, "y1"), 0.0, true));
        const Point to(gradientNumber(gradientAttribute(element, "x2"), 1.0, true),
            gradientNumber(gradientAttribute(element, "y2"), 0.0, true));
        local = linearPlacement(from, to);
    }
    gradient.placement = toPlacement(space.after(toAffine(local)));
    return gradient;
}

/// The text an element holds, its children's too.
std::string textOf(const tinyxml2::XMLNode& node)
{
    std::string text;
    for (const tinyxml2::XMLNode* child = node.FirstChild(); child; child = child->NextSibling())
    {
        if (const tinyxml2::XMLText* plain = child->ToText())
            text += plain->Value();
        else
            text += textOf(*child);
    }
    return text;
}

void walkSvg(const tinyxml2::XMLElement& element, SvgStyle style, const Affine& outer, Artwork& artwork,
    int depth = 0);

void addText(const tinyxml2::XMLElement& element, const SvgStyle& style, const Affine& m, double x, double y,
    const std::string& words, Artwork& artwork)
{
    std::string text = words;
    // Runs of white space are one space, as SVG shows them.
    std::string squeezed;
    bool space = false;
    for (char c : text)
    {
        const bool white = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (white && space)
            continue;
        squeezed += white ? ' ' : c;
        space = white;
    }
    squeezed = trimmed(squeezed);
    if (squeezed.empty() || !style.fill)
        return;
    Text placed;
    placed.text = squeezed;
    placed.family = style.family;
    placed.size = style.fontSize;
    placed.bold = style.bold;
    placed.italic = style.italic;
    placed.color = *style.fill;
    placed.color[3] = static_cast<uint8_t>(std::clamp(std::lround(placed.color[3] * style.opacity * style.fillOpacity), 0L, 255L));
    placed.placement = toPlacement(m.after(Affine{1.0, 0.0, 0.0, 1.0, x, y}));
    (void)element;
    artwork.order.emplace_back(Artwork::Kind::Text, artwork.texts.size());
    artwork.texts.push_back(std::move(placed));
}

void walkSvg(const tinyxml2::XMLElement& element, SvgStyle style, const Affine& outer, Artwork& artwork, int depth)
{
    if (depth > 32 || ++gSvgWalked > kMaxSvgElements)
        return;
    const std::string name = localName(element);
    if (name == "defs" || name == "clipPath" || name == "mask" || name == "symbol" || name == "metadata" ||
        name == "title" || name == "desc" || name == "style" || name == "pattern" || name == "linearGradient" ||
        name == "radialGradient" || name == "marker" || name == "filter")
        return;
    applyStyle(element, style);
    if (style.hidden)
        return;
    Affine m = outer;
    if (const char* text = element.Attribute("transform"))
        m = outer.after(parseTransform(text));

    std::vector<Subpath> subpaths;
    const auto number = [&element](const char* attribute, double fallback = 0.0) {
        return length(element.Attribute(attribute), fallback);
    };
    if (name == "path")
        subpaths = parsePathData(element.Attribute("d") ? element.Attribute("d") : "");
    else if (name == "rect")
    {
        const double x = number("x"), y = number("y"), w = number("width"), h = number("height");
        double rx = number("rx", -1.0), ry = number("ry", -1.0);
        if (rx < 0.0)
            rx = ry < 0.0 ? 0.0 : ry;
        if (ry < 0.0)
            ry = rx;
        rx = std::min(rx, w / 2.0);
        ry = std::min(ry, h / 2.0);
        if (w > 0.0 && h > 0.0)
        {
            Subpath rect;
            rect.closed = true;
            if (rx <= 0.0 || ry <= 0.0)
            {
                rect.segments = {{Segment::Kind::Move, {Point(x, y)}}, {Segment::Kind::Line, {Point(x + w, y)}},
                    {Segment::Kind::Line, {Point(x + w, y + h)}}, {Segment::Kind::Line, {Point(x, y + h)}},
                    {Segment::Kind::Line, {Point(x, y)}}};
            }
            else
            {
                constexpr double k = 0.5522847498;
                rect.segments = {{Segment::Kind::Move, {Point(x + rx, y)}}, {Segment::Kind::Line, {Point(x + w - rx, y)}},
                    {Segment::Kind::Cubic, {Point(x + w - rx + k * rx, y), Point(x + w, y + ry - k * ry), Point(x + w, y + ry)}},
                    {Segment::Kind::Line, {Point(x + w, y + h - ry)}},
                    {Segment::Kind::Cubic, {Point(x + w, y + h - ry + k * ry), Point(x + w - rx + k * rx, y + h), Point(x + w - rx, y + h)}},
                    {Segment::Kind::Line, {Point(x + rx, y + h)}},
                    {Segment::Kind::Cubic, {Point(x + rx - k * rx, y + h), Point(x, y + h - ry + k * ry), Point(x, y + h - ry)}},
                    {Segment::Kind::Line, {Point(x, y + ry)}},
                    {Segment::Kind::Cubic, {Point(x, y + ry - k * ry), Point(x + rx - k * rx, y), Point(x + rx, y)}}};
            }
            subpaths.push_back(rect);
        }
    }
    else if (name == "circle")
    {
        const double r = number("r");
        if (r > 0.0)
            subpaths.push_back(ellipse(number("cx"), number("cy"), r, r));
    }
    else if (name == "ellipse")
    {
        const double rx = number("rx"), ry = number("ry");
        if (rx > 0.0 && ry > 0.0)
            subpaths.push_back(ellipse(number("cx"), number("cy"), rx, ry));
    }
    else if (name == "line")
    {
        Subpath line;
        line.segments = {{Segment::Kind::Move, {Point(number("x1"), number("y1"))}},
            {Segment::Kind::Line, {Point(number("x2"), number("y2"))}}};
        subpaths.push_back(line);
        // A line has no inside to fill.
        style.fill.reset();
    }
    else if (name == "polyline" || name == "polygon")
    {
        const std::vector<Point> points = pointsOf(element.Attribute("points"));
        if (points.size() >= 2)
        {
            Subpath poly;
            poly.segments.push_back({Segment::Kind::Move, {points.front()}});
            for (size_t i = 1; i < points.size(); ++i)
                poly.segments.push_back({Segment::Kind::Line, {points[i]}});
            poly.closed = name == "polygon";
            subpaths.push_back(poly);
        }
    }
    else if (name == "text")
    {
        // Each tspan with a place of its own is a line of its own; the rest
        // runs on from the text's start.
        const double x = number("x"), y = number("y");
        std::string loose;
        for (const tinyxml2::XMLNode* child = element.FirstChild(); child; child = child->NextSibling())
        {
            const tinyxml2::XMLElement* span = child->ToElement();
            if (span && localName(*span) == "tspan" && (span->Attribute("x") || span->Attribute("y")))
            {
                SvgStyle spanStyle = style;
                applyStyle(*span, spanStyle);
                addText(*span, spanStyle, m, length(span->Attribute("x"), x), length(span->Attribute("y"), y),
                    textOf(*span), artwork);
            }
            else if (const tinyxml2::XMLText* plain = child->ToText())
                loose += plain->Value();
            else if (span)
                loose += textOf(*span);
        }
        addText(element, style, m, x, y, loose, artwork);
        return;
    }
    else if (name == "image")
    {
        const char* href = element.Attribute("xlink:href");
        if (!href)
            href = element.Attribute("href");
        const double w = number("width"), h = number("height");
        if (!href || w <= 0.0 || h <= 0.0)
            return;
        Image image;
        const std::string link = href;
        if (link.rfind("data:", 0) == 0)
        {
            const size_t comma = link.find(',');
            if (comma != std::string::npos && link.find(";base64") < comma)
                image.encoded = fromBase64(link.substr(comma + 1));
            if (image.encoded.empty())
                return;
        }
        else
            image.href = link;
        // The unit square onto the image's box.
        image.placement = toPlacement(m.after(Affine{w, 0.0, 0.0, h, number("x"), number("y")}));
        artwork.order.emplace_back(Artwork::Kind::Image, artwork.images.size());
        artwork.images.push_back(std::move(image));
        return;
    }
    else if (name == "use")
    {
        const char* href = element.Attribute("xlink:href");
        if (!href)
            href = element.Attribute("href");
        if (!href || href[0] != '#' || !gDefs)
            return;
        const auto found = gDefs->ids.find(href + 1);
        if (found == gDefs->ids.end())
            return;
        // A use inside what it uses would repeat itself.
        if (std::find(gSvgUsing.begin(), gSvgUsing.end(), found->second) != gSvgUsing.end())
            return;
        for (const tinyxml2::XMLNode* up = element.Parent(); up; up = up->Parent())
            if (up == found->second)
                return;
        gSvgUsing.push_back(found->second);
        struct Leave
        {
            ~Leave() { gSvgUsing.pop_back(); }
        } leave;
        const Affine placed = m.after(Affine{1.0, 0.0, 0.0, 1.0, number("x"), number("y")});
        if (localName(*found->second) == "symbol")
        {
            SvgStyle inner = style;
            applyStyle(*found->second, inner);
            for (const tinyxml2::XMLElement* child = found->second->FirstChildElement(); child;
                 child = child->NextSiblingElement())
                walkSvg(*child, inner, placed, artwork, depth + 1);
        }
        else
            walkSvg(*found->second, style, placed, artwork, depth + 1);
        return;
    }

    if (!subpaths.empty())
    {
        Painted painted;
        painted.subpaths = std::move(subpaths);
        const auto withAlpha = [](std::optional<Color> color, double alpha) {
            if (color)
                (*color)[3] = static_cast<uint8_t>(std::clamp(std::lround((*color)[3] * alpha), 0L, 255L));
            return color;
        };
        painted.fill = withAlpha(style.fill, style.opacity * style.fillOpacity);
        painted.stroke = withAlpha(style.stroke, style.opacity * style.strokeOpacity);
        painted.strokeWidth = style.strokeWidth;
        painted.evenOdd = style.evenOdd;
        if (!style.fillUrl.empty() || !style.strokeUrl.empty())
        {
            // A gradient's bounding box is the path's, in its own space.
            double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
            for (const Subpath& subpath : painted.subpaths)
                for (const Segment& segment : subpath.segments)
                    for (const Point& point : segment.points)
                    {
                        left = std::min(left, point.x);
                        top = std::min(top, point.y);
                        right = std::max(right, point.x);
                        bottom = std::max(bottom, point.y);
                    }
            if (!style.fillUrl.empty())
            {
                painted.gradient = svgGradient(style.fillUrl, left, top, right - left, bottom - top,
                    style.opacity * style.fillOpacity);
                // The plain color, where a gradient cannot go, is its first.
                if (painted.gradient)
                    painted.fill = painted.gradient->stops.front().color;
                else
                    painted.fill.reset();
            }
            if (!style.strokeUrl.empty())
            {
                // Strokes take a plain color: the gradient's first.
                const std::optional<Gradient> strokeGradient = svgGradient(style.strokeUrl, left, top,
                    right - left, bottom - top, style.opacity * style.strokeOpacity);
                if (strokeGradient)
                    painted.stroke = strokeGradient->stops.front().color;
                else
                    painted.stroke.reset();
            }
        }
        transform(painted, m);
        if (painted.fill || painted.stroke)
            addPath(artwork, std::move(painted));
    }

    for (const tinyxml2::XMLElement* child = element.FirstChildElement(); child; child = child->NextSiblingElement())
        walkSvg(*child, style, m, artwork, depth + 1);
}

// ---------------------------------------------------------------- shapes

/// A subpath flattened to points, for nesting and orientation.
std::vector<Point> flatten(const Subpath& subpath)
{
    std::vector<Point> points;
    Point at;
    for (const Segment& segment : subpath.segments)
    {
        if (segment.points.empty())
            continue;
        if (segment.kind == Segment::Kind::Cubic && segment.points.size() == 3)
        {
            for (int i = 1; i <= 8; ++i)
            {
                const double t = i / 8.0, u = 1.0 - t;
                points.emplace_back(u * u * u * at.x + 3 * u * u * t * segment.points[0].x + 3 * u * t * t * segment.points[1].x + t * t * t * segment.points[2].x,
                    u * u * u * at.y + 3 * u * u * t * segment.points[0].y + 3 * u * t * t * segment.points[1].y + t * t * t * segment.points[2].y);
            }
        }
        else
            points.push_back(segment.points.back());
        at = segment.points.back();
    }
    return points;
}

double signedArea(const std::vector<Point>& points)
{
    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area;
}

bool inside(const std::vector<Point>& polygon, const Point& p)
{
    bool in = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const Point& a = polygon[i];
        const Point& b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

// ---------------------------------------------------------------- PDF

std::vector<uint8_t> inflate(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> out;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return out;
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    uint8_t buffer[65536];
    int status = Z_OK;
    while (status == Z_OK)
    {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        status = ::inflate(&stream, Z_NO_FLUSH);
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
        if (status == Z_BUF_ERROR && stream.avail_in == 0)
            break;
    }
    inflateEnd(&stream);
    return out;
}

std::vector<int> contentRefs(const std::string& page)
{
    std::vector<int> refs;
    const size_t at = page.find("/Contents");
    if (at == std::string::npos)
        return refs;
    size_t p = at + 9;
    while (p < page.size() && std::isspace(static_cast<unsigned char>(page[p])))
        ++p;
    const bool array = p < page.size() && page[p] == '[';
    const size_t end = array ? page.find(']', p) : std::min(page.size(), p + 32);
    std::string inside = page.substr(p + (array ? 1 : 0), end - p - (array ? 1 : 0));
    Scanner scan{inside.data(), inside.data() + inside.size()};
    double number = 0, generation = 0;
    while (scan.number(number) && scan.number(generation))
    {
        scan.skip();
        if (scan.at < scan.end && *scan.at == 'R')
            ++scan.at;
        refs.push_back(static_cast<int>(number));
        if (!array)
            break;
    }
    return refs;
}

/// A PDF file's objects, by number, as text the content reader takes
/// apart: a dictionary (or value) as PDF writes it, a stream's data
/// through its filters.
struct PdfFile
{
    pdf::File file;

    /// An object's dictionary (or value) and, for a stream, its data.
    bool object(int number, std::string& dictionary, std::vector<uint8_t>& data, bool& isStream)
    {
        const pdf::Object* found = file.object(number);
        if (!found)
            return false;
        isStream = found->type == pdf::Object::Type::Stream;
        if (isStream)
        {
            std::string remaining;
            data = file.decoded(*found, &remaining);
            dictionary = pdf::File::text(*found, &remaining);
        }
        else
        {
            data.clear();
            dictionary = pdf::File::text(*found);
        }
        return true;
    }

    /// An object's dictionary alone: a stream's data is not decoded for it.
    std::string dictionaryOf(int number)
    {
        const pdf::Object* found = file.object(number);
        if (!found)
            return std::string();
        if (found->type != pdf::Object::Type::Stream)
            return pdf::File::text(*found);
        const std::string remaining = file.remainingFilter(*found);
        return pdf::File::text(*found, &remaining);
    }
};

/// The value of \a key in a dictionary's text: a nested dictionary, an
/// array, or a single token (a reference keeps its "N 0 R").
std::string valueOf(const std::string& dictionary, const std::string& key)
{
    size_t at = 0;
    while ((at = dictionary.find(key, at)) != std::string::npos)
    {
        const size_t after = at + key.size();
        // The whole key, not the start of a longer one.
        if (after < dictionary.size() && (std::isalnum(static_cast<unsigned char>(dictionary[after])) ||
                                             dictionary[after] == '_'))
        {
            at = after;
            continue;
        }
        size_t p = dictionary.find_first_not_of(" \r\n\t", after);
        if (p == std::string::npos)
            return std::string();
        if (dictionary.compare(p, 2, "<<") == 0)
        {
            int depth = 0;
            for (size_t i = p; i + 1 < dictionary.size(); ++i)
            {
                if (dictionary.compare(i, 2, "<<") == 0)
                {
                    ++depth;
                    ++i;
                }
                else if (dictionary.compare(i, 2, ">>") == 0)
                {
                    ++i;
                    if (--depth == 0)
                        return dictionary.substr(p, i - p + 1);
                }
            }
            return dictionary.substr(p);
        }
        if (dictionary[p] == '[')
        {
            int depth = 0;
            for (size_t i = p; i < dictionary.size(); ++i)
            {
                if (dictionary[i] == '[')
                    ++depth;
                else if (dictionary[i] == ']' && --depth == 0)
                    return dictionary.substr(p, i - p + 1);
            }
            return dictionary.substr(p);
        }
        // A token, or a reference "N G R".
        size_t end = p + 1;
        while (end < dictionary.size() && std::strchr(" \r\n\t/[]<>()", dictionary[end]) == nullptr)
            ++end;
        std::string token = dictionary.substr(p, end - p);
        const size_t r = dictionary.find_first_not_of(" \r\n\t", end);
        if (r != std::string::npos && std::isdigit(static_cast<unsigned char>(dictionary[p])))
        {
            size_t g = r;
            while (g < dictionary.size() && std::isdigit(static_cast<unsigned char>(dictionary[g])))
                ++g;
            const size_t rr = dictionary.find_first_not_of(" \r\n\t", g);
            if (g > r && rr != std::string::npos && dictionary[rr] == 'R')
                token = dictionary.substr(p, rr - p + 1);
        }
        return token;
    }
    return std::string();
}

/// A reference's object number, or -1 when \a value is not "N G R".
int referenceOf(const std::string& value)
{
    if (value.empty() || value.back() != 'R' || !std::isdigit(static_cast<unsigned char>(value[0])))
        return -1;
    return std::atoi(value.c_str());
}

/// A value, followed through a reference to the object it names.
std::string resolved(PdfFile& file, const std::string& value)
{
    const int ref = referenceOf(value);
    return ref >= 0 ? file.dictionaryOf(ref) : value;
}

std::vector<double> numbersOf(const std::string& array)
{
    std::vector<double> numbers;
    const std::string inside = array.empty() ? array : array.substr(array.front() == '[' ? 1 : 0);
    Scanner scan{inside.data(), inside.data() + inside.size()};
    double value = 0;
    while (scan.at < scan.end)
    {
        if (scan.number(value))
            numbers.push_back(value);
        else
            ++scan.at;
    }
    return numbers;
}

/// A color from \a values in a space of their count: gray, RGB or CMYK.
Color colorOf(const std::vector<double>& values)
{
    const auto byte = [](double v) { return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
    if (values.size() >= 4)
    {
        const double c = values[0], m = values[1], y = values[2], k = values[3];
        return Color{byte((1 - c) * (1 - k)), byte((1 - m) * (1 - k)), byte((1 - y) * (1 - k)), 255};
    }
    if (values.size() == 3)
        return Color{byte(values[0]), byte(values[1]), byte(values[2]), 255};
    if (!values.empty())
        return Color{byte(values[0]), byte(values[0]), byte(values[0]), 255};
    return Color{0, 0, 0, 255};
}

/// The top-level items of an array's text ("[ /Separation /Spot /DeviceCMYK
/// 12 0 R ]"): names, numbers, references as one item, nested arrays and
/// dictionaries whole.
std::vector<std::string> arrayItems(const std::string& text)
{
    std::vector<std::string> items;
    size_t i = text.find('[');
    if (i == std::string::npos)
        return items;
    ++i;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= text.size() || text[i] == ']')
            break;
        const size_t start = i;
        if (text.compare(i, 2, "<<") == 0 || text[i] == '[' || text[i] == '(')
        {
            int depth = 0;
            for (; i < text.size(); ++i)
            {
                if (text.compare(i, 2, "<<") == 0)
                {
                    ++depth;
                    ++i;
                }
                else if (text.compare(i, 2, ">>") == 0)
                {
                    ++i;
                    if (--depth == 0)
                    {
                        ++i;
                        break;
                    }
                }
                else if (text[i] == '[' || text[i] == '(')
                    ++depth;
                else if ((text[i] == ']' || text[i] == ')') && --depth == 0)
                {
                    ++i;
                    break;
                }
            }
        }
        else
        {
            ++i;
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) &&
                   std::strchr("/[]<>()", text[i]) == nullptr)
                ++i;
            // "N G R" stays one item.
            if (std::isdigit(static_cast<unsigned char>(text[start])))
            {
                size_t j = i;
                while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j])))
                    ++j;
                size_t k = j;
                while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k])))
                    ++k;
                size_t r = k;
                while (r < text.size() && std::isspace(static_cast<unsigned char>(text[r])))
                    ++r;
                if (k > j && r < text.size() && text[r] == 'R')
                    i = r + 1;
            }
        }
        items.push_back(text.substr(start, i - start));
    }
    return items;
}

/// A PostScript calculator function (type 4) run on \a in.
std::vector<double> calculate(const std::string& program, std::vector<double> stack)
{
    // Procedures as nested lists of tokens.
    struct Node
    {
        std::string token;
        std::vector<Node> body;
        bool block = false;
    };
    size_t at = program.find('{');
    if (at == std::string::npos)
        return stack;
    // Braces nested past reason end the program there.
    std::function<Node(size_t&, int)> parse = [&](size_t& i, int depth) {
        Node node;
        node.block = true;
        ++i;
        if (depth > 64)
        {
            i = program.size();
            return node;
        }
        while (i < program.size())
        {
            const char c = program[i];
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++i;
                continue;
            }
            if (c == '{')
            {
                node.body.push_back(parse(i, depth + 1));
                continue;
            }
            if (c == '}')
            {
                ++i;
                break;
            }
            const size_t start = i;
            while (i < program.size() && !std::isspace(static_cast<unsigned char>(program[i])) && program[i] != '{' &&
                   program[i] != '}')
                ++i;
            Node word;
            word.token = program.substr(start, i - start);
            node.body.push_back(word);
        }
        return node;
    };
    const Node root = parse(at, 0);
    int steps = 0;
    std::function<void(const Node&)> run = [&](const Node& proc) {
        std::vector<const Node*> pending;
        for (size_t k = 0; k < proc.body.size(); ++k)
        {
            // The calculator's stack holds 100 values; well past that the
            // program is damaged.
            if (++steps > 100000 || stack.size() > 1000)
                return;
            const Node& n = proc.body[k];
            if (n.block)
            {
                pending.push_back(&n);
                continue;
            }
            const std::string& w = n.token;
            const auto pop = [&stack]() {
                if (stack.empty())
                    return 0.0;
                const double v = stack.back();
                stack.pop_back();
                return v;
            };
            char* end = nullptr;
            const double number = std::strtod(w.c_str(), &end);
            if (end && *end == '\0' && !w.empty())
            {
                stack.push_back(number);
                continue;
            }
            const double pi = 3.14159265358979323846;
            if (w == "add") { const double b = pop(), a = pop(); stack.push_back(a + b); }
            else if (w == "sub") { const double b = pop(), a = pop(); stack.push_back(a - b); }
            else if (w == "mul") { const double b = pop(), a = pop(); stack.push_back(a * b); }
            else if (w == "div") { const double b = pop(), a = pop(); stack.push_back(b != 0 ? a / b : 0); }
            else if (w == "idiv") { const double b = pop(), a = pop(); stack.push_back(b != 0 ? std::trunc(a / b) : 0); }
            else if (w == "mod") { const double b = pop(), a = pop(); stack.push_back(b != 0 ? std::fmod(a, b) : 0); }
            else if (w == "neg") stack.push_back(-pop());
            else if (w == "abs") stack.push_back(std::fabs(pop()));
            else if (w == "ceiling") stack.push_back(std::ceil(pop()));
            else if (w == "floor") stack.push_back(std::floor(pop()));
            else if (w == "round") stack.push_back(std::floor(pop() + 0.5));
            else if (w == "truncate" || w == "cvi") stack.push_back(std::trunc(pop()));
            else if (w == "cvr") {}
            else if (w == "sqrt") stack.push_back(std::sqrt(std::max(0.0, pop())));
            else if (w == "sin") stack.push_back(std::sin(pop() * pi / 180.0));
            else if (w == "cos") stack.push_back(std::cos(pop() * pi / 180.0));
            else if (w == "atan") { const double b = pop(), a = pop(); double d = std::atan2(a, b) * 180.0 / pi; stack.push_back(d < 0 ? d + 360 : d); }
            else if (w == "exp") { const double b = pop(), a = pop(); stack.push_back(std::pow(a, b)); }
            else if (w == "ln") stack.push_back(std::log(std::max(1e-300, pop())));
            else if (w == "log") stack.push_back(std::log10(std::max(1e-300, pop())));
            else if (w == "dup") { const double a = pop(); stack.push_back(a); stack.push_back(a); }
            else if (w == "pop") pop();
            else if (w == "exch") { const double b = pop(), a = pop(); stack.push_back(b); stack.push_back(a); }
            else if (w == "index") { const int n2 = static_cast<int>(pop()); stack.push_back(n2 >= 0 && static_cast<size_t>(n2) < stack.size() ? stack[stack.size() - 1 - static_cast<size_t>(n2)] : 0); }
            else if (w == "copy") { const int n2 = static_cast<int>(pop()); if (n2 > 0 && static_cast<size_t>(n2) <= stack.size() && stack.size() <= 1000) { const size_t s = stack.size() - static_cast<size_t>(n2); for (int q = 0; q < n2; ++q) stack.push_back(stack[s + static_cast<size_t>(q)]); } }
            else if (w == "roll")
            {
                int j = static_cast<int>(pop());
                const int n2 = static_cast<int>(pop());
                if (n2 > 0 && static_cast<size_t>(n2) <= stack.size())
                {
                    j = ((j % n2) + n2) % n2;
                    std::rotate(stack.end() - n2, stack.end() - j, stack.end());
                }
            }
            else if (w == "true") stack.push_back(1);
            else if (w == "false") stack.push_back(0);
            else if (w == "eq") { const double b = pop(), a = pop(); stack.push_back(a == b); }
            else if (w == "ne") { const double b = pop(), a = pop(); stack.push_back(a != b); }
            else if (w == "gt") { const double b = pop(), a = pop(); stack.push_back(a > b); }
            else if (w == "ge") { const double b = pop(), a = pop(); stack.push_back(a >= b); }
            else if (w == "lt") { const double b = pop(), a = pop(); stack.push_back(a < b); }
            else if (w == "le") { const double b = pop(), a = pop(); stack.push_back(a <= b); }
            else if (w == "and") { const double b = pop(), a = pop(); stack.push_back((a != 0) && (b != 0)); }
            else if (w == "or") { const double b = pop(), a = pop(); stack.push_back((a != 0) || (b != 0)); }
            else if (w == "not") stack.push_back(pop() == 0);
            else if (w == "if")
            {
                const Node* body = pending.empty() ? nullptr : pending.back();
                if (!pending.empty())
                    pending.pop_back();
                if (pop() != 0 && body)
                    run(*body);
            }
            else if (w == "ifelse")
            {
                const Node* no = pending.size() >= 1 ? pending[pending.size() - 1] : nullptr;
                const Node* yes = pending.size() >= 2 ? pending[pending.size() - 2] : nullptr;
                pending.resize(pending.size() >= 2 ? pending.size() - 2 : 0);
                const bool test = pop() != 0;
                if (test && yes)
                    run(*yes);
                else if (!test && no)
                    run(*no);
            }
        }
    };
    run(root);
    return stack;
}

/// A PDF function (types 0, 2, 3 and 4, or an array of one-output ones)
/// at \a in.
std::vector<double> evaluate(PdfFile& file, const std::string& value, const std::vector<double>& in, int depth = 0)
{
    if (depth > 8)
        return in;
    std::string dictionary = value;
    std::vector<uint8_t> data;
    const int reference = referenceOf(value);
    if (reference >= 0)
    {
        bool isStream = false;
        if (!file.object(reference, dictionary, data, isStream))
            return in;
    }
    // An array of functions, one an output.
    if (!dictionary.empty() && dictionary.front() == '[')
    {
        std::vector<double> out;
        for (const std::string& item : arrayItems(dictionary))
        {
            const std::vector<double> part = evaluate(file, item, in, depth + 1);
            if (!part.empty())
                out.push_back(part.front());
        }
        return out;
    }
    const int type = std::atoi(valueOf(dictionary, "/FunctionType").c_str());
    const std::vector<double> domain = numbersOf(valueOf(dictionary, "/Domain"));
    const double x = in.empty() ? 0.0
        : domain.size() >= 2 ? std::clamp(in[0], std::min(domain[0], domain[1]), std::max(domain[0], domain[1]))
                             : in[0];
    if (type == 2)
    {
        std::vector<double> c0 = numbersOf(valueOf(dictionary, "/C0"));
        std::vector<double> c1 = numbersOf(valueOf(dictionary, "/C1"));
        if (c0.empty())
            c0 = {0.0};
        if (c1.empty())
            c1 = {1.0};
        const double n = valueOf(dictionary, "/N").empty() ? 1.0 : std::atof(valueOf(dictionary, "/N").c_str());
        const double f = std::pow(std::max(0.0, x), n);
        std::vector<double> out;
        for (size_t i = 0; i < c0.size() && i < c1.size(); ++i)
            out.push_back(c0[i] + f * (c1[i] - c0[i]));
        return out;
    }
    if (type == 3)
    {
        const std::vector<std::string> functions = arrayItems(valueOf(dictionary, "/Functions"));
        const std::vector<double> bounds = numbersOf(valueOf(dictionary, "/Bounds"));
        const std::vector<double> encode = numbersOf(valueOf(dictionary, "/Encode"));
        if (functions.empty())
            return in;
        const double low = domain.size() >= 2 ? domain[0] : 0.0, high = domain.size() >= 2 ? domain[1] : 1.0;
        size_t k = 0;
        while (k < bounds.size() && x >= bounds[k])
            ++k;
        k = std::min(k, functions.size() - 1);
        const double from = k == 0 ? low : bounds[k - 1];
        const double to = k < bounds.size() ? bounds[k] : high;
        double e0 = 0.0, e1 = 1.0;
        if (encode.size() >= 2 * (k + 1))
        {
            e0 = encode[2 * k];
            e1 = encode[2 * k + 1];
        }
        const double mapped = to > from ? e0 + (x - from) / (to - from) * (e1 - e0) : e0;
        return evaluate(file, functions[k], {mapped}, depth + 1);
    }
    if (type == 0 && !data.empty())
    {
        // Sampled: one input, linear between samples.
        const std::vector<double> size = numbersOf(valueOf(dictionary, "/Size"));
        const int bits = std::atoi(valueOf(dictionary, "/BitsPerSample").c_str());
        const std::vector<double> range = numbersOf(valueOf(dictionary, "/Range"));
        std::vector<double> decode = numbersOf(valueOf(dictionary, "/Decode"));
        const size_t outputs = range.size() / 2;
        // A Decode too short for the outputs falls back to the range.
        if (decode.size() < 2 * outputs)
            decode = range;
        // At least one sample, and no more than the data could hold.
        if (size.empty() || outputs == 0 || (bits != 8 && bits != 16) || !(size[0] >= 1) ||
            size[0] > static_cast<double>(data.size()))
            return in;
        const int samples = static_cast<int>(size[0]);
        const double low = domain.size() >= 2 ? domain[0] : 0.0, high = domain.size() >= 2 ? domain[1] : 1.0;
        double position = high > low ? (x - low) / (high - low) * (samples - 1) : 0.0;
        if (!(position == position))
            position = 0.0;
        position = std::clamp(position, 0.0, static_cast<double>(samples - 1));
        const int i0 = std::clamp(static_cast<int>(std::floor(position)), 0, samples - 1);
        const int i1 = std::min(samples - 1, i0 + 1);
        const double f = position - i0;
        const size_t bytes = static_cast<size_t>(bits / 8);
        const auto sample = [&](int index, size_t output) {
            const size_t at = (static_cast<size_t>(index) * outputs + output) * bytes;
            if (at + bytes > data.size())
                return 0.0;
            const double raw = bytes == 1 ? data[at] : (data[at] << 8 | data[at + 1]);
            const double maxValue = bytes == 1 ? 255.0 : 65535.0;
            return decode[2 * output] + raw / maxValue * (decode[2 * output + 1] - decode[2 * output]);
        };
        std::vector<double> out;
        for (size_t o = 0; o < outputs; ++o)
            out.push_back(sample(i0, o) + f * (sample(i1, o) - sample(i0, o)));
        return out;
    }
    if (type == 4 && !data.empty())
        return calculate(std::string(data.begin(), data.end()), in);
    return in;
}

/// How many components a color space's values have.
int componentsOf(PdfFile& file, const std::string& space)
{
    const std::string text = resolved(file, space);
    if (text.find("CMYK") != std::string::npos)
        return 4;
    if (text.find("RGB") != std::string::npos || text.find("/Lab") != std::string::npos)
        return 3;
    if (text.find("ICCBased") != std::string::npos)
    {
        const std::vector<std::string> items = arrayItems(text);
        if (items.size() >= 2)
        {
            const std::vector<double> n = numbersOf(valueOf(resolved(file, items[1]), "/N"));
            if (!n.empty() && n.front() == n.front())
                return static_cast<int>(std::clamp(n.front(), 1.0, 32.0));
        }
    }
    return 1;
}

/// A color in \a space (a color space resource, or a device space's name)
/// from \a values: a separation's or DeviceN's tint through its tint
/// transform into its alternate space.
Color colorIn(PdfFile& file, const std::string& space, const std::vector<double>& values)
{
    const std::string text = resolved(file, space);
    if (!text.empty() && text.front() == '[')
    {
        const std::vector<std::string> items = arrayItems(text);
        if (items.size() >= 4 && (items[0] == "/Separation" || items[0] == "/DeviceN"))
        {
            if (items[1] == "/None")
                return Color{0, 0, 0, 0};
            const std::vector<double> out = evaluate(file, items[3], values);
            const int n = componentsOf(file, items[2]);
            std::vector<double> alternate(out.begin(), out.begin() + std::min(out.size(), static_cast<size_t>(n)));
            if (n == 3 && resolved(file, items[2]).find("/Lab") != std::string::npos && alternate.size() == 3)
            {
                // Lab, roughly: lightness as gray.
                const double l = std::clamp(alternate[0] / 100.0, 0.0, 1.0);
                alternate = {l, l, l};
            }
            return colorOf(alternate);
        }
    }
    return colorOf(values);
}

/// A shading's colors, from its function: an exponential one between two
/// colors, or stitched ones end to end. Empty for ones not read.
std::vector<Gradient::Stop> shadingStops(PdfFile& file, const std::string& function, int depth = 0)
{
    std::vector<Gradient::Stop> stops;
    const std::string dictionary = resolved(file, function);
    if (dictionary.empty() || depth > 4)
        return stops;
    const int type = std::atoi(valueOf(dictionary, "/FunctionType").c_str());
    if (type == 2)
    {
        std::vector<double> c0 = numbersOf(valueOf(dictionary, "/C0"));
        std::vector<double> c1 = numbersOf(valueOf(dictionary, "/C1"));
        if (c0.empty())
            c0 = {0.0};
        if (c1.empty())
            c1 = {1.0};
        stops.push_back({0.0, colorOf(c0)});
        stops.push_back({1.0, colorOf(c1)});
    }
    else if (type == 3)
    {
        const std::string list = valueOf(dictionary, "/Functions");
        std::vector<double> bounds = numbersOf(valueOf(dictionary, "/Bounds"));
        std::vector<double> domain = numbersOf(valueOf(dictionary, "/Domain"));
        const double low = domain.size() >= 2 ? domain[0] : 0.0;
        const double high = domain.size() >= 2 ? domain[1] : 1.0;
        // The functions: references, or dictionaries written in place.
        std::vector<std::string> parts;
        for (size_t i = 1; i + 1 < list.size();)
        {
            const size_t p = list.find_first_not_of(" \r\n\t", i);
            if (p == std::string::npos || p + 1 >= list.size())
                break;
            if (list.compare(p, 2, "<<") == 0)
            {
                const std::string inner = valueOf(" /X " + list.substr(p), "/X");
                parts.push_back(inner);
                i = p + inner.size();
            }
            else if (std::isdigit(static_cast<unsigned char>(list[p])))
            {
                const size_t r = list.find('R', p);
                if (r == std::string::npos)
                    break;
                parts.push_back(list.substr(p, r - p + 1));
                i = r + 1;
            }
            else
                i = p + 1;
        }
        std::vector<double> edges = {low};
        edges.insert(edges.end(), bounds.begin(), bounds.end());
        edges.push_back(high);
        const double span = high > low ? high - low : 1.0;
        for (size_t k = 0; k < parts.size() && k + 1 < edges.size(); ++k)
        {
            const std::vector<Gradient::Stop> inner = shadingStops(file, parts[k], depth + 1);
            for (const Gradient::Stop& stop : inner)
            {
                const double offset = (edges[k] + stop.offset * (edges[k + 1] - edges[k]) - low) / span;
                if (!stops.empty() && std::fabs(stops.back().offset - offset) < 1.0e-9 &&
                    stops.back().color == stop.color)
                    continue;
                stops.push_back({std::clamp(offset, 0.0, 1.0), stop.color});
            }
        }
    }
    return stops;
}

/// A shading as a gradient, its coordinates mapped to the stage by
/// \a toStage; none for kinds not read.
std::optional<Gradient> shadingGradient(PdfFile& file, const std::string& value, const Affine& toStage)
{
    const std::string shading = resolved(file, value);
    if (shading.empty())
        return std::nullopt;
    const int type = std::atoi(valueOf(shading, "/ShadingType").c_str());
    const std::vector<double> coords = numbersOf(valueOf(shading, "/Coords"));
    Gradient gradient;
    gradient.stops = shadingStops(file, valueOf(shading, "/Function"));
    if (gradient.stops.size() < 2)
        return std::nullopt;
    if (type == 2 && coords.size() >= 4)
    {
        gradient.placement = toPlacement(toStage.after(toAffine(
            linearPlacement(Point(coords[0], coords[1]), Point(coords[2], coords[3])))));
        return gradient;
    }
    if (type == 3 && coords.size() >= 6 && coords[5] > 0.0)
    {
        gradient.radial = true;
        const Point inner(coords[0], coords[1]);
        const Point outer(coords[3], coords[4]);
        const double radius = coords[5];
        const double toward = std::hypot(inner.x - outer.x, inner.y - outer.y);
        gradient.placement = toPlacement(toStage.after(toAffine(radialPlacement(outer, radius,
            toward > 1.0e-9 ? std::atan2(inner.y - outer.y, inner.x - outer.x) : 0.0))));
        gradient.focal = std::clamp(toward / radius, 0.0, 0.99);
        // A start circle of its own: the colors begin out at it.
        const double start = std::clamp(coords[2] / radius, 0.0, 0.99);
        for (Gradient::Stop& stop : gradient.stops)
            stop.offset = start + stop.offset * (1.0 - start);
        return gradient;
    }
    return std::nullopt;
}

/// An image XObject as pixels or its JPEG file; false for kinds not read.
/// A PDF string's bytes from its text: "(...)" with its escapes, or
/// "<hex>".
std::vector<uint8_t> stringBytes(const std::string& text)
{
    std::vector<uint8_t> out;
    if (text.empty())
        return out;
    if (text.front() == '<')
    {
        int high = -1;
        for (size_t i = 1; i < text.size() && text[i] != '>'; ++i)
        {
            const char c = text[i];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F'                                       ? c - 'A' + 10
                                                                             : -1;
            if (v < 0)
                continue;
            if (high < 0)
                high = v;
            else
            {
                out.push_back(static_cast<uint8_t>(high * 16 + v));
                high = -1;
            }
        }
        return out;
    }
    for (size_t i = 1; i + 1 < text.size(); ++i)
    {
        char c = text[i];
        if (c == '\\' && i + 2 < text.size())
        {
            c = text[++i];
            if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
            else if (c >= '0' && c <= '7')
            {
                int value = c - '0';
                for (int k = 0; k < 2 && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '7'; ++k)
                    value = value * 8 + (text[++i] - '0');
                c = static_cast<char>(value & 0xFF);
            }
        }
        out.push_back(static_cast<uint8_t>(c));
    }
    return out;
}

bool pdfImage(PdfFile& file, const std::string& dictionary, const std::vector<uint8_t>& data, Image& image)
{
    const std::string filter = valueOf(dictionary, "/Filter");
    image.width = std::atoi(valueOf(dictionary, "/Width").c_str());
    image.height = std::atoi(valueOf(dictionary, "/Height").c_str());
    if (image.width <= 0 || image.height <= 0)
        return false;
    if (filter.find("DCTDecode") != std::string::npos)
    {
        image.encoded = data;
        // CMYK read as ink unless Decode inverts it.
        const std::vector<double> decode = numbersOf(valueOf(dictionary, "/Decode"));
        const std::string space = resolved(file, valueOf(dictionary, "/ColorSpace"));
        const bool cmyk = space.rfind("/DeviceCMYK", 0) == 0 ||
            (space.find("ICCBased") != std::string::npos && componentsOf(file, space) == 4);
        image.inkCmyk = cmyk && !(decode.size() >= 2 && decode[0] > decode[1]);
        return !image.encoded.empty();
    }
    // Decoded samples (Flate and the rest already undone): gray, RGB, CMYK,
    // a separation, or indices into a palette, 1 to 8 bits each.
    if (!filter.empty() && filter.find("FlateDecode") == std::string::npos)
        return false;
    const int bits = std::atoi(valueOf(dictionary, "/BitsPerComponent").c_str());
    if (bits != 1 && bits != 2 && bits != 4 && bits != 8)
        return false;
    const std::string space = resolved(file, valueOf(dictionary, "/ColorSpace"));
    // A palette: each index's color, worked out once.
    std::vector<Color> palette;
    std::string base = space;
    int components = 0;
    if (space.find("/Indexed") != std::string::npos && !space.empty() && space.front() == '[')
    {
        const std::vector<std::string> items = arrayItems(space);
        if (items.size() < 4)
            return false;
        base = resolved(file, items[1]);
        // A palette holds 256 colors at most.
        const int high = std::clamp(std::atoi(items[2].c_str()), 0, 255);
        std::vector<uint8_t> lookup;
        const int reference = referenceOf(items[3]);
        if (reference >= 0)
        {
            std::string table;
            bool isStream = false;
            if (!file.object(reference, table, lookup, isStream))
                return false;
            if (!isStream)
                lookup = stringBytes(table);
        }
        else
            lookup = stringBytes(items[3]);
        const int n = std::max(1, componentsOf(file, base));
        for (int i = 0; i <= high; ++i)
        {
            std::vector<double> values;
            for (int c = 0; c < n; ++c)
            {
                const size_t at = static_cast<size_t>(i * n + c);
                values.push_back(at < lookup.size() ? lookup[at] / 255.0 : 0.0);
            }
            palette.push_back(colorIn(file, base, values));
        }
        components = 1;
    }
    else if (space.find("/Separation") != std::string::npos || space.find("/DeviceN") != std::string::npos)
    {
        const std::vector<std::string> items = arrayItems(space);
        components = items.size() >= 2 && items[0] == "/DeviceN" ? static_cast<int>(arrayItems(items[1]).size()) : 1;
    }
    else
        components = componentsOf(file, space);
    // Up to the 32 colorants a space can have.
    components = std::clamp(components, 1, 32);
    const std::vector<double> decode = numbersOf(valueOf(dictionary, "/Decode"));
    const bool inverted = palette.empty() && components == 1 && decode.size() >= 2 && decode[0] > decode[1];
    const size_t row = (static_cast<size_t>(image.width) * static_cast<size_t>(components) * static_cast<size_t>(bits) + 7) / 8;
    // Divided rather than multiplied, so a huge size cannot wrap around.
    if (row == 0 || static_cast<size_t>(image.height) > data.size() / row)
        return false;
    const size_t sourcePixels = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    const int maxValue = (1 << bits) - 1;
    const auto sample = [&](size_t y, size_t index) {
        const size_t bit = index * static_cast<size_t>(bits);
        const uint8_t byte = data[y * row + bit / 8];
        const int shift = 8 - bits - static_cast<int>(bit % 8);
        return (byte >> shift) & maxValue;
    };
    // A very large image comes in at a step of its pixels, so a page of
    // them fits in memory; it covers the same place.
    const int sourceWidth = image.width, sourceHeight = image.height;
    const double megapixels = static_cast<double>(sourceWidth) * sourceHeight / 1.0e6;
    const int step = megapixels > 16.0 ? static_cast<int>(std::ceil(std::sqrt(megapixels / 16.0))) : 1;
    image.width = (sourceWidth + step - 1) / step;
    image.height = (sourceHeight + step - 1) / step;
    const size_t pixels = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    image.rgba.assign(pixels * 4, 255);
    const bool special = space.find("/Separation") != std::string::npos || space.find("/DeviceN") != std::string::npos;
    std::map<uint32_t, Color> cache;
    for (size_t oy = 0; oy < static_cast<size_t>(image.height); ++oy)
        for (size_t ox = 0; ox < static_cast<size_t>(image.width); ++ox)
        {
            const size_t p = oy * static_cast<size_t>(image.width) + ox;
            const size_t y = oy * static_cast<size_t>(step), x = ox * static_cast<size_t>(step);
            uint8_t* out = &image.rgba[p * 4];
            Color color;
            if (!palette.empty())
            {
                const int index = sample(y, x);
                color = index < static_cast<int>(palette.size()) ? palette[static_cast<size_t>(index)] : Color{0, 0, 0, 255};
            }
            else
            {
                std::vector<double> values;
                uint32_t key = 0;
                for (int c = 0; c < components; ++c)
                {
                    const int v = sample(y, x * static_cast<size_t>(components) + static_cast<size_t>(c));
                    key = key * 257u + static_cast<uint32_t>(v);
                    values.push_back(static_cast<double>(v) / maxValue);
                }
                if (inverted)
                    values[0] = 1.0 - values[0];
                if (special)
                {
                    const auto found = cache.find(key);
                    if (found != cache.end())
                        color = found->second;
                    else if (cache.size() < 4096)
                        color = cache[key] = colorIn(file, space, values);
                    else
                        color = colorIn(file, space, values);
                }
                else
                    color = colorOf(values);
            }
            out[0] = color[0];
            out[1] = color[1];
            out[2] = color[2];
        }
    // A soft mask: the alpha, 8-bit gray.
    const int mask = referenceOf(valueOf(dictionary, "/SMask"));
    if (mask >= 0)
    {
        std::string maskDictionary;
        std::vector<uint8_t> alpha;
        bool isStream = false;
        if (file.object(mask, maskDictionary, alpha, isStream) && isStream && alpha.size() >= sourcePixels &&
            std::atoi(valueOf(maskDictionary, "/Width").c_str()) == sourceWidth)
            for (size_t oy = 0; oy < static_cast<size_t>(image.height); ++oy)
                for (size_t ox = 0; ox < static_cast<size_t>(image.width); ++ox)
                    image.rgba[(oy * static_cast<size_t>(image.width) + ox) * 4 + 3] =
                        alpha[oy * static_cast<size_t>(step) * static_cast<size_t>(sourceWidth) + ox * static_cast<size_t>(step)];
    }
    return true;
}

// ---------------------------------------------------------------- PDF text

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
    else if (c < 0x110000)
    {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

/// Windows' code page 1252 from 0x80 to 0x9F (0 where it has nothing).
const uint16_t kWinAnsiHigh[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161,
    0x203A, 0x0153, 0, 0x017E, 0x0178};

/// Mac OS Roman from 0x80 to 0xFF.
const uint16_t kMacRomanHigh[128] = {0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2,
    0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022,
    0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265,
    0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1,
    0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A,
    0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE,
    0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8,
    0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};

/// The text a glyph name stands for: the printable ASCII and Latin-1
/// names, the punctuation and letters Windows' code page adds, uniXXXX and
/// uXXXX names, ligatures joined with "_", variants after a ".".
std::string glyphText(std::string name)
{
    static const std::map<std::string, uint32_t> names = [] {
        std::map<std::string, uint32_t> m;
        static const char* const ascii[] = {"space", "exclam", "quotedbl", "numbersign", "dollar", "percent",
            "ampersand", "quotesingle", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period",
            "slash", "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "colon",
            "semicolon", "less", "equal", "greater", "question", "at"};
        for (uint32_t i = 0; i < 33; ++i)
            m[ascii[i]] = 0x20 + i;
        for (uint32_t c = 'A'; c <= 'Z'; ++c)
            m[std::string(1, static_cast<char>(c))] = c;
        for (uint32_t c = 'a'; c <= 'z'; ++c)
            m[std::string(1, static_cast<char>(c))] = c;
        static const char* const brackets[] = {"bracketleft", "backslash", "bracketright", "asciicircum",
            "underscore", "grave"};
        for (uint32_t i = 0; i < 6; ++i)
            m[brackets[i]] = 0x5B + i;
        static const char* const braces[] = {"braceleft", "bar", "braceright", "asciitilde"};
        for (uint32_t i = 0; i < 4; ++i)
            m[braces[i]] = 0x7B + i;
        static const char* const latin1[] = {"nbspace", "exclamdown", "cent", "sterling", "currency", "yen",
            "brokenbar", "section", "dieresis", "copyright", "ordfeminine", "guillemotleft", "logicalnot", "sfthyphen",
            "registered", "macron", "degree", "plusminus", "twosuperior", "threesuperior", "acute", "mu", "paragraph",
            "periodcentered", "cedilla", "onesuperior", "ordmasculine", "guillemotright", "onequarter", "onehalf",
            "threequarters", "questiondown", "Agrave", "Aacute", "Acircumflex", "Atilde", "Adieresis", "Aring", "AE",
            "Ccedilla", "Egrave", "Eacute", "Ecircumflex", "Edieresis", "Igrave", "Iacute", "Icircumflex", "Idieresis",
            "Eth", "Ntilde", "Ograve", "Oacute", "Ocircumflex", "Otilde", "Odieresis", "multiply", "Oslash", "Ugrave",
            "Uacute", "Ucircumflex", "Udieresis", "Yacute", "Thorn", "germandbls", "agrave", "aacute", "acircumflex",
            "atilde", "adieresis", "aring", "ae", "ccedilla", "egrave", "eacute", "ecircumflex", "edieresis", "igrave",
            "iacute", "icircumflex", "idieresis", "eth", "ntilde", "ograve", "oacute", "ocircumflex", "otilde",
            "odieresis", "divide", "oslash", "ugrave", "uacute", "ucircumflex", "udieresis", "yacute", "thorn",
            "ydieresis"};
        for (uint32_t i = 0; i < 96; ++i)
            m[latin1[i]] = 0xA0 + i;
        const std::pair<const char*, uint32_t> more[] = {{"quoteleft", 0x2018}, {"quoteright", 0x2019},
            {"quotedblleft", 0x201C}, {"quotedblright", 0x201D}, {"quotesinglbase", 0x201A}, {"quotedblbase", 0x201E},
            {"endash", 0x2013}, {"emdash", 0x2014}, {"bullet", 0x2022}, {"ellipsis", 0x2026}, {"dagger", 0x2020},
            {"daggerdbl", 0x2021}, {"perthousand", 0x2030}, {"trademark", 0x2122}, {"Euro", 0x20AC},
            {"florin", 0x0192}, {"circumflex", 0x02C6}, {"tilde", 0x02DC}, {"Scaron", 0x0160}, {"scaron", 0x0161},
            {"Zcaron", 0x017D}, {"zcaron", 0x017E}, {"OE", 0x0152}, {"oe", 0x0153}, {"Ydieresis", 0x0178},
            {"guilsinglleft", 0x2039}, {"guilsinglright", 0x203A}, {"fi", 0xFB01}, {"fl", 0xFB02}, {"ff", 0xFB00},
            {"ffi", 0xFB03}, {"ffl", 0xFB04}, {"dotlessi", 0x0131}, {"fraction", 0x2044}, {"minus", 0x2212},
            {"Lslash", 0x0141}, {"lslash", 0x0142}, {"breve", 0x02D8}, {"dotaccent", 0x02D9}, {"ring", 0x02DA},
            {"hungarumlaut", 0x02DD}, {"ogonek", 0x02DB}, {"caron", 0x02C7}, {"space.alt", 0x20}};
        for (const auto& [n, c] : more)
            m[n] = c;
        return m;
    }();
    const size_t dot = name.find('.');
    if (dot != std::string::npos && dot > 0)
        name = name.substr(0, dot);
    std::string out;
    if (name.find('_') != std::string::npos)
    {
        size_t from = 0;
        while (from <= name.size())
        {
            const size_t to = std::min(name.find('_', from), name.size());
            out += glyphText(name.substr(from, to - from));
            from = to + 1;
        }
        return out;
    }
    const auto found = names.find(name);
    if (found != names.end())
    {
        appendUtf8(out, found->second);
        return out;
    }
    const auto hex = [](const std::string& digits) {
        if (digits.empty() || digits.find_first_not_of("0123456789ABCDEFabcdef") != std::string::npos)
            return 0u;
        return static_cast<uint32_t>(std::strtoul(digits.c_str(), nullptr, 16));
    };
    if (name.size() >= 7 && name.compare(0, 3, "uni") == 0)
    {
        for (size_t k = 3; k + 4 <= name.size(); k += 4)
            appendUtf8(out, hex(name.substr(k, 4)));
        return out;
    }
    if (name.size() >= 5 && name[0] == 'u')
    {
        if (const uint32_t c = hex(name.substr(1)))
            appendUtf8(out, c);
    }
    return out;
}

/// A font as a page's text uses it: how its codes read as text, how wide
/// each glyph is, and its family and style.
struct PdfFont
{
    /// Two bytes a code (a composite font), or one.
    bool twoByte = false;
    /// From its ToUnicode map, which wins.
    std::map<uint32_t, std::string> unicode;
    /// A simple font's codes through its encoding.
    std::array<std::string, 256> encoded;
    /// Widths in thousandths of the size; \a missing for the rest.
    std::map<uint32_t, double> widths;
    double missing = 500.0;
    /// Glyph space to text space (a Type 3 font's own; else a thousandth).
    double unitsPerEm = 1000.0;
    std::string family = "Helvetica";
    bool bold = false;
    bool italic = false;

    std::string text(uint32_t code) const
    {
        const auto found = unicode.find(code);
        if (found != unicode.end())
            return found->second;
        if (!twoByte && code < 256)
            return encoded[code];
        return std::string();
    }

    double width(uint32_t code) const
    {
        const auto found = widths.find(code);
        return (found != widths.end() ? found->second : missing) / unitsPerEm;
    }
};

/// A family name from a PostScript name: "ABCDEF+TimesNewRomanPS-BoldMT"
/// is "Times New Roman", bold. The style follows the hyphen or comma.
void fontNameParts(std::string name, std::string& family, bool& bold, bool& italic)
{
    if (!name.empty() && name[0] == '/')
        name = name.substr(1);
    // The subset tag: six capitals and a plus.
    if (name.size() > 7 && name[6] == '+' &&
        std::all_of(name.begin(), name.begin() + 6, [](char c) { return c >= 'A' && c <= 'Z'; }))
        name = name.substr(7);
    // PDF names write a space as #20.
    for (size_t at; (at = name.find("#20")) != std::string::npos;)
        name.replace(at, 3, " ");
    const size_t split = name.find_first_of("-,");
    std::string base = name.substr(0, split);
    const std::string style = split == std::string::npos ? std::string() : name.substr(split + 1);
    const auto has = [&](const char* part) {
        return style.find(part) != std::string::npos || base.find(part) != std::string::npos;
    };
    bold = has("Bold") || has("Black") || has("Heavy") || has("Semibold") || has("Demi");
    // "It" as in BoldIt or SemiboldIt.
    italic = has("Oblique") || style.find("It") != std::string::npos;
    for (const char* suffix : {"PSMT", "PS", "MT"})
    {
        const size_t n = std::strlen(suffix);
        if (base.size() > n + 1 && base.compare(base.size() - n, n, suffix) == 0 &&
            std::islower(static_cast<unsigned char>(base[base.size() - n - 1])))
        {
            base.erase(base.size() - n);
            break;
        }
    }
    // Words run together: a space before each capital after a small letter.
    std::string spaced;
    for (size_t k = 0; k < base.size(); ++k)
    {
        if (k > 0 && std::isupper(static_cast<unsigned char>(base[k])) &&
            std::islower(static_cast<unsigned char>(base[k - 1])))
            spaced += ' ';
        spaced += base[k];
    }
    family = spaced.empty() ? std::string("Helvetica") : spaced;
}

/// Reads a ToUnicode map's bfchar and bfrange entries.
void readToUnicode(const std::string& cmap, std::map<uint32_t, std::string>& out)
{
    const auto code = [](const std::string& token) {
        const std::vector<uint8_t> bytes = stringBytes(token);
        uint32_t v = 0;
        for (uint8_t b : bytes)
            v = (v << 8) | b;
        return v;
    };
    const auto utf16 = [](const std::string& token) {
        const std::vector<uint8_t> bytes = stringBytes(token);
        std::string text;
        for (size_t k = 0; k + 1 < bytes.size(); k += 2)
        {
            uint32_t c = static_cast<uint32_t>(bytes[k] << 8 | bytes[k + 1]);
            if (c >= 0xD800 && c < 0xDC00 && k + 3 < bytes.size())
            {
                const uint32_t low = static_cast<uint32_t>(bytes[k + 2] << 8 | bytes[k + 3]);
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                k += 2;
            }
            appendUtf8(text, c);
        }
        return text;
    };
    // The tokens between a begin and its end: hex strings and arrays.
    const auto tokens = [](const std::string& text, size_t from, size_t to) {
        std::vector<std::string> list;
        for (size_t k = from; k < to;)
        {
            if (text[k] == '<')
            {
                const size_t close = text.find('>', k);
                if (close == std::string::npos || close > to)
                    break;
                list.push_back(text.substr(k, close - k + 1));
                k = close + 1;
            }
            else if (text[k] == '[')
            {
                const size_t close = text.find(']', k);
                if (close == std::string::npos || close > to)
                    break;
                list.push_back(text.substr(k, close - k + 1));
                k = close + 1;
            }
            else
                ++k;
        }
        return list;
    };
    for (size_t at = 0; (at = cmap.find("beginbfchar", at)) != std::string::npos;)
    {
        const size_t end = cmap.find("endbfchar", at);
        if (end == std::string::npos)
            break;
        const std::vector<std::string> list = tokens(cmap, at + 11, end);
        for (size_t k = 0; k + 1 < list.size(); k += 2)
            out[code(list[k])] = utf16(list[k + 1]);
        at = end;
    }
    for (size_t at = 0; (at = cmap.find("beginbfrange", at)) != std::string::npos;)
    {
        const size_t end = cmap.find("endbfrange", at);
        if (end == std::string::npos)
            break;
        const std::vector<std::string> list = tokens(cmap, at + 12, end);
        // Counted in 64 bits so a range at the top of the codes ends, and
        // no more than a million codes in all.
        for (size_t k = 0; k + 2 < list.size() && out.size() < (1u << 20); k += 3)
        {
            const uint64_t low = code(list[k]);
            const uint64_t high = std::min<uint64_t>(code(list[k + 1]), low + 0xFFFF);
            if (list[k + 2][0] == '[')
            {
                const std::vector<std::string> each = tokens(list[k + 2], 1, list[k + 2].size() - 1);
                for (uint64_t c = low; c <= high && c - low < each.size(); ++c)
                    out[static_cast<uint32_t>(c)] = utf16(each[static_cast<size_t>(c - low)]);
            }
            else
            {
                // Each code after the first adds to the last unit.
                std::vector<uint8_t> first = stringBytes(list[k + 2]);
                for (uint64_t c = low; c <= high && !first.empty(); ++c)
                {
                    std::string hex = "<";
                    char digits[3];
                    for (uint8_t b : first)
                    {
                        std::snprintf(digits, sizeof(digits), "%02X", b);
                        hex += digits;
                    }
                    out[static_cast<uint32_t>(c)] = utf16(hex + ">");
                    if (++first.back() == 0 && first.size() >= 2)
                        ++first[first.size() - 2];
                }
            }
        }
        at = end;
    }
}

/// The font a page's text names, read from its dictionary.
PdfFont readFont(PdfFile& file, const std::string& dictionary)
{
    PdfFont font;
    const std::string subtype = valueOf(dictionary, "/Subtype");
    std::string descriptor = resolved(file, valueOf(dictionary, "/FontDescriptor"));
    std::string widthSource = dictionary;
    if (subtype == "/Type0")
    {
        font.twoByte = true;
        const std::vector<std::string> descendants = arrayItems(resolved(file, valueOf(dictionary, "/DescendantFonts")));
        if (!descendants.empty())
        {
            const std::string descendant = resolved(file, descendants[0]);
            descriptor = resolved(file, valueOf(descendant, "/FontDescriptor"));
            const std::string dw = valueOf(descendant, "/DW");
            font.missing = dw.empty() ? 1000.0 : std::atof(dw.c_str());
            // W: "c [w1 w2 ...]" or "first last w".
            const std::vector<std::string> w = arrayItems(resolved(file, valueOf(descendant, "/W")));
            // Counted in 64 bits so a range at the top of the codes ends,
            // and no more than a million widths in all.
            for (size_t k = 0; k + 1 < w.size() && font.widths.size() < (1u << 20);)
            {
                const uint32_t first = static_cast<uint32_t>(std::atol(w[k].c_str()));
                if (w[k + 1][0] == '[')
                {
                    const std::vector<double> list = numbersOf(w[k + 1]);
                    for (size_t g = 0; g < list.size(); ++g)
                        font.widths[first + static_cast<uint32_t>(g)] = list[g];
                    k += 2;
                }
                else if (k + 2 < w.size())
                {
                    const uint64_t last = std::min<uint64_t>(static_cast<uint32_t>(std::atol(w[k + 1].c_str())),
                        static_cast<uint64_t>(first) + 0xFFFF);
                    const double width = std::atof(w[k + 2].c_str());
                    for (uint64_t c = first; c <= last; ++c)
                        font.widths[static_cast<uint32_t>(c)] = width;
                    k += 3;
                }
                else
                    break;
            }
        }
    }
    else
    {
        // A simple font: its encoding (a base and its differences).
        const std::string encoding = resolved(file, valueOf(dictionary, "/Encoding"));
        std::string base = encoding.compare(0, 2, "<<") == 0 ? valueOf(encoding, "/BaseEncoding") : encoding;
        if (base.empty())
            base = subtype == "/TrueType" ? "/WinAnsiEncoding" : "/StandardEncoding";
        for (uint32_t c = 0x20; c < 0x7F; ++c)
            font.encoded[c] = std::string(1, static_cast<char>(c));
        if (base == "/StandardEncoding")
        {
            font.encoded[0x27] = glyphText("quoteright");
            font.encoded[0x60] = glyphText("quoteleft");
        }
        for (uint32_t c = 0x80; c < 0x100; ++c)
        {
            uint32_t u = 0;
            if (base == "/MacRomanEncoding")
                u = kMacRomanHigh[c - 0x80];
            else if (base == "/WinAnsiEncoding")
                u = c < 0xA0 ? kWinAnsiHigh[c - 0x80] : c;
            if (u)
                appendUtf8(font.encoded[c] = std::string(), u);
        }
        if (encoding.compare(0, 2, "<<") == 0)
        {
            uint32_t code = 0;
            for (const std::string& item : arrayItems(valueOf(encoding, "/Differences")))
            {
                if (!item.empty() && item[0] == '/')
                {
                    if (code < 256)
                        font.encoded[code] = glyphText(item.substr(1));
                    ++code;
                }
                else
                    code = static_cast<uint32_t>(std::atol(item.c_str()));
            }
        }
        const uint32_t first = static_cast<uint32_t>(std::atol(valueOf(dictionary, "/FirstChar").c_str()));
        const std::vector<double> widths = numbersOf(resolved(file, valueOf(dictionary, "/Widths")));
        for (size_t g = 0; g < widths.size(); ++g)
            font.widths[first + static_cast<uint32_t>(g)] = widths[g];
        // The standard fonts may leave out their widths: theirs, by what
        // each code reads as.
        if (widths.empty() && subtype != "/Type3")
        {
            const std::string baseFont = valueOf(dictionary, "/BaseFont");
            for (uint32_t c = 0x20; c < 0x100; ++c)
            {
                const std::string& text = font.encoded[c];
                if (text.empty())
                    continue;
                uint32_t u = static_cast<unsigned char>(text[0]);
                if (u >= 0xC0 && text.size() >= 2)
                {
                    const size_t n = u >= 0xF0 ? 4 : u >= 0xE0 ? 3 : 2;
                    u &= n == 4 ? 0x07 : n == 3 ? 0x0F : 0x1F;
                    for (size_t k = 1; k < n && k < text.size(); ++k)
                        u = (u << 6) | (static_cast<unsigned char>(text[k]) & 0x3F);
                }
                font.widths[c] = standardGlyphWidth(baseFont, u);
            }
        }
        if (subtype == "/Type3")
        {
            const std::vector<double> matrix = numbersOf(valueOf(dictionary, "/FontMatrix"));
            if (!matrix.empty() && matrix[0] != 0.0)
                font.unitsPerEm = 1.0 / std::fabs(matrix[0]);
        }
    }
    const int toUnicode = referenceOf(valueOf(dictionary, "/ToUnicode"));
    if (toUnicode >= 0)
    {
        std::string ignored;
        std::vector<uint8_t> data;
        bool isStream = false;
        if (file.object(toUnicode, ignored, data, isStream) && isStream)
            readToUnicode(std::string(data.begin(), data.end()), font.unicode);
    }
    const std::string missing = valueOf(descriptor, "/MissingWidth");
    if (!missing.empty() && !font.twoByte)
        font.missing = std::atof(missing.c_str());
    fontNameParts(valueOf(dictionary, "/BaseFont"), font.family, font.bold, font.italic);
    // The descriptor's family and style, where it gives them, are better
    // than what the name suggests.
    const std::string familyName = valueOf(descriptor, "/FontFamily");
    if (!familyName.empty() && (familyName[0] == '(' || familyName[0] == '<'))
    {
        const std::vector<uint8_t> bytes = stringBytes(familyName);
        if (!bytes.empty())
            font.family.assign(bytes.begin(), bytes.end());
    }
    const int flags = std::atoi(valueOf(descriptor, "/Flags").c_str());
    if (flags & 64)
        font.italic = true;
    if (std::atof(valueOf(descriptor, "/FontWeight").c_str()) >= 600)
        font.bold = true;
    return font;
}

struct PdfRun
{
    PdfFile& file;
    double pageLeft = 0.0;
    double pageTop = 0.0;
    Artwork& artwork;
    std::vector<std::string> notes;
    /// Clips from outside the content being run (a tiling pattern's tiles
    /// are cut to the shape they fill).
    std::vector<std::vector<Subpath>> outerClips;
    /// Tiles drawn so far, to keep a pattern from growing past reason.
    size_t tiles = 0;
    /// Forms drawn so far: a form can draw another many times over.
    size_t forms = 0;

    void note(const std::string& text)
    {
        if (std::find(notes.begin(), notes.end(), text) == notes.end())
            notes.push_back(text);
    }

    /// Page space to the stage: y downward from the page's top.
    Affine stage() const { return Affine{1.0, 0.0, 0.0, -1.0, -pageLeft, pageTop}; }
};

/// Runs content: its paths, as they are painted, with \a resources naming
/// what it draws and \a start its starting transform.
void runContent(const std::string& content, PdfRun& run, const std::string& resources, const Affine& start,
    int depth)
{
    Artwork& artwork = run.artwork;
    struct State
    {
        Affine ctm;
        Color fill{0, 0, 0, 255};
        Color stroke{0, 0, 0, 255};
        double fillAlpha = 1.0;
        double strokeAlpha = 1.0;
        double lineWidth = 1.0;
        std::optional<Gradient> fillGradient;
        std::vector<Subpath> clip;
        /// The color spaces cs and CS set (a resource's value, or a device
        /// space's name).
        std::string fillSpace;
        std::string strokeSpace;
        /// The clipping paths in force, each cutting what is painted.
        std::vector<std::vector<Subpath>> clips;
        /// A tiling pattern fill: its cell's content and resources, its
        /// space onto the page, and the step between tiles.
        struct Tiling
        {
            std::string content;
            std::string resources;
            Affine toPage;
            double xStep = 0, yStep = 0;
            double box[4] = {0, 0, 0, 0};
        };
        std::optional<Tiling> fillTiling;
    };
    std::vector<State> stack(1);
    stack.back().ctm = start;
    std::vector<double> operands;
    std::string lastName;
    std::vector<Subpath> path;
    Point current;
    Point startPoint;
    bool clipNext = false;
    const auto toStage = [&](double x, double y) {
        const Point p = stack.back().ctm.map(Point(x, y));
        return run.stage().map(p);
    };
    const auto resource = [&](const char* category, const std::string& name) {
        const std::string group = resolved(run.file, valueOf(resources, category));
        return valueOf(group, "/" + name);
    };
    const auto faded = [](Color color, double alpha) {
        color[3] = static_cast<uint8_t>(std::clamp(std::lround(color[3] * alpha), 0L, 255L));
        return color;
    };
    // Text: the last string read, a TJ array's strings and spacings, and
    // the text state (which q and Q do not save here, as files rarely
    // rely on it).
    struct ArrayItem
    {
        bool isString = false;
        double number = 0.0;
        std::vector<uint8_t> bytes;
    };
    std::vector<uint8_t> textString;
    std::vector<ArrayItem> textArray;
    int arrayDepth = 0;
    Affine textMatrix, lineMatrix;
    std::string fontName;
    double fontSize = 12.0, charSpacing = 0.0, wordSpacing = 0.0, horizontalScale = 1.0, leading = 0.0, rise = 0.0;
    int renderMode = 0;
    std::map<std::string, PdfFont> fonts;
    const auto currentFont = [&]() -> const PdfFont& {
        auto found = fonts.find(fontName);
        if (found == fonts.end())
            found = fonts.emplace(fontName, readFont(run.file, resolved(run.file, valueOf(resolved(run.file,
                                                                                 valueOf(resources, "/Font")),
                                                                         "/" + fontName))))
                        .first;
        return found->second;
    };
    // Shows a run of strings and spacings (thousandths of the size, back
    // along the line) as one line of text where the run starts.
    const auto showText = [&](const std::vector<ArrayItem>& items) {
        const PdfFont& font = currentFont();
        const Affine start = textMatrix;
        std::string text;
        for (const ArrayItem& item : items)
        {
            if (!item.isString)
            {
                // A wide gap reads as a space between words.
                if (item.number < -250.0 && !text.empty() && text.back() != ' ')
                    text += ' ';
                textMatrix = textMatrix.after(Affine{1, 0, 0, 1, -item.number / 1000.0 * fontSize * horizontalScale, 0});
                continue;
            }
            const size_t step = font.twoByte ? 2 : 1;
            for (size_t k = 0; k + step <= item.bytes.size(); k += step)
            {
                const uint32_t code = font.twoByte ? static_cast<uint32_t>(item.bytes[k] << 8 | item.bytes[k + 1])
                                                   : item.bytes[k];
                text += font.text(code);
                const double spacing = charSpacing + (step == 1 && code == 32 ? wordSpacing : 0.0);
                textMatrix = textMatrix.after(
                    Affine{1, 0, 0, 1, (font.width(code) * fontSize + spacing) * horizontalScale, 0});
            }
        }
        // Invisible text (searchable text over a scan) and text that only
        // clips are not drawn.
        if (renderMode == 3 || renderMode == 7 || text.find_first_not_of(' ') == std::string::npos)
            return;
        const Affine toPage = stack.back().ctm.after(start.after(Affine{1, 0, 0, 1, 0, rise}));
        Text item;
        item.text = text;
        item.family = font.family;
        item.bold = font.bold;
        item.italic = font.italic;
        item.size = std::max(1.0, std::fabs(fontSize) * toPage.scale());
        const bool stroked = renderMode == 1 || renderMode == 5;
        item.color = stroked ? faded(stack.back().stroke, stack.back().strokeAlpha)
                             : faded(stack.back().fill, stack.back().fillAlpha);
        // Text space (y down, a unit a pixel) to the stage, along the
        // line's direction on the page.
        const double length = std::hypot(toPage.a, toPage.b);
        const double ux = length > 0 ? toPage.a / length : 1.0;
        const double uy = length > 0 ? toPage.b / length : 0.0;
        const Point at = run.stage().map(toPage.map(Point(0, 0)));
        item.placement = Placement{ux, -uy, uy, ux, at.x, at.y};
        artwork.order.emplace_back(Artwork::Kind::Text, artwork.texts.size());
        artwork.texts.push_back(item);
    };
    const auto nextLine = [&](double tx, double ty) {
        lineMatrix = lineMatrix.after(Affine{1, 0, 0, 1, tx, ty});
        textMatrix = lineMatrix;
    };
    const auto paint = [&](bool fill, bool stroke, bool close) {
        if (close && !path.empty())
            path.back().closed = true;
        if (clipNext)
        {
            stack.back().clip = path;
            std::vector<Subpath> area = path;
            for (Subpath& s : area)
                s.closed = true;
            if (!area.empty())
                stack.back().clips.push_back(area);
            clipNext = false;
        }
        if (fill && stack.back().fillTiling && !path.empty())
        {
            // A tiling pattern: its cell drawn wherever the shape reaches,
            // each tile cut to the shape.
            const auto& tiling = *stack.back().fillTiling;
            std::vector<Subpath> area = path;
            for (Subpath& s : area)
                s.closed = true;
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
            // The shape's box in the pattern's space.
            const Affine toStage = run.stage().after(tiling.toPage);
            const double det = toStage.a * toStage.d - toStage.b * toStage.c;
            bool drawn = false;
            if (std::fabs(det) > 1e-12 && tiling.xStep != 0 && tiling.yStep != 0)
            {
                const Affine inverse{toStage.d / det, -toStage.b / det, -toStage.c / det, toStage.a / det,
                    (toStage.c * toStage.f - toStage.d * toStage.e) / det, (toStage.b * toStage.e - toStage.a * toStage.f) / det};
                double px0 = 1e300, py0 = 1e300, px1 = -1e300, py1 = -1e300;
                for (const Point& corner : {Point(left, top), Point(right, top), Point(left, bottom), Point(right, bottom)})
                {
                    const Point q = inverse.map(corner);
                    px0 = std::min(px0, q.x);
                    px1 = std::max(px1, q.x);
                    py0 = std::min(py0, q.y);
                    py1 = std::max(py1, q.y);
                }
                const double xs = std::fabs(tiling.xStep), ys = std::fabs(tiling.yStep);
                const long i0 = static_cast<long>(std::floor((px0 - tiling.box[2]) / xs));
                const long i1 = static_cast<long>(std::ceil((px1 - tiling.box[0]) / xs));
                const long j0 = static_cast<long>(std::floor((py0 - tiling.box[3]) / ys));
                const long j1 = static_cast<long>(std::ceil((py1 - tiling.box[1]) / ys));
                const long count = (i1 - i0 + 1) * (j1 - j0 + 1);
                if (count > 0 && count <= 2500 && run.tiles + static_cast<size_t>(count) <= 20000 && depth < 8)
                {
                    run.tiles += static_cast<size_t>(count);
                    const std::vector<std::vector<Subpath>> saved = run.outerClips;
                    for (const std::vector<Subpath>& clip : stack.back().clips)
                        run.outerClips.push_back(clip);
                    run.outerClips.push_back(area);
                    for (long i = i0; i <= i1; ++i)
                        for (long j = j0; j <= j1; ++j)
                            runContent(tiling.content, run, tiling.resources,
                                tiling.toPage.after(Affine{1, 0, 0, 1, i * xs, j * ys}), depth + 1);
                    run.outerClips = saved;
                    drawn = true;
                }
            }
            if (!drawn)
                run.note("Pattern fills too large to draw tile by tile came in as plain colors.");
            if (drawn)
            {
                if (stroke)
                {
                    Painted outline;
                    outline.subpaths = path;
                    outline.stroke = faded(stack.back().stroke, stack.back().strokeAlpha);
                    outline.strokeWidth = std::max(0.1, stack.back().lineWidth * stack.back().ctm.scale());
                    outline.clips = run.outerClips;
                    for (const std::vector<Subpath>& clip : stack.back().clips)
                        outline.clips.push_back(clip);
                    addPath(artwork, std::move(outline));
                }
                path.clear();
                return;
            }
        }
        if (!path.empty() && (fill || stroke))
        {
            Painted painted;
            painted.subpaths = path;
            painted.clips = run.outerClips;
            for (const std::vector<Subpath>& clip : stack.back().clips)
                painted.clips.push_back(clip);
            if (fill)
            {
                painted.fill = faded(stack.back().fill, stack.back().fillAlpha);
                if (stack.back().fillGradient)
                {
                    painted.gradient = stack.back().fillGradient;
                    for (Gradient::Stop& stop : painted.gradient->stops)
                        stop.color = faded(stop.color, stack.back().fillAlpha);
                    painted.fill = painted.gradient->stops.front().color;
                }
            }
            if (stroke)
            {
                painted.stroke = faded(stack.back().stroke, stack.back().strokeAlpha);
                painted.strokeWidth = std::max(0.1, stack.back().lineWidth * stack.back().ctm.scale());
            }
            addPath(artwork, std::move(painted));
        }
        path.clear();
    };

    size_t i = 0;
    const size_t n = content.size();
    while (i < n)
    {
        const char c = content[i];
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            ++i;
            continue;
        }
        if (c == '%')
        {
            while (i < n && content[i] != '\n' && content[i] != '\r')
                ++i;
            continue;
        }
        if (c == '(')
        {
            // A string, with its nesting and escapes: kept for the text
            // operators.
            const size_t begin = i;
            int nesting = 0;
            for (; i < n; ++i)
            {
                if (content[i] == '\\')
                {
                    ++i;
                    continue;
                }
                if (content[i] == '(')
                    ++nesting;
                else if (content[i] == ')' && --nesting == 0)
                {
                    ++i;
                    break;
                }
            }
            textString = stringBytes(content.substr(begin, i - begin));
            if (arrayDepth == 1)
                textArray.push_back({true, 0.0, textString});
            else
                operands.clear();
            continue;
        }
        if (c == '<' && i + 1 < n && content[i + 1] == '<')
        {
            const size_t close = content.find(">>", i);
            i = close == std::string::npos ? n : close + 2;
            continue;
        }
        if (c == '<')
        {
            const size_t close = content.find('>', i);
            const size_t end = close == std::string::npos ? n : close + 1;
            textString = stringBytes(content.substr(i, end - i));
            i = end;
            if (arrayDepth == 1)
                textArray.push_back({true, 0.0, textString});
            else
                operands.clear();
            continue;
        }
        if (c == '[')
        {
            if (++arrayDepth == 1)
                textArray.clear();
            ++i;
            continue;
        }
        if (c == ']')
        {
            arrayDepth = std::max(0, arrayDepth - 1);
            ++i;
            continue;
        }
        if (c == '{' || c == '}' || c == '>')
        {
            ++i;
            continue;
        }
        if (c == '/')
        {
            const size_t begin = ++i;
            while (i < n && !std::isspace(static_cast<unsigned char>(content[i])) && std::strchr("/[]()<>{}%", content[i]) == nullptr)
                ++i;
            lastName = content.substr(begin, i - begin);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.')
        {
            Scanner scan{content.data() + i, content.data() + n};
            double value = 0;
            if (scan.number(value))
            {
                operands.push_back(value);
                if (arrayDepth == 1)
                    textArray.push_back({false, value, {}});
                i = static_cast<size_t>(scan.at - content.data());
                continue;
            }
            ++i;
            continue;
        }
        // An operator.
        size_t end = i;
        while (end < n && !std::isspace(static_cast<unsigned char>(content[end])) && std::strchr("/[]()<>{}%", content[end]) == nullptr)
            ++end;
        const std::string op = content.substr(i, end - i);
        i = end;
        const auto has = [&operands](size_t count) { return operands.size() >= count; };
        const auto arg = [&operands](size_t back) { return operands[operands.size() - back]; };
        State& state = stack.back();
        if (op == "q")
            stack.push_back(stack.back());
        else if (op == "Q")
        {
            if (stack.size() > 1)
                stack.pop_back();
        }
        else if (op == "cm" && has(6))
            state.ctm = state.ctm.after(Affine{arg(6), arg(5), arg(4), arg(3), arg(2), arg(1)});
        else if (op == "w" && has(1))
            state.lineWidth = arg(1);
        else if (op == "m" && has(2))
        {
            current = Point(arg(2), arg(1));
            startPoint = current;
            path.push_back(Subpath());
            path.back().segments.push_back({Segment::Kind::Move, {toStage(current.x, current.y)}});
        }
        else if (op == "l" && has(2) && !path.empty())
        {
            current = Point(arg(2), arg(1));
            path.back().segments.push_back({Segment::Kind::Line, {toStage(current.x, current.y)}});
        }
        else if ((op == "c" && has(6)) || ((op == "v" || op == "y") && has(4)))
        {
            if (!path.empty())
            {
                Point c1, c2, to;
                if (op == "c")
                {
                    c1 = Point(arg(6), arg(5));
                    c2 = Point(arg(4), arg(3));
                    to = Point(arg(2), arg(1));
                }
                else if (op == "v")
                {
                    c1 = current;
                    c2 = Point(arg(4), arg(3));
                    to = Point(arg(2), arg(1));
                }
                else
                {
                    c1 = Point(arg(4), arg(3));
                    to = Point(arg(2), arg(1));
                    c2 = to;
                }
                path.back().segments.push_back({Segment::Kind::Cubic, {toStage(c1.x, c1.y), toStage(c2.x, c2.y), toStage(to.x, to.y)}});
                current = to;
            }
        }
        else if (op == "re" && has(4))
        {
            const double x = arg(4), y = arg(3), w = arg(2), h = arg(1);
            Subpath rect;
            rect.closed = true;
            rect.segments = {{Segment::Kind::Move, {toStage(x, y)}}, {Segment::Kind::Line, {toStage(x + w, y)}},
                {Segment::Kind::Line, {toStage(x + w, y + h)}}, {Segment::Kind::Line, {toStage(x, y + h)}},
                {Segment::Kind::Line, {toStage(x, y)}}};
            path.push_back(rect);
            current = Point(x, y);
            startPoint = current;
        }
        else if (op == "h")
        {
            if (!path.empty())
                path.back().closed = true;
            current = startPoint;
        }
        else if (op == "W" || op == "W*")
            clipNext = true;
        else if (op == "f" || op == "F" || op == "f*")
            paint(true, false, true);
        else if (op == "S")
            paint(false, true, false);
        else if (op == "s")
            paint(false, true, true);
        else if (op == "B" || op == "B*")
            paint(true, true, false);
        else if (op == "b" || op == "b*")
            paint(true, true, true);
        else if (op == "n")
            paint(false, false, false);
        else if (op == "scn" && operands.empty() && !lastName.empty())
        {
            // A pattern: a shading's gradient, placed by the pattern's own
            // matrix in the page's space.
            const std::string pattern = resolved(run.file, resource("/Pattern", lastName));
            const std::vector<double> matrix = numbersOf(valueOf(pattern, "/Matrix"));
            Affine placed;
            if (matrix.size() >= 6)
                placed = Affine{matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]};
            state.fillGradient.reset();
            state.fillTiling.reset();
            const int patternType = std::atoi(valueOf(pattern, "/PatternType").c_str());
            if (patternType == 2)
                state.fillGradient = shadingGradient(run.file, valueOf(pattern, "/Shading"), run.stage().after(start.after(placed)));
            if (state.fillGradient)
                state.fill = state.fillGradient->stops.front().color;
            else if (patternType == 1)
            {
                // A tiling pattern: its cell is a stream of content.
                const int number = referenceOf(resource("/Pattern", lastName));
                std::string dictionary;
                std::vector<uint8_t> data;
                bool isStream = false;
                if (number >= 0 && run.file.object(number, dictionary, data, isStream) && isStream)
                {
                    State::Tiling tiling;
                    tiling.content.assign(data.begin(), data.end());
                    tiling.resources = resolved(run.file, valueOf(dictionary, "/Resources"));
                    tiling.toPage = start.after(placed);
                    tiling.xStep = std::atof(valueOf(dictionary, "/XStep").c_str());
                    tiling.yStep = std::atof(valueOf(dictionary, "/YStep").c_str());
                    const std::vector<double> box = numbersOf(valueOf(dictionary, "/BBox"));
                    for (size_t k = 0; k < 4 && k < box.size(); ++k)
                        tiling.box[k] = box[k];
                    state.fillTiling = tiling;
                }
            }
            else
                run.note("Pattern fills other than gradients and tiles came in as plain colors.");
        }
        else if (op == "cs" || op == "CS")
        {
            const std::string space = lastName == "DeviceGray" || lastName == "DeviceRGB" || lastName == "DeviceCMYK" ||
                    lastName == "Pattern"
                ? "/" + lastName
                : resource("/ColorSpace", lastName);
            (op == "cs" ? state.fillSpace : state.strokeSpace) = space;
        }
        else if (op == "sc" || op == "scn")
        {
            state.fill = colorIn(run.file, state.fillSpace, operands);
            state.fillGradient.reset();
            state.fillTiling.reset();
        }
        else if (op == "SC" || op == "SCN")
            state.stroke = colorIn(run.file, state.strokeSpace, operands);
        else if (op == "g" || op == "rg" || op == "k")
        {
            state.fill = colorOf(operands);
            state.fillGradient.reset();
            state.fillTiling.reset();
        }
        else if (op == "G" || op == "RG" || op == "K")
            state.stroke = colorOf(operands);
        else if (op == "gs" && !lastName.empty())
        {
            const std::string graphics = resolved(run.file, resource("/ExtGState", lastName));
            const std::string fillAlpha = valueOf(graphics, "/ca");
            const std::string strokeAlpha = valueOf(graphics, "/CA");
            if (!fillAlpha.empty())
                state.fillAlpha = std::clamp(std::atof(fillAlpha.c_str()), 0.0, 1.0);
            if (!strokeAlpha.empty())
                state.strokeAlpha = std::clamp(std::atof(strokeAlpha.c_str()), 0.0, 1.0);
        }
        else if (op == "sh" && !lastName.empty())
        {
            // A shading painted over the clip: the clip's outline with the
            // gradient as its fill.
            std::optional<Gradient> gradient =
                shadingGradient(run.file, resource("/Shading", lastName), run.stage().after(state.ctm));
            if (gradient && !state.clip.empty())
            {
                Painted painted;
                painted.subpaths = state.clip;
                for (Gradient::Stop& stop : gradient->stops)
                    stop.color = faded(stop.color, state.fillAlpha);
                painted.fill = gradient->stops.front().color;
                painted.gradient = gradient;
                addPath(artwork, std::move(painted));
            }
            else if (!gradient)
                run.note("Some shadings are of kinds Phoenix does not read and were left out.");
        }
        else if (op == "BT")
        {
            textMatrix = Affine();
            lineMatrix = Affine();
        }
        else if (op == "Tf" && has(1))
        {
            fontName = lastName;
            fontSize = arg(1);
        }
        else if (op == "Tc" && has(1))
            charSpacing = arg(1);
        else if (op == "Tw" && has(1))
            wordSpacing = arg(1);
        else if (op == "Tz" && has(1))
            horizontalScale = arg(1) / 100.0;
        else if (op == "TL" && has(1))
            leading = arg(1);
        else if (op == "Ts" && has(1))
            rise = arg(1);
        else if (op == "Tr" && has(1))
            renderMode = static_cast<int>(arg(1));
        else if (op == "Td" && has(2))
            nextLine(arg(2), arg(1));
        else if (op == "TD" && has(2))
        {
            leading = -arg(1);
            nextLine(arg(2), arg(1));
        }
        else if (op == "Tm" && has(6))
        {
            lineMatrix = Affine{arg(6), arg(5), arg(4), arg(3), arg(2), arg(1)};
            textMatrix = lineMatrix;
        }
        else if (op == "T*")
            nextLine(0, -leading);
        else if (op == "Tj" || op == "'" || op == "\"")
        {
            if (op == "\"" && has(2))
            {
                wordSpacing = arg(2);
                charSpacing = arg(1);
            }
            if (op != "Tj")
                nextLine(0, -leading);
            showText({ArrayItem{true, 0.0, textString}});
        }
        else if (op == "TJ")
            showText(textArray);
        else if (op == "BI")
        {
            // An inline image: skipped to its end.
            const size_t ei = content.find("EI", i);
            i = ei == std::string::npos ? n : ei + 2;
            run.note("Images written into the page's content were left out.");
        }
        else if (op == "Do" && !lastName.empty())
        {
            const std::string reference = resource("/XObject", lastName);
            const int number = referenceOf(reference);
            std::string dictionary;
            std::vector<uint8_t> data;
            bool isStream = false;
            if (run.forms >= 20000)
                run.note("Forms drawn past 20000 times were left out.");
            else if (number >= 0 && run.file.object(number, dictionary, data, isStream) && isStream)
            {
                const std::string subtype = valueOf(dictionary, "/Subtype");
                if (subtype == "/Form" && depth < 12)
                {
                    ++run.forms;
                    // A group or placed art: its content, in its own space.
                    const std::vector<double> matrix = numbersOf(valueOf(dictionary, "/Matrix"));
                    Affine inside = state.ctm;
                    if (matrix.size() >= 6)
                        inside = inside.after(Affine{matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]});
                    std::string own = resolved(run.file, valueOf(dictionary, "/Resources"));
                    if (own.empty())
                        own = resources;
                    runContent(std::string(data.begin(), data.end()), run, own, inside, depth + 1);
                }
                else if (subtype == "/Image")
                {
                    Image image;
                    if (pdfImage(run.file, dictionary, data, image))
                    {
                        // The unit square, its top row first, onto the page.
                        image.placement = toPlacement(run.stage().after(state.ctm.after(Affine{1.0, 0.0, 0.0, -1.0, 0.0, 1.0})));
                        artwork.order.emplace_back(Artwork::Kind::Image, artwork.images.size());
                        artwork.images.push_back(std::move(image));
                    }
                    else
                        run.note("Some images are of kinds Phoenix does not read and were left out.");
                }
            }
        }
        operands.clear();
        lastName.clear();
    }
}

} // namespace

std::string glyphNameText(const std::string& name)
{
    return glyphText(name);
}

std::string fromMacRoman(const std::string& text)
{
    std::string out;
    for (const unsigned char c : text)
    {
        if (c < 0x80)
            out += static_cast<char>(c);
        else
            appendUtf8(out, kMacRomanHigh[c - 0x80]);
    }
    return out;
}

void splitFontName(const std::string& name, std::string& family, bool& bold, bool& italic)
{
    fontNameParts(name, family, bold, italic);
}

double standardGlyphWidth(const std::string& fontName, uint32_t unicode)
{
    // The printable ASCII, from each font's metrics (the standard
    // encoding's quoteright and quoteleft at 0x27 and 0x60).
    static const short helvetica[95] = {278, 278, 355, 556, 556, 889, 667, 222, 333, 333, 389, 584, 278, 333, 278,
        278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722,
        667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611,
        278, 278, 278, 469, 556, 222, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556,
        556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
    static const short helveticaBold[95] = {278, 333, 474, 556, 556, 889, 722, 278, 333, 333, 389, 584, 278, 333, 278,
        278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722,
        667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611,
        333, 278, 333, 584, 556, 278, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611, 611,
        611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};
    static const short times[95] = {250, 333, 408, 500, 500, 833, 778, 333, 333, 333, 500, 564, 250, 333, 250, 278,
        500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444, 921, 722, 667, 667, 722, 611,
        556, 722, 722, 333, 389, 722, 611, 889, 722, 722, 556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333,
        278, 333, 469, 500, 333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500, 500, 500,
        333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541};
    static const short timesBold[95] = {250, 333, 555, 500, 500, 1000, 833, 333, 333, 333, 500, 570, 250, 333, 250,
        278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570, 570, 500, 930, 722, 667, 722, 722,
        667, 611, 778, 778, 389, 500, 778, 667, 944, 722, 778, 611, 778, 722, 556, 667, 722, 722, 1000, 722, 722, 667,
        333, 278, 333, 581, 500, 333, 500, 556, 444, 556, 444, 333, 500, 556, 278, 333, 556, 278, 833, 556, 500, 556,
        556, 444, 389, 333, 556, 500, 722, 500, 500, 444, 394, 220, 394, 520};
    static const short timesItalic[95] = {250, 333, 420, 500, 500, 833, 778, 333, 333, 333, 500, 675, 250, 333, 250,
        278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 675, 675, 675, 500, 920, 611, 611, 667, 722,
        611, 611, 722, 722, 333, 444, 667, 556, 833, 667, 722, 611, 722, 611, 500, 556, 722, 611, 833, 611, 556, 556,
        389, 278, 389, 422, 500, 333, 500, 500, 444, 500, 444, 278, 500, 500, 278, 278, 444, 278, 722, 500, 500, 500,
        500, 389, 389, 278, 500, 444, 667, 444, 444, 389, 400, 275, 400, 541};
    static const short timesBoldItalic[95] = {250, 389, 555, 500, 500, 833, 778, 333, 333, 333, 500, 570, 250, 333,
        250, 278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570, 570, 500, 832, 667, 667, 667,
        722, 667, 667, 722, 778, 389, 500, 667, 611, 889, 722, 722, 611, 722, 667, 556, 611, 722, 667, 889, 667, 611,
        611, 333, 278, 333, 570, 500, 333, 500, 500, 444, 500, 444, 333, 500, 556, 278, 278, 500, 278, 778, 556, 500,
        500, 500, 389, 389, 278, 556, 444, 667, 500, 444, 389, 348, 220, 348, 570};
    const auto has = [&fontName](const char* part) { return fontName.find(part) != std::string::npos; };
    if (has("Courier") || has("Mono"))
        return 600.0;
    const bool bold = has("Bold") || has("Black") || has("Heavy");
    const bool italic = has("Italic") || has("Oblique");
    const bool serif = has("Times") || has("Roman") || has("Serif") || has("Georgia") || has("Garamond") ||
        has("Palatino") || has("Bookman") || has("Century");
    const short* table = serif ? (bold ? (italic ? timesBoldItalic : timesBold) : italic ? timesItalic : times)
                               : (bold ? helveticaBold : helvetica);
    const auto ascii = [table](char c) { return static_cast<double>(table[static_cast<unsigned char>(c) - 32]); };
    if (unicode >= 32 && unicode <= 126)
        return ascii(static_cast<char>(unicode));
    // Latin-1: accented letters as wide as their base letters, the rest
    // like what they resemble.
    if (unicode >= 0xC0 && unicode <= 0xFF)
    {
        static const char base[] = "AAAAAA?CEEEEIIIIDNOOOOO?OUUUUYP?aaaaaa?ceeeeiiiionooooo?ouuuuypy";
        const char b = base[unicode - 0xC0];
        if (b != '?')
            return ascii(b);
        if (unicode == 0xC6)
            return serif ? (bold ? 1000.0 : 889.0) : 1000.0;
        if (unicode == 0xE6)
            return serif ? (bold ? 722.0 : 667.0) : 889.0;
        if (unicode == 0xDF)
            return serif ? (bold && !italic ? 556.0 : 500.0) : 611.0;
        return ascii('+');
    }
    switch (unicode)
    {
    case 0xA0: return ascii(' ');
    case 0xA1: return ascii('!');
    case 0xA2:
    case 0xA3:
    case 0xA4:
    case 0xA5:
    case 0xA7:
    case 0xB6:
    case 0xAB:
    case 0xBB:
    case 0x2020:
    case 0x2021: return ascii('0');
    case 0xA6: return ascii('|');
    case 0xA9:
    case 0xAE: return serif ? (bold ? 747.0 : 760.0) : 737.0;
    case 0xAA:
    case 0xBA: return serif ? 300.0 : 370.0;
    case 0xAC:
    case 0xB1: return ascii('+');
    case 0xAD: return ascii('-');
    case 0xB0: return 400.0;
    case 0xB2:
    case 0xB3:
    case 0xB9: return serif ? 300.0 : 333.0;
    case 0xB5: return ascii('u');
    case 0xB7: return ascii('.');
    case 0xBC:
    case 0xBD:
    case 0xBE: return serif ? 750.0 : 834.0;
    case 0xBF: return ascii('?');
    case 0x2013: return serif ? 500.0 : 556.0;
    case 0x2014:
    case 0x2026:
    case 0x2030: return 1000.0;
    case 0x2018:
    case 0x2019:
    case 0x201A: return ascii('\'');
    case 0x201C:
    case 0x201D:
    case 0x201E: return serif ? (bold ? 500.0 : 444.0) : (bold ? 500.0 : 333.0);
    case 0x2022: return 350.0;
    case 0x2122: return serif ? (bold ? 1000.0 : 980.0) : 1000.0;
    case 0x2212: return ascii('+');
    default: break;
    }
    if ((unicode >= 0xA8 && unicode <= 0xAF) || (unicode >= 0x2C6 && unicode <= 0x2DD))
        return 333.0;
    return serif ? 500.0 : 556.0;
}

std::vector<uint8_t> fromBase64(const std::string& text)
{
    std::vector<uint8_t> out;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text)
    {
        int value = -1;
        if (c >= 'A' && c <= 'Z')
            value = c - 'A';
        else if (c >= 'a' && c <= 'z')
            value = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            value = c - '0' + 52;
        else if (c == '+' || c == '-')
            value = 62;
        else if (c == '/' || c == '_')
            value = 63;
        else if (c == '=')
            break;
        if (value < 0)
            continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(buffer >> bits));
        }
    }
    return out;
}

std::vector<Segment> arcToCubics(const Point& from, double rx, double ry, double rotation, bool large, bool sweep,
    const Point& to)
{
    std::vector<Segment> segments;
    addArcCubics(segments, from, rx, ry, rotation, large, sweep, to);
    return segments;
}

Artwork readSvg(const std::string& text)
{
    Artwork artwork;
    tinyxml2::XMLDocument document;
    if (document.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS || !document.RootElement())
    {
        artwork.notes.push_back("The file is not an SVG document.");
        return artwork;
    }
    const tinyxml2::XMLElement& root = *document.RootElement();
    // The canvas, and the viewBox mapped onto it.
    Affine view;
    double width = length(root.Attribute("width"), 0.0);
    double height = length(root.Attribute("height"), 0.0);
    if (const char* box = root.Attribute("viewBox"))
    {
        const std::vector<Point> corners = pointsOf(box);
        if (corners.size() >= 2 && corners[1].x > 0.0 && corners[1].y > 0.0)
        {
            if (width <= 0.0)
                width = corners[1].x;
            if (height <= 0.0)
                height = corners[1].y;
            const double scale = std::min(width / corners[1].x, height / corners[1].y);
            view = Affine{scale, 0.0, 0.0, scale, -corners[0].x * scale, -corners[0].y * scale};
        }
    }
    artwork.width = width;
    artwork.height = height;

    // Elements by id, and the style sheets' rules.
    SvgDefs defs;
    std::function<void(const tinyxml2::XMLElement&)> gather = [&](const tinyxml2::XMLElement& element) {
        if (const char* id = element.Attribute("id"))
            defs.ids[id] = &element;
        if (localName(element) == "style")
        {
            const std::string sheet = textOf(element);
            size_t at = 0;
            while (at < sheet.size())
            {
                const size_t open = sheet.find('{', at);
                const size_t close = open == std::string::npos ? std::string::npos : sheet.find('}', open);
                if (close == std::string::npos)
                    break;
                const std::string body = sheet.substr(open + 1, close - open - 1);
                std::string selectors = sheet.substr(at, open - at);
                size_t start = 0;
                while (start <= selectors.size())
                {
                    size_t comma = selectors.find(',', start);
                    if (comma == std::string::npos)
                        comma = selectors.size();
                    const std::string selector = trimmed(selectors.substr(start, comma - start));
                    if (selector.size() > 1 && selector[0] == '.' && selector.find_first_of(" >:[") == std::string::npos)
                        defs.classRules[selector.substr(1)] += body + ";";
                    else if (!selector.empty() && std::isalpha(static_cast<unsigned char>(selector[0])) &&
                        selector.find_first_of(" >:[.#") == std::string::npos)
                        defs.typeRules[selector] += body + ";";
                    start = comma + 1;
                }
                at = close + 1;
            }
        }
        for (const tinyxml2::XMLElement* child = element.FirstChildElement(); child; child = child->NextSiblingElement())
            gather(*child);
    };
    gather(root);
    gDefs = &defs;
    gSvgWalked = 0;
    gSvgUsing.clear();

    SvgStyle style;
    for (const tinyxml2::XMLElement* child = root.FirstChildElement(); child; child = child->NextSiblingElement())
    {
        SvgStyle rootStyle = style;
        applyStyle(root, rootStyle);
        walkSvg(*child, rootStyle, view, artwork);
    }
    gDefs = nullptr;
    return artwork;
}

Artwork readPdf(const std::vector<uint8_t>& bytes)
{
    Artwork artwork;
    PdfFile pdf;
    std::string error;
    if (!pdf.file.open(bytes, error))
    {
        // An Illustrator file older than version 9 is PostScript, not PDF.
        if (looksLikePostScript(bytes))
            return readPostScriptArt(bytes);
        artwork.notes.push_back(error == "This is not a PDF file."
                ? std::string("The file is neither PDF nor PostScript (nor an Illustrator file).")
                : error);
        return artwork;
    }
    const std::vector<pdf::Object> pages = pdf.file.pages();
    if (pages.empty())
    {
        artwork.notes.push_back("No page was found in the file.");
        return artwork;
    }
    const std::string page = pdf::File::text(pages.front());
    // The page's box: what its top and left are.
    double left = 0.0, bottom = 0.0, right = 612.0, top = 792.0;
    for (const char* box : {"/ArtBox", "/TrimBox", "/CropBox", "/MediaBox"})
    {
        const size_t at = page.find(box);
        if (at == std::string::npos)
            continue;
        const size_t open = page.find('[', at);
        const size_t close = page.find(']', open);
        if (open == std::string::npos || close == std::string::npos)
            continue;
        const std::string inside = page.substr(open + 1, close - open - 1);
        Scanner scan{inside.data(), inside.data() + inside.size()};
        double v[4];
        if (scan.number(v[0]) && scan.number(v[1]) && scan.number(v[2]) && scan.number(v[3]))
        {
            left = std::min(v[0], v[2]);
            right = std::max(v[0], v[2]);
            bottom = std::min(v[1], v[3]);
            top = std::max(v[1], v[3]);
        }
        if (std::string(box) == "/MediaBox" || std::string(box) == "/CropBox")
            break;
        break;
    }
    artwork.width = right - left;
    artwork.height = top - bottom;
    std::string content;
    for (int ref : contentRefs(page))
    {
        std::string dictionary;
        std::vector<uint8_t> data;
        bool isStream = false;
        if (pdf.object(ref, dictionary, data, isStream) && isStream)
            content.append(data.begin(), data.end()).push_back('\n');
    }
    PdfRun run{pdf, left, top, artwork, {}};
    if (!content.empty())
        runContent(content, run, resolved(pdf, valueOf(page, "/Resources")), Affine(), 0);
    // An Illustrator file saved without PDF content draws only a note on
    // its page; its art is in the private data Illustrator keeps beside it.
    if (artwork.paths.empty() && artwork.images.empty())
    {
        std::vector<uint8_t> privateData = illustratorPrivateData(pdf.file, pages.front());
        if (!privateData.empty())
        {
            Artwork own = readIllustratorPrivateData(privateData);
            if (!own.paths.empty() || !own.images.empty() || !own.texts.empty())
                return own;
            artwork.notes.insert(artwork.notes.end(), own.notes.begin(), own.notes.end());
        }
    }
    if (content.empty())
        artwork.notes.push_back("The page's content could not be read.");
    artwork.notes.insert(artwork.notes.end(), run.notes.begin(), run.notes.end());
    return artwork;
}

Shape* toShape(const Painted& painted)
{
    if (painted.subpaths.empty() || (!painted.fill && !painted.stroke))
        return nullptr;
    Shape* shape = new Shape(nullptr);
    shape->isDrawingObject = true;
    int fillIndex = -1;
    int strokeIndex = -1;
    if (painted.gradient && painted.gradient->stops.size() >= 2)
    {
        const Gradient& gradient = *painted.gradient;
        FillStyle* fill = nullptr;
        if (gradient.radial)
        {
            RadialGradient* radial = new RadialGradient(shape);
            for (const Gradient::Stop& stop : gradient.stops)
                radial->entries.push_back({stop.offset, {stop.color[0], stop.color[1], stop.color[2], stop.color[3]}});
            radial->focalPointRatio = gradient.focal;
            fill = radial;
        }
        else
        {
            LinearGradient* linear = new LinearGradient(shape);
            for (const Gradient::Stop& stop : gradient.stops)
                linear->entries.push_back({stop.offset, {stop.color[0], stop.color[1], stop.color[2], stop.color[3]}});
            fill = linear;
        }
        const Placement& p = gradient.placement;
        fill->transform = Transform(p.a, p.b, p.c, p.d, p.tx, p.ty);
        if (gradient.spread == Gradient::Spread::Reflect)
            fill->spreadMethod = "reflect";
        else if (gradient.spread == Gradient::Spread::Repeat)
            fill->spreadMethod = "repeat";
        shape->fills.push_back(fill);
        shape->fillsMap[1] = fill;
        fillIndex = 1;
    }
    else if (painted.fill)
    {
        SolidColor* fill = new SolidColor(shape);
        for (int i = 0; i < 4; ++i)
            fill->color[i] = (*painted.fill)[static_cast<size_t>(i)];
        shape->fills.push_back(fill);
        shape->fillsMap[1] = fill;
        fillIndex = 1;
    }
    if (painted.stroke)
    {
        SolidStroke* stroke = new SolidStroke(shape);
        stroke->weight = std::max(0.1, painted.strokeWidth);
        SolidColor* color = new SolidColor(stroke);
        for (int i = 0; i < 4; ++i)
            color->color[i] = (*painted.stroke)[static_cast<size_t>(i)];
        stroke->fill = color;
        shape->strokes.push_back(stroke);
        shape->strokesMap[1] = stroke;
        strokeIndex = 1;
    }

    // Which subpaths are holes: those inside an odd number of the others.
    std::vector<std::vector<Point>> rings;
    for (const Subpath& subpath : painted.subpaths)
        rings.push_back(flatten(subpath));

    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
    for (size_t s = 0; s < painted.subpaths.size(); ++s)
    {
        const Subpath& subpath = painted.subpaths[s];
        if (subpath.segments.size() < 2)
            continue;
        bool hole = false;
        if (!rings[s].empty())
        {
            int depth = 0;
            for (size_t other = 0; other < rings.size(); ++other)
                if (other != s && rings[other].size() >= 3 && inside(rings[other], rings[s].front()))
                    ++depth;
            hole = depth % 2 == 1;
        }
        const bool fillable = fillIndex >= 0 && rings[s].size() >= 2;
        const double area = signedArea(rings[s]);
        // The fill on the inside of an outline, on the outside of a hole.
        const bool onLeft = (area >= 0.0) != hole;

        Edge* edge = new Edge(shape);
        edge->strokeStyle = strokeIndex;
        if (fillable)
        {
            if (onLeft)
                edge->fillStyle1 = fillIndex;
            else
                edge->fillStyle0 = fillIndex;
        }
        Path* path = new Path(edge);
        Point first;
        Point last;
        for (const Segment& segment : subpath.segments)
        {
            if (segment.points.empty())
                continue;
            PathSegment::Command command = segment.kind == Segment::Kind::Move ? PathSegment::Command::Move
                : segment.kind == Segment::Kind::Cubic                         ? PathSegment::Command::Cubic
                                                                               : PathSegment::Command::Line;
            if (segment.kind == Segment::Kind::Move)
                first = segment.points.front();
            path->segments.push_back(new PathSegment(command, segment.points, path));
            for (const Point& point : segment.points)
            {
                left = std::min(left, point.x);
                top = std::min(top, point.y);
                right = std::max(right, point.x);
                bottom = std::max(bottom, point.y);
            }
            last = segment.points.back();
        }
        const bool open = std::hypot(last.x - first.x, last.y - first.y) > 1.0e-6;
        if (subpath.closed && open)
            path->segments.push_back(new PathSegment(PathSegment::Command::Line, {first}, path));
        edge->paths.push_back(path);
        shape->edges.push_back(edge);

        // A filled path left open is closed for its fill, with no stroke
        // along the closing line.
        if (fillable && !subpath.closed && open && rings[s].size() >= 2)
        {
            Edge* closing = new Edge(shape);
            if (onLeft)
                closing->fillStyle1 = fillIndex;
            else
                closing->fillStyle0 = fillIndex;
            Path* line = new Path(closing);
            line->segments.push_back(new PathSegment(PathSegment::Command::Move, {last}, line));
            line->segments.push_back(new PathSegment(PathSegment::Command::Line, {first}, line));
            closing->paths.push_back(line);
            shape->edges.push_back(closing);
        }
    }
    if (shape->edges.empty())
    {
        delete shape;
        return nullptr;
    }
    shape->localBounds = Rect(Point(left, top), Point(right, bottom));
    shape->bounds = shape->localBounds;
    return shape;
}

} // namespace vectors

} // namespace fla
