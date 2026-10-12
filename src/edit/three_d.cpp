#include "three_d.h"

#include "../data/document.h"
#include "../data/symbol_instance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla {

namespace threed {

namespace {

constexpr double kPi = 3.14159265358979323846;

const char* const kRotationX = "phoenixRotationX";
const char* const kRotationY = "phoenixRotationY";
const char* const kZ = "phoenixZ";

std::string numberText(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

} // namespace

Pose of(const SymbolInstance& instance)
{
    Pose pose;
    for (const auto& attribute : instance.extraAttributes)
    {
        if (attribute.first == kRotationX)
            pose.rotationX = std::atof(attribute.second.c_str());
        else if (attribute.first == kRotationY)
            pose.rotationY = std::atof(attribute.second.c_str());
        else if (attribute.first == kZ)
            pose.z = std::atof(attribute.second.c_str());
    }
    return pose;
}

std::vector<std::pair<std::string, std::string>> withPose(const SymbolInstance& instance, const Pose& pose)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : instance.extraAttributes)
    {
        if (attribute.first != kRotationX && attribute.first != kRotationY && attribute.first != kZ)
            attributes.push_back(attribute);
    }
    if (pose.rotationX != 0.0)
        attributes.emplace_back(kRotationX, numberText(pose.rotationX));
    if (pose.rotationY != 0.0)
        attributes.emplace_back(kRotationY, numberText(pose.rotationY));
    if (pose.z != 0.0)
        attributes.emplace_back(kZ, numberText(pose.z));
    return attributes;
}

Pose blend(const Pose& a, const Pose& b, double t)
{
    Pose pose;
    pose.rotationX = a.rotationX + (b.rotationX - a.rotationX) * t;
    pose.rotationY = a.rotationY + (b.rotationY - a.rotationY) * t;
    pose.z = a.z + (b.z - a.z) * t;
    return pose;
}

double perspectiveAngle(const Document& document)
{
    for (const auto& attribute : document.extraAttributes)
    {
        if (attribute.first == "phoenixPerspectiveAngle")
            return std::clamp(std::atof(attribute.second.c_str()), 1.0, 179.0);
    }
    return 55.0;
}

double focalLength(double angle, double width)
{
    return (std::max(width, 1.0) / 2.0) / std::tan(std::clamp(angle, 1.0, 179.0) * kPi / 360.0);
}

std::array<double, 9> projection(const Pose& pose, const Point& center, const Point& vanishing, double focal)
{
    // A point of the clip, d from the center on the flat plane, turned
    // about the x axis and then the y axis:
    //   x' = d.x cos b + d.y sin a sin b
    //   y' = d.y cos a
    //   z' = -d.x sin b + d.y sin a cos b
    // then pushed z further in and seen from focal in front of the
    // vanishing point: (V + (C + (x', y') - V) f / w), w = f + z + z'.
    const double a = pose.rotationX * kPi / 180.0;
    const double b = pose.rotationY * kPi / 180.0;
    const double f = std::max(focal, 1.0);
    // Rows over (d.x, d.y, 1).
    const std::array<double, 3> x = {std::cos(b), std::sin(a) * std::sin(b), 0.0};
    const std::array<double, 3> y = {0.0, std::cos(a), 0.0};
    const std::array<double, 3> w = {-std::sin(b), std::sin(a) * std::cos(b), f + pose.z};
    std::array<double, 3> nx, ny;
    for (int i = 0; i < 3; ++i)
    {
        nx[i] = f * x[i] + vanishing.x * w[i];
        ny[i] = f * y[i] + vanishing.y * w[i];
    }
    nx[2] += f * (center.x - vanishing.x);
    ny[2] += f * (center.y - vanishing.y);
    // Over the point itself: d = p - C.
    const auto shift = [&center](std::array<double, 3> row) {
        row[2] -= row[0] * center.x + row[1] * center.y;
        return row;
    };
    const std::array<double, 3> rx = shift(nx), ry = shift(ny), rw = shift(w);
    // Scaled so a flat clip at z 0 maps with W = 1.
    std::array<double, 9> map = {rx[0] / f, rx[1] / f, rx[2] / f, ry[0] / f, ry[1] / f, ry[2] / f, rw[0] / f, rw[1] / f,
        rw[2] / f};
    return map;
}

Point project(const std::array<double, 9>& map, const Point& point)
{
    const double X = map[0] * point.x + map[1] * point.y + map[2];
    const double Y = map[3] * point.x + map[4] * point.y + map[5];
    const double W = map[6] * point.x + map[7] * point.y + map[8];
    if (std::fabs(W) < 1.0e-12)
        return point;
    return Point(X / W, Y / W);
}

} // namespace threed

} // namespace fla
