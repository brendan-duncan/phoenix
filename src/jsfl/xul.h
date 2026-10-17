#pragma once

#include "object.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/// XUL dialogs, as scripts describe them for fl.xmlPanel: the XML read into
/// a tree, the controls found in it with the values they hold, and what a
/// dialog does when a script or the person changes them. Free of Qt; the
/// window builds widgets from it (src/gui/xul_dialog.cpp).
namespace jsfl::xul {

/// An element of the XML: its tag, attributes, text and children.
struct Node
{
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attributes;
    /// The character data directly inside (a script, a description).
    std::string text;
    std::vector<Node> children;

    /// The attribute's value, or null when it is not there.
    const std::string* find(const char* name) const;
    std::string attr(const char* name, const std::string& fallback = std::string()) const;
    /// Whether the attribute says "true".
    bool flag(const char* name) const;
    /// Lower-case tag.
    bool is(const char* name) const { return tag == name; }
};

/// Reads XML as scripts write it: quotes of either kind, entities that are
/// not escaped, a script's code taken as is, close tags that do not match
/// forgiven. The elements land under \a root (a node with no tag). False
/// with \a error when there is no element at all.
bool parse(const std::string& text, Node& root, std::string& error);

/// An entry of a menu list, list box or radio group.
struct Item
{
    std::string label;
    std::string value;
    /// The node it came from, or null when a script made it.
    const Node* node = nullptr;
};

enum class Kind
{
    TextBox,
    CheckBox,
    RadioGroup,
    MenuList,
    ListBox,
    ColorChip,
    Slider,
    ChooseFile,
    TargetList,
    /// A value with no widget (<property id="..." value="..."/>).
    Property,
    /// Controls with no value of their own; an id lets scripts show, hide,
    /// enable or disable them.
    Button,
    Label
};

struct Control
{
    Kind kind = Kind::TextBox;
    std::string id;
    const Node* node = nullptr;
    /// What a text box, editable menu list, color chip, slider, file chooser
    /// or property holds.
    std::string text;
    bool checked = false;
    std::vector<Item> items;
    /// The chosen item, or -1.
    int selected = -1;
    bool enabled = true;
    bool visible = true;

    /// The value a script reads: checkboxes "true" or "false", lists their
    /// chosen item's value, the rest their text.
    std::string value() const;
    /// Whether the dialog's result carries the control.
    bool hasValue() const { return kind != Kind::Button && kind != Kind::Label; }
};

/// One dialog: what the XML asks for and the values of its controls now.
///
/// Scripts change it through fl.xmlui (get, set, setEnabled...); the
/// window changes it as the person edits (edit, toggle, choose, click) and
/// hears of a script's changes through changed and closed. Handler code in
/// the XML (oncreate, onchange, oncommand, script blocks) goes to run.
class Dialog
{
public:
    /// Reads the XML; false with \a error when it holds no dialog.
    bool load(const std::string& xml, std::string& error);

    /// The <dialog> element.
    const Node& root() const { return *_root; }
    std::string title;
    /// The folder the XML was read from, for the files it names (an
    /// embedded SWF panel); empty for XML from a string.
    std::string folder;
    /// The buttons along the bottom, in order: "accept", "cancel".
    std::vector<std::string> buttons;

    const std::vector<std::unique_ptr<Control>>& controls() const { return _controls; }
    /// The control with the id, or null.
    Control* find(const std::string& id) const;
    /// The control made from \a node, or null.
    Control* control(const Node* node) const;

    // Handlers.
    /// Runs handler code in the script's context; set by whoever shows the
    /// dialog.
    std::function<void(const std::string& code)> run;
    /// Runs the script blocks, then each oncreate in order. Once, before
    /// the dialog is shown.
    void create();
    /// Runs \a node's handler for \a event ("oncommand"), if it has one.
    void handle(const Node& node, const char* event);

    // The person, through the window.
    void edit(Control& control, const std::string& text);
    void toggle(Control& control, bool checked);
    void choose(Control& control, int index);
    /// A button pressed.
    void click(const Node& node);

    // Scripts, through fl.xmlui.
    std::string get(const std::string& id) const;
    void set(const std::string& id, const Value& value);
    /// The control with the id; an error naming it when there is none.
    Control& require(const std::string& id) const;
    void setEnabled(const std::string& id, bool enabled);
    void setVisible(const std::string& id, bool visible);
    void setItems(const std::string& id, const std::vector<Item>& items);
    /// The chosen item changed: its label and value.
    void setItem(const std::string& id, const Item& item);

    /// Closes the dialog with "accept" or "cancel" (from a button, the
    /// window closing or fl.xmlui).
    void close(const std::string& dismiss);
    bool closed() const { return !_dismiss.empty(); }
    const std::string& dismiss() const { return _dismiss; }

    /// What the script gets back: dismiss, and each control's value by id.
    Value result() const;

    /// A script changed \a control (its value, items, enabled or visible).
    std::function<void(Control& control)> changed;
    /// A script or a handler closed the dialog.
    std::function<void()> closedByScript;

private:
    void collect(const Node& node);
    void notify(Control& control);

    std::unique_ptr<Node> _document;
    const Node* _root = nullptr;
    std::vector<std::unique_ptr<Control>> _controls;
    std::vector<const Node*> _scripts;
    std::string _dismiss;
    bool _closing = false;
};

/// The kinds of element the dialog builder lays out; the rest it skips.
bool isContainer(const Node& node);

} // namespace jsfl::xul

namespace jsfl {

class Session;

/// fl.xmlPanelFromString: shows the dialog and returns its result. With no
/// one to ask (the command line, an offscreen window), it is accepted
/// with each control's default value and a note traced.
Value showXmlPanel(Session& session, const std::string& xml, const std::string& folder = std::string());
/// fl.xmlPanel and document.xmlPanel: the XML read from a file:/// URI.
Value showXmlPanelFile(Session& session, const std::string& uri);
/// fl.xmlui.
ObjectPtr makeXmlUI(Session& session);

} // namespace jsfl
