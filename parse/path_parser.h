#pragma once

#include "../data/point.h"
#include "../data/path.h"
#include <string>

class PathParser
{
public:
    Path* parse(const std::string& edgeData);

    const std::string& errorString() const
    {
        return _errorString;
    }

private:
    std::string _errorString;

    static Point parsePoint(const std::string& data, int& pos);

    static double parseNumber(const std::string& data, int& pos);

    static void skipWhitespace(const std::string& data, int& pos);
};
