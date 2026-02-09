#pragma once

#include "../data/point.h"
#include "../data/edge.h"
#include <string>

class PathParser
{
public:
    Edge* parse(const std::string& data, bool isCubic);

    const std::string& errorString() const
    {
        return _errorString;
    }

private:
    std::string _errorString;

    static void parseCubicCurve(const std::string& data, int& pos, PathSegment* path);
    //static void parseCubicCurve(const std::string& data, int& pos, std::vector<Point>& points);

    static Point parsePoint(const std::string& data, int& pos);

    static double parseNumber(const std::string& data, int& pos);

    static void skipWhitespace(const std::string& data, int& pos);
};
