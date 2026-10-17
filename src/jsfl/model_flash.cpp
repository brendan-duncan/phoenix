// `fl` and its output panel; colors and the wrappers the model's files share.

#include "flfile.h"
#include "extensions.h"
#include "model.h"
#include "xul.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../edit/drawing_style.h"
#include "../edit/selection.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdint>
#include <memory>

namespace jsfl {

std::string colorText(const uint8_t rgba[4])
{
    char buffer[16];
    if (rgba[3] == 255)
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", rgba[0], rgba[1], rgba[2]);
    else
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", rgba[0], rgba[1], rgba[2], rgba[3]);
    return buffer;
}

bool parseColor(const Value& value, uint8_t rgba[4])
{
    if (value.type == Value::Type::Number)
    {
        const auto number = static_cast<unsigned long>(value.toNumber());
        rgba[0] = static_cast<uint8_t>((number >> 16) & 0xFF);
        rgba[1] = static_cast<uint8_t>((number >> 8) & 0xFF);
        rgba[2] = static_cast<uint8_t>(number & 0xFF);
        rgba[3] = 255;
        return true;
    }
    std::string text = value.toString();
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }),
        text.end());
    if (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0)
        text = "#" + text.substr(2);
    if (text.empty() || text[0] != '#')
        return false;
    std::string hex = text.substr(1);
    if (!std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        return false;
    if (hex.size() == 3)
        hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    if (hex.size() != 6 && hex.size() != 8)
        return false;
    const auto byte = [&hex](size_t at) { return static_cast<uint8_t>(std::stoul(hex.substr(at, 2), nullptr, 16)); };
    rgba[0] = byte(0);
    rgba[1] = byte(2);
    rgba[2] = byte(4);
    rgba[3] = hex.size() == 8 ? byte(6) : 255;
    return true;
}

void DocumentPart::require(const fla::DOMElement* object, const char* what) const
{
    data();
    if (!session.alive(document, object))
        throw Error(std::string("This ") + what + " is no longer in the document (it was deleted, or an edit replaced it).");
}

std::shared_ptr<Document> wrapDocument(Session& session, fla::FLADocument* document)
{
    if (!document)
        return nullptr;
    return std::make_shared<Document>(session, document);
}

namespace {

fla::FLADocument* documentOf(Flash& fl, const Value& value)
{
    if (value.isNullish())
    {
        fla::FLADocument* active = fl.session.host().activeDocument();
        if (!active)
            throw Error("No document is open.");
        return active;
    }
    const auto document = value.as<Document>();
    if (!document)
        throw Error("A Document is needed.");
    document->data();
    return document->document;
}

std::string withSlash(std::string folder)
{
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\')
#ifdef _WIN32
        folder += '\\';
#else
        folder += '/';
#endif
    return folder;
}

Value saveAs(Flash& fl, fla::FLADocument* document, const std::string& path, bool copy)
{
    Session& session = fl.session;
    session.beforeSwitch();
    std::string error;
    if (!session.host().save(document, path, copy, error))
        throw Error(error.empty() ? "The document could not be saved." : error);
    return Value(true);
}

/// Every element on every keyframe of the document, in its scenes and its
/// symbols, inside groups too, that \a wanted picks; as fl.findObjectInDoc*
/// gives them: {obj, keyframe, layer, timeline, parent}.
Value findInDocument(Flash& fl, fla::FLADocument* document, const std::function<bool(Element&)>& wanted)
{
    Session& session = fl.session;
    fla::Document& data = session.data(document);
    Value found = Value::array();
    std::vector<fla::Timeline*> timelines = data.timelines;
    if (data.symbolList)
        for (fla::Symbol* symbol : data.symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (fla::Timeline* timeline : timelines)
    {
        for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
        {
            for (fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            {
                if (!frame)
                    continue;
                std::function<void(fla::Element*, const Value&)> visit = [&](fla::Element* element, const Value& parent) {
                    if (!element)
                        return;
                    auto wrapper = wrapElement(session, document, element);
                    if (wanted(*wrapper))
                    {
                        Value entry = Value::map();
                        entry.set("obj", wrapper);
                        entry.set("keyframe", std::make_shared<Frame>(session, document, layer, frame->index));
                        entry.set("layer", wrapLayer(session, document, layer));
                        entry.set("timeline", wrapTimeline(session, document, timeline));
                        entry.set("parent", parent);
                        found.items.push_back(std::move(entry));
                    }
                    if (auto* group = dynamic_cast<fla::Group*>(element))
                        for (fla::Element* member : group->members)
                            visit(member, Value(wrapper));
                };
                for (fla::Element* element : frame->elements)
                    visit(element, Value::null());
            }
        }
    }
    return found;
}

std::string propertyText(Element& element, const char* name)
{
    for (const ClassInfo* info = &element.classInfo(); info; info = info->parent)
        for (const Property& property : info->properties)
            if (property.name == name)
                return property.get(element).toString();
    return {};
}

} // namespace

const ClassInfo& Flash::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Flash> def(info, "Flash");
        def.get("documents", [](Flash& fl) {
            Value list = Value::array();
            for (fla::FLADocument* document : fl.session.host().documents())
                list.items.emplace_back(wrapDocument(fl.session, document));
            return list;
        });
        def.method("getDocumentDOM", 0, [](Flash& fl, const Args&) {
            return Value(wrapDocument(fl.session, fl.session.host().activeDocument()));
        });
        def.method("openDocument", 1, [](Flash& fl, const Args& args) {
            const std::string path = uri::toPath(arg(args, 0).toString());
            fl.session.beforeSwitch();
            std::string error;
            fla::FLADocument* document = fl.session.host().open(path, error);
            if (!document)
                throw Error(error.empty() ? "Could not open " + path : error);
            return Value(wrapDocument(fl.session, document));
        });
        def.method("createDocument", 1, [](Flash& fl, const Args& args) {
            const std::string type = given(args, 0) ? arg(args, 0).toString() : "timeline";
            if (type != "timeline")
                throw Error("Phoenix makes timeline documents only, not " + type + ".");
            fl.session.beforeSwitch();
            std::string error;
            fla::FLADocument* document = fl.session.host().create(error);
            if (!document)
                throw Error(error.empty() ? "A new document could not be made." : error);
            return Value(wrapDocument(fl.session, document));
        });
        def.method("saveDocument", 2, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            std::string path = given(args, 1) ? uri::toPath(arg(args, 1).toString()) : std::string();
            if (path.empty() && fl.session.host().path(document).empty())
            {
                path = fl.session.host().browseForFile("save", "Save Document");
                if (path.empty())
                    return Value(false);
            }
            return saveAs(fl, document, path, false);
        });
        def.method("saveDocumentAs", 1, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            const std::string path = fl.session.host().browseForFile("save", "Save Document As");
            if (path.empty())
                return Value(false);
            return saveAs(fl, document, path, false);
        });
        // fl.phoenixCall(name, argumentsJson): one of Phoenix's operations,
        // its result as JSON text. Scripts use fl.phoenix.call (the prelude),
        // which takes and gives objects.
        def.method("phoenixCall", 2, [](Flash& fl, const Args& args) {
            std::string result;
            std::string error;
            if (!fl.session.host().callOperation(arg(args, 0).toString(), given(args, 1) ? arg(args, 1).toString() : "{}",
                    result, error))
                throw Error(error);
            return Value(result);
        });
        def.method("saveAll", 0, [](Flash& fl, const Args&) {
            for (fla::FLADocument* document : fl.session.host().documents())
            {
                if (!fl.session.host().path(document).empty() && fl.session.host().modified(document))
                    saveAs(fl, document, std::string(), false);
            }
            return Value();
        });
        def.method("closeDocument", 2, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            const bool ask = given(args, 1) ? arg(args, 1).toBool() : true;
            fl.session.beforeSwitch();
            const bool closed = fl.session.host().close(document, ask);
            if (closed)
                fl.session.forget(document);
            return Value(closed);
        });
        def.method("closeAll", 1, [](Flash& fl, const Args& args) {
            const bool ask = given(args, 0) ? arg(args, 0).toBool() : true;
            fl.session.beforeSwitch();
            bool all = true;
            for (fla::FLADocument* document : fl.session.host().documents())
            {
                if (fl.session.host().close(document, ask))
                    fl.session.forget(document);
                else
                    all = false;
            }
            return Value(all);
        });
        def.method("setActiveWindow", 1, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            if (document != fl.session.host().activeDocument())
            {
                fl.session.beforeSwitch();
                fl.session.host().activate(document);
            }
            return Value();
        });
        def.method("findDocumentIndex", 1, [](Flash& fl, const Args& args) {
            const std::string name = arg(args, 0).toString();
            Value found = Value::array();
            const std::vector<fla::FLADocument*> documents = fl.session.host().documents();
            for (size_t i = 0; i < documents.size(); ++i)
            {
                if (fl.session.host().name(documents[i]) == name)
                    found.items.emplace_back(static_cast<int>(i));
            }
            return found;
        });
        def.method("trace", 1, [](Flash& fl, const Args& args) {
            fl.session.host().trace(arg(args, 0).toString());
            return Value();
        });
        def.get("configDirectory", [](Flash& fl) { return Value(withSlash(fl.session.host().configDirectory())); });
        def.get("configURI", [](Flash& fl) {
            std::string folder = uri::fromPath(fl.session.host().configDirectory());
            if (folder.empty() || folder.back() != '/')
                folder += '/';
            return Value(folder);
        });
        def.get("scriptURI", [](Flash& fl) {
            return fl.session.scriptPath.empty() ? Value("") : Value(uri::fromPath(fl.session.scriptPath));
        });
        def.get("version", [](Flash& fl) {
            // The scripting level Phoenix aims at, first, as scripts read it
            // (fl.version.split(" ")[1]), then Phoenix's own.
#ifdef _WIN32
            std::string text = "WIN 24,0,0,0";
#else
            std::string text = "MAC 24,0,0,0";
#endif
            return Value(text + " Phoenix " + fl.session.host().version());
        });
        def.method("runScript", 2, [](Flash& fl, const Args& args) {
            if (!fl.runScript)
                throw Error("Scripts cannot run other scripts here.");
            const std::string path = uri::toPath(arg(args, 0).toString());
            const std::string function = given(args, 1) ? arg(args, 1).toString() : std::string();
            const Args rest(args.size() > 2 ? args.begin() + 2 : args.end(), args.end());
            return fl.runScript(path, function, rest);
        });
        def.method("browseForFileURL", 2, [](Flash& fl, const Args& args) {
            const std::string mode = given(args, 0) ? arg(args, 0).toString() : "open";
            const std::string path = fl.session.host().browseForFile(mode, arg(args, 1).isNullish() ? "" : arg(args, 1).toString());
            return path.empty() ? Value::null() : Value(uri::fromPath(path));
        });
        def.method("browseForFolderURL", 1, [](Flash& fl, const Args& args) {
            const std::string path = fl.session.host().browseForFolder(arg(args, 0).isNullish() ? "" : arg(args, 0).toString());
            return path.empty() ? Value::null() : Value(uri::fromPath(path));
        });
        def.get("outputPanel", [](Flash& fl) { return Value(std::make_shared<OutputPanel>(fl.session)); });
        def.method("findObjectInDocByName", 2, [](Flash& fl, const Args& args) {
            const std::string name = arg(args, 0).toString();
            return findInDocument(fl, documentOf(fl, arg(args, 1)),
                [&name](Element& element) { return !name.empty() && propertyText(element, "name") == name; });
        });
        def.method("findObjectInDocByType", 2, [](Flash& fl, const Args& args) {
            const std::string type = arg(args, 0).toString();
            return findInDocument(fl, documentOf(fl, arg(args, 1)), [&type](Element& element) {
                const std::string kind = propertyText(element, "elementType");
                // A drawing object is a "shapeObj" to search for.
                if (type == "shapeObj")
                    return kind == "shape" && propertyText(element, "isDrawingObject") == "true";
                return kind == type;
            });
        });
        def.method("selectElement", 2, [](Flash& fl, const Args& args) {
            // An entry fl.findObjectInDoc* gave, or an element.
            const Value& given = arg(args, 0);
            const Value* object = given.type == Value::Type::Map ? given.field("obj") : &given;
            const auto element = object ? object->as<Element>() : nullptr;
            if (!element)
                throw Error("selectElement takes an element, or what fl.findObjectInDocByName gives.");
            fla::Element& target = element->get();
            auto* frame = dynamic_cast<fla::Frame*>(target.parent);
            // Inside a group: the group's frame.
            for (fla::DOMElement* up = target.parent; !frame && up; up = up->parent)
                frame = dynamic_cast<fla::Frame*>(up);
            auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
            auto* timeline = layer ? dynamic_cast<fla::Timeline*>(layer->parent) : nullptr;
            if (!timeline)
                return Value(false);
            Host& host = fl.session.host();
            fla::FLADocument* document = element->document;
            if (document != host.activeDocument())
            {
                fl.session.beforeSwitch();
                host.activate(document);
            }
            host.editTimeline(document, timeline);
            host.setCurrentFrame(document, frame->index);
            const auto at = std::find(timeline->layers.begin(), timeline->layers.end(), layer);
            host.setCurrentLayer(document, static_cast<int>(at - timeline->layers.begin()));
            auto* instance = dynamic_cast<fla::SymbolInstance*>(&target);
            if (arg(args, 1).toBool() && instance && instance->symbol && !instance->symbol->timelines.empty())
                host.editTimeline(document, instance->symbol->timelines.front());
            else
                host.selection(document).select(&target);
            return Value(true);
        });
        def.prop("objectDrawingMode",
            [](Flash& fl) {
                const fla::DrawingStyle* style = fl.session.host().drawingStyle();
                return Value(style && style->objectDrawing);
            },
            [](Flash& fl, const Value& v) {
                if (fla::DrawingStyle* style = fl.session.host().drawingStyle())
                    style->objectDrawing = v.toBool();
            });
        def.method("showIdleMessage", 1,[](Flash&, const Args&) { return Value(); });
        def.method("clipCopyString", 1, [](Flash& fl, const Args& args) {
            fl.session.host().copyText(arg(args, 0).toString());
            return Value();
        });
        def.method("closeAllPlayerDocuments", 0, [](Flash&, const Args&) { return Value(true); });
        def.method("xmlPanel", 1, [](Flash& fl, const Args& args) {
            return showXmlPanelFile(fl.session, arg(args, 0).toString());
        });
        def.method("xmlPanelFromString", 1, [](Flash& fl, const Args& args) {
            return showXmlPanel(fl.session, arg(args, 0).toString());
        });
        def.get("xmlui", [](Flash& fl) { return Value(makeXmlUI(fl.session)); });
        describeFlashPublishing(def);
        describeFlashTools(def);
        return info;
    }();
    return info;
}

const ClassInfo& OutputPanel::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<OutputPanel> def(info, "outputPanel");
        def.method("trace", 1, [](OutputPanel& panel, const Args& args) {
            panel.session.host().trace(arg(args, 0).toString());
            return Value();
        });
        def.method("clear", 0, [](OutputPanel& panel, const Args&) {
            panel.session.host().clearOutput();
            return Value();
        });
        def.method("save", 3, [](OutputPanel& panel, const Args& args) {
            const std::string path = uri::toPath(arg(args, 0).toString());
            const bool append = arg(args, 1).toBool();
            std::ofstream out(std::filesystem::u8path(path), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            const std::string text = panel.session.host().outputText();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return Value(static_cast<bool>(out));
        });
        return info;
    }();
    return info;
}

std::shared_ptr<Flash> makeFlash(Session& session)
{
    return std::make_shared<Flash>(session);
}

} // namespace jsfl
