// fl.xmlPanel, fl.xmlPanelFromString, document.xmlPanel and fl.xmlui.

#include "flfile.h"
#include "session.h"
#include "xul.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>

namespace jsfl {

namespace {

/// fl.xmlui: the controls of the innermost dialog open.
class XmlUI final : public Object
{
public:
    explicit XmlUI(Session& session) : session(session) {}
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return reinterpret_cast<const char*>(&session) + 2; }
    Session& session;

    xul::Dialog& dialog() const
    {
        if (session.dialogs.empty())
            throw Error("fl.xmlui works only while an XML dialog is open.");
        return *session.dialogs.back();
    }
    void close(const std::string& dismiss) const
    {
        xul::Dialog& open = dialog();
        open.close(dismiss);
        if (open.closedByScript)
            open.closedByScript();
    }
};

Value itemValue(const xul::Item& item)
{
    Value value = Value::map();
    value.set("label", item.label);
    value.set("value", item.value);
    return value;
}

xul::Item itemFrom(const Value& value)
{
    xul::Item item;
    if (value.type == Value::Type::Map)
    {
        const Value* label = value.field("label");
        const Value* data = value.field("value");
        item.label = label && !label->isNullish() ? label->toString() : std::string();
        item.value = data && !data->isNullish() ? data->toString() : item.label;
    }
    else
        item.label = item.value = value.toString();
    return item;
}

const ClassInfo& xmlUIInfo()
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<XmlUI> def(info, "XMLUI");
        def.method("get", 1, [](XmlUI& ui, const Args& args) { return Value(ui.dialog().get(arg(args, 0).toString())); });
        def.method("set", 2, [](XmlUI& ui, const Args& args) {
            ui.dialog().set(arg(args, 0).toString(), arg(args, 1));
            return Value();
        });
        def.method("getEnabled", 1, [](XmlUI& ui, const Args& args) {
            return Value(ui.dialog().require(arg(args, 0).toString()).enabled);
        });
        def.method("setEnabled", 2, [](XmlUI& ui, const Args& args) {
            ui.dialog().setEnabled(arg(args, 0).toString(), arg(args, 1).toBool());
            return Value();
        });
        def.method("getVisible", 1, [](XmlUI& ui, const Args& args) {
            return Value(ui.dialog().require(arg(args, 0).toString()).visible);
        });
        def.method("setVisible", 2, [](XmlUI& ui, const Args& args) {
            ui.dialog().setVisible(arg(args, 0).toString(), arg(args, 1).toBool());
            return Value();
        });
        def.method("accept", 0, [](XmlUI& ui, const Args&) {
            ui.close("accept");
            return Value();
        });
        def.method("cancel", 0, [](XmlUI& ui, const Args&) {
            ui.close("cancel");
            return Value();
        });
        def.method("getControlItemElements", 1, [](XmlUI& ui, const Args& args) {
            Value list = Value::array();
            for (const xul::Item& item : ui.dialog().require(arg(args, 0).toString()).items)
                list.items.push_back(itemValue(item));
            return list;
        });
        def.method("setControlItemElements", 2, [](XmlUI& ui, const Args& args) {
            std::vector<xul::Item> items;
            for (const Value& entry : arg(args, 1).items)
                items.push_back(itemFrom(entry));
            ui.dialog().setItems(arg(args, 0).toString(), items);
            return Value();
        });
        def.method("getControlItemElement", 1, [](XmlUI& ui, const Args& args) {
            const xul::Control& control = ui.dialog().require(arg(args, 0).toString());
            if (control.selected < 0 || control.selected >= static_cast<int>(control.items.size()))
                return Value::null();
            return itemValue(control.items[static_cast<size_t>(control.selected)]);
        });
        def.method("setControlItemElement", 2, [](XmlUI& ui, const Args& args) {
            ui.dialog().setItem(arg(args, 0).toString(), itemFrom(arg(args, 1)));
            return Value();
        });
        return info;
    }();
    return info;
}

const ClassInfo& XmlUI::classInfo() const
{
    return xmlUIInfo();
}

/// The file's text as UTF-8, from UTF-8 or UTF-16 with a byte order mark.
bool readText(const std::string& path, std::string& text)
{
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in)
        return false;
    std::ostringstream content;
    content << in.rdbuf();
    const std::string bytes = content.str();
    const bool little = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE;
    const bool big = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE && static_cast<unsigned char>(bytes[1]) == 0xFF;
    if (!little && !big)
    {
        text = bytes;
        return true;
    }
    text.clear();
    const auto unit = [&](size_t at) {
        const unsigned a = static_cast<unsigned char>(bytes[at]);
        const unsigned b = static_cast<unsigned char>(bytes[at + 1]);
        return little ? (a | (b << 8)) : ((a << 8) | b);
    };
    for (size_t i = 2; i + 1 < bytes.size(); i += 2)
    {
        unsigned long code = unit(i);
        if (code >= 0xD800 && code < 0xDC00 && i + 3 < bytes.size())
        {
            const unsigned long low = unit(i + 2);
            if (low >= 0xDC00 && low < 0xE000)
            {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        if (code < 0x80)
            text += static_cast<char>(code);
        else if (code < 0x800)
        {
            text += static_cast<char>(0xC0 | (code >> 6));
            text += static_cast<char>(0x80 | (code & 0x3F));
        }
        else if (code < 0x10000)
        {
            text += static_cast<char>(0xE0 | (code >> 12));
            text += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (code & 0x3F));
        }
        else
        {
            text += static_cast<char>(0xF0 | (code >> 18));
            text += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            text += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return true;
}

/// The named movie clip and button instances of \a timeline and, by
/// their paths, those inside them: what a targetlist offers.
void targetsIn(const fla::Timeline* timeline, const std::string& prefix, int depth, std::vector<std::string>& out)
{
    if (!timeline || depth > 8)
        return;
    std::vector<std::string> seen;
    for (const fla::Layer* layer : timeline->layers)
    {
        for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
        {
            for (const fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
            {
                const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element);
                if (!instance || instance->name.empty() || instance->symbolType == fla::SymbolType::Graphic)
                    continue;
                const std::string path = prefix + "." + instance->name;
                if (std::find(seen.begin(), seen.end(), path) != seen.end())
                    continue;
                seen.push_back(path);
                out.push_back(path);
                if (instance->symbol && !instance->symbol->timelines.empty())
                    targetsIn(instance->symbol->timelines.front(), path, depth + 1, out);
            }
        }
    }
}

/// Each targetlist's items: the instances of the timeline being edited, as
/// paths from it ("this.car.wheel").
void fillTargets(Session& session, xul::Dialog& dialog)
{
    std::vector<std::string> targets;
    bool wanted = false;
    for (const auto& control : dialog.controls())
        wanted = wanted || control->kind == xul::Kind::TargetList;
    if (!wanted)
        return;
    if (fla::FLADocument* document = session.host().activeDocument())
        targetsIn(session.host().editedTimeline(document), "this", 0, targets);
    for (const auto& control : dialog.controls())
    {
        if (control->kind != xul::Kind::TargetList)
            continue;
        control->items.clear();
        for (const std::string& target : targets)
            control->items.push_back(xul::Item{target, target, nullptr});
    }
}

} // namespace

Value showXmlPanel(Session& session, const std::string& xml, const std::string& folder)
{
    xul::Dialog dialog;
    std::string error;
    if (!dialog.load(xml, error))
        throw Error("xmlPanel: " + error);
    dialog.folder = folder;
    fillTargets(session, dialog);
    dialog.run = [&session](const std::string& code) {
        if (!session.evaluate)
            return;
        const std::string failure = session.evaluate(code);
        if (!failure.empty())
            session.host().trace("xmlPanel: " + failure);
    };
    struct Open
    {
        Session& session;
        Open(Session& session, xul::Dialog& dialog) : session(session) { session.dialogs.push_back(&dialog); }
        ~Open() { session.dialogs.pop_back(); }
    } open(session, dialog);
    dialog.create();
    if (!dialog.closed())
    {
        if (!session.host().showDialog(dialog))
        {
            session.host().trace("xmlPanel: no one to answer \"" + dialog.title + "\", so it was accepted as it stood.");
            dialog.close("accept");
        }
        else if (!dialog.closed())
            dialog.close("cancel");
    }
    return dialog.result();
}

Value showXmlPanelFile(Session& session, const std::string& uri)
{
    const std::string path = uri::toPath(uri);
    std::string text;
    if (!readText(path, text))
        throw Error("xmlPanel: could not read " + path + ".");
    const auto folder = std::filesystem::u8path(path).parent_path().u8string();
    return showXmlPanel(session, text, std::string(folder.begin(), folder.end()));
}

ObjectPtr makeXmlUI(Session& session)
{
    return std::make_shared<XmlUI>(session);
}

} // namespace jsfl
