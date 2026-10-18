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
#include <cstdint>
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

/// \a angle as a rotation limit is stored: from 0 up to a whole turn.
double turnFromZero(double angle)
{
    angle = std::fmod(angle, 2.0 * kPi);
    return angle < 0.0 ? angle + 2.0 * kPi : angle;
}

/// Writes a joint's rotation limits as FLA files keep them: rotationMin and
/// rotationMax in radians from 0 up to a whole turn (-45 degrees is 315).
void writeLimit(tinyxml2::XMLElement* element, const JointLimit& limit)
{
    for (const char* name : {"rotationConstrained", "rotationMin", "rotationMax", "phoenixMinRotation", "phoenixMaxRotation"})
        element->DeleteAttribute(name);
    if (!limit.constrained)
        return;
    element->SetAttribute("rotationConstrained", "true");
    setNumber(element, "rotationMin", turnFromZero(std::min(limit.min, limit.max)));
    setNumber(element, "rotationMax", turnFromZero(std::max(limit.min, limit.max)));
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
/// as FLA files store them.
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
    _sprung.clear();
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

    // Phoenix once kept limits under names of its own (phoenixMinRotation,
    // phoenixMaxRotation, in radians from -180 degrees): now as FLA files
    // keep them. And springs on the joint they turn (springStrength,
    // springDamping), played as the armature plays rather than stored in
    // its frames: those stay until the joints are worked out again.
    _legacySprings = false;
    for (Node& node : _nodes)
    {
        tinyxml2::XMLElement* element = node.element;
        if (element->Attribute("phoenixMinRotation") && element->Attribute("phoenixMaxRotation"))
        {
            writeLimit(element, {true, element->DoubleAttribute("phoenixMinRotation", 0.0),
                element->DoubleAttribute("phoenixMaxRotation", 0.0)});
        }
        if (element->Attribute("springDamping"))
            _legacySprings = true;
    }

    // An armature of symbols keeps each instance's registration point in
    // xArray, and the joint where its first pose puts it: how far apart they
    // are turns with the joint. (A node's location is the joint for the
    // root, but not always below it.)
    if (_bendsShape)
        return;
    const std::vector<IKJoint> first = posedAt(0);
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        Node& node = _nodes[i];
        const std::vector<double> xs = numbers(node.element->Attribute("xArray"));
        const std::vector<double> ys = numbers(node.element->Attribute("yArray"));
        const std::vector<double> angles = numbers(node.element->Attribute("angleArray"));
        Point joint;
        if (xs.empty() || ys.empty())
            continue;
        if (i < first.size() && node.element->FirstChildElement("states")->FirstChildElement("State"))
            joint = first[i].position;
        else if (!parseLocation(node.element->Attribute("location"), joint))
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
    measure(joints);
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
    measure(joints);
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
    measure(joints);
    return joints;
}

void IKArmature::storeJoints()
{
    _sprung.clear();
    if (_legacySprings)
        upgradeSprings();
    const int count = frames();
    // The poses, and the springs at work on them: FLA files store the
    // joints as the springs swing them.
    std::vector<std::vector<IKJoint>> posed(static_cast<size_t>(std::max(0, count)));
    for (int frame = 0; frame < count; ++frame)
        posed[static_cast<size_t>(frame)] = posedAt(frame);
    // Locks, targets and bone options, worked into the poses they move and
    // then into every frame.
    if (hasGoals())
    {
        posed = withGoals(posed);
        if (writeBackPoses(posed))
        {
            for (int frame = 0; frame < count; ++frame)
                posed[static_cast<size_t>(frame)] = posedAt(frame);
            posed = withGoals(posed);
        }
    }
    if (hasSprings())
        posed = simulate([&posed](int frame) { return posed[static_cast<size_t>(frame)]; });
    std::vector<std::vector<double>> xs(_nodes.size()), ys(_nodes.size()), angles(_nodes.size());
    for (int frame = 0; frame < count; ++frame)
    {
        const std::vector<IKJoint>& joints = posed[static_cast<size_t>(frame)];
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
        // Stored from 0 up to a whole turn: the least from -180 degrees,
        // and the most that far or less round from it.
        JointLimit limit;
        limit.constrained = node.element->BoolAttribute("rotationConstrained", false);
        if (limit.constrained)
        {
            limit.min = wrapAngle(node.element->DoubleAttribute("rotationMin", 0.0));
            limit.max = limit.min + turnFromZero(node.element->DoubleAttribute("rotationMax", 0.0) - limit.min);
        }
        found.push_back(limit);
    }
    return found;
}

void IKArmature::setLimit(int joint, const JointLimit& limit)
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return;
    writeLimit(_nodes[joint].element, limit);
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
    // Springs Phoenix once played rather than stored swing the joint from
    // where its poses have it: its sprung angle, and the registration point
    // that hangs off it.
    if (playsSprings())
    {
        const std::vector<IKJoint> sprung = sprungAt(frame);
        if (joint >= 0 && static_cast<size_t>(joint) < sprung.size())
        {
            nowAngle = sprung[static_cast<size_t>(joint)].angle;
            const Point offset = rotated(_nodes[static_cast<size_t>(joint)].registration, nowAngle);
            now = Point(sprung[static_cast<size_t>(joint)].position.x + offset.x,
                sprung[static_cast<size_t>(joint)].position.y + offset.y);
        }
    }
    // A joint whose bone stretches scales what hangs on it too.
    for (size_t i = 0; i < _nodes.size() && !playsSprings(); ++i)
    {
        if (_nodes[i].parent == joint && _nodes[i].element->Attribute("phoenixLength"))
            return then(stored, boneMove(jointsAt(0), jointsAt(std::min(frame, frames() - 1)), joint));
    }
    // Off the first frame's registration point, turned, onto this frame's.
    Transform moved = stored;
    moved.tx -= first.x;
    moved.ty -= first.y;
    moved = then(moved, turn(nowAngle - firstAngle));
    moved.tx += now.x;
    moved.ty += now.y;
    return moved;
}

std::string IKArmature::betweenFrameMatrixList(const std::vector<std::pair<int, const Transform*>>& instances) const
{
    tinyxml2::XMLDocument document;
    tinyxml2::XMLElement* list = document.NewElement("betweenFrameMatrixList");
    document.InsertEndChild(list);
    const int count = std::max(1, frames());
    for (const auto& [joint, stored] : instances)
    {
        Transform back;
        if (!stored || joint < 0 || static_cast<size_t>(joint) >= _nodes.size() || !inverse(*stored, back))
            continue;
        // Each frame's: the move from where the first frame has it.
        for (int frame = 0; frame < count; ++frame)
        {
            tinyxml2::XMLElement* element = document.NewElement("Matrix");
            list->InsertEndChild(element);
            writeMatrix(element, then(back, carried(joint, *stored, frame)));
        }
    }
    tinyxml2::XMLPrinter printer(nullptr, true);
    document.Print(&printer);
    return printer.CStr();
}

void IKArmature::setSpring(int joint, const BoneSpring& spring)
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return;
    if (_legacySprings)
        upgradeSprings();
    // On the node at each bone's tail: springStrength, and dampingValue
    // unless it is 0.
    for (const Node& node : _nodes)
    {
        if (node.parent != joint)
            continue;
        for (const char* name : {"springStrength", "dampingValue", "springDamping"})
            node.element->DeleteAttribute(name);
        if (spring.strength <= 0.0)
            continue;
        setNumber(node.element, "springStrength", std::clamp(spring.strength, 0.0, 100.0));
        if (spring.damping > 0.0)
            setNumber(node.element, "dampingValue", std::clamp(spring.damping, 0.0, 100.0));
    }
    storeJoints();
}

void IKArmature::upgradeSprings()
{
    // Each joint's spring moves to the nodes at the tails of its bones.
    std::vector<BoneSpring> springs(_nodes.size());
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        tinyxml2::XMLElement* element = _nodes[i].element;
        if (!element->Attribute("springDamping"))
            continue;
        springs[i].present = true;
        springs[i].strength = element->DoubleAttribute("springStrength", 0.0);
        springs[i].damping = element->DoubleAttribute("springDamping", 0.0);
        element->DeleteAttribute("springStrength");
        element->DeleteAttribute("springDamping");
    }
    for (const Node& node : _nodes)
    {
        if (node.parent < 0 || !springs[static_cast<size_t>(node.parent)].present)
            continue;
        const BoneSpring& spring = springs[static_cast<size_t>(node.parent)];
        if (spring.strength <= 0.0)
            continue;
        setNumber(node.element, "springStrength", spring.strength);
        if (spring.damping > 0.0)
            setNumber(node.element, "dampingValue", spring.damping);
    }
    _legacySprings = false;
    _sprung.clear();
}

bool IKArmature::hasSprings() const
{
    // The armature's springs can be turned off as a whole.
    const tinyxml2::XMLElement* tree = _document ? _document->FirstChildElement("IKTree") : nullptr;
    if (tree && !tree->BoolAttribute("physicsEnabled", true))
        return false;
    for (size_t i = 0; i < _nodes.size(); ++i)
        if (spring(static_cast<int>(i)).strength > 0.0)
            return true;
    return false;
}

bool IKArmature::playsSprings() const
{
    return _legacySprings && hasSprings();
}

void IKArmature::setFrameRate(double fps)
{
    if (fps > 0.0 && fps != _fps)
    {
        _fps = fps;
        _sprung.clear();
    }
}

std::vector<IKJoint> IKArmature::sprungAt(int frame) const
{
    // Stored with the springs at work, as FLA files keep them.
    if (!playsSprings())
        return shownAt(frame);
    const int count = std::max(1, frames());
    if (_sprung.empty())
        _sprung = simulate([this](int f) { return shownAt(f); });
    return _sprung[static_cast<size_t>(std::clamp(frame, 0, count - 1))];
}

namespace {

/// Smooth noise over time, about -1 to 1, the same for the same seed: values
/// at whole times eased between.
double windNoise(int seed, double t)
{
    const auto at = [seed](long long n) {
        uint32_t h = static_cast<uint32_t>(n) * 0x9E3779B1u ^ static_cast<uint32_t>(seed) * 0x85EBCA77u;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        return static_cast<double>(h) / 4294967295.0 * 2.0 - 1.0;
    };
    const auto octave = [&at](double time) {
        const double whole = std::floor(time);
        const double f = time - whole;
        const long long n = static_cast<long long>(whole);
        return at(n) + (at(n + 1) - at(n)) * f * f * (3.0 - 2.0 * f);
    };
    return 0.65 * octave(t) + 0.35 * octave(2.3 * t + 17.0);
}

/// How gravity and wind turn a sprung bone, in radians a second squared: a
/// pull toward each one's direction, strongest across it.
struct ForceField
{
    BoneForces forces;

    /// On a bone pointing \a direction (radians) at \a time seconds into the
    /// span; \a lag delays the wind's gusts, so they run down a chain.
    double pull(double direction, double time, double lag) const
    {
        // At 100, the softest spring gives way nearly all the way.
        constexpr double kStrongest = 200.0;
        constexpr double kRadians = kPi / 180.0;
        double turn = 0.0;
        if (forces.gravity > 0.0)
            turn += kStrongest * forces.gravity / 100.0 * std::sin(forces.gravityAngle * kRadians - direction);
        if (forces.wind > 0.0)
        {
            const double gust = std::clamp(forces.gusts, 0.0, 100.0) / 100.0;
            const double t = (time - lag) * std::max(forces.gustRate, 0.0);
            const double strength = kStrongest * forces.wind / 100.0 * std::max(0.0, 1.0 + 1.2 * gust * windNoise(forces.seed, t));
            const double angle = forces.windAngle * kRadians + 0.35 * gust * windNoise(forces.seed + 7919, 1.7 * t + 31.0);
            turn += strength * std::sin(angle - direction);
        }
        return turn;
    }
};

/// The way the bones from each joint point, in the joint's own frame: toward
/// its children, on average; 0 for a joint with none.
std::vector<double> bonesPointing(const std::vector<IKJoint>& joints)
{
    std::vector<Point> sum(joints.size());
    for (const IKJoint& joint : joints)
    {
        if (joint.parent < 0 || static_cast<size_t>(joint.parent) >= joints.size())
            continue;
        const IKJoint& from = joints[static_cast<size_t>(joint.parent)];
        const double dx = joint.position.x - from.position.x;
        const double dy = joint.position.y - from.position.y;
        const double length = std::hypot(dx, dy);
        if (length < 1.0e-9)
            continue;
        sum[static_cast<size_t>(joint.parent)].x += dx / length;
        sum[static_cast<size_t>(joint.parent)].y += dy / length;
    }
    std::vector<double> pointing(joints.size(), 0.0);
    for (size_t i = 0; i < joints.size(); ++i)
    {
        if (sum[i].x != 0.0 || sum[i].y != 0.0)
            pointing[i] = std::atan2(sum[i].y, sum[i].x) - joints[i].angle;
    }
    return pointing;
}

/// Where a bone on a spring of \a omega comes to rest from \a wanted under
/// \a pull (of its angle), let swing critically damped for three seconds.
double settled(double wanted, double omega, const std::function<double(double)>& pull)
{
    constexpr double dt = 1.0 / 240.0;
    double angle = wanted;
    double speed = 0.0;
    for (int step = 0; step < 720; ++step)
    {
        speed += (omega * omega * (wanted - angle) - 2.0 * omega * speed + pull(angle)) * dt;
        angle += speed * dt;
    }
    return angle;
}

} // namespace

std::vector<std::vector<IKJoint>> IKArmature::simulate(const std::function<std::vector<IKJoint>(int)>& posedOn) const
{
    const int count = std::max(1, frames());
    // Each joint's spring: how fast it swings (a stiffer one swings faster
    // and lags less) and how much of each swing it loses.
    constexpr double kTau = 6.28318530717958647692;
    std::vector<double> omega(_nodes.size(), 0.0);
    std::vector<double> zeta(_nodes.size(), 1.0);
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        const BoneSpring each = spring(static_cast<int>(i));
        if (each.strength <= 0.0)
            continue;
        omega[i] = kTau * (0.5 + 4.5 * std::min(each.strength, 100.0) / 100.0);
        zeta[i] = 0.05 + 0.9 * std::clamp(each.damping, 0.0, 100.0) / 100.0;
    }
    std::vector<double> angle(_nodes.size(), 0.0);
    std::vector<double> speed(_nodes.size(), 0.0);
    std::vector<double> target(_nodes.size(), 0.0);
    constexpr int kSteps = 8;
    const double dt = 1.0 / (_fps * kSteps);
    // Gravity and wind, and how deep each joint hangs, which their gusts
    // reach a little later.
    const ForceField field{forces()};
    const bool pushed = field.forces.any();
    std::vector<double> lag(_nodes.size(), 0.0);
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        const int parent = _nodes[i].parent;
        if (parent >= 0 && static_cast<size_t>(parent) < i)
            lag[i] = lag[static_cast<size_t>(parent)] + 0.08;
    }
    std::vector<std::vector<IKJoint>> sprung(static_cast<size_t>(count));
    for (int f = 0; f < count; ++f)
    {
        const std::vector<IKJoint> posed = posedOn(f);
        std::vector<IKJoint> joints = posed;
        // The way each joint's bones point from it, in its own frame.
        std::vector<double> pointing(posed.size(), 0.0);
        if (pushed)
            pointing = bonesPointing(posed);
        // Parents come before their children, so each joint hangs off its
        // parent as the springs have already moved it; a root turns toward
        // its pose.
        for (size_t i = 0; i < joints.size(); ++i)
        {
            const int parent = posed[i].parent;
            double wanted = posed[i].angle;
            if (parent >= 0 && static_cast<size_t>(parent) < i)
            {
                const IKJoint& above = posed[static_cast<size_t>(parent)];
                const IKJoint& moved = joints[static_cast<size_t>(parent)];
                // Its place and turn from its parent, as posed.
                const Point offset = rotated(Point(posed[i].position.x - above.position.x,
                    posed[i].position.y - above.position.y), -above.angle);
                const Point placed = rotated(offset, moved.angle);
                joints[i].position = Point(moved.position.x + placed.x, moved.position.y + placed.y);
                wanted = moved.angle + (posed[i].angle - above.angle);
            }
            if (omega[i] <= 0.0 || f == 0)
            {
                angle[i] = wanted;
                speed[i] = 0.0;
                target[i] = wanted;
                // The span starts with the bone settled under gravity and
                // the wind as they are then.
                if (pushed && omega[i] > 0.0)
                    angle[i] = settled(wanted, omega[i], [&](double at) {
                        return field.pull(at + pointing[i], 0.0, lag[i]);
                    });
                joints[i].angle = angle[i];
                continue;
            }
            // The nearest turn of the angle wanted, so a pose that crosses
            // half a turn does not send it the long way round.
            wanted += kTau * std::round((angle[i] - wanted) / kTau);
            const double from = target[i];
            for (int s = 1; s <= kSteps; ++s)
            {
                const double goal = from + (wanted - from) * s / kSteps;
                double pull = omega[i] * omega[i] * (goal - angle[i]) - 2.0 * zeta[i] * omega[i] * speed[i];
                if (pushed)
                    pull += field.pull(angle[i] + pointing[i], (f - 1 + static_cast<double>(s) / kSteps) / _fps, lag[i]);
                speed[i] += pull * dt;
                angle[i] += speed[i] * dt;
            }
            target[i] = wanted;
            joints[i].angle = angle[i];
        }
        sprung[static_cast<size_t>(f)] = std::move(joints);
    }
    return sprung;
}

BoneSpring IKArmature::spring(int joint) const
{
    BoneSpring found;
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return found;
    // Phoenix's own, once: on the joint itself.
    if (_legacySprings)
    {
        const tinyxml2::XMLElement* element = _nodes[static_cast<size_t>(joint)].element;
        found.present = element->Attribute("springDamping") != nullptr;
        found.strength = element->DoubleAttribute("springStrength", 0.0);
        found.damping = element->DoubleAttribute("springDamping", 0.0);
        return found;
    }
    // On the node at the tail of a bone the joint turns.
    for (const Node& node : _nodes)
    {
        if (node.parent != joint || !node.element->Attribute("springStrength"))
            continue;
        found.present = true;
        found.strength = node.element->DoubleAttribute("springStrength", 0.0);
        found.damping = node.element->DoubleAttribute("dampingValue", 0.0);
        break;
    }
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

void IKArmature::readBinding(const std::function<void(int, const BoneWeight&, const double*)>& each) const
{
    const tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const std::vector<double> indices = numbers(tree ? tree->Attribute("connectionIndices") : nullptr);
    const std::vector<double> data = numbers(tree ? tree->Attribute("connectionData") : nullptr);
    if (indices.empty())
        return;
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
        while (at < indices.size() && indices[at] >= 0.0)
        {
            BoneWeight bound;
            bound.bone = static_cast<int>(indices[at]);
            const size_t group = at + 1 < indices.size() ? static_cast<size_t>(std::max(0.0, indices[at + 1])) : 0;
            const size_t handles = at + 2 < indices.size() ? static_cast<size_t>(std::max(0.0, indices[at + 2])) : 0;
            const bool grouped = group + 3 < data.size();
            bound.weight = grouped ? data[group + 3] : 1.0;
            each(number, bound, grouped ? &data[group] : nullptr);
            at += 3 + 2 * handles;
        }
        ++at;
    }
}

ShapeBinding IKArmature::binding() const
{
    ShapeBinding found;
    readBinding([&found](int number, const BoneWeight& bound, const double*) {
        if (found.size() <= static_cast<size_t>(number))
            found.resize(static_cast<size_t>(number) + 1);
        found[static_cast<size_t>(number)].push_back(bound);
    });
    // As many points as the outline has, though its last points are bound
    // to no bone.
    const tinyxml2::XMLElement* tree = _document->FirstChildElement("IKTree");
    const std::vector<double> indices = numbers(tree ? tree->Attribute("connectionIndices") : nullptr);
    if (!found.empty() && !indices.empty() && indices[0] > static_cast<double>(found.size()) && indices[0] <= 100000.0)
        found.resize(static_cast<size_t>(indices[0]));
    return found;
}

ShapeBinding IKArmature::bindingFor(const std::vector<Point>& anchors) const
{
    // Where each bound point was: along its first bone, and to its side, as
    // the span's first frame has the bones.
    const std::vector<IKJoint> joints = jointsAt(0);
    std::map<int, Point> places;
    ShapeBinding bound;
    readBinding([&](int number, const BoneWeight& weight, const double* group) {
        if (static_cast<size_t>(number) >= bound.size())
            bound.resize(static_cast<size_t>(number) + 1);
        bound[static_cast<size_t>(number)].push_back(weight);
        if (!group || places.count(number) || weight.bone < 0 || static_cast<size_t>(weight.bone) >= joints.size() ||
            joints[weight.bone].parent < 0)
        {
            return;
        }
        const Point& head = joints[joints[weight.bone].parent].position;
        const Point& tail = joints[weight.bone].position;
        const double length = std::hypot(tail.x - head.x, tail.y - head.y);
        if (length < 1.0e-9)
            return;
        const Point along((tail.x - head.x) / length, (tail.y - head.y) / length);
        places[number] = Point(head.x + (tail.x - head.x) * group[0] - along.y * group[1],
            head.y + (tail.y - head.y) * group[0] + along.x * group[1]);
    });
    if (places.empty())
        return ShapeBinding();
    // Each anchor takes the bones of the point bound on it; one with none
    // there, its nearest bones.
    const ShapeBinding nearest = bindToNearestBones(anchors, joints);
    ShapeBinding found(anchors.size());
    size_t matched = 0;
    for (size_t i = 0; i < anchors.size(); ++i)
    {
        double best = 1.0;
        for (const auto& [number, place] : places)
        {
            const double distance = std::hypot(anchors[i].x - place.x, anchors[i].y - place.y);
            if (distance < best)
            {
                best = distance;
                found[i] = bound[static_cast<size_t>(number)];
            }
        }
        if (found[i].empty())
            found[i] = nearest[i];
        else
            ++matched;
    }
    return matched * 2 >= anchors.size() ? found : ShapeBinding();
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
        writeLimit(element, joints[i].limit);
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
        const double d = distanceToSegment(point, from.position, before[i].position);
        const double weight = 1.0 / std::pow(d * d + 1.0, 2.0);
        const Point carried = carryOnBone(point, before, after, static_cast<int>(i));
        sum.x += weight * carried.x;
        sum.y += weight * carried.y;
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

bool springShape(Shape& shape, const Shape& first, const Transform& toArmature, const IKArmature& armature, int frame)
{
    if (!armature.bendsShape() || !armature.playsSprings())
        return false;
    const std::vector<IKJoint> sprung = armature.sprungAt(frame);
    // Bound by the Bind tool: bent from the first frame's shape, as posing
    // bends every frame, but to the sprung joints.
    if (armature.bindsAnchors())
    {
        const ShapeBinding binding = armature.binding();
        if (binding.size() == shapeAnchors(first).size())
        {
            std::unique_ptr<Shape> bent(cloneShape(first, nullptr));
            bent->transform = shape.transform;
            bindShape(*bent, toArmature, binding, armature.shownAt(0), sprung);
            setShapeContents(shape, *bent);
            return true;
        }
    }
    reskinShape(shape, toArmature, armature.shownAt(frame), sprung);
    return true;
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
            const Point carried = carryOnBone(p, before, after, bound.bone);
            sum.x += bound.weight * carried.x;
            sum.y += bound.weight * carried.y;
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

// ---------------------------------------------------------------------------
// Locks, targets and bone options.

namespace {

bool hangsBelow(const std::vector<IKJoint>& joints, size_t joint, int above)
{
    for (int at = static_cast<int>(joint); at >= 0; at = joints[static_cast<size_t>(at)].parent)
    {
        if (at == above)
            return true;
    }
    return false;
}

/// Turns \a pivot, and every joint below it, by \a angle about the pivot.
void turnBelow(std::vector<IKJoint>& joints, int pivot, double angle)
{
    if (pivot < 0 || static_cast<size_t>(pivot) >= joints.size() || std::fabs(angle) < 1.0e-15)
        return;
    const Point centre = joints[static_cast<size_t>(pivot)].position;
    for (size_t i = 0; i < joints.size(); ++i)
    {
        if (!hangsBelow(joints, i, pivot))
            continue;
        joints[i].angle += angle;
        const Point offset = rotated(Point(joints[i].position.x - centre.x, joints[i].position.y - centre.y), angle);
        joints[i].position = Point(centre.x + offset.x, centre.y + offset.y);
    }
}

/// Moves \a joint, and every joint below it, by \a by.
void moveBelow(std::vector<IKJoint>& joints, int joint, const Point& by)
{
    for (size_t i = 0; i < joints.size(); ++i)
    {
        if (hangsBelow(joints, i, joint))
            joints[i].position = Point(joints[i].position.x + by.x, joints[i].position.y + by.y);
    }
}

double boneLength(const std::vector<IKJoint>& joints, int joint)
{
    const int head = joints[static_cast<size_t>(joint)].parent;
    if (head < 0)
        return 0.0;
    const Point& a = joints[static_cast<size_t>(head)].position;
    const Point& b = joints[static_cast<size_t>(joint)].position;
    return std::hypot(b.x - a.x, b.y - a.y);
}

/// Makes the bone ending at \a joint \a length long, the joints below it
/// moving with its tail.
void setBoneLength(std::vector<IKJoint>& joints, int joint, double length)
{
    const int head = joints[static_cast<size_t>(joint)].parent;
    const double now = boneLength(joints, joint);
    if (head < 0 || now < 1.0e-9)
        return;
    const Point& a = joints[static_cast<size_t>(head)].position;
    const Point& b = joints[static_cast<size_t>(joint)].position;
    const double grow = length / now - 1.0;
    moveBelow(joints, joint, Point((b.x - a.x) * grow, (b.y - a.y) * grow));
}

/// One pass of cyclic coordinate descent: each of \a pivots in turn swings
/// \a end toward \a goal, no further than its limits allow.
void reachPass(std::vector<IKJoint>& joints, const std::vector<int>& pivots, int end, const Point& goal,
    const std::vector<JointLimit>& limits)
{
    for (int pivot : pivots)
    {
        const Point centre = joints[static_cast<size_t>(pivot)].position;
        const Point tip = joints[static_cast<size_t>(end)].position;
        if (std::hypot(tip.x - centre.x, tip.y - centre.y) < 1.0e-9 ||
            std::hypot(goal.x - centre.x, goal.y - centre.y) < 1.0e-9)
        {
            continue;
        }
        double turn = wrapAngle(std::atan2(goal.y - centre.y, goal.x - centre.x) - std::atan2(tip.y - centre.y, tip.x - centre.x));
        // A chain lying straight toward a goal nearer than its end cannot
        // fold by turning toward it: the joint nearest the end bends it a
        // little first.
        if (pivot == pivots.front() && pivots.size() > 1 && std::fabs(turn) < 1.0e-6 &&
            std::hypot(goal.x - centre.x, goal.y - centre.y) < std::hypot(tip.x - centre.x, tip.y - centre.y) - 1.0e-3)
        {
            turn = 0.2;
        }
        if (static_cast<size_t>(pivot) < limits.size() && limits[static_cast<size_t>(pivot)].constrained)
        {
            const int above = joints[static_cast<size_t>(pivot)].parent;
            const double relative = joints[static_cast<size_t>(pivot)].angle - (above >= 0 ? joints[static_cast<size_t>(above)].angle : 0.0);
            const double low = std::min(limits[static_cast<size_t>(pivot)].min, limits[static_cast<size_t>(pivot)].max);
            const double high = std::max(limits[static_cast<size_t>(pivot)].min, limits[static_cast<size_t>(pivot)].max);
            turn = std::max(low, std::min(high, relative + turn)) - relative;
        }
        turnBelow(joints, pivot, turn);
    }
}

/// Up to \a passes of reachPass, until \a end is there.
void reachStep(std::vector<IKJoint>& joints, const std::vector<int>& pivots, int end, const Point& goal,
    const std::vector<JointLimit>& limits, int passes = 1)
{
    for (int pass = 0; pass < passes; ++pass)
    {
        const Point& tip = joints[static_cast<size_t>(end)].position;
        if (pass > 0 && std::hypot(tip.x - goal.x, tip.y - goal.y) < 1.0e-3)
            break;
        reachPass(joints, pivots, end, goal, limits);
    }
}

/// Grows or shrinks the bones with IK stretch from \a top (a joint above
/// \a end) down to \a end so the chain, laid straight, would just reach
/// \a goal: no shorter than their own lengths, no longer than they may grow.
void stretchToward(std::vector<IKJoint>& joints, int top, int end, const Point& goal, const PoseGoals& goals)
{
    double rigid = 0.0;
    double own = 0.0;
    double most = 0.0;
    std::vector<int> stretchy;
    for (int at = end; at >= 0 && at != top; at = joints[static_cast<size_t>(at)].parent)
    {
        const size_t i = static_cast<size_t>(at);
        const double length = i < goals.lengths.size() ? goals.lengths[i] : 0.0;
        const double stretch = i < goals.options.size() ? goals.options[i].maxStretch : 1.0;
        if (length > 0.0 && stretch > 1.0)
        {
            stretchy.push_back(at);
            own += length;
            most += length * stretch;
        }
        else
            rigid += boneLength(joints, at);
    }
    if (stretchy.empty() || top < 0)
        return;
    const Point& base = joints[static_cast<size_t>(top)].position;
    const double reach = std::hypot(goal.x - base.x, goal.y - base.y);
    const double factor = std::clamp(reach - rigid, own, most) / own;
    for (int bone : stretchy)
    {
        const size_t i = static_cast<size_t>(bone);
        setBoneLength(joints, bone, goals.lengths[i] * std::min(factor, goals.options[i].maxStretch));
    }
}

/// The joints that turn to bring \a end where it is wanted: from the joint
/// above it up to the nearest one where the armature branches, short of
/// any \a blocked; those \a held keep their angles and are passed over.
std::vector<int> chainAbove(const std::vector<IKJoint>& joints, int end, const std::vector<bool>& blocked,
    const std::vector<bool>& held, const std::vector<int>& children)
{
    std::vector<int> pivots;
    const int first = joints[static_cast<size_t>(end)].parent;
    for (int at = first; at >= 0; at = joints[static_cast<size_t>(at)].parent)
    {
        const size_t i = static_cast<size_t>(at);
        if (blocked[i] || (at != first && children[i] > 1))
            break;
        if (!held[i])
            pivots.push_back(at);
    }
    return pivots;
}

std::string pointText(const Point& p)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.4f,%.4f", p.x, p.y);
    return buffer;
}

} // namespace

bool PoseGoals::any() const
{
    if (!pins.empty() || !reaches.empty() || !level.empty())
        return true;
    for (const BoneOptions& each : options)
    {
        if (each.maxStretch > 1.0)
            return true;
    }
    return false;
}

std::vector<IKJoint> solvePose(std::vector<IKJoint> joints, const PoseGoals& goals, int effector, const Point& target,
    const std::vector<JointLimit>& limits, int iterations)
{
    const bool dragging = effector >= 0 && static_cast<size_t>(effector) < joints.size();
    if (!goals.any())
        return dragging ? solveIK(joints, effector, target, iterations, limits) : joints;
    const size_t count = joints.size();
    std::vector<int> children(count, 0);
    for (const IKJoint& joint : joints)
    {
        if (joint.parent >= 0 && static_cast<size_t>(joint.parent) < count)
            ++children[static_cast<size_t>(joint.parent)];
    }
    // The joints the drag turns, which nothing else may.
    std::vector<int> dragged;
    std::vector<bool> blocked(count, false);
    for (int at = dragging ? joints[static_cast<size_t>(effector)].parent : -1; at >= 0; at = joints[static_cast<size_t>(at)].parent)
    {
        // No further up than a locked bone's head: turning that, or any
        // joint above it, would move the bone.
        const bool holds = std::any_of(goals.pins.begin(), goals.pins.end(), [&](const PoseGoals::Pin& pin) {
            const int head = pin.joint >= 0 && static_cast<size_t>(pin.joint) < count && pin.joint != effector
                ? joints[static_cast<size_t>(pin.joint)].parent : -1;
            return head >= 0 && hangsBelow(joints, static_cast<size_t>(head), at);
        });
        if (holds)
            break;
        dragged.push_back(at);
        blocked[static_cast<size_t>(at)] = true;
    }
    std::vector<bool> held(count, false);
    for (const auto& [joint, angle] : goals.level)
    {
        if (joint >= 0 && static_cast<size_t>(joint) < count)
            held[static_cast<size_t>(joint)] = true;
    }
    const auto keepLevel = [&]() {
        for (const auto& [joint, angle] : goals.level)
        {
            if (joint >= 0 && static_cast<size_t>(joint) < count && !blocked[static_cast<size_t>(joint)])
                turnBelow(joints, joint, wrapAngle(angle - joints[static_cast<size_t>(joint)].angle));
        }
    };
    const auto valid = [count](int joint) { return joint >= 0 && static_cast<size_t>(joint) < count; };

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        if (!dragged.empty())
        {
            stretchToward(joints, dragged.back(), effector, target, goals);
            reachStep(joints, dragged, effector, target, limits);
        }
        keepLevel();
        for (const PoseGoals::Reach& reach : goals.reaches)
        {
            if (!valid(reach.joint) || reach.joint == effector)
                continue;
            const Point goal = valid(reach.toJoint) ? joints[static_cast<size_t>(reach.toJoint)].position : reach.at;
            const std::vector<int> pivots = chainAbove(joints, reach.joint, blocked, held, children);
            if (pivots.empty())
                continue;
            stretchToward(joints, pivots.back(), reach.joint, goal, goals);
            reachStep(joints, pivots, reach.joint, goal, limits, 8);
        }
        keepLevel();
        // Locked bones last, so nothing moves them: each head back in its
        // place by the joints above it, or by moving the whole armature when
        // it is the root, and turned to its angle.
        for (const PoseGoals::Pin& pin : goals.pins)
        {
            if (!valid(pin.joint) || pin.joint == effector)
                continue;
            const int head = joints[static_cast<size_t>(pin.joint)].parent;
            if (!valid(head))
                continue;
            IKJoint& at = joints[static_cast<size_t>(head)];
            if (at.parent < 0)
                moveBelow(joints, head, Point(pin.head.x - at.position.x, pin.head.y - at.position.y));
            else
            {
                const std::vector<int> pivots = chainAbove(joints, head, blocked, held, children);
                if (!pivots.empty())
                {
                    stretchToward(joints, pivots.back(), head, pin.head, goals);
                    reachStep(joints, pivots, head, pin.head, limits, 8);
                }
            }
            if (!blocked[static_cast<size_t>(head)])
                turnBelow(joints, head, wrapAngle(pin.angle - joints[static_cast<size_t>(head)].angle));
        }
    }
    return joints;
}

std::vector<IKJoint> stretchBone(std::vector<IKJoint> joints, int joint, const Point& to, const PoseGoals& goals,
    const std::vector<JointLimit>& limits)
{
    if (joint < 0 || static_cast<size_t>(joint) >= joints.size())
        return joints;
    const int head = joints[static_cast<size_t>(joint)].parent;
    if (head < 0)
        return joints;
    // Turned about its head, as one pass of the solver turns a joint.
    reachStep(joints, {head}, joint, to, limits);
    const size_t i = static_cast<size_t>(joint);
    const double own = i < goals.lengths.size() ? goals.lengths[i] : 0.0;
    if (own > 0.0 && i < goals.options.size() && goals.options[i].squash)
    {
        const Point& from = joints[static_cast<size_t>(head)].position;
        setBoneLength(joints, joint, std::clamp(std::hypot(to.x - from.x, to.y - from.y), 0.05 * own, 20.0 * own));
    }
    return solvePose(joints, goals, -1, Point(), limits);
}

Transform boneMove(const std::vector<IKJoint>& before, const std::vector<IKJoint>& after, int joint)
{
    if (joint < 0 || static_cast<size_t>(joint) >= before.size() || static_cast<size_t>(joint) >= after.size())
        return Transform();
    // The bone below it that stretches, if one does.
    int child = -1;
    for (size_t i = 0; i < before.size() && i < after.size() && child < 0; ++i)
    {
        if (before[i].parent == joint && (before[i].stretch != 1.0 || after[i].stretch != 1.0 ||
                                             before[i].thickness != 1.0 || after[i].thickness != 1.0))
        {
            child = static_cast<int>(i);
        }
    }
    // The joint's frame, scaled along and across that bone.
    const auto frameOf = [&](const std::vector<IKJoint>& joints) {
        const IKJoint& at = joints[static_cast<size_t>(joint)];
        Transform placed;
        if (child >= 0)
        {
            const IKJoint& end = joints[static_cast<size_t>(child)];
            const Point along = rotated(Point(end.position.x - at.position.x, end.position.y - at.position.y), -at.angle);
            const double beta = std::atan2(along.y, along.x);
            placed = then(then(turn(-beta), Transform(end.stretch, 0.0, 0.0, end.thickness, 0.0, 0.0)), turn(beta));
        }
        placed = then(placed, turn(at.angle));
        placed.tx += at.position.x;
        placed.ty += at.position.y;
        return placed;
    };
    Transform back;
    if (!inverse(frameOf(before), back))
        return Transform();
    return then(back, frameOf(after));
}

Point carryOnBone(const Point& point, const std::vector<IKJoint>& before, const std::vector<IKJoint>& after, int bone)
{
    if (bone < 0 || static_cast<size_t>(bone) >= before.size() || static_cast<size_t>(bone) >= after.size())
        return point;
    const int head = before[static_cast<size_t>(bone)].parent;
    if (head < 0 || static_cast<size_t>(head) >= after.size())
        return point;
    const IKJoint& from = before[static_cast<size_t>(head)];
    const IKJoint& to = after[static_cast<size_t>(head)];
    Point local(point.x - from.position.x, point.y - from.position.y);
    // Scaled along the bone and across it as it stretches.
    const IKJoint& was = before[static_cast<size_t>(bone)];
    const IKJoint& now = after[static_cast<size_t>(bone)];
    const double along = was.stretch > 1.0e-9 ? now.stretch / was.stretch : 1.0;
    const double across = was.thickness > 1.0e-9 ? now.thickness / was.thickness : 1.0;
    const double length = std::hypot(was.position.x - from.position.x, was.position.y - from.position.y);
    if ((along != 1.0 || across != 1.0) && length > 1.0e-9)
    {
        const Point d((was.position.x - from.position.x) / length, (was.position.y - from.position.y) / length);
        const double a = (local.x * d.x + local.y * d.y) * along;
        const double b = (local.y * d.x - local.x * d.y) * across;
        local = Point(d.x * a - d.y * b, d.y * a + d.x * b);
    }
    local = rotated(local, to.angle - from.angle);
    return Point(to.position.x + local.x, to.position.y + local.y);
}

int IKArmature::jointNamed(const std::string& name) const
{
    for (size_t i = 0; i < _nodes.size(); ++i)
    {
        const char* own = _nodes[i].element->Attribute("name");
        if (own && name == own)
            return static_cast<int>(i);
    }
    return -1;
}

std::string IKArmature::nodeName(int joint) const
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return std::string();
    const char* name = _nodes[static_cast<size_t>(joint)].element->Attribute("name");
    return name ? name : std::string();
}

size_t IKArmature::poseIndexAt(int frame) const
{
    const std::vector<int> starts = poses();
    size_t k = 0;
    while (k + 1 < starts.size() && starts[k + 1] <= frame)
        ++k;
    return k;
}

tinyxml2::XMLElement* IKArmature::stateOf(int joint, size_t pose) const
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return nullptr;
    const std::vector<tinyxml2::XMLElement*> list = states(_nodes[static_cast<size_t>(joint)].element, "State");
    return pose < list.size() ? list[pose] : nullptr;
}

bool IKArmature::ensurePose(int frame)
{
    if (frame < 0 || frame >= frames())
        return false;
    const std::vector<int> starts = poses();
    if (std::find(starts.begin(), starts.end(), frame) != starts.end())
        return true;
    return setPlacedPose(frame, shownAt(frame));
}

void IKArmature::writePlaces(size_t pose, const std::vector<IKJoint>& joints, bool withAngles)
{
    for (size_t i = 0; i < _nodes.size() && i < joints.size(); ++i)
    {
        tinyxml2::XMLElement* state = stateOf(static_cast<int>(i), pose);
        if (!state)
            continue;
        const int parent = _nodes[i].parent;
        // The angle from the parent's, the nearest turn of it to the one
        // kept, and the place in the parent's frame, in twips.
        double angle = joints[i].angle;
        Point place(joints[i].position.x * 20.0, joints[i].position.y * 20.0);
        if (parent >= 0 && static_cast<size_t>(parent) < joints.size())
        {
            const IKJoint& above = joints[static_cast<size_t>(parent)];
            angle -= above.angle;
            const Point offset = rotated(Point(joints[i].position.x - above.position.x, joints[i].position.y - above.position.y), -above.angle);
            place = Point(offset.x * 20.0, offset.y * 20.0);
        }
        const double kept = state->DoubleAttribute("angle", 0.0);
        angle += 2.0 * kPi * std::round((kept - angle) / (2.0 * kPi));
        if (withAngles && std::fabs(angle - kept) > 1.0e-9)
            setNumber(state, "angle", angle);
        // Only a root moves, and only a bone that can stretch changes
        // length: the others keep their places as posed, which joints shown
        // between frames would only blur.
        if (parent >= 0 && !_nodes[i].element->Attribute("phoenixLength"))
            continue;
        if (std::fabs(place.x - state->DoubleAttribute("x", 0.0)) > 0.01 || std::fabs(place.y - state->DoubleAttribute("y", 0.0)) > 0.01)
        {
            setNumber(state, "x", place.x);
            setNumber(state, "y", place.y);
        }
    }
}

bool IKArmature::setPlacedPose(int frame, const std::vector<IKJoint>& joints, const std::vector<double>* angles)
{
    if (joints.size() != _nodes.size() || (angles && angles->size() != joints.size()))
        return false;
    std::vector<double> posed;
    for (const IKJoint& joint : joints)
        posed.push_back(joint.angle);
    if (!setPose(frame, angles ? *angles : posed))
        return false;
    writePlaces(poseIndexAt(frame), joints, !angles);
    storeJoints();
    return true;
}

void IKArmature::measure(std::vector<IKJoint>& joints) const
{
    for (size_t i = 0; i < joints.size() && i < _nodes.size(); ++i)
    {
        const double own = _nodes[i].element->DoubleAttribute("phoenixLength", 0.0);
        const int parent = joints[i].parent;
        if (own <= 0.0 || parent < 0 || static_cast<size_t>(parent) >= joints.size())
            continue;
        const double stretch = std::max(1.0e-3, boneLength(joints, static_cast<int>(i)) / own);
        joints[i].stretch = stretch;
        const char* squash = _nodes[i].element->Attribute("phoenixSquash");
        joints[i].thickness = squash && std::strcmp(squash, "volume") == 0 ? 1.0 / stretch : 1.0;
    }
}

bool IKArmature::locked(int joint, int frame) const
{
    const tinyxml2::XMLElement* state = stateOf(joint, poseIndexAt(frame));
    return state && state->BoolAttribute("phoenixLocked", false);
}

bool IKArmature::setLocked(int joint, int frame, bool on)
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size() || _nodes[static_cast<size_t>(joint)].parent < 0 ||
        frame < 0 || frame >= frames())
    {
        return false;
    }
    // Nothing to do when this pose and every later one are so already.
    bool same = true;
    for (size_t k = poseIndexAt(frame); k < poses().size() && same; ++k)
    {
        const tinyxml2::XMLElement* state = stateOf(joint, k);
        same = state && state->BoolAttribute("phoenixLocked", false) == on;
    }
    if (same || !ensurePose(frame))
        return false;
    const std::vector<int> starts = poses();
    const size_t first = poseIndexAt(frame);
    // Where the bone is on the pose, which the later ones keep it.
    const std::vector<IKJoint> here = jointsAt(frame);
    const int head = _nodes[static_cast<size_t>(joint)].parent;
    PoseGoals goals = goalsAt(frame, here);
    goals.pins.clear();
    goals.reaches.clear();
    goals.level.clear();
    goals.pins.push_back({joint, here[static_cast<size_t>(head)].position, here[static_cast<size_t>(head)].angle});
    const std::vector<JointLimit> limited = limits();
    for (size_t k = first; k < starts.size(); ++k)
    {
        tinyxml2::XMLElement* state = stateOf(joint, k);
        if (!state || state->BoolAttribute("phoenixLocked", false) == on)
            continue;
        if (!on)
        {
            state->DeleteAttribute("phoenixLocked");
            continue;
        }
        state->SetAttribute("phoenixLocked", "true");
        if (k > first)
            writePlaces(k, solvePose(posedAt(starts[k]), goals, -1, Point(), limited));
    }
    storeJoints();
    return true;
}

BoneTarget IKArmature::target(int joint, int frame) const
{
    BoneTarget found;
    const size_t k = poseIndexAt(frame);
    const tinyxml2::XMLElement* state = stateOf(joint, k);
    if (!state)
        return found;
    if (const char* name = state->Attribute("phoenixTarget"))
    {
        found.joint = jointNamed(name);
        return found;
    }
    const auto pointOf = [](const tinyxml2::XMLElement* each, Point& at) {
        const std::vector<double> values = numbers(each ? each->Attribute("phoenixTargetPoint") : nullptr);
        if (values.size() < 2)
            return false;
        at = Point(values[0], values[1]);
        return true;
    };
    if (!pointOf(state, found.at))
        return found;
    found.point = true;
    // Eased toward the next pose's point.
    const std::vector<int> starts = poses();
    Point next;
    if (k + 1 < starts.size() && pointOf(stateOf(joint, k + 1), next))
    {
        const double t = easedTime(static_cast<int>(k),
            static_cast<double>(frame - starts[k]) / std::max(1, starts[k + 1] - starts[k]));
        found.at = Point(found.at.x + (next.x - found.at.x) * t, found.at.y + (next.y - found.at.y) * t);
    }
    return found;
}

bool IKArmature::setTarget(int joint, int frame, const BoneTarget& target, bool onward)
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size() || _nodes[static_cast<size_t>(joint)].parent < 0 ||
        frame < 0 || frame >= frames())
    {
        return false;
    }
    // Not a joint below it, which would move as it reaches.
    const std::vector<IKJoint> joints = posedAt(0);
    if (target.joint >= 0 && (static_cast<size_t>(target.joint) >= joints.size() ||
                                 hangsBelow(joints, static_cast<size_t>(target.joint), joint)))
    {
        return false;
    }
    const std::string name = target.joint >= 0 ? nodeName(target.joint) : std::string();
    const std::string point = target.joint < 0 && target.point ? pointText(target.at) : std::string();
    const auto write = [&](tinyxml2::XMLElement* state) {
        const char* oldName = state->Attribute("phoenixTarget");
        const char* oldPoint = state->Attribute("phoenixTargetPoint");
        if ((oldName ? oldName : "") == name && (oldPoint ? oldPoint : "") == point)
            return false;
        state->DeleteAttribute("phoenixTarget");
        state->DeleteAttribute("phoenixTargetPoint");
        if (!name.empty())
            state->SetAttribute("phoenixTarget", name.c_str());
        if (!point.empty())
            state->SetAttribute("phoenixTargetPoint", point.c_str());
        return true;
    };
    // Nothing to do when the poses have it already.
    bool same = true;
    for (size_t k = poseIndexAt(frame); k < poses().size() && same; ++k)
    {
        const tinyxml2::XMLElement* state = stateOf(joint, k);
        const char* oldName = state ? state->Attribute("phoenixTarget") : nullptr;
        const char* oldPoint = state ? state->Attribute("phoenixTargetPoint") : nullptr;
        same = state && (oldName ? oldName : "") == name && (oldPoint ? oldPoint : "") == point;
        if (!onward)
            break;
    }
    const std::vector<int> before = poses();
    if ((same && std::find(before.begin(), before.end(), frame) != before.end()) || !ensurePose(frame))
        return false;
    const size_t first = poseIndexAt(frame);
    const size_t last = onward ? poses().size() : first + 1;
    for (size_t k = first; k < last; ++k)
    {
        if (tinyxml2::XMLElement* state = stateOf(joint, k))
            write(state);
    }
    storeJoints();
    return true;
}

BoneOptions IKArmature::options(int joint) const
{
    BoneOptions found;
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size())
        return found;
    const tinyxml2::XMLElement* element = _nodes[static_cast<size_t>(joint)].element;
    found.independent = element->BoolAttribute("phoenixIndependent", false);
    const char* squash = element->Attribute("phoenixSquash");
    found.squash = squash != nullptr;
    found.keepVolume = squash && std::strcmp(squash, "volume") == 0;
    found.maxStretch = std::max(1.0, element->DoubleAttribute("phoenixStretch", 1.0));
    return found;
}

void IKArmature::setOptions(int joint, const BoneOptions& options)
{
    if (joint < 0 || static_cast<size_t>(joint) >= _nodes.size() || _nodes[static_cast<size_t>(joint)].parent < 0)
        return;
    tinyxml2::XMLElement* element = _nodes[static_cast<size_t>(joint)].element;
    for (const char* name : {"phoenixIndependent", "phoenixSquash", "phoenixStretch"})
        element->DeleteAttribute(name);
    if (options.independent)
        element->SetAttribute("phoenixIndependent", "true");
    if (options.squash)
        element->SetAttribute("phoenixSquash", options.keepVolume ? "volume" : "stretch");
    if (options.maxStretch > 1.0)
        setNumber(element, "phoenixStretch", std::min(options.maxStretch, 10.0));
    // The bone's own length, which its stretching is measured from.
    const bool sizes = options.squash || options.maxStretch > 1.0;
    if (sizes && !element->Attribute("phoenixLength"))
        setNumber(element, "phoenixLength", boneLength(posedAt(0), joint));
    else if (!sizes && element->Attribute("phoenixLength"))
    {
        // Back to that length in every pose.
        const double own = element->DoubleAttribute("phoenixLength", 0.0);
        for (tinyxml2::XMLElement* state : states(element, "State"))
        {
            const double x = state->DoubleAttribute("x", 0.0);
            const double y = state->DoubleAttribute("y", 0.0);
            const double length = std::hypot(x, y);
            if (own > 0.0 && length > 1.0e-9)
            {
                setNumber(state, "x", x * own * 20.0 / length);
                setNumber(state, "y", y * own * 20.0 / length);
            }
        }
        element->DeleteAttribute("phoenixLength");
    }
    storeJoints();
}

bool IKArmature::hasGoals() const
{
    for (const Node& node : _nodes)
    {
        if (node.element->BoolAttribute("phoenixIndependent", false) || node.element->DoubleAttribute("phoenixStretch", 1.0) > 1.0)
            return true;
        for (const tinyxml2::XMLElement* state : states(node.element, "State"))
        {
            if (state->Attribute("phoenixLocked") || state->Attribute("phoenixTarget") || state->Attribute("phoenixTargetPoint"))
                return true;
        }
    }
    return false;
}

PoseGoals IKArmature::goalsAt(int frame, const std::vector<IKJoint>& shown, int dragged) const
{
    PoseGoals goals;
    const size_t count = _nodes.size();
    goals.options.resize(count);
    goals.lengths.resize(count, 0.0);
    for (size_t i = 0; i < count; ++i)
    {
        goals.options[i] = options(static_cast<int>(i));
        goals.lengths[i] = _nodes[i].element->DoubleAttribute("phoenixLength", 0.0);
    }
    if (shown.size() != count)
        return goals;
    // The joints the drag turns keep no angle of their own.
    std::vector<bool> turning(count, false);
    for (int at = dragged >= 0 && static_cast<size_t>(dragged) < count ? _nodes[static_cast<size_t>(dragged)].parent : -1; at >= 0;
         at = _nodes[static_cast<size_t>(at)].parent)
    {
        turning[static_cast<size_t>(at)] = true;
    }
    for (size_t i = 0; i < count; ++i)
    {
        const int head = _nodes[i].parent;
        if (head < 0 || static_cast<int>(i) == dragged)
            continue;
        if (goals.options[i].independent && !turning[static_cast<size_t>(head)])
            goals.level.emplace_back(head, shown[static_cast<size_t>(head)].angle);
        if (locked(static_cast<int>(i), frame))
            goals.pins.push_back({static_cast<int>(i), shown[static_cast<size_t>(head)].position, shown[static_cast<size_t>(head)].angle});
        const BoneTarget aim = target(static_cast<int>(i), frame);
        if (aim.joint >= 0)
            goals.reaches.push_back({static_cast<int>(i), Point(), aim.joint});
        else if (aim.point)
            goals.reaches.push_back({static_cast<int>(i), aim.at, -1});
    }
    return goals;
}

std::vector<std::vector<IKJoint>> IKArmature::withGoals(const std::vector<std::vector<IKJoint>>& posed) const
{
    std::vector<std::vector<IKJoint>> kept = posed;
    const std::vector<int> starts = poses();
    if (starts.empty() || posed.empty())
        return kept;
    const std::vector<JointLimit> limited = limits();
    const size_t count = _nodes.size();
    std::vector<PoseGoals::Pin> spanPins;
    for (size_t f = 0; f < posed.size(); ++f)
    {
        const int frame = static_cast<int>(f);
        const size_t k = poseIndexAt(frame);
        const bool spanStart = k < starts.size() && starts[k] == frame;
        // The targets, as for posing; then the independent angles eased
        // from pose to pose on the stage, rather than from their parents.
        PoseGoals goals = goalsAt(frame, posed[f]);
        goals.pins.clear();
        goals.level.clear();
        for (size_t i = 0; i < count; ++i)
        {
            const int head = _nodes[i].parent;
            if (head < 0 || !goals.options[i].independent)
                continue;
            const size_t h = static_cast<size_t>(head);
            const size_t from = static_cast<size_t>(std::clamp(starts[k], 0, static_cast<int>(posed.size()) - 1));
            double angle = posed[from][h].angle;
            if (k + 1 < starts.size() && static_cast<size_t>(starts[k + 1]) < posed.size())
            {
                double next = posed[static_cast<size_t>(starts[k + 1])][h].angle;
                next += 2.0 * kPi * std::round((angle - next) / (2.0 * kPi));
                const double t = easedTime(static_cast<int>(k),
                    static_cast<double>(frame - starts[k]) / std::max(1, starts[k + 1] - starts[k]));
                angle += (next - angle) * t;
            }
            goals.level.emplace_back(head, angle);
        }
        // Bones locked on the pose and the next stay where the pose has
        // them.
        if (!spanStart)
            goals.pins = spanPins;
        kept[f] = solvePose(posed[f], goals, -1, Point(), limited);
        if (spanStart)
        {
            spanPins.clear();
            for (size_t i = 0; i < count; ++i)
            {
                const int head = _nodes[i].parent;
                const tinyxml2::XMLElement* here = stateOf(static_cast<int>(i), k);
                const tinyxml2::XMLElement* next = k + 1 < starts.size() ? stateOf(static_cast<int>(i), k + 1) : nullptr;
                if (head < 0 || !here || !here->BoolAttribute("phoenixLocked", false) ||
                    (k + 1 < starts.size() && !(next && next->BoolAttribute("phoenixLocked", false))))
                {
                    continue;
                }
                const IKJoint& at = kept[f][static_cast<size_t>(head)];
                spanPins.push_back({static_cast<int>(i), at.position, at.angle});
            }
        }
    }
    return kept;
}

bool IKArmature::writeBackPoses(const std::vector<std::vector<IKJoint>>& joints)
{
    bool changed = false;
    const std::vector<int> starts = poses();
    for (size_t k = 0; k < starts.size(); ++k)
    {
        if (starts[k] < 0 || static_cast<size_t>(starts[k]) >= joints.size())
            continue;
        const std::vector<IKJoint> posed = posedAt(starts[k]);
        const std::vector<IKJoint>& kept = joints[static_cast<size_t>(starts[k])];
        bool differs = false;
        for (size_t i = 0; i < posed.size() && i < kept.size() && !differs; ++i)
        {
            differs = std::fabs(wrapAngle(posed[i].angle - kept[i].angle)) > 1.0e-7 ||
                std::hypot(posed[i].position.x - kept[i].position.x, posed[i].position.y - kept[i].position.y) > 1.0e-4;
        }
        if (differs)
        {
            writePlaces(k, kept);
            changed = true;
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Gravity and wind

BoneForces IKArmature::forces() const
{
    BoneForces found;
    const tinyxml2::XMLElement* tree = _document ? _document->FirstChildElement("IKTree") : nullptr;
    if (!tree)
        return found;
    found.gravity = std::clamp(tree->DoubleAttribute("phoenixGravity", 0.0), 0.0, 100.0);
    found.gravityAngle = tree->DoubleAttribute("phoenixGravityAngle", found.gravityAngle);
    found.wind = std::clamp(tree->DoubleAttribute("phoenixWind", 0.0), 0.0, 100.0);
    found.windAngle = tree->DoubleAttribute("phoenixWindAngle", found.windAngle);
    found.gusts = std::clamp(tree->DoubleAttribute("phoenixWindGusts", 0.0), 0.0, 100.0);
    found.gustRate = std::clamp(tree->DoubleAttribute("phoenixWindRate", found.gustRate), 0.0, 10.0);
    found.seed = tree->IntAttribute("phoenixWindSeed", found.seed);
    return found;
}

void IKArmature::setForces(const BoneForces& forces)
{
    tinyxml2::XMLElement* tree = _document ? _document->FirstChildElement("IKTree") : nullptr;
    if (!tree)
        return;
    for (const char* name : {"phoenixGravity", "phoenixGravityAngle", "phoenixWind", "phoenixWindAngle",
             "phoenixWindGusts", "phoenixWindRate", "phoenixWindSeed"})
    {
        tree->DeleteAttribute(name);
    }
    // Only what is in use: an armature without them is as it was.
    if (forces.gravity > 0.0)
    {
        setNumber(tree, "phoenixGravity", std::clamp(forces.gravity, 0.0, 100.0));
        setNumber(tree, "phoenixGravityAngle", forces.gravityAngle);
    }
    if (forces.wind > 0.0)
    {
        setNumber(tree, "phoenixWind", std::clamp(forces.wind, 0.0, 100.0));
        setNumber(tree, "phoenixWindAngle", forces.windAngle);
        if (forces.gusts > 0.0)
        {
            setNumber(tree, "phoenixWindGusts", std::clamp(forces.gusts, 0.0, 100.0));
            setNumber(tree, "phoenixWindRate", std::clamp(forces.gustRate, 0.0, 10.0));
            tree->SetAttribute("phoenixWindSeed", forces.seed);
        }
    }
    storeJoints();
}

std::vector<double> IKArmature::poseFromShown(int frame, const std::vector<double>& angles) const
{
    if (!forces().any() || !hasSprings())
        return angles;
    const std::vector<IKJoint> shown = shownAt(frame);
    const std::vector<IKJoint> posed = posedAt(frame);
    if (angles.size() != shown.size() || posed.size() != shown.size())
        return angles;
    // Parents come before their children.
    std::vector<double> pose(angles.size(), 0.0);
    for (size_t i = 0; i < angles.size(); ++i)
    {
        const int parent = shown[i].parent;
        const bool above = parent >= 0 && static_cast<size_t>(parent) < i;
        const size_t p = above ? static_cast<size_t>(parent) : 0;
        const double turned = angles[i] - (above ? angles[p] : 0.0);
        const double showing = shown[i].angle - (above ? shown[p].angle : 0.0);
        const double posing = posed[i].angle - (above ? posed[p].angle : 0.0);
        pose[i] = (above ? pose[p] : 0.0) + posing + wrapAngle(turned - showing);
    }
    return pose;
}

double IKArmature::smoothness() const
{
    const tinyxml2::XMLElement* tree = _document ? _document->FirstChildElement("IKTree") : nullptr;
    return tree ? std::clamp(tree->DoubleAttribute("phoenixSmoothness", 0.0), 0.0, 100.0) : 0.0;
}

void IKArmature::setSmoothness(double smoothness)
{
    tinyxml2::XMLElement* tree = _document ? _document->FirstChildElement("IKTree") : nullptr;
    if (!tree)
        return;
    tree->DeleteAttribute("phoenixSmoothness");
    if (smoothness > 0.0)
        setNumber(tree, "phoenixSmoothness", std::clamp(smoothness, 0.0, 100.0));
}

// ---------------------------------------------------------------------------
// Smooth binding

ShapeBinding smoothJoint(const ShapeBinding& binding, const std::vector<Point>& anchors,
    const std::vector<IKJoint>& joints, int joint, double smoothness)
{
    ShapeBinding smoothed = binding;
    if (joint < 0 || static_cast<size_t>(joint) >= joints.size() || anchors.size() != binding.size() || smoothness <= 0.0)
        return smoothed;
    // The bone into the joint, numbered by the joint itself, and the bones
    // out of it, by their tails.
    const int head = joints[static_cast<size_t>(joint)].parent;
    if (head < 0 || static_cast<size_t>(head) >= joints.size())
        return smoothed;
    const Point& at = joints[static_cast<size_t>(joint)].position;
    const Point& from = joints[static_cast<size_t>(head)].position;
    const double inLength = std::hypot(at.x - from.x, at.y - from.y);
    std::vector<int> out;
    for (size_t k = 0; k < joints.size(); ++k)
    {
        const Point& tail = joints[k].position;
        if (joints[k].parent == joint && std::hypot(tail.x - at.x, tail.y - at.y) > 1.0e-9)
            out.push_back(static_cast<int>(k));
    }
    if (out.empty() || inLength < 1.0e-9)
        return smoothed;
    for (size_t point = 0; point < anchors.size(); ++point)
    {
        const Point& p = anchors[point];
        const std::vector<BoneWeight>& bones = binding[point];
        if (bones.empty())
            continue;
        // Between the bone in and the nearest bone out; a point that follows
        // any other bone is left as bound.
        int next = out.front();
        double nearest = distanceToSegment(p, at, joints[static_cast<size_t>(next)].position);
        for (int k : out)
        {
            const double distance = distanceToSegment(p, at, joints[static_cast<size_t>(k)].position);
            if (distance < nearest)
            {
                nearest = distance;
                next = k;
            }
        }
        const bool ours = std::all_of(bones.begin(), bones.end(),
            [&](const BoneWeight& bound) { return bound.bone == joint || bound.bone == next; });
        if (!ours)
            continue;
        // How far the point is past the joint, across the line halfway
        // between the two bones, against the reach of the blend.
        const Point& tail = joints[static_cast<size_t>(next)].position;
        const double outLength = std::hypot(tail.x - at.x, tail.y - at.y);
        Point across((at.x - from.x) / inLength + (tail.x - at.x) / outLength,
            (at.y - from.y) / inLength + (tail.y - at.y) / outLength);
        double length = std::hypot(across.x, across.y);
        if (length < 1.0e-6)
        {
            across = Point((tail.x - at.x) / outLength, (tail.y - at.y) / outLength);
            length = 1.0;
        }
        const double reach = std::clamp(smoothness, 0.0, 100.0) / 100.0 * 0.5 * std::min(inLength, outLength);
        const double past = ((p.x - at.x) * across.x + (p.y - at.y) * across.y) / length / reach;
        if (past <= -1.0 || past >= 1.0)
        {
            // Past the blend: a point a wider one shared goes to its side's
            // bone alone.
            if (bones.size() > 1)
                smoothed[point] = {{past < 0.0 ? joint : next, 1.0}};
            continue;
        }
        const double x = (past + 1.0) / 2.0;
        const double weight = x * x * (3.0 - 2.0 * x);
        smoothed[point] = {{joint, 1.0 - weight}, {next, weight}};
    }
    return smoothed;
}

ShapeBinding smoothJoints(const ShapeBinding& binding, const std::vector<Point>& anchors,
    const std::vector<IKJoint>& joints, double smoothness)
{
    // Each joint from the binding as made, so one joint's blend does not
    // keep a point from another's.
    ShapeBinding smoothed = binding;
    for (size_t joint = 0; joint < joints.size(); ++joint)
    {
        const ShapeBinding here = smoothJoint(binding, anchors, joints, static_cast<int>(joint), smoothness);
        for (size_t point = 0; point < here.size() && point < smoothed.size(); ++point)
        {
            if (here[point].size() != binding[point].size() ||
                !std::equal(here[point].begin(), here[point].end(), binding[point].begin(),
                    [](const BoneWeight& a, const BoneWeight& b) { return a.bone == b.bone && a.weight == b.weight; }))
            {
                smoothed[point] = here[point];
            }
        }
    }
    return smoothed;
}

std::vector<BoneWeight> reweighted(const std::vector<BoneWeight>& bones, int bone, double change)
{
    double total = 0.0;
    double own = 0.0;
    for (const BoneWeight& bound : bones)
    {
        total += std::max(0.0, bound.weight);
        if (bound.bone == bone)
            own += std::max(0.0, bound.weight);
    }
    // Shares of the whole, the bone's own changed.
    const double was = total > 0.0 ? own / total : 0.0;
    const double now = std::clamp(was + change, 0.0, 1.0);
    const double others = total - own;
    std::vector<BoneWeight> result;
    for (const BoneWeight& bound : bones)
    {
        if (bound.bone == bone)
            continue;
        const double share = others > 0.0 ? std::max(0.0, bound.weight) / others : 0.0;
        if (share * (1.0 - now) > 1.0e-6)
            result.push_back({bound.bone, share * (1.0 - now)});
    }
    if (now > 1.0e-6 || result.empty())
        result.push_back({bone, result.empty() ? 1.0 : now});
    return result;
}

} // namespace fla
