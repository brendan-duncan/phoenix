#include "shortcut_sets.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace fla::shortcutsets {

namespace {

/// Animate's names for commands Phoenix names otherwise, normalized.
const std::map<std::string, std::string>& aliases()
{
    static const std::map<std::string, std::string> table = [] {
        const std::pair<const char*, const char*> pairs[] = {
            {"Tools > Fluid Brush", "Tools > Flow Brush"},
            {"Tools > Classic Brush", "Tools > Nib Brush"},
            {"Tools > Brush", "Tools > Nib Brush"},
            {"View > Magnification > Show Frame", "View > Magnification > Fit in Window"},
            {"View > Grid > Show Grid", "View > Show Grid"},
            {"View > Grid > Edit Grid", "View > Edit Grid"},
            {"View > Snapping > Snap to Grid", "View > Snap to Grid"},
            {"View > Snapping > Snap to Guides", "View > Guides > Snap to Guides"},
            {"View > Snapping > Snap to Objects", "View > Snap to Objects"},
            {"View > Snapping > Snap to Pixels", "View > Snap to Pixels"},
            {"Control > Mute Sounds", "View > Mute Sounds"},
            {"Control > Loop Playback", "Control > Loop Playback"},
            {"Control > Go to End", "Control > Go To End"},
            {"Control > Step Forward", "Control > Step Forward One Frame"},
            {"Control > Step Backward", "Control > Step Backward One Frame"},
            {"Modify > Timeline > Convert to Keyframes", "Insert > Timeline > Keyframe"},
            {"Modify > Timeline > Convert to Blank Keyframes", "Insert > Timeline > Blank Keyframe"},
            {"Modify > Symbol > Swap Symbol", "Modify > Symbol > Swap Symbol"},
            {"Insert > Timeline > Layer Folder", "Insert > Timeline > Layer Folder"},
            {"Edit > Paste", "Edit > Paste in Center"},
            {"File > Import > Import to Library", "File > Import > Import to Library"},
            {"Window > Scene", "Window > Scene"},
        };
        std::map<std::string, std::string> out;
        for (const auto& [from, to] : pairs)
            out[normalized(from)] = normalized(to);
        return out;
    }();
    return table;
}

std::string trimmed(std::string s)
{
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// The last part of a normalized path.
std::string lastPart(const std::string& path)
{
    const size_t at = path.rfind('>');
    return at == std::string::npos ? path : path.substr(at + 1);
}

/// Whether \a text reads as a key: modifiers and a key, or a lone key.
bool looksLikeKey(const std::string& text)
{
    const std::string key = trimmed(text);
    if (key.empty() || key.size() > 32)
        return false;
    const std::string low = lower(key);
    for (const char* modifier : {"ctrl", "cmd", "command", "control", "alt", "option", "shift", "meta"})
        if (low.find(modifier) != std::string::npos)
            return true;
    if (key.size() == 1)
        return true;
    if (low.size() >= 2 && low[0] == 'f' && std::isdigit(static_cast<unsigned char>(low[1])))
        return true;
    for (const char* named : {"enter", "return", "backspace", "delete", "del", "home", "end", "pageup", "page up",
             "pagedown", "page down", "esc", "escape", "tab", "space", "up", "down", "left", "right", "insert"})
        if (low == named)
            return true;
    return false;
}

} // namespace

const std::vector<Binding>& animateStandard()
{
    static const std::vector<Binding> bindings = {
        // File.
        {"File > New", "Ctrl+N"}, {"File > Open", "Ctrl+O"}, {"File > Close", "Ctrl+W"},
        {"File > Close All", "Ctrl+Alt+W"}, {"File > Save", "Ctrl+S"}, {"File > Save As", "Ctrl+Shift+S"},
        {"File > Import > Import to Stage", "Ctrl+R"}, {"File > Import > Open External Library", "Ctrl+Shift+O"},
        {"File > Export > Export Movie", "Ctrl+Alt+Shift+S"}, {"File > Publish Settings", "Ctrl+Shift+F12"},
        {"File > Publish", "Shift+F12"}, {"File > Exit", "Ctrl+Q"},
        // Edit.
        {"Edit > Undo", "Ctrl+Z"}, {"Edit > Redo", "Ctrl+Y"}, {"Edit > Cut", "Ctrl+X"}, {"Edit > Copy", "Ctrl+C"},
        {"Edit > Paste in Center", "Ctrl+V"}, {"Edit > Paste in Place", "Ctrl+Shift+V"},
        {"Edit > Clear", "Backspace"}, {"Edit > Duplicate", "Ctrl+D"}, {"Edit > Select All", "Ctrl+A"},
        {"Edit > Deselect All", "Ctrl+Shift+A"}, {"Edit > Find and Replace", "Ctrl+F"}, {"Edit > Find Next", "F3"},
        {"Edit > Timeline > Remove Frames", "Shift+F5"}, {"Edit > Timeline > Cut Frames", "Ctrl+Alt+X"},
        {"Edit > Timeline > Copy Frames", "Ctrl+Alt+C"}, {"Edit > Timeline > Paste Frames", "Ctrl+Alt+V"},
        {"Edit > Timeline > Clear Frames", "Alt+Backspace"}, {"Edit > Timeline > Select All Frames", "Ctrl+Alt+A"},
        {"Edit > Timeline > Clear Keyframe", "Shift+F6"}, {"Edit > Edit Symbols", "Ctrl+E"},
        {"Edit > Preferences", "Ctrl+U"}, {"Edit > Keyboard Shortcuts", "Ctrl+Alt+Shift+K"},
        // View.
        {"View > Zoom In", "Ctrl+="}, {"View > Zoom Out", "Ctrl+-"}, {"View > Magnification > 100%", "Ctrl+1"},
        {"View > Magnification > Show Frame", "Ctrl+2"}, {"View > Magnification > Show All", "Ctrl+3"},
        {"View > Magnification > 400%", "Ctrl+4"}, {"View > Magnification > 800%", "Ctrl+8"},
        {"View > Pasteboard", "Ctrl+Shift+W"}, {"View > Rulers", "Ctrl+Alt+Shift+R"},
        {"View > Grid > Show Grid", "Ctrl+'"}, {"View > Grid > Edit Grid", "Ctrl+Alt+G"},
        {"View > Guides > Show Guides", "Ctrl+;"}, {"View > Guides > Lock Guides", "Ctrl+Alt+;"},
        {"View > Guides > Edit Guides", "Ctrl+Alt+Shift+G"}, {"View > Snapping > Snap to Grid", "Ctrl+Shift+'"},
        {"View > Snapping > Snap to Guides", "Ctrl+Shift+;"}, {"View > Snapping > Snap to Objects", "Ctrl+Shift+/"},
        {"View > Show Shape Hints", "Ctrl+Alt+H"}, {"View > Hide Edges", "Ctrl+H"},
        // Insert.
        {"Insert > New Symbol", "Ctrl+F8"}, {"Insert > Timeline > Frame", "F5"},
        {"Insert > Timeline > Keyframe", "F6"}, {"Insert > Timeline > Blank Keyframe", "F7"},
        // Modify.
        {"Modify > Document", "Ctrl+J"}, {"Modify > Convert to Symbol", "F8"}, {"Modify > Break Apart", "Ctrl+B"},
        {"Modify > Shape > Advanced Smooth", "Ctrl+Alt+Shift+M"}, {"Modify > Shape > Advanced Straighten", "Ctrl+Alt+Shift+N"},
        {"Modify > Shape > Optimize", "Ctrl+Alt+Shift+C"}, {"Modify > Shape > Add Shape Hint", "Ctrl+Shift+H"},
        {"Modify > Timeline > Distribute to Layers", "Ctrl+Shift+D"},
        {"Modify > Transform > Scale and Rotate", "Ctrl+Alt+S"}, {"Modify > Transform > Rotate 90° CW", "Ctrl+Shift+9"},
        {"Modify > Transform > Rotate 90° CCW", "Ctrl+Shift+7"}, {"Modify > Transform > Remove Transform", "Ctrl+Shift+Z"},
        {"Modify > Arrange > Bring to Front", "Ctrl+Shift+Up"}, {"Modify > Arrange > Bring Forward", "Ctrl+Up"},
        {"Modify > Arrange > Send Backward", "Ctrl+Down"}, {"Modify > Arrange > Send to Back", "Ctrl+Shift+Down"},
        {"Modify > Arrange > Lock", "Ctrl+Alt+L"}, {"Modify > Arrange > Unlock All", "Ctrl+Alt+Shift+L"},
        {"Modify > Align > Left", "Ctrl+Alt+1"}, {"Modify > Align > Horizontal Center", "Ctrl+Alt+2"},
        {"Modify > Align > Right", "Ctrl+Alt+3"}, {"Modify > Align > Top", "Ctrl+Alt+4"},
        {"Modify > Align > Vertical Center", "Ctrl+Alt+5"}, {"Modify > Align > Bottom", "Ctrl+Alt+6"},
        {"Modify > Align > Make Same Width", "Ctrl+Alt+Shift+7"}, {"Modify > Align > Make Same Height", "Ctrl+Alt+Shift+9"},
        {"Modify > Align > To Stage", "Ctrl+Alt+8"}, {"Modify > Group", "Ctrl+G"}, {"Modify > Ungroup", "Ctrl+Shift+G"},
        // Text.
        {"Text > Style > Bold", "Ctrl+Shift+B"}, {"Text > Style > Italic", "Ctrl+Shift+I"},
        {"Text > Align > Align Left", "Ctrl+Shift+L"}, {"Text > Align > Center", "Ctrl+Shift+C"},
        {"Text > Align > Align Right", "Ctrl+Shift+R"}, {"Text > Align > Justify", "Ctrl+Shift+J"},
        // Control.
        {"Control > Play", "Return"}, {"Control > Rewind", "Shift+,"}, {"Control > Go to End", "Shift+."},
        {"Control > Step Forward One Frame", "."}, {"Control > Step Backward One Frame", ","},
        {"Control > Test Movie > Test", "Ctrl+Return"}, {"Control > Mute Sounds", "Ctrl+Alt+M"},
        // Window.
        {"Window > Duplicate Window", "Ctrl+Alt+K"}, {"Window > Timeline", "Ctrl+Alt+T"}, {"Window > Tools", "Ctrl+F2"},
        {"Window > Properties", "Ctrl+F3"}, {"Window > Library", "Ctrl+L"}, {"Window > Actions", "F9"},
        {"Window > Align", "Ctrl+K"}, {"Window > Color", "Ctrl+Shift+F9"}, {"Window > Info", "Ctrl+I"},
        {"Window > Swatches", "Ctrl+F9"}, {"Window > Transform", "Ctrl+T"}, {"Window > Components", "Ctrl+F7"},
        {"Window > History", "Ctrl+F10"}, {"Window > Movie Explorer", "Alt+F3"}, {"Window > Output", "F2"},
        {"Window > Scene", "Shift+F2"}, {"Window > Hide Panels", "F4"},
        {"Help > Animate Help", "F1"},
        // Tools.
        {"Tools > Selection", "V"}, {"Tools > Subselection", "A"}, {"Tools > Free Transform", "Q"},
        {"Tools > Gradient Transform", "F"}, {"Tools > 3D Rotation", "W"}, {"Tools > 3D Translation", "G"},
        {"Tools > Lasso", "L"}, {"Tools > Pen", "P"}, {"Tools > Add Anchor Point", "="},
        {"Tools > Delete Anchor Point", "-"}, {"Tools > Convert Anchor Point", "Shift+C"}, {"Tools > Text", "T"},
        {"Tools > Line", "N"}, {"Tools > Rectangle", "R"}, {"Tools > Rectangle Primitive", "Shift+R"},
        {"Tools > Oval", "O"}, {"Tools > Oval Primitive", "Shift+O"}, {"Tools > Pencil", "Shift+Y"},
        {"Tools > Paint Brush", "Y"}, {"Tools > Fluid Brush", "B"}, {"Tools > Classic Brush", "Shift+B"},
        {"Tools > Bone", "M"}, {"Tools > Paint Bucket", "K"}, {"Tools > Ink Bottle", "S"},
        {"Tools > Eyedropper", "I"}, {"Tools > Eraser", "E"}, {"Tools > Width", "U"}, {"Tools > Camera", "C"},
        {"Tools > Hand", "H"}, {"Tools > Rotation", "Shift+H"}, {"Tools > Zoom", "Z"},
    };
    return bindings;
}

std::string normalized(const std::string& command)
{
    std::string out;
    std::string part;
    const auto flush = [&]() {
        if (!part.empty())
        {
            if (!out.empty())
                out += '>';
            out += part;
        }
        part.clear();
    };
    std::string text = command;
    // Menus are joined by ">" or "/" or "|"; an ellipsis ends a name.
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '>' || c == '|' || (c == '/' && i + 1 < text.size() && text[i + 1] == ' '))
        {
            flush();
            continue;
        }
        if (c == '&')
            continue;
        if (std::isalnum(c))
            part += static_cast<char>(std::tolower(c));
        else if (c == '%' || c == '+' || c == '-' || c == '=')
            part += static_cast<char>(c);
    }
    flush();
    return out;
}

std::string normalizedKey(const std::string& key)
{
    std::string text = trimmed(key);
    // Split on "+" (or "-" between words), keeping a key that is itself one.
    std::vector<std::string> parts;
    std::string part;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        const bool separator = (c == '+' || c == '-') && !part.empty() && i + 1 < text.size();
        if (separator)
        {
            parts.push_back(trimmed(part));
            part.clear();
        }
        else
            part += c;
    }
    if (!part.empty())
        parts.push_back(trimmed(part));
    bool ctrl = false, alt = false, shift = false, meta = false;
    std::string main;
    for (const std::string& piece : parts)
    {
        const std::string low = lower(piece);
        if (low == "ctrl" || low == "control" || low == "cmd" || low == "command" || low == "\xe2\x8c\x98")
            ctrl = true;
        else if (low == "alt" || low == "option" || low == "opt" || low == "\xe2\x8c\xa5")
            alt = true;
        else if (low == "shift" || low == "\xe2\x87\xa7")
            shift = true;
        else if (low == "meta" || low == "win")
            meta = true;
        else if (low == "enter" || low == "return")
            main = "Return";
        else if (low == "del")
            main = "Delete";
        else if (low == "esc")
            main = "Escape";
        else if (low == "pgup" || low == "page up")
            main = "PgUp";
        else if (low == "pgdn" || low == "page down")
            main = "PgDown";
        else if (piece.size() == 1)
            main = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(piece[0]))));
        else
        {
            main = piece;
            main[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(main[0])));
        }
    }
    std::string out;
    if (ctrl)
        out += "Ctrl+";
    if (alt)
        out += "Alt+";
    if (shift)
        out += "Shift+";
    if (meta)
        out += "Meta+";
    return out + main;
}

std::map<std::string, std::string> match(const std::vector<Binding>& bindings, const std::vector<std::string>& commands,
    std::vector<std::string>* unmatched)
{
    std::map<std::string, std::string> byPath;
    std::map<std::string, std::vector<std::string>> byLast;
    for (const std::string& command : commands)
    {
        const std::string key = normalized(command);
        byPath[key] = command;
        byLast[lastPart(key)].push_back(command);
    }
    std::map<std::string, std::string> keys;
    for (const Binding& binding : bindings)
    {
        const std::string key = normalizedKey(binding.key);
        if (key.empty())
            continue;
        std::string path = normalized(binding.command);
        auto alias = aliases().find(path);
        // A bare name may be a tool's, as a tool list gives them.
        if (alias == aliases().end() && path.find('>') == std::string::npos)
            alias = aliases().find("tools>" + path);
        if (alias != aliases().end())
            path = alias->second;
        auto found = byPath.find(path);
        if (found != byPath.end())
        {
            keys[found->second] = key;
            continue;
        }
        // The same name under another menu, when only one command has it.
        const auto last = byLast.find(lastPart(path));
        if (last != byLast.end() && last->second.size() == 1)
        {
            keys[last->second.front()] = key;
            continue;
        }
        if (unmatched)
            unmatched->push_back(binding.command);
    }
    return keys;
}

namespace {

const char* const kKeyAttributes[] = {"key", "keys", "shortcut", "accelerator", "accel", "keySequence", "keystroke",
    "keystrokes", "hotkey", "binding"};
const char* const kNameAttributes[] = {"menuPath", "path", "command", "name", "label", "title", "text", "action", "id"};

std::string attributeOf(const tinyxml2::XMLElement& element, const char* const* names, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (const char* value = element.Attribute(names[i]))
            if (*value)
                return value;
    return std::string();
}

void walk(const tinyxml2::XMLElement& element, const std::string& menus, std::vector<Binding>& out)
{
    std::string name = attributeOf(element, kNameAttributes, std::size(kNameAttributes));
    std::string key = attributeOf(element, kKeyAttributes, std::size(kKeyAttributes));
    // A key in a child element of its own: <key>Ctrl+N</key>.
    for (const tinyxml2::XMLElement* child = element.FirstChildElement(); child && key.empty();
         child = child->NextSiblingElement())
    {
        const std::string tag = lower(child->Name());
        if ((tag == "key" || tag == "shortcut" || tag == "keys" || tag == "accelerator") && child->GetText())
            key = child->GetText();
    }
    if (!key.empty() && looksLikeKey(key) && !name.empty())
    {
        const std::string command = name.find('>') != std::string::npos || menus.empty() ? name : menus + " > " + name;
        out.push_back({command, key});
    }
    // Children sit under this element's name as a menu.
    const std::string inner = key.empty() && !name.empty() ? (menus.empty() ? name : menus + " > " + name) : menus;
    for (const tinyxml2::XMLElement* child = element.FirstChildElement(); child; child = child->NextSiblingElement())
    {
        const std::string tag = lower(child->Name());
        if (tag == "key" || tag == "shortcut" || tag == "keys" || tag == "accelerator")
            continue;
        walk(*child, inner, out);
    }
}

} // namespace

std::vector<Binding> read(const std::string& text)
{
    std::vector<Binding> out;
    const size_t start = text.find_first_not_of(" \t\r\n\xef\xbb\xbf");
    if (start != std::string::npos && text[start] == '<')
    {
        tinyxml2::XMLDocument document;
        if (document.Parse(text.c_str(), text.size()) == tinyxml2::XML_SUCCESS && document.RootElement())
        {
            // The root names the set, not a menu.
            for (const tinyxml2::XMLElement* child = document.RootElement()->FirstChildElement(); child;
                 child = child->NextSiblingElement())
                walk(*child, std::string(), out);
        }
        return out;
    }
    // Text: a command and its key a line.
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = trimmed(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        std::string command, key;
        for (const char* separator : {"\t", " = ", ",", "  "})
        {
            const size_t split = line.rfind(separator);
            if (split == std::string::npos)
                continue;
            command = trimmed(line.substr(0, split));
            key = trimmed(line.substr(split + std::char_traits<char>::length(separator)));
            if (!command.empty() && looksLikeKey(key))
                break;
            command.clear();
        }
        if (!command.empty())
            out.push_back({command, key});
    }
    return out;
}

} // namespace fla::shortcutsets
