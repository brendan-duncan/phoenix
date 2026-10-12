#include "motion_object.h"

#include "transform_parts.h"
#include "tween_ease.h"

#include "../third_party/tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

std::vector<double> MotionObject::keyTimes() const
{
    std::vector<double> times;
    for (const auto& property : _properties)
    {
        for (const Key& key : property.second)
            times.push_back(key.time);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return times;
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

Transform unapplyMotion(const Transform& shown, const Point& pivot, const MotionState& start,
    const MotionState& now)
{
    // applyMotion's linear part is the stored one, then the start's undone,
    // then now's; its pivot is the stored one moved by the change in offset.
    Affine undoStart;
    Affine undoChange;
    if (!linearPart(start).inverse(undoStart) || !undoStart.then(linearPart(now)).inverse(undoChange))
        return shown;
    Affine linear;
    linear.a = shown.m11;
    linear.b = shown.m12;
    linear.c = shown.m21;
    linear.d = shown.m22;
    const Affine stored = linear.then(undoChange);
    const double shownX = shown.m11 * pivot.x + shown.m21 * pivot.y + shown.tx;
    const double shownY = shown.m12 * pivot.x + shown.m22 * pivot.y + shown.ty;
    const double px = shownX - (now.x - start.x);
    const double py = shownY - (now.y - start.y);
    return Transform(stored.a, stored.b, stored.c, stored.d, px - (stored.a * pivot.x + stored.c * pivot.y),
        py - (stored.b * pivot.x + stored.d * pivot.y));
}

namespace motion {

namespace {

/// The AnimationCore inside a motionObjectXML document.
tinyxml2::XMLElement* coreOf(tinyxml2::XMLDocument& document)
{
    tinyxml2::XMLElement* root = document.RootElement();
    if (root && std::strcmp(root->Name(), "motionObjectXML") == 0)
        root = root->FirstChildElement("AnimationCore");
    return root && std::strcmp(root->Name(), "AnimationCore") == 0 ? root : nullptr;
}

/// Every Property element, however deep its containers.
void properties(tinyxml2::XMLElement* container, std::vector<tinyxml2::XMLElement*>& out)
{
    for (tinyxml2::XMLElement* child = container->FirstChildElement(); child; child = child->NextSiblingElement())
    {
        if (std::strcmp(child->Name(), "Property") == 0)
            out.push_back(child);
        else
            properties(child, out);
    }
}

std::string number(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

/// "time,value" with its time multiplied by \a factor.
std::string scaledPair(const char* text, double factor)
{
    double a = 0.0;
    double b = 0.0;
    if (!pair(text, a, b))
        return text ? text : "";
    return number(a * factor) + "," + number(b);
}

std::string print(const tinyxml2::XMLDocument& document)
{
    tinyxml2::XMLPrinter printer(nullptr, true);
    document.Print(&printer);
    return printer.CStr();
}

/// A keyframe holding \a value at \a time, straight on either side.
tinyxml2::XMLElement* plainKey(tinyxml2::XMLDocument& document, double time, double value)
{
    tinyxml2::XMLElement* key = document.NewElement("Keyframe");
    const std::string anchor = "0," + number(value);
    key->SetAttribute("anchor", anchor.c_str());
    key->SetAttribute("next", anchor.c_str());
    key->SetAttribute("previous", anchor.c_str());
    key->SetAttribute("roving", "0");
    key->SetAttribute("timevalue", number(time).c_str());
    return key;
}

/// A keyframe at \a time put into \a property before \a after (null: at the
/// end), holding \a value. Inside a curved stretch the curve is split there
/// (de Casteljau), so it keeps its shape, and the new keyframe then moves
/// to \a value with its handles.
void insertKey(tinyxml2::XMLDocument& document, tinyxml2::XMLElement* property, tinyxml2::XMLElement* after,
    double time, double value)
{
    tinyxml2::XMLElement* key = plainKey(document, time, value);
    tinyxml2::XMLElement* before = after ? after->PreviousSiblingElement("Keyframe")
                                         : property->LastChildElement("Keyframe");
    if (before)
        property->InsertAfterChild(before, key);
    else
        property->InsertFirstChild(key);
    if (!before || !after)
        return;

    double ignored = 0.0;
    double t0 = before->DoubleAttribute("timevalue", 0.0);
    double t3 = after->DoubleAttribute("timevalue", 0.0);
    double v0 = 0.0;
    double v3 = 0.0;
    pair(before->Attribute("anchor"), ignored, v0);
    pair(after->Attribute("anchor"), ignored, v3);
    double n0 = 0.0, nv = v0, p3 = 0.0, pv = v3;
    if (!pair(before->Attribute("next"), n0, nv))
        nv = v0;
    if (!pair(after->Attribute("previous"), p3, pv))
        pv = v3;
    // Handles at the keyframes: a straight stretch, nothing to keep.
    if (n0 == 0.0 && p3 == 0.0 && nv == v0 && pv == v3)
        return;
    const double t1 = t0 + n0;
    const double t2 = t3 + p3;
    double low = 0.0;
    double high = 1.0;
    for (int step = 0; step < 60; ++step)
    {
        const double mid = (low + high) * 0.5;
        if (cubic(t0, t1, t2, t3, mid) < time)
            low = mid;
        else
            high = mid;
    }
    const double s = (low + high) * 0.5;
    const auto lerp = [s](double a, double b) { return a + (b - a) * s; };
    // The left and right halves' control points, in time and value.
    const double at01 = lerp(t0, t1), av01 = lerp(v0, nv);
    const double at12 = lerp(t1, t2), av12 = lerp(nv, pv);
    const double at23 = lerp(t2, t3), av23 = lerp(pv, v3);
    const double bt0 = lerp(at01, at12), bv0 = lerp(av01, av12);
    const double bt1 = lerp(at12, at23), bv1 = lerp(av12, av23);
    const double mt = lerp(bt0, bt1), mv = lerp(bv0, bv1);
    const double shift = value - mv;
    before->SetAttribute("next", (number(at01 - t0) + "," + number(av01)).c_str());
    after->SetAttribute("previous", (number(at23 - t3) + "," + number(av23)).c_str());
    key->SetAttribute("previous", (number(bt0 - mt) + "," + number(bv0 + shift)).c_str());
    key->SetAttribute("next", (number(bt1 - mt) + "," + number(bv1 + shift)).c_str());
}

} // namespace

bool rescale(std::string& xml, double factor)
{
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS || factor <= 0.0)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    core->SetAttribute("duration", number(core->DoubleAttribute("duration", 0.0) * factor).c_str());
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (tinyxml2::XMLElement* property : found)
    {
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;
             key = key->NextSiblingElement("Keyframe"))
        {
            key->SetAttribute("timevalue", number(key->DoubleAttribute("timevalue", 0.0) * factor).c_str());
            for (const char* handle : {"next", "previous"})
            {
                if (key->Attribute(handle))
                    key->SetAttribute(handle, scaledPair(key->Attribute(handle), factor).c_str());
            }
        }
    }
    xml = print(document);
    return true;
}

bool truncate(std::string& xml, double time)
{
    MotionObject motion;
    tinyxml2::XMLDocument document;
    if (!motion.parse(xml) || document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    core->SetAttribute("duration", number(time).c_str());
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (tinyxml2::XMLElement* property : found)
    {
        const char* id = property->Attribute("id");
        if (!id)
            continue;
        bool past = false;
        bool atEnd = false;
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;)
        {
            tinyxml2::XMLElement* next = key->NextSiblingElement("Keyframe");
            const double at = key->DoubleAttribute("timevalue", 0.0);
            if (at > time + 1.0e-6)
            {
                past = true;
                property->DeleteChild(key);
            }
            else if (at > time - 1.0e-6)
            {
                atEnd = true;
            }
            key = next;
        }
        // Where it was cut, the value it had there, so the motion up to the
        // cut is as it was.
        if (past && !atEnd)
            property->InsertEndChild(plainKey(document, time, motion.valueAt(id, time, 0.0)));
    }
    xml = print(document);
    return true;
}

bool addKeys(std::string& xml, double time)
{
    MotionObject motion;
    tinyxml2::XMLDocument document;
    if (!motion.parse(xml) || document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (tinyxml2::XMLElement* property : found)
    {
        const char* id = property->Attribute("id");
        if (!id)
            continue;
        // In time order, before the first keyframe later than it.
        tinyxml2::XMLElement* after = nullptr;
        bool there = false;
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;
             key = key->NextSiblingElement("Keyframe"))
        {
            const double at = key->DoubleAttribute("timevalue", 0.0);
            if (std::fabs(at - time) < 1.0e-6)
                there = true;
            if (at > time && !after)
                after = key;
        }
        if (there)
            continue;
        const double fallback = std::strncmp(id, "Scale_", 6) == 0 ? 100.0 : 0.0;
        insertKey(document, property, after, time, motion.valueAt(id, time, fallback));
    }
    xml = print(document);
    return true;
}

namespace {

/// The tween's properties, the container Animate files each under, and the
/// state's value of each.
struct PropertySlot
{
    const char* id;
    const char* container;
    double MotionState::*field;
};

const PropertySlot kSlots[] = {
    {"Motion_X", "Basic_Motion", &MotionState::x},
    {"Motion_Y", "Basic_Motion", &MotionState::y},
    {"Rotation_Z", "Basic_Motion", &MotionState::rotation},
    {"Skew_X", "Transformation", &MotionState::skewX},
    {"Skew_Y", "Transformation", &MotionState::skewY},
    {"Scale_X", "Transformation", &MotionState::scaleX},
    {"Scale_Y", "Transformation", &MotionState::scaleY},
};

tinyxml2::XMLElement* newProperty(tinyxml2::XMLDocument& document, const char* id)
{
    tinyxml2::XMLElement* property = document.NewElement("Property");
    property->SetAttribute("enabled", "1");
    property->SetAttribute("id", id);
    property->SetAttribute("ignoreTimeMap", "0");
    property->SetAttribute("readonly", "0");
    property->SetAttribute("visible", "1");
    return property;
}

/// The container element called \a id, however deep, or null.
tinyxml2::XMLElement* containerOf(tinyxml2::XMLElement* parent, const char* id)
{
    for (tinyxml2::XMLElement* child = parent->FirstChildElement("PropertyContainer"); child;
         child = child->NextSiblingElement("PropertyContainer"))
    {
        const char* name = child->Attribute("id");
        if (name && std::strcmp(name, id) == 0)
            return child;
        if (tinyxml2::XMLElement* found = containerOf(child, id))
            return found;
    }
    return nullptr;
}

/// An angle moved by whole turns to within half a turn of \a near.
double nearest(double angle, double near)
{
    return angle - 360.0 * std::round((angle - near) / 360.0);
}

Affine affineOf(const Transform& t)
{
    Affine m;
    m.a = t.m11;
    m.b = t.m12;
    m.c = t.m21;
    m.d = t.m22;
    m.tx = t.tx;
    m.ty = t.ty;
    return m;
}

} // namespace

MotionState stateOf(const Transform& transform)
{
    const TransformParts parts = TransformParts::of(transform);
    MotionState state;
    state.rotation = parts.skewY;
    state.skewX = nearest(parts.skewX - parts.skewY, 0.0);
    if (std::fabs(state.skewX) < 1.0e-4)
        state.skewX = 0.0;
    state.skewY = 0.0;
    state.scaleX = parts.scaleX;
    state.scaleY = parts.scaleY;
    return state;
}

std::string create(const MotionState& start, int frames, double frameRate, double pivotX, double pivotY)
{
    constexpr double kTimeScale = 30000.0;
    tinyxml2::XMLDocument document;
    tinyxml2::XMLElement* root = document.NewElement("motionObjectXML");
    document.InsertEndChild(root);
    tinyxml2::XMLElement* core = document.NewElement("AnimationCore");
    root->InsertEndChild(core);
    core->SetAttribute("TimeScale", number(kTimeScale).c_str());
    core->SetAttribute("Version", "1");
    core->SetAttribute("duration", number(std::max(1, frames) * kTimeScale / std::max(frameRate, 1.0e-6)).c_str());
    tinyxml2::XMLElement* timeMap = document.NewElement("TimeMap");
    timeMap->SetAttribute("strength", "0");
    timeMap->SetAttribute("type", "Quadratic");
    core->InsertEndChild(timeMap);
    tinyxml2::XMLElement* metadata = document.NewElement("metadata");
    core->InsertEndChild(metadata);
    tinyxml2::XMLElement* names = document.NewElement("names");
    metadata->InsertEndChild(names);
    tinyxml2::XMLElement* name = document.NewElement("name");
    name->SetAttribute("langID", "en_US");
    name->SetAttribute("value", "");
    names->InsertEndChild(name);
    tinyxml2::XMLElement* settings = document.NewElement("Settings");
    settings->SetAttribute("orientToPath", "0");
    settings->SetAttribute("xformPtXOffsetPct", number(pivotX).c_str());
    settings->SetAttribute("xformPtYOffsetPct", number(pivotY).c_str());
    settings->SetAttribute("xformPtZOffsetPixels", "0");
    metadata->InsertEndChild(settings);

    tinyxml2::XMLElement* head = document.NewElement("PropertyContainer");
    head->SetAttribute("id", "headContainer");
    core->InsertEndChild(head);
    for (const char* id : {"Basic_Motion", "Transformation", "Colors", "Filters"})
    {
        tinyxml2::XMLElement* container = document.NewElement("PropertyContainer");
        container->SetAttribute("id", id);
        head->InsertEndChild(container);
    }
    for (const PropertySlot& slot : kSlots)
    {
        tinyxml2::XMLElement* property = newProperty(document, slot.id);
        property->InsertEndChild(plainKey(document, 0.0, start.*slot.field));
        containerOf(head, slot.container)->InsertEndChild(property);
        // Animate keeps the depth among the basic motion, after the turn.
        if (std::strcmp(slot.id, "Rotation_Z") == 0)
        {
            tinyxml2::XMLElement* depth = newProperty(document, "Depth");
            depth->InsertEndChild(plainKey(document, 0.0, 0.0));
            containerOf(head, slot.container)->InsertEndChild(depth);
        }
    }
    return print(document);
}

bool setKeys(std::string& xml, double time, const MotionState& state)
{
    MotionObject motion;
    tinyxml2::XMLDocument document;
    if (!motion.parse(xml) || document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (const PropertySlot& slot : kSlots)
    {
        const double fallback = std::strncmp(slot.id, "Scale_", 6) == 0 ? 100.0 : 0.0;
        const double wanted = state.*slot.field;
        if (std::fabs(motion.valueAt(slot.id, time, fallback) - wanted) < 1.0e-6)
            continue;
        tinyxml2::XMLElement* property = nullptr;
        for (tinyxml2::XMLElement* candidate : found)
        {
            const char* id = candidate->Attribute("id");
            if (id && std::strcmp(id, slot.id) == 0)
                property = candidate;
        }
        if (property && property->IntAttribute("enabled", 1) == 0)
        {
            // A switched-off property is replaced: it held nothing.
            property->DeleteChildren();
            property->SetAttribute("enabled", "1");
        }
        if (!property)
        {
            tinyxml2::XMLElement* head = containerOf(core, "headContainer");
            if (!head)
            {
                head = document.NewElement("PropertyContainer");
                head->SetAttribute("id", "headContainer");
                core->InsertEndChild(head);
            }
            tinyxml2::XMLElement* container = containerOf(core, slot.container);
            if (!container)
            {
                container = document.NewElement("PropertyContainer");
                container->SetAttribute("id", slot.container);
                head->InsertEndChild(container);
            }
            property = newProperty(document, slot.id);
            container->InsertEndChild(property);
        }
        if (!property->FirstChildElement("Keyframe") && time > 1.0e-6)
            property->InsertEndChild(plainKey(document, 0.0, motion.valueAt(slot.id, 0.0, fallback)));

        tinyxml2::XMLElement* after = nullptr;
        tinyxml2::XMLElement* there = nullptr;
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;
             key = key->NextSiblingElement("Keyframe"))
        {
            const double at = key->DoubleAttribute("timevalue", 0.0);
            if (std::fabs(at - time) < 1.0e-6)
                there = key;
            else if (at > time && !after)
                after = key;
        }
        if (there)
        {
            // The keyframe moves, its handles keeping their shape.
            double offset = 0.0;
            double old = 0.0;
            pair(there->Attribute("anchor"), offset, old);
            const double delta = wanted - old;
            there->SetAttribute("anchor", (number(offset) + "," + number(wanted)).c_str());
            for (const char* handle : {"next", "previous"})
            {
                double handleTime = 0.0;
                double handleValue = 0.0;
                if (pair(there->Attribute(handle), handleTime, handleValue))
                    there->SetAttribute(handle, (number(handleTime) + "," + number(handleValue + delta)).c_str());
            }
            continue;
        }
        insertKey(document, property, after, time, wanted);
    }
    xml = print(document);
    return true;
}

bool moveStart(std::string& xml, double dx, double dy)
{
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (tinyxml2::XMLElement* property : found)
    {
        const char* id = property->Attribute("id");
        const double move = !id ? 0.0 : std::strcmp(id, "Motion_X") == 0 ? dx : std::strcmp(id, "Motion_Y") == 0 ? dy : 0.0;
        if (move == 0.0)
            continue;
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;
             key = key->NextSiblingElement("Keyframe"))
        {
            if (key->DoubleAttribute("timevalue", 0.0) <= 1.0e-6)
                continue;
            for (const char* name : {"anchor", "next", "previous"})
            {
                double a = 0.0;
                double b = 0.0;
                if (pair(key->Attribute(name), a, b))
                    key->SetAttribute(name, (number(a) + "," + number(b - move)).c_str());
            }
        }
    }
    xml = print(document);
    return true;
}

bool bend(std::string& xml, double time, double end, double x, double y)
{
    MotionObject motion;
    tinyxml2::XMLDocument document;
    if (!motion.parse(xml) || document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (tinyxml2::XMLElement* property : found)
    {
        const char* id = property->Attribute("id");
        if (!id || (std::strcmp(id, "Motion_X") != 0 && std::strcmp(id, "Motion_Y") != 0))
            continue;
        // A keyframe after \a time to bend toward, at the end if need be.
        bool after = false;
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;
             key = key->NextSiblingElement("Keyframe"))
            after = after || key->DoubleAttribute("timevalue", 0.0) > time + 1.0e-6;
        if (!after && end > time + 1.0e-6)
            property->InsertEndChild(plainKey(document, end, motion.valueAt(id, end, 0.0)));
        const double wanted = std::strcmp(id, "Motion_X") == 0 ? x : y;
        tinyxml2::XMLElement* from = nullptr;
        tinyxml2::XMLElement* to = nullptr;
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;
             key = key->NextSiblingElement("Keyframe"))
        {
            const double at = key->DoubleAttribute("timevalue", 0.0);
            if (at <= time + 1.0e-6)
                from = key;
            else if (!to)
                to = key;
        }
        if (!from || !to)
            continue;
        const double t0 = from->DoubleAttribute("timevalue", 0.0);
        const double t1 = to->DoubleAttribute("timevalue", 0.0);
        double ignored = 0.0;
        double v0 = 0.0;
        double v1 = 0.0;
        pair(from->Attribute("anchor"), ignored, v0);
        pair(to->Attribute("anchor"), ignored, v1);
        const double s = (time - t0) / std::max(t1 - t0, 1.0e-9);
        if (s <= 1.0e-3 || s >= 1.0 - 1.0e-3)
            continue;
        // Handles a third of the way in time, so time runs evenly along the
        // curve, both raised alike so it passes through the value wanted.
        const double raise = (wanted - (v0 + (v1 - v0) * s)) / (3.0 * s * (1.0 - s));
        const double third = (t1 - t0) / 3.0;
        from->SetAttribute("next", (number(third) + "," + number(v0 + (v1 - v0) / 3.0 + raise)).c_str());
        to->SetAttribute("previous", (number(-third) + "," + number(v0 + 2.0 * (v1 - v0) / 3.0 + raise)).c_str());
    }
    xml = print(document);
    return true;
}

bool rebase(std::string& xml, const MotionState& from, const MotionState& to)
{
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (tinyxml2::XMLElement* property : found)
    {
        const char* id = property->Attribute("id");
        if (!id)
            continue;
        double add = 0.0;
        double times = 1.0;
        if (std::strcmp(id, "Rotation_Z") == 0)
            add = to.rotation - from.rotation;
        else if (std::strcmp(id, "Skew_X") == 0)
            add = to.skewX - from.skewX;
        else if (std::strcmp(id, "Skew_Y") == 0)
            add = to.skewY - from.skewY;
        else if (std::strcmp(id, "Scale_X") == 0 && from.scaleX != 0.0)
            times = to.scaleX / from.scaleX;
        else if (std::strcmp(id, "Scale_Y") == 0 && from.scaleY != 0.0)
            times = to.scaleY / from.scaleY;
        else
            continue;
        for (tinyxml2::XMLElement* key = property->FirstChildElement("Keyframe"); key;
             key = key->NextSiblingElement("Keyframe"))
        {
            for (const char* name : {"anchor", "next", "previous"})
            {
                double a = 0.0;
                double b = 0.0;
                if (pair(key->Attribute(name), a, b))
                    key->SetAttribute(name, (number(a) + "," + number(b * times + add)).c_str());
            }
        }
    }
    xml = print(document);
    return true;
}

std::string wrapped(const std::string& text)
{
    tinyxml2::XMLDocument document;
    if (document.Parse(text.c_str()) != tinyxml2::XML_SUCCESS)
        return std::string();
    tinyxml2::XMLElement* root = document.RootElement();
    if (!root)
        return std::string();
    if (std::strcmp(root->Name(), "motionObjectXML") == 0)
        return root->FirstChildElement("AnimationCore") ? print(document) : std::string();
    if (std::strcmp(root->Name(), "AnimationCore") != 0)
        return std::string();
    tinyxml2::XMLDocument out;
    tinyxml2::XMLElement* wrapper = out.NewElement("motionObjectXML");
    out.InsertEndChild(wrapper);
    wrapper->InsertEndChild(root->DeepClone(&out));
    return print(out);
}

std::vector<PropertyKey> keysOf(const std::string& xml, const std::string& id)
{
    std::vector<PropertyKey> keys;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return keys;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return keys;
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    for (tinyxml2::XMLElement* property : found)
    {
        const char* name = property->Attribute("id");
        if (!name || id != name || property->IntAttribute("enabled", 1) == 0)
            continue;
        for (tinyxml2::XMLElement* element = property->FirstChildElement("Keyframe"); element;
             element = element->NextSiblingElement("Keyframe"))
        {
            PropertyKey key;
            double ignored = 0.0;
            key.time = element->DoubleAttribute("timevalue", 0.0);
            if (!pair(element->Attribute("anchor"), ignored, key.value))
                continue;
            if (!pair(element->Attribute("next"), key.nextTime, key.nextValue))
                key.nextValue = key.value;
            if (!pair(element->Attribute("previous"), key.previousTime, key.previousValue))
                key.previousValue = key.value;
            keys.push_back(key);
        }
    }
    std::sort(keys.begin(), keys.end(), [](const PropertyKey& a, const PropertyKey& b) { return a.time < b.time; });
    return keys;
}

bool setKeysOf(std::string& xml, const std::string& id, const std::vector<PropertyKey>& keys)
{
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    std::vector<tinyxml2::XMLElement*> found;
    properties(core, found);
    tinyxml2::XMLElement* property = nullptr;
    for (tinyxml2::XMLElement* candidate : found)
    {
        const char* name = candidate->Attribute("id");
        if (name && id == name)
            property = candidate;
    }
    if (!property)
    {
        const char* containerId = "Transformation";
        for (const PropertySlot& slot : kSlots)
        {
            if (id == slot.id)
                containerId = slot.container;
        }
        tinyxml2::XMLElement* container = containerOf(core, containerId);
        if (!container)
        {
            tinyxml2::XMLElement* head = containerOf(core, "headContainer");
            if (!head)
            {
                head = document.NewElement("PropertyContainer");
                head->SetAttribute("id", "headContainer");
                core->InsertEndChild(head);
            }
            container = document.NewElement("PropertyContainer");
            container->SetAttribute("id", containerId);
            head->InsertEndChild(container);
        }
        property = newProperty(document, id.c_str());
        container->InsertEndChild(property);
    }
    property->DeleteChildren();
    property->SetAttribute("enabled", "1");
    std::vector<PropertyKey> sorted = keys;
    std::sort(sorted.begin(), sorted.end(), [](const PropertyKey& a, const PropertyKey& b) { return a.time < b.time; });
    for (const PropertyKey& key : sorted)
    {
        tinyxml2::XMLElement* element = document.NewElement("Keyframe");
        element->SetAttribute("anchor", ("0," + number(key.value)).c_str());
        element->SetAttribute("next", (number(key.nextTime) + "," + number(key.nextValue)).c_str());
        element->SetAttribute("previous", (number(key.previousTime) + "," + number(key.previousValue)).c_str());
        element->SetAttribute("roving", "0");
        element->SetAttribute("timevalue", number(key.time).c_str());
        property->InsertEndChild(element);
    }
    xml = print(document);
    return true;
}

void easeSegment(std::vector<PropertyKey>& keys, size_t segment, double x1, double y1, double x2, double y2)
{
    if (segment + 1 >= keys.size())
        return;
    PropertyKey& from = keys[segment];
    PropertyKey& to = keys[segment + 1];
    const double span = to.time - from.time;
    const double change = to.value - from.value;
    from.nextTime = x1 * span;
    from.nextValue = from.value + y1 * change;
    to.previousTime = (x2 - 1.0) * span;
    to.previousValue = from.value + y2 * change;
}

namespace {

/// Where on the stretch from \a from to \a to the curve's time is \a time,
/// 0 to 1.
double curveAt(const PropertyKey& from, const PropertyKey& to, double time)
{
    double low = 0.0;
    double high = 1.0;
    for (int step = 0; step < 60; ++step)
    {
        const double mid = (low + high) * 0.5;
        if (cubic(from.time, from.time + from.nextTime, to.time + to.previousTime, to.time, mid) < time)
            low = mid;
        else
            high = mid;
    }
    return (low + high) * 0.5;
}

} // namespace

double valueOf(const std::vector<PropertyKey>& keys, double time)
{
    if (keys.empty())
        return 0.0;
    if (time <= keys.front().time)
        return keys.front().value;
    for (size_t i = 0; i + 1 < keys.size(); ++i)
    {
        const PropertyKey& from = keys[i];
        const PropertyKey& to = keys[i + 1];
        if (time > to.time)
            continue;
        return cubic(from.value, from.nextValue, to.previousValue, to.value, curveAt(from, to, time));
    }
    return keys.back().value;
}

size_t splitAt(std::vector<PropertyKey>& keys, double time)
{
    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (std::fabs(keys[i].time - time) < 1.0e-6)
            return i;
    }
    size_t after = 0;
    while (after < keys.size() && keys[after].time < time)
        ++after;
    if (after == 0 || after == keys.size())
    {
        // Before the first or past the last: holding the value there.
        const PropertyKey key = PropertyKey::at(time, valueOf(keys, time));
        keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(after), key);
        return after;
    }
    PropertyKey& from = keys[after - 1];
    PropertyKey& to = keys[after];
    const double s = curveAt(from, to, time);
    const auto lerp = [s](double a, double b) { return a + (b - a) * s; };
    const double t1 = from.time + from.nextTime, t2 = to.time + to.previousTime;
    const double at01 = lerp(from.time, t1), av01 = lerp(from.value, from.nextValue);
    const double at12 = lerp(t1, t2), av12 = lerp(from.nextValue, to.previousValue);
    const double at23 = lerp(t2, to.time), av23 = lerp(to.previousValue, to.value);
    const double bt0 = lerp(at01, at12), bv0 = lerp(av01, av12);
    const double bt1 = lerp(at12, at23), bv1 = lerp(av12, av23);
    PropertyKey key;
    key.time = time;
    key.value = lerp(bv0, bv1);
    const double mt = lerp(bt0, bt1);
    key.previousTime = bt0 - mt;
    key.previousValue = bv0;
    key.nextTime = bt1 - mt;
    key.nextValue = bv1;
    from.nextTime = at01 - from.time;
    from.nextValue = av01;
    to.previousTime = at23 - to.time;
    to.previousValue = av23;
    keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(after), key);
    return after;
}

double spanEase(const std::string& xml)
{
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return 0.0;
    tinyxml2::XMLElement* core = coreOf(document);
    const tinyxml2::XMLElement* timeMap = core ? core->FirstChildElement("TimeMap") : nullptr;
    return timeMap ? timeMap->DoubleAttribute("strength", 0.0) : 0.0;
}

bool setSpanEase(std::string& xml, double strength)
{
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    tinyxml2::XMLElement* core = coreOf(document);
    if (!core)
        return false;
    tinyxml2::XMLElement* timeMap = core->FirstChildElement("TimeMap");
    if (!timeMap)
    {
        timeMap = document.NewElement("TimeMap");
        timeMap->SetAttribute("type", "Quadratic");
        core->InsertFirstChild(timeMap);
    }
    timeMap->SetAttribute("strength", number(std::max(-100.0, std::min(100.0, strength))).c_str());
    xml = print(document);
    return true;
}

MotionState solve(const Transform& stored, const Point& pivot, const MotionState& start, const Transform& wanted,
    const MotionState& near)
{
    Affine linearStored = affineOf(stored);
    linearStored.tx = linearStored.ty = 0.0;
    Affine linearWanted = affineOf(wanted);
    linearWanted.tx = linearWanted.ty = 0.0;
    Affine undoStored;
    MotionState state = near;
    if (!linearStored.inverse(undoStored))
        return state;
    // applyMotion's turn, skew and scale are the stored ones, undone back
    // to the start's, then the state's: the state's is what is left.
    const Affine now = linearPart(start).then(undoStored).then(linearWanted);
    state = stateOf(Transform(now.a, now.b, now.c, now.d, 0.0, 0.0));
    // Whole turns change nothing shown; the nearest keeps a tween from
    // spinning the long way round.
    state.rotation = nearest(state.rotation, near.rotation);
    state.skewX = nearest(state.skewX, near.skewX);

    // The offset moves the transformation point.
    const Affine base = affineOf(stored);
    const Affine target = affineOf(wanted);
    state.x = start.x + (target.a * pivot.x + target.c * pivot.y + target.tx) -
              (base.a * pivot.x + base.c * pivot.y + base.tx);
    state.y = start.y + (target.b * pivot.x + target.d * pivot.y + target.ty) -
              (base.b * pivot.x + base.d * pivot.y + base.ty);
    return state;
}

} // namespace motion

} // namespace fla
