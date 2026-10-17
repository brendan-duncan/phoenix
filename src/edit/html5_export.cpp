#include "html5_export.h"

#include "deformers.h"
#include "frame_bake.h"
#include "frame_sound.h"
#include "graphic_loop.h"
#include "layer_commands.h"
#include "motion_object.h"
#include "three_d.h"
#include "timeline_edit.h"

#include "../data/bitmap.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../play/display/bitmap_filter.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>

namespace fla::html5 {

std::string number(double value, int decimals)
{
    if (!std::isfinite(value))
        return "0";
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f", std::clamp(decimals, 0, 10), value);
    std::string out = text;
    if (out.find('.') != std::string::npos)
    {
        while (!out.empty() && out.back() == '0')
            out.pop_back();
        if (!out.empty() && out.back() == '.')
            out.pop_back();
    }
    if (out == "-0" || out.empty())
        out = "0";
    return out;
}

std::string quoted(const std::string& value)
{
    std::string out = "\"";
    for (size_t i = 0; i < value.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '<':
            // Not "</script>" inside a page.
            out += i + 1 < value.size() && value[i + 1] == '/' ? "\\u003c" : "<";
            break;
        default:
            if (c < 0x20)
            {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x", c);
                out += escape;
            }
            // U+2028 and U+2029 end lines in older JavaScript.
            else if (c == 0xe2 && i + 2 < value.size() && static_cast<unsigned char>(value[i + 1]) == 0x80 &&
                     (static_cast<unsigned char>(value[i + 2]) == 0xa8 || static_cast<unsigned char>(value[i + 2]) == 0xa9))
            {
                out += static_cast<unsigned char>(value[i + 2]) == 0xa8 ? "\\u2028" : "\\u2029";
                i += 2;
            }
            else
                out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string documentClass(const Document& document)
{
    for (const auto& [path, bytes] : document.extraFiles)
    {
        if (path != "PublishSettings.xml")
            continue;
        const std::vector<uint8_t>& data = bytes.get();
        tinyxml2::XMLDocument xml;
        if (xml.Parse(reinterpret_cast<const char*>(data.data()), data.size()) != tinyxml2::XML_SUCCESS)
            return std::string();
        std::vector<const tinyxml2::XMLElement*> stack{xml.RootElement()};
        while (!stack.empty())
        {
            const tinyxml2::XMLElement* at = stack.back();
            stack.pop_back();
            for (const tinyxml2::XMLElement* child = at ? at->FirstChildElement() : nullptr; child;
                 child = child->NextSiblingElement())
            {
                if (std::strcmp(child->Name(), "DocumentClass") == 0 && child->GetText())
                    return child->GetText();
                stack.push_back(child);
            }
        }
    }
    return std::string();
}

namespace {

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const char* name)
{
    for (const auto& [key, value] : attributes)
        if (key == name)
            return value;
    return std::string();
}

std::string matrixJson(const Transform& t)
{
    return "[" + number(t.m11, 5) + "," + number(t.m12, 5) + "," + number(t.m21, 5) + "," + number(t.m22, 5) + "," +
        number(t.tx, 3) + "," + number(t.ty, 3) + "]";
}

bool isIdentity(const Transform& t)
{
    return t.m11 == 1.0 && t.m12 == 0.0 && t.m21 == 0.0 && t.m22 == 1.0 && t.tx == 0.0 && t.ty == 0.0;
}

/// \a first then \a second, as fla::Transform composes them.
Transform followedBy(const Transform& first, const Transform& second)
{
    Transform out = second;
    out.concatenate(first);
    return out;
}

uint32_t rgbOf(const uint8_t color[4])
{
    return (uint32_t(color[0]) << 16) | (uint32_t(color[1]) << 8) | color[2];
}

/// An instance's color effect as the multipliers and offsets of a
/// ColorTransform: tint, then brightness, then the advanced settings, as
/// the stage applies them.
std::string colorJson(const ColorTransform& c)
{
    if (c.isIdentity())
        return std::string();
    double m[3] = {1.0, 1.0, 1.0};
    double o[3] = {0.0, 0.0, 0.0};
    if (c.tintMultiplier != 0.0)
        for (int i = 0; i < 3; ++i)
        {
            m[i] = 1.0 - c.tintMultiplier;
            o[i] = c.tintMultiplier * c.tintColor[i];
        }
    if (c.brightness != 0.0)
    {
        const double k = c.brightness > 0.0 ? 1.0 - c.brightness : 1.0 + c.brightness;
        const double add = c.brightness > 0.0 ? 255.0 * c.brightness : 0.0;
        for (int i = 0; i < 3; ++i)
        {
            m[i] *= k;
            o[i] = o[i] * k + add;
        }
    }
    const double mult[3] = {c.redMultiplier, c.greenMultiplier, c.blueMultiplier};
    const int offset[3] = {c.redOffset, c.greenOffset, c.blueOffset};
    for (int i = 0; i < 3; ++i)
    {
        m[i] *= mult[i];
        o[i] = o[i] * mult[i] + offset[i];
    }
    return "[" + number(m[0], 4) + "," + number(m[1], 4) + "," + number(m[2], 4) + "," + number(c.alphaMultiplier, 4) +
        "," + number(o[0], 2) + "," + number(o[1], 2) + "," + number(o[2], 2) + "," + number(c.alphaOffset, 2) + "]";
}

std::string filterJson(const Filter& f)
{
    const auto common = [&f]() {
        return ",\"bx\":" + number(f.blurX, 2) + ",\"by\":" + number(f.blurY, 2) + ",\"q\":" + std::to_string(f.quality) +
            ",\"s\":" + number(f.strength, 3);
    };
    const auto gradient = [&f]() {
        std::string colors, alphas, ratios;
        for (const GradientEntry& entry : f.entries)
        {
            if (!colors.empty())
            {
                colors += ",";
                alphas += ",";
                ratios += ",";
            }
            colors += std::to_string(rgbOf(entry.color));
            alphas += number(entry.color[3] / 255.0, 3);
            ratios += number(entry.ratio <= 1.0 ? entry.ratio * 255.0 : entry.ratio, 1);
        }
        return ",\"cs\":[" + colors + "],\"as\":[" + alphas + "],\"rs\":[" + ratios + "]";
    };
    switch (f.type)
    {
    case Filter::Type::Blur:
        return "{\"t\":\"blur\",\"bx\":" + number(f.blurX, 2) + ",\"by\":" + number(f.blurY, 2) + ",\"q\":" +
            std::to_string(f.quality) + "}";
    case Filter::Type::DropShadow:
        return "{\"t\":\"shadow\",\"d\":" + number(f.distance, 2) + ",\"an\":" + number(f.angle, 2) + ",\"c\":" +
            std::to_string(rgbOf(f.color)) + ",\"a\":" + number(f.color[3] / 255.0, 3) + common() +
            (f.inner ? ",\"in\":1" : "") + (f.knockout ? ",\"ko\":1" : "") + (f.hideObject ? ",\"ho\":1" : "") + "}";
    case Filter::Type::Glow:
        return "{\"t\":\"glow\",\"c\":" + std::to_string(rgbOf(f.color)) + ",\"a\":" + number(f.color[3] / 255.0, 3) +
            common() + (f.inner ? ",\"in\":1" : "") + (f.knockout ? ",\"ko\":1" : "") + "}";
    case Filter::Type::Bevel:
        return "{\"t\":\"bevel\",\"d\":" + number(f.distance, 2) + ",\"an\":" + number(f.angle, 2) + ",\"hc\":" +
            std::to_string(rgbOf(f.highlightColor)) + ",\"ha\":" + number(f.highlightColor[3] / 255.0, 3) + ",\"sc\":" +
            std::to_string(rgbOf(f.shadowColor)) + ",\"sa\":" + number(f.shadowColor[3] / 255.0, 3) + common() +
            ",\"bt\":" + quoted(f.bevelType.empty() ? std::string("inner") : f.bevelType) + (f.knockout ? ",\"ko\":1" : "") + "}";
    case Filter::Type::GradientGlow:
        return "{\"t\":\"gglow\",\"d\":" + number(f.distance, 2) + ",\"an\":" + number(f.angle, 2) + gradient() + common() +
            (f.inner ? ",\"in\":1" : "") + (f.knockout ? ",\"ko\":1" : "") + "}";
    case Filter::Type::GradientBevel:
        return "{\"t\":\"gbevel\",\"d\":" + number(f.distance, 2) + ",\"an\":" + number(f.angle, 2) + gradient() + common() +
            ",\"bt\":" + quoted(f.bevelType.empty() ? std::string("inner") : f.bevelType) + (f.knockout ? ",\"ko\":1" : "") + "}";
    case Filter::Type::AdjustColor:
    {
        const display::BitmapFilter matrix = display::BitmapFilter::adjustColor(f.brightness, f.contrast, f.saturation, f.hue);
        std::string values;
        for (const double v : matrix.matrix)
            values += (values.empty() ? "" : ",") + number(v, 5);
        return "{\"t\":\"matrix\",\"m\":[" + values + "]}";
    }
    }
    return std::string();
}

std::string filtersJson(const std::vector<Filter>& filters)
{
    std::string out;
    for (const Filter& filter : filters)
    {
        if (!filter.enabled)
            continue;
        const std::string json = filterJson(filter);
        if (json.empty())
            continue;
        out += (out.empty() ? "" : ",") + json;
    }
    return out.empty() ? out : "[" + out + "]";
}

/// The keyframe of \a layer whose span holds \a frame, or null.
const Frame* spanAt(const Layer& layer, int frame)
{
    for (const Frame* each : layer.frames)
        if (each && each->index <= frame && frame < each->index + std::max(1, each->duration))
            return each;
    return nullptr;
}
const Frame* keyframeAt(const Layer& layer, int index)
{
    for (const Frame* each : layer.frames)
        if (each && each->index == index)
            return each;
    return nullptr;
}

/// How long a timeline runs: to the end of its last frame.
int lengthOf(const Timeline& timeline)
{
    int length = 1;
    for (const Layer* layer : timeline.layers)
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            if (frame)
                length = std::max(length, frame->index + std::max(1, frame->duration));
    return length;
}
int symbolLength(const Symbol& symbol)
{
    int length = 1;
    for (const Timeline* timeline : symbol.timelines)
        if (timeline)
            length = std::max(length, lengthOf(*timeline));
    return length;
}

/// What a span shows \a framesIn frames in: its elements moved along its
/// tween (copies, owned here), or its own elements when they stand still.
struct Shown
{
    std::vector<const Element*> elements;
    std::vector<std::unique_ptr<Element>> owned;
    /// Each shown element's own (the span's) element, for art that only
    /// moves; null for one the tween reshapes (a shape tween's morph).
    std::vector<const Element*> originals;
    /// Copies carry a graphic's frame already played on.
    bool copies = false;
};
Shown shownAt(const Frame& span, const Layer& layer, int framesIn, double frameRate)
{
    Shown shown;
    const Frame* next = keyframeAt(layer, span.index + std::max(1, span.duration));
    const bool moving = span.otherTweenType == "motion object" ||
        (span.tweenType != TweenType::None && next && framesIn > 0);
    if (!moving)
    {
        for (const Element* element : span.elements)
            if (element)
            {
                shown.elements.push_back(element);
                shown.originals.push_back(element);
            }
        return shown;
    }
    shown.copies = true;
    std::vector<const Element*> sources;
    for (const Element* element : span.elements)
        if (element)
            sources.push_back(element);
    const std::vector<Element*> made = frames::elementsAt(span, next, framesIn, frameRate, const_cast<Frame*>(&span));
    // A motion tween's color and filter curves, as the stage draws them.
    if (span.otherTweenType == "motion object")
    {
        MotionObject motion;
        std::string* xml = timeline::motionOf(const_cast<Frame&>(span));
        if (xml && motion.parse(*xml) && (motion.animatesColor() || motion.animatesFilters()))
            for (Element* element : made)
                if (element && element->elementType() == Element::Type::SymbolInstance)
                {
                    auto* instance = static_cast<SymbolInstance*>(element);
                    instance->colorTransform = motion.colorAt(framesIn, frameRate, instance->colorTransform);
                    instance->filters = motion.filtersAt(framesIn, frameRate, instance->filters);
                }
    }
    const bool reshaping = span.tweenType == TweenType::Shape;
    for (size_t i = 0; i < made.size(); ++i)
    {
        shown.owned.emplace_back(made[i]);
        shown.elements.push_back(made[i]);
        const Element* original = !reshaping && made.size() == sources.size() ? sources[i] : nullptr;
        if (reshaping && made.size() == sources.size() && made[i]->elementType() != Element::Type::Shape)
            original = sources[i];
        shown.originals.push_back(original);
    }
    return shown;
}

class Walker
{
public:
    Walker(const Document& document, ArtSource& source, const Options& options, std::vector<std::string>* notes)
        : _document(document)
        , _source(source)
        , _options(options)
        , _notes(notes)
        , _rate(document.frameRate > 0.0 ? document.frameRate : 24.0)
    {}

    std::string run()
    {
        std::vector<const Timeline*> scenes;
        for (const Timeline* timeline : _document.timelines)
            if (timeline)
                scenes.push_back(timeline);
        // Symbols exported for ActionScript come whether or not a frame
        // shows them, as scripts make them by name.
        if (_document.symbolList)
            for (const Symbol* symbol : _document.symbolList->symbols)
                if (symbol && (_options.allSymbols || !attributeOf(symbol->extraAttributes, "linkageClassName").empty()))
                    indexOf(symbol);
        const std::string root = timelineJson(scenes, true);
        std::vector<std::string> symbols;
        while (symbols.size() < _symbols.size())
        {
            const Symbol* symbol = _symbols[symbols.size()];
            symbols.push_back(symbolJson(*symbol));
        }
        // Library bitmaps scripts make by their linkage.
        for (const Resource* resource : _document.resources)
        {
            const auto* bitmap = dynamic_cast<const Bitmap*>(resource);
            if (bitmap && !attributeOf(bitmap->extraAttributes, "linkageClassName").empty())
                _source.bitmap(*bitmap);
        }
        std::string out = "{\"version\":1";
        out += ",\"width\":" + std::to_string(std::max(1, _document.width));
        out += ",\"height\":" + std::to_string(std::max(1, _document.height));
        out += ",\"frameRate\":" + number(_rate, 3);
        out += ",\"background\":" + std::to_string(rgbOf(_document.backgroundColor));
        const std::string documentClassName = documentClass(_document);
        if (!documentClassName.empty())
            out += ",\"documentClass\":" + quoted(documentClassName);
        if (_options.javaScript)
            out += ",\"javaScript\":true";
        out += ",\"images\":" + _source.images();
        out += ",\"sounds\":" + _source.sounds();
        out += ",\"art\":[";
        for (size_t i = 0; i < _art.size(); ++i)
            out += (i ? ",\n" : "\n") + _art[i];
        out += "],\"texts\":[";
        for (size_t i = 0; i < _texts.size(); ++i)
            out += (i ? "," : "") + _texts[i];
        out += "],\"symbols\":[";
        for (size_t i = 0; i < symbols.size(); ++i)
            out += (i ? ",\n" : "\n") + symbols[i];
        out += "],\n\"root\":" + root + "}";
        return out;
    }

private:
    void note(const std::string& message)
    {
        if (_notes && std::find(_notes->begin(), _notes->end(), message) == _notes->end())
            _notes->push_back(message);
    }

    int indexOf(const Symbol* symbol)
    {
        const auto found = _symbolIndex.find(symbol);
        if (found != _symbolIndex.end())
            return found->second;
        const int index = static_cast<int>(_symbols.size());
        _symbolIndex[symbol] = index;
        _symbols.push_back(symbol);
        return index;
    }

    std::string symbolJson(const Symbol& symbol)
    {
        std::string out = "{\"name\":" + quoted(symbol.name);
        out += std::string(",\"type\":") + (symbol.symbolType == SymbolType::Button ? "\"button\""
                : symbol.symbolType == SymbolType::Graphic                          ? "\"graphic\""
                                                                                    : "\"mc\"");
        const std::string linkage = attributeOf(symbol.extraAttributes, "linkageClassName");
        if (!linkage.empty())
            out += ",\"linkage\":" + quoted(linkage);
        const std::string base = attributeOf(symbol.extraAttributes, "linkageBaseClass");
        if (!base.empty())
            out += ",\"base\":" + quoted(base);
        if (symbol.scale9)
            out += ",\"scale9\":[" + number(symbol.scaleGridLeft) + "," + number(symbol.scaleGridTop) + "," +
                number(symbol.scaleGridRight) + "," + number(symbol.scaleGridBottom) + "]";
        std::vector<const Timeline*> timelines;
        if (!symbol.timelines.empty() && symbol.timelines.front())
            timelines.push_back(symbol.timelines.front());
        out += ",\"timeline\":" + timelineJson(timelines, false) + "}";
        return out;
    }

    /// A timeline (the scenes, one after another, for the main one).
    std::string timelineJson(const std::vector<const Timeline*>& timelines, bool scene)
    {
        std::vector<std::string> states;
        std::map<std::string, int> stateIndex;
        std::vector<int> map;
        std::vector<std::pair<int, std::string>> labels;
        std::vector<std::string> sounds;
        std::vector<std::pair<int, std::string>> scripts;
        std::string scenes;
        int offset = 0;
        for (size_t s = 0; s < timelines.size(); ++s)
        {
            const Timeline& timeline = *timelines[s];
            const int length = lengthOf(timeline);
            if (scene)
                scenes += std::string(scenes.empty() ? "" : ",") + "{\"name\":" + quoted(timeline.name) + ",\"at\":" +
                    std::to_string(offset) + ",\"n\":" + std::to_string(length) + "}";
            gatherFrameData(timeline, offset, scene, labels, sounds, scripts);
            for (int frame = 0; frame < length; ++frame)
            {
                const std::string state = stateJson(timeline, frame, scene, "S" + std::to_string(s));
                const auto found = stateIndex.find(state);
                if (found != stateIndex.end())
                    map.push_back(found->second);
                else
                {
                    const int index = static_cast<int>(states.size());
                    stateIndex[state] = index;
                    states.push_back(state);
                    map.push_back(index);
                }
            }
            offset += length;
        }
        if (timelines.empty())
        {
            states.push_back("[]");
            map.push_back(0);
            offset = 1;
        }
        std::string out = "{\"n\":" + std::to_string(std::max(1, offset));
        if (scene && !_options.loop)
            out += ",\"once\":true";
        if (scene && !scenes.empty())
            out += ",\"scenes\":[" + scenes + "]";
        if (!labels.empty())
        {
            std::sort(labels.begin(), labels.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            out += ",\"labels\":[";
            for (size_t i = 0; i < labels.size(); ++i)
                out += std::string(i ? "," : "") + "[" + std::to_string(labels[i].first) + "," + quoted(labels[i].second) + "]";
            out += "]";
        }
        if (!sounds.empty())
        {
            out += ",\"sounds\":[";
            for (size_t i = 0; i < sounds.size(); ++i)
                out += (i ? "," : "") + sounds[i];
            out += "]";
        }
        if (!scripts.empty())
        {
            out += ",\"js\":[";
            for (size_t i = 0; i < scripts.size(); ++i)
                out += std::string(i ? "," : "") + "[" + std::to_string(scripts[i].first) + "," + quoted(scripts[i].second) + "]";
            out += "]";
        }
        out += ",\"states\":[";
        for (size_t i = 0; i < states.size(); ++i)
            out += (i ? ",\n" : "\n") + states[i];
        out += "],\"map\":[";
        for (size_t i = 0; i < map.size(); ++i)
            out += (i ? "," : "") + std::to_string(map[i]);
        out += "]}";
        return out;
    }

    /// The labels, sounds and scripts on a timeline's keyframes.
    void gatherFrameData(const Timeline& timeline, int offset, bool scene, std::vector<std::pair<int, std::string>>& labels,
        std::vector<std::string>& sounds, std::vector<std::pair<int, std::string>>& scripts)
    {
        std::set<int> labeled;
        std::map<int, std::string> code;
        bool actionScript = false;
        for (const Layer* layer : timeline.layers)
        {
            if (!layer || layer->layerType == Layer::Type::Folder)
                continue;
            for (const Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                const int at = offset + frame->index;
                if (!frame->name.empty() && frame->labelType != "comment" && labeled.insert(at).second)
                    labels.emplace_back(at, frame->name);
                const frame_sound::Settings sound = frame_sound::read(*frame);
                if (!sound.name.empty() && layer->layerType != Layer::Type::Guide)
                {
                    const int index = _source.sound(sound.name);
                    if (index >= 0)
                        sounds.push_back("{\"f\":" + std::to_string(at) + ",\"s\":" + std::to_string(index) + ",\"y\":" +
                            quoted(sound.sync) + ",\"l\":" + std::to_string(sound.loop ? 9999 : std::max(1, sound.loops)) +
                            (sound.sync == "stream" ? ",\"e\":" + std::to_string(at + std::max(1, frame->duration)) : std::string()) +
                            "}");
                    else
                        note("The sound " + sound.name + " could not be written.");
                }
                if (frame->actionScript && !frame->actionScript->code.empty())
                {
                    if (_options.javaScript)
                    {
                        std::string& joined = code[at];
                        joined += (joined.empty() ? "" : "\n") + frame->actionScript->code;
                    }
                    else
                        actionScript = true;
                }
            }
        }
        for (const auto& [frame, source] : code)
            scripts.emplace_back(frame, source);
        if (actionScript)
            note("ActionScript is not published yet: the movie plays its timelines without their scripts.");
        (void)scene;
    }

    /// What frame \a frame of \a timeline shows, bottom to top, as JSON.
    std::string stateJson(const Timeline& timeline, int frame, bool scene, const std::string& prefix)
    {
        const std::vector<Layer*>& layers = timeline.layers;
        const auto layerIndex = [&layers](const Layer* layer) {
            const auto found = std::find(layers.begin(), layers.end(), layer);
            return found == layers.end() ? -1 : static_cast<int>(found - layers.begin());
        };
        const auto shown = [scene](const Layer& layer) { return !scene || layer.isVisible(); };
        std::string out;
        for (int li = static_cast<int>(layers.size()) - 1; li >= 0; --li)
        {
            const Layer* layer = layers[static_cast<size_t>(li)];
            if (!layer || layer->layerType == Layer::Type::Folder || layer->layerType == Layer::Type::Guide || !shown(*layer))
                continue;
            int maskGroup = -1, maskedBy = -1;
            if (layer->layerType == Layer::Type::Mask)
                maskGroup = li;
            else if (layer->layerType == Layer::Type::Masked && layer->parentLayer &&
                     layer->parentLayer->layerType == Layer::Type::Mask && shown(*layer->parentLayer))
                maskedBy = layerIndex(layer->parentLayer);
            const Frame* span = spanAt(*layer, frame);
            if (!span || !span->visible)
                continue;
            const int framesIn = frame - span->index;
            Shown here = shownAt(*span, *layer, framesIn, _rate);
            const Transform parent = parentTransform(timeline, *layer, *span, frame, here, 0);
            for (size_t ei = 0; ei < here.elements.size(); ++ei)
            {
                const Element* element = here.elements[ei];
                const std::string entry = entryJson(*element, here.originals[ei], here.copies ? 0 : framesIn, parent,
                    prefix + "L" + std::to_string(li) + "E" + std::to_string(ei),
                    (static_cast<int>(layers.size()) - li) * 65536 + static_cast<int>(ei) + 1, maskGroup, maskedBy);
                if (!entry.empty())
                    out += (out.empty() ? "" : ",") + entry;
            }
        }
        return "[" + out + "]";
    }

    /// Layer parenting: what places the layer's content, its parent layer's
    /// first element as shown, through its own parent in turn.
    Transform parentTransform(const Timeline& timeline, const Layer& layer, const Frame& span, int frame, const Shown& here,
        int depth)
    {
        const std::string rigText = attributeOf(span.extraAttributes, "parentLayerIndex");
        if (rigText.empty() || depth > 16)
            return Transform();
        const int rig = std::atoi(rigText.c_str());
        for (const Layer* candidate : timeline.layers)
        {
            if (!candidate || candidate == &layer || attributeOf(candidate->extraAttributes, "layerRiggingIndex").empty() ||
                std::atoi(attributeOf(candidate->extraAttributes, "layerRiggingIndex").c_str()) != rig)
                continue;
            const Frame* parentSpan = spanAt(*candidate, frame);
            if (!parentSpan)
                return Transform();
            Shown parentShown = shownAt(*parentSpan, *candidate, frame - parentSpan->index, _rate);
            if (parentShown.elements.empty())
                return Transform();
            const Transform world = followedBy(parentShown.elements.front()->transform,
                parentTransform(timeline, *candidate, *parentSpan, frame, parentShown, depth + 1));
            Transform turn;
            if (layers::rigPropagationOf(*parentSpan, turn))
            {
                const Element* first = here.elements.empty() ? nullptr : here.elements.front();
                return layers::riggedTransform(world, turn, first ? first->transform : Transform(),
                    first ? first->transformationPoint : Point());
            }
            const layers::ParentPropagation propagation = layers::propagationOf(layer);
            return propagation.all() ? world : layers::propagatedTransform(world, propagation);
        }
        return Transform();
    }

    /// One display list entry; empty for what does not show.
    std::string entryJson(const Element& element, const Element* original, int elapsed, const Transform& parent,
        const std::string& key, int depth, int maskGroup, int maskedBy)
    {
        std::string out = "{\"k\":" + quoted(key) + ",\"d\":" + std::to_string(depth);
        const auto placed = [&](const Transform& own) {
            const Transform m = followedBy(own, parent);
            return isIdentity(m) ? std::string() : ",\"m\":" + matrixJson(m);
        };
        const auto masks = [&]() {
            return (maskGroup >= 0 ? ",\"mk\":" + std::to_string(maskGroup) : std::string()) +
                (maskedBy >= 0 ? ",\"mb\":" + std::to_string(maskedBy) : std::string());
        };
        switch (element.elementType())
        {
        case Element::Type::SymbolInstance:
        {
            const auto& instance = static_cast<const SymbolInstance&>(element);
            if (!instance.symbol)
                return std::string();
            // Bent or turned out of the plane (an Asset Warp puppet, a
            // deformer, a 3D turn): no display object does that, so it goes
            // as art, drawn as the stage draws it.
            const std::optional<deform::Deformer> deformer = deform::of(instance);
            if (instance.otherSymbolType == "puppet" || (deformer && !deform::atRest(*deformer)) ||
                !threed::of(instance).flat())
                break;
            const Symbol& symbol = *instance.symbol;
            out += ",\"t\":\"s\",\"id\":" + std::to_string(indexOf(&symbol));
            const SymbolType type = instance.otherSymbolType.empty() ? instance.symbolType : SymbolType::Graphic;
            if (type != symbol.symbolType)
                out += std::string(",\"it\":") + (type == SymbolType::Graphic ? "\"graphic\"" : type == SymbolType::Button ? "\"button\"" : "\"mc\"");
            out += placed(instance.transform);
            if (type == SymbolType::Graphic)
            {
                const int shownFrame = graphicFrame(instance.loopType, instance.firstFrame, instance.lastFrame,
                    symbolLength(symbol), elapsed);
                if (shownFrame)
                    out += ",\"gf\":" + std::to_string(shownFrame);
            }
            else if (!instance.name.empty())
                out += ",\"n\":" + quoted(instance.name);
            const std::string color = colorJson(instance.colorTransform);
            if (!color.empty())
                out += ",\"ct\":" + color;
            const std::string filters = filtersJson(instance.filters);
            if (!filters.empty())
                out += ",\"f\":" + filters;
            if (!instance.blendMode.empty() && instance.blendMode != "normal")
                out += ",\"bm\":" + quoted(instance.blendMode);
            if (!instance.visible || attributeOf(instance.extraAttributes, "isVisible") == "false")
                out += ",\"v\":0";
            if (attributeOf(instance.extraAttributes, "cacheAsBitmap") == "true")
                out += ",\"cab\":1";
            return out + masks() + "}";
        }
        case Element::Type::StaticText:
        {
            const auto& text = static_cast<const StaticText&>(element);
            if (text.textType != StaticText::TextType::Static)
            {
                out += ",\"t\":\"x\",\"id\":" + std::to_string(textIndex(text));
                out += placed(text.transform);
                // Its instance name, which the file may keep as name.
                const std::string name =
                    text.instanceName.empty() ? attributeOf(text.extraAttributes, "name") : text.instanceName;
                if (!name.empty())
                    out += ",\"n\":" + quoted(name);
                return out + masks() + "}";
            }
            break;
        }
        case Element::Type::Raw:
            note("Components and videos placed on the stage are not published.");
            return std::string();
        default:
            break;
        }
        if (!element.visible)
            return std::string();
        // Art: drawn once in its own space and placed by its transform. A
        // group's members carry their own.
        const bool group = element.elementType() == Element::Type::Group;
        const int art = artIndex(original ? *original : element);
        if (art < 0)
            return std::string();
        out += ",\"t\":\"a\",\"id\":" + std::to_string(art);
        out += placed(group ? Transform() : element.transform);
        return out + masks() + "}";
    }

    int artIndex(const Element& element)
    {
        const auto known = _artByElement.find(&element);
        if (known != _artByElement.end())
            return known->second;
        const std::string json = _source.art(element);
        int index = -1;
        if (!json.empty())
        {
            const auto found = _artIndex.find(json);
            if (found != _artIndex.end())
                index = found->second;
            else
            {
                index = static_cast<int>(_art.size());
                _artIndex[json] = index;
                _art.push_back(json);
            }
        }
        // Copies a tween makes come and go; only the document's own
        // elements are remembered by address.
        if (element.parent && dynamic_cast<const Frame*>(element.parent) &&
            std::find(static_cast<const Frame*>(element.parent)->elements.begin(),
                static_cast<const Frame*>(element.parent)->elements.end(), &element) !=
                static_cast<const Frame*>(element.parent)->elements.end())
            _artByElement[&element] = index;
        return index;
    }

    /// A dynamic or input text field's description.
    int textIndex(const StaticText& text)
    {
        std::string out = "{\"x\":" + number(text.left, 2) + ",\"y\":" + number(text.top, 2) + ",\"w\":" +
            number(std::max(text.width, 1.0), 2) + ",\"h\":" + number(std::max(text.height, 1.0), 2);
        if (text.textType == StaticText::TextType::Input)
            out += ",\"type\":\"input\"";
        if (text.lineType == "multiline" || text.lineType == "multiline no wrap")
            out += ",\"ml\":1";
        if (text.lineType == "multiline")
            out += ",\"wrap\":1";
        if (text.lineType == "password")
            out += ",\"pw\":1";
        if (attributeOf(text.extraAttributes, "border") == "true")
            out += ",\"border\":1";
        if (attributeOf(text.extraAttributes, "isSelectable") == "false")
            out += ",\"sel\":0";
        const std::string maxChars = attributeOf(text.extraAttributes, "maxCharacters");
        if (!maxChars.empty() && std::atoi(maxChars.c_str()) > 0)
            out += ",\"max\":" + std::to_string(std::atoi(maxChars.c_str()));
        if (text.autoExpand)
            out += ",\"auto\":\"left\"";
        out += ",\"runs\":[";
        for (size_t i = 0; i < text.runs.size(); ++i)
        {
            const TextRun& run = text.runs[i];
            out += std::string(i ? "," : "") + "{\"t\":" + quoted(run.text) + ",\"f\":" +
                quoted(run.face.empty() ? std::string("Times New Roman") : run.face) + ",\"s\":" +
                number(run.size > 0.0 ? run.size : 12.0, 2) + ",\"c\":" + std::to_string(rgbOf(run.fillColor));
            if (attributeOf(run.extraAttributes, "bold") == "true")
                out += ",\"b\":1";
            if (attributeOf(run.extraAttributes, "italic") == "true")
                out += ",\"i\":1";
            switch (run.alignment)
            {
            case TextRun::Alignment::Center: out += ",\"a\":\"center\""; break;
            case TextRun::Alignment::Right: out += ",\"a\":\"right\""; break;
            case TextRun::Alignment::Justify: out += ",\"a\":\"justify\""; break;
            default: break;
            }
            if (run.letterSpacing != 0.0)
                out += ",\"ls\":" + number(run.letterSpacing, 2);
            const std::string spacing = attributeOf(run.extraAttributes, "lineSpacing");
            if (!spacing.empty() && std::atof(spacing.c_str()) != 0.0)
                out += ",\"ld\":" + number(std::atof(spacing.c_str()), 2);
            const std::string url = attributeOf(run.extraAttributes, "url");
            if (!url.empty())
                out += ",\"u\":" + quoted(url);
            out += "}";
        }
        out += "]}";
        const auto found = _textIndex.find(out);
        if (found != _textIndex.end())
            return found->second;
        const int index = static_cast<int>(_texts.size());
        _textIndex[out] = index;
        _texts.push_back(out);
        return index;
    }

    const Document& _document;
    ArtSource& _source;
    Options _options;
    std::vector<std::string>* _notes;
    double _rate;
    std::vector<const Symbol*> _symbols;
    std::map<const Symbol*, int> _symbolIndex;
    std::vector<std::string> _art;
    std::map<std::string, int> _artIndex;
    std::map<const Element*, int> _artByElement;
    std::vector<std::string> _texts;
    std::map<std::string, int> _textIndex;
};

} // namespace

std::string content(const Document& document, ArtSource& source, const Options& options, std::vector<std::string>* notes)
{
    Walker walker(document, source, options, notes);
    return walker.run();
}

} // namespace fla::html5
