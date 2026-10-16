#include "script_api.h"

#include "document_view.h"
#include "draw_placement.h"
#include "main_window.h"
#include "phoenix_view.h"
#include "timeline_view.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/folder.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "audio_engine.h"
#include "timeline_audio.h"

#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/command_stack.h"
#include "../edit/adjustments.h"
#include "../edit/node_graph.h"
#include "../edit/behaviors.h"
#include "../edit/shape_operators.h"
#include "../edit/templates.h"
#include "../edit/text_animators.h"
#include "behavior_library.h"
#include "../edit/element_clone.h"
#include "../edit/frame_bake.h"
#include "../edit/lip_sync.h"
#include "../edit/element_commands.h"
#include "../edit/layer_commands.h"
#include "../edit/library_commands.h"
#include "../edit/property_command.h"
#include "../edit/timeline_edit.h"
#include "../edit/vector_import.h"
#include "../parser/fla_doctor.h"

#include <QBuffer>
#include <QRandomGenerator>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {

/// An operation's failure, in words for the one who asked.
struct ScriptError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const QString& message)
{
    throw ScriptError(message.toStdString());
}

QJsonObject prop(const QString& type, const QString& description)
{
    return QJsonObject{{"type", type}, {"description", description}};
}

QJsonObject anyOf(const QString& description)
{
    return QJsonObject{{"description", description},
        {"anyOf", QJsonArray{QJsonObject{{"type", "string"}}, QJsonObject{{"type", "integer"}}}}};
}

QJsonObject choice(const QStringList& values, const QString& description)
{
    return QJsonObject{{"type", "string"}, {"enum", QJsonArray::fromStringList(values)}, {"description", description}};
}

const QJsonObject kSymbolArg = prop("string", "A library symbol to work inside; the scene shown when left out");
const QJsonObject kLayerArg = anyOf("A layer's name, or its index counting from 0 at the top");
const QJsonObject kFrameArg = prop("integer", "A frame number, counting from 1");

QString colorText(const uint8_t rgba[4])
{
    QString text = QString("#%1%2%3").arg(rgba[0], 2, 16, QChar('0')).arg(rgba[1], 2, 16, QChar('0'))
                       .arg(rgba[2], 2, 16, QChar('0'));
    if (rgba[3] != 255)
        text += QString("%1").arg(rgba[3], 2, 16, QChar('0'));
    return text.toUpper();
}

bool parseColor(const QString& text, uint8_t rgba[4])
{
    const QColor color(text.trimmed().length() == 9 && text.startsWith('#')
            ? "#" + text.mid(7, 2) + text.mid(1, 6)  // #RRGGBBAA, as CSS writes it
            : text.trimmed());
    if (!color.isValid())
        return false;
    rgba[0] = static_cast<uint8_t>(color.red());
    rgba[1] = static_cast<uint8_t>(color.green());
    rgba[2] = static_cast<uint8_t>(color.blue());
    rgba[3] = static_cast<uint8_t>(color.alpha());
    return true;
}

QString layerTypeName(const fla::Layer& layer)
{
    switch (layer.layerType)
    {
    case fla::Layer::Type::Mask: return "mask";
    case fla::Layer::Type::Masked: return "masked";
    case fla::Layer::Type::Folder: return "folder";
    case fla::Layer::Type::Guide: return "guide";
    default: return layer.parentLayer && layer.parentLayer->layerType == fla::Layer::Type::Guide ? "guided" : "normal";
    }
}

QString tweenName(const fla::Frame& frame)
{
    if (frame.tweenType == fla::TweenType::None)
        return "none";
    if (frame.tweenType == fla::TweenType::Shape)
        return "shape";
    return fla::timeline::motionOf(const_cast<fla::Frame&>(frame)) ? "motion" : "classic";
}

QString elementTypeName(const fla::Element& element)
{
    switch (element.elementType())
    {
    case fla::Element::Type::Shape:
        return static_cast<const fla::Shape&>(element).isDrawingObject ? "drawing object" : "shape";
    case fla::Element::Type::SymbolInstance:
        return "instance";
    case fla::Element::Type::StaticText:
        return "text";
    case fla::Element::Type::Group:
        return "group";
    case fla::Element::Type::BitmapInstance:
        return "bitmap";
    default:
        return "other";
    }
}

QString symbolTypeName(fla::SymbolType type)
{
    switch (type)
    {
    case fla::SymbolType::MovieClip: return "movie clip";
    case fla::SymbolType::Button: return "button";
    default: return "graphic";
    }
}

/// The keyframe showing at \a index (from 0) on \a layer, or null.
fla::Frame* keyframeAt(const fla::Layer& layer, int index)
{
    for (fla::Frame* frame : layer.frames)
    {
        if (frame && frame->index <= index && index < frame->index + std::max(1, frame->duration))
            return frame;
    }
    return nullptr;
}

} // namespace

// The parts of the main window the operations work through. MainWindow makes
// ScriptApi a friend so these stay out of its public face.
struct ScriptContext
{
    MainWindow& window;

    fla::Document& document() const
    {
        if (!window._flaDocument || !window._flaDocument->document)
            fail("No document is open.");
        return *window._flaDocument->document;
    }

    fla::CommandStack& stack() const { return window._editContext.commandStack(); }

    fla::Symbol* symbolNamed(const QString& name) const
    {
        fla::SymbolList* list = document().symbolList;
        const auto found = list ? list->symbolMap.find(name.toStdString()) : decltype(list->symbolMap)::iterator();
        if (!list || found == list->symbolMap.end())
            fail(QString("There is no symbol called %1 in the library.").arg(name));
        return found->second;
    }

    fla::Timeline& timeline(const QJsonObject& args) const
    {
        const QString symbol = args.value("symbol").toString();
        if (!symbol.isEmpty())
        {
            fla::Symbol* found = symbolNamed(symbol);
            if (found->timelines.empty() || !found->timelines.front())
                fail(QString("%1 has no timeline.").arg(symbol));
            return *found->timelines.front();
        }
        fla::Timeline* scene = document().currentScene();
        if (!scene)
            fail("The document has no scene.");
        return *scene;
    }

    int layerIndex(const fla::Timeline& timeline, const QJsonObject& args, const char* key = "layer") const
    {
        const QJsonValue value = args.value(key);
        if (value.isDouble())
        {
            const int index = value.toInt(-1);
            if (index < 0 || index >= static_cast<int>(timeline.layers.size()))
                fail(QString("There is no layer %1; the timeline has %2.").arg(index).arg(timeline.layers.size()));
            return index;
        }
        const QString name = value.toString();
        if (name.isEmpty())
            fail(QString("Say which layer, by name or index (\"%1\").").arg(key));
        for (size_t i = 0; i < timeline.layers.size(); ++i)
        {
            if (timeline.layers[i] && QString::fromStdString(timeline.layers[i]->name) == name)
                return static_cast<int>(i);
        }
        fail(QString("There is no layer called %1.").arg(name));
    }

    fla::Layer& layer(fla::Timeline& timeline, const QJsonObject& args) const
    {
        fla::Layer* found = timeline.layers[static_cast<size_t>(layerIndex(timeline, args))];
        if (!found)
            fail("That layer is missing.");
        return *found;
    }

    static int frameIndex(const QJsonObject& args, const char* key = "frame", int fallback = 1)
    {
        const int frame = args.value(key).toInt(fallback);
        if (frame < 1 || frame > 100000)
            fail(QString("%1 is not a frame number (they count from 1).").arg(frame));
        return frame - 1;
    }

    fla::Frame& keyframe(fla::Layer& layer, const QJsonObject& args) const
    {
        fla::Frame* frame = keyframeAt(layer, frameIndex(args));
        if (!frame)
            fail(QString("Layer %1 has no frame %2.").arg(QString::fromStdString(layer.name)).arg(args.value("frame").toInt(1)));
        return *frame;
    }

    fla::Element& element(fla::Frame& frame, const QJsonObject& args) const
    {
        const int index = args.value("index").toInt(-1);
        if (index < 0 || index >= static_cast<int>(frame.elements.size()) || !frame.elements[static_cast<size_t>(index)])
            fail(QString("There is no element %1 on that keyframe; it has %2.").arg(index).arg(frame.elements.size()));
        return *frame.elements[static_cast<size_t>(index)];
    }

    /// Everything shown changed: the stage, the timeline, the library.
    void refresh() const
    {
        window.refreshAfterScript();
    }
};

namespace {

QJsonObject boundsJson(const QRectF& bounds)
{
    return QJsonObject{{"x", std::round(bounds.x() * 100) / 100}, {"y", std::round(bounds.y() * 100) / 100},
        {"width", std::round(bounds.width() * 100) / 100}, {"height", std::round(bounds.height() * 100) / 100}};
}

QJsonObject elementJson(MainWindow& window, const fla::Element& element, int index)
{
    QJsonObject json{{"index", index}, {"type", elementTypeName(element)}};
    const fla::Transform& m = element.transform;
    const double scaleX = std::hypot(m.m11, m.m12);
    const double scaleY = std::hypot(m.m21, m.m22);
    json["transform"] = QJsonObject{{"x", m.tx}, {"y", m.ty}, {"scaleX", scaleX}, {"scaleY", scaleY},
        {"rotation", std::atan2(m.m12, m.m11) * 180.0 / 3.14159265358979}};
    if (PhoenixView* view = window.phoenixView())
    {
        const QRectF bounds = view->elementBounds(&element);
        if (bounds.isValid())
            json["bounds"] = boundsJson(bounds);
    }
    switch (element.elementType())
    {
    case fla::Element::Type::SymbolInstance:
    {
        const auto& instance = static_cast<const fla::SymbolInstance&>(element);
        json["symbol"] = QString::fromStdString(instance.libraryItemName);
        json["symbolType"] = symbolTypeName(instance.symbolType);
        if (!instance.name.empty())
            json["name"] = QString::fromStdString(instance.name);
        if (!instance.colorTransform.isIdentity())
            json["alpha"] = instance.colorTransform.alphaMultiplier;
        if (instance.colorTransform.tintMultiplier != 0.0)
        {
            json["tint"] = colorText(instance.colorTransform.tintColor);
            json["tintAmount"] = instance.colorTransform.tintMultiplier;
        }
        break;
    }
    case fla::Element::Type::StaticText:
    {
        const auto& text = static_cast<const fla::StaticText&>(element);
        QString characters;
        for (const fla::TextRun& run : text.runs)
            characters += QString::fromStdString(run.text);
        json["text"] = characters.replace('\r', '\n');
        if (!text.runs.empty())
        {
            json["font"] = QString::fromStdString(text.runs.front().face);
            json["size"] = text.runs.front().size;
            json["color"] = colorText(text.runs.front().fillColor);
        }
        break;
    }
    case fla::Element::Type::Group:
        json["members"] = static_cast<int>(static_cast<const fla::Group&>(element).members.size());
        break;
    default:
        break;
    }
    // What changes it over time with no keyframes, by type (set_effects
    // changes them), and the template fields an instance fills in.
    for (const char* attribute : {"phoenixBehaviors", fla::shapeops::kAttribute, fla::textanim::kAttribute})
    {
        const std::vector<fla::behaviors::Applied> list = fla::behaviors::listOf(element, attribute);
        if (list.empty())
            continue;
        QJsonArray types;
        for (const fla::behaviors::Applied& applied : list)
            types.append(QString::fromStdString(applied.type) + (applied.enabled ? "" : " (off)"));
        json["effects"] = types;
    }
    // An instance's effects on its own picture.
    if (element.elementType() == fla::Element::Type::SymbolInstance)
    {
        const std::vector<fla::behaviors::Applied> list = fla::compositing::instanceEffectsOf(element);
        QJsonArray types;
        for (const fla::behaviors::Applied& applied : list)
            types.append(QString::fromStdString(applied.type) + (applied.enabled ? "" : " (off)"));
        if (!types.isEmpty())
            json["pictureEffects"] = types;
    }
    if (element.elementType() == fla::Element::Type::SymbolInstance)
    {
        const fla::templates::Values values = fla::templates::valuesOf(static_cast<const fla::SymbolInstance&>(element));
        if (!values.empty())
        {
            QJsonObject fields;
            for (const auto& [name, value] : values)
                fields[QString::fromStdString(name)] = QString::fromStdString(value);
            json["templateValues"] = fields;
        }
    }
    return json;
}

/// Library items: symbols, bitmaps and sounds, with what they are and how
/// often they are used.
QJsonArray libraryJson(fla::Document& document)
{
    const std::map<std::string, int> uses = fla::library::useCounts(document);
    const auto useOf = [&](const std::string& name) {
        const auto found = uses.find(name);
        return found == uses.end() ? 0 : found->second;
    };
    QJsonArray items;
    if (document.symbolList)
    {
        for (const fla::Symbol* symbol : document.symbolList->symbols)
        {
            if (symbol)
                items.append(QJsonObject{{"name", QString::fromStdString(symbol->name)},
                    {"kind", "symbol"}, {"symbolType", symbolTypeName(symbol->symbolType)},
                    {"uses", useOf(symbol->name)}});
        }
    }
    for (const fla::Resource* resource : document.resources)
    {
        if (const auto* bitmap = dynamic_cast<const fla::Bitmap*>(resource))
            items.append(QJsonObject{{"name", QString::fromStdString(bitmap->name)}, {"kind", "bitmap"},
                {"width", bitmap->frameRight / 20}, {"height", bitmap->frameBottom / 20},
                {"uses", useOf(bitmap->name)}});
    }
    for (const auto& sound : document.sounds)
        items.append(QJsonObject{{"name", QString::fromStdString(sound.name)}, {"kind", "sound"},
            {"uses", useOf(sound.name)}});
    for (const fla::Folder* folder : document.folders)
    {
        if (folder)
            items.append(QJsonObject{{"name", QString::fromStdString(folder->name)}, {"kind", "folder"}});
    }
    return items;
}

QJsonObject timelineJson(const fla::Timeline& timeline, bool frames)
{
    QJsonArray layers;
    for (size_t i = 0; i < timeline.layers.size(); ++i)
    {
        const fla::Layer* layer = timeline.layers[i];
        if (!layer)
            continue;
        QJsonObject json{{"index", static_cast<int>(i)}, {"name", QString::fromStdString(layer->name)},
            {"type", layerTypeName(*layer)}, {"visible", layer->visible}, {"locked", layer->locked}};
        if (layer->parentLayer)
            json["in"] = QString::fromStdString(layer->parentLayer->name);
        if (fla::compositing::matteOf(*layer) != fla::compositing::Matte::None)
            json["matte"] = fla::compositing::matteName(fla::compositing::matteOf(*layer));
        if (fla::compositing::isAdjustment(*layer))
            json["adjustment"] = true;
        int length = 0;
        QJsonArray keyframes;
        for (const fla::Frame* frame : layer->frames)
        {
            if (!frame)
                continue;
            length = std::max(length, frame->index + std::max(1, frame->duration));
            if (!frames)
                continue;
            QJsonObject key{{"frame", frame->index + 1}, {"duration", frame->duration},
                {"elements", static_cast<int>(frame->elements.size())}};
            const QString tween = tweenName(*frame);
            if (tween != "none")
            {
                key["tween"] = tween;
                if (frame->acceleration != 0)
                    key["ease"] = frame->acceleration;
            }
            if (!frame->name.empty())
            {
                key["label"] = QString::fromStdString(frame->name);
                key["labelType"] = QString::fromStdString(frame->labelType.empty() ? "name" : frame->labelType);
            }
            if (frame->actionScript)
                key["script"] = true;
            keyframes.append(key);
        }
        json["frames"] = length;
        if (frames)
            json["keyframes"] = keyframes;
        layers.append(json);
    }
    return QJsonObject{{"name", QString::fromStdString(timeline.name)}, {"layers", layers}};
}

QJsonObject documentJson(MainWindow& window, fla::Document& document)
{
    QJsonArray scenes;
    for (const fla::Timeline* scene : document.timelines)
    {
        if (scene)
            scenes.append(QString::fromStdString(scene->name));
    }
    const fla::Timeline* current = document.currentScene();
    QJsonObject json{{"width", document.width}, {"height", document.height}, {"fps", document.frameRate},
        {"background", colorText(document.backgroundColor)}, {"scenes", scenes},
        {"scene", current ? QString::fromStdString(current->name) : QString()},
        {"name", window.windowTitle()}};
    if (current)
        json["timeline"] = timelineJson(*current, false);
    json["library"] = libraryJson(document);
    if (window.phoenixView() && window.phoenixView()->player())
        json["playhead"] = window.phoenixView()->player()->currentFrame() + 1;
    return json;
}

QString fontFamilyOf(const std::string& face)
{
    // Faces are kept as family plus "-Bold" or "-Italic".
    QString family = QString::fromStdString(face);
    const int dash = static_cast<int>(family.lastIndexOf('-'));
    if (dash > 0)
        family = family.left(dash);
    return family;
}

} // namespace

ScriptApi::ScriptApi(MainWindow& window)
    : _window(window)
{
    addReadOperations();
    addWriteOperations();
    addHelperOperations();
    addMotionGraphicsOperations();
}

void ScriptApi::add(const QString& name, const QString& title, const QString& description,
    const QJsonObject& properties, const QStringList& required, bool writes, std::function<Result(const QJsonObject&)> run)
{
    QJsonObject schema{{"type", "object"}, {"properties", properties}};
    if (!required.isEmpty())
        schema["required"] = QJsonArray::fromStringList(required);
    _operations.push_back({name, title, description, schema, writes, std::move(run)});
}

ScriptApi::Result ScriptApi::call(const QString& name, const QJsonObject& arguments)
{
    const auto found = std::find_if(_operations.begin(), _operations.end(),
        [&](const Operation& operation) { return operation.name == name; });
    if (found == _operations.end())
        return {QJsonValue(), QImage(), QString("There is no operation called %1.").arg(name)};
    ScriptContext context{_window};
    const bool grouped = !_group.isEmpty();
    try
    {
        if (!found->writes)
            return found->run(arguments);
        if (_window.isReadOnlyDocument())
            fail("This document is read-only: it came from a newer Phoenix. Save a copy to change it.");
        fla::CommandStack& stack = context.stack();
        if (grouped)
        {
            // The group went from under it: its changes would land elsewhere.
            const QString trouble = groupTrouble();
            if (!trouble.isEmpty())
            {
                _group.clear();
                fail(QString("The edit begin_edit opened has ended: %1. Begin another.").arg(trouble));
            }
        }
        else if (fromOutside && stack.isInMacro())
        {
            // A change of the person's own (a drag, a script) is under way:
            // this one would fold into it.
            fail("Phoenix is in the middle of another change; try again in a moment.");
        }
        // Each change is one undo step named for what asked it, unless a
        // group gathers several.
        if (!grouped)
            stack.beginMacro(("AI: " + found->title).toStdString());
        Result result;
        try
        {
            result = found->run(arguments);
        }
        catch (...)
        {
            if (!grouped)
                stack.endMacro();
            context.refresh();
            throw;
        }
        if (!grouped)
        {
            stack.endMacro();
            stack.breakMergeChain();
        }
        context.refresh();
        return result;
    }
    catch (const ScriptError& error)
    {
        return {QJsonValue(), QImage(), QString::fromStdString(error.what())};
    }
    catch (const std::exception& error)
    {
        return {QJsonValue(), QImage(), QString("That failed: %1").arg(error.what())};
    }
}

QString ScriptApi::groupTrouble() const
{
    if (_group.isEmpty())
        return QString();
    if (_window._flaDocument != _groupDocument)
        return "the document it was on is no longer the one in front";
    if (!_window._editContext.commandStack().isInMacro())
        return "its undo step was closed by something else";
    return QString();
}

void ScriptApi::addReadOperations()
{
    add("get_document", "Document summary",
        "The open document: stage size, frame rate, background, scenes, the current scene's layers, the library "
        "with how often each item is used, and the playhead.",
        {}, {}, false, [this](const QJsonObject&) {
            ScriptContext context{_window};
            return Result{documentJson(_window, context.document())};
        });

    add("get_timeline", "Timeline",
        "Every layer of the scene (or of a symbol) from the top, with its keyframes: where each starts, how long "
        "it lasts, its tween and ease, its label, and how many elements it holds.",
        {{"symbol", kSymbolArg}}, {}, false, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            return Result{timelineJson(context.timeline(args), true)};
        });

    add("get_elements", "Elements",
        "The elements on a layer at a frame, bottom first: type, transform, bounds on the stage, and for "
        "instances their symbol and color effect, for text its characters and font.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg}}, {"layer", "frame"}, false,
        [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            QJsonArray elements;
            for (size_t i = 0; i < frame.elements.size(); ++i)
            {
                if (frame.elements[i])
                    elements.append(elementJson(_window, *frame.elements[i], static_cast<int>(i)));
            }
            return Result{QJsonObject{{"keyframe", frame.index + 1}, {"duration", frame.duration},
                {"elements", elements}}};
        });

    add("get_library", "Library",
        "Every library item: symbols with their type, bitmaps with their size, sounds and folders, each with its "
        "use count. Items used nowhere have 0.",
        {}, {}, false, [this](const QJsonObject&) {
            ScriptContext context{_window};
            return Result{libraryJson(context.document())};
        });

    add("render_frame", "Render frame",
        "A picture of a frame of the scene, or of a symbol, as a PNG, so you can see the artwork. Width defaults "
        "to 640 pixels.",
        {{"symbol", kSymbolArg}, {"frame", kFrameArg}, {"width", prop("integer", "The picture's width in pixels, 16 to 2048")}},
        {}, false, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            PhoenixView* view = _window.phoenixView();
            if (!view)
                fail("There is no stage to draw with.");
            const int width = std::clamp(args.value("width").toInt(640), 16, 2048);
            const int frame = ScriptContext::frameIndex(args);
            QImage image;
            const QString symbol = args.value("symbol").toString();
            if (!symbol.isEmpty())
            {
                fla::Symbol* found = context.symbolNamed(symbol);
                const QRectF area = view->symbolFramesBounds(found, frame, frame);
                if (!area.isValid() || area.isEmpty())
                    fail(QString("%1 shows nothing on frame %2.").arg(symbol).arg(frame + 1));
                image = view->renderSymbolFrame(found, frame, area.adjusted(-4, -4, 4, 4), width / (area.width() + 8));
            }
            else
            {
                const double scale = width / std::max(1.0, static_cast<double>(document.width));
                const QSize size(width, std::max(1, static_cast<int>(std::lround(document.height * scale))));
                image = view->renderFrame(frame, size, false);
            }
            if (image.isNull())
                fail("The frame could not be drawn.");
            return Result{QJsonObject{{"width", image.width()}, {"height", image.height()}}, image};
        });

    add("check_document", "Check document",
        "Problems worth fixing: damage the document checker finds, library items used nowhere, empty and hidden "
        "layers, artwork entirely off the stage, very large bitmaps and very heavy shapes.",
        {}, {}, false, [this](const QJsonObject&) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            QJsonArray issues;
            const auto issue = [&](const QString& severity, const QString& where, const QString& what) {
                issues.append(QJsonObject{{"severity", severity}, {"where", where}, {"what", what}});
            };
            fla::doctor::Report report;
            fla::doctor::checkDocument(document, report, false);
            for (const fla::doctor::Issue& found : report.issues)
            {
                const QString severity = found.severity == fla::doctor::Issue::Severity::Error ? "error"
                    : found.severity == fla::doctor::Issue::Severity::Warning               ? "warning"
                                                                                             : "info";
                issue(severity, QString::fromStdString(found.where), QString::fromStdString(found.what));
            }
            const std::map<std::string, int> uses = fla::library::useCounts(document);
            for (const QJsonValue& value : libraryJson(document))
            {
                const QJsonObject item = value.toObject();
                if (item["kind"] != "folder" && item["uses"].toInt() == 0)
                    issue("info", item["name"].toString(), "used nowhere in the document");
                if (item["kind"] == "bitmap" && (item["width"].toInt() > 4096 || item["height"].toInt() > 4096))
                    issue("warning", item["name"].toString(),
                        QString("a very large bitmap (%1x%2)").arg(item["width"].toInt()).arg(item["height"].toInt()));
            }
            const QRectF stage(0, 0, document.width, document.height);
            for (const fla::Timeline* scene : document.timelines)
            {
                if (!scene)
                    continue;
                for (const fla::Layer* layer : scene->layers)
                {
                    if (!layer)
                        continue;
                    const QString where = QString("%1 > %2").arg(QString::fromStdString(scene->name),
                        QString::fromStdString(layer->name));
                    if (layer->layerType != fla::Layer::Type::Folder)
                    {
                        bool empty = true;
                        for (const fla::Frame* frame : layer->frames)
                            empty = empty && (!frame || frame->elements.empty());
                        if (empty)
                            issue("info", where, "an empty layer");
                    }
                    if (!layer->visible)
                        issue("info", where, "a hidden layer");
                    for (const fla::Frame* frame : layer->frames)
                    {
                        for (const fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                        {
                            if (!element)
                                continue;
                            if (scene == document.currentScene() && _window.phoenixView())
                            {
                                const QRectF bounds = _window.phoenixView()->elementBounds(element);
                                if (bounds.isValid() && !bounds.intersects(stage))
                                    issue("info", QString("%1, frame %2").arg(where).arg(frame->index + 1),
                                        QString("a %1 entirely off the stage").arg(elementTypeName(*element)));
                            }
                            if (element->elementType() == fla::Element::Type::Shape &&
                                static_cast<const fla::Shape*>(element)->edges.size() > 3000)
                                issue("warning", QString("%1, frame %2").arg(where).arg(frame->index + 1),
                                    QString("a very heavy shape (%1 edges)")
                                        .arg(static_cast<const fla::Shape*>(element)->edges.size()));
                        }
                    }
                }
            }
            return Result{QJsonObject{{"issues", issues}, {"count", issues.size()}}};
        });
}

void ScriptApi::addWriteOperations()
{
    add("new_document", "New document",
        "Opens a new, empty document in a tab of its own and shows it. Nothing already open is touched.",
        {{"width", prop("integer", "Stage width in pixels; 1280 when left out")},
            {"height", prop("integer", "Stage height; 720 when left out")},
            {"fps", prop("number", "Frames per second; 24 when left out")}},
        {}, false, [this](const QJsonObject& args) {
            _window.newLessonDocument(std::clamp(args.value("width").toInt(1280), 1, 8192),
                std::clamp(args.value("height").toInt(720), 1, 8192), std::clamp(args.value("fps").toDouble(24.0), 1.0, 120.0));
            ScriptContext context{_window};
            return Result{documentJson(_window, context.document())};
        });

    add("open_document", "Open document",
        "Opens an FLA, XFL or Phoenix document from disk in a tab of its own and shows it.",
        {{"path", prop("string", "The document's path")}}, {"path"}, false, [this](const QJsonObject& args) {
            const QString path = args.value("path").toString();
            if (!QFileInfo::exists(path))
                fail(QString("There is no file at %1.").arg(path));
            _window.openDocumentPath(path);
            ScriptContext context{_window};
            return Result{documentJson(_window, context.document())};
        });

    add("begin_edit", "Begin edit",
        "Starts gathering changes into one undo step, named as given, until end_edit. Use it around a request "
        "that takes several calls, so the person can undo it all at once.",
        {{"label", prop("string", "What the changes do, for the Edit menu: \"Add walk cycle\"")}}, {"label"}, false,
        [this](const QJsonObject& args) {
            ScriptContext context{_window};
            context.document();
            // One whose document or undo step went is let go.
            if (!_group.isEmpty() && !groupTrouble().isEmpty())
                _group.clear();
            if (!_group.isEmpty())
                fail("An edit is already open; end it first.");
            fla::CommandStack& stack = context.stack();
            if (fromOutside && stack.isInMacro())
                fail("Phoenix is in the middle of another change; try again in a moment.");
            const QString label = args.value("label").toString().trimmed();
            _group = label.isEmpty() ? "Edit" : label;
            stack.beginMacro(("AI: " + _group).toStdString());
            _groupDocument = _window._flaDocument;
            _groupIndex = stack.index();
            _groupCount = stack.count();
            return Result{QJsonObject{{"open", _group}}};
        });

    add("end_edit", "End edit", "Ends the undo step begin_edit started.", {}, {}, false,
        [this](const QJsonObject&) {
            ScriptContext context{_window};
            if (_group.isEmpty())
                fail("No edit is open.");
            const QString closed = _group;
            _group.clear();
            const QString trouble = groupTrouble();
            if (!trouble.isEmpty())
                fail(QString("The edit had already ended: %1.").arg(trouble));
            fla::CommandStack& stack = context.stack();
            const bool moved = stack.index() != _groupIndex || stack.count() != _groupCount;
            stack.endMacro();
            stack.breakMergeChain();
            context.refresh();
            QJsonObject result{{"closed", closed}};
            if (moved)
                result["note"] = "The undo history changed while the edit was open (something was undone or redone), "
                                 "so the document may not be as the edit left it. Look before going on.";
            return Result{result};
        });

    add("add_layer", "Add layer",
        "Adds a layer above another (or at the top) of the scene or a symbol.",
        {{"symbol", kSymbolArg}, {"name", prop("string", "The new layer's name")},
            {"type", choice({"normal", "guide", "mask", "folder"}, "What kind of layer; normal when left out")},
            {"above", kLayerArg}},
        {"name"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            const QString type = args.value("type").toString("normal");
            const int index = args.contains("above") ? context.layerIndex(timeline, args, "above") : 0;
            fla::Layer* layer = fla::newLayerFor(timeline, type == "folder");
            layer->name = args.value("name").toString().toStdString();
            layer->autoNamed = false;
            if (type == "guide")
                layer->layerType = fla::Layer::Type::Guide;
            else if (type == "mask")
                layer->layerType = fla::Layer::Type::Mask;
            context.stack().push(fla::CommandPtr(new fla::AddLayerCommand(&timeline, index, layer, "Add Layer")));
            return Result{QJsonObject{{"layer", QString::fromStdString(layer->name)}, {"index", index}}};
        });

    add("set_layer", "Change layer",
        "Renames a layer, shows or hides it, locks or unlocks it, or changes its kind.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"name", prop("string", "A new name")},
            {"visible", prop("boolean", "Shown or hidden")}, {"locked", prop("boolean", "Locked or not")},
            {"type", choice({"normal", "guide", "guided", "mask", "masked"}, "A new kind")}},
        {"layer"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            fla::Layer* layer = timeline.layers[static_cast<size_t>(context.layerIndex(timeline, args))];
            fla::LayerSnapshot after = fla::snapshotLayers(timeline);
            for (fla::LayerState& state : after)
            {
                if (state.layer != layer)
                    continue;
                if (args.contains("name"))
                {
                    state.name = args.value("name").toString().toStdString();
                    state.autoNamed = false;
                }
                if (args.contains("visible"))
                    state.visible = args.value("visible").toBool();
                if (args.contains("locked"))
                    state.locked = args.value("locked").toBool();
            }
            if (args.contains("type"))
            {
                const QString type = args.value("type").toString();
                const fla::layers::Kind kind = type == "guide" ? fla::layers::Kind::Guide
                    : type == "guided"                        ? fla::layers::Kind::Guided
                    : type == "mask"                          ? fla::layers::Kind::Mask
                    : type == "masked"                        ? fla::layers::Kind::Masked
                                                              : fla::layers::Kind::Normal;
                if (!fla::layers::setKind(after, layer, kind))
                    fail(QString("That layer cannot become %1 where it is (a guided or masked layer needs a guide or "
                                 "mask above it).").arg(type));
            }
            context.stack().push(fla::CommandPtr(
                new fla::LayerEditCommand(&timeline, fla::snapshotLayers(timeline), after, "Layer Properties")));
            return Result{timelineJson(timeline, false)};
        });

    add("delete_layer", "Delete layer", "Deletes a layer and everything on it (a folder with what is in it).",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}}, {"layer"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            if (timeline.layers.size() <= 1)
                fail("A timeline keeps at least one layer.");
            const int index = context.layerIndex(timeline, args);
            _window._selection.clear();
            context.stack().push(fla::CommandPtr(new fla::RemoveLayerCommand(&timeline, index, "Delete Layer")));
            return Result{timelineJson(timeline, false)};
        });

    const auto frameEdit = [this](const QJsonObject& args, const char* name,
                               const std::function<void(fla::Layer&, std::vector<fla::Frame*>&)>& edit) {
        ScriptContext context{_window};
        fla::Timeline& timeline = context.timeline(args);
        fla::Layer& layer = context.layer(timeline, args);
        _window._selection.clear();
        fla::Layer* target = &layer;
        auto* command = new fla::EditFramesCommand({target}, name,
            [target, edit](std::vector<fla::Frame*>& removed) { edit(*target, removed); });
        context.stack().push(fla::CommandPtr(command));
        return timelineJson(timeline, true);
    };

    add("insert_keyframe", "Insert keyframe",
        "Inserts a keyframe at a frame, holding a copy of what shows there (or an empty one with blank). Past the "
        "layer's end its last frame is stretched up to it.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg},
            {"blank", prop("boolean", "An empty keyframe rather than a copy")}},
        {"layer", "frame"}, true, [frameEdit](const QJsonObject& args) {
            const int index = ScriptContext::frameIndex(args);
            const bool blank = args.value("blank").toBool(false);
            return Result{frameEdit(args, "Insert Keyframe", [index, blank](fla::Layer& layer, std::vector<fla::Frame*>&) {
                fla::timeline::insertKeyframe(layer, index, blank);
            })};
        });

    add("insert_frames", "Insert frames", "Inserts frames at a frame, lengthening the span there.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg},
            {"count", prop("integer", "How many frames; 1 when left out")}},
        {"layer", "frame"}, true, [frameEdit](const QJsonObject& args) {
            const int index = ScriptContext::frameIndex(args);
            const int count = std::clamp(args.value("count").toInt(1), 1, 10000);
            return Result{frameEdit(args, "Insert Frame", [index, count](fla::Layer& layer, std::vector<fla::Frame*>&) {
                fla::timeline::insertFrames(layer, index, count);
            })};
        });

    add("remove_frames", "Remove frames", "Removes frames from a layer, shortening it.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg},
            {"count", prop("integer", "How many frames; 1 when left out")}},
        {"layer", "frame"}, true, [frameEdit](const QJsonObject& args) {
            const int index = ScriptContext::frameIndex(args);
            const int count = std::clamp(args.value("count").toInt(1), 1, 10000);
            return Result{frameEdit(args, "Remove Frames", [index, count](fla::Layer& layer, std::vector<fla::Frame*>& removed) {
                fla::timeline::removeFrames(layer, index, count, removed);
            })};
        });

    add("set_tween", "Set tween",
        "Puts a tween on the span holding a frame: classic (between keyframes of the same symbol instance), shape "
        "(between shapes), motion (one instance or text block alone on its span), or none. Ease runs from -100 (in) "
        "to 100 (out).",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg},
            {"tween", choice({"classic", "shape", "motion", "none"}, "The kind of tween")},
            {"ease", prop("integer", "-100 (ease in) to 100 (ease out); 0 for none")}},
        {"layer", "frame", "tween"}, true, [this, frameEdit](const QJsonObject& args) {
            ScriptContext context{_window};
            const int index = ScriptContext::frameIndex(args);
            const QString tween = args.value("tween").toString();
            const bool hasEase = args.contains("ease");
            const int ease = std::clamp(args.value("ease").toInt(0), -100, 100);
            const double fps = context.document().frameRate;
            bool done = true;
            const QJsonObject result = frameEdit(args, "Tween",
                [&done, index, tween, hasEase, ease, fps](fla::Layer& layer, std::vector<fla::Frame*>&) {
                    if (tween == "classic")
                        done = fla::timeline::setTween(layer, index, true);
                    else if (tween == "none")
                        done = fla::timeline::setTween(layer, index, false);
                    else if (tween == "shape")
                        done = fla::timeline::setShapeTween(layer, index);
                    else if (tween == "motion")
                        done = fla::timeline::createMotionTween(layer, index, fps);
                    if (fla::Frame* span = fla::timeline::spanAt(layer, index); span && hasEase)
                        span->acceleration = ease;
                });
            if (!done)
                fail(tween == "motion"
                        ? "A motion tween needs one symbol instance or text block alone on its span."
                        : QString("No %1 tween could be made there.").arg(tween));
            return Result{result};
        });

    add("set_label", "Label frame", "Gives the keyframe at a frame a label (a name, a comment or an anchor), or "
        "removes it with an empty label.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg}, {"label", prop("string", "The label")},
            {"labelType", choice({"name", "comment", "anchor"}, "What kind of label")}},
        {"layer", "frame", "label"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            const std::string label = args.value("label").toString().toStdString();
            const std::string type = args.value("labelType").toString(label.empty() ? "" : "name").toStdString();
            context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(frame.name, frame.name, label, "Label")));
            context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(frame.labelType, frame.labelType,
                type == "name" ? std::string() : type, "Label")));
            return Result{QJsonObject{{"frame", frame.index + 1}, {"label", QString::fromStdString(label)}}};
        });

    add("add_shape", "Add shape",
        "Draws a shape from SVG path data (\"M 0 0 L 100 0 L 50 80 Z\", in stage pixels) on a layer at a frame, "
        "with a fill and an optional stroke. It is a drawing object, which keeps apart from other artwork, unless "
        "merge is true. For a rectangle or ellipse, write it as a path.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg},
            {"path", prop("string", "SVG path data, in the stage's pixels")},
            {"fill", prop("string", "Fill color, #RRGGBB or #RRGGBBAA; none for no fill")},
            {"stroke", prop("string", "Stroke color; none for no stroke (the default)")},
            {"strokeWidth", prop("number", "Stroke width in pixels; 1 when left out")},
            {"merge", prop("boolean", "Merge into the artwork beneath, as merge drawing does")}},
        {"layer", "frame", "path"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            const QString path = args.value("path").toString();
            if (path.trimmed().isEmpty() || path.contains('"') || path.contains('<'))
                fail("Give the shape as SVG path data.");
            const QString fill = args.value("fill").toString("#000000");
            const QString stroke = args.value("stroke").toString("none");
            uint8_t check[4];
            if (fill != "none" && !parseColor(fill, check))
                fail(QString("%1 is not a color.").arg(fill));
            if (stroke != "none" && !parseColor(stroke, check))
                fail(QString("%1 is not a color.").arg(stroke));
            const QString svg = QString("<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"%1\" fill=\"%2\" stroke=\"%3\" "
                                        "stroke-width=\"%4\"/></svg>")
                                    .arg(path, fill, stroke)
                                    .arg(std::clamp(args.value("strokeWidth").toDouble(1.0), 0.1, 200.0));
            const fla::vectors::Artwork art = fla::vectors::readSvg(svg.toStdString());
            if (art.paths.empty())
                fail("That path draws nothing.");
            fla::Shape* shape = fla::vectors::toShape(art.paths.front());
            if (!shape)
                fail("That path could not be made into a shape.");
            const bool merge = args.value("merge").toBool(false);
            shape->isDrawingObject = !merge;
            shape->parent = &frame;
            PhoenixView* view = _window.phoenixView();
            if (merge && view && view->activeFrame() == &frame)
            {
                // As drawing it by hand would: cut into the artwork under it.
                placeDrawnElement(*view, context.stack(), _window._selection, &frame, shape, "Shape", false);
                commitPendingMerge(*view, context.stack(), _window._selection, shape, &frame);
            }
            else
            {
                if (merge)
                    shape->isDrawingObject = false;
                context.stack().push(fla::CommandPtr(new fla::AddElementCommand(&frame, shape, "Shape", &_window._selection)));
            }
            const auto at = std::find(frame.elements.begin(), frame.elements.end(), shape);
            const int index = at == frame.elements.end() ? -1 : static_cast<int>(at - frame.elements.begin());
            return Result{index >= 0 ? QJsonValue(elementJson(_window, *shape, index))
                                     : QJsonValue(QJsonObject{{"merged", true}})};
        });

    add("add_text", "Add text", "Puts a line or block of text on the stage.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg}, {"text", prop("string", "What it says; \\n for new lines")},
            {"x", prop("number", "Left edge, in stage pixels")}, {"y", prop("number", "Top edge, in stage pixels")},
            {"font", prop("string", "Font family; Arial when left out")}, {"size", prop("number", "Size in points; 24 when left out")},
            {"color", prop("string", "#RRGGBB; black when left out")}, {"bold", prop("boolean", "Bold")},
            {"italic", prop("boolean", "Italic")}},
        {"layer", "frame", "text"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            auto* text = new fla::StaticText(&frame);
            text->autoExpand = true;
            fla::TextRun run;
            run.text = args.value("text").toString().replace('\n', '\r').toStdString();
            run.face = args.value("font").toString("Arial").toStdString();
            const bool bold = args.value("bold").toBool();
            const bool italic = args.value("italic").toBool();
            if (bold || italic)
                run.face += std::string("-") + (bold ? "Bold" : "") + (italic ? "Italic" : "");
            run.size = std::clamp(args.value("size").toDouble(24.0), 1.0, 1000.0);
            if (!parseColor(args.value("color").toString("#000000"), run.fillColor))
                fail("That is not a color.");
            text->runs.push_back(run);
            text->transform.tx = args.value("x").toDouble(0.0);
            text->transform.ty = args.value("y").toDouble(0.0);
            context.stack().push(fla::CommandPtr(new fla::AddElementCommand(&frame, text, "Text", &_window._selection)));
            return Result{elementJson(_window, *text, static_cast<int>(frame.elements.size()) - 1)};
        });

    add("add_instance", "Add instance",
        "Places an instance of a library symbol with its registration point at x, y, optionally scaled and turned.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg},
            {"item", prop("string", "The library symbol to place")}, {"x", prop("number", "Stage x")},
            {"y", prop("number", "Stage y")}, {"scaleX", prop("number", "1 for its own size")},
            {"scaleY", prop("number", "1 for its own size")}, {"rotation", prop("number", "Degrees, clockwise")}},
        {"layer", "frame", "item"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            fla::Layer& layer = context.layer(timeline, args);
            fla::Frame& frame = context.keyframe(layer, args);
            fla::Symbol* symbol = context.symbolNamed(args.value("item").toString());
            const QString inside = args.value("symbol").toString();
            if (!inside.isEmpty() && fla::symbolContains(symbol, context.symbolNamed(inside)))
                fail("A symbol cannot be placed inside itself.");
            auto* instance = new fla::SymbolInstance(&frame);
            instance->libraryItemName = symbol->name;
            instance->symbol = symbol;
            instance->symbolType = symbol->symbolType;
            instance->loopType = fla::LoopType::Loop;
            const double angle = args.value("rotation").toDouble(0.0) * 3.14159265358979 / 180.0;
            const double sx = args.value("scaleX").toDouble(1.0);
            const double sy = args.value("scaleY").toDouble(1.0);
            instance->transform = fla::Transform(sx * std::cos(angle), sx * std::sin(angle), -sy * std::sin(angle),
                sy * std::cos(angle), args.value("x").toDouble(0.0), args.value("y").toDouble(0.0));
            context.stack().push(fla::CommandPtr(new fla::AddElementCommand(&frame, instance, "Add Instance", &_window._selection)));
            const auto at = std::find(frame.elements.begin(), frame.elements.end(), instance);
            return Result{elementJson(_window, *instance, static_cast<int>(at - frame.elements.begin()))};
        });

    add("modify_element", "Change element",
        "Moves, scales or turns an element, and for symbol instances sets its alpha, tint or instance name. Values "
        "left out stay as they are.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg}, {"index", prop("integer", "The element, from 0 at the bottom")},
            {"x", prop("number", "Stage x of its registration point")}, {"y", prop("number", "Stage y")},
            {"scaleX", prop("number", "Horizontal scale, 1 for its own size")}, {"scaleY", prop("number", "Vertical scale")},
            {"rotation", prop("number", "Degrees, clockwise")}, {"alpha", prop("number", "0 (clear) to 1 (solid), instances only")},
            {"tint", prop("string", "A tint color, instances only")}, {"tintAmount", prop("number", "0 to 1")},
            {"name", prop("string", "Instance name, for scripts")}},
        {"layer", "frame", "index"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            fla::Element& element = context.element(frame, args);
            const fla::Transform before = element.transform;
            fla::Transform after = before;
            const double scaleX = std::hypot(before.m11, before.m12);
            const double scaleY = std::hypot(before.m21, before.m22);
            const double angle = std::atan2(before.m12, before.m11);
            const double newScaleX = args.value("scaleX").toDouble(scaleX);
            const double newScaleY = args.value("scaleY").toDouble(scaleY);
            const double newAngle = args.contains("rotation") ? args.value("rotation").toDouble() * 3.14159265358979 / 180.0 : angle;
            if (args.contains("scaleX") || args.contains("scaleY") || args.contains("rotation"))
            {
                after.m11 = newScaleX * std::cos(newAngle);
                after.m12 = newScaleX * std::sin(newAngle);
                after.m21 = -newScaleY * std::sin(newAngle);
                after.m22 = newScaleY * std::cos(newAngle);
            }
            after.tx = args.value("x").toDouble(before.tx);
            after.ty = args.value("y").toDouble(before.ty);
            if (after.m11 != before.m11 || after.m12 != before.m12 || after.m21 != before.m21 || after.m22 != before.m22 ||
                after.tx != before.tx || after.ty != before.ty)
                context.stack().push(fla::CommandPtr(new fla::SetElementTransformCommand(&element, before, after, "Transform")));
            if (args.contains("alpha") || args.contains("tint") || args.contains("tintAmount") || args.contains("name"))
            {
                if (element.elementType() != fla::Element::Type::SymbolInstance)
                    fail("Alpha, tint and names are for symbol instances.");
                auto& instance = static_cast<fla::SymbolInstance&>(element);
                fla::ColorTransform color = instance.colorTransform;
                if (args.contains("alpha"))
                    color.alphaMultiplier = std::clamp(args.value("alpha").toDouble(), 0.0, 1.0);
                if (args.contains("tint"))
                {
                    if (!parseColor(args.value("tint").toString(), color.tintColor))
                        fail("That is not a color.");
                    color.tintMultiplier = std::clamp(args.value("tintAmount").toDouble(0.5), 0.0, 1.0);
                }
                else if (args.contains("tintAmount"))
                    color.tintMultiplier = std::clamp(args.value("tintAmount").toDouble(), 0.0, 1.0);
                context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<fla::ColorTransform>(
                    instance.colorTransform, instance.colorTransform, color, "Color Effect")));
                if (args.contains("name"))
                    context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(
                        instance.name, instance.name, args.value("name").toString().toStdString(), "Instance Name")));
            }
            return Result{elementJson(_window, element, args.value("index").toInt())};
        });

    add("delete_element", "Delete element", "Removes an element from its keyframe.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg}, {"index", prop("integer", "The element")}},
        {"layer", "frame", "index"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            fla::Element& element = context.element(frame, args);
            context.stack().push(fla::CommandPtr(new fla::RemoveElementCommand(&frame, &element, "Delete", &_window._selection)));
            return Result{QJsonObject{{"remaining", static_cast<int>(frame.elements.size())}}};
        });

    add("convert_to_symbol", "Convert to symbol",
        "Turns elements on a keyframe into a new library symbol and leaves an instance of it in their place.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg},
            {"indices", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}},
                            {"description", "The elements; all on the keyframe when left out"}}},
            {"name", prop("string", "The new symbol's name")},
            {"type", choice({"graphic", "movie clip", "button"}, "Graphic when left out")}},
        {"layer", "frame", "name"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            const std::string name = args.value("name").toString().trimmed().toStdString();
            if (name.empty() || !fla::library::nameIsFree(document, name))
                fail(QString("%1 is already in the library, or empty.").arg(QString::fromStdString(name)));
            std::vector<fla::Element*> elements;
            if (args.contains("indices"))
            {
                for (const QJsonValue& value : args.value("indices").toArray())
                {
                    QJsonObject one = args;
                    one["index"] = value.toInt(-1);
                    elements.push_back(&context.element(frame, one));
                }
            }
            else
                elements.assign(frame.elements.begin(), frame.elements.end());
            if (elements.empty())
                fail("There is nothing on that keyframe to convert.");
            QRectF bounds;
            for (const fla::Element* element : elements)
            {
                const QRectF box = _window.phoenixView() ? _window.phoenixView()->elementBounds(element) : QRectF();
                bounds = bounds.isValid() ? bounds.united(box) : box;
            }
            const QPointF registration = bounds.isValid() ? bounds.center() : QPointF();
            const QString type = args.value("type").toString("graphic");
            const fla::SymbolType symbolType = type == "movie clip" ? fla::SymbolType::MovieClip
                : type == "button"                               ? fla::SymbolType::Button
                                                                 : fla::SymbolType::Graphic;
            context.stack().push(fla::CommandPtr(new fla::ConvertToSymbolCommand(&document, &frame, elements, name,
                registration.x(), registration.y(), "Convert to Symbol", &_window._selection, symbolType)));
            return Result{QJsonObject{{"symbol", QString::fromStdString(name)}, {"type", type}}};
        });

    add("rename_library_item", "Rename library item",
        "Renames a library item or folder, or moves it into a folder by giving a path (\"Characters/Ball\"). "
        "Instances follow.",
        {{"from", prop("string", "Its name now, with its folder path")}, {"to", prop("string", "The new name or path")}},
        {"from", "to"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            const std::string to = args.value("to").toString().toStdString();
            // A folder the new path needs is made first.
            const QString folder = QFileInfo(QString::fromStdString(to)).path();
            if (folder != "." && !fla::library::isFolder(document, folder.toStdString()))
                context.stack().push(fla::CommandPtr(new fla::AddFolderCommand(&document, folder.toStdString())));
            std::string problem;
            const fla::library::Renames renames =
                fla::library::pathRenames(document, args.value("from").toString().toStdString(), to, &problem);
            if (renames.empty())
                fail(problem.empty() ? QString("Nothing is called %1.").arg(args.value("from").toString())
                                     : QString::fromStdString(problem));
            context.stack().push(fla::CommandPtr(new fla::RenameLibraryCommand(&document, renames, "Rename")));
            return Result{QJsonObject{{"renamed", static_cast<int>(renames.size())}}};
        });

    add("go_to", "Go to",
        "Shows a frame on the stage, in the scene or inside a symbol, so the person sees what you changed.",
        {{"symbol", kSymbolArg}, {"frame", kFrameArg}}, {}, false, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            PhoenixView* view = _window.phoenixView();
            if (!view)
                fail("There is no stage.");
            const QString symbol = args.value("symbol").toString();
            if (!symbol.isEmpty())
                view->editSymbol(context.symbolNamed(symbol));
            else
                view->exitToDepth(0);
            if (args.contains("frame") && view->player())
                view->player()->setCurrentFrame(ScriptContext::frameIndex(args));
            context.refresh();
            return Result{QJsonObject{{"shown", symbol.isEmpty() ? "scene" : symbol}}};
        });

    add("export_image", "Export image",
        "Writes a frame of the scene to a PNG file. Phoenix asks the person before writing anywhere new.",
        {{"path", prop("string", "Where to write the PNG")}, {"frame", kFrameArg},
            {"width", prop("integer", "Width in pixels; the stage's when left out")}},
        {"path"}, false, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            const QString path = QDir::cleanPath(args.value("path").toString());
            if (!path.endsWith(".png", Qt::CaseInsensitive))
                fail("Give a path ending in .png.");
            if (!approveWrite || !approveWrite(path))
                fail("The person did not allow writing there.");
            const int width = std::clamp(args.value("width").toInt(static_cast<int>(document.width)), 16, 8192);
            const double scale = width / std::max(1.0, static_cast<double>(document.width));
            const QSize size(width, std::max(1, static_cast<int>(std::lround(document.height * scale))));
            const QImage image = _window.phoenixView()->renderFrame(ScriptContext::frameIndex(args), size, false);
            if (image.isNull() || !image.save(path, "PNG"))
                fail(QString("%1 could not be written.").arg(path));
            return Result{QJsonObject{{"written", path}, {"width", image.width()}, {"height", image.height()}}};
        });
}

namespace {

/// The kinds of list an element or layer can keep: behaviors on instances,
/// operators on shapes, letter animators on text, effects on adjustment
/// layers.
struct EffectKind
{
    const char* name;
    const char* attribute;
};

const EffectKind kEffectKinds[] = {{"behaviors", "phoenixBehaviors"}, {"shape operators", fla::shapeops::kAttribute},
    {"letter animators", fla::textanim::kAttribute}, {"adjustments", fla::compositing::kAdjustments}};

const fla::behaviors::Definition* effectDefinition(const QString& kind, const std::string& id)
{
    if (kind == "behaviors")
        return BehaviorLibrary::instance().engine().find(id);
    if (kind == "shape operators")
        return fla::shapeops::find(id);
    if (kind == "letter animators")
        return fla::textanim::find(id);
    return fla::compositing::find(id);
}

std::vector<fla::behaviors::Definition> effectDefinitions(const QString& kind)
{
    if (kind == "behaviors")
        return BehaviorLibrary::instance().engine().definitions();
    if (kind == "shape operators")
        return fla::shapeops::definitions();
    if (kind == "letter animators")
        return fla::textanim::definitions();
    return fla::compositing::definitions();
}

fla::behaviors::Applied effectDefaults(const QString& kind, const std::string& id)
{
    const int seed = 1 + static_cast<int>(QRandomGenerator::global()->bounded(9998));
    if (kind == "behaviors")
        return BehaviorLibrary::instance().engine().defaults(id, seed);
    if (kind == "shape operators")
        return fla::shapeops::defaults(id, seed);
    if (kind == "letter animators")
        return fla::textanim::defaults(id, seed);
    return fla::compositing::defaults(id, seed);
}

std::string settingText(const QJsonValue& value)
{
    if (value.isBool())
        return value.toBool() ? "true" : "false";
    if (value.isDouble())
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.10g", value.toDouble());
        return text;
    }
    return value.toString().toStdString();
}

/// A list given as JSON ([{type, enabled, settings, keys}]) as the element
/// keeps it, each unknown type or setting refused by name.
std::vector<fla::behaviors::Applied> effectList(const QString& kind, const QJsonArray& given)
{
    std::vector<fla::behaviors::Applied> list;
    for (const QJsonValue& value : given)
    {
        const QJsonObject entry = value.toObject();
        const std::string type = entry.value("type").toString().toStdString();
        const fla::behaviors::Definition* definition = effectDefinition(kind, type);
        if (!definition)
            fail(QString("There are no %1 called %2; list_effects names them.").arg(kind, QString::fromStdString(type)));
        fla::behaviors::Applied applied = effectDefaults(kind, type);
        applied.enabled = entry.value("enabled").toBool(true);
        const auto known = [&](const QString& name) {
            for (const fla::behaviors::ParamSpec& spec : definition->params)
                if (QString::fromStdString(spec.name) == name)
                    return true;
            fail(QString("%1 has no setting called %2.").arg(QString::fromStdString(definition->name), name));
        };
        const QJsonObject settings = entry.value("settings").toObject();
        for (auto it = settings.begin(); it != settings.end(); ++it)
            if (known(it.key()))
                applied.params[it.key().toStdString()] = settingText(it.value());
        const QJsonObject keys = entry.value("keys").toObject();
        for (auto it = keys.begin(); it != keys.end(); ++it)
        {
            known(it.key());
            const QJsonObject frames = it.value().toObject();
            for (auto key = frames.begin(); key != frames.end(); ++key)
            {
                const int frame = key.key().toInt() - 1;
                if (frame < 0)
                    fail(QString("Keys are at frames counted from 1, not %1.").arg(key.key()));
                applied.setKey(it.key().toStdString(), frame, settingText(key.value()));
            }
        }
        list.push_back(applied);
    }
    return list;
}

QJsonArray effectListJson(const std::vector<fla::behaviors::Applied>& list)
{
    QJsonArray out;
    for (const fla::behaviors::Applied& applied : list)
    {
        QJsonObject settings;
        for (const auto& [name, value] : applied.params)
            settings[QString::fromStdString(name)] = QString::fromStdString(value);
        out.append(QJsonObject{{"type", QString::fromStdString(applied.type)}, {"enabled", applied.enabled}, {"settings", settings}});
    }
    return out;
}

QStringList wordsOf(const QString& name)
{
    // Words split at spaces, dashes, underscores, dots and lower-to-upper
    // changes ("walkCycle", "Walk_cycle 2").
    QStringList words;
    QString word;
    for (int i = 0; i < name.size(); ++i)
    {
        const QChar c = name[i];
        if (c.isSpace() || c == '_' || c == '-' || c == '.')
        {
            if (!word.isEmpty())
                words << word;
            word.clear();
            continue;
        }
        if (c.isUpper() && !word.isEmpty() && (word.back().isLower() || (i + 1 < name.size() && name[i + 1].isLower() && word.back().isUpper())))
        {
            words << word;
            word.clear();
        }
        word += c;
    }
    if (!word.isEmpty())
        words << word;
    return words;
}

QString styled(const QString& name, const QString& style, const QString& prefix)
{
    QStringList words = wordsOf(name);
    QString out;
    if (style == "snake_case" || style == "kebab-case")
    {
        for (QString& word : words)
            word = word.toLower();
        out = words.join(style == "snake_case" ? "_" : "-");
    }
    else if (style == "camelCase" || style == "PascalCase")
    {
        for (int i = 0; i < words.size(); ++i)
        {
            QString word = words[i].toLower();
            if (i > 0 || style == "PascalCase")
                word[0] = word[0].toUpper();
            out += word;
        }
    }
    else
    {
        // Title Case: each word capitalized, spaced.
        for (QString& word : words)
        {
            word = word.toLower();
            word[0] = word[0].toUpper();
        }
        out = words.join(' ');
    }
    if (!prefix.isEmpty() && !out.startsWith(prefix))
        out = prefix + out;
    return out;
}

} // namespace

void ScriptApi::addMotionGraphicsOperations()
{
    const QJsonObject listArg{{"type", "array"},
        {"description", "In the order they apply: [{\"type\": \"wiggle\", \"settings\": {\"amount\": 20}, \"keys\": "
                        "{\"radius\": {\"1\": 50, \"48\": 200}}, \"enabled\": true}]; settings left out keep their "
                        "defaults, keys are by frame from 1"},
        {"items", QJsonObject{{"type", "object"}}}};

    add("list_effects", "List effects",
        "What Phoenix can do to elements over time with no keyframes: behaviors for symbol instances (wiggle, "
        "repeat in a circle, particles, follow sound...), operators for drawn shapes (trim paths, wiggle path...), "
        "letter animators for text, and effects for adjustment layers and for symbol instances' own pictures; each "
        "with its settings.",
        {{"kind", choice({"behaviors", "shape operators", "letter animators", "adjustments"}, "One kind; all when left out")}},
        {}, false, [](const QJsonObject& args) {
            QJsonObject out;
            for (const EffectKind& kind : kEffectKinds)
            {
                if (args.contains("kind") && args.value("kind").toString() != kind.name)
                    continue;
                QJsonArray list;
                for (const fla::behaviors::Definition& definition : effectDefinitions(kind.name))
                {
                    QJsonArray params;
                    for (const fla::behaviors::ParamSpec& spec : definition.params)
                    {
                        QJsonObject param{{"name", QString::fromStdString(spec.name)}, {"label", QString::fromStdString(spec.label)},
                            {"type", QString::fromStdString(spec.type)}, {"default", QString::fromStdString(spec.text)}};
                        if (spec.type == "number" || spec.type == "angle")
                        {
                            param["min"] = spec.min;
                            param["max"] = spec.max;
                        }
                        if (!spec.choices.empty())
                        {
                            QJsonArray choices;
                            for (const std::string& each : spec.choices)
                                choices.append(QString::fromStdString(each));
                            param["choices"] = choices;
                        }
                        params.append(param);
                    }
                    list.append(QJsonObject{{"type", QString::fromStdString(definition.id)},
                        {"name", QString::fromStdString(definition.name)},
                        {"description", QString::fromStdString(definition.description)}, {"settings", params}});
                }
                out[kind.name] = list;
            }
            return Result{out};
        });

    add("set_effects", "Set effects",
        "Gives an element its behaviors (a symbol instance), operators (a drawn shape) or letter animators (a text "
        "field), replacing those it had, or adding to them with append. With picture set, a symbol instance's "
        "effects on its own picture instead (the adjustments list_effects names: blur, glow, outline, wipes...). "
        "Settings can be keyed to change over time. An empty list takes them all off.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg}, {"index", prop("integer", "The element")},
            {"list", listArg}, {"append", prop("boolean", "Add after those it has rather than replacing them")},
            {"picture", prop("boolean", "For a symbol instance: set the effects on its picture rather than its behaviors")}},
        {"layer", "frame", "index", "list"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            fla::Element& element = context.element(frame, args);
            const bool picture = args.value("picture").toBool();
            const bool instance = element.elementType() == fla::Element::Type::SymbolInstance;
            if (picture && !instance)
                fail("Effects on a picture are for symbol instances; an adjustment layer's are set with set_layer_compositing.");
            const QString kind = picture                                                     ? "adjustments"
                : instance                                                                   ? "behaviors"
                : element.elementType() == fla::Element::Type::Shape                         ? "shape operators"
                : element.elementType() == fla::Element::Type::StaticText                    ? "letter animators"
                                                                                               : QString();
            if (kind.isEmpty())
                fail("Behaviors are for symbol instances, operators for shapes and letter animators for text.");
            const std::string attribute = picture ? fla::compositing::kInstanceEffects
                : kind == "behaviors"              ? "phoenixBehaviors"
                : kind == "shape operators"        ? fla::shapeops::kAttribute
                                                   : fla::textanim::kAttribute;
            std::vector<fla::behaviors::Applied> list = args.value("append").toBool()
                ? fla::behaviors::listOf(element, attribute) : std::vector<fla::behaviors::Applied>();
            for (const fla::behaviors::Applied& applied : effectList(kind, args.value("list").toArray()))
                list.push_back(applied);
            using Attributes = std::vector<std::pair<std::string, std::string>>;
            const Attributes after = fla::behaviors::withList(element, attribute, list);
            if (after != element.extraAttributes)
                context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(element.extraAttributes,
                    element.extraAttributes, after, picture ? "Effects" : kind == "behaviors" ? "Behaviors"
                        : kind == "shape operators" ? "Operators" : "Letter Animators")));
            context.refresh();
            return Result{QJsonObject{{"kind", picture ? QString("picture effects") : kind}, {"list", effectListJson(list)}}};
        });

    add("set_layer_compositing", "Set layer compositing",
        "Makes a layer show only through the layer above it (a track matte: by its alpha or its lightness, or the "
        "inverse), or an adjustment layer whose effects (blur, color, tint) change everything below it.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg},
            {"matte", choice({"none", "alpha", "alphaInverted", "luma", "lumaInverted"}, "The track matte; unchanged when left out")},
            {"adjustment", prop("boolean", "Whether it is an adjustment layer; unchanged when left out")},
            {"effects", listArg}},
        {"layer"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            using Attributes = std::vector<std::pair<std::string, std::string>>;
            fla::Layer scratch(nullptr);
            scratch.extraAttributes = layer.extraAttributes;
            if (args.contains("matte"))
            {
                const QString matte = args.value("matte").toString();
                const fla::compositing::Matte kind = matte == "alpha" ? fla::compositing::Matte::Alpha
                    : matte == "alphaInverted"                         ? fla::compositing::Matte::AlphaInverted
                    : matte == "luma"                                  ? fla::compositing::Matte::Luma
                    : matte == "lumaInverted"                          ? fla::compositing::Matte::LumaInverted
                                                                       : fla::compositing::Matte::None;
                scratch.extraAttributes = fla::compositing::withMatte(scratch, kind);
            }
            if (args.contains("adjustment"))
                scratch.extraAttributes = fla::compositing::withAdjustment(scratch, args.value("adjustment").toBool());
            if (args.contains("effects"))
                scratch.extraAttributes = fla::behaviors::withList(scratch, fla::compositing::kAdjustments,
                    effectList("adjustments", args.value("effects").toArray()));
            const Attributes after = scratch.extraAttributes;
            if (after != layer.extraAttributes)
                context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(layer.extraAttributes,
                    layer.extraAttributes, after, "Layer Compositing")));
            context.refresh();
            const fla::compositing::Matte matte = fla::compositing::matteOf(scratch);
            return Result{QJsonObject{{"matte", matte == fla::compositing::Matte::None ? "none" : fla::compositing::matteName(matte)},
                {"adjustment", fla::compositing::isAdjustment(scratch)},
                {"effects", effectListJson(fla::compositing::effectsOf(scratch))}}};
        });

    add("list_node_types", "List node graph nodes",
        "The kinds of node a timeline's node graph can have: id, name, category, inputs and settings (with their "
        "type, range and default), for get_node_graph and set_node_graph.",
        {}, {}, false, [](const QJsonObject&) {
            QJsonArray types;
            for (const fla::nodegraph::Type& type : fla::nodegraph::types())
            {
                QJsonArray inputs;
                for (const std::string& input : type.inputs)
                    inputs.append(QString::fromStdString(input));
                QJsonArray settings;
                for (const fla::behaviors::ParamSpec& spec : type.params)
                {
                    QJsonObject setting{{"name", QString::fromStdString(spec.name)},
                        {"label", QString::fromStdString(spec.label)}, {"type", QString::fromStdString(spec.type)},
                        {"default", QString::fromStdString(spec.text)}};
                    if (spec.type == "number" || spec.type == "angle" || spec.type == "percent" || spec.type == "seed")
                    {
                        setting["min"] = spec.min;
                        setting["max"] = spec.max;
                    }
                    if (!spec.choices.empty())
                    {
                        QJsonArray choices;
                        for (const std::string& choice : spec.choices)
                            choices.append(QString::fromStdString(choice));
                        setting["choices"] = choices;
                    }
                    settings.append(setting);
                }
                types.append(QJsonObject{{"id", QString::fromStdString(type.id)}, {"name", QString::fromStdString(type.name)},
                    {"category", QString::fromStdString(type.category)},
                    {"description", QString::fromStdString(type.description)}, {"inputs", inputs},
                    {"growingInputs", type.growing}, {"hasOutput", type.hasOutput}, {"settings", settings}});
            }
            return Result{QJsonObject{{"types", types}}};
        });

    const auto graphJson = [](const fla::nodegraph::Graph& graph) {
        QJsonArray nodes;
        for (const fla::nodegraph::Node& node : graph.nodes)
        {
            QJsonObject settings;
            for (const auto& [name, value] : node.applied.params)
                settings[QString::fromStdString(name)] = QString::fromStdString(value);
            QJsonObject entry{{"id", node.id}, {"type", QString::fromStdString(node.type())}, {"x", node.x}, {"y", node.y},
                {"settings", settings}};
            if (!node.label.empty())
                entry["label"] = QString::fromStdString(node.label);
            if (node.bypassed())
                entry["bypass"] = true;
            nodes.append(entry);
        }
        QJsonArray links;
        for (const fla::nodegraph::Link& link : graph.links)
            links.append(QJsonObject{{"from", link.from}, {"to", link.to}, {"input", link.input}});
        return QJsonObject{{"enabled", graph.enabled}, {"nodes", nodes}, {"links", links}};
    };

    add("get_node_graph", "Get a node graph",
        "A timeline's node graph, which composites its layers through nodes: whether it is on, its nodes (id, "
        "type, place, settings; keyed settings read \"@frame=value;frame=value\") and the links between them (a "
        "node's picture into another's input, counted from 0). hasGraph is false when the timeline has none.",
        {{"symbol", kSymbolArg}}, {}, false, [this, graphJson](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            const std::shared_ptr<const fla::nodegraph::Graph> graph = fla::nodegraph::of(timeline);
            QJsonObject result = graph ? graphJson(*graph) : QJsonObject();
            result["hasGraph"] = graph != nullptr;
            return Result{result};
        });

    add("set_node_graph", "Set a node graph",
        "Sets a timeline's node graph, as get_node_graph gives it: nodes with an id, a type from list_node_types, "
        "a place and settings by name (left-out settings take their defaults), and links. It must have one node "
        "of type output. remove true takes the graph away, so the layers stack as usual again; a timeline "
        "without one gets a starting graph (Other Layers into the Output) when only enabled is given.",
        {{"symbol", kSymbolArg}, {"enabled", prop("boolean", "Whether the graph composites the timeline")},
            {"nodes", QJsonObject{{"type", "array"}, {"description", "The nodes"}, {"items", QJsonObject{{"type", "object"}}}}},
            {"links", QJsonObject{{"type", "array"}, {"description", "The links"}, {"items", QJsonObject{{"type", "object"}}}}},
            {"remove", prop("boolean", "Take the graph away")}},
        {}, true, [this, graphJson](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            using Attributes = std::vector<std::pair<std::string, std::string>>;
            const std::shared_ptr<const fla::nodegraph::Graph> current = fla::nodegraph::of(timeline);
            fla::nodegraph::Graph graph = current ? *current : fla::nodegraph::starter();
            Attributes after;
            if (args.value("remove").toBool())
                after = fla::nodegraph::withGraph(timeline, nullptr);
            else
            {
                if (args.contains("nodes"))
                {
                    graph.nodes.clear();
                    graph.links.clear();
                    for (const QJsonValue& value : args.value("nodes").toArray())
                    {
                        const QJsonObject entry = value.toObject();
                        const std::string type = entry.value("type").toString().toStdString();
                        if (!fla::nodegraph::type(type))
                            fail(QString("There is no kind of node called %1; list_node_types names them.")
                                                  .arg(QString::fromStdString(type)));
                        fla::nodegraph::Node node = fla::nodegraph::make(type, entry.value("id").toInt(), entry.value("x").toDouble(),
                            entry.value("y").toDouble());
                        if (node.id <= 0 || graph.node(node.id))
                            fail("Each node needs an id of its own, above 0.");
                        node.label = entry.value("label").toString().toStdString();
                        node.applied.enabled = !entry.value("bypass").toBool();
                        const QJsonObject settings = entry.value("settings").toObject();
                        for (auto it = settings.begin(); it != settings.end(); ++it)
                            node.applied.params[it.key().toStdString()] =
                                it.value().isString() ? it.value().toString().toStdString()
                                : it.value().isBool() ? std::string(it.value().toBool() ? "true" : "false")
                                                      : QString::number(it.value().toDouble(), 'g', 10).toStdString();
                        graph.nodes.push_back(node);
                    }
                    if (!graph.output())
                        fail("A node graph needs one node of type output.");
                }
                if (args.contains("links"))
                {
                    graph.links.clear();
                    for (const QJsonValue& value : args.value("links").toArray())
                    {
                        const QJsonObject link = value.toObject();
                        if (!graph.connect(link.value("from").toInt(), link.value("to").toInt(), link.value("input").toInt()))
                            fail(QString("Node %1 cannot be linked into input %2 of node %3.")
                                                  .arg(link.value("from").toInt())
                                                  .arg(link.value("input").toInt())
                                                  .arg(link.value("to").toInt()));
                    }
                }
                if (args.contains("enabled"))
                    graph.enabled = args.value("enabled").toBool();
                after = fla::nodegraph::withGraph(timeline, &graph);
            }
            if (after != timeline.extraAttributes)
                context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(timeline.extraAttributes,
                    timeline.extraAttributes, after, "Node Graph")));
            context.refresh();
            const std::shared_ptr<const fla::nodegraph::Graph> now = fla::nodegraph::of(timeline);
            QJsonObject result = now ? graphJson(*now) : QJsonObject();
            result["hasGraph"] = now != nullptr;
            return Result{result};
        });

    add("set_template_values", "Set template values",
        "Fills in a symbol instance's template fields: the words of the named dynamic text fields inside its symbol "
        "and the colors (\"#RRGGBB\") of its named movie clips. An empty value shows what the symbol has. Returns "
        "the fields there are.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", kFrameArg}, {"index", prop("integer", "The instance")},
            {"values", QJsonObject{{"type", "object"}, {"description", "Field name to its words or color"},
                           {"additionalProperties", QJsonObject{{"type", "string"}}}}}},
        {"layer", "frame", "index"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& frame = context.keyframe(layer, args);
            fla::Element& element = context.element(frame, args);
            if (element.elementType() != fla::Element::Type::SymbolInstance || !static_cast<fla::SymbolInstance&>(element).symbol)
                fail("Template fields are on symbol instances.");
            auto& instance = static_cast<fla::SymbolInstance&>(element);
            const std::vector<fla::templates::Field> fields = fla::templates::fieldsOf(*instance.symbol);
            fla::templates::Values values = fla::templates::valuesOf(instance);
            const QJsonObject given = args.value("values").toObject();
            for (auto it = given.begin(); it != given.end(); ++it)
            {
                const std::string name = it.key().toStdString();
                if (std::none_of(fields.begin(), fields.end(), [&](const fla::templates::Field& f) { return f.name == name; }))
                    fail(QString("%1 has no field called %2.").arg(QString::fromStdString(instance.symbol->name), it.key()));
                const std::string value = it.value().toString().toStdString();
                if (value.empty())
                    values.erase(name);
                else
                    values[name] = value;
            }
            using Attributes = std::vector<std::pair<std::string, std::string>>;
            const Attributes after = fla::templates::withValues(instance, values);
            if (after != instance.extraAttributes)
                context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(instance.extraAttributes,
                    instance.extraAttributes, after, "Template Fields")));
            context.refresh();
            QJsonArray list;
            for (const fla::templates::Field& field : fields)
            {
                const auto set = values.find(field.name);
                list.append(QJsonObject{{"name", QString::fromStdString(field.name)},
                    {"kind", field.kind == fla::templates::Field::Kind::Text ? "text" : "color"},
                    {"asDrawn", QString::fromStdString(field.value)},
                    {"value", set == values.end() ? QString() : QString::fromStdString(set->second)}});
            }
            return Result{QJsonObject{{"fields", list}}};
        });

    add("rename_to_convention", "Rename to a convention",
        "Renames layers, library symbols or instance names to one naming style (Title Case, camelCase, PascalCase, "
        "snake_case or kebab-case), with a prefix if given; instances follow renamed symbols. Returns what changed.",
        {{"what", choice({"layers", "symbols", "instances"}, "What to rename")},
            {"style", choice({"Title Case", "camelCase", "PascalCase", "snake_case", "kebab-case"}, "The style")},
            {"prefix", prop("string", "Put before each name that does not have it yet, such as \"mc_\"")}},
        {"what", "style"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            const QString what = args.value("what").toString();
            const QString style = args.value("style").toString();
            const QString prefix = args.value("prefix").toString();
            QJsonArray changed;
            const auto note = [&](const std::string& from, const QString& to) {
                changed.append(QJsonObject{{"from", QString::fromStdString(from)}, {"to", to}});
            };
            // Every timeline: the scenes and the symbols'.
            std::vector<fla::Timeline*> timelines(document.timelines.begin(), document.timelines.end());
            if (document.symbolList)
                for (fla::Symbol* symbol : document.symbolList->symbols)
                    for (fla::Timeline* timeline : symbol ? symbol->timelines : std::vector<fla::Timeline*>())
                        timelines.push_back(timeline);
            if (what == "layers" || what == "instances")
            {
                for (fla::Timeline* timeline : timelines)
                    for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
                    {
                        if (!layer)
                            continue;
                        if (what == "layers")
                        {
                            const QString to = styled(QString::fromStdString(layer->name), style, prefix);
                            if (!to.isEmpty() && to.toStdString() != layer->name)
                            {
                                note(layer->name, to);
                                // Its timeline's node graph follows it.
                                const auto graphAfter =
                                    fla::nodegraph::withLayerRenamed(*timeline, layer->name, to.toStdString());
                                if (graphAfter != timeline->extraAttributes)
                                    context.stack().push(fla::CommandPtr(
                                        new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                                            timeline->extraAttributes, timeline->extraAttributes, graphAfter,
                                            "Rename Layer")));
                                context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(
                                    layer->name, layer->name, to.toStdString(), "Rename Layer")));
                            }
                            continue;
                        }
                        for (fla::Frame* frame : layer->frames)
                            for (fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                            {
                                auto* instance = dynamic_cast<fla::SymbolInstance*>(element);
                                if (!instance || instance->name.empty())
                                    continue;
                                const QString to = styled(QString::fromStdString(instance->name), style, prefix);
                                if (!to.isEmpty() && to.toStdString() != instance->name)
                                {
                                    note(instance->name, to);
                                    context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(
                                        instance->name, instance->name, to.toStdString(), "Instance Name")));
                                }
                            }
                    }
            }
            else if (what == "symbols" && document.symbolList)
            {
                std::vector<std::string> names;
                for (const fla::Symbol* symbol : document.symbolList->symbols)
                    if (symbol)
                        names.push_back(symbol->name);
                for (const std::string& name : names)
                {
                    // The folder path stays; the last part is renamed.
                    const QString full = QString::fromStdString(name);
                    const int slash = static_cast<int>(full.lastIndexOf('/'));
                    const QString folder = slash >= 0 ? full.left(slash + 1) : QString();
                    const QString to = folder + styled(full.mid(slash + 1), style, prefix);
                    if (to == full || to.endsWith('/'))
                        continue;
                    std::string problem;
                    const fla::library::Renames renames = fla::library::pathRenames(document, name, to.toStdString(), &problem);
                    if (renames.empty())
                        continue;
                    note(name, to);
                    context.stack().push(fla::CommandPtr(new fla::RenameLibraryCommand(&document, renames, "Rename")));
                }
            }
            context.refresh();
            return Result{QJsonObject{{"renamed", changed}}};
        });

    add("render_from_data", "Render from data",
        "Renders the movie once for each row, its symbols' template fields filled from the row, into a folder: "
        "one file a row, named by its \"file\" value or numbered. Phoenix asks the person before writing there.",
        {{"rows", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "object"}}},
                      {"description", "Field name to value, one object a movie: [{\"file\": \"ada\", \"name\": \"Ada Lovelace\"}]"}}},
            {"folder", prop("string", "Where to write the movies")},
            {"format", choice({"mp4", "mov", "gif", "png"}, "mp4 when left out")}},
        {"rows", "folder"}, false, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            context.document();
            const QString folder = QDir::cleanPath(args.value("folder").toString());
            if (!approveWrite || !approveWrite(folder))
                fail("The person did not allow writing there.");
            std::vector<fla::templates::Values> rows;
            for (const QJsonValue& value : args.value("rows").toArray())
            {
                fla::templates::Values row;
                const QJsonObject object = value.toObject();
                for (auto it = object.begin(); it != object.end(); ++it)
                    if (!settingText(it.value()).empty())
                        row[it.key().toStdString()] = settingText(it.value());
                rows.push_back(row);
            }
            if (rows.empty())
                fail("Give at least one row.");
            QString error;
            if (!_window.renderRows(rows, folder, args.value("format").toString("mp4"), error))
                fail(error);
            return Result{QJsonObject{{"rendered", static_cast<int>(rows.size())}, {"folder", folder}}};
        });
}

void ScriptApi::addHelperOperations()
{
    add("tidy_library", "Tidy library",
        "Moves loose library items into folders by kind (Symbols, Bitmaps, Sounds), leaving items already in "
        "folders where they are. With removeUnused, also deletes symbols used nowhere.",
        {{"removeUnused", prop("boolean", "Also delete symbols used nowhere")}}, {}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            int moved = 0;
            int removed = 0;
            const std::map<std::string, int> uses = fla::library::useCounts(document);
            if (args.value("removeUnused").toBool() && document.symbolList)
            {
                for (fla::Symbol* symbol : std::vector<fla::Symbol*>(document.symbolList->symbols))
                {
                    const auto found = symbol ? uses.find(symbol->name) : uses.end();
                    if (symbol && (found == uses.end() || found->second == 0))
                    {
                        context.stack().push(fla::CommandPtr(new fla::DeleteSymbolCommand(&document, symbol)));
                        ++removed;
                    }
                }
            }
            for (const QJsonValue& value : libraryJson(document))
            {
                const QJsonObject item = value.toObject();
                const QString name = item["name"].toString();
                const QString kind = item["kind"].toString();
                if (kind == "folder" || name.contains('/'))
                    continue;
                const QString folder = kind == "symbol" ? "Symbols" : kind == "bitmap" ? "Bitmaps" : "Sounds";
                if (!fla::library::isFolder(document, folder.toStdString()))
                    context.stack().push(fla::CommandPtr(new fla::AddFolderCommand(&document, folder.toStdString())));
                std::string problem;
                const fla::library::Renames renames = fla::library::pathRenames(document, name.toStdString(),
                    (folder + "/" + name).toStdString(), &problem);
                if (renames.empty())
                    continue;
                context.stack().push(fla::CommandPtr(new fla::RenameLibraryCommand(&document, renames, "Move")));
                ++moved;
            }
            return Result{QJsonObject{{"moved", moved}, {"removed", removed}}};
        });

    add("bounce", "Bounce",
        "Makes the symbol instance alone on a layer's keyframe bounce in place: up and down a number of times over "
        "the frames given, easing out of each landing and into each fall, squashing as it lands and stretching as "
        "it leaves when squash is on. Builds keyframes and classic tweens.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", prop("integer", "The keyframe it starts on; 1 when left out")},
            {"height", prop("number", "How high it rises, in pixels; 150 when left out")},
            {"bounces", prop("integer", "How many times; 3 when left out")},
            {"frames", prop("integer", "Frames for each bounce; 12 when left out")},
            {"squash", prop("boolean", "Squash and stretch; on when left out")}},
        {"layer"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            fla::Layer& layer = context.layer(timeline, args);
            const int start = ScriptContext::frameIndex(args);
            fla::Frame* key = keyframeAt(layer, start);
            if (!key || key->elements.size() != 1 || !key->elements.front() ||
                key->elements.front()->elementType() != fla::Element::Type::SymbolInstance)
                fail("Put one symbol instance alone on that keyframe first.");
            const double height = std::clamp(args.value("height").toDouble(150.0), 1.0, 10000.0);
            const int bounces = std::clamp(args.value("bounces").toInt(3), 1, 50);
            const int each = std::clamp(args.value("frames").toInt(12), 4, 240);
            const bool squash = args.value("squash").toBool(true);
            const fla::Transform ground = key->elements.front()->transform;
            // How far the instance reaches below its registration point: a
            // squash keeps that edge on the ground.
            const QRectF resting = _window.phoenixView() ? _window.phoenixView()->elementBounds(key->elements.front()) : QRectF();
            const double below = resting.isValid() ? resting.bottom() - ground.ty : 0.0;
            const int startKey = key->index;
            const int half = each / 2;

            fla::Layer* target = &layer;
            _window._selection.clear();
            auto* command = new fla::EditFramesCommand({target}, "Bounce",
                [target, startKey, bounces, each, half, height, squash, ground, below](std::vector<fla::Frame*>&) {
                    // A keyframe at each landing and each top, the instance
                    // placed and shaped for that moment, and the ease of the
                    // tween leaving it: out of a landing fast and slowing to
                    // the top (ease out), falling faster and faster (ease in).
                    struct Key
                    {
                        int index;
                        double rise;
                        double sx;
                        double sy;
                        int ease;
                    };
                    std::vector<Key> keys;
                    for (int b = 0; b < bounces; ++b)
                    {
                        const int land = startKey + b * each;
                        keys.push_back({land, 0.0, squash ? 1.2 : 1.0, squash ? 0.8 : 1.0, 100});
                        if (squash)
                            keys.push_back({land + 1, height * 0.08, 0.9, 1.15, 100});
                        keys.push_back({land + half, height, 1.0, 1.0, -100});
                    }
                    keys.push_back({startKey + bounces * each, 0.0, squash ? 1.2 : 1.0, squash ? 0.8 : 1.0, 0});
                    // Every keyframe first, then the tweens between them,
                    // which each need the next keyframe there.
                    for (const Key& key : keys)
                    {
                        fla::Frame* frame = key.index == startKey ? fla::timeline::spanAt(*target, key.index)
                                                                  : fla::timeline::insertKeyframe(*target, key.index, false);
                        if (!frame || frame->elements.size() != 1)
                            continue;
                        fla::Transform m = ground;
                        m.m11 *= key.sx;
                        m.m12 *= key.sx;
                        m.m21 *= key.sy;
                        m.m22 *= key.sy;
                        m.ty -= key.rise;
                        m.ty += below * (1.0 - key.sy);
                        frame->elements.front()->transform = m;
                    }
                    for (size_t i = 0; i + 1 < keys.size(); ++i)
                    {
                        if (fla::timeline::setTween(*target, keys[i].index, true))
                            if (fla::Frame* span = fla::timeline::spanAt(*target, keys[i].index))
                                span->acceleration = keys[i].ease;
                    }
                });
            context.stack().push(fla::CommandPtr(command));
            return Result{QJsonObject{{"frames", bounces * each + 1}, {"layer", QString::fromStdString(layer.name)}}};
        });

    add("lip_sync", "Lip sync",
        "Keys a mouth to a line of dialogue: the mouth is a graphic symbol instance alone on a layer's keyframe, "
        "whose frames are the mouth shapes. The shapes come from a library sound Phoenix listens to, or from cues "
        "Rhubarb Lip Sync or Papagayo wrote. Each change of shape gets a keyframe showing that shape's frame of "
        "the mouth chart.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg},
            {"frame", prop("integer", "The mouth's keyframe, where the line starts; 1 when left out")},
            {"sound", prop("string", "A library sound to listen to")},
            {"cues", prop("string", "Cues as text instead: Rhubarb's TSV (time, tab, shape a line) or a Papagayo / "
                                    "Moho switch file")},
            {"chart", QJsonObject{{"type", "object"},
                          {"description", "Which frame of the mouth symbol (from 1) shows each shape, such as "
                                          "{\"A\": 1, \"B\": 2, \"X\": 9}; the chart saved on the symbol, or one "
                                          "frame a shape in order, when left out"},
                          {"additionalProperties", QJsonObject{{"type", "integer"}}}}}},
        {"layer"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Layer& layer = context.layer(context.timeline(args), args);
            fla::Frame& key = context.keyframe(layer, args);
            auto* mouth = key.elements.size() == 1 ? dynamic_cast<fla::SymbolInstance*>(key.elements.front()) : nullptr;
            if (!mouth)
                fail("Put the mouth, a graphic symbol instance, alone on that keyframe first.");
            if (layer.isLocked())
                fail("That layer is locked.");
            const double fps = context.document().frameRate > 0.0 ? context.document().frameRate : 24.0;
            std::vector<fla::lipsync::Cue> cues;
            if (args.contains("cues"))
            {
                cues = fla::lipsync::parse(args.value("cues").toString().toStdString(), fps);
            }
            else
            {
                const QString name = args.value("sound").toString();
                if (name.isEmpty())
                    fail("Give a library sound to listen to, or the cues.");
                std::shared_ptr<const PcmBuffer> sound =
                    _window._timelineAudio ? _window._timelineAudio->librarySound(name.toStdString()) : nullptr;
                if (!sound || sound->samples.empty())
                    fail(QString("There is no sound called %1 in the library, or it cannot be read.").arg(name));
                std::vector<int16_t> samples(sound->samples.size());
                for (size_t i = 0; i < samples.size(); ++i)
                    samples[i] = static_cast<int16_t>(std::lround(std::clamp(sound->samples[i], -1.0f, 1.0f) * 32767.0f));
                cues = fla::lipsync::listen(samples, 2, PcmBuffer::kRate, fps);
            }
            if (cues.empty())
                fail("No mouth shapes came of that.");
            // The chart given, else the one kept on the symbol, else one
            // frame a shape in order.
            std::map<std::string, int> chart;
            const QJsonObject given = args.value("chart").toObject();
            for (auto it = given.begin(); it != given.end(); ++it)
                chart[it.key().toStdString()] = std::max(1, it.value().toInt(1));
            if (chart.empty() && mouth->symbol)
                for (const auto& [name, value] : mouth->symbol->extraAttributes)
                    if (name == "phoenixMouthChart")
                        chart = fla::lipsync::chartFrom(value);
            if (chart.empty())
                chart = fla::lipsync::defaultChart(fla::lipsync::shapesOf(cues));
            fla::CommandPtr keys = fla::lipsync::apply(layer, *mouth, cues, chart, key.index, fps);
            if (!keys)
                fail("No mouth shapes came of that.");
            _window._selection.clear();
            if (fla::Symbol* symbol = mouth->symbol)
            {
                using Attributes = std::vector<std::pair<std::string, std::string>>;
                Attributes after = symbol->extraAttributes;
                after.erase(std::remove_if(after.begin(), after.end(),
                                [](const auto& pair) { return pair.first == "phoenixMouthChart"; }),
                    after.end());
                after.emplace_back("phoenixMouthChart", fla::lipsync::chartText(chart));
                context.stack().push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(
                    symbol->extraAttributes, symbol->extraAttributes, after, "Lip Sync")));
            }
            context.stack().push(std::move(keys));
            QJsonObject used;
            for (const auto& [shape, frame] : chart)
                used[QString::fromStdString(shape)] = frame;
            return Result{QJsonObject{{"cues", static_cast<int>(cues.size())}, {"chart", used}}};
        });

    add("in_betweens", "In-betweens",
        "Draws the in-betweens between two keyframes of a layer as keyframes of their own, as an animator would: "
        "the frames between are worked out as a tween (classic for one symbol instance, shape for shapes) with "
        "the ease given, then each becomes a keyframe that can be redrawn by hand. On twos makes one every other "
        "frame, each held for two.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg}, {"frame", prop("integer", "The first keyframe")},
            {"ease", prop("integer", "-100 (ease in, slow start) to 100 (ease out, slow end); 0 when left out")},
            {"onTwos", prop("boolean", "One drawing every two frames; on ones when left out")}},
        {"layer", "frame"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            fla::Layer& layer = context.layer(timeline, args);
            fla::Frame& from = context.keyframe(layer, args);
            const int first = from.index;
            const int next = first + std::max(1, from.duration);
            fla::Frame* to = keyframeAt(layer, next);
            if (!to || to->index != next)
                fail("There has to be a keyframe after it to work toward.");
            if (from.duration < 2)
                fail("The keyframes are next to each other: insert frames between them first.");
            const auto onlyInstance = [](const fla::Frame& frame) {
                return frame.elements.size() == 1 && frame.elements.front() &&
                    frame.elements.front()->elementType() == fla::Element::Type::SymbolInstance;
            };
            const auto allShapes = [](const fla::Frame& frame) {
                return !frame.elements.empty() && std::all_of(frame.elements.begin(), frame.elements.end(),
                    [](const fla::Element* e) { return e && e->elementType() == fla::Element::Type::Shape; });
            };
            const bool classic = onlyInstance(from) && onlyInstance(*to);
            if (!classic && !(allShapes(from) && allShapes(*to)))
                fail("Both keyframes need one symbol instance each, or shapes only, to work out what goes between.");
            const int ease = std::clamp(args.value("ease").toInt(0), -100, 100);
            const bool twos = args.value("onTwos").toBool(false);
            const double fps = context.document().frameRate > 0.0 ? context.document().frameRate : 24.0;
            const int span = from.duration;
            fla::Layer* target = &layer;
            _window._selection.clear();
            auto made = std::make_shared<bool>(true);
            auto* command = new fla::EditFramesCommand({target}, "In-betweens",
                [target, first, span, classic, ease, twos, fps, made](std::vector<fla::Frame*>& removed) {
                    *made = classic ? fla::timeline::setTween(*target, first, true) : fla::timeline::setShapeTween(*target, first);
                    if (!*made)
                        return;
                    if (fla::Frame* tween = fla::timeline::spanAt(*target, first))
                        tween->acceleration = ease;
                    fla::frames::convertToFrameByFrame(*target, first, first + span - 1, fps);
                    // On twos: every other drawing goes, the one before held
                    // over its frame.
                    if (twos)
                        for (int i = first + 1; i < first + span; i += 2)
                            fla::timeline::clearKeyframe(*target, i, removed);
                });
            context.stack().push(fla::CommandPtr(command));
            if (!*made)
                fail("No tween could be made between those keyframes.");
            return Result{QJsonObject{{"drawings", twos ? (span + 1) / 2 - 1 : span - 1},
                {"timeline", timelineJson(timeline, true)}}};
        });

    add("pose_cycle", "Pose cycle",
        "Makes a looping cycle, such as a walk or a run, from poses: the keyframes of a layer from one frame to "
        "another, in order. The cycle goes on a new layer above, the poses' layer is hidden and kept as it was. "
        "Each pose is held for the frames given (or as long as it is now), the cycle repeats, and it can move "
        "across the stage as it goes, so a walk in place walks along.",
        {{"symbol", kSymbolArg}, {"layer", kLayerArg},
            {"from", prop("integer", "The first pose's frame; 1 when left out")},
            {"to", prop("integer", "The last frame of the poses; the layer's end when left out")},
            {"hold", prop("integer", "Frames each pose shows; as long as it is now when left out")},
            {"repeats", prop("integer", "How many times the cycle plays; 4 when left out")},
            {"advance", prop("number", "Pixels the whole cycle moves to the right each time round (negative for "
                                       "left); 0, in place, when left out")},
            {"tween", choice({"none", "classic", "shape"}, "Tweens between the poses; none when left out")}},
        {"layer"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            const int index = context.layerIndex(timeline, args);
            fla::Layer& layer = *timeline.layers[static_cast<size_t>(index)];
            const int from = ScriptContext::frameIndex(args, "from");
            const int to = args.contains("to") ? ScriptContext::frameIndex(args, "to")
                                               : std::max(from, fla::timeline::layerEnd(layer) - 1);
            struct Pose
            {
                const fla::Frame* frame;
                int hold;
            };
            std::vector<Pose> poses;
            for (const fla::Frame* frame : layer.frames)
            {
                if (!frame || frame->index < from || frame->index > to)
                    continue;
                const int own = std::min(frame->index + std::max(1, frame->duration), to + 1) - frame->index;
                poses.push_back({frame, std::max(1, own)});
            }
            if (poses.size() < 2)
                fail("Give a range with at least two pose keyframes.");
            if (args.contains("hold"))
                for (Pose& pose : poses)
                    pose.hold = std::clamp(args.value("hold").toInt(2), 1, 240);
            const int repeats = std::clamp(args.value("repeats").toInt(4), 1, 100);
            const double advance = args.value("advance").toDouble(0.0);
            const QString tween = args.value("tween").toString("none");
            int length = 0;
            for (const Pose& pose : poses)
                length += pose.hold;

            fla::Layer* cycle = fla::newLayerFor(timeline, false);
            cycle->name = layer.name + " cycle";
            cycle->autoNamed = false;
            const int start = poses.front().frame->index;
            int at = start;
            std::vector<int> keys;
            for (int round = 0; round < repeats; ++round)
                for (const Pose& pose : poses)
                {
                    fla::Frame* frame = fla::timeline::insertKeyframe(*cycle, at, true);
                    if (!frame)
                        continue;
                    // Moved along by how far into the whole run it is.
                    const double shift = advance * (at - start) / length;
                    for (const fla::Element* element : pose.frame->elements)
                    {
                        if (!element)
                            continue;
                        fla::Element* copy = fla::cloneElement(*element, frame);
                        copy->transform.tx += shift;
                        frame->elements.push_back(copy);
                    }
                    keys.push_back(at);
                    at += pose.hold;
                }
            // The last pose held for its frames too.
            if (poses.back().hold > 1)
                fla::timeline::insertFrames(*cycle, keys.back(), poses.back().hold - 1);
            if (tween != "none")
                for (size_t i = 0; i + 1 < keys.size(); ++i)
                {
                    if (tween == "classic")
                        fla::timeline::setTween(*cycle, keys[i], true);
                    else
                        fla::timeline::setShapeTween(*cycle, keys[i]);
                }
            _window._selection.clear();
            context.stack().push(fla::CommandPtr(new fla::AddLayerCommand(&timeline, index, cycle, "Pose Cycle")));
            context.stack().push(fla::CommandPtr(
                new fla::SetPropertyCommand<bool>(layer.visible, layer.visible, false, "Pose Cycle")));
            return Result{QJsonObject{{"layer", QString::fromStdString(cycle->name)}, {"frames", repeats * length},
                {"cycleLength", length}}};
        });

    add("apply_palette", "Apply palette",
        "Recolors artwork to a palette: every solid fill and stroke in the scene (or a symbol, or one layer of "
        "it), and in the symbols it shows, takes the palette color nearest its own, keeping its transparency. "
        "A symbol is recolored everywhere it is used. For moving a scene's colors onto a color script, or "
        "tidying near-duplicate colors.",
        {{"symbol", kSymbolArg}, {"layer", prop("string", "Only this layer, by name; every layer when left out")},
            {"colors", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                           {"description", "The palette, as \"#RRGGBB\" colors"}}},
            {"inSymbols", prop("boolean", "Also recolor the symbols shown; on when left out")}},
        {"colors"}, true, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Timeline& timeline = context.timeline(args);
            std::vector<std::array<int, 3>> palette;
            for (const QJsonValue& value : args.value("colors").toArray())
            {
                uint8_t rgba[4];
                if (!parseColor(value.toString(), rgba))
                    fail(QString("%1 is not a color.").arg(value.toString()));
                palette.push_back({rgba[0], rgba[1], rgba[2]});
            }
            if (palette.empty())
                fail("Give the palette's colors.");
            // Nearest as the eye judges it, roughly: red and blue weighed by
            // how red the pair is (the "redmean" distance).
            const auto nearest = [&palette](uint8_t rgba[4]) {
                double best = 1.0e300;
                const std::array<int, 3>* chosen = &palette.front();
                for (const auto& color : palette)
                {
                    const double mean = (rgba[0] + color[0]) / 2.0;
                    const double dr = rgba[0] - color[0], dg = rgba[1] - color[1], db = rgba[2] - color[2];
                    const double d = (2.0 + mean / 256.0) * dr * dr + 4.0 * dg * dg + (2.0 + (255.0 - mean) / 256.0) * db * db;
                    if (d < best)
                    {
                        best = d;
                        chosen = &color;
                    }
                }
                bool changed = false;
                for (int c = 0; c < 3; ++c)
                {
                    changed = changed || rgba[c] != (*chosen)[static_cast<size_t>(c)];
                    rgba[c] = static_cast<uint8_t>((*chosen)[static_cast<size_t>(c)]);
                }
                return changed;
            };
            const QString only = args.value("layer").toString();
            const bool inSymbols = args.value("inSymbols").toBool(true);
            int shapes = 0;
            int colors = 0;
            std::set<const fla::Shape*> seen;
            std::set<const fla::Symbol*> symbolsSeen;
            std::function<void(fla::Element*)> visit;
            const auto visitTimeline = [&](fla::Timeline& inside) {
                for (fla::Layer* layer : inside.layers)
                    if (layer && !layer->isLocked())
                        for (fla::Frame* frame : layer->frames)
                            if (frame)
                                for (fla::Element* element : frame->elements)
                                    visit(element);
            };
            visit = [&](fla::Element* element) {
                if (!element)
                    return;
                if (auto* group = dynamic_cast<fla::Group*>(element))
                {
                    for (fla::Element* member : group->members)
                        visit(member);
                    return;
                }
                if (auto* instance = dynamic_cast<fla::SymbolInstance*>(element))
                {
                    if (inSymbols && instance->symbol && symbolsSeen.insert(instance->symbol).second)
                        for (fla::Timeline* inside : instance->symbol->timelines)
                            if (inside)
                                visitTimeline(*inside);
                    return;
                }
                auto* shape = dynamic_cast<fla::Shape*>(element);
                if (!shape || !seen.insert(shape).second)
                    return;
                std::unique_ptr<fla::Element> copy(fla::cloneElement(*shape, shape->parent));
                auto* after = dynamic_cast<fla::Shape*>(copy.get());
                if (!after)
                    return;
                int changed = 0;
                for (fla::FillStyle* fill : after->fills)
                    if (auto* solid = dynamic_cast<fla::SolidColor*>(fill))
                        changed += nearest(solid->color) ? 1 : 0;
                for (fla::StrokeStyle* stroke : after->strokes)
                    if (auto* solid = stroke ? dynamic_cast<fla::SolidColor*>(stroke->fill) : nullptr)
                        changed += nearest(solid->color) ? 1 : 0;
                if (changed == 0)
                    return;
                context.stack().push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, "Apply Palette")));
                ++shapes;
                colors += changed;
            };
            bool found = only.isEmpty();
            for (fla::Layer* layer : timeline.layers)
            {
                if (!layer || (!only.isEmpty() && QString::fromStdString(layer->name) != only))
                    continue;
                found = true;
                if (layer->isLocked())
                    continue;
                for (fla::Frame* frame : layer->frames)
                    if (frame)
                        for (fla::Element* element : frame->elements)
                            visit(element);
            }
            if (!found)
                fail(QString("There is no layer called %1.").arg(only));
            return Result{QJsonObject{{"shapes", shapes}, {"colorsChanged", colors},
                {"symbols", static_cast<int>(symbolsSeen.size())}}};
        });

    add("color_script", "Color script",
        "A strip of small pictures across the scene, one every so many frames or one at each frame label, so the "
        "colors and light can be judged over time, as a color script shows them. Returns the picture and which "
        "frame each one is.",
        {{"every", prop("integer", "Frames between pictures; one a second when left out")},
            {"labels", prop("boolean", "One picture at each frame label instead")},
            {"width", prop("integer", "Each picture's width in pixels, 48 to 480; 160 when left out")},
            {"columns", prop("integer", "Pictures a row; 6 when left out")}},
        {}, false, [this](const QJsonObject& args) {
            ScriptContext context{_window};
            fla::Document& document = context.document();
            fla::Timeline& timeline = context.timeline(QJsonObject());
            PhoenixView* view = _window.phoenixView();
            if (!view)
                fail("There is no stage to draw with.");
            int end = 1;
            for (const fla::Layer* layer : timeline.layers)
                if (layer)
                    end = std::max(end, fla::timeline::layerEnd(*layer));
            std::vector<int> frames;
            if (args.value("labels").toBool(false))
            {
                std::set<int> labeled;
                for (const fla::Layer* layer : timeline.layers)
                    if (layer)
                        for (const fla::Frame* frame : layer->frames)
                            if (frame && !frame->name.empty() && frame->labelType != "comment")
                                labeled.insert(frame->index);
                frames.assign(labeled.begin(), labeled.end());
                if (frames.empty())
                    fail("The scene has no frame labels.");
            }
            else
            {
                const int every = std::max(1, args.value("every").toInt(static_cast<int>(std::lround(
                    document.frameRate > 0.0 ? document.frameRate : 24.0))));
                for (int f = 0; f < end; f += every)
                    frames.push_back(f);
            }
            if (frames.size() > 60)
                fail(QString("That would be %1 pictures; choose a larger \"every\" (60 at most).").arg(frames.size()));
            const int width = std::clamp(args.value("width").toInt(160), 48, 480);
            const int columns = std::clamp(args.value("columns").toInt(6), 1, 20);
            const int height = std::max(1, static_cast<int>(std::lround(
                width * static_cast<double>(document.height) / std::max(1, document.width))));
            const int caption = 16;
            const int gap = 4;
            const int rows = (static_cast<int>(frames.size()) + columns - 1) / columns;
            const int across = std::min(columns, static_cast<int>(frames.size()));
            QImage strip(across * (width + gap) + gap, rows * (height + caption + gap) + gap, QImage::Format_ARGB32);
            strip.fill(QColor(30, 30, 30));
            QPainter painter(&strip);
            painter.setPen(QColor(220, 220, 220));
            QJsonArray shown;
            for (size_t i = 0; i < frames.size(); ++i)
            {
                const int x = gap + static_cast<int>(i % static_cast<size_t>(columns)) * (width + gap);
                const int y = gap + static_cast<int>(i / static_cast<size_t>(columns)) * (height + caption + gap);
                const QImage picture = view->renderFrame(frames[i], QSize(width, height), false);
                painter.drawImage(x, y, picture);
                painter.drawText(QRect(x + 3, y + height, width - 3, caption), Qt::AlignLeft | Qt::AlignVCenter,
                    QString::number(frames[i] + 1));
                shown.append(frames[i] + 1);
            }
            painter.end();
            return Result{QJsonObject{{"frames", shown}, {"width", strip.width()}, {"height", strip.height()}}, strip};
        });
}

QJsonArray ScriptApi::resources() const
{
    QJsonArray list{QJsonObject{{"uri", "phoenix://document"}, {"name", "Document"},
                        {"description", "The open document's summary"}, {"mimeType", "application/json"}},
        QJsonObject{{"uri", "phoenix://timeline"}, {"name", "Timeline"},
            {"description", "The scene's layers and keyframes"}, {"mimeType", "application/json"}}};
    const fla::Document* document =
        _window._flaDocument && _window._flaDocument->document ? _window._flaDocument->document : nullptr;
    if (document && document->symbolList)
    {
        for (const fla::Symbol* symbol : document->symbolList->symbols)
        {
            if (symbol)
                list.append(QJsonObject{{"uri", "phoenix://symbol/" + QString::fromStdString(symbol->name)},
                    {"name", QString::fromStdString(symbol->name)}, {"description", "A symbol's layers and keyframes"},
                    {"mimeType", "application/json"}});
        }
    }
    return list;
}

ScriptApi::Result ScriptApi::readResource(const QString& uri)
{
    if (uri == "phoenix://document")
        return call("get_document", {});
    if (uri == "phoenix://timeline")
        return call("get_timeline", {});
    const QString prefix = "phoenix://symbol/";
    if (uri.startsWith(prefix))
        return call("get_timeline", QJsonObject{{"symbol", uri.mid(prefix.size())}});
    return {QJsonValue(), QImage(), QString("There is no resource %1.").arg(uri)};
}
