#include "path_parser.h"
#include <iostream>
#include <cmath>

namespace {
inline bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f');
}

inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

inline bool isSpace(char c)
{
    //return std::isspace(static_cast<unsigned char>(c));
    return c == '\n' || c == '\r' || c == ' ' || c == '\t';
}
} // namespace

fla::Edge* PathParser::parse(const std::string& data, fla::DOMElement* parent)
{
    fla::Edge* edge = new fla::Edge(parent);

    int pos = 0;

    bool isFirst = true;
    fla::Point firstPoint;
    fla::Point lastPoint;

    fla::Path* lastPath = nullptr;

    while (pos < data.size())
    {
        skipWhitespace(data, pos);

        if (pos >= data.size())
            break;

        char cmd = data[pos];

        if (cmd == '!')
        {
            // Move to command: !x y
            pos++;
            fla::Point point = parsePoint(data, pos);

            // Close previous segment if it exists and endpoints match the start
            if (lastPath && !lastPath->segments.empty())
            {
                fla::Point segmentStart = lastPath->segments[0]->points[0];
                if (std::abs(segmentStart.x - lastPoint.x) < 0.01 &&
                    std::abs(segmentStart.y - lastPoint.y) < 0.01)
                {
                    lastPath->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Close, {}, lastPath});
                }

                // Check if this Move is actually a continuation (starts where we ended)
                if (std::abs(point.x - lastPoint.x) < 0.01 &&
                    std::abs(point.y - lastPoint.y) < 0.01)
                {
                    // This is a continuation - don't create a new path, just continue the current one
                    // No need to add anything since we're already at this point
                }
                else
                {
                    // This is a real move to a new location - start a new Path
                    edge->paths.push_back(new fla::Path(edge));
                    lastPath = edge->paths.back();
                    lastPath->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Move, {point}, lastPath});
                }
            }
            else if (lastPath == nullptr)
            {
                edge->paths.push_back(new fla::Path(edge));
                lastPath = edge->paths.back();
                lastPath->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Move, {point}, lastPath});
            }
            lastPoint = point;
        }
        else if (cmd == '|')
        {
            // Line to command: |x y
            pos++;
            fla::Point point = parsePoint(data, pos);
            if (lastPath)
            {
                lastPath->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Line, {point}, lastPath});
            }
            else
            {
                _errorString = "Path data error: LineTo command without a preceding MoveTo.";
                delete edge;
                return nullptr;
            }
            lastPoint = point;
        }
        else if (cmd == '/')
        {
            // Line to command: /x y
            pos++;
            fla::Point point = parsePoint(data, pos);
            if (lastPath)
            {
                lastPath->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Line, {point}, lastPath});
            }
            else
            {
                _errorString = "Path data error: LineTo command without a preceding MoveTo.";
                delete edge;
                return nullptr;
            }
            lastPoint = point;
        }
        else if (cmd == '[')
        {
            // Quadratic curve: [cx cy x y
            pos++;
            fla::Point control = parsePoint(data, pos);
            fla::Point end = parsePoint(data, pos);

            if (lastPath)
            {
                lastPath->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Quad, {control, end}, lastPath});
            }
            else
            {
                _errorString = "Path data error: QuadTo command without a preceding MoveTo.";
                delete edge;
                return nullptr;
            }

            lastPoint = end;
        }
        else if (cmd == '(')
        {
            // Cubic curve: (x1,y1 x2,y2 x3,y3...)
            pos++;
            if (lastPath)
            {
                int segmentCountBefore = lastPath->segments.size();
                parseCubicCurve(data, pos, lastPath);

                // Update lastPoint to the endpoint of the cubic curve
                if (lastPath->segments.size() > segmentCountBefore)
                {
                    const fla::PathSegment* lastSegment = lastPath->segments.back();
                    if (lastSegment->command == fla::PathSegment::Command::Cubic && lastSegment->points.size() >= 3)
                    {
                        lastPoint = lastSegment->points[2]; // Endpoint is the 3rd point
                    }
                    else if (lastSegment->command == fla::PathSegment::Command::Line && lastSegment->points.size() >= 1)
                    {
                        lastPoint = lastSegment->points[0];
                    }
                }
            }
            else
            {
                _errorString = "Path data error: CubicTo command without a preceding MoveTo.";
                delete edge;
                return nullptr;
            }
        }
        else if (cmd == 'S' || cmd == 's')
        {
            // Style change: S followed by number (e.g., S1)
            pos++;
            int styleIndex = static_cast<int>(parseNumber(data, pos));
            if (lastPath)
            {
                lastPath->styleIndex = styleIndex;
            }
        }
        else if (cmd == 'F' && pos + 1 < data.size() &&
                 (data[pos + 1] == 'S' || data[pos + 1] == 's'))
        {
            // Fill style change: FS followed by number
            pos += 2;
            int fillStyleIndex = static_cast<int>(parseNumber(data, pos));
            if (lastPath)
            {
                lastPath->fillStyleIndex = fillStyleIndex;
            }
        }
        else if (cmd == 'L' && pos + 1 < data.size() &&
                 (data[pos + 1] == 'S' || data[pos + 1] == 's'))
        {
            // Line style change: LS followed by number
            pos += 2;
            int strokeStyleIndex = static_cast<int>(parseNumber(data, pos));
            if (lastPath)
            {
                lastPath->lineStyleIndex = strokeStyleIndex;
            }
        }
        else if (cmd == ';' || cmd == ')')
        {
            // End of cubic curve or separator
            pos++;
        }
        else
        {
            // Skip unknown character
            std::cout << "!!!! UNKNOWN PATH CONTROL CHAR: " << cmd << ": " << pos << std::endl;
            std::cout << data << std::endl;
            break;
        }
    }

    return edge;
}

void PathParser::parseCubicCurve(const std::string& data, int& pos, fla::Path* path)
{
    std::vector<fla::Point> controlPoints;
    fla::Point startPoint = path->segments[0]->points[0]; // Starting point is the last MoveTo point of the current segment
    fla::Point endPoint;
    bool hasExplicitEndpoint = false;

    skipWhitespace(data, pos);

    // Skip leading semicolon if present
    if (pos < data.length() && data[pos] == ';')
    {
        pos++;
    }

    // Parse coordinate pairs until we hit 'q', 'Q', 'p', 'P', or ')'
    while (pos < data.length() && data[pos] != ')' && data[pos] != 'q' && data[pos] != 'Q' && data[pos] != 'p' && data[pos] != 'P')
    {
        skipWhitespace(data, pos);

        if (data[pos] == ';')
        {
            pos++;
            continue;
        }

        if (isDigit(data[pos]) || data[pos] == '-' || data[pos] == '#')
        {
            double x = parseNumber(data, pos);
            skipWhitespace(data, pos);

            // Skip comma separator
            if (pos < data.length() && data[pos] == ',')
            {
                pos++;
                skipWhitespace(data, pos);
            }

            double y = parseNumber(data, pos);
            controlPoints.push_back(fla::Point(x / 20.0, y / 20.0));

            // Skip comma/space after y coordinate
            skipWhitespace(data, pos);
            if (pos < data.length() && data[pos] == ',')
            {
                pos++;
            }
        }
        else
        {
            break;
        }
    }

    // Skip 'q'/'Q' or 'p'/'P' section (reference data)
    if (pos < data.length() && (data[pos] == 'q' || data[pos] == 'Q' || data[pos] == 'p' || data[pos] == 'P'))
    {
        while (pos < data.length() && data[pos] != ')')
        {
            if (data[pos] == 'q' || data[pos] == 'Q' || data[pos] == 'p' || data[pos] == 'P')
            {
                pos++;
                skipWhitespace(data, pos);
            }
            else if (isDigit(data[pos]) || data[pos] == '-' || data[pos] == '#')
            {
                parseNumber(data, pos);
                skipWhitespace(data, pos);
            }
            else
            {
                pos++;
            }
        }
    }

    // Skip closing parenthesis
    if (pos < data.length() && data[pos] == ')')
    {
        pos++;
    }

    // Check if there's an explicit endpoint after ')'
    skipWhitespace(data, pos);

    // If next char is semicolon, no explicit endpoint
    if (pos < data.length() && data[pos] == ';')
    {
        pos++;
        hasExplicitEndpoint = false;
    }
    // Otherwise try to parse endpoint coordinates
    else if (pos < data.length() && (isDigit(data[pos]) || data[pos] == '-' || data[pos] == '#'))
    {
        double x = parseNumber(data, pos);
        skipWhitespace(data, pos);

        if (pos < data.length() && data[pos] == ',')
        {
            pos++;
            skipWhitespace(data, pos);
        }

        double y = parseNumber(data, pos);
        endPoint = fla::Point(x / 20.0, y / 20.0);
        hasExplicitEndpoint = true;

        // Skip trailing semicolon
        if (pos < data.length() && data[pos] == ';')
        {
            pos++;
        }
    }

    // Construct the cubic Bezier curve
    if (hasExplicitEndpoint && controlPoints.size() >= 2)
    {
        // Format: (;cp1 cp2 cp3 q... )endpoint;
        // cp1, cp2 are control points, endpoint is separate
        fla::Point cp1 = controlPoints[0];
        fla::Point cp2 = controlPoints[1];
        path->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Cubic, {cp1, cp2, endPoint}, path});
    }
    else if (!hasExplicitEndpoint && controlPoints.size() >= 3)
    {
        // Format: (;cp1 cp2 endpoint q... );
        // Last point in controlPoints is the endpoint
        fla::Point cp1 = controlPoints[0];
        fla::Point cp2 = controlPoints[1];
        fla::Point ep = controlPoints[2];
        path->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Cubic, {cp1, cp2, ep}, path});
    }
    else if (controlPoints.size() >= 2)
    {
        // Fallback: use first two as control points, last as endpoint
        fla::Point cp1 = controlPoints[0];
        fla::Point cp2 = controlPoints[1];
        fla::Point ep = controlPoints.size() >= 3 ? controlPoints[2] : controlPoints[1];
        path->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Cubic, {cp1, cp2, ep}, path});
    }
    else if (hasExplicitEndpoint)
    {
        // Only explicit endpoint available
        path->segments.push_back(new fla::PathSegment{fla::PathSegment::Command::Line, {endPoint}, path});
    }
}

fla::Point PathParser::parsePoint(const std::string& data, int& pos)
{
    std::string xStr, yStr;
    double x = parseNumber(data, pos, &xStr);
    double y = parseNumber(data, pos, &yStr);
    // Convert from twips to pixels (1 twip = 1/20 pixel)
    return fla::Point(x / 20.0, y / 20.0, xStr, yStr);
}

double PathParser::parseNumber(const std::string& data, int& pos, std::string* outStr)
{
    skipWhitespace(data, pos);

    if (pos >= data.size())
        return 0.0;

    int start = pos;
    bool isHex = false;

    // Check for hex prefix #
    if (data[pos] == '#')
    {
        isHex = true;
        pos++;
        start = pos;
        if (outStr)
            outStr->append("#");
    }

    if (isHex)
    {
        // Parse hexadecimal number
        while (pos < data.size() && isHexDigit(data[pos]))
            pos++;

        std::string hexPart = data.substr(start, pos - start);
        bool ok;
        long long value = std::stoll(hexPart, nullptr, 16);

        // Handle two's complement negative numbers
        // Determine bit width based on number of hex digits
        int hexDigits = hexPart.length();
        if (hexDigits > 0)
        {
            // Calculate the bit width (round up to 8, 16, 24, or 32 bits)
            int bitWidth = ((hexDigits + 1) / 2) * 8;
            if (bitWidth > 32)
                bitWidth = 32;

            // Check if high bit is set (indicating negative in two's complement)
            long long maxPositive = 1LL << (bitWidth - 1);
            long long maxValue = 1LL << bitWidth;

            if (value >= maxPositive)
            {
                // Convert from two's complement to negative
                value = value - maxValue;
            }
        }

        double result = static_cast<double>(value);

        // Check for decimal part (e.g., #1608.53)
        if (pos < data.size() && data[pos] == '.')
        {
            pos++;
            int decStart = pos;
            while (pos < data.size() && isHexDigit(data[pos]))
                pos++;

            std::string decPart = data.substr(decStart, pos - decStart);
            if (!decPart.empty())
            {
                double decValue = std::stoll(decPart, nullptr, 16);
                int decDigits = pos - decStart;
                result += decValue / pow(16.0, decDigits);
            }
        }

        if (outStr)
            outStr->append(data.substr(start, pos - start));

        return result;
    }
    else
    {
        // Handle negative sign for decimal numbers
        if (data[pos] == '-')
            pos++;

        // Parse decimal number
        while (pos < data.size() && isDigit(data[pos]))
        {
            pos++;
        }

        if (pos < data.size() && data[pos] == '.')
        {
            pos++;
            while (pos < data.size() && isDigit(data[pos]))
            {
                pos++;
            }
        }

        std::string numStr = data.substr(start, pos - start);

        if (outStr)
        {
            outStr->append(numStr);
        }

        return std::stod(numStr);
    }
}

void PathParser::skipWhitespace(const std::string& data, int& pos)
{
    while (pos < data.size() && isSpace(data[pos]))
    {
        pos++;
    }
}
