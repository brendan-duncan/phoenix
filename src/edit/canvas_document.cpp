#include "canvas_document.h"

#include "../data/document.h"

#include "tinyxml2.h"

#include <cstdlib>
#include <cstring>

namespace fla::canvas {

namespace {

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

const char* const kSettingsFile = "PublishSettings.xml";

/// The profile in use: the one marked current, or the first.
const XMLElement* currentProfile(const XMLDocument& document)
{
    const XMLElement* root = document.RootElement();
    if (!root)
        return nullptr;
    if (std::strcmp(root->Name(), "flash_profile") == 0)
        return root;
    const XMLElement* first = root->FirstChildElement("flash_profile");
    for (const XMLElement* profile = first; profile; profile = profile->NextSiblingElement("flash_profile"))
    {
        const char* current = profile->Attribute("current");
        if (current && std::strcmp(current, "true") == 0)
            return profile;
    }
    return first;
}

/// The document's PublishSettings.xml as text, empty when it has none.
std::string settingsOf(const Document& document)
{
    for (const auto& [path, bytes] : document.extraFiles)
        if (path == kSettingsFile)
        {
            const std::vector<uint8_t>& data = bytes.get();
            return std::string(data.begin(), data.end());
        }
    return std::string();
}

} // namespace

bool publishesCanvas(const std::string& publishSettings)
{
    XMLDocument xml;
    if (publishSettings.empty() || xml.Parse(publishSettings.c_str(), publishSettings.size()) != tinyxml2::XML_SUCCESS)
        return false;
    const XMLElement* profile = currentProfile(xml);
    for (const XMLElement* group = profile ? profile->FirstChildElement("PublishProperties") : nullptr; group;
         group = group->NextSiblingElement("PublishProperties"))
    {
        const char* id = group->Attribute("id");
        if (id && std::strcmp(id, kPublishFormat) == 0)
            return true;
    }
    return false;
}

bool isCanvasDocument(const Document& document)
{
    return publishesCanvas(settingsOf(document));
}

std::string newPublishSettings(const std::string& name)
{
    XMLDocument xml;
    xml.InsertEndChild(xml.NewDeclaration());
    XMLElement* root = xml.NewElement("flash_profiles");
    xml.InsertEndChild(root);
    XMLElement* profile = xml.NewElement("flash_profile");
    profile->SetAttribute("version", "1.0");
    profile->SetAttribute("name", "Default");
    profile->SetAttribute("current", "true");
    root->InsertEndChild(profile);
    const auto text = [&xml](XMLElement* parent, const char* tag, const std::string& value) {
        XMLElement* element = xml.NewElement(tag);
        element->SetText(value.c_str());
        parent->InsertEndChild(element);
    };
    // The SWF formats off: a Canvas document publishes HTML and JavaScript.
    XMLElement* formats = xml.NewElement("PublishFormatProperties");
    formats->SetAttribute("enabled", "true");
    profile->InsertEndChild(formats);
    text(formats, "defaultNames", "1");
    for (const char* format : {"flash", "projectorWin", "projectorMac", "html", "gif", "jpeg", "png", "svg", "swc"})
        text(formats, format, "0");
    XMLElement* canvas = xml.NewElement("PublishProperties");
    canvas->SetAttribute("id", kPublishFormat);
    canvas->SetAttribute("name", kPublishFormat);
    canvas->SetAttribute("enabled", "true");
    profile->InsertEndChild(canvas);
    const std::pair<const char*, std::string> properties[] = {{"filename", name + ".html"}, {"default", "true"},
        {"loop", "true"}, {"includeHTML", "true"}, {"hostedLibs", "true"}, {"libraryPath", "libs/"},
        {"exportImages", "true"}, {"imagesPath", "images/"}, {"exportSounds", "true"}, {"soundsPath", "sounds/"},
        {"namespace", "lib"}, {"frameBounds", "false"}};
    for (const auto& [key, value] : properties)
    {
        XMLElement* property = xml.NewElement("Property");
        property->SetAttribute("name", key);
        property->SetText(value.c_str());
        canvas->InsertEndChild(property);
    }
    tinyxml2::XMLPrinter printer;
    xml.Print(&printer);
    return printer.CStr();
}

void makeCanvasDocument(Document& document, const std::string& name)
{
    const std::string xml = newPublishSettings(name);
    std::vector<uint8_t> bytes(xml.begin(), xml.end());
    for (auto& [path, data] : document.extraFiles)
        if (path == kSettingsFile)
        {
            data = std::move(bytes);
            return;
        }
    document.extraFiles.emplace_back(kSettingsFile, LazyBytes(std::move(bytes)));
}

std::string documentType(const Document& document)
{
    const std::string settings = settingsOf(document);
    if (publishesCanvas(settings))
        return "HTML5 Canvas";
    XMLDocument xml;
    const XMLElement* profile = !settings.empty() && xml.Parse(settings.c_str(), settings.size()) == tinyxml2::XML_SUCCESS
        ? currentProfile(xml)
        : nullptr;
    const XMLElement* flash = profile ? profile->FirstChildElement("PublishFlashProperties") : nullptr;
    const XMLElement* version = flash ? flash->FirstChildElement("ActionScriptVersion") : nullptr;
    const int number = version && version->GetText() ? std::atoi(version->GetText()) : 0;
    return number > 0 ? "ActionScript " + std::to_string(number) + ".0" : std::string("ActionScript");
}

} // namespace fla::canvas
