#include "ik_pose.h"

#include "shape_transform.h"

#include "../data/frame.h"
#include "../data/shape.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>

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

std::string symbolArmatureXml(const std::string& name, const std::string& treeName,
    const std::vector<ArmatureJoint>& joints, int frames)
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
    };
    std::function<void(tinyxml2::XMLElement*, size_t)> node = [&](tinyxml2::XMLElement* list, size_t i) {
        tinyxml2::XMLElement* element = document.NewElement("ChildNode");
        list->InsertEndChild(element);
        describe(element, i);
        const size_t span = static_cast<size_t>(frames);
        element->SetAttribute("xArray", listOf(std::vector<double>(span, joints[i].registration.x * 20.0)).c_str());
        element->SetAttribute("yArray", listOf(std::vector<double>(span, joints[i].registration.y * 20.0)).c_str());
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

} // namespace fla
