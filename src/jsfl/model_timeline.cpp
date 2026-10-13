// Timeline, Layer and Frame: layers and their frames, keyframes, tweens,
// labels, and the frame and layer selections scripts work on.

#include "model.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/layer_commands.h"
#include "../edit/property_command.h"
#include "../edit/selection.h"
#include "../edit/timeline_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>

namespace jsfl {

namespace {

/// Puts a frame's script in or takes it out, keeping whichever is out.
class SetActionScriptCommand : public fla::Command
{
public:
    SetActionScriptCommand(fla::Frame* frame, const std::string& code)
        : _frame(frame)
    {
        if (!code.empty())
        {
            _other = new fla::ActionScript(frame);
            _other->code = code;
        }
    }
    ~SetActionScriptCommand() override { delete _other; }
    SetActionScriptCommand(const SetActionScriptCommand&) = delete;
    SetActionScriptCommand& operator=(const SetActionScriptCommand&) = delete;
    void redo() override { std::swap(_frame->actionScript, _other); }
    void undo() override { std::swap(_frame->actionScript, _other); }
    std::string name() const override { return "Actions"; }

private:
    fla::Frame* _frame;
    fla::ActionScript* _other = nullptr;
};

std::string attribute(const fla::DOMElement& object, const std::string& key)
{
    for (const auto& entry : object.extraAttributes)
    {
        if (entry.first == key)
            return entry.second;
    }
    return {};
}

/// Sets (or, for an empty value, removes) one of the attributes an object
/// keeps as read.
void setAttribute(DocumentPart& part, fla::DOMElement& object, const std::string& key, const std::string& value)
{
    std::vector<std::pair<std::string, std::string>> after = object.extraAttributes;
    auto found = std::find_if(after.begin(), after.end(), [&key](const auto& entry) { return entry.first == key; });
    if (value.empty())
    {
        if (found == after.end())
            return;
        after.erase(found);
    }
    else if (found != after.end())
        found->second = value;
    else
        after.emplace_back(key, value);
    part.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                  object.extraAttributes, object.extraAttributes, after, key)),
        false);
}

fla::Timeline* timelineOf(const fla::Layer& layer)
{
    return dynamic_cast<fla::Timeline*>(layer.parent);
}

int indexOf(const fla::Timeline& timeline, const fla::Layer* layer)
{
    const auto found = std::find(timeline.layers.begin(), timeline.layers.end(), layer);
    return found == timeline.layers.end() ? -1 : static_cast<int>(found - timeline.layers.begin());
}

bool isEdited(const Timeline& t)
{
    return t.session.host().editedTimeline(t.document) == t.timeline;
}

int currentFrameOf(const Timeline& t)
{
    return isEdited(t) ? t.session.host().currentFrame(t.document) : t.session.position(t.timeline).frame;
}

int currentLayerOf(const Timeline& t)
{
    const int count = static_cast<int>(t.get().layers.size());
    const int layer = isEdited(t) ? t.session.host().currentLayer(t.document) : t.session.position(t.timeline).layer;
    return std::clamp(layer, 0, std::max(0, count - 1));
}

int frameCountOf(const fla::Timeline& timeline)
{
    int count = 0;
    for (const fla::Layer* layer : timeline.layers)
    {
        if (layer)
            count = std::max(count, fla::timeline::layerEnd(*layer));
    }
    return count;
}

fla::Layer* layerAt(Timeline& t, int index)
{
    fla::Timeline& timeline = t.get();
    if (index < 0 || index >= static_cast<int>(timeline.layers.size()) || !timeline.layers[static_cast<size_t>(index)])
        throw Error("There is no layer " + std::to_string(index) + "; the timeline has " +
                    std::to_string(timeline.layers.size()) + ".");
    return timeline.layers[static_cast<size_t>(index)];
}

/// The layers frame operations act on: the selected ones, else the current.
std::vector<fla::Layer*> selectedLayers(Timeline& t)
{
    std::vector<int>& chosen = t.session.layerSelection(t.document, t.timeline);
    std::vector<fla::Layer*> layers;
    for (int index : chosen)
    {
        if (index >= 0 && index < static_cast<int>(t.get().layers.size()))
        {
            fla::Layer* layer = t.get().layers[static_cast<size_t>(index)];
            if (layer && layer->layerType != fla::Layer::Type::Folder)
                layers.push_back(layer);
        }
    }
    if (layers.empty())
    {
        fla::Layer* current = layerAt(t, currentLayerOf(t));
        if (current->layerType != fla::Layer::Type::Folder)
            layers.push_back(current);
    }
    return layers;
}

/// Frames to work on: \a first to \a last (included) of a layer.
struct Span
{
    fla::Layer* layer;
    int first;
    int last;
};

/// The frames a range method works on. With a start (and an end, not
/// included) given at \a at, those frames of the selected layers; else the
/// selected frames, else the playhead on the current layer.
std::vector<Span> spansFor(Timeline& t, const Args& args, size_t at)
{
    std::vector<Span> spans;
    if (given(args, at))
    {
        const int first = std::max(0, arg(args, at).toInt());
        const int last = given(args, at + 1) ? std::max(first, arg(args, at + 1).toInt() - 1) : first;
        for (fla::Layer* layer : selectedLayers(t))
            spans.push_back({layer, first, last});
        return spans;
    }
    for (const FrameRange& range : t.session.frameSelection(t.document, t.timeline))
    {
        if (range.layer >= 0 && range.layer < static_cast<int>(t.get().layers.size()) && t.get().layers[static_cast<size_t>(range.layer)])
            spans.push_back({t.get().layers[static_cast<size_t>(range.layer)], range.first, range.last});
    }
    if (spans.empty())
    {
        const int frame = currentFrameOf(t);
        for (fla::Layer* layer : selectedLayers(t))
            spans.push_back({layer, frame, frame});
    }
    return spans;
}

/// Runs \a edit on each span's layer as one frame edit.
void editFrames(Timeline& t, const std::vector<Span>& spans, const char* name,
    const std::function<void(const Span&, std::vector<fla::Frame*>& removed)>& edit)
{
    if (spans.empty())
        return;
    std::vector<fla::Layer*> layers;
    for (const Span& span : spans)
    {
        t.require(span.layer, "layer");
        if (std::find(layers.begin(), layers.end(), span.layer) == layers.end())
            layers.push_back(span.layer);
    }
    // Frames taken off may hold what is selected.
    t.session.host().selection(t.document).clear();
    t.push(fla::CommandPtr(new fla::EditFramesCommand(layers, name, [spans, edit](std::vector<fla::Frame*>& removed) {
        for (const Span& span : spans)
            edit(span, removed);
    })));
}

/// The start of each keyframe span touching frames first to last.
std::vector<int> keyframesIn(fla::Layer& layer, int first, int last)
{
    std::vector<int> starts;
    for (const fla::Frame* frame : layer.frames)
    {
        if (frame && frame->index <= last && frame->index + std::max(1, frame->duration) > first)
            starts.push_back(frame->index);
    }
    return starts;
}

/// Sets \a name on every keyframe of \a spans, through Frame's own setter.
void setOnFrames(Timeline& t, const std::vector<Span>& spans, const std::string& name, const Value& value)
{
    std::vector<fla::Frame*> done;
    for (const Span& span : spans)
    {
        for (int start : keyframesIn(*span.layer, span.first, span.last))
        {
            fla::Frame* frame = fla::timeline::spanAt(*span.layer, start);
            if (!frame || std::find(done.begin(), done.end(), frame) != done.end())
                continue;
            done.push_back(frame);
            Frame wrapper(t.session, t.document, span.layer, start);
            const ClassInfo& info = wrapper.classInfo();
            const auto found = std::find_if(info.properties.begin(), info.properties.end(),
                [&name](const Property& p) { return p.name == name; });
            if (found == info.properties.end() || !found->set)
                throw Error("Frames have no property " + name + " to set.");
            found->set(wrapper, value);
        }
    }
}

/// Frames copied by copyFrames or cutFrames, a row for each layer, kept
/// for pasting until the next copy (between runs too, as a clipboard is).
std::vector<std::vector<std::unique_ptr<fla::Frame>>>& frameClipboard()
{
    static std::vector<std::vector<std::unique_ptr<fla::Frame>>> rows;
    return rows;
}

void frameClipboardCopy(Timeline& t, const std::vector<Span>& spans)
{
    std::vector<std::vector<std::unique_ptr<fla::Frame>>> rows;
    for (const Span& span : spans)
    {
        t.require(span.layer, "layer");
        std::vector<std::unique_ptr<fla::Frame>> row;
        for (fla::Frame* frame : fla::timeline::copyFrames(*span.layer, span.first, span.last))
            row.emplace_back(frame);
        rows.push_back(std::move(row));
    }
    if (!rows.empty())
        frameClipboard() = std::move(rows);
}

void frameClipboardPaste(Timeline& t, const std::vector<Span>& spans)
{
    const auto& rows = frameClipboard();
    if (rows.empty())
        throw Error("No frames have been copied.");
    // The copied rows go onto the layers in order, from the first; one row
    // goes onto every layer.
    std::vector<Span> targets;
    std::vector<std::vector<const fla::Frame*>> copied;
    for (size_t i = 0; i < spans.size(); ++i)
    {
        if (i >= rows.size() && rows.size() > 1)
            break;
        const auto& row = rows[std::min(i, rows.size() - 1)];
        std::vector<const fla::Frame*> frames;
        for (const auto& frame : row)
            frames.push_back(frame.get());
        targets.push_back(spans[i]);
        copied.push_back(frames);
    }
    std::map<const fla::Layer*, std::vector<const fla::Frame*>> byLayer;
    for (size_t i = 0; i < targets.size(); ++i)
        byLayer[targets[i].layer] = copied[i];
    editFrames(t, targets, "Paste Frames", [byLayer](const Span& span, std::vector<fla::Frame*>& removed) {
        fla::timeline::pasteFrames(*span.layer, span.first, byLayer.at(span.layer), removed);
    });
}

/// The layers setLayerProperty changes.
std::vector<fla::Layer*> layersFor(Timeline& t, const std::string& which)
{
    std::vector<fla::Layer*> chosen;
    const std::vector<int>& selected = t.session.layerSelection(t.document, t.timeline);
    const fla::Timeline& timeline = t.get();
    for (size_t i = 0; i < timeline.layers.size(); ++i)
    {
        const bool isSelected = std::find(selected.begin(), selected.end(), static_cast<int>(i)) != selected.end();
        if (which == "all" || (which == "others" ? !isSelected : isSelected))
            chosen.push_back(timeline.layers[i]);
    }
    if (chosen.empty() && which != "others" && which != "all")
        chosen.push_back(layerAt(t, currentLayerOf(t)));
    return chosen;
}

void setOnLayer(Timeline& t, fla::Layer* layer, const std::string& name, const Value& value)
{
    Layer wrapper(t.session, t.document, layer);
    const ClassInfo& info = wrapper.classInfo();
    const auto found = std::find_if(info.properties.begin(), info.properties.end(),
        [&name](const Property& p) { return p.name == name; });
    if (found == info.properties.end() || !found->set)
        throw Error("Layers have no property " + name + " to set.");
    found->set(wrapper, value);
}

/// Changes one layer through a snapshot of its timeline, as one edit.
void editLayer(Layer& l, const char* name, const std::function<void(fla::LayerSnapshot&, fla::LayerState&)>& edit)
{
    fla::Layer& layer = l.get();
    fla::Timeline* timeline = timelineOf(layer);
    if (!timeline)
        throw Error("That layer is not on a timeline.");
    const fla::LayerSnapshot before = fla::snapshotLayers(*timeline);
    fla::LayerSnapshot after = before;
    for (fla::LayerState& state : after)
    {
        if (state.layer == &layer)
        {
            edit(after, state);
            break;
        }
    }
    l.push(fla::CommandPtr(new fla::LayerEditCommand(timeline, before, after, name)), false);
}

std::string kindName(fla::layers::Kind kind)
{
    switch (kind)
    {
    case fla::layers::Kind::Guide: return "guide";
    case fla::layers::Kind::Guided: return "guided";
    case fla::layers::Kind::Mask: return "mask";
    case fla::layers::Kind::Masked: return "masked";
    case fla::layers::Kind::Folder: return "folder";
    default: return "normal";
    }
}

fla::layers::Kind kindNamed(const std::string& name)
{
    if (name == "guide")
        return fla::layers::Kind::Guide;
    if (name == "guided")
        return fla::layers::Kind::Guided;
    if (name == "mask")
        return fla::layers::Kind::Mask;
    if (name == "masked")
        return fla::layers::Kind::Masked;
    if (name == "folder")
        return fla::layers::Kind::Folder;
    if (name == "normal")
        return fla::layers::Kind::Normal;
    throw Error("A layer is normal, guide, guided, mask, masked or folder, not " + name + ".");
}

} // namespace

fla::Timeline& Timeline::get() const
{
    require(timeline, "timeline");
    return *timeline;
}

fla::Layer& Layer::get() const
{
    require(layer, "layer");
    return *layer;
}

fla::Frame& Frame::get() const
{
    require(layer, "layer");
    fla::Frame* frame = fla::timeline::spanAt(*layer, index);
    if (!frame)
        throw Error("Frame " + std::to_string(index) + " is past the end of layer " + layer->name + ".");
    return *frame;
}

std::shared_ptr<Timeline> wrapTimeline(Session& session, fla::FLADocument* document, fla::Timeline* timeline)
{
    return timeline ? std::make_shared<Timeline>(session, document, timeline) : nullptr;
}

std::shared_ptr<Layer> wrapLayer(Session& session, fla::FLADocument* document, fla::Layer* layer)
{
    return layer ? std::make_shared<Layer>(session, document, layer) : nullptr;
}

const ClassInfo& Timeline::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Timeline> def(info, "Timeline");
        def.prop("name",
            [](Timeline& t) {
                // A symbol's timeline goes by the symbol's name.
                if (auto* symbol = dynamic_cast<fla::Symbol*>(t.get().parent))
                {
                    const size_t slash = symbol->name.rfind('/');
                    return Value(slash == std::string::npos ? symbol->name : symbol->name.substr(slash + 1));
                }
                return Value(t.get().name);
            },
            [](Timeline& t, const Value& v) {
                fla::Timeline& timeline = t.get();
                t.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(timeline.name, timeline.name, v.toString(), "Rename")), false);
            });
        def.get("layers", [](Timeline& t) {
            Value list = Value::array();
            for (fla::Layer* layer : t.get().layers)
                list.items.emplace_back(wrapLayer(t.session, t.document, layer));
            return list;
        });
        def.get("layerCount", [](Timeline& t) { return Value(static_cast<int>(t.get().layers.size())); });
        def.get("frameCount", [](Timeline& t) { return Value(frameCountOf(t.get())); });
        def.get("libraryItem", [](Timeline& t) {
            auto* symbol = dynamic_cast<fla::Symbol*>(t.get().parent);
            return symbol ? Value(wrapSymbol(t.session, t.document, symbol)) : Value();
        });
        def.prop("currentFrame", [](Timeline& t) { t.get(); return Value(currentFrameOf(t)); },
            [](Timeline& t, const Value& v) {
                t.get();
                const int frame = std::max(0, v.toInt());
                if (isEdited(t))
                    t.session.host().setCurrentFrame(t.document, frame);
                else
                    t.session.position(t.timeline).frame = frame;
            });
        def.prop("currentLayer", [](Timeline& t) { t.get(); return Value(currentLayerOf(t)); },
            [](Timeline& t, const Value& v) {
                const int index = v.toInt();
                layerAt(t, index);
                if (isEdited(t))
                    t.session.host().setCurrentLayer(t.document, index);
                else
                    t.session.position(t.timeline).layer = index;
                t.session.layerSelection(t.document, t.timeline) = {index};
            });
        def.method("addNewLayer", 3, [](Timeline& t, const Args& args) {
            fla::Timeline& timeline = t.get();
            const std::string type = given(args, 1) ? arg(args, 1).toString() : "normal";
            const fla::layers::Kind kind = kindNamed(type);
            const bool above = given(args, 2) ? arg(args, 2).toBool() : true;
            const int current = timeline.layers.empty() ? 0 : currentLayerOf(t);
            const int index = timeline.layers.empty() ? 0 : above ? current : current + 1;
            fla::Layer* layer = fla::newLayerFor(timeline, kind == fla::layers::Kind::Folder);
            if (given(args, 0))
            {
                layer->name = arg(args, 0).toString();
                layer->autoNamed = false;
            }
            if (kind == fla::layers::Kind::Guide)
                layer->layerType = fla::Layer::Type::Guide;
            else if (kind == fla::layers::Kind::Mask)
                layer->layerType = fla::Layer::Type::Mask;
            t.push(fla::CommandPtr(new fla::AddLayerCommand(&timeline, index, layer, "Add Layer")));
            if (kind == fla::layers::Kind::Guided || kind == fla::layers::Kind::Masked)
            {
                fla::LayerSnapshot before = fla::snapshotLayers(timeline);
                fla::LayerSnapshot after = before;
                if (fla::layers::setKind(after, layer, kind))
                    t.push(fla::CommandPtr(new fla::LayerEditCommand(&timeline, before, after, "Layer Properties")), false);
            }
            const int placed = indexOf(timeline, layer);
            if (isEdited(t))
                t.session.host().setCurrentLayer(t.document, placed);
            else
                t.session.position(t.timeline).layer = placed;
            t.session.layerSelection(t.document, t.timeline) = {placed};
            return Value(placed);
        });
        def.method("deleteLayer", 1, [](Timeline& t, const Args& args) {
            fla::Timeline& timeline = t.get();
            std::vector<fla::Layer*> doomed;
            if (given(args, 0))
                doomed.push_back(layerAt(t, arg(args, 0).toInt()));
            else
            {
                for (int index : t.session.layerSelection(t.document, t.timeline))
                {
                    if (index >= 0 && index < static_cast<int>(timeline.layers.size()))
                        doomed.push_back(timeline.layers[static_cast<size_t>(index)]);
                }
                if (doomed.empty())
                    doomed.push_back(layerAt(t, currentLayerOf(t)));
            }
            t.session.host().selection(t.document).clear();
            for (fla::Layer* layer : doomed)
            {
                const int index = indexOf(timeline, layer);
                // A timeline keeps one layer, as in the timeline itself.
                if (index < 0 || timeline.layers.size() <= 1)
                    continue;
                t.push(fla::CommandPtr(new fla::RemoveLayerCommand(&timeline, index, "Delete Layer")));
            }
            t.session.layerSelection(t.document, t.timeline).clear();
            const int count = static_cast<int>(timeline.layers.size());
            if (isEdited(t))
                t.session.host().setCurrentLayer(t.document, std::clamp(t.session.host().currentLayer(t.document), 0, count - 1));
            return Value();
        });
        def.method("setSelectedLayers", 2, [](Timeline& t, const Args& args) {
            const int index = arg(args, 0).toInt();
            layerAt(t, index);
            std::vector<int>& selected = t.session.layerSelection(t.document, t.timeline);
            if (!given(args, 1) || arg(args, 1).toBool())
                selected.clear();
            if (std::find(selected.begin(), selected.end(), index) == selected.end())
                selected.push_back(index);
            if (isEdited(t))
                t.session.host().setCurrentLayer(t.document, index);
            else
                t.session.position(t.timeline).layer = index;
            return Value();
        });
        def.method("getSelectedLayers", 0, [](Timeline& t, const Args&) {
            t.get();
            std::vector<int> selected = t.session.layerSelection(t.document, t.timeline);
            std::sort(selected.begin(), selected.end());
            return Value::list(selected);
        });
        def.method("findLayerIndex", 1, [](Timeline& t, const Args& args) {
            const std::string name = arg(args, 0).toString();
            Value found = Value::array();
            const fla::Timeline& timeline = t.get();
            for (size_t i = 0; i < timeline.layers.size(); ++i)
            {
                if (timeline.layers[i] && timeline.layers[i]->name == name)
                    found.items.emplace_back(static_cast<int>(i));
            }
            return found.items.empty() ? Value() : found;
        });
        def.method("reorderLayer", 3, [](Timeline& t, const Args& args) {
            fla::Timeline& timeline = t.get();
            fla::Layer* moving = layerAt(t, arg(args, 0).toInt());
            fla::Layer* target = layerAt(t, arg(args, 1).toInt());
            const bool before = given(args, 2) ? arg(args, 2).toBool() : true;
            if (moving == target)
                return Value();
            fla::LayerSnapshot snapshot = fla::snapshotLayers(timeline);
            const fla::LayerSnapshot original = snapshot;
            // Above the target, or above whatever comes after it.
            fla::Layer* above = target;
            fla::Layer* parent = nullptr;
            for (const fla::LayerState& state : snapshot)
            {
                if (state.layer == target)
                    parent = state.parent;
            }
            if (!before)
            {
                above = nullptr;
                const int at = indexOf(timeline, target);
                for (size_t i = static_cast<size_t>(at) + 1; i < timeline.layers.size(); ++i)
                {
                    if (timeline.layers[i] != moving)
                    {
                        above = timeline.layers[i];
                        break;
                    }
                }
            }
            if (!fla::layers::moveLayer(snapshot, moving, above, parent))
                throw Error("That layer cannot move there.");
            t.push(fla::CommandPtr(new fla::LayerEditCommand(&timeline, original, snapshot, "Move Layer")), false);
            return Value();
        });
        def.method("insertFrames", 3, [](Timeline& t, const Args& args) {
            const int count = given(args, 0) ? std::max(1, arg(args, 0).toInt()) : 1;
            const int at = given(args, 2) ? std::max(0, arg(args, 2).toInt()) : currentFrameOf(t);
            std::vector<Span> spans;
            if (arg(args, 1).toBool())
            {
                for (fla::Layer* layer : t.get().layers)
                {
                    if (layer && layer->layerType != fla::Layer::Type::Folder)
                        spans.push_back({layer, at, at});
                }
            }
            else
            {
                for (fla::Layer* layer : selectedLayers(t))
                    spans.push_back({layer, at, at});
            }
            editFrames(t, spans, "Insert Frame", [count](const Span& span, std::vector<fla::Frame*>&) {
                fla::timeline::insertFrames(*span.layer, span.first, count);
            });
            return Value();
        });
        def.method("removeFrames", 2, [](Timeline& t, const Args& args) {
            editFrames(t, spansFor(t, args, 0), "Remove Frames", [](const Span& span, std::vector<fla::Frame*>& removed) {
                fla::timeline::removeFrames(*span.layer, span.first, span.last - span.first + 1, removed);
            });
            return Value();
        });
        def.method("insertKeyframe", 1, [](Timeline& t, const Args& args) {
            const int at = given(args, 0) ? std::max(0, arg(args, 0).toInt()) : currentFrameOf(t);
            std::vector<Span> spans;
            for (fla::Layer* layer : selectedLayers(t))
                spans.push_back({layer, at, at});
            editFrames(t, spans, "Insert Keyframe", [](const Span& span, std::vector<fla::Frame*>&) {
                fla::timeline::insertKeyframe(*span.layer, span.first, false);
            });
            return Value();
        });
        def.method("insertBlankKeyframe", 1, [](Timeline& t, const Args& args) {
            const int at = given(args, 0) ? std::max(0, arg(args, 0).toInt()) : currentFrameOf(t);
            std::vector<Span> spans;
            for (fla::Layer* layer : selectedLayers(t))
                spans.push_back({layer, at, at});
            editFrames(t, spans, "Insert Blank Keyframe", [](const Span& span, std::vector<fla::Frame*>&) {
                fla::timeline::insertKeyframe(*span.layer, span.first, true);
            });
            return Value();
        });
        def.method("convertToKeyframes", 2, [](Timeline& t, const Args& args) {
            editFrames(t, spansFor(t, args, 0), "Convert to Keyframes", [](const Span& span, std::vector<fla::Frame*>&) {
                for (int i = span.first; i <= span.last; ++i)
                    fla::timeline::insertKeyframe(*span.layer, i, false);
            });
            return Value();
        });
        def.method("convertToBlankKeyframes", 2, [](Timeline& t, const Args& args) {
            editFrames(t, spansFor(t, args, 0), "Convert to Blank Keyframes", [](const Span& span, std::vector<fla::Frame*>& removed) {
                fla::timeline::clearFrames(*span.layer, span.first, span.first, removed);
                for (int i = span.first + 1; i <= span.last; ++i)
                    fla::timeline::insertKeyframe(*span.layer, i, true);
            });
            return Value();
        });
        def.method("clearKeyframes", 2, [](Timeline& t, const Args& args) {
            editFrames(t, spansFor(t, args, 0), "Clear Keyframe", [](const Span& span, std::vector<fla::Frame*>& removed) {
                for (int start : keyframesIn(*span.layer, span.first, span.last))
                {
                    if (start >= span.first && start > 0)
                        fla::timeline::clearKeyframe(*span.layer, start, removed);
                }
            });
            return Value();
        });
        def.method("clearFrames", 2, [](Timeline& t, const Args& args) {
            editFrames(t, spansFor(t, args, 0), "Clear Frames", [](const Span& span, std::vector<fla::Frame*>& removed) {
                fla::timeline::clearFrames(*span.layer, span.first, span.last, removed);
            });
            return Value();
        });
        def.method("createMotionTween", 2, [](Timeline& t, const Args& args) {
            editFrames(t, spansFor(t, args, 0), "Create Classic Tween", [](const Span& span, std::vector<fla::Frame*>&) {
                for (int start : keyframesIn(*span.layer, span.first, span.last))
                {
                    if (!fla::timeline::setTween(*span.layer, start, true))
                    {
                        // With no keyframe after it to tween to, the span
                        // still says it is tweened, as Animate keeps it.
                        if (fla::Frame* frame = fla::timeline::spanAt(*span.layer, start))
                            frame->tweenType = fla::TweenType::Motion;
                    }
                }
            });
            return Value();
        });
        def.method("removeMotionTween", 2, [](Timeline& t, const Args& args) {
            editFrames(t, spansFor(t, args, 0), "Remove Tween", [](const Span& span, std::vector<fla::Frame*>&) {
                for (int start : keyframesIn(*span.layer, span.first, span.last))
                {
                    if (fla::Frame* frame = fla::timeline::spanAt(*span.layer, start))
                        frame->tweenType = fla::TweenType::None;
                }
            });
            return Value();
        });
        def.method("setSelectedFrames", 3, [](Timeline& t, const Args& args) {
            fla::Timeline& timeline = t.get();
            std::vector<FrameRange>& selected = t.session.frameSelection(t.document, t.timeline);
            if (arg(args, 0).type == Value::Type::Array)
            {
                if (!given(args, 1) || arg(args, 1).toBool())
                    selected.clear();
                const std::vector<Value>& list = arg(args, 0).items;
                for (size_t i = 0; i + 2 < list.size(); i += 3)
                {
                    const int layer = list[i].toInt();
                    layerAt(t, layer);
                    const int first = std::max(0, list[i + 1].toInt());
                    selected.push_back({layer, first, std::max(first, list[i + 2].toInt() - 1)});
                }
            }
            else
            {
                if (!given(args, 2) || arg(args, 2).toBool())
                    selected.clear();
                const int first = std::max(0, arg(args, 0).toInt());
                const int last = given(args, 1) ? std::max(first, arg(args, 1).toInt() - 1) : first;
                for (fla::Layer* layer : selectedLayers(t))
                    selected.push_back({indexOf(timeline, layer), first, last});
            }
            // The layers of the frames become the selected layers.
            std::vector<int>& layers = t.session.layerSelection(t.document, t.timeline);
            layers.clear();
            for (const FrameRange& range : selected)
            {
                if (std::find(layers.begin(), layers.end(), range.layer) == layers.end())
                    layers.push_back(range.layer);
            }
            return Value();
        });
        def.method("getSelectedFrames", 0, [](Timeline& t, const Args&) {
            t.get();
            Value list = Value::array();
            for (const FrameRange& range : t.session.frameSelection(t.document, t.timeline))
            {
                list.items.emplace_back(range.layer);
                list.items.emplace_back(range.first);
                list.items.emplace_back(range.last + 1);
            }
            return list;
        });
        def.method("selectAllFrames", 0, [](Timeline& t, const Args&) {
            const fla::Timeline& timeline = t.get();
            std::vector<FrameRange>& selected = t.session.frameSelection(t.document, t.timeline);
            selected.clear();
            for (size_t i = 0; i < timeline.layers.size(); ++i)
            {
                const int end = timeline.layers[i] ? fla::timeline::layerEnd(*timeline.layers[i]) : 0;
                if (end > 0)
                    selected.push_back({static_cast<int>(i), 0, end - 1});
            }
            return Value();
        });
        def.method("copyFrames", 2, [](Timeline& t, const Args& args) {
            frameClipboardCopy(t, spansFor(t, args, 0));
            return Value();
        });
        def.method("cutFrames", 2, [](Timeline& t, const Args& args) {
            const std::vector<Span> spans = spansFor(t, args, 0);
            frameClipboardCopy(t, spans);
            editFrames(t, spans, "Cut Frames", [](const Span& span, std::vector<fla::Frame*>& removed) {
                fla::timeline::clearFrames(*span.layer, span.first, span.last, removed);
            });
            return Value();
        });
        def.method("pasteFrames", 2, [](Timeline& t, const Args& args) {
            frameClipboardPaste(t, spansFor(t, args, 0));
            return Value();
        });
        def.method("setFrameProperty", 4, [](Timeline& t, const Args& args) {
            setOnFrames(t, spansFor(t, args, 2), arg(args, 0).toString(), arg(args, 1));
            return Value();
        });
        def.method("getFrameProperty", 3, [](Timeline& t, const Args& args) {
            const std::vector<Span> spans = spansFor(t, args, 1);
            if (spans.empty())
                return Value();
            fla::Frame* frame = fla::timeline::spanAt(*spans.front().layer, spans.front().first);
            if (!frame)
                return Value();
            Frame wrapper(t.session, t.document, spans.front().layer, spans.front().first);
            for (const Property& property : wrapper.classInfo().properties)
            {
                if (property.name == arg(args, 0).toString())
                    return property.get(wrapper);
            }
            return Value();
        });
        def.method("setLayerProperty", 3, [](Timeline& t, const Args& args) {
            const std::string which = given(args, 2) ? arg(args, 2).toString() : "selected";
            for (fla::Layer* layer : layersFor(t, which))
                setOnLayer(t, layer, arg(args, 0).toString(), arg(args, 1));
            return Value();
        });
        def.method("getLayerProperty", 1, [](Timeline& t, const Args& args) {
            const std::vector<fla::Layer*> layers = layersFor(t, "selected");
            if (layers.empty())
                return Value();
            Layer wrapper(t.session, t.document, layers.front());
            for (const Property& property : wrapper.classInfo().properties)
            {
                if (property.name == arg(args, 0).toString())
                    return property.get(wrapper);
            }
            return Value();
        });
        def.method("expandFolder", 3, [](Timeline&, const Args&) { return Value(); });
        def.method("startPlayback", 0, [](Timeline&, const Args&) { return Value(); });
        def.method("stopPlayback", 0, [](Timeline&, const Args&) { return Value(); });
        return info;
    }();
    return info;
}

const ClassInfo& Layer::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Layer> def(info, "Layer");
        def.prop("name", [](Layer& l) { return Value(l.get().name); },
            [](Layer& l, const Value& v) {
                editLayer(l, "Rename Layer", [&v](fla::LayerSnapshot&, fla::LayerState& state) {
                    state.name = v.toString();
                    state.autoNamed = false;
                });
            });
        def.prop("layerType",
            [](Layer& l) {
                fla::Layer& layer = l.get();
                fla::Timeline* timeline = timelineOf(layer);
                if (!layer.otherLayerType.empty())
                    return Value(layer.otherLayerType);
                return Value(timeline ? kindName(fla::layers::kindOf(fla::snapshotLayers(*timeline), &layer)) : std::string("normal"));
            },
            [](Layer& l, const Value& v) {
                const fla::layers::Kind kind = kindNamed(v.toString());
                editLayer(l, "Layer Properties", [&l, kind, &v](fla::LayerSnapshot& snapshot, fla::LayerState&) {
                    if (!fla::layers::setKind(snapshot, l.layer, kind))
                        throw Error("That layer cannot become " + v.toString() + " where it is.");
                });
            });
        def.prop("visible", [](Layer& l) { return Value(l.get().visible); },
            [](Layer& l, const Value& v) {
                editLayer(l, "Show Layer", [&v](fla::LayerSnapshot&, fla::LayerState& state) { state.visible = v.toBool(); });
            });
        def.prop("locked", [](Layer& l) { return Value(l.get().locked); },
            [](Layer& l, const Value& v) {
                editLayer(l, "Lock Layer", [&v](fla::LayerSnapshot&, fla::LayerState& state) { state.locked = v.toBool(); });
            });
        def.prop("outline", [](Layer& l) { return Value(l.get().outline); },
            [](Layer& l, const Value& v) {
                editLayer(l, "Outline Layer", [&v](fla::LayerSnapshot&, fla::LayerState& state) { state.outline = v.toBool(); });
            });
        def.prop("color", [](Layer& l) { return Value(colorText(l.get().color)); },
            [](Layer& l, const Value& v) {
                uint8_t color[4];
                if (!parseColor(v, color))
                    throw Error("That is not a color: " + v.toString());
                editLayer(l, "Layer Color", [&color](fla::LayerSnapshot&, fla::LayerState& state) {
                    std::copy(color, color + 4, state.color);
                });
            });
        def.get("frameCount", [](Layer& l) { return Value(fla::timeline::layerEnd(l.get())); });
        def.get("frames", [](Layer& l) {
            fla::Layer& layer = l.get();
            Value list = Value::array();
            const int end = fla::timeline::layerEnd(layer);
            list.items.reserve(static_cast<size_t>(end));
            for (int i = 0; i < end; ++i)
            {
                fla::Frame* frame = fla::timeline::spanAt(layer, i);
                list.items.emplace_back(frame ? Value(std::make_shared<Frame>(l.session, l.document, &layer, i)) : Value());
            }
            return list;
        });
        def.get("parentLayer", [](Layer& l) {
            fla::Layer* parent = l.get().parentLayer;
            return parent ? Value(wrapLayer(l.session, l.document, parent)) : Value::null();
        });
        return info;
    }();
    return info;
}

const ClassInfo& Frame::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Frame> def(info, "Frame");
        def.get("elements", [](Frame& f) {
            Value list = Value::array();
            for (fla::Element* element : f.get().elements)
            {
                if (element)
                    list.items.emplace_back(wrapElement(f.session, f.document, element));
            }
            return list;
        });
        def.get("startFrame", [](Frame& f) { return Value(f.get().index); });
        def.prop("duration", [](Frame& f) { return Value(f.get().duration); },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                fla::Layer* layer = f.layer;
                const int wanted = std::max(1, v.toInt());
                const int change = wanted - frame.duration;
                if (change == 0)
                    return;
                const int end = frame.index + frame.duration;
                f.session.host().selection(f.document).clear();
                f.push(fla::CommandPtr(new fla::EditFramesCommand({layer}, "Frame Duration",
                    [layer, end, change](std::vector<fla::Frame*>& removed) {
                        if (change > 0)
                            fla::timeline::insertFrames(*layer, end - 1, change);
                        else
                            fla::timeline::removeFrames(*layer, end + change, -change, removed);
                    })));
            });
        def.get("isEmpty", [](Frame& f) { return Value(f.get().elements.empty()); });
        def.prop("name", [](Frame& f) { return Value(f.get().name); },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                const std::string name = v.isNullish() ? std::string() : v.toString();
                f.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(frame.name, frame.name, name, "Label")), false);
            });
        def.prop("labelType",
            [](Frame& f) {
                const fla::Frame& frame = f.get();
                if (frame.name.empty())
                    return Value("none");
                return Value(frame.labelType.empty() ? std::string("name") : frame.labelType);
            },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                const std::string type = v.toString();
                if (type != "none" && type != "name" && type != "comment" && type != "anchor")
                    throw Error("A label is none, name, comment or anchor, not " + type + ".");
                if (type == "none")
                {
                    f.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(frame.name, frame.name, std::string(), "Label")), false);
                    return;
                }
                f.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(frame.labelType, frame.labelType,
                    type == "name" ? std::string() : type, "Label")), false);
            });
        def.prop("tweenType",
            [](Frame& f) {
                fla::Frame& frame = f.get();
                if (!frame.otherTweenType.empty())
                    return Value(frame.otherTweenType);
                if (frame.tweenType == fla::TweenType::Shape)
                    return Value("shape");
                if (frame.tweenType == fla::TweenType::Motion)
                    return Value(fla::timeline::motionOf(frame) ? "motion object" : "motion");
                return Value("none");
            },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                fla::Layer* layer = f.layer;
                const std::string type = v.toString();
                if ((type != "motion" && type != "shape" && type != "none"))
                    throw Error("A frame's tween is motion, shape or none, not " + type + ".");
                const int start = frame.index;
                f.session.host().selection(f.document).clear();
                f.push(fla::CommandPtr(new fla::EditFramesCommand({layer}, "Tween", [layer, start, type](std::vector<fla::Frame*>&) {
                    bool done = false;
                    if (type == "shape")
                        done = fla::timeline::setShapeTween(*layer, start);
                    else
                        done = fla::timeline::setTween(*layer, start, type == "motion");
                    if (!done)
                    {
                        if (fla::Frame* span = fla::timeline::spanAt(*layer, start))
                            span->tweenType = type == "shape" ? fla::TweenType::Shape
                                : type == "motion"          ? fla::TweenType::Motion
                                                            : fla::TweenType::None;
                    }
                })));
            });
        def.prop("tweenEasing", [](Frame& f) { return Value(f.get().acceleration); },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                const int ease = std::clamp(v.toInt(), -100, 100);
                f.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(frame.acceleration, frame.acceleration, ease, "Ease")), false);
            });
        def.get("hasCustomEase", [](Frame& f) { return Value(f.get().hasCustomEase); });
        def.prop("motionTweenRotate",
            [](Frame& f) {
                const std::string& rotate = f.get().motionTweenRotate;
                return Value(rotate.empty() ? std::string("auto") : rotate);
            },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                const std::string rotate = v.toString();
                if (rotate != "auto" && rotate != "none" && rotate != "clockwise" && rotate != "counter-clockwise")
                    throw Error("Rotation is auto, none, clockwise or counter-clockwise, not " + rotate + ".");
                f.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(frame.motionTweenRotate, frame.motionTweenRotate,
                    rotate == "auto" ? std::string() : rotate, "Rotate")), false);
            });
        def.prop("motionTweenRotateTimes", [](Frame& f) { return Value(f.get().motionTweenRotateTimes); },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                f.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(frame.motionTweenRotateTimes, frame.motionTweenRotateTimes,
                    std::max(0, v.toInt()), "Rotate")), false);
            });
        def.prop("motionTweenSnap", [](Frame& f) { return Value(f.get().motionTWeenSnap); },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                f.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(frame.motionTWeenSnap, frame.motionTWeenSnap, v.toBool(), "Snap")), false);
            });
        def.prop("motionTweenSync", [](Frame& f) { return Value(f.get().motionTweenSync); },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                f.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(frame.motionTweenSync, frame.motionTweenSync, v.toBool(), "Sync")), false);
            });
        // Kept among the frame's attributes as the file has them.
        for (const char* key : {"motionTweenScale", "motionTweenOrientToPath", "soundName", "soundSync", "soundLoopMode",
                 "soundLoop", "soundEffect"})
        {
            const std::string name = key;
            const bool flag = name == "motionTweenScale" || name == "motionTweenOrientToPath";
            def.prop(key,
                [name, flag](Frame& f) {
                    const std::string value = attribute(f.get(), name);
                    if (flag)
                        return Value(value.empty() ? name == "motionTweenScale" : value == "true");
                    if (name == "soundLoop")
                        return Value(value.empty() ? 0 : std::atoi(value.c_str()));
                    if (value.empty() && name == "soundSync")
                        return Value("event");
                    return Value(value);
                },
                [name, flag](Frame& f, const Value& v) {
                    fla::Frame& frame = f.get();
                    std::string value = flag ? (v.toBool() ? "true" : "false") : v.isNullish() ? std::string() : v.toString();
                    setAttribute(f, frame, name, value);
                });
        }
        def.prop("actionScript", [](Frame& f) { return Value(f.get().actionScript ? f.get().actionScript->code : std::string()); },
            [](Frame& f, const Value& v) {
                fla::Frame& frame = f.get();
                f.push(fla::CommandPtr(new SetActionScriptCommand(&frame, v.isNullish() ? std::string() : v.toString())), false);
            });
        def.get("soundLibraryItem", [](Frame& f) {
            const std::string name = attribute(f.get(), "soundName");
            auto item = name.empty() ? nullptr : findItem(f.session, f.document, name);
            return item ? Value(item) : Value::null();
        });
        def.get("keyMode", [](Frame& f) { return Value(f.get().keyMode); });
        def.method("isMotionObject", 0, [](Frame& f, const Args&) {
            return Value(fla::timeline::motionOf(f.get()) != nullptr);
        });
        return info;
    }();
    return info;
}

} // namespace jsfl
