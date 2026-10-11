#include "asset_warp.h"

#include <cmath>
#include <cstdlib>
#include <map>

namespace fla {

namespace AssetWarp {

namespace {

/// The number after "key": in \a text, searching from \a from up to \a end.
bool number(const std::string& text, const std::string& key, size_t from, size_t end, double& value)
{
    const size_t at = text.find("\"" + key + "\":", from);
    if (at == std::string::npos || at >= end)
        return false;
    const char* start = text.c_str() + at + key.size() + 3;
    char* stop = nullptr;
    value = std::strtod(start, &stop);
    return stop != start;
}

/// The x and y of the object after "key": in \a text, between \a from and
/// \a end.
bool location(const std::string& text, const std::string& key, size_t from, size_t end, Point& point)
{
    const size_t at = text.find("\"" + key + "\":", from);
    if (at == std::string::npos || at >= end)
        return false;
    const size_t close = text.find('}', at);
    if (close == std::string::npos || close > end)
        return false;
    return number(text, "x", at, close, point.x) && number(text, "y", at, close, point.y);
}

} // namespace

std::vector<WarpPin> pins(const std::string& json)
{
    std::vector<WarpPin> result;
    const size_t list = json.find("\"puppetPoints\":[");
    if (list == std::string::npos)
        return result;
    // The list ends where its brackets balance.
    size_t end = list;
    int depth = 0;
    for (size_t i = json.find('[', list); i < json.size(); ++i)
    {
        if (json[i] == '[' || json[i] == '{')
            ++depth;
        else if (json[i] == ']' || json[i] == '}')
        {
            if (--depth == 0)
            {
                end = i;
                break;
            }
        }
    }
    // Each pin is an object at depth one of the list.
    size_t at = json.find('{', list);
    while (at != std::string::npos && at < end)
    {
        size_t close = at;
        int nested = 0;
        for (size_t i = at; i < end; ++i)
        {
            if (json[i] == '{')
                ++nested;
            else if (json[i] == '}' && --nested == 0)
            {
                close = i;
                break;
            }
        }
        WarpPin pin;
        double id = 0.0;
        Point to;
        if (number(json, "ID", at, close, id) && location(json, "orgLocation", at, close, pin.from) &&
            location(json, "newLocation", at, close, to))
        {
            pin.id = static_cast<int>(id);
            pin.to = Point(to.x / 20.0, to.y / 20.0);
            result.push_back(pin);
        }
        at = json.find('{', close + 1);
    }
    return result;
}

std::vector<WarpPin> blend(const std::vector<WarpPin>& a, const std::vector<WarpPin>& b, double t)
{
    std::map<int, const WarpPin*> later;
    for (const WarpPin& pin : b)
        later[pin.id] = &pin;
    std::vector<WarpPin> result = a;
    for (WarpPin& pin : result)
    {
        const auto found = later.find(pin.id);
        if (found == later.end())
            continue;
        pin.to.x += (found->second->to.x - pin.to.x) * t;
        pin.to.y += (found->second->to.y - pin.to.y) * t;
    }
    return result;
}

Point warp(const std::vector<WarpPin>& pins, const Point& point)
{
    if (pins.empty())
        return point;
    if (pins.size() == 1)
        return Point(point.x + pins[0].to.x - pins[0].from.x, point.y + pins[0].to.y - pins[0].from.y);

    // Weights fall off with the square of the distance to each pin; on a pin
    // the point goes where the pin went.
    double total = 0.0;
    double px = 0.0, py = 0.0, qx = 0.0, qy = 0.0;
    std::vector<double> weights(pins.size());
    for (size_t i = 0; i < pins.size(); ++i)
    {
        const double dx = pins[i].from.x - point.x;
        const double dy = pins[i].from.y - point.y;
        const double d2 = dx * dx + dy * dy;
        if (d2 < 1.0e-12)
            return pins[i].to;
        weights[i] = 1.0 / d2;
        total += weights[i];
        px += weights[i] * pins[i].from.x;
        py += weights[i] * pins[i].from.y;
        qx += weights[i] * pins[i].to.x;
        qy += weights[i] * pins[i].to.y;
    }
    px /= total;
    py /= total;
    qx /= total;
    qy /= total;

    // The rotation that best takes the pins' spread about their weighted
    // center to where they went, applied to the point's offset from it.
    const double dx = point.x - px;
    const double dy = point.y - py;
    double fx = 0.0;
    double fy = 0.0;
    for (size_t i = 0; i < pins.size(); ++i)
    {
        const double ax = pins[i].from.x - px;
        const double ay = pins[i].from.y - py;
        const double bx = pins[i].to.x - qx;
        const double by = pins[i].to.y - qy;
        const double w = weights[i];
        // q^ * A, with A = w [p^; -p^perp][d; -d^perp]^T.
        const double a00 = w * (ax * dx + ay * dy);
        const double a01 = w * (ax * dy - ay * dx);
        const double a10 = w * (ay * dx - ax * dy);
        const double a11 = w * (ay * dy + ax * dx);
        fx += bx * a00 + by * a10;
        fy += bx * a01 + by * a11;
    }
    const double length = std::hypot(fx, fy);
    const double reach = std::hypot(dx, dy);
    if (length < 1.0e-12)
        return Point(qx + dx, qy + dy);
    return Point(qx + fx / length * reach, qy + fy / length * reach);
}

} // namespace AssetWarp

} // namespace fla
