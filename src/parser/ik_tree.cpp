#include "ik_tree.h"

#include "../third_party/tinyxml2/tinyxml2.h"

#include <cmath>
#include <cstring>

namespace fla {

namespace {

/// Where a node's joint is: the translation of its worldMatrix.
bool jointOf(const tinyxml2::XMLElement* node, Point& joint)
{
    const tinyxml2::XMLElement* world = node->FirstChildElement("worldMatrix");
    const tinyxml2::XMLElement* matrix = world ? world->FirstChildElement("Matrix") : nullptr;
    if (!matrix)
        return false;
    joint.x = matrix->DoubleAttribute("tx", 0.0);
    joint.y = matrix->DoubleAttribute("ty", 0.0);
    return true;
}

void collect(const tinyxml2::XMLElement* node, const Point& joint, std::vector<IKBone>& bones)
{
    const tinyxml2::XMLElement* children = node->FirstChildElement("childNodes");
    if (!children)
        return;
    for (const tinyxml2::XMLElement* child = children->FirstChildElement("ChildNode"); child;
         child = child->NextSiblingElement("ChildNode"))
    {
        Point tail;
        if (!jointOf(child, tail))
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
        collect(child, tail, bones);
    }
}

} // namespace

std::vector<IKBone> ikBones(const std::string& xml)
{
    std::vector<IKBone> bones;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS)
        return bones;
    const tinyxml2::XMLElement* tree = document.FirstChildElement("IKTree");
    if (!tree)
        return bones;
    for (const tinyxml2::XMLElement* root = tree->FirstChildElement("IKNode"); root;
         root = root->NextSiblingElement("IKNode"))
    {
        Point joint;
        if (jointOf(root, joint))
            collect(root, joint, bones);
    }
    return bones;
}

} // namespace fla
