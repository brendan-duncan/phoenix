#include "asset_warp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

/// Where the array after "key": starts and ends (its brackets), or false.
bool arrayOf(const std::string& json, const std::string& key, size_t& open, size_t& close)
{
    const size_t at = json.find("\"" + key + "\":[");
    if (at == std::string::npos)
        return false;
    open = json.find('[', at);
    int depth = 0;
    for (size_t i = open; i < json.size(); ++i)
    {
        if (json[i] == '[' || json[i] == '{')
            ++depth;
        else if ((json[i] == ']' || json[i] == '}') && --depth == 0)
        {
            close = i;
            return true;
        }
    }
    return false;
}

/// The objects at depth one of the array between \a open and \a close.
std::vector<std::string> objectsIn(const std::string& json, size_t open, size_t close)
{
    std::vector<std::string> objects;
    size_t at = json.find('{', open);
    while (at != std::string::npos && at < close)
    {
        int depth = 0;
        size_t end = at;
        for (size_t i = at; i < close; ++i)
        {
            if (json[i] == '{')
                ++depth;
            else if (json[i] == '}' && --depth == 0)
            {
                end = i;
                break;
            }
        }
        objects.push_back(json.substr(at, end - at + 1));
        at = json.find('{', end + 1);
    }
    return objects;
}

std::string numberText(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    return buffer;
}

std::string pointText(const Point& point)
{
    return "{\"x\":" + numberText(point.x) + ",\"y\":" + numberText(point.y) + "}";
}

/// \a object with the plain value after "key": (a number or true/false)
/// replaced by \a value, or added at its end when it has none.
std::string withValue(std::string object, const std::string& key, const std::string& value)
{
    const std::string label = "\"" + key + "\":";
    const size_t at = object.find(label);
    if (at == std::string::npos)
    {
        const size_t close = object.rfind('}');
        if (close == std::string::npos)
            return object;
        return object.insert(close, (close > 1 ? "," : "") + label + value);
    }
    const size_t start = at + label.size();
    size_t end = start;
    while (end < object.size() && object[end] != ',' && object[end] != '}')
        ++end;
    return object.replace(start, end - start, value);
}

/// \a object with the object after "key": replaced by \a value.
std::string replaced(std::string object, const std::string& key, const std::string& value)
{
    const size_t at = object.find("\"" + key + "\":{");
    if (at == std::string::npos)
        return object;
    const size_t open = object.find('{', at);
    const size_t close = object.find('}', open);
    return object.replace(open, close - open + 1, value);
}

} // namespace

std::string withPins(const std::string& puppetHandles, const std::vector<WarpPin>& pins)
{
    std::string json = puppetHandles;
    if (json.find("\"puppetPoints\":[") == std::string::npos)
        json = "{\"envelope_bz\":{\"deformed_bz\":[],\"original_bz\":[]},\"puppetMeshDensity\":7,\"puppetPoints\":[],"
               "\"rigTree\":[],\"type\":1}";

    // What was written for each pin read, by ID.
    std::map<int, std::string> written;
    size_t open = 0, close = 0;
    if (arrayOf(json, "puppetPoints", open, close))
    {
        for (const std::string& object : objectsIn(json, open, close))
        {
            double id = 0.0;
            if (number(object, "ID", 0, object.size(), id))
                written[static_cast<int>(id)] = object;
        }
    }
    std::string points;
    std::string tree;
    for (const WarpPin& pin : pins)
    {
        const Point to(std::round(pin.to.x * 20.0), std::round(pin.to.y * 20.0));
        std::string object;
        const auto found = written.find(pin.id);
        if (found != written.end())
        {
            object = replaced(found->second, "newLocation", pointText(to));
            // A turn is kept in turning mode; a pin turned back is left in
            // the mode it had.
            if (pin.rotation != 0.0)
                object = withValue(object, "mode", "2");
            object = withValue(object, "rotationAngle", numberText(pin.rotation));
            object = withValue(object, "pointFreeze", pin.frozen ? "true" : "false");
        }
        else
        {
            object = "{\"ID\":" + std::to_string(pin.id) +
                ",\"boneHandleEnable\":true,\"isPartialBezier\":false,\"mode\":1,\"newEndControlPt\":{\"x\":0,\"y\":0},"
                "\"newLocation\":" + pointText(to) +
                ",\"newStartControlPt\":{\"x\":0,\"y\":0},\"orgEndControlPt\":{\"x\":0,\"y\":0},\"orgLocation\":" +
                pointText(pin.from) +
                ",\"orgStartControlPt\":{\"x\":0,\"y\":0},\"pointFreeze\":false,\"rotationAngle\":0,\"selected\":false}";
            if (pin.rotation != 0.0 || pin.frozen)
            {
                object = withValue(object, "mode", pin.rotation != 0.0 ? "2" : "1");
                object = withValue(object, "rotationAngle", numberText(pin.rotation));
                object = withValue(object, "pointFreeze", pin.frozen ? "true" : "false");
            }
        }
        points += (points.empty() ? "" : ",") + object;
    }
    if (arrayOf(json, "puppetPoints", open, close))
        json.replace(open + 1, close - open - 1, points);

    // The rig tree from the pins' bones, each node's connection kept.
    std::map<int, std::string> connection;
    if (arrayOf(json, "rigTree", open, close))
        for (const std::string& node : objectsIn(json, open, close))
        {
            double id = 0.0;
            const size_t type = node.find("\"connectionType\":\"");
            if (number(node, "ID", 0, node.size(), id) && type != std::string::npos)
            {
                const size_t start = type + 18;
                connection[static_cast<int>(id)] = node.substr(start, node.find('"', start) - start);
            }
        }
    for (const WarpPin& pin : pins)
    {
        std::string children;
        for (const int child : pin.children)
            children += (children.empty() ? "" : ",") + std::to_string(child);
        const auto kind = connection.find(pin.id);
        tree += std::string(tree.empty() ? "" : ",") + "{\"ID\":" + std::to_string(pin.id) + ",\"children\":[" + children +
            "],\"connectionType\":\"" + (kind == connection.end() ? std::string("smooth") : kind->second) + "\"}";
    }
    if (arrayOf(json, "rigTree", open, close))
        json.replace(open + 1, close - open - 1, tree);
    return json;
}

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
            double mode = 1.0;
            double angle = 0.0;
            // Turned only in turning mode (2).
            if (number(json, "mode", at, close, mode) && std::lround(mode) == 2 && number(json, "rotationAngle", at, close, angle))
                pin.rotation = angle;
            const size_t freeze = json.find("\"pointFreeze\":", at);
            pin.frozen = freeze != std::string::npos && freeze < close && json.compare(freeze + 14, 4, "true") == 0;
            result.push_back(pin);
        }
        at = json.find('{', close + 1);
    }
    // The bones: each rig tree entry's children.
    size_t open = 0, shut = 0;
    if (arrayOf(json, "rigTree", open, shut))
    {
        std::map<int, WarpPin*> byId;
        for (WarpPin& pin : result)
            byId[pin.id] = &pin;
        for (const std::string& node : objectsIn(json, open, shut))
        {
            double nodeId = 0.0;
            if (!number(node, "ID", 0, node.size(), nodeId))
                continue;
            const auto parent = byId.find(static_cast<int>(nodeId));
            const size_t list = node.find("\"children\":[");
            if (parent == byId.end() || list == std::string::npos)
                continue;
            // Children are IDs, or objects with an ID, in the list.
            const size_t start = node.find('[', list);
            int depth = 0;
            size_t end = start;
            for (size_t i = start; i < node.size(); ++i)
            {
                if (node[i] == '[' || node[i] == '{')
                    ++depth;
                else if ((node[i] == ']' || node[i] == '}') && --depth == 0)
                {
                    end = i;
                    break;
                }
            }
            const std::string children = node.substr(start + 1, end - start - 1);
            if (children.find('{') != std::string::npos)
            {
                for (const std::string& child : objectsIn(children, 0, children.size()))
                {
                    double childId = 0.0;
                    if (number(child, "ID", 0, child.size(), childId) && byId.count(static_cast<int>(childId)))
                        parent->second->children.push_back(static_cast<int>(childId));
                }
            }
            else
            {
                const char* text = children.c_str();
                char* stop = nullptr;
                for (;;)
                {
                    while (*text == ',' || *text == ' ')
                        ++text;
                    const double childId = std::strtod(text, &stop);
                    if (stop == text)
                        break;
                    if (byId.count(static_cast<int>(childId)))
                        parent->second->children.push_back(static_cast<int>(childId));
                    text = stop;
                }
            }
        }
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
        // The short way round.
        double turn = found->second->rotation - pin.rotation;
        turn = std::remainder(turn, 2.0 * 3.14159265358979323846);
        pin.rotation += turn * t;
    }
    return result;
}

namespace {

/// The handles the warp pulls on: each pin, two close beside a turned or
/// frozen pin to hold the art round it at its angle, and a few along each
/// bone to keep the art there on the line between its pins.
std::vector<WarpPin> handlesOf(const std::vector<WarpPin>& pins)
{
    bool plain = true;
    for (const WarpPin& pin : pins)
        plain = plain && pin.rotation == 0.0 && !pin.frozen && pin.children.empty();
    if (plain)
        return pins;
    std::vector<WarpPin> handles = pins;
    std::map<int, const WarpPin*> byId;
    for (const WarpPin& pin : pins)
        byId[pin.id] = &pin;
    constexpr double kNear = 6.0;
    for (const WarpPin& pin : pins)
    {
        if (pin.rotation != 0.0 || pin.frozen)
        {
            // A frozen pin keeps its turn; the art about it no other.
            const double c = std::cos(pin.rotation);
            const double s = std::sin(pin.rotation);
            for (const Point& offset : {Point(kNear, 0.0), Point(0.0, kNear)})
            {
                WarpPin side;
                side.id = -1;
                side.from = Point(pin.from.x + offset.x, pin.from.y + offset.y);
                side.to = Point(pin.to.x + offset.x * c - offset.y * s, pin.to.y + offset.x * s + offset.y * c);
                handles.push_back(side);
            }
        }
        for (const int child : pin.children)
        {
            const auto other = byId.find(child);
            if (other == byId.end())
                continue;
            for (const double u : {0.25, 0.5, 0.75})
            {
                WarpPin along;
                along.id = -1;
                along.from = Point(pin.from.x + (other->second->from.x - pin.from.x) * u,
                    pin.from.y + (other->second->from.y - pin.from.y) * u);
                along.to = Point(pin.to.x + (other->second->to.x - pin.to.x) * u, pin.to.y + (other->second->to.y - pin.to.y) * u);
                handles.push_back(along);
            }
        }
    }
    return handles;
}

} // namespace

Point warp(const std::vector<WarpPin>& given, const Point& point)
{
    if (given.empty())
        return point;
    const std::vector<WarpPin> pins = handlesOf(given);
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
