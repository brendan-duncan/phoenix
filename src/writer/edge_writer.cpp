#include "edge_writer.h"

#include <cmath>

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

} // namespace

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

void EdgeWriter::writePath(const Path& path, std::string& out)
{
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
            }
            if (!stylesWritten)
                writeStyles();
            break;

        case PathSegment::Command::Line:
            if (segment->points.size() >= 1)
            {
                out.push_back('|');
                appendPoint(segment->points[0], out);
            }
            break;

        case PathSegment::Command::Quad:
            if (segment->points.size() >= 2)
            {
                out.push_back('[');
                appendPoint(segment->points[0], out);
                out.push_back(' ');
                appendPoint(segment->points[1], out);
            }
            break;

        case PathSegment::Command::Cubic:
            if (segment->points.size() >= 3)
            {
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
