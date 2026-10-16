// Document: the stage's settings, the selection and what is done to it, the
// timelines, saving and exporting.

#include "flfile.h"
#include "extensions.h"
#include "model.h"
#include "xul.h"

#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/document_settings.h"
#include "../edit/drawing_style.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"
#include "../edit/layer_commands.h"
#include "../edit/library_commands.h"
#include "../edit/property_command.h"
#include "../edit/scene_commands.h"
#include "../edit/selection.h"
#include "../edit/timeline_edit.h"
#include "../edit/trace_bitmap.h"
#include "../edit/vector_import.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <system_error>
#include <cstdint>

namespace jsfl {

namespace {

constexpr double kPi = 3.14159265358979323846;

Rect unite(const Rect& a, const Rect& b)
{
    if (!a.valid())
        return b;
    if (!b.valid())
        return a;
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
}

Rect selectionBounds(Session& session, fla::FLADocument* document, const std::vector<fla::Element*>& elements)
{
    Rect bounds;
    for (const fla::Element* element : elements)
        bounds = unite(bounds, session.host().bounds(document, element));
    return bounds;
}

/// A named point of \a box: "top left", "center", "bottom right"...
bool namedPoint(const Rect& box, std::string name, double& x, double& y)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool top = name.find("top") != std::string::npos;
    const bool bottom = name.find("bottom") != std::string::npos;
    const bool left = name.find("left") != std::string::npos;
    const bool right = name.find("right") != std::string::npos;
    if (!top && !bottom && !left && !right && name.find("center") == std::string::npos)
        return false;
    x = left ? box.left : right ? box.right : (box.left + box.right) / 2;
    y = top ? box.top : bottom ? box.bottom : (box.top + box.bottom) / 2;
    return true;
}

double field(const Value& value, const char* name, double fallback = 0.0)
{
    const Value* found = value.field(name);
    return found && !found->isNullish() ? found->toNumber() : fallback;
}

Value rectValue(const Rect& box)
{
    Value value = Value::map();
    value.set("left", box.left);
    value.set("top", box.top);
    value.set("right", box.right);
    value.set("bottom", box.bottom);
    return value;
}

fla::Frame* frameOf(const fla::Element* element)
{
    return element ? dynamic_cast<fla::Frame*>(element->parent) : nullptr;
}

/// \a change about the point \a x, \a y, applied to each element.
void transformAll(Session& session, fla::FLADocument* document, const std::vector<fla::Element*>& elements,
    const fla::Transform& change, double x, double y, const char* name)
{
    const fla::Transform about = compose(fla::Transform::fromTranslate(x, y), compose(change, fla::Transform::fromTranslate(-x, -y)));
    for (fla::Element* element : elements)
        setTransform(session, document, element, compose(about, element->transform), name);
}

/// Elements copied by clipCopy or clipCut, kept between runs as a
/// clipboard is.
std::vector<std::unique_ptr<fla::Element>>& elementClipboard()
{
    static std::vector<std::unique_ptr<fla::Element>> elements;
    return elements;
}

void copyToClipboard(const std::vector<fla::Element*>& elements)
{
    std::vector<std::unique_ptr<fla::Element>> copies;
    for (const fla::Element* element : elements)
    {
        if (fla::Element* copy = fla::cloneElement(*element, nullptr))
        {
            // Kept apart from the document it came from.
            fla::relinkSymbols(*copy, nullptr);
            copies.emplace_back(copy);
        }
    }
    elementClipboard() = std::move(copies);
}

std::vector<fla::Element*> needSelection(Session& session, fla::FLADocument* document)
{
    std::vector<fla::Element*> elements = selectedElements(session, document);
    if (elements.empty())
        throw Error("Nothing is selected.");
    return elements;
}

/// The toolbar's fill and stroke: the host's, or one kept for the run.
fla::DrawingStyle& toolbarStyle(Document& d)
{
    d.data();
    if (fla::DrawingStyle* style = d.session.host().drawingStyle())
        return *style;
    static fla::DrawingStyle fallback;
    return fallback;
}

/// A new shape from SVG path data, filled and stroked as \a style says,
/// placed on the current keyframe as a drawing object or merged into the
/// artwork there.
void addShape(Document& doc, const std::string& path, bool fill, bool stroke)
{
    Session& session = doc.session;
    const fla::DrawingStyle* style = &toolbarStyle(doc);
    fla::Frame* frame = currentKeyframe(session, doc.document);
    const bool filled = fill && style->hasFill;
    const bool stroked = stroke && style->hasStroke;
    if (!filled && !stroked)
        return;
    const auto color = [](const uint8_t rgba[4]) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", rgba[0], rgba[1], rgba[2]);
        return std::string(buffer);
    };
    char opacity[64];
    std::snprintf(opacity, sizeof(opacity), " fill-opacity=\"%g\" stroke-opacity=\"%g\"", style->fillColor[3] / 255.0,
        style->strokeColor[3] / 255.0);
    char width[32];
    std::snprintf(width, sizeof(width), "%g", std::max(0.1, style->strokeWeight));
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"" + path + "\" fill=\"" +
        (filled ? color(style->fillColor) : std::string("none")) + "\" stroke=\"" +
        (stroked ? color(style->strokeColor) : std::string("none")) + "\" stroke-width=\"" + width + "\"" + opacity + "/></svg>";
    const fla::vectors::Artwork art = fla::vectors::readSvg(svg);
    if (art.paths.empty())
        throw Error("That draws nothing.");
    fla::Shape* shape = fla::vectors::toShape(art.paths.front());
    if (!shape)
        throw Error("That could not be made into a shape.");
    shape->parent = frame;
    fla::Selection& selection = session.host().selection(doc.document);
    if (style->objectDrawing)
    {
        shape->isDrawingObject = true;
        session.push(doc.document, fla::CommandPtr(new fla::AddElementCommand(frame, shape, "Shape", &selection)));
        return;
    }
    shape->isDrawingObject = false;
    // Merge drawing: into the artwork already on the frame, as drawing it
    // by hand would.
    for (fla::Element* element : frame->elements)
    {
        if (element && element->elementType() == fla::Element::Type::Shape && !static_cast<fla::Shape*>(element)->isDrawingObject)
        {
            session.push(doc.document, fla::CommandPtr(new fla::MergeShapeCommand(static_cast<fla::Shape*>(element), *shape, "Shape")));
            delete shape;
            return;
        }
    }
    session.push(doc.document, fla::CommandPtr(new fla::AddElementCommand(frame, shape, "Shape", &selection, 0)));
}

std::string number(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    return buffer;
}

/// Sets \a name on every selected element, as Element's own property does.
void setOnSelection(Document& doc, const std::string& name, const Value& value)
{
    for (fla::Element* element : needSelection(doc.session, doc.document))
    {
        auto wrapper = wrapElement(doc.session, doc.document, element);
        bool found = false;
        for (const ClassInfo* info = &wrapper->classInfo(); info && !found; info = info->parent)
        {
            for (const Property& property : info->properties)
            {
                if (property.name == name && property.set)
                {
                    property.set(*wrapper, value);
                    found = true;
                    break;
                }
            }
        }
    }
}

/// Whether aligning, distributing, spacing and matching go by the stage
/// rather than the selection when a script does not say: the Align
/// panel's To Stage, kept between runs.
bool& alignToStage()
{
    static bool toStage = false;
    return toStage;
}

bool useStage(const Args& args, size_t at)
{
    return given(args, at) ? arg(args, at).toBool() : alignToStage();
}

const Property* findProperty(const Object& object, const std::string& name)
{
    for (const ClassInfo* info = &object.classInfo(); info; info = info->parent)
        for (const Property& property : info->properties)
            if (property.name == name)
                return &property;
    return nullptr;
}

const Method* findMethod(const Object& object, const std::string& name)
{
    for (const ClassInfo* info = &object.classInfo(); info; info = info->parent)
        for (const Method& method : info->methods)
            if (method.name == name)
                return &method;
    return nullptr;
}

/// Calls the element method \a name on each selected element that has it
/// (text fields for the text methods); what the first one gave back, or
/// undefined when none has it.
Value callOnSelection(Document& doc, const std::string& name, const Args& args)
{
    Value first;
    bool called = false;
    for (fla::Element* element : needSelection(doc.session, doc.document))
    {
        auto wrapper = wrapElement(doc.session, doc.document, element);
        if (const Method* method = findMethod(*wrapper, name))
        {
            Value result = method->call(*wrapper, args);
            if (!called)
                first = std::move(result);
            called = true;
        }
    }
    return first;
}

/// Changes the filters of each selected element that has them.
void editFilters(Document& doc, const std::function<void(std::vector<Value>& filters)>& edit)
{
    for (fla::Element* element : needSelection(doc.session, doc.document))
    {
        auto wrapper = wrapElement(doc.session, doc.document, element);
        const Property* property = findProperty(*wrapper, "filters");
        if (!property || !property->set)
            continue;
        Value list = property->get(*wrapper);
        edit(list.items);
        property->set(*wrapper, list);
    }
}

/// The filter at \a index of \a filters; an error when there is none.
Value& filterAt(std::vector<Value>& filters, const Value& index)
{
    const int at = index.toInt();
    if (at < 0 || at >= static_cast<int>(filters.size()))
        throw Error("There is no filter " + index.toString() + ".");
    return filters[static_cast<size_t>(at)];
}

bool sceneNamed(const fla::Document& data, const std::string& name)
{
    return std::any_of(data.timelines.begin(), data.timelines.end(),
        [&name](const fla::Timeline* scene) { return scene && scene->name == name; });
}

/// A new order of the scenes and the one current in it.
struct SceneList
{
    std::vector<fla::Timeline*> scenes;
    int current = 0;
};

/// The scene being edited, or the one a symbol being edited was entered
/// from.
int sceneIndex(Document& d)
{
    const fla::Document& data = d.data();
    if (data.timelines.empty())
        throw Error("The document has no scenes.");
    const fla::Timeline* edited = d.session.host().editedTimeline(d.document);
    const auto found = std::find(data.timelines.begin(), data.timelines.end(), edited);
    return found != data.timelines.end() ? static_cast<int>(found - data.timelines.begin()) : data.currentSceneIndex();
}

/// The scenes with \a added after the current one, and current.
SceneList sceneAdded(Document& d, fla::Timeline* added)
{
    const fla::Document& data = d.data();
    SceneList list{data.timelines, sceneIndex(d) + 1};
    list.scenes.insert(list.scenes.begin() + list.current, added);
    return list;
}

/// Puts the scenes in \a list's order as one step, and edits the current
/// one: the window shows it, its timeline and its scene list follow.
void pushScenes(Document& d, SceneList list, const char* name)
{
    // Undone, the scene edited now is current again.
    d.data().setCurrentScene(sceneIndex(d));
    d.push(fla::CommandPtr(new fla::SceneListCommand(&d.data(), std::move(list.scenes), list.current, name)));
    d.session.host().editTimeline(d.document, d.data().currentScene());
}

} // namespace

const ClassInfo& Document::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Document> def(info, "Document");

        def.prop("width", [](Document& d) { return Value(d.data().width); },
            [](Document& d, const Value& v) {
                const int width = std::clamp(v.toInt(), 1, 8192);
                if (width != d.data().width)
                    d.push(fla::docsettings::resize(d.data(), width, d.data().height, false, 0.0, 0.0), false);
            });
        def.prop("height", [](Document& d) { return Value(d.data().height); },
            [](Document& d, const Value& v) {
                const int height = std::clamp(v.toInt(), 1, 8192);
                if (height != d.data().height)
                    d.push(fla::docsettings::resize(d.data(), d.data().width, height, false, 0.0, 0.0), false);
            });
        def.prop("frameRate", [](Document& d) { return Value(d.data().frameRate); },
            [](Document& d, const Value& v) {
                const double rate = std::clamp(v.toNumber(), 0.01, 120.0);
                d.push(fla::CommandPtr(new fla::SetPropertyCommand<double>(d.data().frameRate, d.data().frameRate, rate, "Frame Rate")), false);
            });
        def.prop("backgroundColor", [](Document& d) { return Value(colorText(d.data().backgroundColor)); },
            [](Document& d, const Value& v) {
                uint8_t color[4];
                if (!parseColor(v, color))
                    throw Error("That is not a color: " + v.toString());
                d.push(fla::CommandPtr(new fla::SetColorCommand(d.data().backgroundColor, d.data().backgroundColor, color, "Background Color")), false);
            });
        def.get("name", [](Document& d) {
            d.data();
            return Value(d.session.host().name(d.document));
        });
        def.get("path", [](Document& d) {
            d.data();
            const std::string path = d.session.host().path(d.document);
            return path.empty() ? Value() : Value(path);
        });
        def.get("pathURI", [](Document& d) {
            d.data();
            const std::string path = d.session.host().path(d.document);
            return path.empty() ? Value() : Value(uri::fromPath(path));
        });
        def.get("library", [](Document& d) {
            d.data();
            return Value(std::make_shared<Library>(d.session, d.document));
        });
        def.get("timelines", [](Document& d) {
            Value list = Value::array();
            for (fla::Timeline* scene : d.data().timelines)
                list.items.emplace_back(wrapTimeline(d.session, d.document, scene));
            return list;
        });
        def.prop("currentTimeline", [](Document& d) { return Value(sceneIndex(d)); },
            [](Document& d, const Value& v) {
                const int index = v.toInt();
                if (index < 0 || index >= static_cast<int>(d.data().timelines.size()))
                    throw Error("There is no scene " + v.toString() + ".");
                d.session.host().editTimeline(d.document, d.data().timelines[static_cast<size_t>(index)]);
            });
        def.method("getTimeline", 0, [](Document& d, const Args&) {
            d.data();
            return Value(wrapTimeline(d.session, d.document, d.session.host().editedTimeline(d.document)));
        });
        def.method("editScene", 1, [](Document& d, const Args& args) {
            const int index = arg(args, 0).toInt();
            if (index < 0 || index >= static_cast<int>(d.data().timelines.size()))
                throw Error("There is no scene " + arg(args, 0).toString() + ".");
            d.session.host().editTimeline(d.document, d.data().timelines[static_cast<size_t>(index)]);
            return Value();
        });

        // The scenes: each change one step of the script's undo, the scene
        // it leaves current then edited.
        def.method("addNewScene", 1, [](Document& d, const Args& args) {
            fla::Document& data = d.data();
            std::string name = given(args, 0) ? arg(args, 0).toString() : std::string();
            if (name.empty())
                name = fla::scenes::freeName(data);
            else if (sceneNamed(data, name))
                return Value(false);
            pushScenes(d, sceneAdded(d, fla::scenes::newScene(data, name)), "Add Scene");
            return Value(true);
        });
        def.method("duplicateScene", 0, [](Document& d, const Args&) {
            fla::Document& data = d.data();
            const fla::Timeline* original = data.timelines[static_cast<size_t>(sceneIndex(d))];
            if (!original)
                return Value(false);
            const std::string name = fla::scenes::freeName(data, original->name + " copy");
            pushScenes(d, sceneAdded(d, fla::scenes::duplicate(*original, data, name)), "Duplicate Scene");
            return Value(true);
        });
        def.method("deleteScene", 0, [](Document& d, const Args&) {
            fla::Document& data = d.data();
            // A document keeps at least one scene.
            if (data.timelines.size() <= 1)
                return Value(false);
            const int current = sceneIndex(d);
            std::vector<fla::Timeline*> scenes = data.timelines;
            scenes.erase(scenes.begin() + current);
            // The next scene becomes current, or the first after the last.
            const int next = current < static_cast<int>(scenes.size()) ? current : 0;
            pushScenes(d, {std::move(scenes), next}, "Delete Scene");
            return Value(true);
        });
        def.method("renameScene", 1, [](Document& d, const Args& args) {
            fla::Document& data = d.data();
            fla::Timeline* scene = data.timelines[static_cast<size_t>(sceneIndex(d))];
            const std::string name = arg(args, 0).toString();
            if (!scene || name.empty())
                return Value(false);
            if (name == scene->name)
                return Value(true);
            if (sceneNamed(data, name))
                return Value(false);
            d.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(scene->name, scene->name, name, "Rename Scene")), false);
            return Value(true);
        });
        def.method("reorderScene", 2, [](Document& d, const Args& args) {
            fla::Document& data = d.data();
            const int count = static_cast<int>(data.timelines.size());
            const int from = arg(args, 0).toInt();
            const int before = given(args, 1) ? arg(args, 1).toInt() : -1;
            if (from < 0 || from >= count)
                throw Error("There is no scene " + arg(args, 0).toString() + ".");
            if (before >= count || before < -1)
                throw Error("There is no scene " + arg(args, 1).toString() + ".");
            if (before == from || before == from + 1 || (before == -1 && from == count - 1))
                return Value();
            std::vector<fla::Timeline*> scenes = data.timelines;
            fla::Timeline* moving = scenes[static_cast<size_t>(from)];
            fla::Timeline* target = before < 0 ? nullptr : scenes[static_cast<size_t>(before)];
            scenes.erase(scenes.begin() + from);
            scenes.insert(target ? std::find(scenes.begin(), scenes.end(), target) : scenes.end(), moving);
            // The scene being edited stays the one being edited.
            const fla::Timeline* current = data.timelines[static_cast<size_t>(sceneIndex(d))];
            const int at = static_cast<int>(std::find(scenes.begin(), scenes.end(), current) - scenes.begin());
            pushScenes(d, {std::move(scenes), at}, "Reorder Scenes");
            return Value();
        });
        def.method("exitEditMode", 0, [](Document& d, const Args&) {
            d.session.host().editTimeline(d.document, d.data().currentScene());
            return Value();
        });
        def.method("enterEditMode", 1, [](Document& d, const Args&) {
            const std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            auto* instance = dynamic_cast<fla::SymbolInstance*>(elements.front());
            if (!instance || !instance->symbol || instance->symbol->timelines.empty())
                throw Error("Select a symbol instance to edit its symbol.");
            d.session.host().editTimeline(d.document, instance->symbol->timelines.front());
            return Value();
        });

        // The selection.
        def.prop("selection",
            [](Document& d) {
                Value list = Value::array();
                for (fla::Element* element : selectedElements(d.session, d.document))
                    list.items.emplace_back(wrapElement(d.session, d.document, element));
                return list;
            },
            [](Document& d, const Value& v) {
                std::vector<fla::DOMElement*> chosen;
                for (const Value& item : v.items)
                {
                    const auto element = item.as<Element>();
                    if (!element)
                        throw Error("The selection takes elements.");
                    chosen.push_back(&element->get());
                }
                d.session.host().selection(d.document).select(chosen);
            });
        def.method("selectAll", 0, [](Document& d, const Args&) {
            std::vector<fla::DOMElement*> chosen;
            fla::Timeline* timeline = d.session.host().editedTimeline(d.document);
            const int frame = d.session.host().currentFrame(d.document);
            for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            {
                if (!layer || layer->isLocked() || !layer->visible || layer->layerType == fla::Layer::Type::Folder)
                    continue;
                if (fla::Frame* key = fla::timeline::spanAt(*layer, frame))
                {
                    for (fla::Element* element : key->elements)
                    {
                        if (element)
                            chosen.push_back(element);
                    }
                }
            }
            d.session.host().selection(d.document).select(chosen);
            return Value();
        });
        def.method("selectNone", 0, [](Document& d, const Args&) {
            d.data();
            d.session.host().selection(d.document).clear();
            return Value();
        });
        def.method("getSelectionRect", 0, [](Document& d, const Args&) {
            const Rect box = selectionBounds(d.session, d.document, needSelection(d.session, d.document));
            return rectValue(box);
        });
        def.method("deleteSelection", 0, [](Document& d, const Args&) {
            fla::Selection& selection = d.session.host().selection(d.document);
            for (fla::Element* element : selectedElements(d.session, d.document))
            {
                if (fla::Frame* frame = frameOf(element))
                    d.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, element, "Delete", &selection)));
            }
            selection.clear();
            return Value();
        });
        def.method("duplicateSelection", 0, [](Document& d, const Args&) {
            fla::Selection& selection = d.session.host().selection(d.document);
            std::vector<fla::DOMElement*> copies;
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                fla::Frame* frame = frameOf(element);
                fla::Element* copy = frame ? fla::cloneElement(*element, frame) : nullptr;
                if (!copy)
                    continue;
                copy->transform.tx += 10;
                copy->transform.ty += 10;
                d.push(fla::CommandPtr(new fla::AddElementCommand(frame, copy, "Duplicate", &selection)));
                copies.push_back(copy);
            }
            selection.select(copies);
            return Value();
        });
        def.method("clipCopy", 0, [](Document& d, const Args&) {
            copyToClipboard(needSelection(d.session, d.document));
            return Value();
        });
        def.method("clipCut", 0, [](Document& d, const Args&) {
            copyToClipboard(needSelection(d.session, d.document));
            fla::Selection& selection = d.session.host().selection(d.document);
            for (fla::Element* element : selectedElements(d.session, d.document))
            {
                if (fla::Frame* frame = frameOf(element))
                    d.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, element, "Cut", &selection)));
            }
            selection.clear();
            return Value();
        });
        def.method("clipPaste", 1, [](Document& d, const Args& args) {
            // In place, or with the pasted objects centered on the stage.
            const auto& copied = elementClipboard();
            if (copied.empty())
                return Value();
            fla::Frame* frame = currentKeyframe(d.session, d.document);
            fla::Selection& selection = d.session.host().selection(d.document);
            std::vector<fla::Element*> pasted;
            for (const auto& element : copied)
            {
                fla::Element* copy = fla::cloneElement(*element, frame);
                if (!copy)
                    continue;
                fla::relinkSymbols(*copy, d.data().symbolList);
                d.push(fla::CommandPtr(new fla::AddElementCommand(frame, copy, "Paste", &selection)));
                pasted.push_back(copy);
            }
            if (!arg(args, 0).toBool())
            {
                const Rect box = selectionBounds(d.session, d.document, pasted);
                if (box.valid())
                {
                    const double dx = d.data().width / 2.0 - (box.left + box.right) / 2;
                    const double dy = d.data().height / 2.0 - (box.top + box.bottom) / 2;
                    for (fla::Element* element : pasted)
                        setTransform(d.session, d.document, element, compose(fla::Transform::fromTranslate(dx, dy), element->transform), "Paste");
                }
            }
            selection.select(std::vector<fla::DOMElement*>(pasted.begin(), pasted.end()));
            return Value();
        });
        def.method("getElementProperty", 1, [](Document& d, const Args& args) {
            const std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            auto wrapper = wrapElement(d.session, d.document, elements.front());
            for (const ClassInfo* info = &wrapper->classInfo(); info; info = info->parent)
            {
                for (const Property& property : info->properties)
                {
                    if (property.name == arg(args, 0).toString())
                        return property.get(*wrapper);
                }
            }
            return Value();
        });
        def.method("setSelectionRect", 2, [](Document& d, const Args& args) {
            // Everything on the current frame inside the rectangle.
            const Value& box = arg(args, 0);
            const Rect area{field(box, "left"), field(box, "top"), field(box, "right"), field(box, "bottom")};
            std::vector<fla::DOMElement*> chosen;
            if (given(args, 1) && !arg(args, 1).toBool())
            {
                for (fla::Element* element : selectedElements(d.session, d.document))
                    chosen.push_back(element);
            }
            fla::Timeline* timeline = d.session.host().editedTimeline(d.document);
            const int frame = d.session.host().currentFrame(d.document);
            for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            {
                fla::Frame* key = layer && !layer->isLocked() && layer->visible ? fla::timeline::spanAt(*layer, frame) : nullptr;
                for (fla::Element* element : key ? key->elements : std::vector<fla::Element*>())
                {
                    const Rect bounds = d.session.host().bounds(d.document, element);
                    if (bounds.valid() && bounds.left >= area.left && bounds.right <= area.right && bounds.top >= area.top &&
                        bounds.bottom <= area.bottom && std::find(chosen.begin(), chosen.end(), element) == chosen.end())
                        chosen.push_back(element);
                }
            }
            d.session.host().selection(d.document).select(chosen);
            return Value();
        });
        def.method("group", 0, [](Document& d, const Args&) {
            std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            fla::Frame* frame = frameOf(elements.front());
            elements.erase(std::remove_if(elements.begin(), elements.end(), [frame](fla::Element* e) { return frameOf(e) != frame; }),
                elements.end());
            if (!frame)
                throw Error("The selection cannot be grouped.");
            d.push(fla::CommandPtr(new fla::GroupElementsCommand(frame, elements, "Group", &d.session.host().selection(d.document))));
            return Value();
        });
        def.method("unGroup", 0, [](Document& d, const Args&) {
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                fla::Frame* frame = frameOf(element);
                if (frame && element->elementType() == fla::Element::Type::Group)
                    d.push(fla::CommandPtr(new fla::UngroupCommand(frame, static_cast<fla::Group*>(element), "Ungroup",
                        &d.session.host().selection(d.document))));
            }
            return Value();
        });
        def.method("breakApart", 0, [](Document& d, const Args&) {
            fla::Selection& selection = d.session.host().selection(d.document);
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                fla::Frame* frame = frameOf(element);
                if (!frame)
                    continue;
                switch (element->elementType())
                {
                case fla::Element::Type::SymbolInstance:
                    d.push(fla::CommandPtr(new fla::BreakApartInstanceCommand(frame, static_cast<fla::SymbolInstance*>(element),
                        "Break Apart", &selection)));
                    break;
                case fla::Element::Type::Group:
                    d.push(fla::CommandPtr(new fla::UngroupCommand(frame, static_cast<fla::Group*>(element), "Break Apart", &selection)));
                    break;
                case fla::Element::Type::Shape:
                    if (static_cast<fla::Shape*>(element)->isDrawingObject)
                        d.push(fla::CommandPtr(new fla::SetDrawingObjectCommand(static_cast<fla::Shape*>(element), false, "Break Apart")));
                    break;
                default:
                    break;
                }
            }
            return Value();
        });
        // traceBitmap(threshold, minimumArea, curveFit, cornerThreshold): each
        // selected bitmap becomes vector artwork, as Modify > Bitmap > Trace
        // Bitmap. The corner threshold is taken but not used.
        def.method("traceBitmap", 4, [](Document& d, const Args& args) {
            fla::trace::Options options;
            options.colorThreshold = std::clamp(given(args, 0) ? arg(args, 0).toInt() : 100, 0, 500);
            options.minimumArea = std::clamp(given(args, 1) ? arg(args, 1).toInt() : 8, 1, 1000);
            const std::string fit = given(args, 2) ? arg(args, 2).toString() : "normal";
            const auto found = std::find_if(std::begin(fla::trace::kCurveFits), std::end(fla::trace::kCurveFits),
                [&fit](const fla::trace::CurveFit& each) { return fit == each.name; });
            if (found == std::end(fla::trace::kCurveFits))
                throw Error("The curve fit must be \"pixels\", \"very tight\", \"tight\", \"normal\", \"smooth\" or \"very smooth\".");
            options.curveFit = found->tolerance;
            fla::Selection& selection = d.session.host().selection(d.document);
            std::vector<fla::DOMElement*> traced;
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                fla::Frame* frame = frameOf(element);
                if (!frame || element->elementType() != fla::Element::Type::BitmapInstance)
                    continue;
                auto* bitmap = static_cast<fla::BitmapInstance*>(element);
                int width = 0;
                int height = 0;
                std::vector<uint8_t> rgba;
                if (!d.session.host().bitmapPixels(d.document, bitmap->libraryItemName, width, height, rgba))
                    throw Error("The bitmap " + bitmap->libraryItemName + " could not be read.");
                fla::Shape* shape = fla::trace::traceBitmap(rgba.data(), width, height, options);
                if (!shape)
                    continue;
                shape->parent = frame;
                shape->transform = bitmap->transform;
                const auto at = std::find(frame->elements.begin(), frame->elements.end(), element);
                const int index = at == frame->elements.end() ? -1 : static_cast<int>(at - frame->elements.begin());
                d.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, element, "Trace Bitmap", &selection)));
                d.push(fla::CommandPtr(new fla::AddElementCommand(frame, shape, "Trace Bitmap", &selection, index)));
                traced.push_back(shape);
            }
            if (!traced.empty())
                selection.select(traced);
            return Value();
        });
        def.method("convertToSymbol", 3, [](Document& d, const Args& args) {
            const std::string type = arg(args, 0).toString();
            const std::string name = arg(args, 1).isNullish() ? std::string() : arg(args, 1).toString();
            std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            fla::Frame* frame = frameOf(elements.front());
            elements.erase(std::remove_if(elements.begin(), elements.end(), [frame](fla::Element* e) { return frameOf(e) != frame; }),
                elements.end());
            if (!frame)
                throw Error("The selection cannot be made into a symbol.");
            fla::Document& data = d.data();
            std::string symbolName = name;
            for (int n = 1; symbolName.empty() || (name.empty() && !fla::library::nameIsFree(data, symbolName)); ++n)
                symbolName = "Symbol " + std::to_string(n);
            if (!fla::library::nameIsFree(data, symbolName))
                throw Error("The library already has an item called " + symbolName + ".");
            const fla::SymbolType symbolType = type == "movie clip" ? fla::SymbolType::MovieClip
                : type == "button"                               ? fla::SymbolType::Button
                                                                 : fla::SymbolType::Graphic;
            double x = 0.0;
            double y = 0.0;
            const Rect box = selectionBounds(d.session, d.document, elements);
            if (!box.valid() || !namedPoint(box, given(args, 2) ? arg(args, 2).toString() : "center", x, y))
            {
                x = (box.left + box.right) / 2;
                y = (box.top + box.bottom) / 2;
            }
            auto* command = new fla::ConvertToSymbolCommand(&data, frame, elements, symbolName, x, y, "Convert to Symbol",
                &d.session.host().selection(d.document), symbolType);
            d.push(fla::CommandPtr(command));
            if (command->instance())
                d.session.host().selection(d.document).select(command->instance());
            return Value(wrapSymbol(d.session, d.document, command->symbol()));
        });
        def.method("swapElement", 1, [](Document& d, const Args& args) {
            const auto item = findItem(d.session, d.document, arg(args, 0).toString());
            if (!item || !item->symbol())
                throw Error("There is no symbol called " + arg(args, 0).toString() + " in the library.");
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                auto* instance = dynamic_cast<fla::SymbolInstance*>(element);
                if (!instance)
                    continue;
                fla::Symbol* symbol = item->symbol();
                d.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(instance->libraryItemName,
                    instance->libraryItemName, symbol->name, "Swap Symbol")), false);
                d.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Symbol*>(instance->symbol, instance->symbol, symbol,
                    "Swap Symbol")), false);
            }
            return Value();
        });

        // Arranging and transforming the selection.
        def.method("arrange", 1, [](Document& d, const Args& args) {
            const std::string mode = arg(args, 0).toString();
            std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            // Forward and to the front start from the top, so each move
            // leaves the next one's place alone.
            const bool up = mode == "forward" || mode == "front";
            const auto indexOf = [](fla::Element* e) {
                fla::Frame* frame = frameOf(e);
                return frame ? static_cast<int>(std::find(frame->elements.begin(), frame->elements.end(), e) - frame->elements.begin()) : -1;
            };
            std::sort(elements.begin(), elements.end(), [&](fla::Element* a, fla::Element* b) {
                return up ? indexOf(a) > indexOf(b) : indexOf(a) < indexOf(b);
            });
            int placed = 0;
            for (fla::Element* element : elements)
            {
                fla::Frame* frame = frameOf(element);
                const int at = indexOf(element);
                if (!frame || at < 0)
                    continue;
                const int last = static_cast<int>(frame->elements.size()) - 1;
                int to = at;
                if (mode == "back")
                    to = placed++;
                else if (mode == "front")
                    to = last - placed++;
                else if (mode == "backward")
                    to = std::max(0, at - 1);
                else if (mode == "forward")
                    to = std::min(last, at + 1);
                else
                    throw Error("arrange takes back, backward, forward or front, not " + mode + ".");
                if (to != at)
                    d.push(fla::CommandPtr(new fla::ReorderElementCommand(frame, element, to, "Arrange")));
            }
            return Value();
        });
        def.method("align", 2, [](Document& d, const Args& args) {
            const std::string mode = arg(args, 0).toString();
            const std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            const Rect area = useStage(args, 1) ? Rect{0.0, 0.0, static_cast<double>(d.data().width), static_cast<double>(d.data().height)}
                                                    : selectionBounds(d.session, d.document, elements);
            for (fla::Element* element : elements)
            {
                const Rect box = d.session.host().bounds(d.document, element);
                if (!box.valid())
                    continue;
                double dx = 0.0;
                double dy = 0.0;
                if (mode == "left")
                    dx = area.left - box.left;
                else if (mode == "right")
                    dx = area.right - box.right;
                else if (mode == "top")
                    dy = area.top - box.top;
                else if (mode == "bottom")
                    dy = area.bottom - box.bottom;
                else if (mode == "vertical center")
                    dx = (area.left + area.right) / 2 - (box.left + box.right) / 2;
                else if (mode == "horizontal center")
                    dy = (area.top + area.bottom) / 2 - (box.top + box.bottom) / 2;
                else
                    throw Error("align takes left, right, top, bottom, vertical center or horizontal center, not " + mode + ".");
                if (dx != 0.0 || dy != 0.0)
                    setTransform(d.session, d.document, element, compose(fla::Transform::fromTranslate(dx, dy), element->transform), "Align");
            }
            return Value();
        });
        def.method("distribute", 2, [](Document& d, const Args& args) {
            const std::string mode = arg(args, 0).toString();
            std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            const bool horizontal = mode.find("left") != std::string::npos || mode.find("right") != std::string::npos ||
                                    mode == "horizontal center";
            const auto key = [&](const Rect& box) {
                if (mode == "left edge")
                    return box.left;
                if (mode == "right edge")
                    return box.right;
                if (mode == "top edge")
                    return box.top;
                if (mode == "bottom edge")
                    return box.bottom;
                if (mode == "horizontal center")
                    return (box.left + box.right) / 2;
                if (mode == "vertical center")
                    return (box.top + box.bottom) / 2;
                throw Error("distribute takes left edge, horizontal center, right edge, top edge, vertical center or bottom edge, not " + mode + ".");
            };
            std::vector<std::pair<double, fla::Element*>> placed;
            for (fla::Element* element : elements)
            {
                const Rect box = d.session.host().bounds(d.document, element);
                if (box.valid())
                    placed.emplace_back(key(box), element);
            }
            if (placed.size() < 2)
                return Value();
            std::sort(placed.begin(), placed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            double first = placed.front().first;
            double last = placed.back().first;
            if (useStage(args, 1))
            {
                first = 0.0;
                last = horizontal ? d.data().width : d.data().height;
            }
            const double step = (last - first) / static_cast<double>(placed.size() - 1);
            for (size_t i = 0; i < placed.size(); ++i)
            {
                const double move = first + step * static_cast<double>(i) - placed[i].first;
                if (move == 0.0)
                    continue;
                fla::Element* element = placed[i].second;
                setTransform(d.session, d.document, element,
                    compose(fla::Transform::fromTranslate(horizontal ? move : 0.0, horizontal ? 0.0 : move), element->transform), "Distribute");
            }
            return Value();
        });
        def.method("moveSelectionBy", 1, [](Document& d, const Args& args) {
            const double dx = field(arg(args, 0), "x");
            const double dy = field(arg(args, 0), "y");
            for (fla::Element* element : needSelection(d.session, d.document))
                setTransform(d.session, d.document, element, compose(fla::Transform::fromTranslate(dx, dy), element->transform), "Move");
            return Value();
        });
        def.method("scaleSelection", 3, [](Document& d, const Args& args) {
            const std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            const Rect box = selectionBounds(d.session, d.document, elements);
            double x = (box.left + box.right) / 2;
            double y = (box.top + box.bottom) / 2;
            if (given(args, 2))
                namedPoint(box, arg(args, 2).toString(), x, y);
            transformAll(d.session, d.document, elements,
                fla::Transform::fromScale(given(args, 0) ? arg(args, 0).toNumber() : 1.0, given(args, 1) ? arg(args, 1).toNumber() : 1.0),
                x, y, "Scale");
            return Value();
        });
        def.method("rotateSelection", 2, [](Document& d, const Args& args) {
            const std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            const Rect box = selectionBounds(d.session, d.document, elements);
            double x = (box.left + box.right) / 2;
            double y = (box.top + box.bottom) / 2;
            if (given(args, 1))
                namedPoint(box, arg(args, 1).toString(), x, y);
            const double angle = arg(args, 0).toNumber() * kPi / 180.0;
            transformAll(d.session, d.document, elements,
                fla::Transform(std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), 0.0, 0.0), x, y, "Rotate");
            return Value();
        });
        def.method("transformSelection", 4, [](Document& d, const Args& args) {
            const std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            const Rect box = selectionBounds(d.session, d.document, elements);
            transformAll(d.session, d.document, elements,
                fla::Transform(arg(args, 0).toNumber(), arg(args, 1).toNumber(), arg(args, 2).toNumber(), arg(args, 3).toNumber(), 0.0, 0.0),
                (box.left + box.right) / 2, (box.top + box.bottom) / 2, "Transform");
            return Value();
        });
        def.method("setElementProperty", 2, [](Document& d, const Args& args) {
            setOnSelection(d, arg(args, 0).toString(), arg(args, 1));
            return Value();
        });
        def.method("setInstanceAlpha", 1, [](Document& d, const Args& args) {
            setOnSelection(d, "colorMode", Value("alpha"));
            setOnSelection(d, "colorAlphaPercent", arg(args, 0));
            return Value();
        });
        def.method("setInstanceBrightness", 1, [](Document& d, const Args& args) {
            setOnSelection(d, "colorMode", Value("brightness"));
            setOnSelection(d, "brightness", arg(args, 0));
            return Value();
        });
        def.method("setInstanceTint", 2, [](Document& d, const Args& args) {
            setOnSelection(d, "colorMode", Value("tint"));
            setOnSelection(d, "tintColor", arg(args, 0));
            setOnSelection(d, "tintPercent", arg(args, 1));
            return Value();
        });
        def.method("getAlignToDocument", 0, [](Document& d, const Args&) {
            d.data();
            return Value(alignToStage());
        });
        def.method("setAlignToDocument", 1, [](Document& d, const Args& args) {
            d.data();
            alignToStage() = arg(args, 0).toBool();
            return Value();
        });
        def.method("space", 2, [](Document& d, const Args& args) {
            const std::string direction = arg(args, 0).toString();
            if (direction != "horizontal" && direction != "vertical")
                throw Error("space takes horizontal or vertical, not " + direction + ".");
            const bool horizontal = direction == "horizontal";
            std::vector<std::pair<Rect, fla::Element*>> placed;
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                const Rect box = d.session.host().bounds(d.document, element);
                if (box.valid())
                    placed.emplace_back(box, element);
            }
            if (placed.size() < 2)
                return Value();
            const auto start = [horizontal](const Rect& box) { return horizontal ? box.left : box.top; };
            const auto size = [horizontal](const Rect& box) { return horizontal ? box.width() : box.height(); };
            std::sort(placed.begin(), placed.end(), [&start](const auto& a, const auto& b) { return start(a.first) < start(b.first); });
            // Equal gaps between them, across the stage or across the
            // space they already take up.
            double from = start(placed.front().first);
            double to = horizontal ? placed.back().first.right : placed.back().first.bottom;
            if (useStage(args, 1))
            {
                from = 0.0;
                to = horizontal ? d.data().width : d.data().height;
            }
            double total = 0.0;
            for (const auto& each : placed)
                total += size(each.first);
            const double gap = (to - from - total) / static_cast<double>(placed.size() - 1);
            double at = from;
            for (const auto& [box, element] : placed)
            {
                const double move = at - start(box);
                at += size(box) + gap;
                if (move != 0.0)
                    setTransform(d.session, d.document, element,
                        compose(fla::Transform::fromTranslate(horizontal ? move : 0.0, horizontal ? 0.0 : move), element->transform), "Space");
            }
            return Value();
        });
        def.method("match", 3, [](Document& d, const Args& args) {
            const bool width = arg(args, 0).toBool();
            const bool height = arg(args, 1).toBool();
            std::vector<std::pair<Rect, fla::Element*>> placed;
            double wantWidth = 0.0;
            double wantHeight = 0.0;
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                const Rect box = d.session.host().bounds(d.document, element);
                if (!box.valid())
                    continue;
                placed.emplace_back(box, element);
                wantWidth = std::max(wantWidth, box.width());
                wantHeight = std::max(wantHeight, box.height());
            }
            // The largest of them, or the stage.
            if (useStage(args, 2))
            {
                wantWidth = d.data().width;
                wantHeight = d.data().height;
            }
            for (const auto& [box, element] : placed)
            {
                const double sx = width && box.width() > 0.0 ? wantWidth / box.width() : 1.0;
                const double sy = height && box.height() > 0.0 ? wantHeight / box.height() : 1.0;
                if (sx != 1.0 || sy != 1.0)
                    transformAll(d.session, d.document, {element}, fla::Transform::fromScale(sx, sy), box.left, box.top, "Match Size");
            }
            return Value();
        });
        def.method("invertSelection", 0, [](Document& d, const Args&) {
            fla::Selection& selection = d.session.host().selection(d.document);
            std::vector<fla::DOMElement*> chosen;
            fla::Timeline* timeline = d.session.host().editedTimeline(d.document);
            const int frame = d.session.host().currentFrame(d.document);
            for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            {
                if (!layer || layer->isLocked() || !layer->visible || layer->layerType == fla::Layer::Type::Folder)
                    continue;
                if (fla::Frame* key = fla::timeline::spanAt(*layer, frame))
                    for (fla::Element* element : key->elements)
                        if (element && !element->isLocked && !selection.contains(element))
                            chosen.push_back(element);
            }
            selection.select(chosen);
            return Value();
        });
        def.method("unlockAllElements", 0, [](Document& d, const Args&) {
            fla::Timeline* timeline = d.session.host().editedTimeline(d.document);
            const int frame = d.session.host().currentFrame(d.document);
            for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            {
                fla::Frame* key = layer ? fla::timeline::spanAt(*layer, frame) : nullptr;
                for (fla::Element* element : key ? key->elements : std::vector<fla::Element*>())
                    if (element && element->isLocked)
                        d.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(element->isLocked, element->isLocked, false, "Unlock All")), false);
            }
            return Value();
        });
        def.method("skewSelection", 3, [](Document& d, const Args& args) {
            const std::vector<fla::Element*> elements = needSelection(d.session, d.document);
            const Rect box = selectionBounds(d.session, d.document, elements);
            double x = (box.left + box.right) / 2;
            double y = (box.top + box.bottom) / 2;
            if (given(args, 2))
                namedPoint(box, arg(args, 2).toString(), x, y);
            // A skew along x shifts each point across by its height, one
            // along y shifts it down by its distance across.
            const double xSkew = std::tan(arg(args, 0).toNumber() * kPi / 180.0);
            const double ySkew = std::tan(arg(args, 1).toNumber() * kPi / 180.0);
            transformAll(d.session, d.document, elements, fla::Transform(1.0, ySkew, xSkew, 1.0, 0.0, 0.0), x, y, "Skew");
            return Value();
        });
        def.method("resetTransformation", 0, [](Document& d, const Args&) {
            // Scale, rotation and skew go; the registration point stays.
            for (fla::Element* element : needSelection(d.session, d.document))
                setTransform(d.session, d.document, element,
                    fla::Transform(1.0, 0.0, 0.0, 1.0, element->transform.tx, element->transform.ty), "Remove Transform");
            return Value();
        });
        def.method("distributeToLayers", 0, [](Document& d, const Args&) {
            fla::Timeline* timeline = d.session.host().editedTimeline(d.document);
            if (!timeline)
                return Value();
            fla::Selection& selection = d.session.host().selection(d.document);
            std::vector<fla::CommandPtr> commands = fla::layers::distributeToLayers(*timeline, d.session.host().currentLayer(d.document),
                needSelection(d.session, d.document), d.data().symbolList, &selection);
            selection.clear();
            for (fla::CommandPtr& command : commands)
                d.push(std::move(command));
            return Value();
        });

        // Text fields, through the selection.
        def.method("getTextString", 2, [](Document& d, const Args& args) { return callOnSelection(d, "getTextString", args); });
        def.method("setTextString", 3, [](Document& d, const Args& args) {
            callOnSelection(d, "setTextString", args);
            return Value(true);
        });
        def.method("setElementTextAttr", 4, [](Document& d, const Args& args) {
            callOnSelection(d, "setTextAttr", args);
            return Value(true);
        });

        // Blend modes and filters of the selected symbol instances.
        def.method("setBlendMode", 1, [](Document& d, const Args& args) {
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                auto wrapper = wrapElement(d.session, d.document, element);
                if (const Property* property = findProperty(*wrapper, "blendMode"))
                    property->set(*wrapper, arg(args, 0));
            }
            return Value();
        });
        def.method("getFilters", 0, [](Document& d, const Args&) {
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                auto wrapper = wrapElement(d.session, d.document, element);
                if (const Property* property = findProperty(*wrapper, "filters"))
                    return property->get(*wrapper);
            }
            return Value::array();
        });
        def.method("setFilters", 1, [](Document& d, const Args& args) {
            const Value filters = arg(args, 0);
            editFilters(d, [&filters](std::vector<Value>& list) { list = filters.items; });
            return Value();
        });
        def.method("addFilter", 1, [](Document& d, const Args& args) {
            Value filter = Value::map();
            filter.set("name", arg(args, 0).toString());
            editFilters(d, [&filter](std::vector<Value>& list) { list.push_back(filter); });
            return Value();
        });
        def.method("removeAllFilters", 0, [](Document& d, const Args&) {
            editFilters(d, [](std::vector<Value>& list) { list.clear(); });
            return Value();
        });
        def.method("removeFilter", 1, [](Document& d, const Args& args) {
            editFilters(d, [&args](std::vector<Value>& list) {
                filterAt(list, arg(args, 0));
                list.erase(list.begin() + arg(args, 0).toInt());
            });
            return Value();
        });
        def.method("setFilterProperty", 3, [](Document& d, const Args& args) {
            editFilters(d, [&args](std::vector<Value>& list) { filterAt(list, arg(args, 1)).set(arg(args, 0).toString(), arg(args, 2)); });
            return Value();
        });
        def.method("enableFilter", 1, [](Document& d, const Args& args) {
            editFilters(d, [&args](std::vector<Value>& list) { filterAt(list, arg(args, 0)).set("enabled", true); });
            return Value();
        });
        def.method("disableFilter", 1, [](Document& d, const Args& args) {
            editFilters(d, [&args](std::vector<Value>& list) { filterAt(list, arg(args, 0)).set("enabled", false); });
            return Value();
        });
        def.method("enableAllFilters", 0, [](Document& d, const Args&) {
            editFilters(d, [](std::vector<Value>& list) {
                for (Value& filter : list)
                    filter.set("enabled", true);
            });
            return Value();
        });
        def.method("disableAllFilters", 0, [](Document& d, const Args&) {
            editFilters(d, [](std::vector<Value>& list) {
                for (Value& filter : list)
                    filter.set("enabled", false);
            });
            return Value();
        });
        def.method("disableOtherFilters", 1, [](Document& d, const Args& args) {
            editFilters(d, [&args](std::vector<Value>& list) {
                filterAt(list, arg(args, 0));
                for (size_t i = 0; i < list.size(); ++i)
                    list[i].set("enabled", static_cast<int>(i) == arg(args, 0).toInt());
            });
            return Value();
        });
        def.method("changeFilterOrder", 2, [](Document& d, const Args& args) {
            editFilters(d, [&args](std::vector<Value>& list) {
                const Value moving = filterAt(list, arg(args, 0));
                filterAt(list, arg(args, 1));
                list.erase(list.begin() + arg(args, 0).toInt());
                list.insert(list.begin() + arg(args, 1).toInt(), moving);
            });
            return Value();
        });

        // The fill and stroke new shapes get (the toolbar's).
        def.method("getCustomFill", 1, [](Document& d, const Args&) {
            const fla::DrawingStyle& style = toolbarStyle(d);
            Value fill = Value::map();
            fill.set("style", style.hasFill ? "solid" : "noFill");
            fill.set("color", colorText(style.fillColor));
            return fill;
        });
        def.method("getCustomStroke", 1, [](Document& d, const Args&) {
            const fla::DrawingStyle& style = toolbarStyle(d);
            Value stroke = Value::map();
            stroke.set("style", style.hasStroke ? "solid" : "noStroke");
            stroke.set("color", colorText(style.strokeColor));
            stroke.set("thickness", style.strokeWeight);
            return stroke;
        });
        def.method("setCustomFill", 1, [](Document& d, const Args& args) {
            fla::DrawingStyle& style = toolbarStyle(d);
            if (const Value* kind = arg(args, 0).field("style"))
                style.hasFill = kind->toString() != "noFill";
            if (const Value* color = arg(args, 0).field("color"))
                parseColor(*color, style.fillColor);
            return Value();
        });
        def.method("setCustomStroke", 1, [](Document& d, const Args& args) {
            fla::DrawingStyle& style = toolbarStyle(d);
            if (const Value* kind = arg(args, 0).field("style"))
                style.hasStroke = kind->toString() != "noStroke";
            if (const Value* color = arg(args, 0).field("color"))
                parseColor(*color, style.strokeColor);
            if (const Value* thickness = arg(args, 0).field("thickness"))
                style.strokeWeight = std::max(0.1, thickness->toNumber());
            return Value();
        });
        def.method("setFillColor", 1, [](Document& d, const Args& args) {
            fla::DrawingStyle& style = toolbarStyle(d);
            style.hasFill = !arg(args, 0).isNullish();
            if (style.hasFill && !parseColor(arg(args, 0), style.fillColor))
                throw Error("That is not a color: " + arg(args, 0).toString());
            return Value();
        });
        def.method("setStrokeColor", 1, [](Document& d, const Args& args) {
            fla::DrawingStyle& style = toolbarStyle(d);
            style.hasStroke = !arg(args, 0).isNullish();
            if (style.hasStroke && !parseColor(arg(args, 0), style.strokeColor))
                throw Error("That is not a color: " + arg(args, 0).toString());
            return Value();
        });
        def.method("setStrokeSize", 1, [](Document& d, const Args& args) {
            toolbarStyle(d).strokeWeight = std::max(0.1, arg(args, 0).toNumber());
            return Value();
        });
        def.method("swapStrokeAndFill", 0, [](Document& d, const Args&) {
            fla::DrawingStyle& style = toolbarStyle(d);
            std::swap(style.hasFill, style.hasStroke);
            for (int i = 0; i < 4; ++i)
                std::swap(style.fillColor[i], style.strokeColor[i]);
            return Value();
        });
        def.method("getTransformationPoint", 0, [](Document& d, const Args&) {
            const fla::Point& point = needSelection(d.session, d.document).front()->transformationPoint;
            Value value = Value::map();
            value.set("x", point.x);
            value.set("y", point.y);
            return value;
        });
        def.method("setTransformationPoint", 1, [](Document& d, const Args& args) {
            for (fla::Element* element : needSelection(d.session, d.document))
            {
                fla::Point point = element->transformationPoint;
                if (const Value* x = arg(args, 0).field("x"))
                    point.x = x->toNumber();
                if (const Value* y = arg(args, 0).field("y"))
                    point.y = y->toNumber();
                d.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Point>(element->transformationPoint,
                           element->transformationPoint, point, "Transformation Point")),
                    false);
            }
            return Value();
        });
        def.method("addNewText", 2, [](Document& d, const Args& args) {
            // A text block over the rectangle, in the toolbar's fill color.
            fla::Frame* frame = currentKeyframe(d.session, d.document);
            auto* text = new fla::StaticText(frame);
            const Value& box = arg(args, 0);
            text->transform.tx = field(box, "left");
            text->transform.ty = field(box, "top");
            text->width = std::max(1.0, field(box, "right") - field(box, "left"));
            text->height = std::max(1.0, field(box, "bottom") - field(box, "top"));
            text->autoExpand = !box.field("right");
            fla::TextRun run;
            run.text = given(args, 1) ? arg(args, 1).toString() : std::string();
            std::replace(run.text.begin(), run.text.end(), '\n', '\r');
            run.face = "Arial";
            run.size = 12.0;
            std::copy(toolbarStyle(d).fillColor, toolbarStyle(d).fillColor + 4, run.fillColor);
            text->runs.push_back(run);
            fla::Selection& selection = d.session.host().selection(d.document);
            d.push(fla::CommandPtr(new fla::AddElementCommand(frame, text, "Text", &selection)));
            selection.select(text);
            return Value();
        });

        // New shapes.
        def.method("addNewRectangle", 4, [](Document& d, const Args& args) {
            const Value& box = arg(args, 0);
            double left = field(box, "left");
            double top = field(box, "top");
            double right = field(box, "right");
            double bottom = field(box, "bottom");
            if (right < left)
                std::swap(left, right);
            if (bottom < top)
                std::swap(top, bottom);
            const double radius = std::clamp(arg(args, 1).toNumber(), 0.0, std::min(right - left, bottom - top) / 2);
            std::string path;
            if (radius > 0.0)
            {
                const std::string r = number(radius);
                path = "M " + number(left + radius) + " " + number(top) + " L " + number(right - radius) + " " + number(top) +
                       " A " + r + " " + r + " 0 0 1 " + number(right) + " " + number(top + radius) + " L " + number(right) + " " +
                       number(bottom - radius) + " A " + r + " " + r + " 0 0 1 " + number(right - radius) + " " + number(bottom) +
                       " L " + number(left + radius) + " " + number(bottom) + " A " + r + " " + r + " 0 0 1 " + number(left) + " " +
                       number(bottom - radius) + " L " + number(left) + " " + number(top + radius) + " A " + r + " " + r + " 0 0 1 " +
                       number(left + radius) + " " + number(top) + " Z";
            }
            else
                path = "M " + number(left) + " " + number(top) + " L " + number(right) + " " + number(top) + " L " + number(right) +
                       " " + number(bottom) + " L " + number(left) + " " + number(bottom) + " Z";
            addShape(d, path, !arg(args, 2).toBool(), !arg(args, 3).toBool());
            return Value();
        });
        def.method("addNewOval", 3, [](Document& d, const Args& args) {
            const Value& box = arg(args, 0);
            const double left = field(box, "left");
            const double top = field(box, "top");
            const double right = field(box, "right");
            const double bottom = field(box, "bottom");
            const double rx = std::fabs(right - left) / 2;
            const double ry = std::fabs(bottom - top) / 2;
            const double cx = (left + right) / 2;
            const double cy = (top + bottom) / 2;
            const std::string r = number(rx) + " " + number(ry);
            const std::string path = "M " + number(cx + rx) + " " + number(cy) + " A " + r + " 0 0 1 " + number(cx) + " " +
                                      number(cy + ry) + " A " + r + " 0 0 1 " + number(cx - rx) + " " + number(cy) + " A " + r +
                                      " 0 0 1 " + number(cx) + " " + number(cy - ry) + " A " + r + " 0 0 1 " + number(cx + rx) +
                                      " " + number(cy) + " Z";
            addShape(d, path, !arg(args, 1).toBool(), !arg(args, 2).toBool());
            return Value();
        });
        def.method("addNewLine", 2, [](Document& d, const Args& args) {
            const std::string path = "M " + number(field(arg(args, 0), "x")) + " " + number(field(arg(args, 0), "y")) + " L " +
                                     number(field(arg(args, 1), "x")) + " " + number(field(arg(args, 1), "y"));
            addShape(d, path, false, true);
            return Value();
        });
        // Phoenix's own: a shape from SVG path data (M, L, Q, C, A, Z), in
        // the toolbar's fill and stroke, as a JSFL tool's path.makeShape()
        // adds one.
        def.method("addNewPath", 3, [](Document& d, const Args& args) {
            const std::string path = arg(args, 0).toString();
            if (path.empty())
                throw Error("addNewPath needs SVG path data.");
            addShape(d, path, given(args, 1) ? arg(args, 1).toBool() : true, given(args, 2) ? arg(args, 2).toBool() : true);
            return Value();
        });
        def.method("addItem", 2, [](Document& d, const Args& args) {
            const auto item = arg(args, 1).as<Item>();
            if (!item)
                throw Error("addItem takes a library item.");
            placeItem(d.session, d.document, *item, field(arg(args, 0), "x"), field(arg(args, 0), "y"));
            return Value(true);
        });

        // Files.
        def.method("save", 1, [](Document& d, const Args& args) {
            d.data();
            Session& session = d.session;
            std::string path;
            if (session.host().path(d.document).empty())
            {
                if (given(args, 0) && !arg(args, 0).toBool())
                    return Value(false);
                path = session.host().browseForFile("save", "Save Document");
                if (path.empty())
                    return Value(false);
            }
            session.beforeSwitch();
            std::string error;
            if (!session.host().save(d.document, path, false, error))
                throw Error(error.empty() ? "The document could not be saved." : error);
            return Value(true);
        });
        def.method("saveAndCompact", 1, [](Document& d, const Args&) {
            d.data();
            if (d.session.host().path(d.document).empty())
                return Value(false);
            d.session.beforeSwitch();
            std::string error;
            if (!d.session.host().save(d.document, std::string(), false, error))
                throw Error(error.empty() ? "The document could not be saved." : error);
            return Value(true);
        });
        def.method("saveAsCopy", 1, [](Document& d, const Args& args) {
            d.data();
            d.session.beforeSwitch();
            std::string error;
            if (!d.session.host().save(d.document, uri::toPath(arg(args, 0).toString()), true, error))
                throw Error(error.empty() ? "The copy could not be saved." : error);
            return Value(true);
        });
        def.method("close", 1, [](Document& d, const Args& args) {
            d.data();
            d.session.beforeSwitch();
            const bool closed = d.session.host().close(d.document, given(args, 0) ? arg(args, 0).toBool() : true);
            if (closed)
                d.session.forget(d.document);
            return Value(closed);
        });
        def.method("exportPNG", 3, [](Document& d, const Args& args) {
            d.data();
            std::string path = given(args, 0) ? uri::toPath(arg(args, 0).toString()) : std::string();
            if (path.empty())
            {
                path = d.session.host().browseForFile("save", "Export PNG");
                if (path.empty())
                    return Value(false);
            }
            if (d.document != d.session.host().activeDocument())
                d.session.beforeSwitch();
            std::string error;
            if (!d.session.host().exportPNG(d.document, path, arg(args, 2).toBool(), error))
                throw Error(error.empty() ? "The PNG could not be written." : error);
            return Value(true);
        });
        def.method("importFile", 4, [](Document& d, const Args& args) {
            d.data();
            const std::string path = uri::toPath(arg(args, 0).toString());
            std::error_code missing;
            if (path.empty() || !std::filesystem::is_regular_file(std::filesystem::u8path(path), missing))
                return Value(false);
            if (d.document != d.session.host().activeDocument())
            {
                d.session.beforeSwitch();
                d.session.host().activate(d.document);
            }
            // What the import pushes is part of the script's undo step.
            d.session.beginStep(d.document);
            std::string error;
            const bool imported = d.session.host().importFile(d.document, path, arg(args, 1).toBool(), error);
            d.session.invalidate(d.document);
            if (!imported && !error.empty())
                d.session.host().trace(error);
            return Value(imported);
        });
        def.method("xmlPanel", 1, [](Document& d, const Args& args) {
            return showXmlPanelFile(d.session, arg(args, 0).toString());
        });
        def.method("xmlPanelFromString", 1, [](Document& d, const Args& args) {
            return showXmlPanel(d.session, arg(args, 0).toString());
        });
        describeDocumentData(def);
        describeDocumentPublishing(def);
        return info;
    }();
    return info;
}

fla::Frame* currentKeyframe(Session& session, fla::FLADocument* document)
{
    session.data(document);
    fla::Timeline* timeline = session.host().editedTimeline(document);
    if (!timeline || timeline->layers.empty())
        throw Error("There is no layer to put it on.");
    const int index = std::clamp(session.host().currentLayer(document), 0, static_cast<int>(timeline->layers.size()) - 1);
    fla::Layer* layer = timeline->layers[static_cast<size_t>(index)];
    if (!layer || layer->layerType == fla::Layer::Type::Folder)
        throw Error("The current layer is a folder; choose a layer to put it on.");
    fla::Frame* frame = fla::timeline::spanAt(*layer, session.host().currentFrame(document));
    if (!frame)
        throw Error("The current layer has no frame at the playhead.");
    return frame;
}

std::vector<fla::Element*> selectedElements(Session& session, fla::FLADocument* document)
{
    session.data(document);
    std::vector<fla::Element*> elements;
    for (fla::DOMElement* selected : session.host().selection(document).elements())
    {
        auto* element = dynamic_cast<fla::Element*>(selected);
        if (element && session.alive(document, element))
            elements.push_back(element);
    }
    return elements;
}

void setTransform(Session& session, fla::FLADocument* document, fla::Element* element, const fla::Transform& after,
    const char* name)
{
    const fla::Transform before = element->transform;
    if (before.m11 == after.m11 && before.m12 == after.m12 && before.m21 == after.m21 && before.m22 == after.m22 &&
        before.tx == after.tx && before.ty == after.ty)
        return;
    session.push(document, fla::CommandPtr(new fla::SetElementTransformCommand(element, before, after, name)), false);
}

fla::Transform compose(const fla::Transform& a, const fla::Transform& b)
{
    fla::Transform result = a;
    result.concatenate(b);
    return result;
}

} // namespace jsfl
