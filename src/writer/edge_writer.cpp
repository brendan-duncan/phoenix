#include "edge_writer.h"

#include <cmath>
#include <cstdio>

namespace fla {

namespace {

/// Twips are stored with 8 bits of fraction, so this is the smallest
/// representable step.
constexpr long long kFractionScale = 256;

/// Coordinates are signed 24-bit twip values. A hex integer part at or above
/// half of this range reads back as negative, which bounds what the format can
/// represent.
constexpr long long kIntegerRange = 1LL << 24;

void appendHex(long long value, int minDigits, std::string& out)
{
    static const char* kDigits = "0123456789ABCDEF";

    char buffer[16];
    int length = 0;
    while (value > 0 && length < static_cast<int>(sizeof(buffer)))
    {
        buffer[length++] = kDigits[value & 0xF];
        value >>= 4;
    }

    while (length < minDigits)
        buffer[length++] = '0';

    if (length == 0)
        buffer[length++] = '0';

    for (int i = length; i > 0; --i)
        out.push_back(buffer[i - 1]);
}

/// Whole twips, as the `cubics` text writes them.
std::string twips(const Point& point)
{
    return std::to_string(std::llround(point.x * 20.0)) + " " + std::to_string(std::llround(point.y * 20.0));
}

std::string twipsComma(const Point& point)
{
    return std::to_string(std::llround(point.x * 20.0)) + "," + std::to_string(std::llround(point.y * 20.0));
}

Point lerp(const Point& a, const Point& b, double t)
{
    return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

/// Quadratics for the cubic \a p0 to \a p3, as control and end pairs,
/// split in half until each lies within a fortieth of a pixel (half a twip)
/// of the cubic.
void cubicToQuads(const Point& p0, const Point& p1, const Point& p2, const Point& p3,
    std::vector<std::pair<Point, Point>>& quads, int depth = 0)
{
    // How far the midpoint quadratic can stray from the cubic.
    const double dx = p3.x - 3.0 * p2.x + 3.0 * p1.x - p0.x;
    const double dy = p3.y - 3.0 * p2.y + 3.0 * p1.y - p0.y;
    const double error = std::sqrt(3.0) / 36.0 * std::sqrt(dx * dx + dy * dy);
    if (error <= 0.025 || depth >= 8)
    {
        const Point control((3.0 * (p1.x + p2.x) - p0.x - p3.x) / 4.0, (3.0 * (p1.y + p2.y) - p0.y - p3.y) / 4.0);
        quads.emplace_back(control, p3);
        return;
    }
    const Point a = lerp(p0, p1, 0.5), b = lerp(p1, p2, 0.5), c = lerp(p2, p3, 0.5);
    const Point d = lerp(a, b, 0.5), e = lerp(b, c, 0.5);
    const Point mid = lerp(d, e, 0.5);
    cubicToQuads(p0, a, d, mid, quads, depth + 1);
    cubicToQuads(mid, e, c, p3, quads, depth + 1);
}

} // namespace

std::string EdgeWriter::formatMorphCoordinate(double pixels)
{
    // The whole part is twips over 256, written as a 24-bit two's
    // complement integer, and the fraction is the remaining twips as two
    // hex digits (a trailing zero dropped, as the format writes it).
    const long long twips = static_cast<long long>(std::llround(pixels * 20.0));
    long long whole = twips / 256;
    long long fraction = twips % 256;
    if (fraction < 0)
    {
        fraction += 256;
        whole -= 1;
    }
    const long long whole24 = whole < 0 ? whole + (1LL << 24) : whole;
    char buffer[32];
    if (fraction % 16 == 0)
        std::snprintf(buffer, sizeof(buffer), "#%llX.%llX", whole24, fraction / 16);
    else
        std::snprintf(buffer, sizeof(buffer), "#%llX.%02llX", whole24, fraction);
    return buffer;
}

std::string EdgeWriter::formatCoordinate(double pixels)
{
    // Snap to the 1/256-twip grid the format stores, so what is written reads
    // back as the same number.
    const double scaled = std::round(pixels * 20.0 * static_cast<double>(kFractionScale));
    long long fixed = static_cast<long long>(scaled);

    // Split into whole twips plus a non-negative fraction. Truncating division
    // rounds toward zero, which would put the fraction on the wrong side of the
    // value for negative coordinates, so bias it back to a floor.
    long long twips = fixed / kFractionScale;
    long long fraction = fixed % kFractionScale;
    if (fraction < 0)
    {
        fraction += kFractionScale;
        twips -= 1;
    }

    if (fraction == 0)
        return std::to_string(twips);

    // Hex fixed point. Negatives are written as 24-bit two's complement, which
    // is how the reader interprets a value above the signed maximum.
    std::string out("#");
    if (twips < 0)
        appendHex(twips + kIntegerRange, 6, out);
    else
        appendHex(twips, 1, out);

    out.push_back('.');
    appendHex(fraction, 2, out);
    return out;
}

void EdgeWriter::appendPoint(const Point& point, std::string& out)
{
    out += formatCoordinate(point.x);
    out.push_back(' ');
    out += formatCoordinate(point.y);
}

void EdgeWriter::writePath(const Path& path, std::string& out, std::vector<std::string>* cubics)
{
    Point current;
    bool stylesWritten = false;
    const auto writeStyles = [&]()
    {
        stylesWritten = true;
        if (path.styleIndex != -1)
            out += "S" + std::to_string(path.styleIndex);
        if (path.fillStyleIndex != -1)
            out += "FS" + std::to_string(path.fillStyleIndex);
        if (path.lineStyleIndex != -1)
            out += "LS" + std::to_string(path.lineStyleIndex);
    };

    for (const PathSegment* segment : path.segments)
    {
        if (!segment)
            continue;

        // A style select binds to the path the preceding move opened, so it has
        // to follow that move rather than lead it. Written the other way round
        // the reader has no path to attach it to yet and drops it.
        if (!stylesWritten && segment->command != PathSegment::Command::Move)
            writeStyles();

        switch (segment->command)
        {
        case PathSegment::Command::Move:
            if (segment->points.size() >= 1)
            {
                out.push_back('!');
                appendPoint(segment->points[0], out);
                current = segment->points[0];
            }
            if (!stylesWritten)
                writeStyles();
            break;

        case PathSegment::Command::Line:
            if (segment->points.size() >= 1)
            {
                out.push_back('|');
                appendPoint(segment->points[0], out);
                current = segment->points[0];
            }
            break;

        case PathSegment::Command::Quad:
            if (segment->points.size() >= 2)
            {
                out.push_back('[');
                appendPoint(segment->points[0], out);
                out.push_back(' ');
                appendPoint(segment->points[1], out);
                current = segment->points[1];
            }
            break;

        case PathSegment::Command::Cubic:
            if (segment->points.size() >= 3 && cubics)
            {
                // Animate's way: quadratics in the edges, and the cubic with
                // the same quadratics in its own record, "!start(;control
                // control end" then the quadratics as q (on the curve) and Q
                // (control) points, ");".
                std::vector<std::pair<Point, Point>> quads;
                cubicToQuads(current, segment->points[0], segment->points[1], segment->points[2], quads);
                std::string record = "!" + twips(current) + "(;" + twipsComma(segment->points[0]) + " " +
                    twipsComma(segment->points[1]) + " " + twipsComma(segment->points[2]) + "q" + twips(current);
                for (const auto& quad : quads)
                {
                    out.push_back('[');
                    appendPoint(quad.first, out);
                    out.push_back(' ');
                    appendPoint(quad.second, out);
                    record += "Q" + twips(quad.first) + "q" + twips(quad.second);
                }
                record += ");";
                cubics->push_back(record);
                current = segment->points[2];
            }
            else if (segment->points.size() >= 3)
            {
                current = segment->points[2];
                // The reader takes three points before the closing paren as
                // control, control, endpoint. Flash also stores a `q`/`p`
                // quadratic approximation inside the parens, which the reader
                // skips, so it is left out here.
                out.push_back('(');
                appendPoint(segment->points[0], out);
                out.push_back(' ');
                appendPoint(segment->points[1], out);
                out.push_back(' ');
                appendPoint(segment->points[2], out);
                out.push_back(')');
            }
            break;

        case PathSegment::Command::Close:
            // The format has no close command. A closed path simply ends where
            // it started, and the reader infers the close from that.
            break;
        }
    }

    // A path with styles but no segments at all still records them.
    if (!stylesWritten)
        writeStyles();
}

std::string EdgeWriter::writeEdgeAsAnimate(const Edge& edge, std::vector<std::string>& cubics)
{
    std::string out;
    for (const Path* path : edge.paths)
    {
        if (path)
            writePath(*path, out, &cubics);
    }
    return out;
}

std::string EdgeWriter::writeEdge(const Edge& edge)
{
    std::string out;
    for (const Path* path : edge.paths)
    {
        if (path)
            writePath(*path, out);
    }
    return out;
}

} // namespace fla
