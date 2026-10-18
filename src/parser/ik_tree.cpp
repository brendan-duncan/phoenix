#include "ik_tree.h"

#include "../third_party/tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace fla {

namespace {

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    const size_t last = text.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

/// The \a index'th of a comma-separated list of numbers.
bool listEntry(const char* list, int index, double& value)
{
    if (!list || index < 0)
        return false;
    std::stringstream stream(list);
    std::string item;
    for (int i = 0; std::getline(stream, item, ','); ++i)
    {
        if (i == index)
        {
            char* end = nullptr;
            value = std::strtod(item.c_str(), &end);
            return end != item.c_str();
        }
    }
    return false;
}

/// A list read at \a at, between the entries either side.
bool listAt(const char* list, double at, double& value)
{
    const int below = static_cast<int>(std::floor(at));
    double a = 0.0;
    if (!listEntry(list, below, a))
        return false;
    double b = a;
    if (!listEntry(list, below + 1, b))
        b = a;
    value = a + (b - a) * (at - below);
    return true;
}

/// Where a node's joint is, at \a at entries into its lists.
bool jointOf(const tinyxml2::XMLElement* node, double at, Point& joint)
{
    // A shape's armature keeps every frame's joint, in twips.
    const char* type = node->Attribute("nodeType");
    double x = 0.0;
    double y = 0.0;
    if (type && std::strcmp(type, "lsd") == 0 && listAt(node->Attribute("xArray"), at, x) &&
        listAt(node->Attribute("yArray"), at, y))
    {
        joint = Point(x / 20.0, y / 20.0);
        return true;
    }
    // Otherwise where it was saved.
    if (const char* location = node->Attribute("location"))
    {
        const std::string text = location;
        const size_t comma = text.find(',');
        if (comma != std::string::npos && parseIKCoordinate(text.substr(0, comma), x) &&
            parseIKCoordinate(text.substr(comma + 1), y))
        {
            joint = Point(x, y);
            return true;
        }
    }
    const tinyxml2::XMLElement* world = node->FirstChildElement("worldMatrix");
    const tinyxml2::XMLElement* matrix = world ? world->FirstChildElement("Matrix") : nullptr;
    if (!matrix)
        return false;
    joint = Point(matrix->DoubleAttribute("tx", 0.0), matrix->DoubleAttribute("ty", 0.0));
    return true;
}

void collect(const tinyxml2::XMLElement* node, const Point& joint, double frame, std::vector<IKBone>& bones)
{
    const tinyxml2::XMLElement* children = node->FirstChildElement("childNodes");
    if (!children)
        return;
    for (const tinyxml2::XMLElement* child = children->FirstChildElement("ChildNode"); child;
         child = child->NextSiblingElement("ChildNode"))
    {
        Point tail;
        if (!jointOf(child, frame, tail))
            continue;
        if (std::hypot(tail.x - joint.x, tail.y - joint.y) > 1.0e-6)
        {
            IKBone bone;
            bone.head = joint;
            bone.tail = tail;
            const char* name = child->Attribute("boneName");
            bone.name = name ? name : "";
            bones.push_back(bone);
        }
        collect(child, tail, frame, bones);
    }
}

/// Which entry of the joint lists shows \a frame frames into the span.
/// The lists hold the poses' in-betweens evenly in time; the tree's
/// IKState entries give each pose's length and the ease to the next, and
/// the art follows the eased time. A simple quadratic ease of strength s
/// (0 to 100) mixes linear time with its square, so a positive strength
/// starts slowly; a negative one, its mirror, ends slowly. Checked against
/// the shapes ik_poses.fla stores for every frame.
double easedEntry(const tinyxml2::XMLElement* tree, int frame)
{
    const tinyxml2::XMLElement* states = tree->FirstChildElement("states");
    if (!states || frame <= 0)
        return frame;
    int start = 0;
    for (const tinyxml2::XMLElement* state = states->FirstChildElement("IKState"); state;
         state = state->NextSiblingElement("IKState"))
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
            const double amount = std::fabs(strength);
            eased = (1.0 - amount) * t + amount * curve;
        }
        return start + eased * length;
    }
    return frame;
}

} // namespace

bool parseIKCoordinate(const std::string& raw, double& pixels)
{
    const std::string text = trimmed(raw);
    if (text.empty())
        return false;
    double units = 0.0;
    if (text[0] == '#')
    {
        const size_t dot = text.find('.');
        const std::string whole = text.substr(1, dot == std::string::npos ? std::string::npos : dot - 1);
        const std::string fraction = dot == std::string::npos ? std::string() : text.substr(dot + 1);
        if (whole.empty())
            return false;
        char* end = nullptr;
        long value = std::strtol(whole.c_str(), &end, 16);
        if (*end != '\0')
            return false;
        // The whole part is 24-bit two's complement.
        if (value >= 0x800000)
            value -= 0x1000000;
        double part = 0.0;
        if (!fraction.empty())
        {
            const long bits = std::strtol(fraction.c_str(), &end, 16);
            if (*end != '\0')
                return false;
            part = bits / std::pow(16.0, static_cast<double>(fraction.size()));
        }
        units = static_cast<double>(value) + part;
    }
    else
    {
        char* end = nullptr;
        units = std::strtod(text.c_str(), &end);
        if (end == text.c_str())
            return false;
    }
    pixels = units * 256.0 / 20.0;
    return true;
}

std::vector<IKBone> ikBones(const std::string& xml, int frame)
{
    std::vector<IKBone> bones;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return bones;
    const tinyxml2::XMLElement* tree = document.FirstChildElement("IKTree");
    if (!tree)
        return bones;
    const double at = easedEntry(tree, frame);
    for (const tinyxml2::XMLElement* root = tree->FirstChildElement("IKNode"); root;
         root = root->NextSiblingElement("IKNode"))
    {
        Point joint;
        if (!jointOf(root, at, joint))
            continue;
        // The root's joint is the posed node under it of the same name,
        // which keeps every frame's place as the armature moves.
        const tinyxml2::XMLElement* children = root->FirstChildElement("childNodes");
        const char* name = root->Attribute("name");
        for (const tinyxml2::XMLElement* child = children && name ? children->FirstChildElement("ChildNode") : nullptr;
             child; child = child->NextSiblingElement("ChildNode"))
        {
            const char* own = child->Attribute("name");
            Point moved;
            if (own && std::strcmp(own, name) == 0 && child->Attribute("xArray") && jointOf(child, at, moved))
                joint = moved;
        }
        collect(root, joint, at, bones);
    }
    return bones;
}

} // namespace fla
