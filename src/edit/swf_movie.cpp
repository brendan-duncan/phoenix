#include "swf_movie.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/linear_gradient.h"
#include "../data/morph_shape.h"
#include "../data/radial_gradient.h"
#include "../data/raw_element.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../play/display/bitmap_filter.h"
#include "../play/script/class_path.h"
#include "camera.h"
#include "motion_object.h"
#include "phoenix_features.h"
#include "shape_tween.h"
#include "swf_actionscript3.h"
#include "script_edit.h"
#include "timeline_edit.h"
#include "tween_ease.h"

#include "avm1/avm1.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <set>
#include <sstream>
#include <tuple>

namespace fla {
namespace swfmovie {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& name)
{
    for (const auto& attribute : attributes)
        if (attribute.first == name)
            return attribute.second;
    return std::string();
}

int timelineLength(const Timeline* timeline)
{
    int length = 0;
    for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
        if (layer)
            length = std::max(length, timeline::layerEnd(*layer));
    return std::max(1, length);
}

/// The keyframe whose span holds \a index, or null.
const Frame* keyframeAt(const Layer& layer, int index)
{
    for (const Frame* frame : layer.frames)
        if (frame && index >= frame->index && index < frame->index + std::max(1, frame->duration))
            return frame;
    return nullptr;
}

std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}

/// The key of a keyPress handler ("keyPress <Left>", "keyPress a") as
/// buttons and clip actions number them.
int keyCode(const std::string& event)
{
    std::string key = event.substr(std::min<size_t>(event.size(), 9));
    while (!key.empty() && key.front() == ' ')
        key.erase(key.begin());
    static const std::pair<const char*, int> named[] = {{"<Left>", 1}, {"<Right>", 2}, {"<Home>", 3}, {"<End>", 4},
        {"<Insert>", 5}, {"<Delete>", 6}, {"<Backspace>", 8}, {"<Enter>", 13}, {"<Up>", 14}, {"<Down>", 15},
        {"<PageUp>", 16}, {"<PageDown>", 17}, {"<Tab>", 18}, {"<Escape>", 19}, {"<Space>", 32}};
    for (const auto& [name, code] : named)
        if (key == name)
            return code;
    return key.empty() ? 0 : static_cast<unsigned char>(key[0]);
}

/// Whether a document's scripts look like ActionScript 3 (\a three) or like
/// ActionScript 1 and 2, by what only each writes, as Test Movie tells.
bool scriptsLookLike(const Document& document, bool three)
{
    static const char* const older[] = {"setProperty(", "getProperty(", "tellTarget", "ifFrameLoaded", "onClipEvent",
        "_root.", "_parent.", "._visible", "._x", "attachMovie("};
    static const char* const newer[] = {"import flash.", "import fl.", "addEventListener", ":void", "package ",
        "MouseEvent.", "Event.ENTER_FRAME", "new Sprite(", "new MovieClip("};
    const auto marked = [three](const std::string& code) {
        const char* const* marks = three ? newer : older;
        const size_t count = three ? std::size(newer) : std::size(older);
        for (size_t i = 0; i < count; ++i)
            if (code.find(marks[i]) != std::string::npos)
                return true;
        return false;
    };
    std::vector<const Timeline*> timelines(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (const auto& [name, symbol] : document.symbolList->symbolMap)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                if (frame->actionScript && marked(frame->actionScript->code))
                    return true;
                // Scripts on instances are ActionScript 1 and 2's alone.
                if (!three)
                    for (const Element* element : frame->elements)
                        if (element && element->elementType() == Element::Type::SymbolInstance &&
                            !scripts::instanceScript(*element).empty())
                            return true;
            }
    return false;
}

bool sameMatrix(const swf::Matrix& a, const swf::Matrix& b)
{
    const auto near = [](double x, double y, double tolerance) { return std::fabs(x - y) <= tolerance; };
    return near(a.a, b.a, 1.0e-5) && near(a.b, b.b, 1.0e-5) && near(a.c, b.c, 1.0e-5) && near(a.d, b.d, 1.0e-5) &&
        near(a.tx, b.tx, 0.025) && near(a.ty, b.ty, 0.025);
}

bool sameColor(const swf::ColorTransform& a, const swf::ColorTransform& b)
{
    return std::lround(a.red * 256) == std::lround(b.red * 256) &&
        std::lround(a.green * 256) == std::lround(b.green * 256) &&
        std::lround(a.blue * 256) == std::lround(b.blue * 256) &&
        std::lround(a.alpha * 256) == std::lround(b.alpha * 256) && a.redAdd == b.redAdd &&
        a.greenAdd == b.greenAdd && a.blueAdd == b.blueAdd && a.alphaAdd == b.alphaAdd;
}

/// What tells filter lists apart.
std::string filtersKey(const std::vector<swf::Filter>& filters)
{
    std::ostringstream key;
    key.precision(6);
    for (const swf::Filter& f : filters)
    {
        key << static_cast<int>(f.type) << ':' << int(f.color.r) << ',' << int(f.color.g) << ',' << int(f.color.b)
            << ',' << int(f.color.a) << ',' << int(f.highlight.r) << ',' << int(f.highlight.g) << ','
            << int(f.highlight.b) << ',' << int(f.highlight.a) << ',' << f.blurX << ',' << f.blurY << ',' << f.angle
            << ',' << f.distance << ',' << f.strength << ',' << f.inner << f.knockout << f.onTop << ',' << f.passes;
        for (const swf::Filter::Stop& stop : f.stops)
            key << ',' << stop.offset << '/' << int(stop.color.r) << '.' << int(stop.color.g) << '.'
                << int(stop.color.b) << '.' << int(stop.color.a);
        for (float value : f.matrix)
            key << ',' << value;
        key << ';';
    }
    return key.str();
}

/// \a inner placed by \a outer: one matrix that does both.
swf::Matrix times(const swf::Matrix& outer, const swf::Matrix& inner)
{
    swf::Matrix m;
    m.a = outer.a * inner.a + outer.c * inner.b;
    m.b = outer.b * inner.a + outer.d * inner.b;
    m.c = outer.a * inner.c + outer.c * inner.d;
    m.d = outer.b * inner.c + outer.d * inner.d;
    m.tx = outer.a * inner.tx + outer.c * inner.ty + outer.tx;
    m.ty = outer.b * inner.tx + outer.d * inner.ty + outer.ty;
    return m;
}

swf::Matrix matrixOf(const Transform& t)
{
    return swf::Matrix{t.m11, t.m12, t.m21, t.m22, t.tx, t.ty};
}

/// \a inner's color change, then \a outer's: one that does both.
swf::ColorTransform then(const swf::ColorTransform& inner, const swf::ColorTransform& outer)
{
    swf::ColorTransform c;
    c.red = inner.red * outer.red;
    c.green = inner.green * outer.green;
    c.blue = inner.blue * outer.blue;
    c.alpha = inner.alpha * outer.alpha;
    c.redAdd = static_cast<int>(std::lround(inner.redAdd * outer.red)) + outer.redAdd;
    c.greenAdd = static_cast<int>(std::lround(inner.greenAdd * outer.green)) + outer.greenAdd;
    c.blueAdd = static_cast<int>(std::lround(inner.blueAdd * outer.blue)) + outer.blueAdd;
    c.alphaAdd = static_cast<int>(std::lround(inner.alphaAdd * outer.alpha)) + outer.alphaAdd;
    return c;
}

/// A shape tween's fill as a morph shape has it, in the timeline's space
/// (the shape placed by \a place): a color or a gradient; false for what a
/// morph shape cannot blend (bitmaps, a radial gradient's focal point).
bool morphFill(const FillStyle* style, const Transform& place, swf::Fill& out)
{
    if (!style)
        return false;
    const auto color = [](const uint8_t c[4]) { return swf::Color{c[0], c[1], c[2], c[3]}; };
    out = swf::Fill();
    switch (style->type())
    {
    case FillStyle::Type::SolidColor:
        out.type = swf::Fill::Type::Solid;
        out.color = color(static_cast<const SolidColor*>(style)->color);
        return true;
    case FillStyle::Type::LinearGradient:
        out.type = swf::Fill::Type::Linear;
        for (const GradientEntry& entry : static_cast<const LinearGradient*>(style)->entries)
            out.stops.push_back({entry.ratio, color(entry.color)});
        break;
    case FillStyle::Type::RadialGradient:
    {
        const auto* radial = static_cast<const RadialGradient*>(style);
        if (std::fabs(radial->focalPointRatio) > 1.0e-3)
            return false;
        out.type = swf::Fill::Type::Radial;
        for (const RadialEntry& entry : radial->entries)
            out.stops.push_back({entry.ratio, color(entry.color)});
        break;
    }
    default:
        return false;
    }
    out.matrix = times(matrixOf(place), matrixOf(style->transform));
    return !out.stops.empty();
}

/// A shape tween's line as a morph shape has it: a plain solid line only.
bool morphLine(const StrokeStyle* style, const Transform& place, double& width, swf::Color& color)
{
    if (!style || style->style() != StrokeStyle::Style::Solid || !style->fill ||
        style->fill->type() != FillStyle::Type::SolidColor)
        return false;
    const uint8_t* c = static_cast<const SolidColor*>(style->fill)->color;
    color = swf::Color{c[0], c[1], c[2], c[3]};
    // Its weight scales with the shape, as Phoenix draws it.
    const double scale = style->scaleMode == "none"
        ? 1.0 : std::sqrt(std::fabs(place.m11 * place.m22 - place.m12 * place.m21));
    width = style->weight * scale;
    return true;
}

} // namespace

std::string fieldName(const StaticText& text)
{
    if (!text.instanceName.empty())
        return text.instanceName;
    return attributeOf(text.extraAttributes, "name");
}

bool scriptedField(const StaticText& text)
{
    if (text.textType == StaticText::TextType::Input)
        return true;
    return text.textType == StaticText::TextType::Dynamic &&
        (!fieldName(text).empty() || !attributeOf(text.extraAttributes, "variableName").empty());
}

bool holdsClips(const Symbol& symbol)
{
    // Looked into once a symbol, a symbol inside itself not again.
    std::set<const Symbol*> seen;
    std::function<bool(const Symbol&)> holds;
    std::function<bool(const Element*)> holdsIn = [&](const Element* element) {
        if (!element)
            return false;
        if (element->elementType() == Element::Type::Group)
        {
            for (const Element* member : static_cast<const Group*>(element)->members)
                if (holdsIn(member))
                    return true;
            return false;
        }
        if (element->elementType() == Element::Type::StaticText)
            return scriptedField(*static_cast<const StaticText*>(element));
        if (element->elementType() != Element::Type::SymbolInstance)
            return false;
        const auto* instance = static_cast<const SymbolInstance*>(element);
        if (instance->symbolType != SymbolType::Graphic)
            return instance->symbol != nullptr;
        return instance->symbol && holds(*instance->symbol);
    };
    holds = [&](const Symbol& graphic) {
        if (!seen.insert(&graphic).second || graphic.timelines.empty() || !graphic.timelines.front())
            return false;
        bool found = false;
        for (const Layer* layer : graphic.timelines.front()->layers)
        {
            if (!layer || layer->layerType == Layer::Type::Guide || layer->layerType == Layer::Type::Folder)
                continue;
            // A mask in it: drawn as art, as masks are where they are.
            if (layer->layerType == Layer::Type::Mask)
                return false;
            for (const Frame* frame : layer->frames)
                for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                    found = found || holdsIn(element);
        }
        return found;
    };
    return holds(symbol);
}

bool Drawer::shows(const Timeline& timeline, const Layer& layer) const
{
    // The main timeline's eye hides a layer; a symbol's layers show where
    // it is placed.
    const bool scene = dynamic_cast<const Document*>(timeline.parent) != nullptr;
    return !scene || layer.isVisible();
}

std::string unstructured(const Document& document)
{
    // What Phoenix draws that a movie's own drawing of its clips cannot
    // show: the movie then goes out as frames of shapes.
    static const char* const blocking[] = {"effect-layers", "line-and-color-art", "multiplane-camera", "pegs",
        "deformers", "behaviors", "compositing", "instance-effects", "node-graph", "mask-edges", "several-masks",
        "motion-blur", "templates", "master-controllers", "3d-rotation", "depth-sorting", "parenting-options", "bone-corrections"};
    for (const features::Use& use : features::used(document))
    {
        if (!use.feature)
            continue;
        for (const char* id : blocking)
            if (std::strcmp(use.feature->id, id) == 0)
                return std::string(use.feature->title) + (use.where.empty() ? std::string() : " (" + use.where + ")");
    }
    // The FLA's own camera layer and motion tweens in 3D, which a player
    // shows only through scripts the movie does not have.
    for (const Timeline* scene : document.timelines)
        if (scene && camera::filmingOf(*scene))
            return "Camera layer (" + scene->name + ")";
    std::vector<const Timeline*> timelines(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame || frame->otherTweenType != "motion object")
                    continue;
                MotionObject motion;
                const std::string* xml = timeline::motionOf(const_cast<Frame&>(*frame));
                if (xml && motion.parse(*xml) && motion.threeD())
                    return "3D motion tween (" + timeline->name + ", " + layer->name + ")";
            }
    return std::string();
}

std::map<std::string, std::string> actionScript2Classes(const Document& document,
    const std::function<std::optional<std::string>(const std::string& qualifiedName)>& findClass)
{
    if (!findClass)
        return {};
    // The names the scripts mention that are classes on the class path:
    // dotted paths and capitalized names, imports among them.
    std::map<std::string, std::string> sources;
    std::vector<std::string> pending;
    std::set<std::string> tried;
    const auto look = [&](const std::string& text) {
        size_t i = 0;
        while (i < text.size())
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (std::isalpha(c) || c == '_' || c == '$')
            {
                size_t j = i;
                while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_' ||
                                              text[j] == '$' || text[j] == '.'))
                    ++j;
                std::string word = text.substr(i, j - i);
                while (!word.empty() && word.back() == '.')
                    word.pop_back();
                // Each prefix of a dotted path: com.studio.Hat.make
                // names com.studio.Hat.
                for (size_t end = word.size(); end != std::string::npos && end > 0;)
                {
                    const std::string candidate = word.substr(0, end);
                    if (tried.insert(candidate).second)
                        pending.push_back(candidate);
                    end = word.rfind('.', end - 1);
                }
                i = j;
            }
            else if (c == '"' || c == '\'')
            {
                // Strings name no classes.
                size_t j = i + 1;
                while (j < text.size() && text[j] != text[i])
                    j += text[j] == '\\' ? 2 : 1;
                i = j + 1;
            }
            else
                ++i;
        }
    };
    const auto scriptsOf = [&](const Timeline* timeline) {
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                if (frame->actionScript)
                    look(frame->actionScript->code);
                for (const Element* element : frame->elements)
                    if (element && element->elementType() == Element::Type::SymbolInstance)
                        look(scripts::instanceScript(*element));
            }
    };
    for (const Timeline* scene : document.timelines)
        if (scene)
            scriptsOf(scene);
    if (document.symbolList)
        for (const auto& [name, symbol] : document.symbolList->symbolMap)
        {
            if (!symbol)
                continue;
            for (const Timeline* timeline : symbol->timelines)
                scriptsOf(timeline);
            const std::string linked = attributeOf(symbol->extraAttributes, "linkageClassName");
            if (!linked.empty() && tried.insert(linked).second)
                pending.push_back(linked);
        }
    while (!pending.empty())
    {
        const std::string name = pending.back();
        pending.pop_back();
        // Classes are named by words that start upper case, or paths.
        const size_t dot = name.find_last_of('.');
        const char first = name[dot == std::string::npos ? 0 : dot + 1];
        if (!std::isupper(static_cast<unsigned char>(first)) || sources.count(name))
            continue;
        const std::optional<std::string> source = findClass(name);
        if (!source)
            continue;
        sources[name] = *source;
        look(*source);
    }
    return sources;
}

bool hasScripts(const Document& document)
{
    std::vector<const Timeline*> timelines(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (const auto& [name, symbol] : document.symbolList->symbolMap)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    std::function<bool(const Element*)> scripted = [&](const Element* element) {
        if (!element)
            return false;
        if (element->elementType() == Element::Type::SymbolInstance && !scripts::instanceScript(*element).empty())
            return true;
        if (element->elementType() == Element::Type::Group)
            for (const Element* member : static_cast<const Group*>(element)->members)
                if (scripted(member))
                    return true;
        return false;
    };
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                if (frame->actionScript && !frame->actionScript->code.empty())
                    return true;
                for (const Element* element : frame->elements)
                    if (scripted(element))
                        return true;
            }
    return false;
}

swf::ColorTransform colorTransform(const ColorTransform& c)
{
    // Tint, then brightness, then the advanced multiply and add, made one.
    double multiply = 1.0;
    double add[3] = {0.0, 0.0, 0.0};
    if (c.tintMultiplier != 0.0)
    {
        multiply = 1.0 - c.tintMultiplier;
        for (int k = 0; k < 3; ++k)
            add[k] = c.tintMultiplier * c.tintColor[k];
    }
    if (c.brightness != 0.0)
    {
        const double scale = c.brightness > 0.0 ? 1.0 - c.brightness : 1.0 + c.brightness;
        const double offset = c.brightness > 0.0 ? 255.0 * c.brightness : 0.0;
        multiply *= scale;
        for (double& value : add)
            value = value * scale + offset;
    }
    const double multipliers[3] = {c.redMultiplier, c.greenMultiplier, c.blueMultiplier};
    const int offsets[3] = {c.redOffset, c.greenOffset, c.blueOffset};
    swf::ColorTransform out;
    double* channels[3] = {&out.red, &out.green, &out.blue};
    int* adds[3] = {&out.redAdd, &out.greenAdd, &out.blueAdd};
    for (int k = 0; k < 3; ++k)
    {
        *channels[k] = multipliers[k] * multiply;
        *adds[k] = static_cast<int>(std::lround(multipliers[k] * add[k])) + offsets[k];
    }
    out.alpha = c.alphaMultiplier;
    out.alphaAdd = c.alphaOffset;
    return out;
}

std::vector<swf::Filter> filters(const std::vector<Filter>& list)
{
    std::vector<swf::Filter> out;
    const auto color = [](const uint8_t rgba[4]) { return swf::Color{rgba[0], rgba[1], rgba[2], rgba[3]}; };
    for (const Filter& f : list)
    {
        if (!f.enabled)
            continue;
        swf::Filter s;
        s.blurX = f.blurX;
        s.blurY = f.blurY;
        s.passes = std::clamp(f.quality, 1, 3);
        s.angle = f.angle * kPi / 180.0;
        s.distance = f.distance;
        s.strength = f.strength;
        s.inner = f.inner;
        s.knockout = f.knockout;
        // Inner, outer or full, as a bevel or a gradient filter is placed.
        const auto placeBevel = [&]() {
            s.inner = f.bevelType.empty() || f.bevelType == "inner";
            s.onTop = f.bevelType == "full";
            if (s.onTop)
                s.inner = false;
        };
        switch (f.type)
        {
        case Filter::Type::Blur:
            s.type = swf::Filter::Type::Blur;
            break;
        case Filter::Type::DropShadow:
            s.type = swf::Filter::Type::DropShadow;
            s.color = color(f.color);
            break;
        case Filter::Type::Glow:
            s.type = swf::Filter::Type::Glow;
            s.color = color(f.color);
            break;
        case Filter::Type::Bevel:
            s.type = swf::Filter::Type::Bevel;
            s.color = color(f.shadowColor);
            s.highlight = color(f.highlightColor);
            placeBevel();
            break;
        case Filter::Type::GradientGlow:
        case Filter::Type::GradientBevel:
            s.type = f.type == Filter::Type::GradientGlow ? swf::Filter::Type::GradientGlow
                                                         : swf::Filter::Type::GradientBevel;
            for (const GradientEntry& entry : f.entries)
                s.stops.push_back({entry.ratio, color(entry.color)});
            placeBevel();
            break;
        case Filter::Type::AdjustColor:
        {
            // The matrix an FLA's Adjust Color is published as (fl.motion.AdjustColor).
            s.type = swf::Filter::Type::ColorMatrix;
            const std::vector<double> m =
                display::BitmapFilter::adjustColor(f.brightness, f.contrast, f.saturation, f.hue).matrix;
            for (size_t k = 0; k < 20 && k < m.size(); ++k)
                s.matrix[k] = static_cast<float>(m[k]);
            break;
        }
        case Filter::Type::ColorMatrix:
            s.type = swf::Filter::Type::ColorMatrix;
            for (size_t k = 0; k < 20 && k < f.matrix.size(); ++k)
                s.matrix[k] = static_cast<float>(f.matrix[k]);
            break;
        case Filter::Type::Convolution:
            s.type = swf::Filter::Type::Convolution;
            s.matrixX = f.matrixX;
            s.matrixY = f.matrixY;
            s.convolution.assign(f.matrix.begin(), f.matrix.end());
            s.divisor = static_cast<float>(f.divisor);
            s.bias = static_cast<float>(f.bias);
            s.clamp = f.clamp;
            s.preserveAlpha = f.preserveAlpha;
            s.color = color(f.color);
            break;
        }
        out.push_back(s);
    }
    return out;
}

int blendMode(const std::string& name)
{
    static const std::pair<const char*, int> modes[] = {{"normal", 1}, {"layer", 2}, {"multiply", 3}, {"screen", 4},
        {"lighten", 5}, {"darken", 6}, {"difference", 7}, {"add", 8}, {"subtract", 9}, {"invert", 10},
        {"alpha", 11}, {"erase", 12}, {"overlay", 13}, {"hardlight", 14}};
    for (const auto& [mode, number] : modes)
        if (name == mode)
            return number;
    return 1;
}

// ---- The movie

/// What shows at a depth on a frame.
struct Shown
{
    int character = 0;
    /// What makes it the same thing frame to frame: the symbol or the art.
    const void* identity = nullptr;
    swf::Matrix matrix;
    swf::ColorTransform color;
    std::vector<swf::Filter> filters;
    std::string filterKey;
    int blend = 1;
    std::string name;
    int clipDepth = 0;
    std::vector<uint8_t> clipActions;
    /// A morph shape's ratio, -1 for none.
    int ratio = -1;
};

/// What places \a shown at \a depth anew.
swf::Placement placementOf(int depth, const Shown& shown)
{
    swf::Placement p;
    p.depth = depth;
    p.character = shown.character;
    p.hasMatrix = true;
    p.matrix = shown.matrix;
    p.hasColor = !shown.color.identity();
    p.color = shown.color;
    p.ratio = shown.ratio;
    p.name = shown.name;
    p.clipDepth = shown.clipDepth;
    p.hasFilters = !shown.filters.empty();
    p.filters = shown.filters;
    p.hasBlend = shown.blend != 1;
    p.blend = shown.blend;
    p.clipActions = shown.clipActions;
    return p;
}

/// Where a timeline's tags go: the movie's main timeline, or a sprite's.
struct Out
{
    swf::Writer* writer = nullptr;
    swf::Sprite* sprite = nullptr;

    void place(const swf::Placement& p) { sprite ? sprite->place(p) : writer->place(p); }
    void remove(int depth) { sprite ? sprite->remove(depth) : writer->remove(depth); }
    void label(const std::string& name, bool anchor) { sprite ? sprite->frameLabel(name, anchor) : writer->frameLabel(name, anchor); }
    void doAction(const std::vector<uint8_t>& actions) { sprite ? sprite->doAction(actions) : writer->doAction(actions); }
    void startSound(int id, const swf::SoundInfo& info) { sprite ? sprite->startSound(id, info) : writer->startSound(id, info); }
    void show() { sprite ? sprite->showFrame() : writer->showFrame(); }
};

/// A layer's band of depths, and for a mask the masked layers' top.
struct Band
{
    int layer = -1;
    bool mask = false;
    int base = 0;
    /// Where each (element, place in the graphics it is in, part) goes,
    /// counted from base.
    std::map<std::tuple<int, std::vector<int>, int>, int> slots;
    int size = 0;
    int clipTop = 0;
};

struct Movie::State
{
    const Document& document;
    swf::Writer& writer;
    Drawer& drawer;
    Options options;
    std::vector<std::string> problems;

    std::vector<const Timeline*> scenes;
    std::vector<int> sceneStarts;
    bool actionScript3 = false;
    as3::avm1::Options compileOptions;

    std::map<const Symbol*, int> sprites;
    std::set<const Symbol*> writing;
    std::map<std::pair<const Symbol*, std::string>, int> buttons;
    std::map<std::string, int> artSprites;
    std::map<std::string, int> fonts;
    std::map<const StaticText*, int> fields;
    std::map<std::string, std::vector<uint8_t>> clipActionsByScript;
    std::map<const Symbol*, bool> staticSymbols;
    /// What the frames a symbol's own script runs before its first frame
    /// (#initclip), for its DoInitAction.
    std::map<const Symbol*, std::vector<uint8_t>> initActions;
    /// Element features that change the art frame by frame without a tween.
    bool animatedArt = false;
    /// An ActionScript 3 movie's components, placed as instances of their
    /// symbols for their classes to make.
    std::map<const RawElement*, Component> components;
    std::map<const RawElement*, std::unique_ptr<SymbolInstance>> componentInstances;
    /// Each shape tween's morph shape by its first keyframe, 0 for one that
    /// cannot be.
    std::map<const Frame*, int> morphs;
    /// Button states baked with their masks, by what they place.
    std::map<std::string, int> stateSprites;

    State(const Document& d, swf::Writer& w, Drawer& dr, const Options& o)
        : document(d)
        , writer(w)
        , drawer(dr)
        , options(o)
    {}

    void problem(const std::string& message)
    {
        if (std::find(problems.begin(), problems.end(), message) == problems.end())
            problems.push_back(message);
    }

    // ---- Scripts

    as3::avm1::Options optionsFor(bool main, int scene) const
    {
        as3::avm1::Options out = compileOptions;
        if (main && scene >= 0 && scene < static_cast<int>(scenes.size()))
        {
            out.sceneStart = sceneStarts[static_cast<size_t>(scene)];
            out.nextScene = scene + 1 < static_cast<int>(scenes.size()) ? sceneStarts[static_cast<size_t>(scene) + 1] : -1;
            out.prevScene = scene > 0 ? sceneStarts[static_cast<size_t>(scene) - 1] : -1;
        }
        return out;
    }

    void tell(const std::string& where, const std::vector<std::string>& errors)
    {
        for (const std::string& error : errors)
            problem(where + ": " + error);
    }

    /// The ActionScript 2 classes the scripts and linkage reach, compiled
    /// and written, each in a clip of its own whose DoInitAction defines it,
    /// a class before those that extend it.
    void writeClasses()
    {
        if (!options.findClass)
            return;
        const std::map<std::string, std::string> sources = actionScript2Classes(document, options.findClass);
        if (sources.empty())
            return;

        // What each declares, for the names their methods find, then each
        // compiled.
        as3::avm1::Compiler lister;
        std::map<std::string, std::string> byDeclared;
        for (const auto& [file, source] : sources)
        {
            std::string declared;
            as3::avm1::ClassMembers members;
            if (!lister.classMembers(source, declared, members, file + ".as") || declared.empty())
                continue;
            byDeclared[declared] = source;
            compileOptions.classes.push_back(declared);
            compileOptions.members[declared] = members;
        }
        // A superclass named short, in the class's own package.
        for (auto& [name, members] : compileOptions.members)
        {
            const size_t dot = name.find_last_of('.');
            if (members.superName.find('.') == std::string::npos && dot != std::string::npos &&
                compileOptions.members.count(name.substr(0, dot + 1) + members.superName))
                members.superName = name.substr(0, dot + 1) + members.superName;
        }
        std::map<std::string, as3::avm1::ClassCode> compiled;
        for (const auto& [name, source] : byDeclared)
        {
            as3::avm1::Compiler compiler(compileOptions);
            as3::avm1::ClassCode code;
            if (!compiler.compileClass(source, code, name + ".as"))
            {
                tell("class " + name, compiler.errors());
                continue;
            }
            compiled[name] = std::move(code);
        }
        // A class after what it needs.
        std::set<std::string> done;
        std::function<void(const std::string&, int)> emit = [&](const std::string& name, int depth) {
            const auto found = compiled.find(name);
            if (found == compiled.end() || done.count(name) || depth > 64)
                return;
            done.insert(name);
            for (const std::string& need : found->second.needs)
                emit(need, depth + 1);
            swf::Sprite empty;
            const int id = writer.defineSprite(empty);
            writer.exportAssets({{id, "__Packages." + name}});
            writer.doInitAction(id, found->second.actions);
        };
        for (const auto& [name, code] : compiled)
            emit(name, 0);
    }

    // ---- Characters

    /// Whether \a symbol looks the same on every frame, so art with it in
    /// need not be drawn again while its keyframe lasts.
    bool staticSymbol(const Symbol* symbol, int depth = 0)
    {
        if (!symbol)
            return true;
        const auto known = staticSymbols.find(symbol);
        if (known != staticSymbols.end())
            return known->second;
        bool still = depth < 32;
        for (const Timeline* timeline : symbol->timelines)
        {
            if (!still)
                break;
            if (timelineLength(timeline) > 1)
                still = false;
            for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
                for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                    for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                        if (still && !staticElement(element, depth + 1))
                            still = false;
        }
        staticSymbols[symbol] = still;
        return still;
    }

    bool staticElement(const Element* element, int depth)
    {
        if (!element)
            return true;
        if (element->elementType() == Element::Type::SymbolInstance)
            return staticSymbol(static_cast<const SymbolInstance*>(element)->symbol, depth);
        if (element->elementType() == Element::Type::Group)
            for (const Element* member : static_cast<const Group*>(element)->members)
                if (!staticElement(member, depth))
                    return false;
        return true;
    }

    /// Whether \a layer shows the same on \a frame as on the frame before.
    bool sameAsBefore(const Layer& layer, int frame)
    {
        if (frame <= 0 || animatedArt)
            return false;
        const Frame* key = keyframeAt(layer, frame);
        if (!key || key != keyframeAt(layer, frame - 1) || key->tweenType != TweenType::None ||
            !key->otherTweenType.empty() || !attributeOf(key->extraAttributes, "parentLayerIndex").empty())
            return false;
        for (const Element* element : key->elements)
        {
            if (!element)
                continue;
            // Movie clips and buttons play on their own.
            if (element->elementType() == Element::Type::SymbolInstance &&
                static_cast<const SymbolInstance*>(element)->symbolType != SymbolType::Graphic)
                continue;
            if (!staticElement(element, 0))
                return false;
        }
        return true;
    }

    /// The components on \a layer's frame \a frame added to its pieces, as
    /// movie clips.
    void addComponents(const Layer& layer, int frame, std::vector<Piece>& pieces)
    {
        const Frame* key = components.empty() ? nullptr : keyframeAt(layer, frame);
        for (size_t i = 0; key && i < key->elements.size(); ++i)
        {
            const Element* element = key->elements[i];
            if (!element || element->elementType() != Element::Type::Raw)
                continue;
            const auto* raw = static_cast<const RawElement*>(element);
            const auto found = components.find(raw);
            if (found == components.end())
                continue;
            std::unique_ptr<SymbolInstance>& instance = componentInstances[raw];
            if (!instance)
            {
                const auto symbol = document.symbolList ? document.symbolList->symbolMap.find(found->second.symbol)
                                                        : std::map<std::string, Symbol*>::const_iterator();
                if (!document.symbolList || symbol == document.symbolList->symbolMap.end() || !symbol->second)
                {
                    problem("component " + found->second.name + ": its symbol " + found->second.symbol +
                        " is not in the library, left out");
                    continue;
                }
                instance = std::make_unique<SymbolInstance>(raw->parent);
                instance->symbol = symbol->second;
                instance->libraryItemName = symbol->second->name;
                instance->symbolType = SymbolType::MovieClip;
                instance->name = found->second.name;
            }
            Piece piece;
            piece.element = static_cast<int>(i);
            piece.instance = instance.get();
            piece.matrix = found->second.matrix;
            pieces.push_back(piece);
        }
    }

    /// What \a layer shows on \a frame of its timeline: what the drawer
    /// draws, a shape tween's shape as its morph shape (with \a morphs),
    /// the components, and the graphics with clips in them opened into what
    /// they show on the frame they are at.
    std::vector<Piece> layerPieces(const Layer& layer, int frame, bool morphs, int depth = 0)
    {
        std::vector<Piece> pieces = drawer.layerFrame(layer, frame, writer);
        if (morphs)
            morphPieces(layer, frame, pieces);
        addComponents(layer, frame, pieces);
        if (std::none_of(pieces.begin(), pieces.end(), [](const Piece& piece) { return piece.graphicFrame >= 0; }))
            return pieces;
        std::vector<Piece> out;
        for (Piece& piece : pieces)
        {
            const Symbol* symbol = piece.instance ? piece.instance->symbol : nullptr;
            if (piece.graphicFrame < 0 || !symbol || symbol->timelines.empty() || !symbol->timelines.front())
            {
                if (piece.graphicFrame < 0)
                    out.push_back(std::move(piece));
                continue;
            }
            if (writing.count(symbol) || depth > 32)
            {
                problem(symbol->name + " is inside itself: left out there");
                continue;
            }
            writing.insert(symbol);
            // Its layers bottom first, each piece placed by the graphic and
            // colored by it after its own.
            const Timeline& timeline = *symbol->timelines.front();
            const swf::ColorTransform color = then(colorTransform(piece.color), piece.within);
            const int count = static_cast<int>(timeline.layers.size());
            for (int i = count - 1; i >= 0; --i)
            {
                const Layer* inner = timeline.layers[static_cast<size_t>(i)];
                if (!inner || !usable(timeline, *inner) || piece.graphicFrame >= timeline::layerEnd(*inner))
                    continue;
                for (Piece& part : layerPieces(*inner, piece.graphicFrame, morphs, depth + 1))
                {
                    std::vector<int> path = piece.path;
                    path.push_back(count - 1 - i);
                    path.push_back(part.element);
                    path.insert(path.end(), part.path.begin(), part.path.end());
                    part.path = std::move(path);
                    part.element = piece.element;
                    part.matrix = times(piece.matrix, part.matrix);
                    part.within = then(part.within, color);
                    out.push_back(std::move(part));
                }
            }
            writing.erase(symbol);
        }
        return out;
    }

    /// A shape tween's in-between on \a layer's \a frame as its morph shape,
    /// in place of the shape drawn, when the tween's fills and lines can be
    /// a morph shape's.
    void morphPieces(const Layer& layer, int frame, std::vector<Piece>& pieces)
    {
        if (layer.layerType == Layer::Type::Mask)
            return;
        const Frame* key = keyframeAt(layer, frame);
        if (!key || key->tweenType != TweenType::Shape || frame <= key->index)
            return;
        const Frame* next = nullptr;
        for (const Frame* f : layer.frames)
            if (f && f->index == key->index + std::max(1, key->duration))
                next = f;
        const Shape* from = nullptr;
        const Shape* to = nullptr;
        if (!next || !tweenShapes(*key, *next, from, to))
            return;
        const int id = morphFor(*key, *next, *from, *to);
        if (!id)
            return;
        const auto at = std::find(key->elements.begin(), key->elements.end(), static_cast<const Element*>(from));
        if (at == key->elements.end())
            return;
        Piece morph;
        morph.element = static_cast<int>(at - key->elements.begin());
        morph.morph = id;
        const double progress = std::clamp(static_cast<double>(frame - key->index) / std::max(1, key->duration), 0.0, 1.0);
        const double t = easeProgress(progress, key->acceleration,
            key->hasCustomEase ? key->customEase : std::vector<Point>(), key->easeMethod);
        morph.ratio = static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 65535.0));
        // The shape's place among the frame's pieces, its drawing gone.
        auto first = std::find_if(pieces.begin(), pieces.end(),
            [&](const Piece& piece) { return piece.element >= morph.element; });
        const size_t place = static_cast<size_t>(first - pieces.begin());
        pieces.erase(std::remove_if(pieces.begin(), pieces.end(),
                         [&](const Piece& piece) { return piece.element == morph.element; }),
            pieces.end());
        pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(std::min(place, pieces.size())), morph);
    }

    /// The morph shape of the shape tween from \a key to \a next, written
    /// the first time; 0 when its fills or lines cannot be a morph shape's.
    int morphFor(const Frame& key, const Frame& next, const Shape& from, const Shape& to)
    {
        const auto known = morphs.find(&key);
        if (known != morphs.end())
            return known->second;
        int& id = morphs[&key];
        ensureMorph(const_cast<Frame&>(key), next);
        if (!key.morphShape)
            return id = 0;
        // The fills and lines by the first keyframe's indices, each with
        // the last keyframe's of the same index (its own when that has
        // none), numbered from 1 as the morph shape has them. A tween's
        // styles are drawn in the first shape's space, as Phoenix blends
        // them.
        std::vector<swf::MorphFill> fills;
        std::vector<swf::MorphLine> lines;
        std::map<int, int> fillAt, lineAt;
        for (const auto& [index, style] : from.fillsMap)
        {
            const FillStyle* end = to.getFillStyleByIndex(index);
            swf::MorphFill fill;
            if (!morphFill(style, from.transform, fill.start))
                return id = 0;
            // Unlike (another kind, or gradients of other stops): the first
            // keyframe's all the way.
            if (!end || !morphFill(end, from.transform, fill.end) || fill.end.type != fill.start.type ||
                fill.end.stops.size() != fill.start.stops.size())
                fill.end = fill.start;
            fills.push_back(fill);
            fillAt[index] = static_cast<int>(fills.size());
        }
        for (const auto& [index, style] : from.strokesMap)
        {
            const StrokeStyle* end = to.getStrokeStyleByIndex(index);
            swf::MorphLine line;
            if (!morphLine(style, from.transform, line.startWidth, line.startColor) ||
                !morphLine(end ? end : style, from.transform, line.endWidth, line.endColor))
                return id = 0;
            lines.push_back(line);
            lineAt[index] = static_cast<int>(lines.size());
        }
        const auto indexIn = [](const std::map<int, int>& table, int index) {
            const auto found = table.find(index);
            return found == table.end() ? 0 : found->second;
        };
        bool styled = false;
        for (const MorphSegment* segment : key.morphShape->segments)
            styled = styled ||
                (segment && (segment->fillIndex1 || segment->fillIndex2 || segment->strokeIndex1 || segment->strokeIndex2));
        const auto point = [](const Transform& t, const Point& p) {
            return std::make_pair(t.m11 * p.x + t.m21 * p.y + t.tx, t.m12 * p.x + t.m22 * p.y + t.ty);
        };
        std::vector<swf::MorphPath> paths;
        for (const MorphSegment* segment : key.morphShape->segments)
        {
            if (!segment)
                continue;
            swf::MorphPath path;
            if (styled)
            {
                // As the file's edges number their sides: index 1 is fill 0.
                path.fill0 = indexIn(fillAt, segment->fillIndex1);
                path.fill1 = indexIn(fillAt, segment->fillIndex2);
                path.line = indexIn(lineAt, segment->strokeIndex1 > 0 ? segment->strokeIndex1 : segment->strokeIndex2);
            }
            else
            {
                path.fill1 = fills.empty() ? 0 : 1;
                path.line = lines.empty() ? 0 : 1;
            }
            const auto add = [&](swf::Segment::Kind kind, const Point& a, const Point& b, const Point& ca,
                                 const Point& cb) {
                const auto [ax, ay] = point(from.transform, a);
                const auto [bx, by] = point(to.transform, b);
                const auto [cax, cay] = point(from.transform, ca);
                const auto [cbx, cby] = point(to.transform, cb);
                path.start.push_back({kind, ax, ay, cax, cay});
                path.end.push_back({kind, bx, by, cbx, cby});
            };
            add(swf::Segment::Kind::Move, segment->startPointA, segment->startPointB, Point(), Point());
            for (const MorphCurves* curve : segment->curves)
                if (curve)
                    add(swf::Segment::Kind::Quad, curve->anchorPointA, curve->anchorPointB, curve->controlPointA,
                        curve->controlPointB);
            if (path.start.size() > 1 && (path.fill0 || path.fill1 || path.line))
                paths.push_back(std::move(path));
        }
        return id = writer.defineMorphShape(fills, lines, paths);
    }

    /// Art of several shapes, or of one faded or blended: a clip of them.
    int artSprite(const std::vector<Piece::Shape>& shapes)
    {
        std::string key;
        for (const Piece::Shape& shape : shapes)
            key += std::to_string(shape.id) + "/" + std::to_string(std::lround(shape.alpha * 1000.0)) + "/" +
                std::to_string(static_cast<int>(shape.blend)) + ";";
        const auto found = artSprites.find(key);
        if (found != artSprites.end())
            return found->second;
        swf::Sprite sprite;
        int depth = 0;
        for (const Piece::Shape& shape : shapes)
        {
            swf::Placement p;
            p.depth = ++depth;
            p.character = shape.id;
            if (shape.alpha < 0.999)
            {
                p.hasColor = true;
                p.color.alpha = std::clamp(shape.alpha, 0.0, 1.0);
            }
            if (shape.blend != swf::Writer::Blend::Normal)
            {
                p.hasBlend = true;
                p.blend = static_cast<int>(shape.blend);
            }
            sprite.place(p);
        }
        sprite.showFrame();
        const int id = writer.defineSprite(sprite);
        artSprites[key] = id;
        return id;
    }

    int fontFor(const TextRun* run)
    {
        std::string face = run && !run->face.empty() ? run->face : std::string("_sans");
        // A face as written may carry its style: "Arial-BoldMT".
        const bool bold = run && (attributeOf(run->extraAttributes, "bold") == "true" ||
                                     face.find("Bold") != std::string::npos);
        const bool italic = run && (attributeOf(run->extraAttributes, "italic") == "true" ||
                                       face.find("Italic") != std::string::npos);
        const size_t dash = face.find('-');
        if (dash != std::string::npos && dash > 0)
            face = face.substr(0, dash);
        if (face.size() > 2 && face.compare(face.size() - 2, 2, "MT") == 0)
            face = face.substr(0, face.size() - 2);
        const std::string key = face + (bold ? "|b" : "") + (italic ? "|i" : "");
        const auto found = fonts.find(key);
        if (found != fonts.end())
            return found->second;
        const int id = writer.defineFont(face, bold, italic);
        fonts[key] = id;
        return id;
    }

    int fieldFor(const StaticText& text)
    {
        const auto found = fields.find(&text);
        if (found != fields.end())
            return found->second;
        swf::EditText field;
        // A field's box as the file has it, with the gutter players keep.
        field.left = text.left - 2.0;
        field.top = text.top - 2.0;
        field.right = text.left + std::max(1.0, text.width) + 2.0;
        field.bottom = text.top + std::max(1.0, text.height) + 2.0;
        const TextRun* run = text.runs.empty() ? nullptr : &text.runs.front();
        field.font = fontFor(run);
        field.size = run && run->size > 0.0 ? run->size : 12.0;
        if (run)
        {
            field.color = swf::Color{run->fillColor[0], run->fillColor[1], run->fillColor[2], run->fillColor[3]};
            field.align = run->alignment == TextRun::Alignment::Right ? 1
                : run->alignment == TextRun::Alignment::Center      ? 2
                : run->alignment == TextRun::Alignment::Justify     ? 3
                                                                    : 0;
            if (run->lineHeight > 0.0)
                field.leading = std::max(0.0, run->lineHeight - field.size);
        }
        for (const TextRun& part : text.runs)
            field.text += part.text;
        std::replace(field.text.begin(), field.text.end(), '\n', '\r');
        field.variable = attributeOf(text.extraAttributes, "variableName");
        const std::string lines = text.lineType;
        field.multiline = lines == "multiline" || lines == "multiline no wrap";
        field.wordWrap = lines == "multiline";
        field.password = lines == "password";
        field.readOnly = text.textType != StaticText::TextType::Input;
        field.noSelect = attributeOf(text.extraAttributes, "isSelectable") == "false";
        field.border = attributeOf(text.extraAttributes, "border") == "true";
        field.html = attributeOf(text.extraAttributes, "renderAsHTML") == "true";
        field.maxLength = std::atoi(attributeOf(text.extraAttributes, "maxCharacters").c_str());
        field.autoSize = text.autoExpand;
        const int id = writer.defineEditText(field);
        fields[&text] = id;
        return id;
    }

    /// An instance's on() and onClipEvent() handlers as a movie clip's clip
    /// actions.
    const std::vector<uint8_t>& clipActionsFor(const SymbolInstance& instance)
    {
        const std::string script = scripts::instanceScript(instance);
        const auto found = clipActionsByScript.find(script);
        if (found != clipActionsByScript.end())
            return found->second;
        std::vector<uint8_t>& out = clipActionsByScript[script];
        if (script.empty())
            return out;
        as3::avm1::Compiler compiler(compileOptions);
        const std::vector<as3::avm1::Handler> handlers =
            compiler.compileHandlers(script, instance.name.empty() ? "movie clip" : instance.name);
        tell("movie clip " + (instance.name.empty() ? instance.libraryItemName : instance.name), compiler.errors());
        static const std::pair<const char*, uint32_t> events[] = {{"load", swf::ClipEvent::Load},
            {"enterFrame", swf::ClipEvent::EnterFrame}, {"unload", swf::ClipEvent::Unload},
            {"mouseMove", swf::ClipEvent::MouseMove}, {"mouseDown", swf::ClipEvent::MouseDown},
            {"mouseUp", swf::ClipEvent::MouseUp}, {"keyDown", swf::ClipEvent::KeyDown},
            {"keyUp", swf::ClipEvent::KeyUp}, {"data", swf::ClipEvent::Data},
            {"initialize", swf::ClipEvent::Initialize}, {"construct", swf::ClipEvent::Construct},
            {"press", swf::ClipEvent::Press}, {"release", swf::ClipEvent::Release},
            {"releaseOutside", swf::ClipEvent::ReleaseOutside}, {"rollOver", swf::ClipEvent::RollOver},
            {"rollOut", swf::ClipEvent::RollOut}, {"dragOver", swf::ClipEvent::DragOver},
            {"dragOut", swf::ClipEvent::DragOut}};
        std::vector<swf::ClipAction> actions;
        for (const as3::avm1::Handler& handler : handlers)
        {
            swf::ClipAction action;
            action.actions = handler.actions;
            for (const std::string& event : handler.events)
            {
                if (event.rfind("keyPress", 0) == 0)
                {
                    // One key a record.
                    swf::ClipAction key;
                    key.events = swf::ClipEvent::KeyPress;
                    key.key = keyCode(event);
                    key.actions = handler.actions;
                    actions.push_back(key);
                    continue;
                }
                bool known = false;
                for (const auto& [name, flag] : events)
                    if (event == name)
                    {
                        action.events |= flag;
                        known = true;
                    }
                if (!known)
                    problem("movie clip handler for " + event + ": not an event movie clips have");
            }
            if (action.events)
                actions.push_back(action);
        }
        out = swf::clipActions(actions);
        return out;
    }

    /// A movie clip symbol's sprite, written the first time.
    int spriteFor(const Symbol* symbol)
    {
        if (!symbol || symbol->timelines.empty())
            return 0;
        const auto found = sprites.find(symbol);
        if (found != sprites.end())
            return found->second;
        if (writing.count(symbol))
        {
            problem(symbol->name + " is inside itself: left out there");
            return 0;
        }
        writing.insert(symbol);
        swf::Sprite sprite;
        Out out;
        out.sprite = &sprite;
        std::vector<const Timeline*> own{symbol->timelines.front()};
        writeTimeline(own, out, false, symbol, std::function<bool(int)>());
        const int id = writer.defineSprite(sprite);
        writing.erase(symbol);
        sprites[symbol] = id;
        // A clip with 9-slice guides scales by them.
        if (symbol->scale9 && symbol->symbolType == SymbolType::MovieClip)
            writer.defineScalingGrid(id, symbol->scaleGridLeft, symbol->scaleGridTop, symbol->scaleGridRight,
                symbol->scaleGridBottom);
        const auto init = initActions.find(symbol);
        if (init != initActions.end())
            writer.doInitAction(id, init->second);
        return id;
    }

    /// A button symbol, with the handlers \a script gives it, written the
    /// first time.
    int buttonFor(const Symbol* symbol, const std::string& script, const std::string& where)
    {
        if (!symbol || symbol->timelines.empty())
            return 0;
        const auto key = std::make_pair(symbol, script);
        const auto found = buttons.find(key);
        if (found != buttons.end())
            return found->second;
        if (writing.count(symbol))
        {
            problem(symbol->name + " is inside itself: left out there");
            return 0;
        }
        writing.insert(symbol);
        const Timeline* timeline = symbol->timelines.front();
        // Its up, over, down and hit frames, each layer's pieces at depths
        // as a timeline has them.
        std::vector<std::vector<std::vector<Piece>>> captured(timeline->layers.size(),
            std::vector<std::vector<Piece>>(4));
        bool masked = false;
        for (size_t i = 0; i < timeline->layers.size(); ++i)
        {
            const Layer* layer = timeline->layers[i];
            if (!layer || !usable(*timeline, *layer))
                continue;
            masked = masked || layer->layerType == Layer::Type::Mask;
            for (int state = 0; state < 4; ++state)
                captured[i][static_cast<size_t>(state)] = layerPieces(*layer, state, false);
        }
        std::vector<Band> bands = plan(*timeline, captured, masked);
        std::vector<swf::ButtonRecord> records;
        const auto add = [&records](const swf::ButtonRecord& record) {
            // The same in another state: one record for both.
            for (swf::ButtonRecord& other : records)
                if (other.character == record.character && other.depth == record.depth &&
                    sameMatrix(other.matrix, record.matrix) && sameColor(other.color, record.color) &&
                    filtersKey(other.filters) == filtersKey(record.filters) && other.blend == record.blend)
                {
                    other.states |= record.states;
                    return;
                }
            records.push_back(record);
        };
        if (masked)
        {
            // A button's characters cannot clip one another: each state
            // with a mask is a clip of its own, its masks clipping in it.
            for (int state = 0; state < 4; ++state)
            {
                const std::map<int, Shown> now = shownOn(*timeline, captured, bands, state, false, -1);
                if (now.empty())
                    continue;
                std::ostringstream key;
                key.precision(6);
                for (const auto& [depth, shown] : now)
                {
                    const swf::Matrix& m = shown.matrix;
                    const swf::ColorTransform& c = shown.color;
                    key << depth << ':' << shown.character << ',' << m.a << ',' << m.b << ',' << m.c << ',' << m.d
                        << ',' << m.tx << ',' << m.ty << ',' << c.red << ',' << c.green << ',' << c.blue << ','
                        << c.alpha << ',' << c.redAdd << ',' << c.greenAdd << ',' << c.blueAdd << ',' << c.alphaAdd
                        << ',' << shown.ratio << ',' << shown.name << ',' << shown.clipDepth << ',' << shown.blend
                        << ',' << shown.filterKey << ';';
                }
                int& sprite = stateSprites[key.str()];
                if (!sprite)
                {
                    swf::Sprite state;
                    for (const auto& [depth, shown] : now)
                        state.place(placementOf(depth, shown));
                    state.showFrame();
                    sprite = writer.defineSprite(state);
                }
                swf::ButtonRecord record;
                record.states = 1 << state;
                record.character = sprite;
                record.depth = 1;
                add(record);
            }
        }
        else
            for (int state = 0; state < 4; ++state)
                for (const auto& [depth, shown] : shownOn(*timeline, captured, bands, state, false, -1))
                {
                    swf::ButtonRecord record;
                    record.states = 1 << state;
                    record.character = shown.character;
                    record.depth = depth;
                    record.matrix = shown.matrix;
                    record.color = shown.color;
                    record.filters = shown.filters;
                    record.blend = shown.blend != 1 ? shown.blend : 0;
                    add(record);
                }
        // Without a hit frame the up frame is where the pointer finds it.
        if (std::none_of(records.begin(), records.end(), [](const swf::ButtonRecord& r) { return r.states & 8; }))
            for (swf::ButtonRecord& record : records)
                if (record.states & 1)
                    record.states |= 8;
        std::vector<swf::ButtonAction> actions;
        if (!script.empty())
        {
            as3::avm1::Compiler compiler(compileOptions);
            const std::vector<as3::avm1::Handler> handlers = compiler.compileHandlers(script, where);
            tell(where, compiler.errors());
            for (const as3::avm1::Handler& handler : handlers)
            {
                if (handler.clipEvent)
                {
                    problem(where + ": onClipEvent() is for movie clips, not buttons");
                    continue;
                }
                swf::ButtonAction action;
                action.actions = handler.actions;
                for (const std::string& event : handler.events)
                {
                    if (event == "press")
                        action.conditions |= swf::ButtonCondition::OverUpToOverDown;
                    else if (event == "release")
                        action.conditions |= swf::ButtonCondition::OverDownToOverUp;
                    else if (event == "releaseOutside")
                        action.conditions |= swf::ButtonCondition::OutDownToIdle;
                    else if (event == "rollOver")
                        action.conditions |= swf::ButtonCondition::IdleToOverUp;
                    else if (event == "rollOut")
                        action.conditions |= swf::ButtonCondition::OverUpToIdle;
                    else if (event == "dragOver")
                        action.conditions |= swf::ButtonCondition::OutDownToOverDown;
                    else if (event == "dragOut")
                        action.conditions |= swf::ButtonCondition::OverDownToOutDown;
                    else if (event.rfind("keyPress", 0) == 0)
                        action.conditions |= swf::ButtonCondition::key(keyCode(event));
                    else
                        problem(where + ": on(" + event + ") is not an event buttons have");
                }
                if (action.conditions)
                    actions.push_back(action);
            }
        }
        const bool menu = attributeOf(symbol->extraAttributes, "trackAsMenu") == "true";
        const int id = writer.defineButton(records, actions, menu);
        // The sounds of its states' frames: Up's as the pointer leaves, Over's
        // as it comes, Down's as it presses and Hit's as it lets go.
        if (options.eventSound)
        {
            std::vector<std::pair<int, swf::SoundInfo>> sounds(4);
            bool any = false;
            for (int state = 0; state < 4; ++state)
                for (const Layer* layer : timeline->layers)
                {
                    const Frame* frame = layer && layer->visible && layer->layerType != Layer::Type::Guide &&
                            layer->layerType != Layer::Type::Folder
                        ? keyframeAt(*layer, state) : nullptr;
                    std::pair<int, swf::SoundInfo>& sound = sounds[static_cast<size_t>(state)];
                    if (sound.first || !frame || frame->index != state)
                        continue;
                    if (options.eventSound(*frame, sound.first, sound.second) && sound.first)
                        any = true;
                    else
                        sound.first = 0;
                }
            if (any)
                writer.defineButtonSound(id, sounds);
        }
        writing.erase(symbol);
        buttons[key] = id;
        return id;
    }

    /// What \a piece shows as: false for nothing. \a main scales it to the
    /// movie; \a scene is the main timeline's scene it is on, -1 elsewhere.
    bool showOf(const Piece& piece, bool main, int scene, Shown& shown)
    {
        shown = Shown();
        if (piece.instance)
        {
            const SymbolInstance& instance = *piece.instance;
            const Symbol* symbol = instance.symbol;
            if (!symbol)
                return false;
            if (instance.symbolType == SymbolType::Button)
            {
                const std::string where = "button " + (instance.name.empty() ? instance.libraryItemName : instance.name);
                // A button's handlers run on the timeline it is on.
                std::string script = scripts::instanceScript(instance);
                const as3::avm1::Options saved = compileOptions;
                if (main)
                    compileOptions = optionsFor(true, scene);
                shown.character = buttonFor(symbol, script, where);
                compileOptions = saved;
            }
            else
            {
                shown.character = spriteFor(symbol);
                shown.clipActions = clipActionsFor(instance);
            }
            if (!shown.character)
                return false;
            shown.identity = symbol;
            shown.name = instance.name;
            shown.matrix = piece.matrix;
            shown.color = then(colorTransform(piece.color), piece.within);
            shown.filters = filters(piece.filters);
            shown.blend = blendMode(piece.blendMode);
        }
        else if (piece.text)
        {
            shown.character = fieldFor(*piece.text);
            shown.identity = piece.text;
            shown.name = fieldName(*piece.text);
            shown.matrix = piece.matrix;
            shown.color = piece.within;
        }
        else if (piece.morph)
        {
            // A shape tween's morph shape, in the timeline's space or the
            // graphic's it is in.
            shown.character = piece.morph;
            shown.ratio = std::clamp(piece.ratio, 0, 65535);
            if (!piece.path.empty())
                shown.matrix = piece.matrix;
            shown.color = piece.within;
        }
        else
        {
            if (piece.shapes.empty())
                return false;
            const Piece::Shape& only = piece.shapes.front();
            shown.character = piece.shapes.size() == 1 && only.alpha >= 0.999 && only.blend == swf::Writer::Blend::Normal
                ? only.id : artSprite(piece.shapes);
            shown.identity = nullptr;
            // Art is drawn in the timeline's space; in a graphic opened
            // into it, placed by the graphic.
            if (!piece.path.empty())
                shown.matrix = piece.matrix;
            shown.color = piece.within;
        }
        if (main && (options.scaleX != 1.0 || options.scaleY != 1.0))
        {
            swf::Matrix& m = shown.matrix;
            m.a *= options.scaleX;
            m.c *= options.scaleX;
            m.tx *= options.scaleX;
            m.b *= options.scaleY;
            m.d *= options.scaleY;
            m.ty *= options.scaleY;
        }
        shown.filterKey = filtersKey(shown.filters);
        return shown.character > 0;
    }

    /// Whether a layer goes in: guides and folders do not, nor hidden ones
    /// unless the settings include them.
    bool usable(const Timeline& timeline, const Layer& layer) const
    {
        return layer.layerType != Layer::Type::Guide && layer.layerType != Layer::Type::Folder &&
            (options.hiddenLayers || drawer.shows(timeline, layer));
    }

    /// The depths of \a timeline's layers, bottom first: a band for each
    /// layer as many deep as the most parts it shows on a frame, a mask's
    /// one depth just under the layers it masks.
    std::vector<Band> plan(const Timeline& timeline, const std::vector<std::vector<std::vector<Piece>>>& captured,
        bool masks)
    {
        std::vector<Band> bands;
        int next = 1;
        std::set<int> masksPlaced;
        const int count = static_cast<int>(timeline.layers.size());
        for (int i = count - 1; i >= 0; --i)
        {
            const Layer* layer = timeline.layers[static_cast<size_t>(i)];
            if (!layer || captured[static_cast<size_t>(i)].empty())
                continue;
            if (layer->layerType == Layer::Type::Mask)
                continue;
            // A masked layer: its mask first, once.
            const int maskIndex = layer->layerType == Layer::Type::Masked ? layer->parentLayerIndex : -1;
            if (masks && maskIndex >= 0 && maskIndex < count && !masksPlaced.count(maskIndex) &&
                timeline.layers[static_cast<size_t>(maskIndex)] &&
                timeline.layers[static_cast<size_t>(maskIndex)]->layerType == Layer::Type::Mask &&
                !captured[static_cast<size_t>(maskIndex)].empty())
            {
                masksPlaced.insert(maskIndex);
                Band mask;
                mask.layer = maskIndex;
                mask.mask = true;
                mask.base = next++;
                mask.size = 1;
                bands.push_back(mask);
            }
            Band band;
            band.layer = i;
            band.base = next;
            // Each element's parts, as many as it has at most, and those of
            // each place in the graphics opened into it.
            std::map<std::pair<int, std::vector<int>>, int> most;
            for (const std::vector<Piece>& frame : captured[static_cast<size_t>(i)])
            {
                std::map<std::pair<int, std::vector<int>>, int> parts;
                for (const Piece& piece : frame)
                    ++parts[{piece.element, piece.path}];
                for (const auto& [where, n] : parts)
                    most[where] = std::max(most[where], n);
            }
            int offset = 0;
            for (const auto& [where, n] : most)
                for (int part = 0; part < n; ++part)
                    band.slots[std::make_tuple(where.first, where.second, part)] = offset++;
            band.size = offset;
            next += offset;
            bands.push_back(band);
        }
        // Each mask clips up to the top of the layers it masks.
        for (Band& mask : bands)
        {
            if (!mask.mask)
                continue;
            mask.clipTop = mask.base;
            for (const Band& band : bands)
            {
                const Layer* layer = band.mask ? nullptr : timeline.layers[static_cast<size_t>(band.layer)];
                if (layer && layer->layerType == Layer::Type::Masked && layer->parentLayerIndex == mask.layer)
                    mask.clipTop = std::max(mask.clipTop, band.base + band.size - 1);
            }
        }
        return bands;
    }

    /// The pieces of a mask layer's frame as one clip, to clip with.
    int maskSprite(const std::vector<Piece>& pieces, std::string& key)
    {
        std::vector<Shown> shown;
        key.clear();
        for (const Piece& piece : pieces)
        {
            Shown one;
            if (!showOf(piece, false, -1, one))
                continue;
            shown.push_back(one);
            const swf::Matrix& m = one.matrix;
            std::ostringstream text;
            text.precision(6);
            text << one.character << ':' << m.a << ',' << m.b << ',' << m.c << ',' << m.d << ',' << m.tx << ','
                 << m.ty << ';';
            key += text.str();
        }
        if (shown.empty())
            return 0;
        if (shown.size() == 1 && !pieces.front().instance && !pieces.front().text)
            return shown.front().character;
        const auto found = artSprites.find("mask " + key);
        if (found != artSprites.end())
            return found->second;
        swf::Sprite sprite;
        int depth = 0;
        for (const Shown& one : shown)
        {
            swf::Placement p;
            p.depth = ++depth;
            p.character = one.character;
            p.hasMatrix = true;
            p.matrix = one.matrix;
            sprite.place(p);
        }
        sprite.showFrame();
        const int id = writer.defineSprite(sprite);
        artSprites["mask " + key] = id;
        return id;
    }

    /// What shows at each depth of \a timeline on \a frame, from its
    /// layers' pieces \a captured and their depths \a bands. \a main and
    /// \a scene as for showOf.
    std::map<int, Shown> shownOn(const Timeline& timeline, const std::vector<std::vector<std::vector<Piece>>>& captured,
        const std::vector<Band>& bands, int frame, bool main, int scene)
    {
        std::map<int, Shown> now;
        for (const Band& band : bands)
        {
            const std::vector<std::vector<Piece>>& layer = captured[static_cast<size_t>(band.layer)];
            if (frame < 0 || static_cast<size_t>(frame) >= layer.size())
                continue;
            const std::vector<Piece>& pieces = layer[static_cast<size_t>(frame)];
            if (band.mask)
            {
                Shown mask;
                std::string key;
                mask.character = maskSprite(pieces, key);
                if (!mask.character)
                    continue;
                mask.identity = timeline.layers[static_cast<size_t>(band.layer)];
                mask.clipDepth = band.clipTop;
                if (main && (options.scaleX != 1.0 || options.scaleY != 1.0))
                {
                    mask.matrix.a = options.scaleX;
                    mask.matrix.d = options.scaleY;
                }
                now[band.base] = mask;
                continue;
            }
            std::map<std::pair<int, std::vector<int>>, int> parts;
            for (const Piece& piece : pieces)
            {
                const int part = parts[{piece.element, piece.path}]++;
                const auto slot = band.slots.find(std::make_tuple(piece.element, piece.path, part));
                Shown shown;
                if (slot == band.slots.end() || !showOf(piece, main, scene, shown))
                    continue;
                now[band.base + slot->second] = shown;
            }
        }
        return now;
    }

    /// The sounds that start on \a timeline's \a frame (its keyframes'
    /// event, start and stop sounds), started in \a out.
    void startSounds(const Timeline& timeline, int frame, Out& out)
    {
        if (!options.eventSound)
            return;
        for (const Layer* layer : timeline.layers)
        {
            // Hidden layers are silent, as playback has them, but for the
            // main timeline's when the settings include hidden layers.
            const bool scene = dynamic_cast<const Document*>(timeline.parent) != nullptr;
            const bool heard = layer && (layer->visible || (options.hiddenLayers && scene));
            if (!heard || layer->layerType == Layer::Type::Guide || layer->layerType == Layer::Type::Folder)
                continue;
            const Frame* key = keyframeAt(*layer, frame);
            int id = 0;
            swf::SoundInfo info;
            if (key && key->index == frame && options.eventSound(*key, id, info) && id)
                out.startSound(id, info);
        }
    }

    /// Writes \a timelines one after another into \a out: the main
    /// timeline's scenes, or a symbol's timeline. \a beforeShow is called
    /// with each frame before it is shown.
    int writeTimeline(const std::vector<const Timeline*>& timelines, Out& out, bool main, const Symbol* symbol,
        const std::function<bool(int)>& beforeShow)
    {
        int written = 0;
        std::map<int, Shown> previous;
        std::map<const Frame*, std::vector<uint8_t>> scriptCache;
        for (size_t t = 0; t < timelines.size(); ++t)
        {
            const Timeline* timeline = timelines[t];
            if (!timeline)
                continue;
            const int scene = main ? static_cast<int>(t) : -1;
            const int length = timelineLength(timeline);
            // Every layer's pieces on every frame, then their depths.
            std::vector<std::vector<std::vector<Piece>>> captured(timeline->layers.size());
            for (size_t i = 0; i < timeline->layers.size(); ++i)
            {
                const Layer* layer = timeline->layers[i];
                if (!layer || !usable(*timeline, *layer))
                    continue;
                // A mask with nothing under it masks nothing and shows not.
                captured[i].resize(static_cast<size_t>(length));
                for (int frame = 0; frame < length; ++frame)
                {
                    if (sameAsBefore(*layer, frame))
                        captured[i][static_cast<size_t>(frame)] = captured[i][static_cast<size_t>(frame) - 1];
                    else if (frame < timeline::layerEnd(*layer))
                        captured[i][static_cast<size_t>(frame)] =
                            layerPieces(*layer, frame, layer->layerType != Layer::Type::Mask);
                }
            }
            const std::vector<Band> bands = plan(*timeline, captured, true);

            for (int frame = 0; frame < length; ++frame)
            {
                const std::map<int, Shown> now = shownOn(*timeline, captured, bands, frame, main, scene);
                // What went, or became something else, is taken off; then
                // what is new is placed and what moved is moved.
                for (const auto& [depth, was] : previous)
                {
                    const auto still = now.find(depth);
                    if (still == now.end() || still->second.character != was.character ||
                        still->second.name != was.name || still->second.clipDepth != was.clipDepth)
                        out.remove(depth);
                }
                for (const auto& [depth, shown] : now)
                {
                    const auto was = previous.find(depth);
                    const bool fresh = was == previous.end() || was->second.character != shown.character ||
                        was->second.name != shown.name || was->second.clipDepth != shown.clipDepth;
                    if (fresh)
                    {
                        out.place(placementOf(depth, shown));
                        continue;
                    }
                    const Shown& before = was->second;
                    swf::Placement p;
                    p.depth = depth;
                    p.move = true;
                    p.hasMatrix = !sameMatrix(before.matrix, shown.matrix);
                    p.matrix = shown.matrix;
                    p.hasColor = !sameColor(before.color, shown.color);
                    p.color = shown.color;
                    p.hasFilters = before.filterKey != shown.filterKey;
                    p.filters = shown.filters;
                    p.hasBlend = before.blend != shown.blend;
                    p.blend = shown.blend;
                    p.ratio = before.ratio != shown.ratio ? shown.ratio : -1;
                    if (p.hasMatrix || p.hasColor || p.hasFilters || p.hasBlend || p.ratio >= 0)
                        out.place(p);
                }
                previous = now;

                // Its label: the top layer's that starts here.
                for (const Layer* layer : timeline->layers)
                {
                    const Frame* key = layer ? keyframeAt(*layer, frame) : nullptr;
                    if (key && key->index == frame && !key->name.empty() && key->labelType != "comment")
                    {
                        out.label(key->name, key->labelType == "anchor");
                        break;
                    }
                }
                // Its scripts, the top layer's first.
                for (const Layer* layer : timeline->layers)
                {
                    // A hidden layer left out of the movie takes its scripts
                    // with it.
                    if (!layer || layer->layerType == Layer::Type::Folder ||
                        (!options.hiddenLayers && !drawer.shows(*timeline, *layer)))
                        continue;
                    const Frame* key = keyframeAt(*layer, frame);
                    if (!key || key->index != frame || !key->actionScript || key->actionScript->code.empty())
                        continue;
                    if (actionScript3)
                        continue;
                    auto cached = scriptCache.find(key);
                    if (cached == scriptCache.end())
                    {
                        const std::string where = (symbol ? symbol->name : timeline->name) + ", layer " + layer->name +
                            ", frame " + std::to_string(frame + 1);
                        as3::avm1::Compiler compiler(optionsFor(main, scene));
                        as3::avm1::FrameScript compiled = compiler.compileFrameScript(key->actionScript->code, where);
                        tell(where, compiler.errors());
                        if (!compiled.initActions.empty())
                        {
                            if (symbol && frame == 0)
                            {
                                std::vector<uint8_t>& init = initActions[symbol];
                                // Several layers' #initclip run one after
                                // another: each ends with ActionEnd.
                                if (!init.empty())
                                    init.pop_back();
                                init.insert(init.end(), compiled.initActions.begin(), compiled.initActions.end());
                            }
                            else
                                problem(where + ": #initclip is for a movie clip's first frame, left out");
                        }
                        cached = scriptCache.emplace(key, std::move(compiled.actions)).first;
                    }
                    out.doAction(cached->second);
                }
                // Its sounds, where a player starts them: on this timeline,
                // so a clip stopped starts none.
                startSounds(*timeline, frame, out);
                if (beforeShow && !beforeShow(written))
                    return written;
                out.show();
                ++written;
            }
            // The next scene starts with nothing of this one's.
            if (t + 1 < timelines.size())
            {
                for (const auto& [depth, was] : previous)
                    out.remove(depth);
                previous.clear();
            }
        }
        return written;
    }
};

Movie::Movie(const Document& document, swf::Writer& writer, Drawer& drawer, const Options& options)
    : _state(std::make_unique<State>(document, writer, drawer, options))
{
    int start = 0;
    for (const Timeline* scene : document.timelines)
    {
        if (!scene)
            continue;
        _state->scenes.push_back(scene);
        _state->sceneStarts.push_back(start);
        _state->compileOptions.scenes[scene->name] = start;
        start += timelineLength(scene);
    }
    // As Test Movie decides: what the publish settings say, unless the
    // scripts are plainly of the other kind (a file saved again with new
    // settings).
    const int version = play::actionScriptVersion(document);
    _state->actionScript3 = version == 3 ? !scriptsLookLike(document, false) || scriptsLookLike(document, true)
                                         : version == 0 && scriptsLookLike(document, true);
    for (const features::Use& use : features::used(document))
        if (use.feature && (std::strcmp(use.feature->id, "shape-operators") == 0 ||
                               std::strcmp(use.feature->id, "text-animators") == 0))
            _state->animatedArt = true;
}

Movie::~Movie() = default;

int Movie::sceneStart(const Timeline* scene) const
{
    for (size_t i = 0; i < _state->scenes.size(); ++i)
        if (_state->scenes[i] == scene)
            return _state->sceneStarts[i];
    return 0;
}

const std::vector<std::string>& Movie::problems() const
{
    return _state->problems;
}

int Movie::write(const std::function<bool(int frame)>& beforeShow)
{
    State& state = *_state;
    const Document& document = state.document;
    if (state.actionScript3)
    {
        if (state.options.actionScript3)
        {
            state.writer.setActionScript3(true);
            state.components = components(document);
        }
        else if (hasScripts(document))
            state.problem("ActionScript 3 is not compiled into SWF movies yet: the movie goes out without its scripts");
    }
    else
        state.writeClasses();

    // The symbols scripts reach by linkage, and those an ActionScript 3
    // movie gives classes, first: attachMovie finds them from the start.
    std::vector<std::pair<int, std::string>> exported;
    std::map<const Symbol*, int> classed;
    if (document.symbolList)
        for (const auto& [name, symbol] : document.symbolList->symbolMap)
        {
            if (!symbol || symbol->symbolType == SymbolType::Graphic)
                continue;
            const std::string identifier = attributeOf(symbol->extraAttributes, "linkageIdentifier");
            const std::string className = attributeOf(symbol->extraAttributes, "linkageClassName");
            const bool exports = attributeOf(symbol->extraAttributes, "linkageExportForAS") == "true";
            if (state.actionScript3)
            {
                bool scripted = !className.empty();
                for (const Timeline* timeline : symbol->timelines)
                    for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
                        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                        {
                            scripted = scripted || (frame && frame->actionScript && !frame->actionScript->code.empty());
                            // Components' parameters are set by its class.
                            for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                                scripted = scripted ||
                                    (element && element->elementType() == Element::Type::Raw &&
                                        state.components.count(static_cast<const RawElement*>(element)));
                        }
                if (!scripted)
                    continue;
                // A button with a class: the button as it is without handlers.
                if (symbol->symbolType == SymbolType::MovieClip)
                {
                    if (const int id = state.spriteFor(symbol))
                        classed[symbol] = id;
                }
                else if (!className.empty())
                    if (const int id = state.buttonFor(symbol, std::string(), "button " + symbol->name))
                        classed[symbol] = id;
                continue;
            }
            if (!exports || identifier.empty() || symbol->symbolType != SymbolType::MovieClip)
                continue;
            const int id = state.spriteFor(symbol);
            if (!id)
                continue;
            exported.emplace_back(id, identifier);
            if (!className.empty())
            {
                // Its class, for attachMovie and the clips placed of it.
                as3::avm1::Compiler compiler(state.compileOptions);
                const std::vector<uint8_t> registration = compiler.compileScript(
                    "Object.registerClass(\"" + escaped(identifier) + "\", " + className + ");", symbol->name);
                state.tell(symbol->name, compiler.errors());
                state.writer.doInitAction(id, registration);
            }
        }
    state.writer.exportAssets(exported);
    if (state.actionScript3 && state.options.actionScript3)
        state.options.actionScript3(state.writer, classed);

    Out out;
    out.writer = &state.writer;
    return state.writeTimeline(state.scenes, out, true, nullptr, beforeShow);
}

} // namespace swfmovie
} // namespace fla
