#include "paint_brushes.h"

#include "vector_import.h"

#include "../data/document.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

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
    Centerline(const std::vector<P>& points, bool closed)
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
            return;
        if (_closed)
            _points.push_back(_points.front());
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
            P sum{before.x + after.x, before.y + after.y};
            const double length = std::hypot(sum.x, sum.y);
            if (length < 1.0e-6)
            {
                _normals[i] = after;
                continue;
            }
            sum = {sum.x / length, sum.y / length};
            // Mitred, so the art keeps its width round a bend, up to twice.
            const double cosine = std::max(sum.x * after.x + sum.y * after.y, 0.5);
            _normals[i] = {sum.x / cosine, sum.y / cosine};
        }
    }

    double length() const { return _at.empty() ? 0.0 : _at.back(); }

    /// The point \a along the line and \a out to its left.
    P at(double along, double out) const
    {
        const double total = length();
        if (_closed && total > 0.0)
        {
            along = std::fmod(along, total);
            if (along < 0.0)
                along += total;
        }
        size_t i = 0;
        double t = 0.0;
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
        const P& a = _points[i];
        const P& b = _points[i + 1];
        const double w = std::clamp(t, 0.0, 1.0);
        const P& na = _normals[i];
        const P& nb = _normals[i + 1];
        const P normal{na.x + (nb.x - na.x) * w, na.y + (nb.y - na.y) * w};
        return {a.x + (b.x - a.x) * t + normal.x * out, a.y + (b.y - a.y) * t + normal.y * out};
    }

private:
    bool _closed = false;
    std::vector<P> _points;
    std::vector<double> _at;
    std::vector<P> _normals;
};

} // namespace

// ---------------------------------------------------------------- reading

PaintBrushDefinition paintBrushDefinition(int index, const std::string& json)
{
    PaintBrushDefinition definition;
    definition.index = index;
    definition.params = flatJson(json);
    const auto& params = definition.params;
    const auto name = params.find("name");
    if (name != params.end())
        definition.name = name->second;
    const auto type = params.find("FlashPaintBrushType");
    definition.pattern = type != params.end() && type->second == "PatternBrush";
    if (definition.pattern)
    {
        definition.flipAlong = flagOf(params, "FlipAlong");
        definition.flipAcross = flagOf(params, "FlipAcross");
        const int fit = static_cast<int>(numberOf(params, "adjustment", 0.0));
        definition.fit = fit == 1 ? PaintBrushDefinition::Fit::AddSpace
            : fit == 2           ? PaintBrushDefinition::Fit::Approximate
                                 : PaintBrushDefinition::Fit::Stretch;
        definition.spacing = std::max(numberOf(params, "spacing", 0.0), 0.0) / 100.0;
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
    }
    return definition;
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

// ---------------------------------------------------------------- drawing

std::vector<PaintBrushArt::Piece> layPaintBrush(const PaintBrushDefinition& definition, const PaintBrushArt& art,
    const std::vector<P>& centerline, bool closed, double weight, const PaintBrushArt::Color& color)
{
    std::vector<PaintBrushArt::Piece> laid;
    const Centerline line(centerline, closed);
    const double length = line.length();
    if (length <= 1.0e-9 || art.width <= 0.0 || art.height <= 0.0 || art.pieces.empty())
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

    // How far along the stroke each tile starts, and how the art's length
    // and height scale onto it.
    struct Tile
    {
        double start = 0.0;
        double alongScale = 1.0;
    };
    std::vector<Tile> tiles;
    double acrossScale = weight / C;
    // For an art brush stretched between guides: the guides, and the
    // lengths on the stroke of the ends past them.
    bool guided = false;
    double guideLeft = 0.0, guideRight = A, head = 0.0, middle = length, tail = 0.0;
    if (definition.pattern)
    {
        const double tile = A * acrossScale;
        const double pitch = tile * (1.0 + definition.spacing);
        int count = std::max(1, static_cast<int>(std::lround(length / pitch)));
        if (definition.fit == PaintBrushDefinition::Fit::AddSpace)
        {
            count = std::max(1, static_cast<int>(std::floor(length / pitch)));
            const double gap = (length - count * tile) / count;
            for (int i = 0; i < count; ++i)
                tiles.push_back({i * (tile + gap) + gap * 0.5, acrossScale});
        }
        else
        {
            const double stretch = length / (count * pitch);
            for (int i = 0; i < count; ++i)
                tiles.push_back({i * pitch * stretch, acrossScale * stretch});
        }
    }
    else if (definition.scale == PaintBrushDefinition::Scale::Proportional)
    {
        acrossScale = length / A;
        tiles.push_back({0.0, acrossScale});
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
        tiles.push_back({0.0, 1.0});
    }
    else
    {
        tiles.push_back({0.0, length / A});
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
        for (const Tile& tile : tiles)
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
                        warped.push_back(line.at(along(tile, x), (y - middleAcross) * acrossScale));
                    }
                }
                out.rings.push_back(std::move(warped));
            }
        }
        laid.push_back(std::move(out));
    }
    return laid;
}

} // namespace fla
