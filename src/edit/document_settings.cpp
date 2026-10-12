#include "document_settings.h"

#include "element_commands.h"
#include "property_command.h"

#include "../data/document.h"
#include "../data/element.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/timeline.h"

namespace fla {

namespace docsettings {

namespace {

const char* const kAttribute = "rulerUnitType";

struct UnitName
{
    Unit unit;
    const char* attribute;
    const char* label;
    double pixels;
};

const UnitName kUnits[] = {
    {Unit::Pixels, "", "px", 1.0},
    {Unit::Inches, "inches", "in", 72.0},
    {Unit::Points, "points", "pt", 1.0},
    {Unit::Centimeters, "centimeters", "cm", 72.0 / 2.54},
    {Unit::Millimeters, "millimeters", "mm", 72.0 / 25.4},
};

const UnitName& nameOf(Unit unit)
{
    for (const UnitName& name : kUnits)
        if (name.unit == unit)
            return name;
    return kUnits[0];
}

void collect(Element* element, std::vector<Element*>& into)
{
    if (auto* group = dynamic_cast<Group*>(element))
    {
        for (Element* member : group->members)
            collect(member, into);
        return;
    }
    into.push_back(element);
}

} // namespace

Unit unitOf(const Document& document)
{
    for (const auto& [key, value] : document.extraAttributes)
        if (key == kAttribute)
            for (const UnitName& name : kUnits)
                if (value == name.attribute)
                    return name.unit;
    return Unit::Pixels;
}

std::vector<std::pair<std::string, std::string>> withUnit(const Document& document, Unit unit)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : document.extraAttributes)
        if (attribute.first != kAttribute)
            attributes.push_back(attribute);
    if (unit != Unit::Pixels)
        attributes.emplace_back(kAttribute, nameOf(unit).attribute);
    return attributes;
}

double pixelsPer(Unit unit)
{
    return nameOf(unit).pixels;
}

const char* label(Unit unit)
{
    return nameOf(unit).label;
}

std::vector<Element*> sceneElements(Document& document)
{
    std::vector<Element*> elements;
    for (Timeline* scene : document.timelines)
        for (Layer* layer : scene ? scene->layers : std::vector<Layer*>())
            for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (Element* element : frame ? frame->elements : std::vector<Element*>())
                    collect(element, elements);
    return elements;
}

CommandPtr resize(Document& document, int width, int height, bool scaleContent, double dx, double dy)
{
    auto* command = new CompoundCommand("Document Settings");
    const double sx = scaleContent && document.width > 0 ? static_cast<double>(width) / document.width : 1.0;
    const double sy = scaleContent && document.height > 0 ? static_cast<double>(height) / document.height : 1.0;
    if (sx != 1.0 || sy != 1.0 || dx != 0.0 || dy != 0.0)
    {
        for (Element* element : sceneElements(document))
        {
            const Transform& t = element->transform;
            // Scaled about the stage's corner, then moved.
            const Transform after(t.m11 * sx, t.m12 * sy, t.m21 * sx, t.m22 * sy, t.tx * sx + dx, t.ty * sy + dy);
            command->addCommand(CommandPtr(new SetElementTransformCommand(element, t, after, "Document Settings")));
        }
    }
    if (width != document.width)
        command->addCommand(CommandPtr(new SetPropertyCommand<int>(document.width, document.width, width, "Document Settings")));
    if (height != document.height)
        command->addCommand(CommandPtr(new SetPropertyCommand<int>(document.height, document.height, height, "Document Settings")));
    return CommandPtr(command);
}

} // namespace docsettings

} // namespace fla
