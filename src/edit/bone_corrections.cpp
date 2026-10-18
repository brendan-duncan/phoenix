#include "bone_corrections.h"

#include "graphic_loop.h"
#include "shape_merge.h"
#include "shape_transform.h"

#include "../data/document.h"
#include "../data/edge.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace fla {

namespace corrections {

const char* const kAttribute = "phoenixCorrections";

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Fields are parted by '|', corrections by ';', a shape's points by ' '
/// and a point's numbers by ','; those and '%' are escaped as %XX.
std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '|' || c == ';' || c == ' ' || c == ',' || c == '%')
        {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
            out += buffer;
        }
        else
            out += c;
    }
    return out;
}

std::string unescaped(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            out += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, separator))
        parts.push_back(part);
    return parts;
}

std::string number(double value, int decimals)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos)
    {
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

const char* propertyName(Property property)
{
    switch (property)
    {
    case Property::Frame:
        return "frame";
    case Property::Alpha:
        return "alpha";
    default:
        return "shape";
    }
}

/// A 2 by 2 matrix, as a Transform's m11, m12, m21 and m22 move a vector.
struct Linear
{
    double m11 = 1.0;
    double m12 = 0.0;
    double m21 = 0.0;
    double m22 = 1.0;

    Point apply(const Point& v) const { return Point(m11 * v.x + m21 * v.y, m12 * v.x + m22 * v.y); }

    bool inverted(Linear& out) const
    {
        const double det = m11 * m22 - m12 * m21;
        if (std::fabs(det) < 1.0e-12)
            return false;
        out.m11 = m22 / det;
        out.m12 = -m12 / det;
        out.m21 = -m21 / det;
        out.m22 = m11 / det;
        return true;
    }

    static Linear of(const Transform& t) { return Linear{t.m11, t.m12, t.m21, t.m22}; }
};

std::pair<long long, long long> key(const Point& p)
{
    return {std::llround(p.x * 1000.0), std::llround(p.y * 1000.0)};
}

/// What each point of a boned shape's rest pose is: the anchor whose bones
/// carry it (a curve's control point goes with the end of its segment) and
/// where it is in the armature's space.
struct PointInfo
{
    int anchor = -1;
    Point place;
};

/// The anchors numbered as the armature's binding numbers them (each
/// segment's end, in the order the edges reach them), then every point.
std::vector<PointInfo> infoOf(const Shape& rest, const Transform& toArmature)
{
    std::map<std::pair<long long, long long>, int> numbers;
    const auto walk = [&](const std::function<void(const PathSegment&, int)>& each) {
        for (const Edge* edge : rest.edges)
            for (const Path* path : edge ? edge->paths : std::vector<Path*>())
                for (const PathSegment* segment : path ? path->segments : std::vector<PathSegment*>())
                {
                    if (!segment || segment->points.empty())
                        continue;
                    int anchor = -1;
                    if (segment->command != PathSegment::Command::Close)
                    {
                        const auto found = numbers.emplace(key(segment->points.back()), static_cast<int>(numbers.size()));
                        anchor = found.first->second;
                    }
                    each(*segment, anchor);
                }
    };
    std::vector<PointInfo> info;
    walk([&](const PathSegment& segment, int anchor) {
        for (const Point& p : segment.points)
            info.push_back(PointInfo{anchor, p.transformed(toArmature)});
    });
    return info;
}

/// How the bones carrying a point turn a small move of it from the rest
/// pose (\a first) to \a now: by its binding's bones, weighted, or as the
/// bones nearest it carry it.
Linear carriedBy(const PointInfo& info, const ShapeBinding* binding, const std::vector<IKJoint>& first,
    const std::vector<IKJoint>& now)
{
    if (binding)
    {
        if (info.anchor < 0 || static_cast<size_t>(info.anchor) >= binding->size())
            return Linear();
        Linear sum{0.0, 0.0, 0.0, 0.0};
        double total = 0.0;
        for (const BoneWeight& bound : (*binding)[static_cast<size_t>(info.anchor)])
        {
            if (bound.bone < 0 || static_cast<size_t>(bound.bone) >= first.size() ||
                static_cast<size_t>(bound.bone) >= now.size() || bound.weight <= 0.0)
                continue;
            const int parent = first[static_cast<size_t>(bound.bone)].parent;
            if (parent < 0 || static_cast<size_t>(parent) >= now.size())
                continue;
            const double turn = now[static_cast<size_t>(parent)].angle - first[static_cast<size_t>(parent)].angle;
            sum.m11 += bound.weight * std::cos(turn);
            sum.m12 += bound.weight * std::sin(turn);
            sum.m21 -= bound.weight * std::sin(turn);
            sum.m22 += bound.weight * std::cos(turn);
            total += bound.weight;
        }
        if (total <= 0.0)
            return Linear();
        return Linear{sum.m11 / total, sum.m12 / total, sum.m21 / total, sum.m22 / total};
    }
    // Carried by the nearest bones, which change with the place: how a
    // pixel's move either way comes out.
    const Point at = skinPoint(info.place, first, now);
    const Point x = skinPoint(Point(info.place.x + 1.0, info.place.y), first, now);
    const Point y = skinPoint(Point(info.place.x, info.place.y + 1.0), first, now);
    return Linear{x.x - at.x, x.y - at.y, y.x - at.x, y.y - at.y};
}

/// The binding a boned shape's points follow, when it numbers them as the
/// rest shape's anchors; null to carry them by their nearest bones.
const ShapeBinding* bindingFor(const IKArmature& armature, const Shape& rest, ShapeBinding& kept)
{
    if (!armature.bindsAnchors())
        return nullptr;
    kept = armature.binding();
    return kept.size() == shapeAnchors(rest).size() ? &kept : nullptr;
}

Transform toArmatureOf(const SymbolInstance& container, const Shape& shape)
{
    Transform toArmature = container.transform;
    toArmature.concatenate(shape.transform);
    return toArmature;
}

/// Moves every point of \a shape by its offset, numbered as points() lists
/// them.
void movePoints(Shape& shape, const std::vector<Point>& offsets)
{
    size_t n = 0;
    for (Edge* edge : shape.edges)
        for (Path* path : edge ? edge->paths : std::vector<Path*>())
            for (PathSegment* segment : path ? path->segments : std::vector<PathSegment*>())
            {
                if (!segment)
                    continue;
                for (Point& p : segment->points)
                {
                    if (n < offsets.size())
                    {
                        p.x += offsets[n].x;
                        p.y += offsets[n].y;
                    }
                    ++n;
                }
            }
    // The edges' kept path text and the bounds made again from the points.
    warpShape(shape, [](const Point& p) { return p; });
}

Timeline* timelineOf(const DOMElement* element)
{
    const auto* frame = element ? dynamic_cast<const Frame*>(element->parent) : nullptr;
    const auto* layer = frame ? dynamic_cast<const Layer*>(frame->parent) : nullptr;
    return layer ? dynamic_cast<Timeline*>(layer->parent) : nullptr;
}

int symbolLength(const Symbol& symbol)
{
    int length = 1;
    if (!symbol.timelines.empty() && symbol.timelines.front())
        for (const Layer* layer : symbol.timelines.front()->layers)
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                if (frame)
                    length = std::max(length, frame->index + std::max(1, frame->duration));
    return length;
}

/// The frame of \a layer showing \a index.
Frame* frameAt(const Layer& layer, int index)
{
    for (Frame* frame : layer.frames)
        if (frame && frame->index <= index && index < frame->index + std::max(1, frame->duration))
            return frame;
    return nullptr;
}

std::vector<Timeline*> allTimelines(Document& document)
{
    std::vector<Timeline*> out(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            for (Timeline* timeline : symbol ? symbol->timelines : std::vector<Timeline*>())
                out.push_back(timeline);
    out.erase(std::remove(out.begin(), out.end(), nullptr), out.end());
    return out;
}

std::string uniqueName(const Document& document, const std::string& wanted)
{
    // A symbol's name is also its file's in the library folder.
    std::string base = wanted;
    for (char& c : base)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = ' ';
    std::string name = base;
    for (int n = 2; document.symbolList && document.symbolList->symbolMap.count(name); ++n)
        name = base + " " + std::to_string(n);
    return name;
}

void addSymbol(Document& document, Symbol* symbol)
{
    if (!document.symbolList)
        document.symbolList = new SymbolList(&document);
    symbol->parent = document.symbolList;
    document.symbolList->symbols.push_back(symbol);
    document.symbolList->link(symbol);
}

void removeAttribute(SymbolInstance& instance)
{
    auto& attributes = instance.extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const auto& attribute) { return attribute.first == kAttribute; }),
        attributes.end());
}

} // namespace

bool Correction::operator==(const Correction& other) const
{
    if (armature != other.armature || joint != other.joint || property != other.property || from != other.from ||
        to != other.to || valueFrom != other.valueFrom || valueTo != other.valueTo || shifts.size() != other.shifts.size())
        return false;
    for (size_t i = 0; i < shifts.size(); ++i)
    {
        const PointShift& a = shifts[i];
        const PointShift& b = other.shifts[i];
        if (a.layer != b.layer || a.element != b.element || a.point != b.point || a.dx != b.dx || a.dy != b.dy)
            return false;
    }
    return true;
}

std::string encode(const std::vector<Correction>& list)
{
    std::string out;
    for (const Correction& each : list)
    {
        if (!out.empty())
            out += ';';
        out += escaped(each.armature) + '|' + escaped(each.joint) + '|' + propertyName(each.property) + '|' +
            number(each.from, 4) + '|' + number(each.to, 4) + '|' + number(each.valueFrom, 4) + '|' +
            number(each.valueTo, 4) + '|';
        bool first = true;
        for (const PointShift& shift : each.shifts)
        {
            if (!first)
                out += ' ';
            first = false;
            out += std::to_string(shift.layer) + ',' + std::to_string(shift.element) + ',' +
                std::to_string(shift.point) + ',' + number(shift.dx, 3) + ',' + number(shift.dy, 3);
        }
    }
    return out;
}

std::vector<Correction> decode(const std::string& text)
{
    std::vector<Correction> list;
    for (const std::string& item : split(text, ';'))
    {
        const std::vector<std::string> fields = split(item, '|');
        if (fields.size() < 7)
            continue;
        Correction each;
        each.armature = unescaped(fields[0]);
        each.joint = unescaped(fields[1]);
        each.property = fields[2] == "frame" ? Property::Frame : fields[2] == "alpha" ? Property::Alpha : Property::Shape;
        each.from = std::atof(fields[3].c_str());
        each.to = std::atof(fields[4].c_str());
        each.valueFrom = std::atof(fields[5].c_str());
        each.valueTo = std::atof(fields[6].c_str());
        if (fields.size() > 7)
            for (const std::string& point : split(fields[7], ' '))
            {
                const std::vector<std::string> numbers = split(point, ',');
                if (numbers.size() != 5)
                    continue;
                PointShift shift;
                shift.layer = std::atoi(numbers[0].c_str());
                shift.element = std::atoi(numbers[1].c_str());
                shift.point = std::atoi(numbers[2].c_str());
                shift.dx = std::atof(numbers[3].c_str());
                shift.dy = std::atof(numbers[4].c_str());
                each.shifts.push_back(shift);
            }
        if (!each.armature.empty() && !each.joint.empty())
            list.push_back(each);
    }
    return list;
}

std::vector<Correction> of(const SymbolInstance& instance)
{
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first == kAttribute)
            return decode(attribute.second);
    return {};
}

bool any(const SymbolInstance& instance)
{
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first == kAttribute)
            return !attribute.second.empty();
    return false;
}

std::vector<std::pair<std::string, std::string>> withCorrections(const SymbolInstance& instance,
    const std::vector<Correction>& list)
{
    std::vector<std::pair<std::string, std::string>> attributes = instance.extraAttributes;
    const std::string text = encode(list);
    for (auto it = attributes.begin(); it != attributes.end(); ++it)
        if (it->first == kAttribute)
        {
            if (text.empty())
                attributes.erase(it);
            else
                it->second = text;
            return attributes;
        }
    if (!text.empty())
        attributes.emplace_back(kAttribute, text);
    return attributes;
}

bool anyIn(const Document& document)
{
    std::vector<Timeline*> timelines = allTimelines(const_cast<Document&>(document));
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline->layers)
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                    if (element && element->elementType() == Element::Type::SymbolInstance &&
                        any(*static_cast<const SymbolInstance*>(element)))
                        return true;
    return false;
}

double weight(const Correction& correction, double degrees)
{
    const double span = correction.to - correction.from;
    if (std::fabs(span) < 1.0e-9)
        return degrees >= correction.to ? 1.0 : 0.0;
    return std::clamp((degrees - correction.from) / span, 0.0, 1.0);
}

double jointAngle(const std::vector<IKJoint>& joints, int joint)
{
    if (joint < 0 || static_cast<size_t>(joint) >= joints.size())
        return 0.0;
    const IKJoint& own = joints[static_cast<size_t>(joint)];
    double turn = own.angle;
    if (own.parent >= 0 && static_cast<size_t>(own.parent) < joints.size())
        turn -= joints[static_cast<size_t>(own.parent)].angle;
    double degrees = std::remainder(turn * 180.0 / kPi, 360.0);
    if (degrees <= -180.0)
        degrees += 360.0;
    return degrees;
}

int jointNamed(const IKArmature& armature, const std::string& node)
{
    const std::vector<ArmatureJoint> joints = armature.symbolJoints();
    for (size_t i = 0; i < joints.size(); ++i)
        if (joints[i].node == node)
            return static_cast<int>(i);
    return -1;
}

const std::string* treeOf(const Frame& frame)
{
    for (const std::string& child : frame.extraChildren)
    {
        const size_t at = child.find('<');
        if (at != std::string::npos && child.compare(at, 7, "<IKTree") == 0)
            return &child;
    }
    return nullptr;
}

std::vector<Point> points(const Shape& shape)
{
    std::vector<Point> out;
    for (const Edge* edge : shape.edges)
        for (const Path* path : edge ? edge->paths : std::vector<Path*>())
            for (const PathSegment* segment : path ? path->segments : std::vector<PathSegment*>())
                if (segment)
                    out.insert(out.end(), segment->points.begin(), segment->points.end());
    return out;
}

std::vector<IKJoint> jointsAt(const IKArmature& armature, int into)
{
    return armature.sprungAt(std::clamp(into, 0, std::max(0, armature.frames() - 1)));
}

Rig::Rig() = default;
Rig::~Rig() = default;

void Rig::clear()
{
    _parsed.clear();
}

const IKArmature* Rig::armatureOf(const Frame& pose)
{
    const std::string* xml = treeOf(pose);
    if (!xml)
        return nullptr;
    Parsed& entry = _parsed[&pose];
    if (!entry.armature || entry.xml != *xml)
    {
        entry.xml = *xml;
        auto armature = std::make_shared<IKArmature>();
        entry.armature = armature->parse(*xml) ? armature : nullptr;
    }
    return entry.armature.get();
}

const IKArmature* Rig::armatureAt(const Timeline& timeline, const std::string& name, int frame, int& into)
{
    const IKArmature* best = nullptr;
    int bestDistance = 0;
    for (const Layer* layer : timeline.layers)
        for (const Frame* pose : layer ? layer->frames : std::vector<Frame*>())
        {
            if (!pose || pose->otherTweenType != "IK pose")
                continue;
            const IKArmature* armature = armatureOf(*pose);
            if (!armature || armature->name() != name)
                continue;
            const int end = pose->index + std::max(1, pose->duration) - 1;
            const int distance = frame < pose->index ? pose->index - frame : frame > end ? frame - end : 0;
            if (!best || distance < bestDistance)
            {
                best = armature;
                bestDistance = distance;
                into = std::clamp(frame - pose->index, 0, std::max(0, armature->frames() - 1));
            }
        }
    return best;
}

bool Rig::angleAt(const Timeline& timeline, int frame, const Correction& correction, double& degrees)
{
    int into = 0;
    const IKArmature* armature = armatureAt(timeline, correction.armature, frame, into);
    const int joint = armature ? jointNamed(*armature, correction.joint) : -1;
    if (joint < 0)
        return false;
    degrees = jointAngle(jointsAt(*armature, into), joint);
    return true;
}

Effect Rig::effect(const SymbolInstance& instance, int frame)
{
    const Timeline* timeline = timelineOf(&instance);
    return timeline ? effect(*timeline, of(instance), frame) : Effect();
}

Effect Rig::effect(const Timeline& timeline, const std::vector<Correction>& list, int frame)
{
    Effect effect;
    for (const Correction& correction : list)
    {
        if (correction.property == Property::Shape)
            continue;
        double degrees = 0.0;
        if (!angleAt(timeline, frame, correction, degrees))
            continue;
        const double t = weight(correction, degrees);
        const double value = correction.valueFrom + (correction.valueTo - correction.valueFrom) * t;
        if (correction.property == Property::Frame)
            effect.frame = std::max(0, static_cast<int>(std::lround(value)) - 1);
        else
            effect.alpha *= std::clamp(value / 100.0, 0.0, 1.0);
    }
    return effect;
}

bool correctShape(Shape& shape, const Shape& rest, const SymbolInstance& container, const IKArmature& armature,
    int into, int layer, int element, const std::vector<Correction>& list, const std::string& leaveOut)
{
    const std::vector<IKJoint> now = jointsAt(armature, into);
    std::vector<Point> offsets;
    std::vector<PointInfo> info;
    ShapeBinding kept;
    const ShapeBinding* binding = nullptr;
    std::vector<IKJoint> first;
    Linear toShape;
    bool moved = false;
    for (const Correction& correction : list)
    {
        if (correction.property != Property::Shape || correction.joint == leaveOut)
            continue;
        const int joint = jointNamed(armature, correction.joint);
        if (joint < 0)
            continue;
        const double t = weight(correction, jointAngle(now, joint));
        if (t <= 0.0)
            continue;
        for (const PointShift& shift : correction.shifts)
        {
            if (shift.layer != layer || shift.element != element)
                continue;
            if (info.empty())
            {
                const Transform toArmature = toArmatureOf(container, shape);
                if (!Linear::of(toArmature).inverted(toShape))
                    return false;
                info = infoOf(rest, toArmatureOf(container, rest));
                offsets.assign(info.size(), Point());
                binding = bindingFor(armature, rest, kept);
                first = armature.shownAt(0);
            }
            if (shift.point < 0 || static_cast<size_t>(shift.point) >= info.size())
                continue;
            const Linear carry = carriedBy(info[static_cast<size_t>(shift.point)], binding, first, now);
            const Point v = toShape.apply(carry.apply(Point(shift.dx * t, shift.dy * t)));
            offsets[static_cast<size_t>(shift.point)].x += v.x;
            offsets[static_cast<size_t>(shift.point)].y += v.y;
            moved = true;
        }
    }
    if (!moved || points(shape).size() != offsets.size())
        return false;
    movePoints(shape, offsets);
    return true;
}

bool recordShifts(const Shape& base, const Shape& edited, const Shape& rest, const SymbolInstance& container,
    const IKArmature& armature, int into, int layer, int element, std::vector<PointShift>& shifts)
{
    const std::vector<Point> before = points(base);
    const std::vector<Point> after = points(edited);
    const std::vector<PointInfo> info = infoOf(rest, toArmatureOf(container, rest));
    if (before.size() != after.size() || before.size() != info.size())
        return false;
    const Linear toArmature = Linear::of(toArmatureOf(container, edited));
    ShapeBinding kept;
    const ShapeBinding* binding = bindingFor(armature, rest, kept);
    const std::vector<IKJoint> first = armature.shownAt(0);
    const std::vector<IKJoint> now = jointsAt(armature, into);
    for (size_t i = 0; i < before.size(); ++i)
    {
        const Point moved(after[i].x - before[i].x, after[i].y - before[i].y);
        if (std::fabs(moved.x) < 1.0e-4 && std::fabs(moved.y) < 1.0e-4)
            continue;
        Linear back;
        if (!carriedBy(info[i], binding, first, now).inverted(back))
            continue;
        const Point v = back.apply(toArmature.apply(moved));
        shifts.push_back(PointShift{layer, element, static_cast<int>(i), v.x, v.y});
    }
    return true;
}

SymbolInstance* containerOn(const Frame& pose)
{
    for (Element* element : pose.elements)
        if (element && element->elementType() == Element::Type::SymbolInstance)
        {
            auto* instance = static_cast<SymbolInstance*>(element);
            if (instance->symbol && instance->symbol->otherSymbolType == "ik container")
                return instance;
        }
    return nullptr;
}

std::set<std::string> drivingJoints(const Timeline& timeline, const std::string& armature)
{
    std::set<std::string> joints;
    for (const Layer* layer : timeline.layers)
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                if (element && element->elementType() == Element::Type::SymbolInstance)
                    for (const Correction& correction : of(*static_cast<const SymbolInstance*>(element)))
                        if (correction.armature == armature)
                            joints.insert(correction.joint);
    return joints;
}

int bake(Document& document, std::vector<std::string>& notes)
{
    Rig rig;
    int baked = 0;
    // The timelines as they are: symbols made on the way hold nothing to
    // bake.
    for (Timeline* timeline : allTimelines(document))
        for (Layer* layer : std::vector<Layer*>(timeline->layers))
            for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (Element* element : frame ? frame->elements : std::vector<Element*>())
                {
                    if (!element || element->elementType() != Element::Type::SymbolInstance)
                        continue;
                    auto* instance = static_cast<SymbolInstance*>(element);
                    const std::vector<Correction> list = of(*instance);
                    if (list.empty())
                        continue;
                    removeAttribute(*instance);
                    ++baked;
                    const bool shapes = std::any_of(list.begin(), list.end(),
                        [](const Correction& each) { return each.property == Property::Shape; });
                    const bool others = std::any_of(list.begin(), list.end(),
                        [](const Correction& each) { return each.property != Property::Shape; });

                    // A boned shape: each frame of its container corrected
                    // as it shows, from the rest shapes as they were.
                    const IKArmature* armature = shapes ? rig.armatureOf(*frame) : nullptr;
                    Symbol* container = instance->symbol;
                    if (armature && container && container->otherSymbolType == "ik container" &&
                        !container->timelines.empty() && container->timelines.front())
                    {
                        const std::vector<Layer*>& inside = container->timelines.front()->layers;
                        for (size_t li = 0; li < inside.size(); ++li)
                        {
                            const Layer* own = inside[li];
                            const Frame* restFrame = own ? frameAt(*own, instance->firstFrame) : nullptr;
                            if (!restFrame)
                                continue;
                            std::vector<std::unique_ptr<Shape>> rests;
                            for (const Element* each : restFrame->elements)
                                rests.emplace_back(each && each->elementType() == Element::Type::Shape
                                        ? cloneShape(*static_cast<const Shape*>(each), nullptr) : nullptr);
                            for (int k = 0; k < armature->frames(); ++k)
                            {
                                Frame* shown = frameAt(*own, instance->firstFrame + k);
                                // A frame held over several of the span's
                                // shows the first of them.
                                if (!shown || shown->index != instance->firstFrame + k)
                                    continue;
                                for (size_t ei = 0; ei < shown->elements.size() && ei < rests.size(); ++ei)
                                {
                                    Element* art = shown->elements[ei];
                                    if (art && rests[ei] && art->elementType() == Element::Type::Shape)
                                        correctShape(*static_cast<Shape*>(art), *rests[ei], *instance, *armature, k,
                                            static_cast<int>(li), static_cast<int>(ei), list);
                                }
                            }
                        }
                        notes.push_back("The bone corrections of the boned shape " + container->name +
                            " are drawn into each of its frames.");
                    }

                    // Frames and alphas: a graphic of its own showing the
                    // symbol as each frame calls for, a keyframe a change.
                    Symbol* symbol = instance->symbol;
                    if (!others || !symbol)
                        continue;
                    const int start = frame->index;
                    const int length = std::max(1, frame->duration);
                    const int symbolFrames = symbolLength(*symbol);
                    auto* wrapper = new Symbol(nullptr);
                    wrapper->name = uniqueName(document, symbol->name + " corrected");
                    wrapper->href = wrapper->name + ".xml";
                    wrapper->symbolType = SymbolType::Graphic;
                    auto* inner = new Timeline(wrapper);
                    inner->name = wrapper->name;
                    wrapper->timelines.push_back(inner);
                    auto* content = new Layer(inner);
                    content->name = "Layer 1";
                    inner->layers.push_back(content);
                    Frame* last = nullptr;
                    int lastFrame = -1;
                    double lastAlpha = -1.0;
                    for (int k = 0; k < length; ++k)
                    {
                        const Effect effect = rig.effect(*timeline, list, start + k);
                        const bool graphic = instance->symbolType == SymbolType::Graphic;
                        int shownFrame = graphic ? graphicFrame(instance->loopType, instance->firstFrame,
                                                       instance->lastFrame, symbolFrames, k) : 0;
                        if (graphic && effect.frame >= 0)
                            shownFrame = std::min(effect.frame, symbolFrames - 1);
                        if (last && shownFrame == lastFrame && effect.alpha == lastAlpha)
                        {
                            ++last->duration;
                            continue;
                        }
                        last = new Frame(content);
                        last->index = k;
                        last->duration = 1;
                        auto* placed = new SymbolInstance(last);
                        placed->libraryItemName = symbol->name;
                        placed->symbol = symbol;
                        placed->symbolType = instance->symbolType;
                        placed->transformationPoint = instance->transformationPoint;
                        placed->firstFrame = graphic ? shownFrame : instance->firstFrame;
                        placed->loopType = graphic ? LoopType::SingleFrame : instance->loopType;
                        placed->colorTransform.alphaMultiplier = effect.alpha;
                        last->elements.push_back(placed);
                        content->frames.push_back(last);
                        lastFrame = shownFrame;
                        lastAlpha = effect.alpha;
                    }
                    addSymbol(document, wrapper);
                    instance->libraryItemName = wrapper->name;
                    instance->symbol = wrapper;
                    instance->symbolType = SymbolType::Graphic;
                    instance->firstFrame = 0;
                    instance->lastFrame = -1;
                    instance->loopType = LoopType::Loop;
                    notes.push_back("The bone corrections of an instance of " + symbol->name + " on layer " +
                        layer->name + " became the graphic symbol " + wrapper->name + ".");
                }
    return baked;
}

} // namespace corrections

} // namespace fla
