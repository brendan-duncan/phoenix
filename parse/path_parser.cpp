#include "path_parser.h"
#include <iostream>

namespace {
bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || 
            (c >= 'A' && c <= 'F') || 
            (c >= 'a' && c <= 'f');
}

bool isDigit(char c)
{
    return (c >= '0' && c <= '9');
}

bool isSpace(char c)
{
    return std::isspace(static_cast<unsigned char>(c));
}
} // namespace

Edge* PathParser::parse(const std::string& data, bool isCubic)
{
    Edge* edge = new Edge();

    int pos = 0;

    bool isFirst = true;
    Point firstPoint;
    Point lastPoint;

    PathSegment* lastSegment = nullptr;

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
            Point point = parsePoint(data, pos);
            if (isFirst || point.x != lastPoint.x || point.y != lastPoint.y)
            {
                if (isFirst)
                    firstPoint = point;
                isFirst = false;

                edge->segments.push_back({});
                lastSegment = &edge->segments.back();

                lastSegment->sections.push_back({PathSection::Command::Move, {point}});

                lastPoint = point;
            }
        }
        else if (cmd == '|')
        {
            // Line to command: |x y
            pos++;
            Point point = parsePoint(data, pos);
            if (lastSegment)
            {
                lastSegment->sections.push_back({PathSection::Command::Line, {point}});
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
            Point point = parsePoint(data, pos);
            if (lastSegment)
            {
                lastSegment->sections.push_back({PathSection::Command::Line, {point}});
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
            Point control = parsePoint(data, pos);
            Point end = parsePoint(data, pos);

            if (lastSegment)
            {
                lastSegment->sections.push_back({PathSection::Command::Quad, {control, end}});
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
            //std::vector<Point> cubicPoints;
            //parseCubicCurve(data, pos, cubicPoints);
            parseCubicCurve(data, pos, lastSegment);
            /*if (cubicPoints.size() >= 3)
            {
                if (lastSegment)
                {
                    lastSegment->sections.push_back({PathSection::Command::Cubic, {cubicPoints[0], cubicPoints[1], cubicPoints[2]}});
                    for (int i = 3; i + 2 < cubicPoints.size(); i += 3)
                    {
                        lastSegment->sections.push_back({PathSection::Command::Cubic, {cubicPoints[i], cubicPoints[i + 1], cubicPoints[i + 2]}});
                    }
                }
                else
                {
                    _errorString = "Path data error: CubicTo command without a preceding MoveTo.";
                    delete edge;
                    return nullptr;
                }
                lastPoint = cubicPoints.back();
            }*/
        }
        else if (cmd == 'S' || cmd == 's')
        {
            // Style change: S followed by number (e.g., S1)
            pos++;
            int styleIndex = static_cast<int>(parseNumber(data, pos));
            if (lastSegment)
            {
                lastSegment->styleIndex = styleIndex;
            }
        }
        else if (cmd == 'F' && pos + 1 < data.size() &&
                 (data[pos + 1] == 'S' || data[pos + 1] == 's'))
        {
            // Fill style change: FS followed by number
            pos += 2;
            int fillStyleIndex = static_cast<int>(parseNumber(data, pos));
            if (lastSegment)
            {
                lastSegment->fillStyleIndex = fillStyleIndex;
            }
        }
        else if (cmd == 'L' && pos + 1 < data.size() &&
                 (data[pos + 1] == 'S' || data[pos + 1] == 's'))
        {
            // Line style change: LS followed by number
            pos += 2;
            int strokeStyleIndex = static_cast<int>(parseNumber(data, pos));
            if (lastSegment)
            {
                lastSegment->lineStyleIndex = strokeStyleIndex;
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

    if (firstPoint.x == lastPoint.x && firstPoint.y == lastPoint.y)
    {
        if (lastSegment)
        {
            lastSegment->sections.push_back({PathSection::Command::Close, {}});
        }
    }

    return edge;
}

void PathParser::parseCubicCurve(const std::string& data, int& pos, PathSegment* path)
{
    std::vector<Point> controlPoints;
    Point startPoint = path->sections[0].points[0]; // Starting point is the last MoveTo point of the current segment
    Point endPoint;
    bool hasExplicitEndpoint = false;

    skipWhitespace(data, pos);

    // Skip leading semicolon if present
    if (pos < data.length() && data[pos] == ';')
    {
        pos++;
    }

    // Parse coordinate pairs until we hit 'q', 'Q', or ')'
    while (pos < data.length() && data[pos] != ')' && data[pos] != 'q' && data[pos] != 'Q')
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
            controlPoints.push_back(Point(x / 20.0, y / 20.0));

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

    // Skip 'q' or 'Q' section (reference data)
    if (pos < data.length() && (data[pos] == 'q' || data[pos] == 'Q'))
    {
        while (pos < data.length() && data[pos] != ')')
        {
            if (data[pos] == 'q' || data[pos] == 'Q')
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
        endPoint = Point(x / 20.0, y / 20.0);
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
        Point cp1 = controlPoints[0];
        Point cp2 = controlPoints[1];
        path->sections.push_back({PathSection::Command::Cubic, {cp1, cp2, endPoint}});
    }
    else if (!hasExplicitEndpoint && controlPoints.size() >= 3)
    {
        // Format: (;cp1 cp2 endpoint q... );
        // Last point in controlPoints is the endpoint
        Point cp1 = controlPoints[0];
        Point cp2 = controlPoints[1];
        Point ep = controlPoints[2];
        path->sections.push_back({PathSection::Command::Cubic, {cp1, cp2, ep}});
    }
    else if (controlPoints.size() >= 2)
    {
        // Fallback: use first two as control points, last as endpoint
        Point cp1 = controlPoints[0];
        Point cp2 = controlPoints[1];
        Point ep = controlPoints.size() >= 3 ? controlPoints[2] : controlPoints[1];
        path->sections.push_back({PathSection::Command::Cubic, {cp1, cp2, ep}});
    }
    else if (hasExplicitEndpoint)
    {
        // Only explicit endpoint available
        path->sections.push_back({PathSection::Command::Line, {endPoint}});
    }
}

/*void PathParser::parseCubicCurve(const std::string& data, int& pos, std::vector<Point>& points)
{
    // Parse cubic curve notation: (;x1,y1 x2,y2 x3,y3...);
    // This is a complex format that may contain multiple points

    while (pos < data.length() && data[pos] != ')')
    {
        skipWhitespace(data, pos);

        if (data[pos] == ';')
        {
            pos++;
            continue;
        }

        if (data[pos] == 'q' || data[pos] == 'Q')
        {
            pos++;
            Point point = parsePoint(data, pos);
            points.push_back(point);
            continue;
        }

        // Parse comma-separated coordinates
        if (isDigit(data[pos]) || data[pos] == '-' || data[pos] == '#')
        {
            double x = parseNumber(data, pos);
            skipWhitespace(data, pos);

            if (pos < data.length() && data[pos] == ',')
            {
                pos++;
                skipWhitespace(data, pos);
            }

            double y = parseNumber(data, pos);
            points.push_back(Point(x / 20.0, y / 20.0));

            // Skip optional comma
            skipWhitespace(data, pos);
            if (pos < data.length() && data[pos] == ',')
            {
                pos++;
            }
        }
        else
        {
            pos++;
        }
    }

    if (pos < data.length() && data[pos] == ')')
    {
        pos++;
    }
}*/

Point PathParser::parsePoint(const std::string& data, int& pos)
{
    double x = parseNumber(data, pos);
    double y = parseNumber(data, pos);
    // Convert from twips to pixels (1 twip = 1/20 pixel)
    return Point(x / 20.0, y / 20.0);
}

double PathParser::parseNumber(const std::string& data, int& pos)
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
    }

    // Handle negative sign
    if (data[pos] == '-')
        pos++;

    if (isHex)
    {
        // Parse hexadecimal number
        while (pos < data.size() && isHexDigit(data[pos]))
            pos++;

        std::string hexPart = data.substr(start, pos - start);
        bool ok;
        double value = std::stoi(hexPart, nullptr, 16);

        // Check for decimal part (e.g., #1608.53)
        if (pos < data.size() && data[pos] == '.')
        {
            pos++;
            int decStart = pos;
            while (pos < data.size() && isHexDigit(data[pos]))
            {
                pos++;
            }

            std::string decPart = data.substr(decStart, pos - decStart);
            if (!decPart.empty())
            {
                double decValue = std::stoi(decPart, nullptr, 16);
                int decDigits = pos - decStart;
                value += decValue / pow(16.0, decDigits);
            }
        }

        return value;
    }
    else
    {
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
