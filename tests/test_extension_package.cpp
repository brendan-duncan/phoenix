#include "test_util.h"

#include "../src/parser/extension_package.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// A fresh folder under the system's temporary folder, removed after.
struct TempFolder
{
    fs::path path;
    TempFolder()
    {
        path = fs::temp_directory_path() /
            ("phoenix-ext-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void put(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string get(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST(extension_packages_install_their_files_and_remove_them)
{
    TempFolder temp;
    const fs::path package = temp.path / "package";
    const fs::path config = temp.path / "Configuration";
    const fs::path installedAt = temp.path / "Extensions";
    put(package / "Align Things.mxi",
        "<?xml version=\"1.0\"?>\n"
        "<macromedia-extension name=\"Align Things\" version=\"1.2\" type=\"Command\">\n"
        "  <author name=\"Someone\" />\n"
        "  <description><![CDATA[Lines things up.]]></description>\n"
        "  <files>\n"
        "    <file source=\"Align Things.jsfl\" destination=\"$flash/Commands\" />\n"
        "    <file source=\"tools\\Nudge.jsfl\" destination=\"$flash/Tools\" />\n"
        "    <file source=\"Panel.swf\" destination=\"$flash/WindowSWF\" />\n"
        "    <file source=\"readme.txt\" destination=\"$ExtensionSpecificEMStore\" />\n"
        "    <file source=\"../outside.jsfl\" destination=\"$flash/Commands\" />\n"
        "  </files>\n"
        "</macromedia-extension>\n");
    put(package / "Align Things.jsfl", "fl.trace('align');");
    put(package / "tools" / "Nudge.jsfl", "function activate() {}");
    put(package / "Panel.swf", "FWS");
    put(package / "readme.txt", "read me");

    extensions::Package read;
    std::string error;
    CHECK(extensions::read(package.string(), read, error));
    CHECK(read.name == "Align Things" && read.version == "1.2" && read.author == "Someone");
    CHECK(read.description == "Lines things up." && read.files.size() == 4);

    extensions::Installed installed;
    std::vector<std::string> notes;
    CHECK(extensions::install(package.string(), config.string(), installedAt.string(), installed, notes, error));
    // Commands and tools where Phoenix looks for them; the SWF panel too,
    // with a note saying where it shows; the readme kept with the extension.
    CHECK(get(config / "Commands" / "Align Things.jsfl") == "fl.trace('align');");
    CHECK(fs::exists(config / "Tools" / "Nudge.jsfl") && fs::exists(config / "WindowSWF" / "Panel.swf"));
    CHECK(!fs::exists(config / "readme.txt") && installed.placed.size() == 3);
    CHECK(notes.size() == 2);
    CHECK(std::any_of(notes.begin(), notes.end(),
        [](const std::string& note) { return note.find("Window > Extensions") != std::string::npos; }));
    // Nothing climbed out of the package.
    CHECK(!fs::exists(config / "Commands" / "outside.jsfl"));

    std::vector<extensions::Installed> all = extensions::list(installedAt.string());
    CHECK(all.size() == 1 && all[0].package.name == "Align Things" && all[0].placed.size() == 3);

    // Installed again: replaced, not doubled.
    CHECK(extensions::install(package.string(), config.string(), installedAt.string(), installed, notes, error));
    CHECK(extensions::list(installedAt.string()).size() == 1);

    // A command changed by hand since is kept when the extension goes.
    put(config / "Tools" / "Nudge.jsfl", "// mine now");
    all = extensions::list(installedAt.string());
    CHECK(extensions::remove(all[0], config.string(), error));
    CHECK(!fs::exists(config / "Commands" / "Align Things.jsfl") && !fs::exists(config / "WindowSWF" / "Panel.swf"));
    CHECK(get(config / "Tools" / "Nudge.jsfl") == "// mine now");
    CHECK(extensions::list(installedAt.string()).empty());
}

TEST(extension_packages_read_html_panels_from_their_manifest)
{
    TempFolder temp;
    const fs::path package = temp.path / "panel";
    put(package / "CSXS" / "manifest.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<ExtensionManifest Version=\"6.0\" ExtensionBundleId=\"com.example.timer\" ExtensionBundleVersion=\"2.0\" "
        "ExtensionBundleName=\"Timer\">\n"
        "  <ExtensionList><Extension Id=\"com.example.timer.panel\" Version=\"2.0\" /></ExtensionList>\n"
        "  <ExecutionEnvironment><HostList><Host Name=\"FLPR\" Version=\"[16.0,99.9]\" /></HostList>"
        "</ExecutionEnvironment>\n"
        "  <DispatchInfoList><Extension Id=\"com.example.timer.panel\"><DispatchInfo>\n"
        "    <Resources><MainPath>./index.html</MainPath><ScriptPath>./host/timer.jsfl</ScriptPath>"
        "<CEFCommandLine><Parameter>--enable-nodejs</Parameter></CEFCommandLine></Resources>\n"
        "    <Lifecycle><AutoVisible>false</AutoVisible><StartOn><Event>com.adobe.csxs.events.ApplicationActivate"
        "</Event></StartOn></Lifecycle>\n"
        "    <UI><Type>Panel</Type><Menu>Shot Timer</Menu><Geometry><Size><Height>300</Height><Width>240</Width>"
        "</Size><MinSize><Height>100</Height><Width>120</Width></MinSize></Geometry>"
        "<Icons><Icon Type=\"Normal\">./icons/timer.png</Icon></Icons></UI>\n"
        "  </DispatchInfo></Extension></DispatchInfoList>\n"
        "</ExtensionManifest>\n");
    put(package / "index.html", "<html></html>");
    put(package / "host" / "timer.jsfl", "");
    extensions::Package read;
    std::string error;
    CHECK(extensions::read(package.string(), read, error));
    CHECK(read.kind == extensions::Package::Kind::Panels && read.id == "com.example.timer" && read.name == "Timer");
    CHECK(read.panels.size() == 1);
    if (!read.panels.empty())
    {
        CHECK(read.panels[0].menu == "Shot Timer" && read.panels[0].mainPath == "index.html");
        CHECK(read.panels[0].scriptPath == "host/timer.jsfl");
        CHECK(read.panels[0].width == 240 && read.panels[0].height == 300);
        CHECK(read.panels[0].minWidth == 120 && read.panels[0].minHeight == 100);
        CHECK(read.panels[0].icon == "icons/timer.png" && !read.panels[0].autoVisible);
        CHECK(read.panels[0].startOn.size() == 1 && read.panels[0].type == "Panel" && read.panels[0].nodeJs);
    }
    CHECK(read.hosts.size() == 1 && read.hosts[0] == "FLPR");
    // Installed: kept whole in its folder, nothing in the Configuration
    // folder, a note on what of Node.js it has, and all of it recorded.
    extensions::Installed installed;
    std::vector<std::string> notes;
    const fs::path config = temp.path / "Configuration";
    CHECK(extensions::install(package.string(), config.string(), (temp.path / "Extensions").string(), installed, notes,
        error));
    CHECK(installed.placed.empty() && notes.size() == 1);
    CHECK(fs::exists(fs::path(installed.folder) / "package" / "index.html"));
    const std::vector<extensions::Installed> listed = extensions::list((temp.path / "Extensions").string());
    CHECK(listed.size() == 1 && listed[0].package.panels.size() == 1 && listed[0].package.hosts.size() == 1);
    if (listed.size() == 1 && listed[0].package.panels.size() == 1)
    {
        const extensions::Package::Panel& kept = listed[0].package.panels[0];
        CHECK(kept.width == 240 && kept.minHeight == 100 && kept.icon == "icons/timer.png" && kept.nodeJs);
        CHECK(!kept.autoVisible && kept.startOn.size() == 1 &&
              kept.startOn[0] == "com.adobe.csxs.events.ApplicationActivate");
    }
    // Not a package at all.
    const fs::path empty = temp.path / "empty";
    fs::create_directories(empty);
    CHECK(!extensions::read(empty.string(), read, error) && !error.empty());
}

TEST(extension_packages_keep_to_their_folder_and_record)
{
    TempFolder temp;
    const fs::path config = temp.path / "Configuration";
    const fs::path extensionsFolder = temp.path / "Extensions";
    const auto manifest = [](const std::string& id, const std::string& mainPath) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<ExtensionManifest Version=\"6.0\" ExtensionBundleId=\"" + id + "\" ExtensionBundleName=\"X\">\n"
               "  <DispatchInfoList><Extension Id=\"panel\"><DispatchInfo>\n"
               "    <Resources><MainPath>" + mainPath + "</MainPath><ScriptPath>./host.jsfl</ScriptPath></Resources>\n"
               "  </DispatchInfo></Extension></DispatchInfoList>\n"
               "</ExtensionManifest>\n";
    };
    // An id of dots alone would install into the extensions' own folder or
    // its parent.
    const fs::path dots = temp.path / "dots";
    put(dots / "CSXS" / "manifest.xml", manifest("..", "./index.html"));
    extensions::Installed installed;
    std::vector<std::string> notes;
    std::string error;
    CHECK(!extensions::install(dots.string(), config.string(), extensionsFolder.string(), installed, notes, error));
    CHECK(!fs::exists(temp.path / "package"));

    // A tab and a line end in a path stay inside its field of the record.
    const fs::path tabbed = temp.path / "tabbed";
    put(tabbed / "CSXS" / "manifest.xml", manifest("com.example.tabbed", "./a&#9;b&#10;placed&#9;x.html"));
    CHECK(extensions::install(tabbed.string(), config.string(), extensionsFolder.string(), installed, notes, error));
    const std::vector<extensions::Installed> all = extensions::list(extensionsFolder.string());
    CHECK(all.size() == 1 && all[0].package.panels.size() == 1 && all[0].placed.empty());
    if (all.size() == 1 && all[0].package.panels.size() == 1)
        CHECK(all[0].package.panels[0].scriptPath == "host.jsfl");
}
