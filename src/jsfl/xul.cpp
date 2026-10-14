// XUL dialogs: reading the XML and keeping the controls' values.

#include "xul.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace jsfl::xul {

namespace {

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool nameStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':' || static_cast<unsigned char>(c) >= 0x80;
}

bool nameChar(char c)
{
    return nameStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.';
}

bool blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void appendUtf8(std::string& out, unsigned long code)
{
    if (code < 0x80)
        out += static_cast<char>(code);
    else if (code < 0x800)
    {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x110000)
    {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

/// The text with its entities replaced; an ampersand that starts none is
/// kept as it is, as scripts often leave them unescaped.
std::string decode(const std::string& text)
{
    if (text.find('&') == std::string::npos)
        return text;
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '&')
        {
            out += text[i];
            continue;
        }
        const size_t end = text.find(';', i + 1);
        if (end == std::string::npos || end - i > 10)
        {
            out += '&';
            continue;
        }
        const std::string name = text.substr(i + 1, end - i - 1);
        bool known = true;
        if (name == "lt")
            out += '<';
        else if (name == "gt")
            out += '>';
        else if (name == "amp")
            out += '&';
        else if (name == "quot")
            out += '"';
        else if (name == "apos")
            out += '\'';
        else if (name == "nbsp")
            out += "\xC2\xA0";
        else if (name.size() > 1 && name[0] == '#')
        {
            char* stop = nullptr;
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const char* digits = name.c_str() + (hex ? 2 : 1);
            const unsigned long code = std::strtoul(digits, &stop, hex ? 16 : 10);
            if (stop && !*stop && *digits)
                appendUtf8(out, code);
            else
                known = false;
        }
        else
            known = false;
        if (known)
            i = end;
        else
            out += '&';
    }
    return out;
}

/// The position of the next "</name" (any case) from \a at, or npos.
size_t findClose(const std::string& text, size_t at, const std::string& name)
{
    const std::string wanted = "</" + name;
    for (size_t i = text.find("</", at); i != std::string::npos; i = text.find("</", i + 2))
    {
        if (lower(text.substr(i, wanted.size())) == wanted)
        {
            const size_t after = i + wanted.size();
            if (after >= text.size() || !nameChar(text[after]))
                return i;
        }
    }
    return std::string::npos;
}

/// A script block's code: CDATA markers taken off, entities replaced.
std::string scriptText(const std::string& raw)
{
    std::string code;
    size_t i = 0;
    while (i < raw.size())
    {
        const size_t open = raw.find("<![CDATA[", i);
        if (open == std::string::npos)
        {
            code += decode(raw.substr(i));
            break;
        }
        code += decode(raw.substr(i, open - i));
        const size_t close = raw.find("]]>", open + 9);
        if (close == std::string::npos)
        {
            code += raw.substr(open + 9);
            break;
        }
        code += raw.substr(open + 9, close - open - 9);
        i = close + 3;
    }
    return code;
}

std::string normalColor(std::string text)
{
    text = trim(text);
    if (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0)
        text = "#" + text.substr(2);
    if (!text.empty() && text[0] != '#' && std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        text = "#" + text;
    for (char& c : text)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text.empty() ? std::string("#000000") : text;
}

/// The value of a list entry: its value, or its label without one.
Item itemOf(const Node& node)
{
    Item item;
    item.label = node.attr("label");
    const std::string* value = node.find("value");
    item.value = value ? *value : item.label;
    item.node = &node;
    return item;
}

void gather(const Node& node, const char* tag, std::vector<const Node*>& found)
{
    for (const Node& child : node.children)
    {
        if (child.is(tag))
            found.push_back(&child);
        else
            gather(child, tag, found);
    }
}

/// The items under \a node (radios, menu items, list items), the chosen
/// one marked selected="true", or the one matching \a value.
void fillItems(Control& control, const Node& node, const char* tag, bool firstByDefault)
{
    std::vector<const Node*> found;
    gather(node, tag, found);
    for (const Node* entry : found)
    {
        if (entry->flag("selected") && control.selected < 0)
            control.selected = static_cast<int>(control.items.size());
        control.items.push_back(itemOf(*entry));
    }
    if (control.selected < 0)
    {
        if (const std::string* value = node.find("value"))
        {
            for (size_t i = 0; i < control.items.size(); ++i)
            {
                if (control.items[i].value == *value)
                {
                    control.selected = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    if (control.selected < 0 && firstByDefault && !control.items.empty())
        control.selected = 0;
}

int matchItem(const Control& control, const std::string& text)
{
    for (size_t i = 0; i < control.items.size(); ++i)
    {
        if (control.items[i].value == text)
            return static_cast<int>(i);
    }
    for (size_t i = 0; i < control.items.size(); ++i)
    {
        if (control.items[i].label == text)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

const std::string* Node::find(const char* name) const
{
    for (const auto& attribute : attributes)
    {
        if (attribute.first == name)
            return &attribute.second;
    }
    return nullptr;
}

std::string Node::attr(const char* name, const std::string& fallback) const
{
    const std::string* value = find(name);
    return value ? *value : fallback;
}

bool Node::flag(const char* name) const
{
    const std::string* value = find(name);
    return value && lower(trim(*value)) == "true";
}

bool parse(const std::string& source, Node& root, std::string& error)
{
    root = Node();
    std::string text = source;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        text.erase(0, 3);
    std::vector<Node*> open{&root};
    bool any = false;
    size_t i = 0;
    while (i < text.size())
    {
        Node& current = *open.back();
        if (text[i] != '<')
        {
            const size_t next = text.find('<', i);
            const size_t stop = next == std::string::npos ? text.size() : next;
            current.text += decode(text.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (text.compare(i, 4, "<!--") == 0)
        {
            const size_t end = text.find("-->", i + 4);
            i = end == std::string::npos ? text.size() : end + 3;
            continue;
        }
        if (text.compare(i, 9, "<![CDATA[") == 0)
        {
            const size_t end = text.find("]]>", i + 9);
            const size_t stop = end == std::string::npos ? text.size() : end;
            current.text += text.substr(i + 9, stop - i - 9);
            i = end == std::string::npos ? text.size() : end + 3;
            continue;
        }
        if (text.compare(i, 2, "<?") == 0)
        {
            const size_t end = text.find("?>", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            continue;
        }
        if (text.compare(i, 2, "<!") == 0)
        {
            // A doctype, perhaps with an internal subset in brackets.
            int depth = 0;
            size_t j = i + 2;
            for (; j < text.size(); ++j)
            {
                if (text[j] == '[')
                    ++depth;
                else if (text[j] == ']')
                    --depth;
                else if (text[j] == '>' && depth <= 0)
                    break;
            }
            i = std::min(j + 1, text.size());
            continue;
        }
        if (text.compare(i, 2, "</") == 0)
        {
            size_t j = i + 2;
            while (j < text.size() && nameChar(text[j]))
                ++j;
            const std::string name = lower(text.substr(i + 2, j - i - 2));
            const size_t end = text.find('>', j);
            i = end == std::string::npos ? text.size() : end + 1;
            // Close up to the matching element; a close tag matching none
            // is dropped.
            for (size_t k = open.size(); k-- > 1;)
            {
                if (open[k]->tag == name)
                {
                    open.resize(k);
                    break;
                }
            }
            continue;
        }
        if (i + 1 >= text.size() || !nameStart(text[i + 1]))
        {
            // A lone '<' in text.
            current.text += '<';
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < text.size() && nameChar(text[j]))
            ++j;
        Node element;
        element.tag = lower(text.substr(i + 1, j - i - 1));
        bool selfClosing = false;
        while (j < text.size())
        {
            while (j < text.size() && blank(text[j]))
                ++j;
            if (j >= text.size())
                break;
            if (text[j] == '>')
            {
                ++j;
                break;
            }
            if (text[j] == '/')
            {
                ++j;
                if (j < text.size() && text[j] == '>')
                {
                    selfClosing = true;
                    ++j;
                    break;
                }
                continue;
            }
            size_t nameEnd = j;
            while (nameEnd < text.size() && !blank(text[nameEnd]) && text[nameEnd] != '=' && text[nameEnd] != '>' &&
                   text[nameEnd] != '/')
                ++nameEnd;
            if (nameEnd == j)
            {
                ++j;
                continue;
            }
            const std::string name = lower(text.substr(j, nameEnd - j));
            j = nameEnd;
            while (j < text.size() && blank(text[j]))
                ++j;
            std::string value;
            if (j < text.size() && text[j] == '=')
            {
                ++j;
                while (j < text.size() && blank(text[j]))
                    ++j;
                if (j < text.size() && (text[j] == '"' || text[j] == '\''))
                {
                    const char quote = text[j];
                    const size_t end = text.find(quote, j + 1);
                    const size_t stop = end == std::string::npos ? text.size() : end;
                    value = decode(text.substr(j + 1, stop - j - 1));
                    j = end == std::string::npos ? text.size() : end + 1;
                }
                else
                {
                    const size_t start = j;
                    while (j < text.size() && !blank(text[j]) && text[j] != '>')
                        ++j;
                    if (j > start && text[j - 1] == '/' && j < text.size() && text[j] == '>')
                        --j;
                    value = decode(text.substr(start, j - start));
                }
            }
            element.attributes.emplace_back(name, value);
        }
        i = j;
        any = true;
        current.children.push_back(std::move(element));
        Node& added = current.children.back();
        if (selfClosing)
            continue;
        if (added.tag == "script")
        {
            // Code is taken as it is: a '<' in it starts no element.
            const size_t close = findClose(text, i, "script");
            const size_t stop = close == std::string::npos ? text.size() : close;
            added.text = scriptText(text.substr(i, stop - i));
            const size_t end = close == std::string::npos ? std::string::npos : text.find('>', close);
            i = end == std::string::npos ? text.size() : end + 1;
            continue;
        }
        open.push_back(&added);
    }
    if (!any)
    {
        error = "The XML holds no elements.";
        return false;
    }
    return true;
}

bool isContainer(const Node& node)
{
    static const char* const kContainers[] = {"dialog", "vbox", "hbox", "box", "content", "groupbox", "grid", "columns",
        "rows", "row", "column", "tabbox", "tabpanels", "tabpanel"};
    for (const char* tag : kContainers)
    {
        if (node.tag == tag)
            return true;
    }
    return false;
}

std::string Control::value() const
{
    switch (kind)
    {
    case Kind::CheckBox:
        return checked ? "true" : "false";
    case Kind::RadioGroup:
    case Kind::ListBox:
        return selected >= 0 && selected < static_cast<int>(items.size()) ? items[static_cast<size_t>(selected)].value
                                                                          : std::string();
    case Kind::MenuList:
    {
        const bool chosen = selected >= 0 && selected < static_cast<int>(items.size());
        if (node && node->flag("editable") && (!chosen || items[static_cast<size_t>(selected)].label != text))
            return text;
        return chosen ? items[static_cast<size_t>(selected)].value : std::string();
    }
    case Kind::ColorChip:
        if (node && lower(node->attr("format")) == "integer")
        {
            const std::string hex = text.size() > 1 && text[0] == '#' ? text.substr(1, 6) : text;
            return std::to_string(std::strtoul(hex.c_str(), nullptr, 16));
        }
        return text;
    default:
        return text;
    }
}

bool Dialog::load(const std::string& xml, std::string& error)
{
    _document = std::make_unique<Node>();
    if (!parse(xml, *_document, error))
        return false;
    // The first <dialog>, wherever it is.
    std::vector<const Node*> pending{_document.get()};
    _root = nullptr;
    while (!pending.empty() && !_root)
    {
        const Node* node = pending.front();
        pending.erase(pending.begin());
        for (const Node& child : node->children)
        {
            if (child.is("dialog"))
            {
                _root = &child;
                break;
            }
            pending.push_back(&child);
        }
    }
    if (!_root)
    {
        error = "The XML has no <dialog> element.";
        return false;
    }
    title = _root->attr("title", "Phoenix");
    buttons.clear();
    const std::string* list = _root->find("buttons");
    const std::string names = list ? *list : std::string("accept, cancel");
    size_t start = 0;
    while (start <= names.size())
    {
        size_t comma = names.find(',', start);
        if (comma == std::string::npos)
            comma = names.size();
        const std::string name = lower(trim(names.substr(start, comma - start)));
        if (!name.empty() && std::find(buttons.begin(), buttons.end(), name) == buttons.end())
            buttons.push_back(name);
        start = comma + 1;
    }
    _controls.clear();
    _scripts.clear();
    _dismiss.clear();
    collect(*_root);
    return true;
}

void Dialog::collect(const Node& node)
{
    for (const Node& child : node.children)
    {
        auto control = std::make_unique<Control>();
        control->node = &child;
        control->id = child.attr("id");
        control->enabled = !child.flag("disabled") && child.attr("enabled") != "false";
        control->visible = !child.flag("hidden") && child.attr("visible") != "false";
        const std::string& tag = child.tag;
        bool made = true;
        if (tag == "textbox")
        {
            control->kind = Kind::TextBox;
            control->text = child.attr("value");
        }
        else if (tag == "checkbox")
        {
            control->kind = Kind::CheckBox;
            control->checked = child.find("checked") ? child.flag("checked") : child.flag("value");
        }
        else if (tag == "radiogroup")
        {
            control->kind = Kind::RadioGroup;
            fillItems(*control, child, "radio", true);
        }
        else if (tag == "menulist")
        {
            control->kind = Kind::MenuList;
            fillItems(*control, child, "menuitem", true);
            if (child.flag("editable"))
            {
                const std::string* value = child.find("value");
                control->text = value ? *value
                    : control->selected >= 0 ? control->items[static_cast<size_t>(control->selected)].label
                                             : std::string();
                if (value)
                {
                    const int match = matchItem(*control, *value);
                    if (match >= 0)
                    {
                        control->selected = match;
                        control->text = control->items[static_cast<size_t>(match)].label;
                    }
                }
            }
        }
        else if (tag == "listbox")
        {
            control->kind = Kind::ListBox;
            fillItems(*control, child, "listitem", false);
        }
        else if (tag == "colorchip")
        {
            control->kind = Kind::ColorChip;
            const std::string* color = child.find("color");
            control->text = normalColor(color ? *color : child.attr("value"));
        }
        else if (tag == "popupslider" || tag == "slider")
        {
            control->kind = Kind::Slider;
            control->text = child.attr("value", child.attr("minvalue", "0"));
        }
        else if (tag == "choosefile")
        {
            control->kind = Kind::ChooseFile;
            control->text = child.attr("value");
        }
        else if (tag == "targetlist")
        {
            control->kind = Kind::TargetList;
            control->text = child.attr("value");
        }
        else if (tag == "property")
        {
            control->kind = Kind::Property;
            control->text = child.attr("value", child.text);
        }
        else if (tag == "button")
            control->kind = Kind::Button;
        else if ((tag == "label" || tag == "description") && !control->id.empty())
        {
            control->kind = Kind::Label;
            control->text = tag == "label" ? child.attr("value", trim(child.text)) : trim(child.text);
        }
        else
            made = false;
        if (tag == "script")
            _scripts.push_back(&child);
        if (made)
            _controls.push_back(std::move(control));
        else
            collect(child);
    }
}

Control* Dialog::find(const std::string& id) const
{
    if (id.empty())
        return nullptr;
    for (const auto& control : _controls)
    {
        if (control->id == id)
            return control.get();
    }
    return nullptr;
}

Control* Dialog::control(const Node* node) const
{
    for (const auto& control : _controls)
    {
        if (control->node == node)
            return control.get();
    }
    return nullptr;
}

void Dialog::create()
{
    for (const Node* script : _scripts)
    {
        if (run && !trim(script->text).empty())
            run(script->text);
    }
    // oncreate, in the order the elements come.
    std::vector<const Node*> order;
    std::function<void(const Node&)> walk = [&](const Node& node) {
        order.push_back(&node);
        for (const Node& child : node.children)
            walk(child);
    };
    walk(*_root);
    for (const Node* node : order)
    {
        if (closed())
            break;
        handle(*node, "oncreate");
    }
}

void Dialog::handle(const Node& node, const char* event)
{
    const std::string* code = node.find(event);
    if (code && run && !trim(*code).empty())
        run(*code);
}

void Dialog::edit(Control& control, const std::string& text)
{
    if (control.text == text)
        return;
    control.text = text;
    if (control.kind == Kind::MenuList)
    {
        const int match = matchItem(control, text);
        if (match >= 0)
            control.selected = match;
    }
    handle(*control.node, "onchange");
}

void Dialog::toggle(Control& control, bool checked)
{
    if (control.checked == checked)
        return;
    control.checked = checked;
    handle(*control.node, "oncommand");
    handle(*control.node, "onchange");
}

void Dialog::choose(Control& control, int index)
{
    if (index < -1 || index >= static_cast<int>(control.items.size()) || control.selected == index)
        return;
    control.selected = index;
    if (control.kind == Kind::MenuList && index >= 0)
        control.text = control.items[static_cast<size_t>(index)].label;
    if (index >= 0 && control.items[static_cast<size_t>(index)].node)
        handle(*control.items[static_cast<size_t>(index)].node, "oncommand");
    handle(*control.node, "oncommand");
    handle(*control.node, "onchange");
}

void Dialog::click(const Node& node)
{
    handle(node, "oncommand");
}

Control& Dialog::require(const std::string& id) const
{
    Control* control = find(id);
    if (!control)
        throw Error("The dialog has no control with the id \"" + id + "\".");
    return *control;
}

std::string Dialog::get(const std::string& id) const
{
    return require(id).value();
}

void Dialog::set(const std::string& id, const Value& value)
{
    Control& control = require(id);
    const std::string text = value.toString();
    switch (control.kind)
    {
    case Kind::CheckBox:
        control.checked = value.type == Value::Type::String ? lower(trim(text)) == "true" : value.toBool();
        break;
    case Kind::RadioGroup:
    case Kind::ListBox:
    case Kind::MenuList:
    {
        const int match = matchItem(control, text);
        if (match >= 0)
        {
            control.selected = match;
            control.text = control.items[static_cast<size_t>(match)].label;
        }
        else if (control.kind == Kind::MenuList && control.node->flag("editable"))
            control.text = text;
        else if (control.kind == Kind::ListBox && text.empty())
            control.selected = -1;
        break;
    }
    case Kind::ColorChip:
        if (value.type == Value::Type::Number)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "#%06lX", static_cast<unsigned long>(value.toNumber()) & 0xFFFFFFul);
            control.text = buffer;
        }
        else
            control.text = normalColor(text);
        break;
    default:
        control.text = text;
        break;
    }
    notify(control);
}

void Dialog::setEnabled(const std::string& id, bool enabled)
{
    Control& control = require(id);
    control.enabled = enabled;
    notify(control);
}

void Dialog::setVisible(const std::string& id, bool visible)
{
    Control& control = require(id);
    control.visible = visible;
    notify(control);
}

void Dialog::setItems(const std::string& id, const std::vector<Item>& items)
{
    Control& control = require(id);
    const std::string before = control.value();
    control.items = items;
    control.selected = matchItem(control, before);
    if (control.selected < 0 && control.kind != Kind::ListBox && !control.items.empty())
        control.selected = 0;
    if (control.kind == Kind::MenuList && control.selected >= 0)
        control.text = control.items[static_cast<size_t>(control.selected)].label;
    notify(control);
}

void Dialog::setItem(const std::string& id, const Item& item)
{
    Control& control = require(id);
    if (control.selected < 0 || control.selected >= static_cast<int>(control.items.size()))
        return;
    Item& chosen = control.items[static_cast<size_t>(control.selected)];
    chosen.label = item.label;
    chosen.value = item.value;
    if (control.kind == Kind::MenuList)
        control.text = item.label;
    notify(control);
}

void Dialog::notify(Control& control)
{
    if (changed)
        changed(control);
}

void Dialog::close(const std::string& dismiss)
{
    if (closed() || _closing)
        return;
    _closing = true;
    handle(*_root, dismiss == "accept" ? "ondialogaccept" : "ondialogcancel");
    _closing = false;
    _dismiss = dismiss;
}

Value Dialog::result() const
{
    Value result = Value::map();
    result.set("dismiss", _dismiss.empty() ? std::string("cancel") : _dismiss);
    for (const auto& control : _controls)
    {
        if (!control->id.empty() && control->hasValue() && !result.field(control->id))
            result.set(control->id, control->value());
    }
    return result;
}

} // namespace jsfl::xul
