#pragma once

#include <string>
#include <vector>

/// Extension packages: a ZXP (or the same files in a folder)
/// read, installed into Phoenix's Configuration folder and removed again.
/// Two kinds are understood: the packages whose .mxi file lists each file
/// and the Configuration folder it goes to (commands, tools, panels), and
/// the HTML panels whose CSXS/manifest.xml describes them. Free of Qt.
namespace extensions {

struct Package
{
    enum class Kind
    {
        /// Files placed by an .mxi list.
        Files,
        /// HTML panels (CSXS/manifest.xml).
        Panels
    };
    Kind kind = Kind::Files;
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;

    /// A file the .mxi places: where it is in the package, and the
    /// Configuration folder it goes to ("Commands", "Tools"; empty for the
    /// Configuration folder itself). Files bound for anywhere else are
    /// kept with the extension only.
    struct File
    {
        std::string source;
        std::string destination;
        bool placed = true;
    };
    std::vector<File> files;

    /// An HTML panel: its id, its name in the menu, its page and the
    /// script it runs in the host, how it shows ("Panel", "Modeless",
    /// "ModalDialog" or "Custom"), its size in pixels (0 when the manifest
    /// gives none), its icon, whether it asks for Node.js, whether it shows
    /// when started, and the events that start it without being opened
    /// ("com.adobe.csxs.events.ApplicationActivate" at launch).
    struct Panel
    {
        std::string id;
        std::string menu;
        std::string mainPath;
        std::string scriptPath;
        std::string type = "Panel";
        int width = 0, height = 0;
        int minWidth = 0, minHeight = 0;
        int maxWidth = 0, maxHeight = 0;
        std::string icon;
        bool nodeJs = false;
        bool autoVisible = true;
        std::vector<std::string> startOn;
    };
    std::vector<Panel> panels;
    /// The applications the panels were made for ("FLPR" is Animate);
    /// empty when the manifest names none.
    std::vector<std::string> hosts;
};

/// Reads the package at \a path: a ZXP (a zip), or a folder.
bool read(const std::string& path, Package& package, std::string& error);

/// An installed extension: its package, its own folder (under the
/// extensions folder), and the files it put in the Configuration folder,
/// relative to it.
struct Installed
{
    Package package;
    std::string folder;
    std::vector<std::string> placed;
};

/// Installs the package at \a path: its files kept in a folder of its own
/// under \a extensionsFolder, and those its list places copied into \a
/// configFolder. An extension of the same id is replaced. \a notes gets
/// what will not work in Phoenix (HTML panels, Flash panels).
bool install(const std::string& path, const std::string& configFolder, const std::string& extensionsFolder,
    Installed& installed, std::vector<std::string>& notes, std::string& error);

/// The extensions installed under \a extensionsFolder, by name.
std::vector<Installed> list(const std::string& extensionsFolder);

/// Removes an installed extension: the files it placed (where they are
/// still the ones it placed) and its own folder.
bool remove(const Installed& installed, const std::string& configFolder, std::string& error);

} // namespace extensions
