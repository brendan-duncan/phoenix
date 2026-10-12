#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fla {

/// Keyboard shortcut sets from Animate: its standard keys, and shortcut
/// files exported from it, matched to Phoenix's commands. Free of Qt; keys
/// are text ("Ctrl+Shift+S").
namespace shortcutsets {

/// A command as another application names it ("File > Save As...", or
/// just "Save As") and the key it gives it.
struct Binding
{
    std::string command;
    std::string key;
};

/// Animate's standard keys on Windows, in Animate's menu names.
const std::vector<Binding>& animateStandard();

/// A command's name reduced for matching: no ampersands, ellipses, case
/// or punctuation, menus joined by ">".
std::string normalized(const std::string& command);

/// A key as Phoenix writes it: Cmd or Control is Ctrl, Option is Alt,
/// "-" between keys is "+", modifiers in Ctrl, Alt, Shift order.
std::string normalizedKey(const std::string& key);

/// The bindings matched to Phoenix's \a commands (its "Menu > Item" ids):
/// by the whole path, then through Animate's names for Phoenix's commands,
/// then by the last part when only one command has it. What matched none
/// goes into \a unmatched.
std::map<std::string, std::string> match(const std::vector<Binding>& bindings,
    const std::vector<std::string>& commands, std::vector<std::string>* unmatched = nullptr);

/// The bindings in an Animate shortcut file: XML, whatever its elements
/// are called, giving a command (by name, label or menu path, its menus
/// from the elements around it) and a key; or text, a command and its key
/// a line, split by a tab, " = ", "," or two spaces.
std::vector<Binding> read(const std::string& text);

} // namespace shortcutsets

} // namespace fla
