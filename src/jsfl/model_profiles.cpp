// Publish profiles from scripts: the document's PublishSettings.xml read
// and changed as Publish Settings does (fla::publish), each change one
// edit of the document's files. publishProfiles, currentPublishProfile,
// adding, duplicating, renaming and deleting profiles, exporting and
// importing them as XML (to a string or a file), and the ActionScript
// version and document class the current profile keeps.

#include "extensions.h"
#include "flfile.h"
#include "model.h"

#include "../data/document.h"
#include "../edit/property_command.h"
#include "../edit/publish_settings.h"

#include "tinyxml2.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace jsfl {

namespace {

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

const char* const kPublishFile = "PublishSettings.xml";

std::string publishXml(const fla::Document& document)
{
    for (const auto& [path, bytes] : document.extraFiles)
    {
        if (path == kPublishFile)
        {
            const std::vector<uint8_t>& data = bytes.get();
            return std::string(data.begin(), data.end());
        }
    }
    return std::string();
}

/// The document's name without its extension, for the file names a new
/// profile gets.
std::string baseName(Document& d)
{
    std::string name = d.session.host().name(d.document);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0)
        name.erase(dot);
    return name.empty() ? std::string("Untitled") : name;
}

/// The settings file, or one of the usual form when the document has none.
std::string settingsOf(Document& d)
{
    const std::string xml = publishXml(d.data());
    if (!xml.empty())
        return xml;
    return fla::publish::write(std::string(), fla::publish::read(std::string(), baseName(d)), baseName(d));
}

/// \a xml made the document's settings file, as part of the script's step.
void setSettings(Document& d, const std::string& xml)
{
    fla::Document& data = d.data();
    if (xml == publishXml(data))
        return;
    using Files = std::vector<std::pair<std::string, fla::LazyBytes>>;
    Files after = data.extraFiles;
    bool found = false;
    for (auto& [path, bytes] : after)
    {
        if (path == kPublishFile)
        {
            bytes = fla::LazyBytes(std::vector<uint8_t>(xml.begin(), xml.end()));
            found = true;
        }
    }
    if (!found)
        after.emplace_back(kPublishFile, fla::LazyBytes(std::vector<uint8_t>(xml.begin(), xml.end())));
    d.push(fla::CommandPtr(new fla::SetPropertyCommand<Files>(data.extraFiles, data.extraFiles, after, "Publish Settings")),
        false);
}

std::string printed(XMLDocument& document)
{
    tinyxml2::XMLPrinter printer;
    document.Print(&printer);
    return printer.CStr();
}

/// The profile of that name, or the current one for an empty name.
XMLElement* profileIn(XMLDocument& document, const std::string& name)
{
    XMLElement* root = document.RootElement();
    if (!root)
        return nullptr;
    if (std::strcmp(root->Name(), "flash_profile") == 0)
        return name.empty() || (root->Attribute("name") && name == root->Attribute("name")) ? root : nullptr;
    XMLElement* current = nullptr;
    for (XMLElement* profile = root->FirstChildElement("flash_profile"); profile;
         profile = profile->NextSiblingElement("flash_profile"))
    {
        const char* own = profile->Attribute("name");
        if (!name.empty() && own && name == own)
            return profile;
        const char* isCurrent = profile->Attribute("current");
        if (!current && isCurrent && std::strcmp(isCurrent, "true") == 0)
            current = profile;
    }
    if (!name.empty())
        return nullptr;
    return current ? current : root->FirstChildElement("flash_profile");
}

int indexOf(const std::string& xml, const std::string& name)
{
    const std::vector<std::string> names = fla::publish::profiles(xml);
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

/// The profile \a name (or the current one) alone, in a file of its own.
std::string exportProfile(Document& d, const std::string& name)
{
    XMLDocument settings;
    const std::string xml = settingsOf(d);
    if (settings.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        throw Error("The document's publish settings could not be read.");
    XMLElement* profile = profileIn(settings, name);
    if (!profile)
        throw Error("There is no publish profile called " + name + ".");
    XMLDocument out;
    out.InsertEndChild(out.NewDeclaration());
    XMLElement* root = out.NewElement("flash_profiles");
    out.InsertEndChild(root);
    root->InsertEndChild(profile->DeepClone(&out));
    return printed(out);
}

/// The profiles in \a text added to the document's, each replacing one of
/// its name, the first made current; its index, or -1 when \a text holds
/// none.
int importProfiles(Document& d, const std::string& text)
{
    XMLDocument incoming;
    if (incoming.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS || !incoming.RootElement())
        return -1;
    std::vector<const XMLElement*> profiles;
    const XMLElement* root = incoming.RootElement();
    if (std::strcmp(root->Name(), "flash_profile") == 0)
        profiles.push_back(root);
    for (const XMLElement* p = root->FirstChildElement("flash_profile"); p; p = p->NextSiblingElement("flash_profile"))
        profiles.push_back(p);
    if (profiles.empty())
        return -1;
    XMLDocument settings;
    const std::string xml = settingsOf(d);
    if (settings.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !settings.RootElement())
        return -1;
    XMLElement* list = settings.RootElement();
    if (std::strcmp(list->Name(), "flash_profiles") != 0)
        return -1;
    for (XMLElement* p = list->FirstChildElement("flash_profile"); p; p = p->NextSiblingElement("flash_profile"))
        p->SetAttribute("current", "false");
    std::string first;
    for (const XMLElement* profile : profiles)
    {
        const std::string name = profile->Attribute("name") ? profile->Attribute("name") : "Imported";
        XMLElement* copy = profile->DeepClone(&settings)->ToElement();
        copy->SetAttribute("name", name.c_str());
        copy->SetAttribute("current", first.empty() ? "true" : "false");
        if (XMLElement* existing = profileIn(settings, name))
        {
            list->InsertAfterChild(existing, copy);
            list->DeleteChild(existing);
        }
        else
            list->InsertEndChild(copy);
        if (first.empty())
            first = name;
    }
    const std::string written = printed(settings);
    setSettings(d, written);
    return indexOf(written, first);
}

/// A setting of the current profile's PublishFlashProperties.
std::string flashSetting(Document& d, const char* key)
{
    XMLDocument settings;
    const std::string xml = publishXml(d.data());
    if (xml.empty() || settings.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return std::string();
    const XMLElement* profile = profileIn(settings, std::string());
    const XMLElement* flash = profile ? profile->FirstChildElement("PublishFlashProperties") : nullptr;
    const XMLElement* value = flash ? flash->FirstChildElement(key) : nullptr;
    return value && value->GetText() ? value->GetText() : std::string();
}

void setFlashSetting(Document& d, const char* key, const std::string& value)
{
    XMLDocument settings;
    const std::string xml = settingsOf(d);
    if (settings.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        throw Error("The document's publish settings could not be read.");
    XMLElement* profile = profileIn(settings, std::string());
    if (!profile)
        throw Error("The document has no publish profile.");
    XMLElement* flash = profile->FirstChildElement("PublishFlashProperties");
    if (!flash)
    {
        flash = settings.NewElement("PublishFlashProperties");
        flash->SetAttribute("enabled", "true");
        profile->InsertEndChild(flash);
    }
    XMLElement* element = flash->FirstChildElement(key);
    if (!element)
    {
        element = settings.NewElement(key);
        flash->InsertEndChild(element);
    }
    element->SetText(value.c_str());
    setSettings(d, printed(settings));
}

bool readFile(const std::string& path, std::string& text)
{
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in)
        return false;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

void describeDocumentProfiles(ClassDef<Document>& def)
{
    def.get("publishProfiles", [](Document& d) { return Value::list(fla::publish::profiles(settingsOf(d))); });
    def.prop("currentPublishProfile", [](Document& d) { return Value(fla::publish::currentProfile(settingsOf(d))); },
        [](Document& d, const Value& v) {
            const std::string xml = settingsOf(d);
            const std::string name = v.toString();
            if (indexOf(xml, name) < 0)
                throw Error("There is no publish profile called " + name + ".");
            setSettings(d, fla::publish::setCurrentProfile(xml, name));
        });
    // A new profile, made current: a copy of the current one, as Publish
    // Settings makes one. Its index.
    const auto add = [](Document& d, const Args& args, const char* fallback) {
        const std::string xml = settingsOf(d);
        std::string name = given(args, 0) ? arg(args, 0).toString() : std::string();
        if (name.empty())
        {
            name = fallback;
            for (int n = 2; indexOf(xml, name) >= 0; ++n)
                name = std::string(fallback) + " " + std::to_string(n);
        }
        if (indexOf(xml, name) >= 0)
            return Value(-1);
        const std::string written = fla::publish::addProfile(xml, name, fla::publish::currentProfile(xml), baseName(d));
        setSettings(d, written);
        return Value(indexOf(written, name));
    };
    def.method("addNewPublishProfile", 1, [add](Document& d, const Args& args) { return add(d, args, "Profile"); });
    def.method("duplicatePublishProfile", 1, [add](Document& d, const Args& args) {
        const std::string current = fla::publish::currentProfile(settingsOf(d));
        return add(d, args, (current + " copy").c_str());
    });
    def.method("renamePublishProfile", 1, [](Document& d, const Args& args) {
        const std::string xml = settingsOf(d);
        const std::string to = arg(args, 0).toString();
        if (to.empty() || indexOf(xml, to) >= 0)
            return Value(false);
        setSettings(d, fla::publish::renameProfile(xml, fla::publish::currentProfile(xml), to));
        return Value(true);
    });
    // The current profile goes (never the last); the index of the one
    // current after.
    def.method("deletePublishProfile", 0, [](Document& d, const Args&) {
        const std::string xml = settingsOf(d);
        const std::string written = fla::publish::removeProfile(xml, fla::publish::currentProfile(xml));
        setSettings(d, written);
        return Value(indexOf(written, fla::publish::currentProfile(written)));
    });
    def.method("exportPublishProfileString", 1, [](Document& d, const Args& args) {
        return Value(exportProfile(d, given(args, 0) ? arg(args, 0).toString() : std::string()));
    });
    def.method("importPublishProfileString", 1, [](Document& d, const Args& args) {
        return Value(importProfiles(d, arg(args, 0).toString()));
    });
    def.method("exportPublishProfile", 2, [](Document& d, const Args& args) {
        const std::string path = uri::toPath(arg(args, 0).toString());
        const std::string xml = exportProfile(d, given(args, 1) ? arg(args, 1).toString() : std::string());
        std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
        out << xml;
        if (!out)
            throw Error(path + " could not be written.");
        return Value();
    });
    def.method("importPublishProfile", 1, [](Document& d, const Args& args) {
        std::string text;
        if (!readFile(uri::toPath(arg(args, 0).toString()), text))
            return Value(-1);
        return Value(importProfiles(d, text));
    });
    // The current profile's ActionScript version (3 where it says none)
    // and document class.
    def.prop("asVersion",
        [](Document& d) {
            const std::string version = flashSetting(d, "ActionScriptVersion");
            return Value(version.empty() ? 3 : std::atoi(version.c_str()));
        },
        [](Document& d, const Value& v) {
            const int version = v.toInt();
            if (version < 1 || version > 3)
                throw Error("asVersion is 1, 2 or 3.");
            setFlashSetting(d, "ActionScriptVersion", std::to_string(version));
        });
    def.prop("docClass", [](Document& d) { return Value(flashSetting(d, "DocumentClass")); },
        [](Document& d, const Value& v) { setFlashSetting(d, "DocumentClass", v.isNullish() ? std::string() : v.toString()); });
}

} // namespace jsfl
