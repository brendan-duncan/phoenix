#include "class_path.h"

#include "../../data/document.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace fla {
namespace play {

namespace {

namespace fs = std::filesystem;

/// The document's publish settings, parsed; false when it has none.
bool publishSettings(const Document& document, tinyxml2::XMLDocument& xml)
{
    for (const auto& [path, bytes] : document.extraFiles)
    {
        if (path != "PublishSettings.xml")
            continue;
        const std::vector<uint8_t>& data = bytes.get();
        return xml.Parse(reinterpret_cast<const char*>(data.data()), data.size()) == tinyxml2::XML_SUCCESS;
    }
    return false;
}

/// Every element named \a name under \a at, in document order.
void elementsNamed(const tinyxml2::XMLElement* at, const char* name, std::vector<const tinyxml2::XMLElement*>& found)
{
    for (const tinyxml2::XMLElement* child = at ? at->FirstChildElement() : nullptr; child;
         child = child->NextSiblingElement())
    {
        if (std::strcmp(child->Name(), name) == 0)
            found.push_back(child);
        elementsNamed(child, name, found);
    }
}

/// The text of the first element named \a name, or empty.
std::string firstText(const tinyxml2::XMLDocument& xml, const char* name)
{
    std::vector<const tinyxml2::XMLElement*> found;
    elementsNamed(xml.RootElement(), name, found);
    for (const tinyxml2::XMLElement* element : found)
        if (element->GetText())
            return element->GetText();
    return std::string();
}

bool isDirectory(const fs::path& path)
{
    std::error_code error;
    return fs::is_directory(path, error);
}

/// A path from the publish settings: absolute, or from the movie's folder;
/// none for one that names a macro ($(AppConfig)...).
std::optional<fs::path> settingsPath(const std::string& entry, const fs::path& base)
{
    if (entry.empty() || entry.find("$(") != std::string::npos)
        return std::nullopt;
    std::string text = entry;
    std::replace(text.begin(), text.end(), '\\', '/');
    const fs::path path = fs::u8path(text);
    return path.is_absolute() ? path : base / path;
}

} // namespace

ClassPath::ClassPath(const Document& document, const std::string& folder, const std::vector<std::string>& extra)
{
    const fs::path base = fs::u8path(folder);
    addRoot(base);
    tinyxml2::XMLDocument xml;
    std::vector<fs::path> libraries;
    if (publishSettings(document, xml))
    {
        std::vector<const tinyxml2::XMLElement*> lists;
        elementsNamed(xml.RootElement(), "AS3PackagePaths", lists);
        for (const tinyxml2::XMLElement* list : lists)
        {
            std::stringstream entries(list->GetText() ? list->GetText() : "");
            std::string entry;
            while (std::getline(entries, entry, ';'))
                if (const auto path = settingsPath(entry, base))
                    addRoot(*path);
        }
        // Libraries: a .swc is not read, but its sources often sit beside
        // it, as as3corelib's do (lib/as3corelib.swc and src/).
        std::vector<const tinyxml2::XMLElement*> swcs;
        elementsNamed(xml.RootElement(), "swc-path", swcs);
        for (const tinyxml2::XMLElement* swc : swcs)
            if (const auto path = settingsPath(swc->GetText() ? swc->GetText() : "", base))
                libraries.push_back(*path);
    }
    for (const fs::path& library : libraries)
    {
        const fs::path folderOf = library.extension() == ".swc" ? library.parent_path() : library;
        if (folderOf.filename() == "lib" || folderOf.filename() == "bin")
            if (isDirectory(folderOf.parent_path() / "src"))
                addRoot(folderOf.parent_path() / "src");
        if (isDirectory(folderOf / "src"))
            addRoot(folderOf / "src");
    }
    // Any src folder near the movie: sources kept with it.
    std::error_code error;
    std::vector<fs::path> near{base};
    for (int depth = 0; depth < 3; ++depth)
    {
        std::vector<fs::path> deeper;
        for (const fs::path& at : near)
        {
            for (fs::directory_iterator it(at, error), end; !error && it != end; it.increment(error))
            {
                if (!it->is_directory(error))
                    continue;
                if (it->path().filename() == "src")
                    addRoot(it->path());
                else
                    deeper.push_back(it->path());
            }
            error.clear();
        }
        near = deeper;
        if (near.size() > 200)
            break;
    }
    for (const std::string& path : extra)
        if (const auto root = settingsPath(path, base))
            addRoot(*root);
}

void ClassPath::addRoot(const fs::path& root)
{
    const fs::path normal = root.lexically_normal();
    if (std::find(_roots.begin(), _roots.end(), normal) == _roots.end())
        _roots.push_back(normal);
}

std::optional<ClassFile> ClassPath::find(const std::string& qualifiedName) const
{
    if (qualifiedName.empty())
        return std::nullopt;
    std::string relative = qualifiedName;
    std::replace(relative.begin(), relative.end(), '.', '/');
    relative += ".as";
    const size_t slash = relative.find_last_of('/');
    const std::string fileName = slash == std::string::npos ? relative : relative.substr(slash + 1);
    for (const fs::path& root : _roots)
    {
        std::ifstream in(root / fs::u8path(relative), std::ios::binary);
        if (!in)
            continue;
        std::stringstream text;
        text << in.rdbuf();
        return ClassFile{text.str(), fileName};
    }
    // Phoenix's own flash.* classes stand for the player's, which name no
    // file in a stack trace.
    if (builtIn)
        if (std::optional<std::string> source = builtIn(relative))
            return ClassFile{*source, relative.compare(0, 6, "flash/") == 0 ? std::string() : fileName};
    return std::nullopt;
}

int actionScriptVersion(const Document& document)
{
    tinyxml2::XMLDocument xml;
    if (!publishSettings(document, xml))
        return 0;
    const std::string version = firstText(xml, "ActionScriptVersion");
    return version.empty() ? 0 : std::atoi(version.c_str());
}

std::string documentClassOf(const Document& document)
{
    tinyxml2::XMLDocument xml;
    if (!publishSettings(document, xml))
        return std::string();
    return firstText(xml, "DocumentClass");
}

} // namespace play
} // namespace fla
