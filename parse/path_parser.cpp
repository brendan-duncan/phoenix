#include "path_parser.h"
#include <iostream>

Path* PathParser::parse(const std::string& data)
{
    Path* path = new Path();

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

                path->segments.push_back({});
                lastSegment = &path->segments.back();

                lastSegment->sections.push_back({PathSection::Command::MoveTo, {point}});

                lastPoint = point;
            }
        }
        else if (cmd == '[')
        {
            // Quadratic curve: [cx cy x y
            pos++;
            Point control = parsePoint(data, pos);
            Point end = parsePoint(data, pos);

            if (lastSegment)
            {
                lastSegment->sections.push_back({PathSection::Command::QuadTo, {control, end}});
            }
            else
            {
                _errorString = "Path data error: QuadTo command without a preceding MoveTo.";
                delete path;
                return nullptr;
            }

            lastPoint = end;
        }
        else if (cmd == '|')
        {
            // Line to command: |x y
            pos++;
            Point point = parsePoint(data, pos);
            if (lastSegment)
            {
                lastSegment->sections.push_back({PathSection::Command::LineTo, {point}});
            }
            else
            {
                _errorString = "Path data error: LineTo command without a preceding MoveTo.";
                delete path;
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
                lastSegment->sections.push_back({PathSection::Command::LineTo, {point}});
            }
            else
            {
                _errorString = "Path data error: LineTo command without a preceding MoveTo.";
                delete path;
                return nullptr;
            }
            lastPoint = point;
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

    return path;
}

Point PathParser::parsePoint(const std::string& data, int& pos)
{
    double x = parseNumber(data, pos);
    double y = parseNumber(data, pos);
    // Convert from twips to pixels (1 twip = 1/20 pixel)
    return Point(x / 20.0, y / 20.0);
}

static bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || 
            (c >= 'A' && c <= 'F') || 
            (c >= 'a' && c <= 'f');
}

static bool isDigit(char c)
{
    return (c >= '0' && c <= '9');
}

static bool isSpace(char c)
{
    return std::isspace(static_cast<unsigned char>(c));
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
