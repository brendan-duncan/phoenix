#include "html5_export.h"

#include "adjustments.h"
#include "camera.h"
#include "command.h"
#include "deformers.h"
#include "depth_sort.h"
#include "feather.h"
#include "frame_bake.h"
#include "frame_sound.h"
#include "graphic_loop.h"
#include "layer_commands.h"
#include "layer_effects.h"
#include "library_transfer.h"
#include "motion_blur.h"
#include "motion_object.h"
#include "node_graph.h"
#include "other_apps.h"
#include "shape_operators.h"
#include "swf_actionscript3.h"
#include "script_edit.h"
#include "three_d.h"
#include "timeline_edit.h"
#include "vector_import.h"

#include "../data/bitmap.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/raw_element.h"
#include "../data/shape.h"
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
#include <numeric>
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
void toneOf(const ColorTransform& c, double m[4], double o[4])
{
    m[0] = m[1] = m[2] = 1.0;
    o[0] = o[1] = o[2] = 0.0;
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
    m[3] = c.alphaMultiplier;
    o[3] = c.alphaOffset;
}

/// \a c as multipliers and offsets, and then \a over on top of it (a
/// camera's color over what it sees); empty when both are none.
std::string colorJson(const ColorTransform& c, const ColorTransform& over = ColorTransform())
{
    if (c.isIdentity() && over.isIdentity())
        return std::string();
    double m[4], o[4], om[4], oo[4];
    toneOf(c, m, o);
    toneOf(over, om, oo);
    for (int i = 0; i < 4; ++i)
    {
        m[i] *= om[i];
        o[i] = o[i] * om[i] + oo[i];
    }
    return "[" + number(m[0], 4) + "," + number(m[1], 4) + "," + number(m[2], 4) + "," + number(m[3], 4) + "," +
        number(o[0], 2) + "," + number(o[1], 2) + "," + number(o[2], 2) + "," + number(o[3], 2) + "]";
}

/// Whether \a timeline has layers attached to the FLA's camera, out of
/// reach of its color: then each layer it colors carries the color itself.
bool attachedLayers(const Timeline& timeline)
{
    return std::any_of(timeline.layers.begin(), timeline.layers.end(),
        [](const Layer* layer) { return layer && camera::attached(*layer); });
}

/// Whether \a frame is an object motion tween in 3D.
bool turnedIn3D(const Frame& frame)
{
    if (frame.otherTweenType != "motion object")
        return false;
    MotionObject motion;
    const std::string* xml = timeline::motionOf(const_cast<Frame&>(frame));
    return xml && motion.parse(*xml) && motion.threeD();
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
    case Filter::Type::ColorMatrix:
    {
        std::string values;
        for (const double v : f.matrix)
            values += (values.empty() ? "" : ",") + number(v, 5);
        return "{\"t\":\"matrix\",\"m\":[" + values + "]}";
    }
    case Filter::Type::Convolution: // the page's player has none
        return std::string();
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

/// Whether \a layer has effects on instances that no filter carries on
/// some frame they show.
bool uncarriedEffects(const Layer& layer)
{
    for (const Frame* frame : layer.frames)
        for (const Element* element : frame ? frame->elements : std::vector<Element*>())
        {
            if (!element || element->elementType() != Element::Type::SymbolInstance ||
                !compositing::hasInstanceEffects(*element))
                continue;
            const std::vector<behaviors::Applied> effects = compositing::instanceEffectsOf(*element);
            for (int f = frame->index; f < frame->index + std::max(1, frame->duration); ++f)
            {
                std::vector<Filter> filters;
                ColorTransform color = static_cast<const SymbolInstance*>(element)->colorTransform;
                if (!otherapps::carryEffects(effects, f, filters, color))
                    return true;
            }
        }
    return false;
}

} // namespace

std::vector<BakedRun> bakedRuns(const Timeline& timeline)
{
    const std::vector<Layer*>& layers = timeline.layers;
    const int n = static_cast<int>(layers.size());
    std::vector<BakedRun> runs;
    const auto add = [&runs](int from, int to, const std::string& why) {
        if (from < to)
            runs.push_back({from, to, why});
    };
    const auto plain = [&layers](int i) {
        return layers[static_cast<size_t>(i)] && layers[static_cast<size_t>(i)]->layerType != Layer::Type::Folder;
    };
    const auto indexOf = [&layers](const Layer* layer) {
        const auto found = std::find(layers.begin(), layers.end(), layer);
        return found == layers.end() ? -1 : static_cast<int>(found - layers.begin());
    };
    if (nodegraph::active(timeline))
        add(0, n, "a node graph");
    // Instances turned in 3D, which Phoenix draws in perspective, lit,
    // shadowed and sorted by depth across their layers: all those layers.
    int first3d = n, last3d = -1;
    for (int i = 0; i < n; ++i)
        if (plain(i))
            for (const Frame* frame : layers[static_cast<size_t>(i)]->frames)
            {
                if (frame && turnedIn3D(*frame))
                {
                    first3d = std::min(first3d, i);
                    last3d = std::max(last3d, i);
                }
                for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                    if (element && element->elementType() == Element::Type::SymbolInstance &&
                        !threed::of(*static_cast<const SymbolInstance*>(element)).flat())
                    {
                        first3d = std::min(first3d, i);
                        last3d = std::max(last3d, i);
                    }
            }
    add(first3d, last3d + 1, "3D rotation");
    for (int i = 0; i < n; ++i)
    {
        const Layer* layer = layers[static_cast<size_t>(i)];
        if (!plain(i))
            continue;
        if (compositing::isAdjustment(*layer))
            add(i, n, "an adjustment layer");
        else if (compositing::layerModeOf(*layer) != compositing::LayerMode::Normal)
            add(i, n, "a layer mode");
        if (effects::isEffect(*layer))
        {
            // It works on the first layer under it that is not an effect.
            int target = i + 1;
            while (target < n && (!plain(target) || effects::isEffect(*layers[static_cast<size_t>(target)])))
                ++target;
            add(i, std::min(n, target + 1), "an effect layer");
        }
        if (layer->layerType == Layer::Type::Normal && compositing::matteOf(*layer) != compositing::Matte::None)
        {
            int source = i - 1;
            while (source >= 0 && !plain(source))
                --source;
            add(std::max(0, source), i + 1, "a track matte");
        }
        if (layer->layerType == Layer::Type::Mask)
        {
            bool soft = !compositing::maskEdgeOf(*layer).plain();
            for (const Frame* frame : layer->frames)
                for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                    soft = soft || (element && compositing::elementFeatherOf(*element, 0.0) > 0.0);
            if (soft)
                add(i, i + 1, "a mask with a soft, grown or inverted edge");
        }
        const std::vector<compositing::MoreMask> more = compositing::moreMasksOf(*layer);
        if (!more.empty())
        {
            int from = i, to = i + 1;
            for (const compositing::MoreMask& mask : more)
                for (int j = 0; j < n; ++j)
                    if (plain(j) && layers[static_cast<size_t>(j)]->layerType == Layer::Type::Mask &&
                        layers[static_cast<size_t>(j)]->name == mask.layer)
                    {
                        from = std::min(from, j);
                        to = std::max(to, j + 1);
                    }
            add(from, to, "several masks");
        }
        if (motionblur::on(*layer) || motionblur::layerTimeOf(*layer).any())
            add(i, i + 1, "motion blur, echo or posterize time");
        bool operated = false;
        for (const Frame* frame : layer->frames)
            for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                operated = operated || (element && element->elementType() == Element::Type::Shape &&
                                           shapeops::any(*static_cast<const Shape*>(element)));
        if (operated)
            add(i, i + 1, "shape operators");
        bool feathered = false;
        for (const Frame* frame : layer->frames)
            for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                feathered = feathered || (element && feather::any(*element));
        if (feathered)
            add(i, i + 1, "feathered fills or strokes");
        if (uncarriedEffects(*layer))
            add(i, i + 1, "effects on instances no filter carries");
    }
    if (runs.empty())
        return runs;
    // A mask goes with all it masks, and what is masked with its mask, so
    // the runtime's masks are never cut in two; overlapping runs are one.
    bool grew = true;
    while (grew)
    {
        grew = false;
        std::sort(runs.begin(), runs.end(), [](const BakedRun& a, const BakedRun& b) { return a.from < b.from; });
        std::vector<BakedRun> merged;
        for (const BakedRun& run : runs)
        {
            if (!merged.empty() && run.from < merged.back().to)
            {
                merged.back().to = std::max(merged.back().to, run.to);
                if (merged.back().why.find(run.why) == std::string::npos)
                    merged.back().why += ", " + run.why;
                grew = true;
            }
            else
                merged.push_back(run);
        }
        runs = merged;
        for (BakedRun& run : runs)
            for (int i = run.from; i < run.to; ++i)
            {
                if (!plain(i))
                    continue;
                const Layer* layer = layers[static_cast<size_t>(i)];
                const Layer* mask = layer->layerType == Layer::Type::Mask ? layer
                    : layer->layerType == Layer::Type::Masked                ? layer->parentLayer
                                                                             : nullptr;
                if (!mask)
                    continue;
                const int m = indexOf(mask);
                if (m >= 0 && m < run.from)
                {
                    run.from = m;
                    grew = true;
                }
                for (int j = 0; j < n; ++j)
                    if (plain(j) && layers[static_cast<size_t>(j)]->parentLayer == mask && (j < run.from || j >= run.to))
                    {
                        run.from = std::min(run.from, j);
                        run.to = std::max(run.to, j + 1);
                        grew = true;
                    }
            }
    }
    return runs;
}

namespace {

class Walker
{
public:
    Walker(const Document& document, ArtSource& source, const Options& options, std::vector<std::string>* notes)
        : _document(document)
        , _source(source)
        , _options(options)
        , _notes(notes)
        , _rate(document.frameRate > 0.0 ? document.frameRate : 24.0)
    {
        // An ActionScript 3 movie's components: instances of their
        // symbols, which their translated classes make.
        if (!options.javaScript)
            _components = swfmovie::components(document);
    }

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
                if (symbol && (_options.allSymbols || !attributeOf(symbol->extraAttributes, "linkageClassName").empty() ||
                                  (_options.actionScript2 && !attributeOf(symbol->extraAttributes, "linkageIdentifier").empty())))
                    indexOf(symbol);
        _timelineName.clear();
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
        // ActionScript 2's name for attachMovie.
        const std::string identifier = attributeOf(symbol.extraAttributes, "linkageIdentifier");
        if (!identifier.empty() && _options.actionScript2)
            out += ",\"ident\":" + quoted(identifier);
        const std::string base = attributeOf(symbol.extraAttributes, "linkageBaseClass");
        if (!base.empty())
            out += ",\"base\":" + quoted(base);
        if (symbol.scale9)
            out += ",\"scale9\":[" + number(symbol.scaleGridLeft) + "," + number(symbol.scaleGridTop) + "," +
                number(symbol.scaleGridRight) + "," + number(symbol.scaleGridBottom) + "]";
        std::vector<const Timeline*> timelines;
        _timelineName = symbol.name;
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
        // The camera's color effect on each frame ("0" for none).
        std::vector<std::string> cameraColors;
        bool toned = false;
        int offset = 0;
        for (size_t s = 0; s < timelines.size(); ++s)
        {
            const Timeline& timeline = *timelines[s];
            const int length = lengthOf(timeline);
            if (scene)
                scenes += std::string(scenes.empty() ? "" : ",") + "{\"name\":" + quoted(timeline.name) + ",\"at\":" +
                    std::to_string(offset) + ",\"n\":" + std::to_string(length) + "}";
            gatherFrameData(timeline, offset, scene, labels, sounds, scripts);
            const bool filmed = scene && camera::filmingOf(timeline) && !attachedLayers(timeline);
            for (int frame = 0; frame < length; ++frame)
            {
                const std::string tone = filmed
                    ? colorJson(camera::stateAt(timeline, frame, _document.width, _document.height).color)
                    : std::string();
                cameraColors.push_back(tone.empty() ? "0" : tone);
                toned = toned || !tone.empty();
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
        if (toned)
        {
            out += ",\"cc\":[";
            for (size_t i = 0; i < cameraColors.size(); ++i)
                out += (i ? "," : "") + cameraColors[i];
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
                    else if (_options.actionScript2)
                        _options.actionScript2->frames.push_back({_timelineName, at, frame->actionScript->code,
                            scene ? timeline.name : std::string(),
                            (_timelineName.empty() ? timeline.name : _timelineName) + ", layer " + layer->name + ", frame " +
                                std::to_string(frame->index + 1)});
                    else
                        actionScript = true;
                }
            }
        }
        for (const auto& [frame, source] : code)
            scripts.emplace_back(frame, source);
        if (actionScript && !_options.actionScriptTranslated)
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
        _frame = frame;
        // A scene's multiplane camera: each layer framed by it at its
        // depth, its color effect on the whole picture (the timeline's
        // "cc").
        const bool filmed = scene && camera::filmingOf(timeline);
        const camera::State shot =
            filmed ? camera::stateAt(timeline, frame, _document.width, _document.height) : camera::State();
        // With layers attached to the FLA's camera, the others carry its
        // color themselves.
        const bool toning = filmed && attachedLayers(timeline);
        const std::vector<BakedRun>& runs = runsOf(timeline);
        std::string out;
        for (int li = static_cast<int>(layers.size()) - 1; li >= 0; --li)
        {
            const Layer* layer = layers[static_cast<size_t>(li)];
            _tone = toning && layer && !camera::attached(*layer) ? shot.color : ColorTransform();
            // Layers Phoenix's compositing draws together: one picture, in
            // the place of the run's bottom layer.
            const auto run = std::find_if(runs.begin(), runs.end(),
                [li](const BakedRun& each) { return li >= each.from && li < each.to; });
            if (run != runs.end())
            {
                if (li != run->to - 1)
                    continue;
                const std::string json = _source.picture(timeline, run->from, run->to, frame);
                const int art = json.empty() ? -1 : artIndexOf(json);
                bool attachedIn = false;
                for (int i = run->from; i < run->to; ++i)
                    attachedIn = attachedIn || (layers[static_cast<size_t>(i)] && camera::attached(*layers[static_cast<size_t>(i)]));
                const std::string tone = toning && !attachedIn ? colorJson(shot.color) : std::string();
                if (art >= 0)
                    out += std::string(out.empty() ? "" : ",") + "{\"k\":" + quoted(prefix + "B" + std::to_string(run->from)) +
                        ",\"d\":" + std::to_string((static_cast<int>(layers.size()) - li) * 65536) + ",\"t\":\"a\",\"id\":" +
                        std::to_string(art) + (tone.empty() ? std::string() : ",\"ct\":" + tone) + "}";
                continue;
            }
            if (!layer || layer->layerType == Layer::Type::Folder || layer->layerType == Layer::Type::Guide || !shown(*layer))
                continue;
            Transform view;
            const bool framed = filmed && !camera::attached(*layer) && !camera::isFileCamera(*layer);
            if (framed && !camera::view(shot, camera::depthOf(*layer), _document.width, _document.height, view))
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
            Transform parent = parentTransform(timeline, *layer, *span, frame, here, 0);
            if (framed)
                parent = followedBy(parent, view);
            // A layer sorted by depth stacks its clips by their Z.
            std::vector<int> rank(here.elements.size());
            std::iota(rank.begin(), rank.end(), 0);
            if (depthsort::sorts(*layer) && here.elements.size() == span->elements.size())
            {
                const std::vector<size_t> order = depthsort::drawOrder(*layer, *span, framesIn);
                for (size_t k = 0; k < order.size(); ++k)
                    rank[order[k]] = static_cast<int>(k);
            }
            for (size_t ei = 0; ei < here.elements.size(); ++ei)
            {
                const Element* element = here.elements[ei];
                const std::string entry = entryJson(*element, here.originals[ei], here.copies ? 0 : framesIn, parent,
                    prefix + "L" + std::to_string(li) + "E" + std::to_string(ei),
                    (static_cast<int>(layers.size()) - li) * 65536 + rank[ei] + 1, maskGroup, maskedBy);
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
        // A camera's color the entry carries (an instance's over its own).
        bool toned = false;
        const auto masks = [&]() {
            return (!toned && !_tone.isIdentity() ? ",\"ct\":" + colorJson(_tone) : std::string()) +
                (maskGroup >= 0 ? ",\"mk\":" + std::to_string(maskGroup) : std::string()) +
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
            if (type != SymbolType::Graphic && _options.actionScript2)
            {
                const int handlers = instanceScriptId(original ? *original : element, instance);
                if (handlers >= 0)
                    out += ",\"h\":" + std::to_string(handlers);
            }
            // Phoenix's effects on the instance, as filters before its own
            // and its color effect (those no filter carries make its layer
            // a picture a frame: see bakedRuns).
            ColorTransform colorEffect = instance.colorTransform;
            std::vector<Filter> filterList = instance.filters;
            const Element& own = original ? *original : element;
            if (compositing::hasInstanceEffects(own))
            {
                std::vector<Filter> carried;
                otherapps::carryEffects(compositing::instanceEffectsOf(own), _frame, carried, colorEffect);
                filterList.insert(filterList.begin(), carried.begin(), carried.end());
            }
            const std::string color = colorJson(colorEffect, _tone);
            toned = true;
            if (!color.empty())
                out += ",\"ct\":" + color;
            const std::string filters = filtersJson(filterList);
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
        {
            const auto* raw = static_cast<const RawElement*>(original ? original : &element);
            const auto component = _components.find(raw);
            const Symbol* symbol = nullptr;
            if (component != _components.end() && _document.symbolList)
            {
                const auto found = _document.symbolList->symbolMap.find(component->second.symbol);
                symbol = found == _document.symbolList->symbolMap.end() ? nullptr : found->second;
            }
            // An ActionScript 2 component the runtime has its own of
            // (as2components.js): its class, and a symbol standing in for
            // its own when the library has none of it.
            std::string as2Class;
            if (component != _components.end() && _options.actionScript2)
            {
                as2Class = symbol ? attributeOf(symbol->extraAttributes, "linkageClassName")
                                  : componentClass(component->second.symbol);
                if (!isAs2Component(as2Class))
                    as2Class.clear();
                if (!symbol && !as2Class.empty())
                    symbol = standIn(component->second.symbol, as2Class);
            }
            if (!symbol)
            {
                // What it is, by its tag: a video, a TLF text field...
                const size_t start = raw->xml.find("<DOM");
                const size_t end = start == std::string::npos ? start : raw->xml.find_first_of(" />", start);
                const std::string tag = start == std::string::npos ? std::string("element")
                                                                   : raw->xml.substr(start + 4, end - start - 4);
                note(component != _components.end()
                        ? "The component " + component->second.name + " is not published: its symbol " +
                            component->second.symbol + " is not in the library."
                        : "What the file keeps as " + tag + " is not published.");
                return std::string();
            }
            // Its name (one made for it when it has none) is what the
            // code setting its parameters calls it.
            const swf::Matrix& m = component->second.matrix;
            Transform own;
            own.m11 = m.a;
            own.m12 = m.b;
            own.m21 = m.c;
            own.m22 = m.d;
            own.tx = m.tx;
            own.ty = m.ty;
            out += ",\"t\":\"s\",\"id\":" + std::to_string(indexOf(symbol));
            if (symbol->symbolType != SymbolType::MovieClip)
                out += ",\"it\":\"mc\"";
            out += placed(own);
            out += ",\"n\":" + quoted(component->second.name);
            if (!as2Class.empty())
                out += ",\"cp\":" + parametersJson(component->second.parameters);
            return out + masks() + "}";
        }
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

    /// The id of an instance's on() and onClipEvent() handlers, or -1
    /// when it has none.
    int instanceScriptId(const Element& element, const SymbolInstance& instance)
    {
        const auto known = _scriptIds.find(&element);
        if (known != _scriptIds.end())
            return known->second;
        const std::string code = scripts::instanceScript(element);
        int id = -1;
        if (!code.empty())
        {
            id = static_cast<int>(_options.actionScript2->instances.size());
            const std::string what = instance.symbolType == SymbolType::Button ? "button " : "movie clip ";
            _options.actionScript2->instances.push_back({id, code,
                (_timelineName.empty() ? std::string("main timeline") : _timelineName) + ", " + what +
                    (instance.name.empty() ? instance.libraryItemName : instance.name)});
        }
        _scriptIds[&element] = id;
        return id;
    }

    int artIndex(const Element& element)
    {
        const auto known = _artByElement.find(&element);
        if (known != _artByElement.end())
            return known->second;
        const std::string json = _source.art(element);
        const int index = json.empty() ? -1 : artIndexOf(json);
        // Copies a tween makes come and go; only the document's own
        // elements are remembered by address.
        if (element.parent && dynamic_cast<const Frame*>(element.parent) &&
            std::find(static_cast<const Frame*>(element.parent)->elements.begin(),
                static_cast<const Frame*>(element.parent)->elements.end(), &element) !=
                static_cast<const Frame*>(element.parent)->elements.end())
            _artByElement[&element] = index;
        return index;
    }

    /// The class of the component library item \a name: its linkage where
    /// the file keeps the item (a compiled clip, say), else the ActionScript
    /// 2 component of that name.
    std::string componentClass(const std::string& name) const
    {
        for (const std::string& item : _document.extraMedia)
        {
            tinyxml2::XMLDocument xml;
            if (item.find(name) == std::string::npos || xml.Parse(item.c_str(), item.size()) != tinyxml2::XML_SUCCESS ||
                !xml.RootElement())
                continue;
            const char* itemName = xml.RootElement()->Attribute("name");
            const char* linkage = xml.RootElement()->Attribute("linkageClassName");
            if (itemName && name == itemName && linkage && *linkage)
                return linkage;
        }
        const size_t slash = name.find_last_of('/');
        return "mx.controls." + (slash == std::string::npos ? name : name.substr(slash + 1));
    }

    static bool isAs2Component(const std::string& cls)
    {
        static const char* const known[] = {"Button", "CheckBox", "RadioButton", "TextInput", "TextArea", "Label",
            "ComboBox", "List", "NumericStepper"};
        for (const char* each : known)
            if (cls == std::string("mx.controls.") + each)
                return true;
        return false;
    }

    /// A symbol for a component the library has none of: empty, linked to
    /// \a cls, which draws it.
    const Symbol* standIn(const std::string& name, const std::string& cls)
    {
        for (const auto& made : _standIns)
            if (made->name == name)
                return made.get();
        auto symbol = std::make_unique<Symbol>(nullptr);
        symbol->name = name;
        symbol->symbolType = SymbolType::MovieClip;
        symbol->extraAttributes.emplace_back("linkageClassName", cls);
        _standIns.push_back(std::move(symbol));
        return _standIns.back().get();
    }

    /// A component's parameters (its parametersAsXML) as a JSON object of
    /// their values, by their types.
    static std::string parametersJson(const std::string& parameters)
    {
        tinyxml2::XMLDocument xml;
        const std::string wrapped = "<parameters>" + parameters + "</parameters>";
        if (parameters.empty() || xml.Parse(wrapped.c_str(), wrapped.size()) != tinyxml2::XML_SUCCESS)
            return "{}";
        std::string out;
        for (const tinyxml2::XMLElement* property = xml.RootElement()->FirstChildElement("property"); property;
             property = property->NextSiblingElement("property"))
        {
            const tinyxml2::XMLElement* inspectable = property->FirstChildElement("Inspectable");
            if (!inspectable)
                continue;
            const char* variable = inspectable->Attribute("variable");
            const char* named = inspectable->Attribute("name");
            const std::string name = variable && *variable ? variable : named ? named : "";
            const std::string value = inspectable->Attribute("defaultValue") ? inspectable->Attribute("defaultValue") : "";
            const std::string type = inspectable->Attribute("type") ? inspectable->Attribute("type") : "String";
            if (name.empty())
                continue;
            std::string json;
            if (type == "Boolean")
                json = value == "true" ? "true" : "false";
            else if (type == "Number")
            {
                char* end = nullptr;
                const double n = std::strtod(value.c_str(), &end);
                json = !value.empty() && end && *end == '\0' ? number(n, 6) : "null";
            }
            else if (type == "Color")
                json = std::to_string(std::strtoul(value.c_str() + (!value.empty() && value[0] == '#' ? 1 : 0), nullptr, 16));
            else if (type == "Array")
            {
                // "[a,b]" or "a,b".
                std::string list = value;
                if (list.size() >= 2 && list.front() == '[' && list.back() == ']')
                    list = list.substr(1, list.size() - 2);
                json = "[";
                size_t at = 0;
                while (!list.empty() && at <= list.size())
                {
                    const size_t comma = std::min(list.find(',', at), list.size());
                    json += (json.size() > 1 ? "," : "") + quoted(list.substr(at, comma - at));
                    at = comma + 1;
                }
                json += "]";
            }
            else if (type == "Collection" || type == "Object")
                continue;
            else
                json = quoted(value);
            out += (out.empty() ? "" : ",") + quoted(name) + ":" + json;
        }
        return "{" + out + "}";
    }

    /// A shape's index in the content's art, the same shape once.
    int artIndexOf(const std::string& json)
    {
        const auto found = _artIndex.find(json);
        if (found != _artIndex.end())
            return found->second;
        const int index = static_cast<int>(_art.size());
        _artIndex[json] = index;
        _art.push_back(json);
        return index;
    }

    /// The runs of \a timeline's layers published as pictures, each noted
    /// once.
    const std::vector<BakedRun>& runsOf(const Timeline& timeline)
    {
        const auto known = _runs.find(&timeline);
        if (known != _runs.end())
            return known->second;
        std::vector<BakedRun>& runs = _runs[&timeline];
        runs = bakedRuns(timeline);
        const std::string where = _timelineName.empty() ? timeline.name : _timelineName;
        for (const BakedRun& run : runs)
        {
            std::string names;
            for (int i = run.from; i < run.to; ++i)
            {
                const Layer* layer = timeline.layers[static_cast<size_t>(i)];
                if (layer && layer->layerType != Layer::Type::Folder)
                    names += (names.empty() ? "" : ", ") + layer->name;
            }
            note(where + ": " + (run.to - run.from == 1 ? "layer " : "layers ") + names + " (" + run.why +
                ") published as a picture a frame, which scripts cannot reach into.");
        }
        return runs;
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
        // A variable it shows (ActionScript 1 and 2), and whether as HTML.
        const std::string variable = attributeOf(text.extraAttributes, "variableName");
        if (!variable.empty())
            out += ",\"var\":" + quoted(variable);
        if (attributeOf(text.extraAttributes, "renderAsHTML") == "true")
            out += ",\"html\":1";
        out += ",\"runs\":[";
        for (size_t i = 0; i < text.runs.size(); ++i)
        {
            const TextRun& run = text.runs[i];
            // A face written as its PostScript name carries its style:
            // Arial-BoldMT is Arial, bold.
            std::string face = run.face.empty() ? std::string("Times New Roman") : run.face;
            bool bold = attributeOf(run.extraAttributes, "bold") == "true";
            bool italic = attributeOf(run.extraAttributes, "italic") == "true";
            const auto endsWith = [&face](const char* end) {
                const size_t n = std::strlen(end);
                return face.size() > n && face.compare(face.size() - n, n, end) == 0;
            };
            if (face.find('-') != std::string::npos || endsWith("MT") || endsWith("PS"))
            {
                bool styledBold = false, styledItalic = false;
                const std::string written = face;
                vectors::splitFontName(written, face, styledBold, styledItalic);
                bold = bold || styledBold;
                italic = italic || styledItalic;
            }
            out += std::string(i ? "," : "") + "{\"t\":" + quoted(run.text) + ",\"f\":" + quoted(face) + ",\"s\":" +
                number(run.size > 0.0 ? run.size : 12.0, 2) + ",\"c\":" + std::to_string(rgbOf(run.fillColor));
            if (bold)
                out += ",\"b\":1";
            if (italic)
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
    std::map<const Timeline*, std::vector<BakedRun>> _runs;
    /// The frame of the timeline being walked.
    int _frame = 0;
    /// The camera's color over the layer being written, when it carries it.
    ColorTransform _tone;
    std::map<const Element*, int> _artByElement;
    std::vector<std::string> _texts;
    std::map<std::string, int> _textIndex;
    std::map<const RawElement*, swfmovie::Component> _components;
    std::vector<std::unique_ptr<Symbol>> _standIns;
    /// The timeline being walked: "" for the main one, else its symbol's
    /// name; and the ids of instances' handlers (ActionScript 2).
    std::string _timelineName;
    std::map<const Element*, int> _scriptIds;
};

} // namespace

std::string content(const Document& document, ArtSource& source, const Options& options, std::vector<std::string>* notes)
{
    Walker walker(document, source, options, notes);
    return walker.run();
}

std::map<std::string, std::vector<uint8_t>> compiledClips(const Document& document)
{
    std::map<std::string, std::vector<uint8_t>> movies;
    for (const std::string& item : document.extraMedia)
    {
        if (item.find("CompiledClip") == std::string::npos)
            continue;
        tinyxml2::XMLDocument xml;
        if (xml.Parse(item.c_str(), item.size()) != tinyxml2::XML_SUCCESS || !xml.RootElement() ||
            !std::strstr(xml.RootElement()->Name(), "CompiledClip") || !xml.RootElement()->Attribute("name"))
            continue;
        // The file the item names (as a bitmap names its data), when it
        // holds a movie.
        for (const tinyxml2::XMLAttribute* attribute = xml.RootElement()->FirstAttribute(); attribute;
             attribute = attribute->Next())
        {
            const std::string key = attribute->Name();
            if (key.size() < 4 || key.compare(key.size() - 4, 4, "HRef") != 0)
                continue;
            const std::string value = attribute->Value();
            for (const auto& [path, bytes] : document.extraFiles)
            {
                if (path != value && path != "bin/" + value)
                    continue;
                const std::vector<uint8_t>& data = bytes.get();
                if (data.size() > 8 && (data[0] == 'F' || data[0] == 'C' || data[0] == 'Z') && data[1] == 'W' &&
                    data[2] == 'S')
                    movies[xml.RootElement()->Attribute("name")] = data;
            }
        }
    }
    return movies;
}

void embedCompiledClips(Document& copy, const std::map<std::string, std::vector<uint8_t>>& movies,
    const std::function<std::unique_ptr<FLADocument>(const std::vector<uint8_t>& swf)>& read,
    std::vector<std::unique_ptr<Command>>& keep, std::vector<std::string>& notes)
{
    for (const auto& [name, bytes] : movies)
    {
        // The movie read as a document of its own, its main timeline made a
        // movie clip symbol, copied into the copy with all it uses.
        std::unique_ptr<FLADocument> movie = read ? read(bytes) : nullptr;
        Document* source = movie ? movie->document : nullptr;
        if (!source || source->timelines.empty() || !source->timelines.front())
        {
            notes.push_back("The compiled clip " + name + " could not be read; it is not published.");
            continue;
        }
        if (!source->symbolList)
            source->symbolList = new SymbolList(source);
        auto* symbol = new Symbol(source->symbolList);
        symbol->name = name;
        while (source->symbolList->symbolMap.count(symbol->name))
            symbol->name += " clip";
        symbol->symbolType = SymbolType::MovieClip;
        Timeline* timeline = source->timelines.front();
        source->timelines.erase(source->timelines.begin());
        timeline->parent = symbol;
        timeline->name = symbol->name;
        symbol->timelines.push_back(timeline);
        source->symbolList->symbols.push_back(symbol);
        source->symbolList->link(symbol);
        std::vector<std::string> added;
        CommandPtr command =
            transfer::copyItems(copy, *source, {symbol->name}, transfer::Resolution::KeepBoth, &added);
        if (!command)
            continue;
        command->redo();
        keep.push_back(std::move(command));
        const std::string inCopy = added.empty() ? symbol->name : added.front();
        Symbol* made = copy.symbolList && copy.symbolList->symbolMap.count(inCopy) ? copy.symbolList->symbolMap.at(inCopy)
                                                                                    : nullptr;
        if (!made)
            continue;
        // Each instance of the compiled clip, an instance of the symbol in
        // its place.
        std::vector<Timeline*> timelines(copy.timelines.begin(), copy.timelines.end());
        for (Symbol* each : copy.symbolList->symbols)
            if (each && each != made)
                timelines.insert(timelines.end(), each->timelines.begin(), each->timelines.end());
        int placed = 0;
        for (Timeline* each : timelines)
            for (Layer* layer : each ? each->layers : std::vector<Layer*>())
                for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                {
                    if (!frame)
                        continue;
                    for (Element*& element : frame->elements)
                    {
                        auto* raw = dynamic_cast<RawElement*>(element);
                        tinyxml2::XMLDocument xml;
                        if (!raw || raw->xml.find("CompiledClipInstance") == std::string::npos ||
                            xml.Parse(raw->xml.c_str(), raw->xml.size()) != tinyxml2::XML_SUCCESS || !xml.RootElement())
                            continue;
                        const tinyxml2::XMLElement* root = xml.RootElement();
                        const char* item = root->Attribute("libraryItemName");
                        if (!item || name != item)
                            continue;
                        auto* instance = new SymbolInstance(frame);
                        instance->libraryItemName = made->name;
                        instance->symbol = made;
                        instance->symbolType = SymbolType::MovieClip;
                        instance->name = root->Attribute("name") ? root->Attribute("name") : "";
                        if (const tinyxml2::XMLElement* matrix = root->FirstChildElement("matrix"))
                            if (const tinyxml2::XMLElement* m = matrix->FirstChildElement("Matrix"))
                                instance->transform = Transform(m->DoubleAttribute("a", 1.0), m->DoubleAttribute("b", 0.0),
                                    m->DoubleAttribute("c", 0.0), m->DoubleAttribute("d", 1.0), m->DoubleAttribute("tx", 0.0),
                                    m->DoubleAttribute("ty", 0.0));
                        copy.symbolInstances.push_back(instance);
                        delete raw;
                        element = instance;
                        ++placed;
                    }
                }
        if (placed)
            notes.push_back("The compiled clip " + name + " is published as the movie it carries.");
    }
}

} // namespace fla::html5
