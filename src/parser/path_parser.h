#pragma once

#include "../data/point.h"
#include "../data/edge.h"
#include <string>

class PathParser
{
public:
    fla::Edge* parse(const std::string& data, fla::DOMElement* parent);

    const std::string& errorString() const
    {
        return _errorString;
    }

    /// Flash-style number (#hex with optional fractional hex, or decimal). Same as edge path syntax.
    static double parseNumber(const std::string& data, int& pos, std::string* outStr = nullptr);

    static void skipWhitespace(const std::string& data, int& pos);

    /// Morph attribute value: "x, y" (comma-separated), same units as path edges; returns pixels (twips / 20).
    static fla::Point parseMorphPoint(const std::string& attributeValue);

private:
    std::string _errorString;

    static void parseCubicCurve(const std::string& data, int& pos, fla::Path* path);

    static fla::Point parsePoint(const std::string& data, int& pos);
};
