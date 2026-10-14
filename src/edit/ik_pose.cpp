#include "ik_pose.h"

#include "layer_commands.h"
#include "shape_merge.h"
#include "shape_transform.h"

#include "../data/edge.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <cstdlib>

namespace fla {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> numbers(const char* list)
{
    std::vector<double> values;
    if (!list)
        return values;
    const char* at = list;
    while (*at)
    {
        char* end = nullptr;
        const double value = std::strtod(at, &end);
        if (end == at)
            break;
        values.push_back(value);
        at = end;
        while (*at == ',' || *at == ' ')
            ++at;
    }
    return values;
}

std::string listOf(const std::vector<double>& values)
{
    std::string text;
    char buffer[64];
    for (size_t i = 0; i < values.size(); ++i)
    {
        std::snprintf(buffer, sizeof(buffer), "%s%.6f", i ? "," : "", values[i]);
        text += buffer;
    }
    return text;
}

void setNumber(tinyxml2::XMLElement* element, const char* name, double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    element->SetAttribute(name, buffer);
}

Point rotated(const Point& p, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return Point(c * p.x - s * p.y, s * p.x + c * p.y);
}

double wrapAngle(double angle)
{
    while (angle > kPi)
        angle -= 2.0 * kPi;
    while (angle <= -kPi)
        angle += 2.0 * kPi;
    return angle;
}

/// One coordinate of a node's location, in pixels: "#1F.8F" (hex fixed
/// point, a 24-bit signed whole part) or plain, in units of 256 twips.
bool locationCoordinate(std::string text, double& pixels)
{
    text.erase(0, text.find_first_not_of(" \t"));
    text.erase(text.find_last_not_of(" \t") + 1);
    if (text.empty())
        return false;
    double units = 0.0;
    char* end = nullptr;
    if (text[0] == '#')
    {
        const size_t dot = text.find('.');
        long whole = std::strtol(text.substr(1, dot == std::string::npos ? std::string::npos : dot - 1).c_str(), &end, 16);
        if (whole >= 0x800000)
            whole -= 0x1000000;
        double part = 0.0;
        if (dot != std::string::npos && dot + 1 < text.size())
        {
            const std::string fraction = text.substr(dot + 1);
            part = std::strtol(fraction.c_str(), &end, 16) / std::pow(16.0, static_cast<double>(fraction.size()));
        }
        units = static_cast<double>(whole) + part;
    }
    else
    {
        units = std::strtod(text.c_str(), &end);
        if (end == text.c_str())
            return false;
    }
    pixels = units * 256.0 / 20.0;
    return true;
}

bool parseLocation(const char* text, Point& at)
{
    if (!text)
        return false;
    const std::string value = text;
    const size_t comma = value.find(',');
    return comma != std::string::npos && locationCoordinate(value.substr(0, comma), at.x) &&
        locationCoordinate(value.substr(comma + 1), at.y);
}

std::string locationText(const Point& at)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.6f, %.6f", at.x * 20.0 / 256.0, at.y * 20.0 / 256.0);
    return buffer;
}

double distanceToSegment(const Point& p, const Point& a, const Point& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length2 = dx * dx + dy * dy;
    double t = length2 > 0.0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

/// \a first, then \a second.
Transform then(const Transform& first, const Transform& second)
{
    return Transform(first.m11 * second.m11 + first.m12 * second.m21, first.m11 * second.m12 + first.m12 * second.m22,
        first.m21 * second.m11 + first.m22 * second.m21, first.m21 * second.m12 + first.m22 * second.m22,
        first.tx * second.m11 + first.ty * second.m21 + second.tx, first.tx * second.m12 + first.ty * second.m22 + second.ty);
}

/// A turn by \a angle radians about the origin, as a transform.
Transform turn(double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return Transform(c, s, -s, c, 0.0, 0.0);
}

bool inverse(const Transform& m, Transform& out)
{
    const double det = m.m11 * m.m22 - m.m12 * m.m21;
    if (std::fabs(det) < 1.0e-12)
        return false;
    out.m11 = m.m22 / det;
    out.m12 = -m.m12 / det;
    out.m21 = -m.m21 / det;
    out.m22 = m.m11 / det;
    out.tx = -(out.m11 * m.tx + out.m21 * m.ty);
    out.ty = -(out.m12 * m.tx + out.m22 * m.ty);
    return true;
}

/// A Matrix element with only the entries that differ from the identity,
/// as Animate writes them.
void writeMatrix(tinyxml2::XMLElement* matrix, const Transform& m)
{
    const auto entry = [matrix](const char* name, double value, double plain) {
        if (std::fabs(value - plain) > 1.0e-9)
            setNumber(matrix, name, value);
    };
    entry("a", m.m11, 1.0);
    entry("b", m.m12, 0.0);
    entry("c", m.m21, 0.0);
    entry("d", m.m22, 1.0);
    entry("tx", m.tx, 0.0);
    entry("ty", m.ty, 0.0);
}

/// A shape's control points numbered as they are first reached, each place
/// once.
struct AnchorIndex
{
    std::vector<Point> anchors;
    std::map<std::pair<long long, long long>, int> numbers;

    static std::pair<long long, long long> key(const Point& p)
    {
        return {std::llround(p.x * 1000.0), std::llround(p.y * 1000.0)};
    }

    int of(const Point& p)
    {
        const auto found = numbers.emplace(key(p), static_cast<int>(anchors.size()));
        if (found.second)
            anchors.push_back(p);
        return found.first->second;
    }

    /// Each segment's end, numbered, in the order the edges reach them.
    void walk(const Shape& shape)
    {
        for (const Edge* edge : shape.edges)
        {
            for (const Path* path : edge ? edge->paths : std::vector<Path*>())
            {
                for (const PathSegment* segment : path ? path->segments : std::vector<PathSegment*>())
                {
                    if (segment && segment->command != PathSegment::Command::Close && !segment->points.empty())
                        of(segment->points.back());
                }
            }
        }
    }
};

} // namespace

IKArmature::IKArmature()
    : _document(new tinyxml2::XMLDocument)
{
}

IKArmature::~IKArmature() = default;

IKArmature::IKArmature(const IKArmature& other)
    : _document(new tinyxml2::XMLDocument)
{
    parse(other.xml());
}

IKArmature& IKArmature::operator=(const IKArmature& other)
{
    if (this != &other)
        parse(other.xml());
    return *this;
}

bool IKArmature::parse(const std::string& xml)
{
    _document.reset(new tinyxml2::XMLDocument);
    _nodes.clear();
    _bendsShape = false;
    if (_document->Parse(xml.c_str()) != tinyxml2::XML_SUCCESS || !_document->FirstChildElement("IKTree"))
        return false;
    index();
    return !_nodes.empty();
}

std::string IKArmature::xml() const
{
    tinyxml2::XMLPrinter printer(nullptr, true);
    _document->Print(&printer);
    return printer.CStr();
}

void IKArmature::index()
{
    _nodes.clear();
    tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    // Depth first, parents before children; the IKNode at the root has no
    // pose of its own, so its children are the armature's root joints.
    struct Walk
    {
        IKArmature& self;
        void children(tinyxml2::XMLElement* owner, int parent)
        {
            tinyxml2::XMLElement* list = owner->FirstChildElement("childNodes");
            for (tinyxml2::XMLElement* child = list ? list->FirstChildElement("ChildNode") : nullptr; child;
                 child = child->NextSiblingElement("ChildNode"))
            {
                int here = parent;
                if (child->FirstChildElement("states"))
                {
                    self._nodes.push_back({child, parent});
                    here = static_cast<int>(self._nodes.size()) - 1;
                    const char* type = child->Attribute("nodeType");
                    if (type && std::strcmp(type, "lsd") == 0)
                        self._bendsShape = true;
                }
                children(child, here);
            }
        }
    };
    Walk walk{*this};
    for (tinyxml2::XMLElement* root = tree->FirstChildElement("IKNode"); root;
         root = root->NextSiblingElement("IKNode"))
    {
        walk.children(root, -1);
    }

    // An armature of symbols keeps each instance's registration point in
    // xArray and the joint, as saved, in location: how far apart they are
    // turns with the joint.
    if (_bendsShape)
        return;
    for (Node& node : _nodes)
    {
        const std::vector<double> xs = numbers(node.element->Attribute("xArray"));
        const std::vector<double> ys = numbers(node.element->Attribute("yArray"));
        const std::vector<double> angles = numbers(node.element->Attribute("angleArray"));
        Point joint;
        if (xs.empty() || ys.empty() || !parseLocation(node.element->Attribute("location"), joint))
            continue;
        node.registration = rotated(Point(xs[0] / 20.0 - joint.x, ys[0] / 20.0 - joint.y), angles.empty() ? 0.0 : -angles[0]);
    }
}

std::vector<tinyxml2::XMLElement*> IKArmature::states(const tinyxml2::XMLElement* owner, const char* name) const
{
    std::vector<tinyxml2::XMLElement*> found;
    const tinyxml2::XMLElement* list = owner ? owner->FirstChildElement("states") : nullptr;
    for (const tinyxml2::XMLElement* state = list ? list->FirstChildElement(name) : nullptr; state;
         state = state->NextSiblingElement(name))
    {
        found.push_back(const_cast<tinyxml2::XMLElement*>(state));
    }
    return found;
}

int IKArmature::frames() const
{
    int total = 0;
    for (const tinyxml2::XMLElement* state : states(_document->FirstChildElement("IKTree"), "IKState"))
        total += std::max(1, state->IntAttribute("duration", 1));
    return total;
}

std::string IKArmature::referenceOf(int joint) const
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return std::string();
    const char* reference = _nodes[joint].element->Attribute("referenceID");
    return reference ? reference : std::string();
}

std::vector<int> IKArmature::poses() const
{
    std::vector<int> at;
    int start = 0;
    for (const tinyxml2::XMLElement* state : states(_document->FirstChildElement("IKTree"), "IKState"))
    {
        at.push_back(start);
        start += std::max(1, state->IntAttribute("duration", 1));
    }
    return at;
}

std::vector<IKJoint> IKArmature::jointsAt(int frame) const
{
    std::vector<IKJoint> joints;
    for (const Node& node : _nodes)
    {
        IKJoint joint;
        joint.parent = node.parent;
        const std::vector<double> xs = numbers(node.element->Attribute("xArray"));
        const std::vector<double> ys = numbers(node.element->Attribute("yArray"));
        const std::vector<double> angles = numbers(node.element->Attribute("angleArray"));
        if (frame >= 0 && static_cast<size_t>(frame) < xs.size() && static_cast<size_t>(frame) < ys.size())
            joint.position = Point(xs[frame] / 20.0, ys[frame] / 20.0);
        if (frame >= 0 && static_cast<size_t>(frame) < angles.size())
            joint.angle = angles[frame];
        if (!_bendsShape)
        {
            const Point offset = rotated(node.registration, joint.angle);
            joint.position = Point(joint.position.x - offset.x, joint.position.y - offset.y);
        }
        joints.push_back(joint);
    }
    return joints;
}

void IKArmature::copyEases(const IKArmature& from)
{
    if (!_document || !from._document)
        return;
    // Where each pose span starts, in both.
    const auto starts = [](const std::vector<tinyxml2::XMLElement*>& list) {
        std::vector<int> at;
        int start = 0;
        for (const tinyxml2::XMLElement* state : list)
        {
            at.push_back(start);
            start += std::max(1, state->IntAttribute("duration", 1));
        }
        return at;
    };
    const std::vector<tinyxml2::XMLElement*> source = states(from._document->FirstChildElement("IKTree"), "IKState");
    const std::vector<tinyxml2::XMLElement*> target = states(_document->FirstChildElement("IKTree"), "IKState");
    const std::vector<int> sourceStarts = starts(source);
    const std::vector<int> targetStarts = starts(target);
    for (size_t t = 0; t < target.size(); ++t)
    {
        for (size_t s = 0; s < source.size(); ++s)
        {
            if (sourceStarts[s] != targetStarts[t])
                continue;
            for (const char* name : {"easeType", "easeValue"})
            {
                if (const char* value = source[s]->Attribute(name))
                    target[t]->SetAttribute(name, value);
                else
                    target[t]->DeleteAttribute(name);
            }
            break;
        }
    }
}

double IKArmature::easedTime(int pose, double t) const
{
    const std::vector<tinyxml2::XMLElement*> list = states(_document->FirstChildElement("IKTree"), "IKState");
    if (pose < 0 || static_cast<size_t>(pose) >= list.size())
        return t;
    const char* type = list[pose]->Attribute("easeType");
    const double strength = std::max(-1.0, std::min(1.0, list[pose]->DoubleAttribute("easeValue", 0.0) / 100.0));
    if (!type || std::strcmp(type, "simple quadratic") != 0 || strength == 0.0)
        return t;
    // Positive eases out, starting fast; negative eases in.
    const double curve = strength > 0.0 ? t * (2.0 - t) : t * t;
    const double amount = std::fabs(strength);
    return (1.0 - amount) * t + amount * curve;
}

std::vector<IKJoint> IKArmature::shownAt(int frame) const
{
    // An armature of symbols shows its stored joints: its instances follow
    // them frame by frame.
    if (!_bendsShape)
        return jointsAt(std::max(0, std::min(frame, frames() - 1)));

    // The entry, as ikBones finds it: the span's time eased with t^2 for a
    // positive strength.
    double entry = frame;
    int start = 0;
    for (const tinyxml2::XMLElement* state : states(_document->FirstChildElement("IKTree"), "IKState"))
    {
        const int length = std::max(1, state->IntAttribute("duration", 1));
        if (frame >= start + length)
        {
            start += length;
            continue;
        }
        const double t = static_cast<double>(frame - start) / length;
        const char* type = state->Attribute("easeType");
        const double strength = std::max(-1.0, std::min(1.0, state->DoubleAttribute("easeValue", 0.0) / 100.0));
        double eased = t;
        if (type && std::strcmp(type, "simple quadratic") == 0 && strength != 0.0)
        {
            const double curve = strength > 0.0 ? t * t : t * (2.0 - t);
            eased = (1.0 - std::fabs(strength)) * t + std::fabs(strength) * curve;
        }
        entry = start + eased * length;
        break;
    }
    const int below = static_cast<int>(std::floor(entry));
    const double fraction = entry - below;
    std::vector<IKJoint> joints = jointsAt(below);
    if (fraction <= 0.0 || below + 1 >= frames())
        return joints;
    const std::vector<IKJoint> next = jointsAt(below + 1);
    for (size_t i = 0; i < joints.size() && i < next.size(); ++i)
    {
        joints[i].position.x += (next[i].position.x - joints[i].position.x) * fraction;
        joints[i].position.y += (next[i].position.y - joints[i].position.y) * fraction;
        joints[i].angle += (next[i].angle - joints[i].angle) * fraction;
    }
    return joints;
}

IKArmature::Pose IKArmature::poseOf(size_t node, int frame) const
{
    const std::vector<int> starts = poses();
    const std::vector<tinyxml2::XMLElement*> list = states(_nodes[node].element, "State");
    Pose pose;
    if (list.empty() || starts.empty())
        return pose;
    size_t k = 0;
    while (k + 1 < starts.size() && starts[k + 1] <= frame)
        ++k;
    k = std::min(k, list.size() - 1);
    const auto read = [](const tinyxml2::XMLElement* state) {
        Pose p;
        p.angle = state->DoubleAttribute("angle", 0.0);
        p.x = state->DoubleAttribute("x", 0.0);
        p.y = state->DoubleAttribute("y", 0.0);
        return p;
    };
    const Pose a = read(list[k]);
    if (k + 1 >= starts.size() || k + 1 >= list.size())
        return a;
    const Pose b = read(list[k + 1]);
    const double t = easedTime(static_cast<int>(k),
        static_cast<double>(frame - starts[k]) / std::max(1, starts[k + 1] - starts[k]));
    pose.angle = a.angle + (b.angle - a.angle) * t;
    pose.x = a.x + (b.x - a.x) * t;
    pose.y = a.y + (b.y - a.y) * t;
    return pose;
}

std::vector<IKJoint> IKArmature::posedAt(int frame) const
{
    std::vector<IKJoint> joints(_nodes.size());
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        const Pose pose = poseOf(i, frame);
        IKJoint& joint = joints[i];
        joint.parent = _nodes[i].parent;
        if (joint.parent < 0)
        {
            joint.position = Point(pose.x / 20.0, pose.y / 20.0);
            joint.angle = pose.angle;
            continue;
        }
        const IKJoint& above = joints[joint.parent];
        const Point offset = rotated(Point(pose.x / 20.0, pose.y / 20.0), above.angle);
        joint.position = Point(above.position.x + offset.x, above.position.y + offset.y);
        joint.angle = above.angle + pose.angle;
    }
    return joints;
}

void IKArmature::storeJoints()
{
    const int count = frames();
    std::vector<std::vector<double>> xs(_nodes.size()), ys(_nodes.size()), angles(_nodes.size());
    for (int frame = 0; frame < count; ++frame)
    {
        const std::vector<IKJoint> joints = posedAt(frame);
        for (size_t i = 0; i < joints.size(); ++i)
        {
            Point at = joints[i].position;
            tinyxml2::XMLElement* world = frame == 0 ? _nodes[i].element->FirstChildElement("worldMatrix") : nullptr;
            if (tinyxml2::XMLElement* matrix = world ? world->FirstChildElement("Matrix") : nullptr)
            {
                // The joint's frame as the span starts.
                for (const char* name : {"a", "b", "c", "d", "tx", "ty"})
                    matrix->DeleteAttribute(name);
                Transform placed = turn(joints[i].angle);
                placed.tx = at.x;
                placed.ty = at.y;
                writeMatrix(matrix, placed);
            }
            if (!_bendsShape)
            {
                // The instance's registration point, which turns with it.
                const Point offset = rotated(_nodes[i].registration, joints[i].angle);
                at = Point(at.x + offset.x, at.y + offset.y);
                if (frame == 0)
                    _nodes[i].element->SetAttribute("location", locationText(joints[i].position).c_str());
            }
            xs[i].push_back(at.x * 20.0);
            ys[i].push_back(at.y * 20.0);
            angles[i].push_back(joints[i].angle);
        }
    }
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        _nodes[i].element->SetAttribute("xArray", listOf(xs[i]).c_str());
        _nodes[i].element->SetAttribute("yArray", listOf(ys[i]).c_str());
        _nodes[i].element->SetAttribute("angleArray", listOf(angles[i]).c_str());
    }
}

bool IKArmature::setPose(int frame, const std::vector<double>& angles)
{
    if (angles.size() != _nodes.size() || frame < 0 || frame >= frames())
        return false;

    // Each joint's angle relative to its parent's, turned to lie nearest the
    // one it had, so the in-betweens do not spin the long way round.
    std::vector<Pose> now(_nodes.size());
    for (size_t i = 0; i < _nodes.size(); ++i)
        now[i] = poseOf(i, frame);
    std::vector<double> relative(_nodes.size());
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        const int parent = _nodes[i].parent;
        double angle = angles[i] - (parent >= 0 ? angles[parent] : 0.0);
        angle += 2.0 * kPi * std::round((now[i].angle - angle) / (2.0 * kPi));
        relative[i] = angle;
    }

    // A pose there already takes the new angles; otherwise the pose span
    // round the frame splits in two, the new pose between.
    tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const std::vector<int> starts = poses();
    size_t k = 0;
    while (k + 1 < starts.size() && starts[k + 1] <= frame)
        ++k;
    const bool existing = !starts.empty() && starts[k] == frame;
    if (!existing)
    {
        std::vector<tinyxml2::XMLElement*> treeStates = states(tree, "IKState");
        if (k >= treeStates.size())
            return false;
        tinyxml2::XMLElement* split = treeStates[k];
        const int length = std::max(1, split->IntAttribute("duration", 1));
        const int before = frame - starts[k];
        tinyxml2::XMLNode* copy = split->DeepClone(_document.get());
        split->Parent()->InsertAfterChild(split, copy);
        split->SetAttribute("duration", before);
        copy->ToElement()->SetAttribute("duration", length - before);
    }
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        std::vector<tinyxml2::XMLElement*> list = states(_nodes[i].element, "State");
        if (k >= list.size())
            return false;
        tinyxml2::XMLElement* state = list[k];
        if (!existing)
        {
            const int length = std::max(1, state->IntAttribute("duration", 1));
            const int before = frame - starts[k];
            tinyxml2::XMLElement* copy = state->DeepClone(_document.get())->ToElement();
            state->Parent()->InsertAfterChild(state, copy);
            state->SetAttribute("duration", before);
            copy->SetAttribute("duration", length - before);
            setNumber(copy, "x", now[i].x);
            setNumber(copy, "y", now[i].y);
            state = copy;
        }
        setNumber(state, "angle", relative[i]);
    }
    storeJoints();
    return true;
}

bool IKArmature::removePose(int frame)
{
    const std::vector<int> starts = poses();
    const auto at = std::find(starts.begin(), starts.end(), frame);
    if (frame <= 0 || at == starts.end())
        return false;
    const size_t k = static_cast<size_t>(at - starts.begin());
    // The pose's span joins the one before: one IKState and one State on
    // every node, their durations added.
    const auto merge = [k](std::vector<tinyxml2::XMLElement*> list) {
        if (k >= list.size())
            return;
        tinyxml2::XMLElement* before = list[k - 1];
        before->SetAttribute("duration",
            std::max(1, before->IntAttribute("duration", 1)) + std::max(1, list[k]->IntAttribute("duration", 1)));
        list[k]->Parent()->DeleteChild(list[k]);
    };
    merge(states(_document->FirstChildElement("IKTree"), "IKState"));
    for (Node& node : _nodes)
        merge(states(node.element, "State"));
    storeJoints();
    return true;
}

bool IKArmature::setFrames(int count)
{
    const std::vector<int> starts = poses();
    if (starts.empty() || count <= starts.back())
        return false;
    const int last = count - starts.back();
    std::vector<tinyxml2::XMLElement*> treeStates = states(_document->FirstChildElement("IKTree"), "IKState");
    treeStates.back()->SetAttribute("duration", last);
    for (Node& node : _nodes)
    {
        std::vector<tinyxml2::XMLElement*> list = states(node.element, "State");
        if (list.size() == starts.size())
            list.back()->SetAttribute("duration", last);
    }
    storeJoints();
    return true;
}

// ---------------------------------------------------------------------------

std::vector<JointLimit> IKArmature::limits() const
{
    std::vector<JointLimit> found;
    for (const Node& node : _nodes)
    {
        JointLimit limit;
        limit.constrained = node.element->Attribute("phoenixMinRotation") && node.element->Attribute("phoenixMaxRotation");
        limit.min = node.element->DoubleAttribute("phoenixMinRotation", 0.0);
        limit.max = node.element->DoubleAttribute("phoenixMaxRotation", 0.0);
        found.push_back(limit);
    }
    return found;
}

void IKArmature::setLimit(int joint, const JointLimit& limit)
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return;
    tinyxml2::XMLElement* element = _nodes[joint].element;
    if (!limit.constrained)
    {
        element->DeleteAttribute("phoenixMinRotation");
        element->DeleteAttribute("phoenixMaxRotation");
        return;
    }
    setNumber(element, "phoenixMinRotation", std::min(limit.min, limit.max));
    setNumber(element, "phoenixMaxRotation", std::max(limit.min, limit.max));
}

std::string IKArmature::name() const
{
    const tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const char* text = tree ? tree->Attribute("name") : nullptr;
    return text ? text : std::string();
}

std::string IKArmature::treeName() const
{
    const tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const char* text = tree ? tree->Attribute("treeName") : nullptr;
    return text ? text : std::string();
}

std::vector<ArmatureJoint> IKArmature::symbolJoints() const
{
    std::vector<ArmatureJoint> found;
    const std::vector<IKJoint> joints = jointsAt(0);
    const std::vector<JointLimit> limited = limits();
    for (size_t i = 0; i < _nodes.size() && i < joints.size(); ++i)
    {
        ArmatureJoint joint;
        joint.position = joints[i].position;
        joint.parent = _nodes[i].parent;
        const char* name = _nodes[i].element->Attribute("name");
        joint.node = name ? name : std::string();
        joint.reference = referenceOf(static_cast<int>(i));
        const Point offset = rotated(_nodes[i].registration, joints[i].angle);
        joint.registration = Point(joints[i].position.x + offset.x, joints[i].position.y + offset.y);
        joint.limit = limited[i];
        found.push_back(joint);
    }
    return found;
}

bool IKArmature::storedAt(int joint, int frame, Point& registration, double& angle) const
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return false;
    const tinyxml2::XMLElement* element = _nodes[static_cast<size_t>(joint)].element;
    const std::vector<double> xs = numbers(element->Attribute("xArray"));
    const std::vector<double> ys = numbers(element->Attribute("yArray"));
    const std::vector<double> angles = numbers(element->Attribute("angleArray"));
    const size_t count = std::min(xs.size(), ys.size());
    if (count == 0)
        return false;
    const size_t at = static_cast<size_t>(std::max(0, std::min(frame, static_cast<int>(count) - 1)));
    registration = Point(xs[at] / 20.0, ys[at] / 20.0);
    angle = angles.empty() ? 0.0 : angles[std::min(at, angles.size() - 1)];
    return true;
}

Transform IKArmature::carried(int joint, const Transform& stored, int frame) const
{
    Point first;
    Point now;
    double firstAngle = 0.0;
    double nowAngle = 0.0;
    if (_bendsShape || frame <= 0 || !storedAt(joint, 0, first, firstAngle) || !storedAt(joint, frame, now, nowAngle))
        return stored;
    // Off the first frame's registration point, turned, onto this frame's.
    Transform moved = stored;
    moved.tx -= first.x;
    moved.ty -= first.y;
    moved = then(moved, turn(nowAngle - firstAngle));
    moved.tx += now.x;
    moved.ty += now.y;
    return moved;
}

std::string IKArmature::betweenFrameMatrixList(const std::vector<const Transform*>& instances) const
{
    tinyxml2::XMLDocument document;
    tinyxml2::XMLElement* list = document.NewElement("betweenFrameMatrixList");
    document.InsertEndChild(list);
    std::vector<Transform> between;
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        Point registration;
        double angle = 0.0;
        if (i >= instances.size() || !instances[i] || !storedAt(static_cast<int>(i), 0, registration, angle))
            continue;
        Transform local = *instances[i];
        local.tx -= registration.x;
        local.ty -= registration.y;
        between.push_back(then(local, turn(-angle)));
    }
    const int count = std::max(1, frames());
    for (int frame = 0; frame < count; ++frame)
    {
        for (const Transform& matrix : between)
        {
            tinyxml2::XMLElement* element = document.NewElement("Matrix");
            list->InsertEndChild(element);
            writeMatrix(element, matrix);
        }
    }
    tinyxml2::XMLPrinter printer(nullptr, true);
    document.Print(&printer);
    return printer.CStr();
}

BoneSpring IKArmature::spring(int joint) const
{
    BoneSpring found;
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return found;
    const tinyxml2::XMLElement* element = _nodes[static_cast<size_t>(joint)].element;
    const char* strength = element->Attribute("springStrength") ? "springStrength" : "strength";
    const char* damping = element->Attribute("springDamping") ? "springDamping" : "damping";
    found.present = element->Attribute(strength) || element->Attribute(damping);
    found.strength = element->DoubleAttribute(strength, 0.0);
    found.damping = element->DoubleAttribute(damping, 0.0);
    return found;
}

void IKArmature::keepAttributesOf(const IKArmature& other)
{
    tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const tinyxml2::XMLElement* otherTree = other._document->FirstChildElement("IKTree");
    if (!tree || !otherTree)
        return;
    const auto keep = [](tinyxml2::XMLElement* to, const tinyxml2::XMLElement* from) {
        for (const tinyxml2::XMLAttribute* attribute = from->FirstAttribute(); attribute; attribute = attribute->Next())
        {
            if (!to->Attribute(attribute->Name()))
                to->SetAttribute(attribute->Name(), attribute->Value());
        }
    };
    keep(tree, otherTree);
    // Every node by its element and name: the root's IKNode and the
    // ChildNode under it share a name.
    std::map<std::string, const tinyxml2::XMLElement*> before;
    std::function<void(const tinyxml2::XMLElement*)> gather = [&](const tinyxml2::XMLElement* owner) {
        for (const tinyxml2::XMLElement* child = owner->FirstChildElement(); child; child = child->NextSiblingElement())
        {
            const char* name = child->Attribute("name");
            if (name && (std::strcmp(child->Name(), "IKNode") == 0 || std::strcmp(child->Name(), "ChildNode") == 0))
                before.emplace(std::string(child->Name()) + "/" + name, child);
            if (std::strcmp(child->Name(), "childNodes") == 0 || std::strcmp(child->Name(), "IKNode") == 0 ||
                std::strcmp(child->Name(), "ChildNode") == 0)
            {
                gather(child);
            }
        }
    };
    gather(otherTree);
    std::function<void(tinyxml2::XMLElement*)> apply = [&](tinyxml2::XMLElement* owner) {
        for (tinyxml2::XMLElement* child = owner->FirstChildElement(); child; child = child->NextSiblingElement())
        {
            const char* name = child->Attribute("name");
            if (name)
            {
                const auto found = before.find(std::string(child->Name()) + "/" + name);
                if (found != before.end())
                    keep(child, found->second);
            }
            if (std::strcmp(child->Name(), "childNodes") == 0 || std::strcmp(child->Name(), "IKNode") == 0 ||
                std::strcmp(child->Name(), "ChildNode") == 0)
            {
                apply(child);
            }
        }
    };
    apply(tree);
    index();
}

ShapeBinding IKArmature::binding() const
{
    ShapeBinding found;
    const tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const std::vector<double> indices = numbers(tree ? tree->Attribute("connectionIndices") : nullptr);
    const std::vector<double> data = numbers(tree ? tree->Attribute("connectionData") : nullptr);
    if (indices.empty())
        return found;
    // The number of points; then each point's number and, for each of its
    // bones, the bone, where its group of four sits in connectionData, a
    // count of handle pairs and where each handle's group sits; -1 ends a
    // point.
    const int points = static_cast<int>(indices[0]);
    size_t at = 1;
    for (int point = 0; point < points && at < indices.size(); ++point)
    {
        const int number = static_cast<int>(indices[at++]);
        if (number < 0 || number > 100000)
            break;
        if (found.size() <= static_cast<size_t>(number))
            found.resize(static_cast<size_t>(number) + 1);
        while (at < indices.size() && indices[at] >= 0.0)
        {
            BoneWeight bound;
            bound.bone = static_cast<int>(indices[at]);
            const size_t group = at + 1 < indices.size() ? static_cast<size_t>(std::max(0.0, indices[at + 1])) : 0;
            const size_t handles = at + 2 < indices.size() ? static_cast<size_t>(std::max(0.0, indices[at + 2])) : 0;
            bound.weight = group + 3 < data.size() ? data[group + 3] : 1.0;
            found[static_cast<size_t>(number)].push_back(bound);
            at += 3 + 2 * handles;
        }
        ++at;
    }
    return found;
}

bool IKArmature::bindsAnchors() const
{
    const tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const char* kind = tree ? tree->Attribute("phoenixBinding") : nullptr;
    return kind && std::strcmp(kind, "anchors") == 0;
}

void IKArmature::setBinding(const ShapeBinding& binding, const std::vector<Point>& anchors, const Transform& contour,
    const std::string& style)
{
    tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    if (!tree)
        return;
    const std::vector<IKJoint> joints = jointsAt(0);
    std::vector<double> indices;
    std::vector<double> data;
    indices.push_back(static_cast<double>(anchors.size()));
    for (size_t point = 0; point < anchors.size(); ++point)
    {
        indices.push_back(static_cast<double>(point));
        const std::vector<BoneWeight> none;
        for (const BoneWeight& bound : point < binding.size() ? binding[point] : none)
        {
            if (bound.bone < 0 || static_cast<size_t>(bound.bone) >= joints.size() || joints[bound.bone].parent < 0)
                continue;
            const Point& head = joints[joints[bound.bone].parent].position;
            const Point& tail = joints[bound.bone].position;
            const double dx = tail.x - head.x;
            const double dy = tail.y - head.y;
            const double length = std::hypot(dx, dy);
            if (length < 1.0e-9)
                continue;
            const Point& p = anchors[point];
            const double along = ((p.x - head.x) * dx + (p.y - head.y) * dy) / (length * length);
            const double side = (dx * (p.y - head.y) - dy * (p.x - head.x)) / length;
            const double distance = distanceToSegment(p, head, tail);
            // The point's group, then its two handles', which sit on it.
            const double group = static_cast<double>(data.size());
            for (int copy = 0; copy < 3; ++copy)
            {
                data.push_back(along);
                data.push_back(side);
                data.push_back(distance);
                data.push_back(bound.weight);
            }
            indices.push_back(static_cast<double>(bound.bone));
            indices.push_back(group);
            indices.push_back(1.0);
            indices.push_back(group + 4.0);
            indices.push_back(group + 8.0);
        }
        indices.push_back(-1.0);
    }
    tree->SetAttribute("connectionIndices", listOf(indices).c_str());
    tree->SetAttribute("connectionData", listOf(data).c_str());
    tree->SetAttribute("phoenixBinding", "anchors");

    // The outline, its points with their handles (on them) in turn: the
    // first point and its handle out, each other's handle in, the point and
    // its handle out, and the last's handle in and the point.
    for (tinyxml2::XMLElement* old = tree->FirstChildElement("IKContour"); old; old = tree->FirstChildElement("IKContour"))
        tree->DeleteChild(old);
    tinyxml2::XMLElement* outline = _document->NewElement("IKContour");
    tree->InsertEndChild(outline);
    outline->SetAttribute("interior", "true");
    outline->SetAttribute("orientation", "counter clockwise");
    if (!anchors.empty())
    {
        double left = anchors[0].x;
        double top = anchors[0].y;
        double right = left;
        double bottom = top;
        for (const Point& p : anchors)
        {
            left = std::min(left, p.x);
            top = std::min(top, p.y);
            right = std::max(right, p.x);
            bottom = std::max(bottom, p.y);
        }
        outline->SetAttribute("rectLeft", static_cast<int>(std::lround(left * 20.0)));
        outline->SetAttribute("rectTop", static_cast<int>(std::lround(top * 20.0)));
        outline->SetAttribute("rectRight", static_cast<int>(std::lround(right * 20.0)));
        outline->SetAttribute("rectBottom", static_cast<int>(std::lround(bottom * 20.0)));
    }
    Transform toContour;
    if (!inverse(contour, toContour))
        toContour = Transform();
    tinyxml2::XMLElement* points = _document->NewElement("dataPoints");
    outline->InsertEndChild(points);
    for (size_t i = 0; i < anchors.size(); ++i)
    {
        const std::string text = locationText(anchors[i].transformed(toContour));
        const int copies = anchors.size() == 1 ? 1 : (i == 0 || i + 1 == anchors.size() ? 2 : 3);
        for (int copy = 0; copy < copies; ++copy)
        {
            tinyxml2::XMLElement* point = _document->NewElement("DataPoint");
            points->InsertEndChild(point);
            point->SetAttribute("integerPoint", text.c_str());
        }
    }
    tinyxml2::XMLElement* matrixList = _document->NewElement("matrix");
    outline->InsertEndChild(matrixList);
    tinyxml2::XMLElement* matrix = _document->NewElement("Matrix");
    matrixList->InsertEndChild(matrix);
    writeMatrix(matrix, contour);
    if (!style.empty())
    {
        tinyxml2::XMLDocument styles;
        if (styles.Parse(("<style>" + style + "</style>").c_str()) == tinyxml2::XML_SUCCESS)
        {
            for (const tinyxml2::XMLElement* part = styles.FirstChildElement()->FirstChildElement(); part;
                 part = part->NextSiblingElement())
            {
                outline->InsertEndChild(part->DeepClone(_document.get()));
            }
        }
    }
}

std::string symbolArmatureXml(const std::string& name, const std::string& treeName,
    const std::vector<ArmatureJoint>& joints, int frames)
{
    return armatureXml(name, treeName, joints, frames, false);
}

std::string armatureXml(const std::string& name, const std::string& treeName, const std::vector<ArmatureJoint>& joints,
    int frames, bool bendsShape)
{
    frames = std::max(1, frames);
    // Each joint turned toward its first child; one with none keeps its
    // parent's angle.
    const size_t count = joints.size();
    std::vector<double> angles(count, 0.0);
    std::vector<std::vector<size_t>> children(count);
    for (size_t i = 0; i < count; ++i)
    {
        const int parent = joints[i].parent;
        if (parent >= 0 && static_cast<size_t>(parent) < count)
            children[static_cast<size_t>(parent)].push_back(i);
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!children[i].empty())
        {
            const Point& to = joints[children[i].front()].position;
            angles[i] = std::atan2(to.y - joints[i].position.y, to.x - joints[i].position.x);
        }
        else if (joints[i].parent >= 0)
            angles[i] = angles[static_cast<size_t>(joints[i].parent)];
    }

    tinyxml2::XMLDocument document;
    tinyxml2::XMLElement* tree = document.NewElement("IKTree");
    document.InsertEndChild(tree);
    tree->SetAttribute("name", name.c_str());
    tree->SetAttribute("treeName", treeName.c_str());
    tree->SetAttribute("physicsEnabled", "true");

    int bones = 0;
    std::vector<std::string> boneNames(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (!children[i].empty())
            boneNames[i] = "ikBoneName" + std::to_string(++bones);
    }
    const auto describe = [&](tinyxml2::XMLElement* element, size_t i) {
        element->SetAttribute("name", joints[i].node.c_str());
        if (!boneNames[i].empty())
            element->SetAttribute("boneName", boneNames[i].c_str());
        element->SetAttribute("location", locationText(joints[i].position).c_str());
        if (bendsShape)
            element->SetAttribute("nodeType", "lsd");
    };
    // A shape's node keeps its joint's frame as a matrix too.
    const auto world = [&](tinyxml2::XMLElement* element, size_t i) {
        if (!bendsShape)
            return;
        tinyxml2::XMLElement* list = document.NewElement("worldMatrix");
        element->InsertEndChild(list);
        tinyxml2::XMLElement* matrix = document.NewElement("Matrix");
        list->InsertEndChild(matrix);
        Transform frame = turn(angles[i]);
        frame.tx = joints[i].position.x;
        frame.ty = joints[i].position.y;
        writeMatrix(matrix, frame);
    };
    std::function<void(tinyxml2::XMLElement*, size_t)> node = [&](tinyxml2::XMLElement* list, size_t i) {
        tinyxml2::XMLElement* element = document.NewElement("ChildNode");
        list->InsertEndChild(element);
        describe(element, i);
        const size_t span = static_cast<size_t>(frames);
        // A shape's joint lists hold the joint; a symbol's, its instance's
        // registration point.
        const Point& at = bendsShape ? joints[i].position : joints[i].registration;
        element->SetAttribute("xArray", listOf(std::vector<double>(span, at.x * 20.0)).c_str());
        element->SetAttribute("yArray", listOf(std::vector<double>(span, at.y * 20.0)).c_str());
        element->SetAttribute("angleArray", listOf(std::vector<double>(span, angles[i])).c_str());
        if (!joints[i].reference.empty())
            element->SetAttribute("referenceID", joints[i].reference.c_str());
        element->SetAttribute("speed", "1");
        if (joints[i].limit.constrained)
        {
            setNumber(element, "phoenixMinRotation", std::min(joints[i].limit.min, joints[i].limit.max));
            setNumber(element, "phoenixMaxRotation", std::max(joints[i].limit.min, joints[i].limit.max));
        }
        // The pose: the angle from the parent's, and the place in the
        // parent's frame (on the stage for the root), in twips.
        const int parent = joints[i].parent;
        Point offset = joints[i].position;
        double angle = angles[i];
        if (parent >= 0)
        {
            const ArmatureJoint& above = joints[static_cast<size_t>(parent)];
            offset = rotated(Point(offset.x - above.position.x, offset.y - above.position.y), -angles[static_cast<size_t>(parent)]);
            angle -= angles[static_cast<size_t>(parent)];
        }
        tinyxml2::XMLElement* states = document.NewElement("states");
        element->InsertEndChild(states);
        tinyxml2::XMLElement* state = document.NewElement("State");
        states->InsertEndChild(state);
        state->SetAttribute("duration", frames);
        setNumber(state, "angle", angle);
        setNumber(state, "x", offset.x * 20.0);
        setNumber(state, "y", offset.y * 20.0);
        state->SetAttribute("blendMode", 1);
        world(element, i);
        if (!children[i].empty())
        {
            tinyxml2::XMLElement* below = document.NewElement("childNodes");
            element->InsertEndChild(below);
            for (size_t child : children[i])
                node(below, child);
        }
    };
    for (size_t i = 0; i < count; ++i)
    {
        if (joints[i].parent >= 0)
            continue;
        // The root's IKNode, with no pose of its own, over the root joint.
        tinyxml2::XMLElement* root = document.NewElement("IKNode");
        tree->InsertEndChild(root);
        describe(root, i);
        if (!joints[i].reference.empty())
            root->SetAttribute("referenceID", joints[i].reference.c_str());
        root->SetAttribute("speed", "1");
        world(root, i);
        tinyxml2::XMLElement* list = document.NewElement("childNodes");
        root->InsertEndChild(list);
        node(list, i);
    }
    tinyxml2::XMLElement* treeStates = document.NewElement("states");
    tree->InsertEndChild(treeStates);
    tinyxml2::XMLElement* pose = document.NewElement("IKState");
    treeStates->InsertEndChild(pose);
    pose->SetAttribute("duration", frames);
    pose->SetAttribute("blendMode", 1);

    tinyxml2::XMLPrinter printer(nullptr, true);
    document.Print(&printer);
    return printer.CStr();
}

std::vector<IKJoint> solveIK(std::vector<IKJoint> joints, int effector, const Point& target, int iterations,
    const std::vector<JointLimit>& limits)
{
    if (effector < 0 || static_cast<size_t>(effector) >= joints.size())
        return joints;
    const auto below = [&](size_t joint, int above) {
        for (int at = static_cast<int>(joint); at >= 0; at = joints[at].parent)
        {
            if (at == above)
                return true;
        }
        return false;
    };
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        for (int pivot = joints[effector].parent; pivot >= 0; pivot = joints[pivot].parent)
        {
            const Point centre = joints[pivot].position;
            const Point end = joints[effector].position;
            if (std::hypot(end.x - centre.x, end.y - centre.y) < 1.0e-9 ||
                std::hypot(target.x - centre.x, target.y - centre.y) < 1.0e-9)
            {
                continue;
            }
            double turn = wrapAngle(std::atan2(target.y - centre.y, target.x - centre.x) -
                std::atan2(end.y - centre.y, end.x - centre.x));
            // No further than the joint's limits from its parent bone.
            if (static_cast<size_t>(pivot) < limits.size() && limits[pivot].constrained)
            {
                const int above = joints[pivot].parent;
                const double relative = joints[pivot].angle - (above >= 0 ? joints[above].angle : 0.0);
                const double low = std::min(limits[pivot].min, limits[pivot].max);
                const double high = std::max(limits[pivot].min, limits[pivot].max);
                turn = std::max(low, std::min(high, relative + turn)) - relative;
            }
            for (size_t i = 0; i < joints.size(); ++i)
            {
                if (!below(i, pivot))
                    continue;
                joints[i].angle += turn;
                const Point offset = rotated(Point(joints[i].position.x - centre.x, joints[i].position.y - centre.y), turn);
                joints[i].position = Point(centre.x + offset.x, centre.y + offset.y);
            }
        }
        const Point end = joints[effector].position;
        if (std::hypot(end.x - target.x, end.y - target.y) < 0.01)
            break;
    }
    return joints;
}

Point skinPoint(const Point& point, const std::vector<IKJoint>& before, const std::vector<IKJoint>& after)
{
    double total = 0.0;
    Point sum;
    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
    {
        const int parent = before[i].parent;
        if (parent < 0 || static_cast<size_t>(parent) >= after.size())
            continue;
        const IKJoint& from = before[parent];
        const IKJoint& to = after[parent];
        const double d = distanceToSegment(point, from.position, before[i].position);
        const double weight = 1.0 / std::pow(d * d + 1.0, 2.0);
        const Point local = rotated(Point(point.x - from.position.x, point.y - from.position.y), to.angle - from.angle);
        sum.x += weight * (to.position.x + local.x);
        sum.y += weight * (to.position.y + local.y);
        total += weight;
    }
    if (total <= 0.0)
        return point;
    return Point(sum.x / total, sum.y / total);
}

void reskinShape(Shape& shape, const Transform& toArmature, const std::vector<IKJoint>& before,
    const std::vector<IKJoint>& after)
{
    Transform toShape = toArmature;
    const double det = toArmature.m11 * toArmature.m22 - toArmature.m12 * toArmature.m21;
    if (std::fabs(det) < 1.0e-12)
        return;
    toShape.m11 = toArmature.m22 / det;
    toShape.m12 = -toArmature.m12 / det;
    toShape.m21 = -toArmature.m21 / det;
    toShape.m22 = toArmature.m11 / det;
    toShape.tx = -(toShape.m11 * toArmature.tx + toShape.m21 * toArmature.ty);
    toShape.ty = -(toShape.m12 * toArmature.tx + toShape.m22 * toArmature.ty);
    warpShape(shape, [&](const Point& p) {
        return skinPoint(p.transformed(toArmature), before, after).transformed(toShape);
    });
}

std::vector<Point> shapeAnchors(const Shape& shape)
{
    AnchorIndex index;
    index.walk(shape);
    return index.anchors;
}

std::string contourStyle(const Shape& shape)
{
    char color[16];
    const auto solid = [&color](const FillStyle* fill) {
        if (!fill || fill->type() != FillStyle::Type::SolidColor)
            return false;
        const SolidColor* paint = static_cast<const SolidColor*>(fill);
        std::snprintf(color, sizeof(color), "#%02X%02X%02X", paint->color[0], paint->color[1], paint->color[2]);
        return true;
    };
    std::string style;
    const StrokeStyle* stroke = shape.strokes.empty() ? nullptr : shape.strokes.front();
    if (stroke && solid(stroke->fill))
    {
        char weight[32];
        std::snprintf(weight, sizeof(weight), "%g", stroke->weight);
        style += std::string("<SolidStroke scaleMode=\"normal\"") + (stroke->weight != 1.0 ? std::string(" weight=\"") + weight + "\"" : "") +
            "><fill><SolidColor color=\"" + color + "\"/></fill></SolidStroke>";
    }
    if (!shape.fills.empty() && solid(shape.fills.front()))
        style += std::string("<SolidColor color=\"") + color + "\"/>";
    return style;
}

ShapeBinding bindToNearestBones(const std::vector<Point>& anchors, const std::vector<IKJoint>& joints)
{
    ShapeBinding binding(anchors.size());
    for (size_t point = 0; point < anchors.size(); ++point)
    {
        std::vector<std::pair<double, int>> distances;
        for (size_t i = 0; i < joints.size(); ++i)
        {
            const int parent = joints[i].parent;
            if (parent < 0 || static_cast<size_t>(parent) >= joints.size())
                continue;
            distances.emplace_back(distanceToSegment(anchors[point], joints[parent].position, joints[i].position),
                static_cast<int>(i));
        }
        if (distances.empty())
            continue;
        double nearest = distances.front().first;
        for (const auto& found : distances)
            nearest = std::min(nearest, found.first);
        // As near one bone as another to a hundredth of a pixel: both.
        for (const auto& found : distances)
        {
            if (found.first <= nearest + 0.01)
                binding[point].push_back({found.second, 1.0});
        }
        for (BoneWeight& bound : binding[point])
            bound.weight = 1.0 / static_cast<double>(binding[point].size());
    }
    return binding;
}

void bindShape(Shape& shape, const Transform& toArmature, const ShapeBinding& binding,
    const std::vector<IKJoint>& before, const std::vector<IKJoint>& after)
{
    Transform toShape;
    if (!inverse(toArmature, toShape))
        return;
    AnchorIndex index;
    index.walk(shape);
    if (index.anchors.size() != binding.size())
        return;

    // A point carried by its bones: each moves it rigidly with the frame of
    // the joint at its head, and the places are averaged by weight.
    const auto carry = [&](const Point& p, const std::vector<BoneWeight>& bones) {
        Point sum;
        double total = 0.0;
        for (const BoneWeight& bound : bones)
        {
            if (bound.bone < 0 || static_cast<size_t>(bound.bone) >= before.size() ||
                static_cast<size_t>(bound.bone) >= after.size() || bound.weight <= 0.0)
            {
                continue;
            }
            const int parent = before[bound.bone].parent;
            if (parent < 0 || static_cast<size_t>(parent) >= after.size())
                continue;
            const IKJoint& from = before[parent];
            const IKJoint& to = after[parent];
            const Point local = rotated(Point(p.x - from.position.x, p.y - from.position.y), to.angle - from.angle);
            sum.x += bound.weight * (to.position.x + local.x);
            sum.y += bound.weight * (to.position.y + local.y);
            total += bound.weight;
        }
        return total > 0.0 ? Point(sum.x / total, sum.y / total) : p;
    };
    // Where each point goes, by where it is: a curve's control point goes
    // with the points either side of it, half by each. The points first, so
    // a control point lying on one does not decide where it goes.
    std::map<std::pair<long long, long long>, Point> moved;
    const auto place = [&](const Point& p, const Point& to) {
        moved.emplace(AnchorIndex::key(p), to.transformed(toShape));
    };
    const auto pass = [&](bool controls) {
        for (const Edge* edge : shape.edges)
        {
            for (const Path* path : edge ? edge->paths : std::vector<Path*>())
            {
                int previous = -1;
                for (const PathSegment* segment : path ? path->segments : std::vector<PathSegment*>())
                {
                    if (!segment || segment->command == PathSegment::Command::Close || segment->points.empty())
                        continue;
                    const int number = index.of(segment->points.back());
                    const std::vector<BoneWeight>& own = binding[static_cast<size_t>(number)];
                    const std::vector<BoneWeight>& last = previous >= 0 ? binding[static_cast<size_t>(previous)] : own;
                    for (size_t i = 0; i < segment->points.size(); ++i)
                    {
                        const Point& p = segment->points[i];
                        const Point at = p.transformed(toArmature);
                        if ((i + 1 != segment->points.size()) != controls)
                            continue;
                        if (i + 1 == segment->points.size())
                            place(p, carry(at, own));
                        else if (segment->command == PathSegment::Command::Cubic)
                            place(p, carry(at, i == 0 ? last : own));
                        else
                        {
                            const Point a = carry(at, last);
                            const Point b = carry(at, own);
                            place(p, Point((a.x + b.x) / 2.0, (a.y + b.y) / 2.0));
                        }
                    }
                    previous = number;
                }
            }
        }
    };
    pass(false);
    pass(true);
    warpShape(shape, [&](const Point& p) {
        const auto found = moved.find(AnchorIndex::key(p));
        return found != moved.end() ? found->second : p;
    });
}

Layer* newArmatureLayer(Timeline& timeline, const Layer& below, const std::string& name, int start, int length,
    Frame*& pose)
{
    Layer* layer = newLayerFor(timeline, false);
    layer->name = name;
    layer->autoNamed = false;
    layer->extraAttributes.emplace_back("animationType", "IK pose");
    if (layer->frames.empty())
        layer->frames.push_back(new Frame(layer));
    pose = layer->frames.front();
    pose->index = start;
    pose->duration = length;
    if (start > 0)
    {
        Frame* empty = new Frame(layer);
        empty->index = 0;
        empty->duration = start;
        layer->frames.insert(layer->frames.begin(), empty);
    }
    pose->otherTweenType = "IK pose";
    pose->keyMode = "9732";
    pose->extraAttributes.emplace_back("isIKPose", "true");
    pose->extraAttributes.emplace_back("poseLocations", "0");
    layer->parentLayer = below.parentLayer && below.parentLayer->layerType == Layer::Type::Folder ? below.parentLayer : nullptr;
    return layer;
}

BonedShape boneShape(Timeline& timeline, const Layer& layer, const Frame& shapeFrame, const Shape& shape,
    const std::vector<ArmatureJoint>& joints, const std::string& symbolName, const std::string& armatureName,
    const std::string& treeName)
{
    BonedShape boned;
    const std::vector<Point> local = shapeAnchors(shape);
    if (local.empty() || joints.size() < 2)
        return boned;
    // The middle of the shape's points, to a twip.
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    for (size_t i = 0; i < local.size(); ++i)
    {
        const Point at = local[i].transformed(shape.transform);
        left = i == 0 ? at.x : std::min(left, at.x);
        top = i == 0 ? at.y : std::min(top, at.y);
        right = i == 0 ? at.x : std::max(right, at.x);
        bottom = i == 0 ? at.y : std::max(bottom, at.y);
    }
    const Point middle(std::round((left + right) * 10.0) / 20.0, std::round((top + bottom) * 10.0) / 20.0);
    const int length = std::max(1, shapeFrame.duration);

    // The container: the shape on a frame of its own for each of the span's,
    // which posing bends one by one.
    Symbol* symbol = new Symbol(nullptr);
    symbol->name = symbolName;
    symbol->symbolType = SymbolType::Graphic;
    symbol->otherSymbolType = "ik container";
    Timeline* inside = new Timeline(symbol);
    inside->name = symbolName;
    symbol->timelines.push_back(inside);
    Layer* art = new Layer(inside);
    art->name = "Layer_1";
    art->color[0] = 0;
    art->color[1] = 255;
    art->color[2] = 255;
    art->color[3] = 255;
    inside->layers.push_back(art);
    Shape* rest = nullptr;
    for (int k = 0; k < length; ++k)
    {
        Frame* frame = new Frame(art);
        frame->index = k;
        frame->duration = 1;
        frame->keyMode = "9728";
        Shape* copy = cloneShape(shape, frame);
        const Transform placed = shape.transform;
        warpShape(*copy, [&](const Point& p) {
            const Point at = p.transformed(placed);
            return Point(at.x - middle.x, at.y - middle.y);
        });
        copy->transform = Transform();
        copy->isDrawingObject = false;
        frame->elements.push_back(copy);
        art->frames.push_back(frame);
        if (!rest)
            rest = copy;
    }
    art->firstFrame = 0;
    art->lastFrame = length;

    Frame* pose = nullptr;
    Layer* armatureLayer = newArmatureLayer(timeline, layer, armatureName, shapeFrame.index, length, pose);
    SymbolInstance* instance = new SymbolInstance(pose);
    instance->libraryItemName = symbolName;
    instance->symbol = symbol;
    instance->symbolType = SymbolType::Graphic;
    instance->loopType = LoopType::PlayOnce;
    instance->firstFrame = 0;
    instance->transform.tx = middle.x;
    instance->transform.ty = middle.y;
    pose->elements.push_back(instance);

    std::vector<ArmatureJoint> lsd = joints;
    for (ArmatureJoint& joint : lsd)
    {
        joint.registration = joint.position;
        joint.reference.clear();
    }
    IKArmature armature;
    armature.parse(armatureXml(armatureName, treeName, lsd, length, true));
    std::vector<Point> anchors;
    for (const Point& p : shapeAnchors(*rest))
        anchors.push_back(p.transformed(instance->transform));
    armature.setBinding(bindToNearestBones(anchors, armature.jointsAt(0)), anchors, instance->transform,
        contourStyle(shape));
    pose->extraChildren.push_back("<betweenFrameMatrixList><Matrix/></betweenFrameMatrixList>");
    pose->extraChildren.push_back(armature.xml());

    boned.symbol = symbol;
    boned.layer = armatureLayer;
    boned.pose = pose;
    boned.instance = instance;
    return boned;
}

void setBetweenFrames(std::vector<std::string>& children, const std::string& list)
{
    const auto starts = [](const std::string& child, const char* tag) {
        const size_t at = child.find('<');
        return at != std::string::npos && child.compare(at, std::strlen(tag), tag) == 0;
    };
    for (std::string& child : children)
    {
        if (starts(child, "<betweenFrameMatrixList"))
        {
            child = list;
            return;
        }
    }
    auto at = children.begin();
    while (at != children.end() && !starts(*at, "<IKTree"))
        ++at;
    children.insert(at, list);
}

// ---------------------------------------------------------------------------

PoseArmatureCommand::PoseArmatureCommand(Frame* frame, size_t child, const std::string& before,
    const std::string& after, const std::string& posesBefore, const std::string& posesAfter)
    : _frame(frame)
    , _child(child)
    , _before(before)
    , _after(after)
    , _posesBefore(posesBefore)
    , _posesAfter(posesAfter)
{
}

void PoseArmatureCommand::apply(Frame* frame, size_t child, const std::string& xml, const std::string& poses)
{
    if (!frame || child >= frame->extraChildren.size())
        return;
    frame->extraChildren[child] = xml;
    for (auto& attribute : frame->extraAttributes)
    {
        if (attribute.first == "poseLocations")
        {
            attribute.second = poses;
            return;
        }
    }
    frame->extraAttributes.emplace_back("poseLocations", poses);
}

void PoseArmatureCommand::redo()
{
    apply(_frame, _child, _after, _posesAfter);
}

void PoseArmatureCommand::undo()
{
    apply(_frame, _child, _before, _posesBefore);
}

void carryPoses(const IKArmature& old, IKArmature& rebuilt)
{
    const std::vector<ArmatureJoint> was = old.symbolJoints();
    const std::vector<ArmatureJoint> now = rebuilt.symbolJoints();
    const std::vector<IKJoint> first = rebuilt.posedAt(0);
    if (first.size() != now.size())
        return;
    for (int pose : old.poses())
    {
        if (pose <= 0)
            continue;
        const std::vector<IKJoint> there = old.posedAt(pose);
        std::vector<double> angles(now.size(), 0.0);
        std::vector<size_t> added;
        for (size_t j = 0; j < now.size(); ++j)
        {
            const auto found = std::find_if(was.begin(), was.end(),
                [&](const ArmatureJoint& joint) { return joint.node == now[j].node; });
            const size_t index = static_cast<size_t>(found - was.begin());
            if (found != was.end() && index < there.size())
                angles[j] = there[index].angle;
            else
                added.push_back(j);
        }
        for (size_t j : added)
        {
            const int parent = first[j].parent;
            angles[j] = parent >= 0 ? angles[static_cast<size_t>(parent)] + first[j].angle - first[static_cast<size_t>(parent)].angle
                                    : first[j].angle;
        }
        rebuilt.setPose(pose, angles);
    }
    // And the eases between them.
    rebuilt.copyEases(old);
}

} // namespace fla
