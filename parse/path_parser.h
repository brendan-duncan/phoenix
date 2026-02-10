#pragma once

#include "../data/point.h"
#include "../data/edge.h"
#include <string>

class PathParser
{
public:
    fla::Edge* parse(const std::string& data);

    const std::string& errorString() const
    {
        return _errorString;
    }

private:
    std::string _errorString;

    static void parseCubicCurve(const std::string& data, int& pos, fla::PathSegment* path);
    //static void parseCubicCurve(const std::string& data, int& pos, std::vector<fla::Point>& points);

    static fla::Point parsePoint(const std::string& data, int& pos);

    static double parseNumber(const std::string& data, int& pos);

    static void skipWhitespace(const std::string& data, int& pos);
};
