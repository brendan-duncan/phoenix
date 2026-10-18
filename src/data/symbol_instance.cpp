#include "symbol_instance.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla {

namespace {

/// The shortest of the usual precisions that reads back as \a value.
std::string shortest(double value)
{
    if (!std::isfinite(value))
        return "0";
    char buffer[64];
    for (int precision = 6; precision <= 17; ++precision)
    {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value)
            break;
    }
    return buffer;
}

} // namespace

bool SymbolInstance::matrix3D(double values[16]) const
{
    for (const auto& attribute : extraAttributes)
    {
        if (attribute.first != "matrix3D")
            continue;
        const char* text = attribute.second.c_str();
        for (int i = 0; i < 16; ++i)
        {
            char* end = nullptr;
            values[i] = std::strtod(text, &end);
            if (end == text)
                return false;
            text = end;
        }
        return true;
    }
    return false;
}

Transform SymbolInstance::transformOf3D(const double values[16])
{
    return Transform(values[0], values[1], values[4], values[5], values[12] / 20.0, values[13] / 20.0);
}

std::vector<std::pair<std::string, std::string>> SymbolInstance::keptAttributes() const
{
    std::vector<std::pair<std::string, std::string>> kept = extraAttributes;
    // Placed by a 3D matrix: its 2D part follows the instance, its depth
    // and the rest as the file had them.
    double values[16];
    if (matrix3D(values))
    {
        const Transform was = transformOf3D(values);
        const Transform& now = transform;
        const auto near = [](double a, double b) { return std::fabs(a - b) < 1.0e-9; };
        if (!near(was.m11, now.m11) || !near(was.m12, now.m12) || !near(was.m21, now.m21) ||
            !near(was.m22, now.m22) || !near(was.tx, now.tx) || !near(was.ty, now.ty))
        {
            values[0] = now.m11;
            values[1] = now.m12;
            values[4] = now.m21;
            values[5] = now.m22;
            values[12] = std::round(now.tx * 20.0 * 1.0e6) / 1.0e6;
            values[13] = std::round(now.ty * 20.0 * 1.0e6) / 1.0e6;
            std::string text;
            for (int i = 0; i < 16; ++i)
                text += (i ? " " : "") + shortest(values[i]);
            for (auto& attribute : kept)
                if (attribute.first == "matrix3D")
                    attribute.second = text;
        }
    }
    const Transform& t = transform;
    const Transform& r = readMatrix;
    if (!hasReadMatrix || (r.m11 == t.m11 && r.m12 == t.m12 && r.m21 == t.m21 && r.m22 == t.m22 &&
                              r.tx == t.tx && r.ty == t.ty))
        return kept;
    std::string* xText = nullptr;
    std::string* yText = nullptr;
    for (auto& attribute : kept)
    {
        if (attribute.first == "centerPoint3DX")
            xText = &attribute.second;
        else if (attribute.first == "centerPoint3DY")
            yText = &attribute.second;
    }
    if (!xText || !yText)
        return kept;
    // Through the matrix it was read with back to the symbol's space, and
    // out through the one it has now. Flash matrices map (u, v) to
    // (a u + c v + tx, b u + d v + ty).
    const Transform& was = readMatrix;
    const double det = was.m11 * was.m22 - was.m12 * was.m21;
    if (std::fabs(det) < 1e-12)
        return kept;
    const double x = std::strtod(xText->c_str(), nullptr) - was.tx;
    const double y = std::strtod(yText->c_str(), nullptr) - was.ty;
    const double u = (was.m22 * x - was.m21 * y) / det;
    const double v = (-was.m12 * x + was.m11 * y) / det;
    *xText = shortest(transform.m11 * u + transform.m21 * v + transform.tx);
    *yText = shortest(transform.m12 * u + transform.m22 * v + transform.ty);
    return kept;
}

} // namespace fla
