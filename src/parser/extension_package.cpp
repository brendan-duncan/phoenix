#include "extension_package.h"

#include "zip_reader.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

namespace extensions {

namespace {

/// What the record of an installed extension is named, in its folder.
const char* const kRecord = "phoenix-extension.txt";

/// A package's files, by their names in it ("/" between folders), from a
/// zip or a folder.
class Source
{
public:
    bool open(const std::string& path, std::string& error)
    {
        std::error_code ec;
        if (fs::is_directory(path, ec))
        {
            _folder = path;
            for (auto it = fs::recursive_directory_iterator(path, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec))
            {
                if (it->is_regular_file(ec))
                    _names.push_back(fs::relative(it->path(), path, ec).generic_string());
            }
            return true;
        }
        _zip = std::make_unique<ZipReader>();
        if (!_zip->open(path))
        {
            error = "This is not an extension package Phoenix can open: " + _zip->errorString();
            return false;
        }
        for (const std::string& name : _zip->getFileList())
            if (!name.empty() && name.back() != '/')
                _names.push_back(name);
        return true;
    }

    const std::vector<std::string>& names() const { return _names; }

    std::vector<uint8_t> read(const std::string& name) const
    {
        if (_zip)
            return _zip->readFile(name);
        std::ifstream in(fs::path(_folder) / fs::u8path(name), std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

private:
    std::string _folder;
    std::unique_ptr<ZipReader> _zip;
    std::vector<std::string> _names;
};

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// A path inside the package or the Configuration folder, with "/"
/// between its parts; empty when it would climb out ("..") or is absolute.
std::string safePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0)
        path.erase(0, 2);
    if (path.empty() || path[0] == '/' || path.find(':') != std::string::npos)
        return std::string();
    std::stringstream parts(path);
    std::string part;
    std::string out;
    while (std::getline(parts, part, '/'))
    {
        if (part.empty() || part == ".")
            continue;
        if (part == "..")
            return std::string();
        out += (out.empty() ? "" : "/") + part;
    }
    return out;
}

/// The name of \a wanted in the package, found whatever its case.
std::string findName(const Source& source, const std::string& wanted)
{
    const std::string key = lower(wanted);
    for (const std::string& name : source.names())
        if (lower(name) == key)
            return name;
    return std::string();
}

std::string text(const tinyxml2::XMLElement* element)
{
    return element && element->GetText() ? element->GetText() : std::string();
}

std::string attribute(const tinyxml2::XMLElement* element, const char* name)
{
    const char* value = element ? element->Attribute(name) : nullptr;
    return value ? value : std::string();
}

/// Where an .mxi destination goes: "$flash/Commands" is "Commands" under
/// the Configuration folder; false for anywhere outside it.
bool configDestination(const std::string& destination, std::string& out)
{
    std::string d = destination;
    std::replace(d.begin(), d.end(), '\\', '/');
    const std::string key = lower(d);
    for (const char* root : {"$flash", "$userflash", "$animate", "$useranimate"})
    {
        const std::string r = root;
        if (key.compare(0, r.size(), r) == 0 && (key.size() == r.size() || key[r.size()] == '/'))
        {
            const std::string rest = d.substr(r.size());
            out = rest.empty() ? std::string() : safePath(rest.substr(1));
            return rest.empty() || !out.empty();
        }
    }
    return false;
}

bool readMxi(const Source& source, const std::string& name, Package& package, std::string& error)
{
    const std::vector<uint8_t> bytes = source.read(name);
    tinyxml2::XMLDocument doc;
    if (doc.Parse(reinterpret_cast<const char*>(bytes.data()), bytes.size()) != tinyxml2::XML_SUCCESS)
    {
        error = "The package's " + name + " could not be read: " + (doc.ErrorStr() ? doc.ErrorStr() : "");
        return false;
    }
    const tinyxml2::XMLElement* root = doc.RootElement();
    package.kind = Package::Kind::Files;
    package.name = attribute(root, "name");
    package.version = attribute(root, "version");
    package.id = attribute(root, "id");
    if (package.id.empty())
        package.id = package.name;
    package.author = attribute(root ? root->FirstChildElement("author") : nullptr, "name");
    package.description = text(root ? root->FirstChildElement("description") : nullptr);
    // Sources are relative to the .mxi.
    const std::string base = name.find('/') == std::string::npos ? std::string() : name.substr(0, name.rfind('/') + 1);
    const tinyxml2::XMLElement* files = root ? root->FirstChildElement("files") : nullptr;
    for (const tinyxml2::XMLElement* file = files ? files->FirstChildElement("file") : nullptr; file;
         file = file->NextSiblingElement("file"))
    {
        Package::File f;
        f.source = findName(source, safePath(base + attribute(file, "source")));
        if (f.source.empty())
            continue;
        f.placed = configDestination(attribute(file, "destination"), f.destination);
        package.files.push_back(f);
    }
    return true;
}

bool readManifest(const Source& source, const std::string& name, Package& package, std::string& error)
{
    const std::vector<uint8_t> bytes = source.read(name);
    tinyxml2::XMLDocument doc;
    if (doc.Parse(reinterpret_cast<const char*>(bytes.data()), bytes.size()) != tinyxml2::XML_SUCCESS)
    {
        error = "The package's manifest could not be read: " + std::string(doc.ErrorStr() ? doc.ErrorStr() : "");
        return false;
    }
    const tinyxml2::XMLElement* root = doc.RootElement();
    package.kind = Package::Kind::Panels;
    package.id = attribute(root, "ExtensionBundleId");
    package.version = attribute(root, "ExtensionBundleVersion");
    package.name = attribute(root, "ExtensionBundleName");
    if (package.name.empty())
        package.name = package.id;
    // Paths in the manifest are relative to the folder holding CSXS.
    const size_t csxs = lower(name).rfind("csxs/");
    const std::string base = csxs == std::string::npos ? std::string() : name.substr(0, csxs);
    const tinyxml2::XMLElement* list = root ? root->FirstChildElement("DispatchInfoList") : nullptr;
    for (const tinyxml2::XMLElement* extension = list ? list->FirstChildElement("Extension") : nullptr; extension;
         extension = extension->NextSiblingElement("Extension"))
    {
        Package::Panel panel;
        panel.id = attribute(extension, "Id");
        const tinyxml2::XMLElement* info = extension->FirstChildElement("DispatchInfo");
        const tinyxml2::XMLElement* resources = info ? info->FirstChildElement("Resources") : nullptr;
        const tinyxml2::XMLElement* ui = info ? info->FirstChildElement("UI") : nullptr;
        panel.mainPath = safePath(base + text(resources ? resources->FirstChildElement("MainPath") : nullptr));
        panel.scriptPath = safePath(base + text(resources ? resources->FirstChildElement("ScriptPath") : nullptr));
        panel.menu = text(ui ? ui->FirstChildElement("Menu") : nullptr);
        if (panel.menu.empty())
            panel.menu = panel.id;
        const std::string type = text(ui ? ui->FirstChildElement("Type") : nullptr);
        if (!type.empty())
            panel.type = type;
        const tinyxml2::XMLElement* geometry = ui ? ui->FirstChildElement("Geometry") : nullptr;
        const auto size = [geometry](const char* name, int& width, int& height) {
            const tinyxml2::XMLElement* e = geometry ? geometry->FirstChildElement(name) : nullptr;
            width = std::atoi(text(e ? e->FirstChildElement("Width") : nullptr).c_str());
            height = std::atoi(text(e ? e->FirstChildElement("Height") : nullptr).c_str());
        };
        size("Size", panel.width, panel.height);
        size("MinSize", panel.minWidth, panel.minHeight);
        size("MaxSize", panel.maxWidth, panel.maxHeight);
        const tinyxml2::XMLElement* icons = ui ? ui->FirstChildElement("Icons") : nullptr;
        for (const tinyxml2::XMLElement* icon = icons ? icons->FirstChildElement("Icon") : nullptr; icon;
             icon = icon->NextSiblingElement("Icon"))
        {
            const std::string kind = attribute(icon, "Type");
            if (panel.icon.empty() || kind == "Normal")
                panel.icon = safePath(base + text(icon));
        }
        const tinyxml2::XMLElement* cef = resources ? resources->FirstChildElement("CEFCommandLine") : nullptr;
        for (const tinyxml2::XMLElement* parameter = cef ? cef->FirstChildElement("Parameter") : nullptr; parameter;
             parameter = parameter->NextSiblingElement("Parameter"))
        {
            const std::string value = text(parameter);
            if (value == "--enable-nodejs" || value == "--mixed-context")
                panel.nodeJs = true;
        }
        const tinyxml2::XMLElement* lifecycle = info ? info->FirstChildElement("Lifecycle") : nullptr;
        const std::string autoVisible = text(lifecycle ? lifecycle->FirstChildElement("AutoVisible") : nullptr);
        panel.autoVisible = lower(autoVisible) != "false";
        const tinyxml2::XMLElement* startOn = lifecycle ? lifecycle->FirstChildElement("StartOn") : nullptr;
        for (const tinyxml2::XMLElement* event = startOn ? startOn->FirstChildElement("Event") : nullptr; event;
             event = event->NextSiblingElement("Event"))
        {
            const std::string type = text(event);
            if (!type.empty() && type.find(',') == std::string::npos)
                panel.startOn.push_back(type);
        }
        package.panels.push_back(panel);
    }
    const tinyxml2::XMLElement* environment = root ? root->FirstChildElement("ExecutionEnvironment") : nullptr;
    const tinyxml2::XMLElement* hosts = environment ? environment->FirstChildElement("HostList") : nullptr;
    for (const tinyxml2::XMLElement* host = hosts ? hosts->FirstChildElement("Host") : nullptr; host;
         host = host->NextSiblingElement("Host"))
    {
        const std::string hostName = attribute(host, "Name");
        if (!hostName.empty())
            package.hosts.push_back(hostName);
    }
    for (const std::string& file : source.names())
        package.files.push_back({file, std::string(), false});
    return true;
}

/// FNV-1a of a file's bytes, to tell later whether a placed file is still
/// the one placed.
uint64_t hashOf(const std::vector<uint8_t>& bytes)
{
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t hashOfFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;
    return hashOf(std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
}

bool writeFile(const fs::path& path, const std::vector<uint8_t>& bytes)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/// A folder name for an id: letters, digits, dots, dashes and underscores.
std::string folderName(const std::string& id)
{
    std::string out;
    for (char c : id)
        out += std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' ? c : '_';
    // Dots alone ("." or "..") would name the folder itself or its parent.
    return out.find_first_not_of('.') == std::string::npos ? std::string("extension") : out;
}

/// A field of the record: tabs and line ends (and the other control
/// characters) would start a new field or line, so they become spaces.
std::string escapeLine(const std::string& value)
{
    std::string out;
    for (char c : value)
        out += static_cast<unsigned char>(c) < 0x20 || c == 0x7F ? ' ' : c;
    return out;
}

/// A list of names in one field of the record, separated by commas.
std::string joined(const std::vector<std::string>& names)
{
    std::string out;
    for (const std::string& name : names)
        out += (out.empty() ? "" : ",") + name;
    return out;
}

void writeRecord(const Installed& installed, const std::map<std::string, uint64_t>& hashes)
{
    std::ofstream out(fs::u8path(installed.folder) / kRecord, std::ios::binary | std::ios::trunc);
    const Package& p = installed.package;
    out << "kind\t" << (p.kind == Package::Kind::Panels ? "panels" : "files") << "\n";
    out << "id\t" << escapeLine(p.id) << "\n";
    out << "name\t" << escapeLine(p.name) << "\n";
    out << "version\t" << escapeLine(p.version) << "\n";
    out << "author\t" << escapeLine(p.author) << "\n";
    out << "description\t" << escapeLine(p.description) << "\n";
    for (const Package::Panel& panel : p.panels)
        out << "panel\t" << escapeLine(panel.id) << "\t" << escapeLine(panel.menu) << "\t" << escapeLine(panel.mainPath)
            << "\t" << escapeLine(panel.scriptPath) << "\t" << escapeLine(panel.type) << "\t" << panel.width << "\t"
            << panel.height << "\t" << panel.minWidth << "\t" << panel.minHeight << "\t" << panel.maxWidth << "\t"
            << panel.maxHeight << "\t" << escapeLine(panel.icon) << "\t" << (panel.nodeJs ? 1 : 0) << "\t"
            << (panel.autoVisible ? 1 : 0) << "\t" << escapeLine(joined(panel.startOn)) << "\n";
    for (const std::string& host : p.hosts)
        out << "host\t" << escapeLine(host) << "\n";
    for (const std::string& placed : installed.placed)
    {
        const auto found = hashes.find(placed);
        out << "placed\t" << escapeLine(placed) << "\t" << (found != hashes.end() ? found->second : 0) << "\n";
    }
}

bool readRecord(const fs::path& folder, Installed& installed, std::map<std::string, uint64_t>* hashes)
{
    std::ifstream in(folder / kRecord, std::ios::binary);
    if (!in)
        return false;
    installed.folder = folder.u8string();
    Package& p = installed.package;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> fields;
        std::stringstream parts(line);
        std::string field;
        while (std::getline(parts, field, '\t'))
            fields.push_back(field);
        if (fields.empty())
            continue;
        const auto at = [&fields](size_t i) { return i < fields.size() ? fields[i] : std::string(); };
        const std::string& key = fields[0];
        if (key == "kind")
            p.kind = at(1) == "panels" ? Package::Kind::Panels : Package::Kind::Files;
        else if (key == "id")
            p.id = at(1);
        else if (key == "name")
            p.name = at(1);
        else if (key == "version")
            p.version = at(1);
        else if (key == "author")
            p.author = at(1);
        else if (key == "description")
            p.description = at(1);
        else if (key == "panel")
        {
            Package::Panel panel;
            panel.id = at(1);
            panel.menu = at(2);
            panel.mainPath = at(3);
            panel.scriptPath = at(4);
            if (!at(5).empty())
                panel.type = at(5);
            const auto number = [&at](size_t i) { return std::atoi(at(i).c_str()); };
            panel.width = number(6);
            panel.height = number(7);
            panel.minWidth = number(8);
            panel.minHeight = number(9);
            panel.maxWidth = number(10);
            panel.maxHeight = number(11);
            panel.icon = at(12);
            panel.nodeJs = at(13) == "1";
            panel.autoVisible = at(14) != "0";
            std::stringstream events(at(15));
            std::string event;
            while (std::getline(events, event, ','))
            {
                if (!event.empty())
                    panel.startOn.push_back(event);
            }
            p.panels.push_back(panel);
        }
        else if (key == "host" && !at(1).empty())
            p.hosts.push_back(at(1));
        else if (key == "placed" && !at(1).empty())
        {
            installed.placed.push_back(at(1));
            if (hashes)
                (*hashes)[at(1)] = std::strtoull(at(2).c_str(), nullptr, 10);
        }
    }
    return true;
}

} // namespace

bool read(const std::string& path, Package& package, std::string& error)
{
    Source source;
    if (!source.open(path, error))
        return false;
    // An .mxi (the shortest path, in case a package carries several), or a
    // CSXS manifest.
    std::string mxi;
    std::string manifest;
    for (const std::string& name : source.names())
    {
        const std::string key = lower(name);
        if (key.size() > 4 && key.compare(key.size() - 4, 4, ".mxi") == 0 && (mxi.empty() || name.size() < mxi.size()))
            mxi = name;
        if ((key == "csxs/manifest.xml" || (key.size() > 18 && key.compare(key.size() - 18, 18, "/csxs/manifest.xml") == 0)) &&
            (manifest.empty() || name.size() < manifest.size()))
            manifest = name;
    }
    if (!mxi.empty())
        return readMxi(source, mxi, package, error);
    if (!manifest.empty())
        return readManifest(source, manifest, package, error);
    error = "This package has neither an .mxi file nor a CSXS/manifest.xml, so Phoenix cannot tell what it installs.";
    return false;
}

bool install(const std::string& path, const std::string& configFolder, const std::string& extensionsFolder,
    Installed& installed, std::vector<std::string>& notes, std::string& error)
{
    Package package;
    if (!read(path, package, error))
        return false;
    Source source;
    if (!source.open(path, error))
        return false;
    if (package.id.empty())
    {
        error = "This package does not say what it is called.";
        return false;
    }
    if (package.id.find_first_not_of('.') == std::string::npos)
    {
        error = "This package's name is only dots, which Phoenix cannot make a folder of.";
        return false;
    }
    // One of the same id is replaced.
    for (const Installed& old : list(extensionsFolder))
    {
        if (old.package.id == package.id && !remove(old, configFolder, error))
            return false;
    }
    installed = Installed();
    installed.package = package;
    const fs::path folder = fs::u8path(extensionsFolder) / folderName(package.id);
    installed.folder = folder.u8string();
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec)
    {
        error = "The extension's folder could not be made: " + ec.message();
        return false;
    }
    std::map<std::string, uint64_t> hashes;
    bool flashPanels = false;
    bool otherMovies = false;
    bool elsewhere = false;
    for (const Package::File& file : package.files)
    {
        const std::string inside = safePath(file.source);
        if (inside.empty() || lower(inside).rfind("meta-inf/", 0) == 0)
            continue;
        const std::vector<uint8_t> bytes = source.read(file.source);
        // Every file is kept with the extension, as the package has it.
        if (!writeFile(folder / "package" / fs::u8path(inside), bytes))
        {
            error = "Could not write " + inside + " into the extension's folder.";
            return false;
        }
        if (package.kind != Package::Kind::Files)
            continue;
        if (!file.placed)
        {
            elsewhere = true;
            continue;
        }
        const std::string leaf = inside.substr(inside.rfind('/') == std::string::npos ? 0 : inside.rfind('/') + 1);
        const std::string placed = file.destination.empty() ? leaf : file.destination + "/" + leaf;
        if (!writeFile(fs::u8path(configFolder) / fs::u8path(placed), bytes))
        {
            error = "Could not write " + placed + " into the Configuration folder.";
            return false;
        }
        installed.placed.push_back(placed);
        hashes[placed] = hashOf(bytes);
        // Movies in WindowSWF are panels Phoenix plays; others (effects) are
        // kept as they are.
        if (lower(leaf).size() > 4 && lower(leaf).compare(leaf.size() - 4, 4, ".swf") == 0)
        {
            if (lower(placed).rfind("windowswf/", 0) == 0)
                flashPanels = true;
            else
                otherMovies = true;
        }
    }
    writeRecord(installed, hashes);
    if (package.kind == Package::Kind::Panels)
    {
        bool forAnimate = package.hosts.empty();
        for (const std::string& host : package.hosts)
            forAnimate = forAnimate || host == "FLPR";
        if (!forAnimate)
            notes.push_back("Its manifest names other applications, not Animate (FLPR); its panels may expect "
                            "scripts Phoenix does not run.");
        bool nodeJs = false;
        for (const Package::Panel& panel : package.panels)
            nodeJs = nodeJs || panel.nodeJs;
        if (nodeJs)
            notes.push_back("Its panels ask for Node.js: Phoenix gives them Node's modules for files, processes, "
                            "events and the web, but not native add-ons, servers or sockets.");
    }
    if (flashPanels)
        notes.push_back("Its SWF panels are under Window > Extensions; their buttons run JSFL as they would "
                        "anywhere.");
    if (otherMovies)
        notes.push_back("Its other SWF movies (effects) are kept, but Phoenix does not run them; its JSFL commands "
                        "and tools work.");
    if (elsewhere)
        notes.push_back("Some of its files were meant for places outside the Configuration folder; they are kept "
                        "with the extension.");
    return true;
}

std::vector<Installed> list(const std::string& extensionsFolder)
{
    std::vector<Installed> found;
    std::error_code ec;
    for (auto it = fs::directory_iterator(fs::u8path(extensionsFolder), ec); !ec && it != fs::directory_iterator();
         it.increment(ec))
    {
        Installed installed;
        if (it->is_directory(ec) && readRecord(it->path(), installed, nullptr))
            found.push_back(installed);
    }
    std::sort(found.begin(), found.end(),
        [](const Installed& a, const Installed& b) { return lower(a.package.name) < lower(b.package.name); });
    return found;
}

bool remove(const Installed& installed, const std::string& configFolder, std::string& error)
{
    Installed recorded;
    std::map<std::string, uint64_t> hashes;
    readRecord(fs::u8path(installed.folder), recorded, &hashes);
    std::error_code ec;
    for (const std::string& placed : recorded.placed)
    {
        const std::string inside = safePath(placed);
        if (inside.empty())
            continue;
        const fs::path file = fs::u8path(configFolder) / fs::u8path(inside);
        // A file someone has changed since is theirs now, and stays.
        const auto hash = hashes.find(placed);
        if (hash != hashes.end() && hash->second != 0 && hashOfFile(file) != hash->second)
            continue;
        fs::remove(file, ec);
    }
    fs::remove_all(fs::u8path(installed.folder), ec);
    if (ec)
    {
        error = "The extension's folder could not be removed: " + ec.message();
        return false;
    }
    return true;
}

} // namespace extensions
