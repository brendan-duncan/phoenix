#include "vector_import.h"

#include "../data/edge.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"

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

void transform(Painted& painted, const Affine& m)
{
    for (Subpath& subpath : painted.subpaths)
        for (Segment& segment : subpath.segments)
            for (Point& point : segment.points)
                point = m.map(point);
    painted.strokeWidth *= m.scale();
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
    std::optional<Color> color;
    bool hidden = false;
};

void applyStyle(const tinyxml2::XMLElement& element, SvgStyle& style)
{
    // Attributes, then the style attribute's declarations over them.
    std::vector<std::pair<std::string, std::string>> declarations;
    for (const char* name : {"color", "fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity", "display", "visibility"})
        if (const char* value = element.Attribute(name))
            declarations.emplace_back(name, value);
    if (const char* css = element.Attribute("style"))
    {
        std::string text = css;
        size_t at = 0;
        while (at < text.size())
        {
            size_t semi = text.find(';', at);
            if (semi == std::string::npos)
                semi = text.size();
            const std::string declaration = text.substr(at, semi - at);
            const size_t colon = declaration.find(':');
            if (colon != std::string::npos)
            {
                std::string key = declaration.substr(0, colon);
                std::string value = declaration.substr(colon + 1);
                const auto trim = [](std::string& s) {
                    s.erase(0, s.find_first_not_of(" \t\r\n"));
                    s.erase(s.find_last_not_of(" \t\r\n") + 1);
                };
                trim(key);
                trim(value);
                declarations.emplace_back(key, value);
            }
            at = semi + 1;
        }
    }
    for (const auto& [key, value] : declarations)
    {
        if (key == "color")
            style.color = parseColor(value, style.color);
        else if (key == "fill")
            style.fill = parseColor(value, style.color);
        else if (key == "stroke")
            style.stroke = parseColor(value, style.color);
        else if (key == "stroke-width")
            style.strokeWidth = length(value.c_str(), style.strokeWidth);
        else if (key == "opacity")
            style.opacity *= std::atof(value.c_str());
        else if (key == "fill-opacity")
            style.fillOpacity = std::atof(value.c_str());
        else if (key == "stroke-opacity")
            style.strokeOpacity = std::atof(value.c_str());
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

void walkSvg(const tinyxml2::XMLElement& element, SvgStyle style, const Affine& outer, Artwork& artwork)
{
    std::string name = element.Name();
    const size_t colon = name.find(':');
    if (colon != std::string::npos)
        name = name.substr(colon + 1);
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
        const std::string note = "Text was left out; convert it to outlines first.";
        if (std::find(artwork.notes.begin(), artwork.notes.end(), note) == artwork.notes.end())
            artwork.notes.push_back(note);
        return;
    }
    else if (name == "image")
    {
        const std::string note = "Embedded images were left out.";
        if (std::find(artwork.notes.begin(), artwork.notes.end(), note) == artwork.notes.end())
            artwork.notes.push_back(note);
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
        transform(painted, m);
        if (painted.fill || painted.stroke)
            artwork.paths.push_back(std::move(painted));
    }

    for (const tinyxml2::XMLElement* child = element.FirstChildElement(); child; child = child->NextSiblingElement())
        walkSvg(*child, style, m, artwork);
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

struct PdfStream
{
    std::string dictionary;
    std::vector<uint8_t> data;
};

/// The object \a number's dictionary and stream, if it is one.
bool findObject(const std::string& file, int number, std::string& dictionary, std::vector<uint8_t>& data, bool& isStream)
{
    const std::string key = std::to_string(number) + " 0 obj";
    size_t at = 0;
    while ((at = file.find(key, at)) != std::string::npos)
    {
        // Not the tail of a longer number.
        if (at > 0 && std::isdigit(static_cast<unsigned char>(file[at - 1])))
        {
            at += key.size();
            continue;
        }
        const size_t end = file.find("endobj", at);
        const size_t streamAt = file.find("stream", at);
        isStream = streamAt != std::string::npos && streamAt < end;
        dictionary = file.substr(at + key.size(), (isStream ? streamAt : end) - at - key.size());
        if (isStream)
        {
            size_t begin = streamAt + 6;
            if (begin < file.size() && file[begin] == '\r')
                ++begin;
            if (begin < file.size() && file[begin] == '\n')
                ++begin;
            const size_t stop = file.find("endstream", begin);
            if (stop == std::string::npos)
                return false;
            std::vector<uint8_t> raw(file.begin() + static_cast<std::ptrdiff_t>(begin), file.begin() + static_cast<std::ptrdiff_t>(stop));
            data = dictionary.find("/FlateDecode") != std::string::npos ? inflate(raw.data(), raw.size()) : raw;
        }
        return true;
    }
    return false;
}

/// Every Flate stream of the file, decompressed: where a page's dictionary
/// may hide in an object stream.
std::vector<std::string> objectStreams(const std::string& file)
{
    std::vector<std::string> texts;
    size_t at = 0;
    while ((at = file.find("/ObjStm", at)) != std::string::npos)
    {
        const size_t streamAt = file.find("stream", at);
        if (streamAt == std::string::npos)
            break;
        size_t begin = streamAt + 6;
        if (begin < file.size() && file[begin] == '\r')
            ++begin;
        if (begin < file.size() && file[begin] == '\n')
            ++begin;
        const size_t stop = file.find("endstream", begin);
        if (stop == std::string::npos)
            break;
        const std::vector<uint8_t> data = inflate(reinterpret_cast<const uint8_t*>(file.data()) + begin, stop - begin);
        texts.emplace_back(data.begin(), data.end());
        at = stop;
    }
    return texts;
}

/// The first page dictionary in \a text, if any.
bool pageDictionary(const std::string& text, std::string& page)
{
    size_t at = 0;
    while ((at = text.find("/Type", at)) != std::string::npos)
    {
        size_t value = text.find_first_not_of(" \r\n\t", at + 5);
        if (value != std::string::npos && text.compare(value, 5, "/Page") == 0 &&
            (value + 5 >= text.size() || !std::isalpha(static_cast<unsigned char>(text[value + 5]))))
        {
            // The dictionary around it.
            const size_t open = text.rfind("<<", at);
            size_t depth = 0;
            size_t close = open;
            for (size_t i = open; i + 1 < text.size(); ++i)
            {
                if (text.compare(i, 2, "<<") == 0)
                {
                    ++depth;
                    ++i;
                }
                else if (text.compare(i, 2, ">>") == 0)
                {
                    --depth;
                    ++i;
                    if (depth == 0)
                    {
                        close = i;
                        break;
                    }
                }
            }
            page = text.substr(open, close - open + 1);
            return true;
        }
        at += 5;
    }
    return false;
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

/// Runs a page's content: its paths, as they are painted.
void runContent(const std::string& content, double pageLeft, double pageTop, Artwork& artwork)
{
    struct State
    {
        Affine ctm;
        Color fill{0, 0, 0, 255};
        Color stroke{0, 0, 0, 255};
        double lineWidth = 1.0;
    };
    std::vector<State> stack(1);
    std::vector<double> operands;
    std::vector<Subpath> path;
    Point current;
    Point start;
    bool sawXObject = false;
    // Page space to the stage: y downward from the page's top.
    const auto toStage = [&](double x, double y) {
        const Point p = stack.back().ctm.map(Point(x, y));
        return Point(p.x - pageLeft, pageTop - p.y);
    };
    const auto color = [&operands]() -> Color {
        const auto byte = [](double v) { return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
        if (operands.size() >= 4)
        {
            // CMYK, as plainly as it goes.
            const size_t n = operands.size();
            const double c = operands[n - 4], m = operands[n - 3], y = operands[n - 2], k = operands[n - 1];
            return Color{byte((1 - c) * (1 - k)), byte((1 - m) * (1 - k)), byte((1 - y) * (1 - k)), 255};
        }
        if (operands.size() == 3)
            return Color{byte(operands[0]), byte(operands[1]), byte(operands[2]), 255};
        if (operands.size() >= 1)
            return Color{byte(operands.back()), byte(operands.back()), byte(operands.back()), 255};
        return Color{0, 0, 0, 255};
    };
    const auto paint = [&](bool fill, bool stroke, bool close) {
        if (close && !path.empty())
            path.back().closed = true;
        if (!path.empty() && (fill || stroke))
        {
            Painted painted;
            painted.subpaths = path;
            if (fill)
                painted.fill = stack.back().fill;
            if (stroke)
            {
                painted.stroke = stack.back().stroke;
                painted.strokeWidth = std::max(0.1, stack.back().lineWidth * stack.back().ctm.scale());
            }
            artwork.paths.push_back(std::move(painted));
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
            // A string, skipped with its nesting and escapes.
            int depth = 0;
            for (; i < n; ++i)
            {
                if (content[i] == '\\')
                {
                    ++i;
                    continue;
                }
                if (content[i] == '(')
                    ++depth;
                else if (content[i] == ')' && --depth == 0)
                {
                    ++i;
                    break;
                }
            }
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
            i = close == std::string::npos ? n : close + 1;
            operands.clear();
            continue;
        }
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == '>')
        {
            ++i;
            continue;
        }
        if (c == '/')
        {
            ++i;
            while (i < n && !std::isspace(static_cast<unsigned char>(content[i])) && std::strchr("/[]()<>{}%", content[i]) == nullptr)
                ++i;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.')
        {
            Scanner scan{content.data() + i, content.data() + n};
            double value = 0;
            if (scan.number(value))
            {
                operands.push_back(value);
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
        if (op == "q")
            stack.push_back(stack.back());
        else if (op == "Q")
        {
            if (stack.size() > 1)
                stack.pop_back();
        }
        else if (op == "cm" && has(6))
            stack.back().ctm = stack.back().ctm.after(Affine{arg(6), arg(5), arg(4), arg(3), arg(2), arg(1)});
        else if (op == "w" && has(1))
            stack.back().lineWidth = arg(1);
        else if (op == "m" && has(2))
        {
            current = Point(arg(2), arg(1));
            start = current;
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
            if (path.empty())
                continue;
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
            start = current;
        }
        else if (op == "h")
        {
            if (!path.empty())
                path.back().closed = true;
            current = start;
        }
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
            path.clear();
        else if (op == "g" || op == "rg" || op == "k" || op == "sc" || op == "scn")
            stack.back().fill = color();
        else if (op == "G" || op == "RG" || op == "K" || op == "SC" || op == "SCN")
            stack.back().stroke = color();
        else if (op == "BI")
        {
            // An inline image: skipped to its end.
            const size_t ei = content.find("EI", i);
            i = ei == std::string::npos ? n : ei + 2;
        }
        else if (op == "Do")
            sawXObject = true;
        operands.clear();
    }
    if (sawXObject)
        artwork.notes.push_back("Artwork in placed or grouped objects of the page was left out.");
}

} // namespace

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
    SvgStyle style;
    for (const tinyxml2::XMLElement* child = root.FirstChildElement(); child; child = child->NextSiblingElement())
    {
        SvgStyle rootStyle = style;
        applyStyle(root, rootStyle);
        walkSvg(*child, rootStyle, view, artwork);
    }
    return artwork;
}

Artwork readPdf(const std::vector<uint8_t>& bytes)
{
    Artwork artwork;
    const std::string file(bytes.begin(), bytes.end());
    if (file.find("%PDF") == std::string::npos)
    {
        artwork.notes.push_back("The file has no PDF page: Illustrator files are read from the PDF they carry, which "
                                "Illustrator 9 and later save by default.");
        return artwork;
    }
    std::string page;
    if (!pageDictionary(file, page))
        for (const std::string& objects : objectStreams(file))
            if (pageDictionary(objects, page))
                break;
    if (page.empty())
    {
        artwork.notes.push_back("No page was found in the file.");
        return artwork;
    }
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
        if (findObject(file, ref, dictionary, data, isStream) && isStream)
            content.append(data.begin(), data.end()).push_back('\n');
    }
    if (content.empty())
    {
        artwork.notes.push_back("The page's content could not be read.");
        return artwork;
    }
    runContent(content, left, top, artwork);
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
    if (painted.fill)
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
