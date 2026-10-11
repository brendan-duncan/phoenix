#include "motion_object.h"

#include "tween_ease.h"

#include "../third_party/tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fla {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// "time,value" as a keyframe's anchor and handles write it.
bool pair(const char* text, double& a, double& b)
{
    if (!text)
        return false;
    char* end = nullptr;
    a = std::strtod(text, &end);
    if (end == text || *end != ',')
        return false;
    const char* second = end + 1;
    b = std::strtod(second, &end);
    return end != second;
}

double cubic(double p0, double p1, double p2, double p3, double s)
{
    const double u = 1.0 - s;
    return u * u * u * p0 + 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s * p3;
}

/// A 2D affine map, x' = a x + c y + tx, y' = b x + d y + ty.
struct Affine
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;

    Affine then(const Affine& next) const
    {
        Affine out;
        out.a = next.a * a + next.c * b;
        out.b = next.b * a + next.d * b;
        out.c = next.a * c + next.c * d;
        out.d = next.b * c + next.d * d;
        out.tx = next.a * tx + next.c * ty + next.tx;
        out.ty = next.b * tx + next.d * ty + next.ty;
        return out;
    }

    bool inverse(Affine& out) const
    {
        const double det = a * d - b * c;
        if (std::fabs(det) < 1.0e-15)
            return false;
        out.a = d / det;
        out.b = -b / det;
        out.c = -c / det;
        out.d = a / det;
        out.tx = -(out.a * tx + out.c * ty);
        out.ty = -(out.b * tx + out.d * ty);
        return true;
    }

    static Affine translation(double x, double y)
    {
        Affine m;
        m.tx = x;
        m.ty = y;
        return m;
    }
};

/// The turn, skew and scale part of a state, as Flash builds a matrix:
/// the x axis turned by skewY + rotation and the y axis by skewX + rotation,
/// each scaled.
Affine linearPart(const MotionState& state)
{
    const double skewX = (state.skewX + state.rotation) * kPi / 180.0;
    const double skewY = (state.skewY + state.rotation) * kPi / 180.0;
    const double sx = state.scaleX / 100.0;
    const double sy = state.scaleY / 100.0;
    Affine m;
    m.a = sx * std::cos(skewY);
    m.b = sx * std::sin(skewY);
    m.c = -sy * std::sin(skewX);
    m.d = sy * std::cos(skewX);
    return m;
}

} // namespace

bool MotionObject::parse(const std::string& xml)
{
    _properties.clear();
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    const tinyxml2::XMLElement* root = document.RootElement();
    if (root && std::strcmp(root->Name(), "motionObjectXML") == 0)
        root = root->FirstChildElement("AnimationCore");
    if (!root || std::strcmp(root->Name(), "AnimationCore") != 0)
        return false;
    _timeScale = std::max(1.0, root->DoubleAttribute("TimeScale", 30000.0));
    _duration = root->DoubleAttribute("duration", 0.0);
    const tinyxml2::XMLElement* timeMap = root->FirstChildElement("TimeMap");
    _ease = timeMap ? timeMap->DoubleAttribute("strength", 0.0) : 0.0;

    // Properties sit in nested containers (Basic_Motion, Transformation,
    // Colors, Filters); every Property found is read.
    std::vector<const tinyxml2::XMLElement*> pending = {root};
    while (!pending.empty())
    {
        const tinyxml2::XMLElement* container = pending.back();
        pending.pop_back();
        for (const tinyxml2::XMLElement* child = container->FirstChildElement(); child;
             child = child->NextSiblingElement())
        {
            if (std::strcmp(child->Name(), "PropertyContainer") == 0)
            {
                pending.push_back(child);
                continue;
            }
            if (std::strcmp(child->Name(), "Property") != 0 || !child->Attribute("id"))
                continue;
            if (child->IntAttribute("enabled", 1) == 0)
                continue;
            std::vector<Key>& keys = _properties[child->Attribute("id")];
            for (const tinyxml2::XMLElement* keyframe = child->FirstChildElement("Keyframe"); keyframe;
                 keyframe = keyframe->NextSiblingElement("Keyframe"))
            {
                Key key;
                key.time = keyframe->DoubleAttribute("timevalue", 0.0);
                double offset = 0.0;
                if (!pair(keyframe->Attribute("anchor"), offset, key.value))
                    continue;
                if (!pair(keyframe->Attribute("next"), key.nextTime, key.nextValue))
                {
                    key.nextTime = 0.0;
                    key.nextValue = key.value;
                }
                if (!pair(keyframe->Attribute("previous"), key.previousTime, key.previousValue))
                {
                    key.previousTime = 0.0;
                    key.previousValue = key.value;
                }
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end(), [](const Key& p, const Key& q) { return p.time < q.time; });
        }
    }
    return true;
}

double MotionObject::frames(double frameRate) const
{
    return _duration * frameRate / _timeScale;
}

double MotionObject::value(const std::string& property, double time, double fallback) const
{
    const auto found = _properties.find(property);
    if (found == _properties.end() || found->second.empty())
        return fallback;
    const std::vector<Key>& keys = found->second;
    if (time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;
    for (size_t i = 0; i + 1 < keys.size(); ++i)
    {
        const Key& from = keys[i];
        const Key& to = keys[i + 1];
        if (time > to.time)
            continue;
        // A cubic Bezier through (time, value) with the handles as offsets
        // in time and values: find where its time reaches the one asked for.
        const double t0 = from.time;
        const double t1 = from.time + from.nextTime;
        const double t2 = to.time + to.previousTime;
        const double t3 = to.time;
        double low = 0.0;
        double high = 1.0;
        for (int step = 0; step < 50; ++step)
        {
            const double mid = (low + high) * 0.5;
            if (cubic(t0, t1, t2, t3, mid) < time)
                low = mid;
            else
                high = mid;
        }
        const double s = (low + high) * 0.5;
        return cubic(from.value, from.nextValue, to.previousValue, to.value, s);
    }
    return keys.back().value;
}

MotionState MotionObject::at(double frame, double frameRate) const
{
    double time = frame * _timeScale / std::max(frameRate, 1.0e-6);
    if (_ease != 0.0 && _duration > 0.0)
    {
        // The TimeMap eases the span as a classic tween's ease does.
        const double t = std::max(0.0, std::min(1.0, time / _duration));
        time = easeProgress(t, static_cast<int>(std::lround(_ease)), {}) * _duration;
    }
    MotionState state;
    state.x = value("Motion_X", time, 0.0);
    state.y = value("Motion_Y", time, 0.0);
    state.rotation = value("Rotation_Z", time, 0.0);
    state.skewX = value("Skew_X", time, 0.0);
    state.skewY = value("Skew_Y", time, 0.0);
    state.scaleX = value("Scale_X", time, 100.0);
    state.scaleY = value("Scale_Y", time, 100.0);
    return state;
}

Transform applyMotion(const Transform& stored, const Point& pivot, const MotionState& start,
    const MotionState& now)
{
    Affine base;
    base.a = stored.m11;
    base.b = stored.m12;
    base.c = stored.m21;
    base.d = stored.m22;
    base.tx = stored.tx;
    base.ty = stored.ty;
    // The pivot where the stored transform puts it.
    const double px = base.a * pivot.x + base.c * pivot.y + base.tx;
    const double py = base.b * pivot.x + base.d * pivot.y + base.ty;

    Affine undo;
    if (!linearPart(start).inverse(undo))
        return stored;
    const Affine moved = base.then(Affine::translation(-px, -py))
                             .then(undo)
                             .then(linearPart(now))
                             .then(Affine::translation(px + now.x - start.x, py + now.y - start.y));
    return Transform(moved.a, moved.b, moved.c, moved.d, moved.tx, moved.ty);
}

} // namespace fla
