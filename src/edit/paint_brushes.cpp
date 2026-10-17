#include "paint_brushes.h"

#include "vector_import.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>

namespace fla {

namespace {

using P = PaintBrushArt::Point;

// ---------------------------------------------------------------- settings

/// A JSON object's members, their values as text: strings unescaped,
/// numbers and the rest as written. Nested objects and arrays are skipped.
std::map<std::string, std::string> flatJson(const std::string& text)
{
    std::map<std::string, std::string> members;
    size_t at = 0;
    const size_t end = text.size();
    const auto skipSpace = [&]() {
        while (at < end && std::isspace(static_cast<unsigned char>(text[at])))
            ++at;
    };
    const auto readString = [&](std::string& out) {
        // At the opening quote.
        ++at;
        while (at < end && text[at] != '"')
        {
            char c = text[at++];
            if (c != '\\' || at >= end)
            {
                out += c;
                continue;
            }
            c = text[at++];
            switch (c)
            {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
            {
                if (at + 4 > end)
                    return;
                const unsigned code = static_cast<unsigned>(std::strtoul(text.substr(at, 4).c_str(), nullptr, 16));
                at += 4;
                // As UTF-8 (the art's text is ASCII; pairs are not joined).
                if (code < 0x80)
                    out += static_cast<char>(code);
                else if (code < 0x800)
                {
                    out += static_cast<char>(0xC0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xE0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
                break;
            }
            default: out += c; break;
            }
        }
        ++at;
    };
    skipSpace();
    if (at >= end || text[at] != '{')
        return members;
    ++at;
    while (at < end)
    {
        skipSpace();
        if (at >= end || text[at] == '}')
            break;
        if (text[at] == ',')
        {
            ++at;
            continue;
        }
        if (text[at] != '"')
            break;
        std::string key;
        readString(key);
        skipSpace();
        if (at >= end || text[at] != ':')
            break;
        ++at;
        skipSpace();
        if (at >= end)
            break;
        std::string value;
        if (text[at] == '"')
            readString(value);
        else if (text[at] == '{' || text[at] == '[')
        {
            // Skipped whole, strings inside it and all.
            int depth = 0;
            bool quoted = false;
            for (; at < end; ++at)
            {
                const char c = text[at];
                if (quoted)
                {
                    if (c == '\\')
                        ++at;
                    else if (c == '"')
                        quoted = false;
                    continue;
                }
                if (c == '"')
                    quoted = true;
                else if (c == '{' || c == '[')
                    ++depth;
                else if ((c == '}' || c == ']') && --depth == 0)
                {
                    ++at;
                    break;
                }
            }
        }
        else
        {
            const size_t start = at;
            while (at < end && text[at] != ',' && text[at] != '}')
                ++at;
            value = text.substr(start, at - start);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
        }
        members[key] = value;
    }
    return members;
}

double numberOf(const std::map<std::string, std::string>& params, const char* key, double fallback)
{
    const auto found = params.find(key);
    if (found == params.end() || found->second.empty())
        return fallback;
    char* stop = nullptr;
    const double value = std::strtod(found->second.c_str(), &stop);
    return stop == found->second.c_str() ? fallback : value;
}

bool flagOf(const std::map<std::string, std::string>& params, const char* key)
{
    const auto found = params.find(key);
    return found != params.end() && (found->second == "true" || found->second == "1");
}

// ---------------------------------------------------------------- art

/// A subpath as points, its curves cut into lines \a step or so long.
std::vector<P> flattened(const vectors::Subpath& subpath, double step)
{
    std::vector<P> points;
    P current;
    for (const vectors::Segment& segment : subpath.segments)
    {
        if (segment.points.empty())
            continue;
        if (segment.kind == vectors::Segment::Kind::Cubic && segment.points.size() >= 3 && !points.empty())
        {
            const P p0 = current;
            const P p1{segment.points[0].x, segment.points[0].y};
            const P p2{segment.points[1].x, segment.points[1].y};
            const P p3{segment.points[2].x, segment.points[2].y};
            const double reach = std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p2.x - p1.x, p2.y - p1.y) +
                std::hypot(p3.x - p2.x, p3.y - p2.y);
            const int pieces = std::clamp(static_cast<int>(std::ceil(reach / step)), 1, 48);
            for (int i = 1; i <= pieces; ++i)
            {
                const double t = static_cast<double>(i) / pieces;
                const double u = 1.0 - t;
                points.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                    u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y});
            }
        }
        else
        {
            points.push_back({segment.points.back().x, segment.points.back().y});
        }
        current = points.back();
    }
    return points;
}

double signedArea(const std::vector<P>& ring)
{
    double area = 0.0;
    for (size_t i = 0, n = ring.size(); i < n; ++i)
    {
        const P& a = ring[i];
        const P& b = ring[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5;
}

/// Wound the one way, so overlapping pieces of an outline add up rather
/// than cancel.
void windForward(std::vector<P>& ring)
{
    if (signedArea(ring) < 0.0)
        std::reverse(ring.begin(), ring.end());
}

/// The area a line \a width wide along \a line covers: a quad for each
/// piece and a disc at each joint, all wound alike.
std::vector<std::vector<P>> strokeOutline(const std::vector<P>& line, bool closed, double width)
{
    std::vector<std::vector<P>> rings;
    const double half = width * 0.5;
    const size_t n = line.size();
    if (n < 2 || half <= 0.0)
        return rings;
    const size_t pieces = closed ? n : n - 1;
    for (size_t i = 0; i < pieces; ++i)
    {
        const P& a = line[i];
        const P& b = line[(i + 1) % n];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (length <= 1.0e-12)
            continue;
        const double nx = -(b.y - a.y) / length * half;
        const double ny = (b.x - a.x) / length * half;
        std::vector<P> quad{{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
        windForward(quad);
        rings.push_back(std::move(quad));
    }
    for (size_t i = closed ? 0 : 1; i < (closed ? n : n - 1); ++i)
    {
        std::vector<P> disc;
        for (int k = 0; k < 8; ++k)
        {
            const double angle = k * 3.14159265358979323846 / 4.0;
            disc.push_back({line[i].x + std::cos(angle) * half, line[i].y + std::sin(angle) * half});
        }
        windForward(disc);
        rings.push_back(std::move(disc));
    }
    return rings;
}

// ---------------------------------------------------------------- laying

/// A polyline measured along its length, with a normal at each point
/// that turns smoothly from one piece to the next.
class Centerline
{
public:
    /// \a points, pieces longer than \a finest cut up so a normal turning
    /// at a corner turns only near it (0 to keep them whole). With
    /// \a relax, each normal is square to the line's way over that reach
    /// either side of its point, so the line's offsets round a sharp turn
    /// rather than cross over themselves on its inside; without, normals
    /// are mitered at each joint.
    Centerline(const std::vector<P>& points, bool closed, double finest = 0.0, double relax = 0.0)
        : _closed(closed)
    {
        for (const P& point : points)
        {
            if (_points.empty() || std::hypot(point.x - _points.back().x, point.y - _points.back().y) > 1.0e-9)
                _points.push_back(point);
        }
        if (_closed && _points.size() > 2 &&
            std::hypot(_points.front().x - _points.back().x, _points.front().y - _points.back().y) <= 1.0e-9)
            _points.pop_back();
        if (_points.size() < 2)
        {
            _points.clear();
            return;
        }
        if (_closed)
            _points.push_back(_points.front());
        if (finest > 0.0)
        {
            double total = 0.0;
            for (size_t i = 0; i + 1 < _points.size(); ++i)
                total += std::hypot(_points[i + 1].x - _points[i].x, _points[i + 1].y - _points[i].y);
            // Not past a few thousand pieces, however long the line.
            finest = std::max(finest, total / 4000.0);
            std::vector<P> cut{_points.front()};
            for (size_t i = 0; i + 1 < _points.size(); ++i)
            {
                const P& a = _points[i];
                const P& b = _points[i + 1];
                const int pieces = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / finest)));
                for (int k = 1; k <= pieces; ++k)
                {
                    const double t = static_cast<double>(k) / pieces;
                    cut.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
                }
            }
            _points = std::move(cut);
        }
        const size_t n = _points.size();
        _at.assign(n, 0.0);
        std::vector<P> pieceNormals(n - 1);
        for (size_t i = 0; i + 1 < n; ++i)
        {
            const double dx = _points[i + 1].x - _points[i].x;
            const double dy = _points[i + 1].y - _points[i].y;
            const double length = std::hypot(dx, dy);
            _at[i + 1] = _at[i] + length;
            // Left of the way the line runs, with y down: (1, 0) has (0, 1).
            pieceNormals[i] = {-dy / length, dx / length};
        }
        _normals.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            const bool hasBefore = i > 0 || _closed;
            const bool hasAfter = i + 1 < n || _closed;
            const P before = hasBefore ? pieceNormals[i > 0 ? i - 1 : n - 2] : pieceNormals[0];
            const P after = hasAfter ? pieceNormals[i + 1 < n ? i : 0] : pieceNormals[n - 2];
            if (relax > 0.0)
            {
                // Over the reach either side, or as far as the line goes.
                const double reach = _closed ? std::min(relax, length() * 0.25)
                                             : std::min({relax, _at[i], length() - _at[i]});
                if (reach > 1.0e-9)
                {
                    const P a = position(_at[i] - reach);
                    const P b = position(_at[i] + reach);
                    const double chord = std::hypot(b.x - a.x, b.y - a.y);
                    if (chord > 1.0e-9)
                    {
                        _normals[i] = {-(b.y - a.y) / chord, (b.x - a.x) / chord};
                        continue;
                    }
                }
                _normals[i] = i + 1 < n ? after : before;
                continue;
            }
            P sum{before.x + after.x, before.y + after.y};
            const double norm = std::hypot(sum.x, sum.y);
            if (norm < 1.0e-6)
            {
                _normals[i] = after;
                continue;
            }
            sum = {sum.x / norm, sum.y / norm};
            // Mitred, so the art keeps its width round a bend, up to twice.
            const double cosine = std::max(sum.x * after.x + sum.y * after.y, 0.5);
            _normals[i] = {sum.x / cosine, sum.y / cosine};
        }
    }

    bool empty() const { return _points.size() < 2; }

    double length() const { return _at.empty() ? 0.0 : _at.back(); }

    /// The points, a closed line's last the same as its first.
    const std::vector<P>& points() const { return _points; }

    /// How far along the line point \a i is.
    double along(size_t i) const { return _at[i]; }

    /// The point \a along the line, on it; a closed line goes round again.
    P position(double along) const
    {
        size_t i = 0;
        double t = 0.0;
        place(along, i, t);
        const P& a = _points[i];
        const P& b = _points[i + 1];
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }

    /// The point \a along the line and \a out to its left; past either end
    /// of an open line, on along the way its end piece runs.
    P at(double along, double out) const
    {
        size_t i = 0;
        double t = 0.0;
        place(along, i, t);
        const P& a = _points[i];
        const P& b = _points[i + 1];
        const double w = std::clamp(t, 0.0, 1.0);
        const P& na = _normals[i];
        const P& nb = _normals[i + 1];
        const P normal{na.x + (nb.x - na.x) * w, na.y + (nb.y - na.y) * w};
        return {a.x + (b.x - a.x) * t + normal.x * out, a.y + (b.y - a.y) * t + normal.y * out};
    }

private:
    /// The piece \a along falls in, and how far through it.
    void place(double along, size_t& i, double& t) const
    {
        const double total = length();
        if (_closed && total > 0.0)
        {
            along = std::fmod(along, total);
            if (along < 0.0)
                along += total;
        }
        if (along <= 0.0)
        {
            i = 0;
            t = along / std::max(_at[1] - _at[0], 1.0e-12);
        }
        else if (along >= total)
        {
            i = _at.size() - 2;
            t = 1.0 + (along - total) / std::max(_at[i + 1] - _at[i], 1.0e-12);
        }
        else
        {
            i = static_cast<size_t>(std::upper_bound(_at.begin(), _at.end(), along) - _at.begin()) - 1;
            i = std::min(i, _at.size() - 2);
            t = (along - _at[i]) / std::max(_at[i + 1] - _at[i], 1.0e-12);
        }
    }

    bool _closed = false;
    std::vector<P> _points;
    std::vector<double> _at;
    std::vector<P> _normals;
};

/// The joints of \a line (its points' indexes) where it turns sharply
/// enough to be a corner, more than \a degrees, in order; a closed line's
/// start among them when it turns there.
std::vector<size_t> sharpCorners(const Centerline& line, bool closed, double degrees)
{
    std::vector<size_t> corners;
    const std::vector<P>& points = line.points();
    const size_t n = points.size();
    if (n < 3)
        return corners;
    const double limit = std::cos(degrees * 3.14159265358979323846 / 180.0);
    const size_t first = closed ? 0 : 1;
    const size_t last = n - 1;
    for (size_t i = first; i < last; ++i)
    {
        const P& before = points[i > 0 ? i - 1 : n - 2];
        const P& here = points[i];
        const P& after = points[i + 1];
        const double ax = here.x - before.x, ay = here.y - before.y;
        const double bx = after.x - here.x, by = after.y - here.y;
        const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la <= 1.0e-12 || lb <= 1.0e-12)
            continue;
        if ((ax * bx + ay * by) / (la * lb) < limit)
            corners.push_back(i);
    }
    return corners;
}

/// \a ring with what lies past the line through \a through square to
/// \a toward cut away (keeping where (p - through) . toward <= 0).
std::vector<P> clipped(const std::vector<P>& ring, const P& through, const P& toward)
{
    std::vector<P> out;
    const size_t n = ring.size();
    if (n == 0)
        return out;
    const auto side = [&](const P& p) { return (p.x - through.x) * toward.x + (p.y - through.y) * toward.y; };
    for (size_t i = 0; i < n; ++i)
    {
        const P& a = ring[i];
        const P& b = ring[(i + 1) % n];
        const double sa = side(a);
        const double sb = side(b);
        if (sa <= 0.0)
            out.push_back(a);
        if ((sa < 0.0 && sb > 0.0) || (sa > 0.0 && sb < 0.0))
        {
            const double t = sa / (sa - sb);
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
        }
    }
    return out;
}

/// The reach to the left and the right, as fractions of the weight, a
/// fraction \a t of the way along a stroke with \a profile (a half and a
/// half for none): its points joined smoothly, or straight into and out
/// of a corner point; the whole width kept between \a narrowest and
/// \a widest.
void reachAt(const std::vector<StrokeStyle::WidthMarker>& profile, double t, double narrowest, double widest,
    double& left, double& right)
{
    left = 0.5;
    right = 0.5;
    if (profile.empty())
        return;
    if (t <= profile.front().position)
    {
        left = profile.front().left;
        right = profile.front().right;
    }
    else if (t >= profile.back().position)
    {
        left = profile.back().left;
        right = profile.back().right;
    }
    else
    {
        for (size_t i = 1; i < profile.size(); ++i)
        {
            const StrokeStyle::WidthMarker& a = profile[i - 1];
            const StrokeStyle::WidthMarker& b = profile[i];
            if (t > b.position)
                continue;
            double u = b.position > a.position ? (t - a.position) / (b.position - a.position) : 1.0;
            if (!a.corner || !b.corner)
                u = u * u * (3.0 - 2.0 * u);
            left = a.left + (b.left - a.left) * u;
            right = a.right + (b.right - a.right) * u;
            break;
        }
    }
    const double whole = left + right;
    const double kept = std::clamp(whole, narrowest, widest);
    if (whole > 1.0e-12)
    {
        left *= kept / whole;
        right *= kept / whole;
    }
    else
    {
        left = right = kept * 0.5;
    }
}

} // namespace

// ---------------------------------------------------------------- reading

PaintBrushDefinition paintBrushDefinition(int index, const std::string& json)
{
    PaintBrushDefinition definition;
    definition.index = index;
    definition.json = json;
    definition.params = flatJson(json);
    const auto& params = definition.params;
    const auto name = params.find("name");
    if (name != params.end())
        definition.name = name->second;
    const auto type = params.find("FlashPaintBrushType");
    definition.pattern = type != params.end() && type->second == "PatternBrush";
    const auto fraction = [&params](const char* key, double fallback) {
        return std::clamp(numberOf(params, key, fallback) / 100.0, 0.0, 10.0);
    };
    definition.minimumWidth = fraction("minimumWidthPercentage", 1.0);
    definition.maximumWidth = std::max(fraction("maximumWidthPercentage", 100.0), definition.minimumWidth);
    definition.minimumTiltWidth = fraction("minimumTiltWidthPercentage", 1.0);
    definition.maximumTiltWidth = std::max(fraction("maximumTiltWidthPercentage", 100.0), definition.minimumTiltWidth);
    if (definition.pattern)
    {
        definition.flipAlong = flagOf(params, "FlipAlong");
        definition.flipAcross = flagOf(params, "FlipAcross");
        const int fit = static_cast<int>(numberOf(params, "adjustment", 0.0));
        definition.fit = fit == 1 ? PaintBrushDefinition::Fit::AddSpace
            : fit == 2           ? PaintBrushDefinition::Fit::Approximate
                                 : PaintBrushDefinition::Fit::Stretch;
        definition.spacing = std::max(numberOf(params, "spacing", 0.0), 0.0) / 100.0;
        const int corner = static_cast<int>(numberOf(params, "cornerValue", 2.0));
        definition.corner = corner == 0 ? PaintBrushDefinition::Corner::Centered
            : corner == 1              ? PaintBrushDefinition::Corner::Between
            : corner == 3              ? PaintBrushDefinition::Corner::Overlap
                                       : PaintBrushDefinition::Corner::Sliced;
    }
    else
    {
        const int direction = static_cast<int>(numberOf(params, "artDirection", 0.0));
        definition.direction = direction == 1 ? PaintBrushDefinition::Direction::RightToLeft
            : direction == 2                 ? PaintBrushDefinition::Direction::TopToBottom
            : direction == 3                 ? PaintBrushDefinition::Direction::BottomToTop
                                             : PaintBrushDefinition::Direction::LeftToRight;
        definition.flipAcross = flagOf(params, "flipArt");
        // The file spells it so.
        const int scale = static_cast<int>(numberOf(params, "scaleOptionm", numberOf(params, "scaleOption", 1.0)));
        definition.scale = scale == 0 ? PaintBrushDefinition::Scale::Proportional
            : scale == 2             ? PaintBrushDefinition::Scale::BetweenGuides
                                     : PaintBrushDefinition::Scale::Stretch;
        definition.leftGuide = std::clamp(numberOf(params, "leftEdgePercentage", 0.0) / 100.0, 0.0, 1.0);
        definition.rightGuide =
            std::clamp(numberOf(params, "rightEdgePercentage", 100.0) / 100.0, definition.leftGuide, 1.0);
        definition.foldAdjust = numberOf(params, "foldAdjust", 1.0) != 0.0;
    }
    return definition;
}

std::string PaintBrushDefinition::guid() const
{
    const auto found = params.find("guid");
    return found != params.end() ? found->second : std::string();
}

std::vector<PaintBrushDefinition> readPaintBrushDefinitions(const std::string& xml)
{
    std::vector<PaintBrushDefinition> definitions;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !document.RootElement())
        return definitions;
    for (const tinyxml2::XMLElement* element = document.RootElement()->FirstChildElement(); element;
         element = element->NextSiblingElement())
    {
        if (std::strcmp(element->Name(), "paintbrushdefinition") != 0)
            continue;
        const char* param = element->Attribute("brushParam");
        definitions.push_back(paintBrushDefinition(element->IntAttribute("index", 0), param ? param : ""));
    }
    return definitions;
}

PaintBrushArt paintBrushArt(const PaintBrushDefinition& definition)
{
    PaintBrushArt art;
    const auto svg = definition.params.find("svgData");
    if (svg == definition.params.end())
        return art;
    const vectors::Artwork artwork = vectors::readSvg(svg->second);
    art.width = artwork.width;
    art.height = artwork.height;
    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
    for (const vectors::Painted& painted : artwork.paths)
        for (const vectors::Subpath& subpath : painted.subpaths)
            for (const vectors::Segment& segment : subpath.segments)
                for (const Point& point : segment.points)
                {
                    left = std::min(left, point.x);
                    top = std::min(top, point.y);
                    right = std::max(right, point.x);
                    bottom = std::max(bottom, point.y);
                }
    // An SVG with no viewBox or size is laid out by its art's own box.
    if (art.width <= 0.0 || art.height <= 0.0)
    {
        if (right <= left || bottom <= top)
            return art;
        art.width = right;
        art.height = bottom;
    }
    const double step = std::max(art.width, art.height) / 200.0;
    bool grays = true;
    for (const vectors::Painted& painted : artwork.paths)
    {
        std::vector<std::pair<std::vector<P>, bool>> lines;
        for (const vectors::Subpath& subpath : painted.subpaths)
        {
            std::vector<P> line = flattened(subpath, step);
            if (line.size() >= 2)
                lines.emplace_back(std::move(line), subpath.closed);
        }
        if (lines.empty())
            continue;
        const auto gray = [](const vectors::Color& color) {
            return std::abs(color[0] - color[1]) <= 3 && std::abs(color[1] - color[2]) <= 3;
        };
        if (painted.fill)
        {
            PaintBrushArt::Piece piece;
            piece.evenOdd = painted.evenOdd;
            piece.color = *painted.fill;
            for (const auto& line : lines)
                if (line.first.size() >= 3)
                    piece.rings.push_back(line.first);
            if (!piece.rings.empty())
            {
                grays = grays && gray(piece.color);
                art.pieces.push_back(std::move(piece));
            }
        }
        if (painted.stroke && painted.strokeWidth > 0.0)
        {
            PaintBrushArt::Piece piece;
            piece.color = *painted.stroke;
            for (const auto& line : lines)
                for (std::vector<P>& ring : strokeOutline(line.first, line.second, painted.strokeWidth))
                    piece.rings.push_back(std::move(ring));
            if (!piece.rings.empty())
            {
                grays = grays && gray(piece.color);
                art.pieces.push_back(std::move(piece));
            }
        }
    }
    art.tinted = grays;
    return art;
}

const PaintBrushDefinition* PaintBrushLibrary::find(int index) const
{
    for (const PaintBrushDefinition& definition : definitions)
        if (definition.index == index)
            return &definition;
    return nullptr;
}

std::shared_ptr<const PaintBrushArt> PaintBrushLibrary::art(int index) const
{
    const PaintBrushDefinition* definition = find(index);
    if (!definition)
        return nullptr;
    std::lock_guard<std::mutex> lock(_mutex);
    auto& slot = _art[index];
    if (!slot)
        slot = std::make_shared<const PaintBrushArt>(paintBrushArt(*definition));
    return slot;
}

std::shared_ptr<const PaintBrushLibrary> paintBrushesOf(const Document& document)
{
    // The file the document's brush definitions include.
    std::string href = "PaintBrushDefinitions.xml";
    for (const Document::RawSection& section : document.extraSections)
    {
        if (section.xml.rfind("<brushdefinitions", 0) != 0)
            continue;
        const size_t at = section.xml.find("href=\"");
        if (at != std::string::npos)
        {
            const size_t end = section.xml.find('"', at + 6);
            if (end != std::string::npos)
                href = section.xml.substr(at + 6, end - at - 6);
        }
    }
    const LazyBytes* bytes = nullptr;
    for (const auto& [path, data] : document.extraFiles)
        if (path == href)
            bytes = &data;

    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (!bytes)
    {
        document.paintBrushes.reset();
        document.paintBrushesSource = nullptr;
        return nullptr;
    }
    const std::vector<uint8_t>& data = bytes->get();
    if (document.paintBrushes && document.paintBrushesSource == data.data())
        return document.paintBrushes;
    auto library = std::make_shared<PaintBrushLibrary>();
    library->definitions = readPaintBrushDefinitions(std::string(data.begin(), data.end()));
    document.paintBrushes = library;
    document.paintBrushesSource = data.data();
    return document.paintBrushes;
}

// ---------------------------------------------------------------- editing

namespace {

/// Where the value of \a json's member \a key lies (its first character
/// and one past its last); false when it has none. Only the object's own
/// members count, not those of objects inside it.
bool memberSpan(const std::string& json, const std::string& key, size_t& from, size_t& to)
{
    size_t at = 0;
    const size_t end = json.size();
    const auto skipSpace = [&]() {
        while (at < end && std::isspace(static_cast<unsigned char>(json[at])))
            ++at;
    };
    // Past a string, at its opening quote; its text, escapes and all.
    const auto skipString = [&]() {
        const size_t start = ++at;
        while (at < end && json[at] != '"')
            at += json[at] == '\\' ? 2 : 1;
        const std::string text = json.substr(start, std::min(at, end) - start);
        ++at;
        return text;
    };
    skipSpace();
    if (at >= end || json[at] != '{')
        return false;
    ++at;
    while (at < end)
    {
        skipSpace();
        if (at >= end || json[at] == '}')
            return false;
        if (json[at] == ',')
        {
            ++at;
            continue;
        }
        if (json[at] != '"')
            return false;
        const std::string name = skipString();
        skipSpace();
        if (at >= end || json[at] != ':')
            return false;
        ++at;
        skipSpace();
        const size_t start = at;
        if (at < end && json[at] == '"')
        {
            skipString();
        }
        else if (at < end && (json[at] == '{' || json[at] == '['))
        {
            int depth = 0;
            for (; at < end; ++at)
            {
                const char c = json[at];
                if (c == '"')
                {
                    skipString();
                    --at;
                }
                else if (c == '{' || c == '[')
                    ++depth;
                else if ((c == '}' || c == ']') && --depth == 0)
                {
                    ++at;
                    break;
                }
            }
        }
        else
        {
            while (at < end && json[at] != ',' && json[at] != '}')
                ++at;
            while (at > start && std::isspace(static_cast<unsigned char>(json[at - 1])))
                --at;
        }
        if (name == key)
        {
            from = start;
            to = at;
            return true;
        }
    }
    return false;
}

/// \a text as a JSON string, quotes and all.
std::string jsonString(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char code[8];
                std::snprintf(code, sizeof code, "\\u%04x", static_cast<unsigned>(c));
                out += code;
            }
            else
            {
                out += c;
            }
        }
    }
    return out + "\"";
}

/// \a text as an XML attribute value in single quotes.
std::string attributeText(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (const char c : text)
    {
        switch (c)
        {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c; break;
        }
    }
    return out;
}

/// Whether \a name holds \a word, ignoring case.
bool mentions(const std::string& name, const char* word)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return lower.find(word) != std::string::npos;
}

/// The strokes \a element draws with, inside groups too.
void strokesOf(Element& element, const std::function<void(StrokeStyle&)>& visit)
{
    switch (element.elementType())
    {
    case Element::Type::Shape:
        for (StrokeStyle* stroke : static_cast<Shape&>(element).strokes)
            if (stroke)
                visit(*stroke);
        break;
    case Element::Type::Rectangle:
        if (StrokeStyle* stroke = static_cast<RectanglePrimitive&>(element).strokeStyle)
            visit(*stroke);
        break;
    case Element::Type::Oval:
        if (StrokeStyle* stroke = static_cast<OvalPrimitive&>(element).strokeStyle)
            visit(*stroke);
        break;
    case Element::Type::Group:
        for (Element* member : static_cast<Group&>(element).members)
            if (member)
                strokesOf(*member, visit);
        break;
    default: break;
    }
}

/// Where a document keeps its brush definitions: the file, by the path
/// its <brushdefinitions> section includes (or the usual name), and that
/// section's place among its sections; -1 for none.
std::string definitionsPath(const Document& document, int* section = nullptr)
{
    std::string href = "PaintBrushDefinitions.xml";
    if (section)
        *section = -1;
    for (size_t i = 0; i < document.extraSections.size(); ++i)
    {
        const std::string& xml = document.extraSections[i].xml;
        if (xml.rfind("<brushdefinitions", 0) != 0)
            continue;
        if (section)
            *section = static_cast<int>(i);
        const size_t at = xml.find("href=\"");
        if (at != std::string::npos)
        {
            const size_t end = xml.find('"', at + 6);
            if (end != std::string::npos)
                href = xml.substr(at + 6, end - at - 6);
        }
    }
    return href;
}

/// A document's brush definitions as kept: the file and the section that
/// includes it, either missing.
struct DefinitionsState
{
    std::string path;
    bool hasFile = false;
    LazyBytes bytes;
    bool hasSection = false;
    Document::RawSection section;
};

DefinitionsState definitionsState(const Document& document)
{
    DefinitionsState state;
    int section = -1;
    state.path = definitionsPath(document, &section);
    if (section >= 0)
    {
        state.hasSection = true;
        state.section = document.extraSections[static_cast<size_t>(section)];
    }
    for (const auto& [path, bytes] : document.extraFiles)
        if (path == state.path)
        {
            state.hasFile = true;
            state.bytes = bytes;
        }
    return state;
}

void restore(Document& document, const DefinitionsState& state)
{
    auto& files = document.extraFiles;
    const auto file = std::find_if(files.begin(), files.end(), [&](const auto& entry) { return entry.first == state.path; });
    if (state.hasFile && file != files.end())
        file->second = state.bytes;
    else if (state.hasFile)
        files.emplace_back(state.path, state.bytes);
    else if (file != files.end())
        files.erase(file);
    auto& sections = document.extraSections;
    const auto section = std::find_if(sections.begin(), sections.end(),
        [](const Document::RawSection& raw) { return raw.xml.rfind("<brushdefinitions", 0) == 0; });
    if (state.hasSection && section != sections.end())
        *section = state.section;
    else if (state.hasSection)
        sections.push_back(state.section);
    else if (section != sections.end())
        sections.erase(section);
}

/// A document's brush definitions set, both ways.
class PaintBrushDefinitionsCommand : public Command
{
public:
    PaintBrushDefinitionsCommand(Document& document, DefinitionsState before, DefinitionsState after, std::string name)
        : _document(document)
        , _before(std::move(before))
        , _after(std::move(after))
        , _name(std::move(name))
    {
    }

    void redo() override { restore(_document, _after); }
    void undo() override { restore(_document, _before); }
    std::string name() const override { return _name; }
    size_t memoryUse() const override { return _after.bytes.size(); }

private:
    Document& _document;
    DefinitionsState _before;
    DefinitionsState _after;
    std::string _name;
};

} // namespace

std::string withJsonMember(const std::string& json, const std::string& key, const std::string& value)
{
    size_t from = 0, to = 0;
    if (memberSpan(json, key, from, to))
        return json.substr(0, from) + jsonString(value) + json.substr(to);
    const size_t close = json.rfind('}');
    if (close == std::string::npos)
        return "{" + jsonString(key) + ":" + jsonString(value) + "}";
    // After the last member, if any.
    size_t last = close;
    while (last > 0 && std::isspace(static_cast<unsigned char>(json[last - 1])))
        --last;
    const bool empty = last > 0 && json[last - 1] == '{';
    return json.substr(0, last) + (empty ? "" : ",") + jsonString(key) + ":" + jsonString(value) + json.substr(last);
}

PaintBrushDefinition withParam(const PaintBrushDefinition& definition, const std::string& key, const std::string& value)
{
    return paintBrushDefinition(definition.index, withJsonMember(definition.json, key, value));
}

const std::vector<PaintBrushOption>& paintBrushOptions(bool pattern)
{
    using Kind = PaintBrushOption::Kind;
    const auto widths = [](std::vector<PaintBrushOption>& options) {
        options.push_back({"minimumWidthPercentage", "Width, lightest touch", Kind::Percent, {}, "1.00", 2,
            "How narrow a light touch of the pen, or a width profile, draws the stroke, as a share of its weight"});
        options.push_back({"maximumWidthPercentage", "Width, pressing hard", Kind::Percent, {}, "100.00", 2,
            "How wide pressing hard, or a width profile, draws the stroke, as a share of its weight"});
        options.push_back({"minimumTiltWidthPercentage", "Width, pen leaning", Kind::Percent, {}, "1.00", 2,
            "How narrow the stroke draws with the pen leaning all the way over"});
        options.push_back({"maximumTiltWidthPercentage", "Width, pen upright", Kind::Percent, {}, "100.00", 2,
            "How wide the stroke draws with the pen upright"});
    };
    static const std::vector<PaintBrushOption> art = [&widths]() {
        std::vector<PaintBrushOption> options{
            {"artDirection", "Art runs", Kind::Choice,
                {{"0", "Left to right"}, {"1", "Right to left"}, {"2", "Top to bottom"}, {"3", "Bottom to top"}}, "0", 0,
                "Which way the art runs along the stroke, from the stroke's start"},
            {"scaleOptionm", "Fit", Kind::Choice,
                {{"0", "Scale proportionately"}, {"1", "Stretch to fit"}, {"2", "Stretch between guides"}}, "1", 0,
                "How the art is fitted to the stroke's length"},
            {"leftEdgePercentage", "Left guide", Kind::Percent, {}, "0.00", 2,
                "Stretching between guides: where the part that keeps its proportions at the start ends"},
            {"rightEdgePercentage", "Right guide", Kind::Percent, {}, "100.00", 2,
                "Stretching between guides: where the part that keeps its proportions at the end begins"},
            {"flipArt", "Flip across", Kind::Flag, {{"false", "Off"}, {"true", "On"}}, "false", 0,
                "Turn the art over from one side of the stroke to the other"},
            {"foldAdjust", "Bend round corners", Kind::Flag, {{"0", "Off"}, {"1", "On"}}, "1", 0,
                "Bend the art round a sharp turn of the stroke rather than folding it over itself there"},
        };
        widths(options);
        return options;
    }();
    static const std::vector<PaintBrushOption> patterns = [&widths]() {
        std::vector<PaintBrushOption> options{
            {"adjustment", "Fit", Kind::Choice,
                {{"0", "Stretch tiles to fit"}, {"1", "Add space to fit"}, {"2", "Approximate path"}}, "0", 0,
                "How a whole number of tiles is fitted to the stroke's length"},
            {"spacing", "Spacing", Kind::Percent, {}, "0", 0, "Space between the tiles, as a share of a tile"},
            {"cornerValue", "Corners", Kind::Choice,
                {{"0", "Auto-centered"}, {"1", "Auto-between"}, {"2", "Auto-sliced"}, {"3", "Auto-overlap"}}, "2", 0,
                "How the tiles turn a sharp corner: a tile folded round it, tiles folded into it, tiles cut along "
                "its bisector, or tiles running on to overlap there"},
            {"FlipAlong", "Flip along", Kind::Flag, {{"0", "Off"}, {"1", "On"}}, "0", 0,
                "Turn each tile end for end"},
            {"FlipAcross", "Flip across", Kind::Flag, {{"0", "Off"}, {"1", "On"}}, "0", 0,
                "Turn each tile over from one side of the stroke to the other"},
        };
        widths(options);
        return options;
    }();
    return pattern ? patterns : art;
}

std::string paintBrushOptionValue(const PaintBrushDefinition& definition, const PaintBrushOption& option)
{
    const auto found = definition.params.find(option.key);
    return found != definition.params.end() && !found->second.empty() ? found->second : option.fallback;
}

std::string newPaintBrushGuid()
{
    static std::mutex mutex;
    static std::mt19937_64 random(std::random_device{}() ^
        static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::lock_guard<std::mutex> lock(mutex);
    const uint64_t high = random();
    const uint64_t low = random();
    char text[40];
    std::snprintf(text, sizeof text, "%08X-%04X-4%03X-%04X-%012llX", static_cast<unsigned>(high >> 32),
        static_cast<unsigned>((high >> 16) & 0xFFFF), static_cast<unsigned>(high & 0x0FFF),
        static_cast<unsigned>(0x8000 | ((low >> 48) & 0x3FFF)),
        static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFull));
    return text;
}

std::string writePaintBrushDefinitions(const std::vector<PaintBrushDefinition>& definitions)
{
    std::string xml = "<paintbrushdefinitions xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                      "xmlns=\"http://ns.adobe.com/xfl/2008/\">";
    for (const PaintBrushDefinition& definition : definitions)
        xml += "\r\n  <paintbrushdefinition index=\"" + std::to_string(definition.index) + "\" brushParam='" +
            attributeText(definition.json) + "'/>";
    return xml + "\r\n</paintbrushdefinitions>";
}

std::string paintBrushCategory(const PaintBrushDefinition& definition)
{
    const std::string& name = definition.name;
    if (mentions(name, "arrow"))
        return "Arrows";
    if (definition.pattern)
        return mentions(name, "dashed") ? "Dashed Borders" : "Pattern Borders";
    if (mentions(name, "chalk") || mentions(name, "charcoal") || mentions(name, "pencil"))
        return "Chalk, Charcoal and Pencil";
    if (mentions(name, "calligraphy") || mentions(name, "ink") || mentions(name, "fountain pen") ||
        mentions(name, "marker") || mentions(name, "tapered"))
        return "Ink";
    if (mentions(name, "scroll pen"))
        return "Scroll Pens";
    if (mentions(name, "brush") && !mentions(name, "floral") && !mentions(name, "vector pack"))
        return "Paint Brushes";
    if (mentions(name, "palette knife"))
        return "Paint Brushes";
    if (mentions(name, "banner") || mentions(name, "starburst") || mentions(name, "balloon"))
        return "Banners and Balloons";
    if (mentions(name, "text divider"))
        return "Text Dividers";
    if (mentions(name, "floral") || mentions(name, "tulip") || mentions(name, "grass") || mentions(name, "ground") ||
        mentions(name, "leaf") || mentions(name, "leaves") || mentions(name, "vine") || mentions(name, "spikey"))
        return "Floral";
    if (mentions(name, "cloud") || mentions(name, "drop") || mentions(name, "wave") || mentions(name, "flame"))
        return "Decorative";
    if (mentions(name, "lineart") || mentions(name, "line art") || mentions(name, "hand drawn"))
        return "Line Art";
    return "Other";
}

std::vector<PaintBrushDefinition> mergedPaintBrushes(const std::vector<PaintBrushDefinition>& existing,
    const std::vector<PaintBrushDefinition>& incoming, std::map<int, int>& indexes)
{
    std::vector<PaintBrushDefinition> merged = existing;
    int highest = 0;
    for (const PaintBrushDefinition& definition : existing)
        highest = std::max(highest, definition.index);
    for (const PaintBrushDefinition& definition : incoming)
    {
        const std::string guid = definition.guid();
        const auto same = std::find_if(merged.begin(), merged.end(), [&](const PaintBrushDefinition& other) {
            return guid.empty() ? other.json == definition.json : other.guid() == guid;
        });
        if (same != merged.end())
        {
            indexes[definition.index] = same->index;
            continue;
        }
        PaintBrushDefinition copy = definition;
        copy.index = ++highest;
        indexes[definition.index] = copy.index;
        merged.push_back(std::move(copy));
    }
    return merged;
}

CommandPtr setPaintBrushDefinitions(
    Document& document, const std::vector<PaintBrushDefinition>& definitions, const std::string& name)
{
    DefinitionsState before = definitionsState(document);
    DefinitionsState after = before;
    const std::string xml = writePaintBrushDefinitions(definitions);
    const std::vector<uint8_t>& had = before.bytes.get();
    if (before.hasFile && before.hasSection && std::string(had.begin(), had.end()) == xml)
        return nullptr;
    after.hasFile = true;
    after.bytes = LazyBytes(std::vector<uint8_t>(xml.begin(), xml.end()));
    if (!after.hasSection)
    {
        // As FLA files include it, before the timelines.
        after.hasSection = true;
        after.section.xml = "<brushdefinitions>\n     <Include href=\"" + after.path + "\"/>\n</brushdefinitions>";
        after.section.beforeTimelines = true;
    }
    return CommandPtr(new PaintBrushDefinitionsCommand(document, std::move(before), std::move(after), name));
}

CommandPtr adoptPaintBrushes(Document& document, const std::vector<PaintBrushDefinition>& incoming,
    std::map<int, int>& indexes, const std::string& name)
{
    const auto library = paintBrushesOf(document);
    const std::vector<PaintBrushDefinition> existing = library ? library->definitions : std::vector<PaintBrushDefinition>();
    const std::vector<PaintBrushDefinition> merged = mergedPaintBrushes(existing, incoming, indexes);
    if (merged.size() == existing.size())
        return nullptr;
    return setPaintBrushDefinitions(document, merged, name);
}

void paintBrushesUsedBy(const Element& element, std::set<int>& used)
{
    strokesOf(const_cast<Element&>(element), [&used](StrokeStyle& stroke) {
        if (stroke.style() == StrokeStyle::Style::PaintBrush)
            used.insert(stroke.paintBrushDefinition);
    });
}

void remapPaintBrushes(Element& element, const std::map<int, int>& indexes)
{
    strokesOf(element, [&indexes](StrokeStyle& stroke) {
        if (stroke.style() != StrokeStyle::Style::PaintBrush)
            return;
        const auto found = indexes.find(stroke.paintBrushDefinition);
        if (found != indexes.end())
            stroke.paintBrushDefinition = found->second;
    });
}

std::map<int, int> paintBrushUses(const Document& document)
{
    std::map<int, int> uses;
    forEachStroke(document, [&uses](const StrokeStyle& stroke) {
        if (stroke.style() == StrokeStyle::Style::PaintBrush)
            ++uses[stroke.paintBrushDefinition];
    });
    return uses;
}

void forEachStroke(const Document& document, const std::function<void(const StrokeStyle&)>& visit)
{
    const auto count = [&visit](StrokeStyle& stroke) { visit(stroke); };
    std::vector<const Timeline*> timelines(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (Element* element : frame ? frame->elements : std::vector<Element*>())
                    if (element)
                        strokesOf(*element, count);
}

std::vector<PaintBrushDefinition> paintBrushesFor(const Document& source, const std::vector<const Element*>& elements)
{
    std::vector<PaintBrushDefinition> definitions;
    std::set<int> used;
    for (const Element* element : elements)
        if (element)
            paintBrushesUsedBy(*element, used);
    const auto library = used.empty() ? nullptr : paintBrushesOf(source);
    if (!library)
        return definitions;
    for (const int index : used)
        if (const PaintBrushDefinition* definition = library->find(index))
            definitions.push_back(*definition);
    return definitions;
}

// ---------------------------------------------------------------- drawing

std::vector<PaintBrushArt::Piece> layPaintBrush(const PaintBrushDefinition& definition, const PaintBrushArt& art,
    const std::vector<P>& centerline, bool closed, double weight, const PaintBrushArt::Color& color,
    const std::vector<StrokeStyle::WidthMarker>& profile)
{
    std::vector<PaintBrushArt::Piece> laid;
    if (art.width <= 0.0 || art.height <= 0.0 || art.pieces.empty())
        return laid;
    const Centerline plain(centerline, closed);
    const double length = plain.length();
    if (plain.empty() || length <= 1.0e-9)
        return laid;
    weight = std::max(weight, 0.1);

    // The art turned to run along the stroke: its length (a) and its
    // height across the stroke (c).
    const bool upright = !definition.pattern && (definition.direction == PaintBrushDefinition::Direction::TopToBottom ||
                                                    definition.direction == PaintBrushDefinition::Direction::BottomToTop);
    const double A = upright ? art.height : art.width;
    const double C = upright ? art.width : art.height;
    const auto turned = [&](const P& p) {
        P q = p;
        switch (definition.pattern ? PaintBrushDefinition::Direction::LeftToRight : definition.direction)
        {
        case PaintBrushDefinition::Direction::LeftToRight: break;
        case PaintBrushDefinition::Direction::RightToLeft: q = {art.width - p.x, art.height - p.y}; break;
        case PaintBrushDefinition::Direction::TopToBottom: q = {p.y, art.width - p.x}; break;
        case PaintBrushDefinition::Direction::BottomToTop: q = {art.height - p.y, p.x}; break;
        }
        if (definition.flipAlong)
            q.x = A - q.x;
        if (definition.flipAcross)
            q.y = C - q.y;
        return q;
    };

    // How far along its line each tile starts, and how the art's length
    // scales onto it.
    struct Tile
    {
        double start = 0.0;
        double alongScale = 1.0;
    };
    // Tiles laid along one line: the whole stroke, or for a pattern cut at
    // its corners one stretch of it, starting \a offset along the stroke,
    // its art cut at either end where a corner's bisector says.
    struct Run
    {
        const Centerline* line = nullptr;
        double offset = 0.0;
        std::vector<Tile> tiles;
        std::vector<std::pair<P, P>> cuts;
    };
    std::vector<std::unique_ptr<Centerline>> lines;
    std::vector<Run> runs;
    double acrossScale = weight / C;
    // For an art brush stretched between guides: the guides, and the
    // lengths on the stroke of the ends past them.
    bool guided = false;
    double guideLeft = 0.0, guideRight = A, head = 0.0, middle = length, tail = 0.0;
    if (definition.pattern)
    {
        const double tile = A * acrossScale;
        const double pitch = tile * (1.0 + definition.spacing);
        // Tiles from \a from for \a span along the line.
        const auto fit = [&](std::vector<Tile>& tiles, double from, double span) {
            if (span <= 1.0e-9)
                return;
            if (definition.fit == PaintBrushDefinition::Fit::AddSpace)
            {
                const int count = std::max(1, static_cast<int>(std::floor(span / pitch)));
                const double gap = (span - count * tile) / count;
                for (int i = 0; i < count; ++i)
                    tiles.push_back({from + i * (tile + gap) + gap * 0.5, acrossScale});
            }
            else
            {
                const int count = std::max(1, static_cast<int>(std::lround(span / pitch)));
                const double stretch = span / (count * pitch);
                for (int i = 0; i < count; ++i)
                    tiles.push_back({from + i * pitch * stretch, acrossScale * stretch});
            }
        };
        constexpr double kCornerDegrees = 40.0;
        const std::vector<size_t> corners = sharpCorners(plain, closed, kCornerDegrees);
        if (corners.empty())
        {
            runs.push_back({&plain});
            fit(runs.back().tiles, 0.0, length);
        }
        else
        {
            const std::vector<P>& points = plain.points();
            const size_t n = points.size();
            // The stretches between corners, as indexes of their ends; a
            // closed stroke's from its first corner round to it again.
            std::vector<std::pair<size_t, size_t>> stretches;
            if (closed)
            {
                for (size_t k = 0; k < corners.size(); ++k)
                    stretches.emplace_back(corners[k], corners[(k + 1) % corners.size()]);
            }
            else
            {
                stretches.emplace_back(0, corners.front());
                for (size_t k = 0; k + 1 < corners.size(); ++k)
                    stretches.emplace_back(corners[k], corners[k + 1]);
                stretches.emplace_back(corners.back(), n - 1);
            }
            const auto isCorner = [&](size_t i) { return std::find(corners.begin(), corners.end(), i) != corners.end(); };
            // How far along the stroke a stretch starts and ends, a closed
            // stroke's last running on past its length.
            const auto span = [&](const std::pair<size_t, size_t>& stretch) {
                const double from = plain.along(stretch.first);
                double to = plain.along(stretch.second);
                if (to <= from)
                    to += length;
                return std::make_pair(from, to);
            };
            // The way the line runs into and out of corner \a i, as units.
            const auto ways = [&](size_t i, P& in, P& out) {
                const P& before = points[i > 0 ? i - 1 : n - 2];
                const P& after = points[i + 1 < n ? i + 1 : 1];
                const double la = std::max(std::hypot(points[i].x - before.x, points[i].y - before.y), 1.0e-12);
                const double lb = std::max(std::hypot(after.x - points[i].x, after.y - points[i].y), 1.0e-12);
                in = {(points[i].x - before.x) / la, (points[i].y - before.y) / la};
                out = {(after.x - points[i].x) / lb, (after.y - points[i].y) / lb};
            };
            if (definition.corner == PaintBrushDefinition::Corner::Centered ||
                definition.corner == PaintBrushDefinition::Corner::Between)
            {
                // One line, its normals mitered at each corner and turning
                // only near it, so the tile there folds round it.
                lines.push_back(std::make_unique<Centerline>(points, closed, std::max(weight * 0.5, 0.5)));
                runs.push_back({lines.back().get()});
                Run& run = runs.back();
                const bool centered = definition.corner == PaintBrushDefinition::Corner::Centered;
                for (const auto& stretch : stretches)
                {
                    auto [from, to] = span(stretch);
                    if (centered)
                    {
                        // A tile across each corner, the stretches between
                        // fitted with the rest.
                        if (isCorner(stretch.first))
                        {
                            run.tiles.push_back({from - tile * 0.5, acrossScale});
                            from += tile * 0.5;
                        }
                        if (isCorner(stretch.second))
                            to -= tile * 0.5;
                    }
                    fit(run.tiles, from, to - from);
                }
            }
            else
            {
                // A line for each stretch, run on straight past a corner far
                // enough to reach its outer point; the tiles there cut along
                // the corner's bisector (Sliced), or left to overlap.
                const bool sliced = definition.corner == PaintBrushDefinition::Corner::Sliced;
                for (const auto& stretch : stretches)
                {
                    std::vector<P> part;
                    size_t i = stretch.first;
                    part.push_back(points[i]);
                    do
                    {
                        i = closed ? (i + 1) % (n - 1) : i + 1;
                        part.push_back(points[i]);
                    } while (i != stretch.second && part.size() <= n);
                    lines.push_back(std::make_unique<Centerline>(part, false));
                    const Centerline& line = *lines.back();
                    if (line.empty())
                        continue;
                    runs.push_back({&line, span(stretch).first});
                    Run& run = runs.back();
                    // How far past the corner the outer side reaches, and
                    // the bisector's line there.
                    const auto reach = [&](size_t corner, bool atStart) {
                        P in, out;
                        ways(corner, in, out);
                        const double cosine = std::clamp(in.x * out.x + in.y * out.y, -1.0, 1.0);
                        const double half = std::acos(cosine) * 0.5;
                        const double past = std::min(weight * 0.5 * std::tan(half), weight * 2.0);
                        if (sliced)
                        {
                            P toward{in.x + out.x, in.y + out.y};
                            const double size = std::hypot(toward.x, toward.y);
                            if (size > 1.0e-9)
                            {
                                toward = {toward.x / size, toward.y / size};
                                if (atStart)
                                    toward = {-toward.x, -toward.y};
                                run.cuts.emplace_back(points[corner], toward);
                            }
                        }
                        return past;
                    };
                    const double before = isCorner(stretch.first) ? reach(stretch.first, true) : 0.0;
                    const double after = isCorner(stretch.second) ? reach(stretch.second, false) : 0.0;
                    fit(run.tiles, -before, line.length() + before + after);
                }
            }
        }
    }
    else
    {
        // Folds adjusted: the art bends round a sharp turn, its normals
        // taken over a reach of its weight; else mitered at each joint.
        if (definition.foldAdjust)
        {
            lines.push_back(std::make_unique<Centerline>(centerline, closed, std::max(weight * 0.25, 0.25), weight));
            runs.push_back({lines.back().get()});
        }
        else
        {
            runs.push_back({&plain});
        }
        Run& run = runs.back();
        if (definition.scale == PaintBrushDefinition::Scale::Proportional)
        {
            acrossScale = length / A;
            run.tiles.push_back({0.0, acrossScale});
        }
        else if (definition.scale == PaintBrushDefinition::Scale::BetweenGuides &&
                 (definition.leftGuide > 0.0 || definition.rightGuide < 1.0))
        {
            guided = true;
            guideLeft = definition.leftGuide * A;
            guideRight = definition.rightGuide * A;
            head = guideLeft * acrossScale;
            tail = (A - guideRight) * acrossScale;
            middle = length - head - tail;
            if (middle < 0.0)
            {
                // Too short for the ends: everything shrinks along the stroke.
                const double squeeze = length / (head + tail);
                head *= squeeze;
                tail *= squeeze;
                middle = 0.0;
            }
            run.tiles.push_back({0.0, 1.0});
        }
        else
        {
            run.tiles.push_back({0.0, length / A});
        }
    }

    const auto along = [&](const Tile& tile, double a) {
        if (!guided)
            return tile.start + a * tile.alongScale;
        if (a <= guideLeft)
            return guideLeft > 0.0 ? a / guideLeft * head : 0.0;
        if (a >= guideRight)
            return guideRight < A ? length - (A - a) / (A - guideRight) * tail : length;
        return head + (a - guideLeft) / std::max(guideRight - guideLeft, 1.0e-12) * middle;
    };
    const double middleAcross = C * 0.5;
    // How far out from the line a point of the art goes: half the art's
    // height either side, or with a width profile its reach to the left
    // and the right there.
    const auto across = [&](const Run& run, double s, double y) {
        if (profile.empty())
            return (y - middleAcross) * acrossScale;
        double left = 0.5, right = 0.5;
        reachAt(profile, (run.offset + s) / length, definition.minimumWidth, definition.maximumWidth, left, right);
        // The line's normal points to its right as the profile has it.
        return (y - middleAcross) * acrossScale * (left + right) + (right - left) * 0.5 * C * acrossScale;
    };

    // The color: gray art takes the stroke's, its tints for the lighter
    // grays; art in colors keeps them. The stroke's alpha fades either.
    const auto colored = [&](const PaintBrushArt::Color& own) {
        PaintBrushArt::Color out = own;
        if (art.tinted)
        {
            const double light = (own[0] + own[1] + own[2]) / (3.0 * 255.0);
            for (int k = 0; k < 3; ++k)
                out[k] = static_cast<uint8_t>(std::lround(color[k] + (255.0 - color[k]) * light));
        }
        out[3] = static_cast<uint8_t>(std::lround(own[3] * color[3] / 255.0));
        return out;
    };

    // Each edge of the art cut finely enough to follow the stroke's bends.
    constexpr double kStep = 2.0;
    for (const PaintBrushArt::Piece& piece : art.pieces)
    {
        PaintBrushArt::Piece out;
        out.evenOdd = piece.evenOdd;
        out.color = colored(piece.color);
        for (const Run& run : runs)
        {
            for (const Tile& tile : run.tiles)
            {
                for (const std::vector<P>& ring : piece.rings)
                {
                    std::vector<P> warped;
                    warped.reserve(ring.size() * 2);
                    const size_t n = ring.size();
                    for (size_t i = 0; i < n; ++i)
                    {
                        const P a = turned(ring[i]);
                        const P b = turned(ring[(i + 1) % n]);
                        const double sa = along(tile, a.x);
                        const double sb = along(tile, b.x);
                        const int pieces = std::clamp(static_cast<int>(std::ceil(std::fabs(sb - sa) / kStep)), 1, 256);
                        for (int k = 0; k < pieces; ++k)
                        {
                            const double t = static_cast<double>(k) / pieces;
                            const double x = a.x + (b.x - a.x) * t;
                            const double y = a.y + (b.y - a.y) * t;
                            const double s = along(tile, x);
                            warped.push_back(run.line->at(s, across(run, s, y)));
                        }
                    }
                    for (const auto& [through, toward] : run.cuts)
                        warped = clipped(warped, through, toward);
                    if (warped.size() >= 3)
                        out.rings.push_back(std::move(warped));
                }
            }
        }
        laid.push_back(std::move(out));
    }
    return laid;
}

std::vector<StrokeStyle::WidthMarker> paintBrushWidthProfile(
    const PaintBrushDefinition& definition, const std::vector<BrushSample>& samples)
{
    std::vector<StrokeStyle::WidthMarker> profile;
    if (samples.size() < 2)
        return profile;
    bool varies = false;
    std::vector<double> at(samples.size(), 0.0);
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const BrushSample& sample = samples[i];
        if (sample.pressure < 0.999 || sample.tiltX != 0.0 || sample.tiltY != 0.0)
            varies = true;
        if (i > 0)
            at[i] = at[i - 1] + std::hypot(sample.x - samples[i - 1].x, sample.y - samples[i - 1].y);
    }
    const double length = at.back();
    if (!varies || length <= 1.0e-9)
        return profile;
    // The width the pen gives at one reading.
    const auto widthOf = [&](const BrushSample& sample) {
        const double pressure = std::clamp(sample.pressure, 0.0, 1.0);
        double width = definition.minimumWidth + (definition.maximumWidth - definition.minimumWidth) * pressure;
        if (sample.tiltX != 0.0 || sample.tiltY != 0.0)
        {
            const double lean = std::clamp(std::hypot(sample.tiltX, sample.tiltY) / 60.0, 0.0, 1.0);
            width *= definition.maximumTiltWidth + (definition.minimumTiltWidth - definition.maximumTiltWidth) * lean;
        }
        return std::clamp(width, definition.minimumWidth, definition.maximumWidth);
    };
    // A point every so far along, each the readings near it averaged, so a
    // jittery pen gives a steady line; at least the two ends.
    const int count = std::clamp(static_cast<int>(length / 12.0), 1, 24);
    size_t from = 0;
    for (int k = 0; k <= count; ++k)
    {
        const double here = length * k / count;
        const double reach = length / count * 0.5;
        double sum = 0.0;
        int readings = 0;
        while (from < at.size() && at[from] < here - reach)
            ++from;
        for (size_t i = from; i < at.size() && at[i] <= here + reach; ++i)
        {
            sum += widthOf(samples[i]);
            ++readings;
        }
        if (readings == 0)
        {
            // Between two readings far apart: the nearer one.
            size_t nearest = 0;
            for (size_t i = 1; i < at.size(); ++i)
                if (std::fabs(at[i] - here) < std::fabs(at[nearest] - here))
                    nearest = i;
            sum = widthOf(samples[nearest]);
            readings = 1;
        }
        const double width = sum / readings;
        StrokeStyle::WidthMarker marker;
        marker.position = static_cast<double>(k) / count;
        marker.left = marker.right = width * 0.5;
        profile.push_back(marker);
    }
    // Points in a straight run with their neighbors say nothing.
    std::vector<StrokeStyle::WidthMarker> kept{profile.front()};
    for (size_t i = 1; i + 1 < profile.size(); ++i)
    {
        const StrokeStyle::WidthMarker& a = kept.back();
        const StrokeStyle::WidthMarker& b = profile[i + 1];
        const double u = (profile[i].position - a.position) / std::max(b.position - a.position, 1.0e-12);
        if (std::fabs(a.left + (b.left - a.left) * u - profile[i].left) > 0.005)
            kept.push_back(profile[i]);
    }
    kept.push_back(profile.back());
    return kept;
}

} // namespace fla
