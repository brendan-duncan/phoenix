#include "test_util.h"

#include "../src/edit/shortcut_sets.h"

#include <algorithm>

using fla::shortcutsets::Binding;

namespace {

const std::vector<std::string> kCommands = {"File > Save As", "Edit > Redo", "Edit > Paste in Center",
    "Modify > Transform > Remove Transform", "Modify > Transform > Rotate 90° CW", "View > Show Grid",
    "View > Magnification > Fit in Window", "Tools > Flow Brush", "Tools > Nib Brush", "Tools > Selection",
    "Control > Play"};

std::string keyOf(const std::map<std::string, std::string>& keys, const std::string& command)
{
    const auto found = keys.find(command);
    return found == keys.end() ? std::string() : found->second;
}

} // namespace

TEST(animate_keys_are_written_as_phoenix_writes_them)
{
    using fla::shortcutsets::normalizedKey;
    CHECK(normalizedKey("Cmd+Shift+s") == "Ctrl+Shift+S");
    CHECK(normalizedKey("Shift+Control+S") == "Ctrl+Shift+S");
    CHECK(normalizedKey("Option-F3") == "Alt+F3");
    CHECK(normalizedKey("Ctrl+-") == "Ctrl+-");
    CHECK(normalizedKey("-") == "-");
    CHECK(normalizedKey("Enter") == "Return");
    CHECK(fla::shortcutsets::normalized("&File > Save &As...") == "file>saveas");
}

TEST(animate_standard_matches_phoenix_commands)
{
    std::vector<std::string> unmatched;
    const std::map<std::string, std::string> keys =
        fla::shortcutsets::match(fla::shortcutsets::animateStandard(), kCommands, &unmatched);
    CHECK(keyOf(keys, "Edit > Redo") == "Ctrl+Y");
    CHECK(keyOf(keys, "Modify > Transform > Remove Transform") == "Ctrl+Shift+Z");
    CHECK(keyOf(keys, "Modify > Transform > Rotate 90° CW") == "Ctrl+Shift+9");
    // Animate's names for Phoenix's commands.
    CHECK(keyOf(keys, "Tools > Flow Brush") == "B");
    CHECK(keyOf(keys, "Tools > Nib Brush") == "Shift+B");
    CHECK(keyOf(keys, "View > Magnification > Fit in Window") == "Ctrl+2");
    CHECK(keyOf(keys, "View > Show Grid") == "Ctrl+'");
    CHECK(keyOf(keys, "Control > Play") == "Return");
    // Commands Phoenix lacks are reported, not guessed at.
    CHECK(std::find(unmatched.begin(), unmatched.end(), "Window > Actions") != unmatched.end());
}

TEST(animate_shortcut_files_are_read_as_xml_or_text)
{
    // Menus around commands, keys as attributes or child elements.
    const std::string xml = R"(<?xml version="1.0"?>
<shortcuts name="Studio">
  <menu name="File">
    <command name="Save As..." key="Cmd+Shift+S"/>
    <menu name="Import"><item label="Import to Stage"><key>Ctrl+R</key></item></menu>
  </menu>
  <command menuPath="Edit > Redo" shortcut="Ctrl+Shift+Z"/>
  <tool name="Classic Brush" key="X"/>
</shortcuts>)";
    const std::vector<Binding> read = fla::shortcutsets::read(xml);
    CHECK(read.size() == 4);
    if (read.size() != 4)
        return;
    CHECK(read[0].command == "File > Save As..." && read[0].key == "Cmd+Shift+S");
    CHECK(read[1].command == "File > Import > Import to Stage" && read[1].key == "Ctrl+R");
    CHECK(read[2].command == "Edit > Redo");
    std::vector<std::string> commands = kCommands;
    commands.push_back("File > Import > Import to Stage");
    const std::map<std::string, std::string> keys = fla::shortcutsets::match(read, commands);
    CHECK(keyOf(keys, "File > Save As") == "Ctrl+Shift+S");
    CHECK(keyOf(keys, "File > Import > Import to Stage") == "Ctrl+R");
    CHECK(keyOf(keys, "Edit > Redo") == "Ctrl+Shift+Z");
    // A bare tool name finds the one command with it, through its alias.
    CHECK(keyOf(keys, "Tools > Nib Brush") == "X");

    // Text: a command and its key a line.
    const std::vector<Binding> lines = fla::shortcutsets::read("File > Save As\tCtrl+Shift+S\n# a comment\nEdit/ Redo = Ctrl+Y\nnot a binding\n");
    CHECK(lines.size() == 2);
    if (lines.size() == 2)
        CHECK(fla::shortcutsets::normalized(lines[1].command) == "edit>redo" && lines[1].key == "Ctrl+Y");
}
