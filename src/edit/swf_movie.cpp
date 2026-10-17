#include "swf_movie.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../play/script/class_path.h"
#include "phoenix_features.h"
#include "script_edit.h"
#include "timeline_edit.h"

#include "avm1/avm1.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <set>
#include <sstream>

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

} // namespace

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
        "motion-blur", "templates", "master-controllers", "3d-rotation", "parenting-options"};
    for (const features::Use& use : features::used(document))
    {
        if (!use.feature)
            continue;
        for (const char* id : blocking)
            if (std::strcmp(use.feature->id, id) == 0)
                return std::string(use.feature->title) + (use.where.empty() ? std::string() : " (" + use.where + ")");
    }
    return std::string();
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
            // As Phoenix adjusts color: saturation about the luminance axis,
            // the hue turned about the gray axis, then contrast about the
            // middle and the brightness added.
            s.type = swf::Filter::Type::ColorMatrix;
            const double brightness = std::clamp(f.brightness, -100.0, 100.0) * 2.55;
            const double contrast = 1.0 + std::clamp(f.contrast, -100.0, 100.0) / 100.0;
            const double saturation = 1.0 + std::clamp(f.saturation, -100.0, 100.0) / 100.0;
            const double hue = std::clamp(f.hue, -180.0, 180.0) * kPi / 180.0;
            constexpr double luminance[3] = {0.3086, 0.6094, 0.0820};
            double m[3][3], h[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    m[r][c] = luminance[c] * (1.0 - saturation) + (r == c ? saturation : 0.0);
            const double cosH = std::cos(hue), sinH = std::sin(hue);
            const double w = 1.0 / 3.0, s3 = std::sqrt(1.0 / 3.0);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    h[r][c] = w + cosH * ((r == c ? 1.0 : 0.0) - w) +
                        sinH * s3 * (((c - r + 3) % 3 == 1) ? 1.0 : ((c - r + 3) % 3 == 2) ? -1.0 : 0.0);
            for (int r = 0; r < 3; ++r)
            {
                for (int c = 0; c < 3; ++c)
                    s.matrix[r * 5 + c] =
                        static_cast<float>(contrast * (h[r][0] * m[0][c] + h[r][1] * m[1][c] + h[r][2] * m[2][c]));
                s.matrix[r * 5 + 3] = 0.0f;
                s.matrix[r * 5 + 4] = static_cast<float>(128.0 * (1.0 - contrast) + brightness);
            }
            for (int c = 0; c < 5; ++c)
                s.matrix[15 + c] = c == 3 ? 1.0f : 0.0f;
            break;
        }
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
};

/// Where a timeline's tags go: the movie's main timeline, or a sprite's.
struct Out
{
    swf::Writer* writer = nullptr;
    swf::Sprite* sprite = nullptr;

    void place(const swf::Placement& p) { sprite ? sprite->place(p) : writer->place(p); }
    void remove(int depth) { sprite ? sprite->remove(depth) : writer->remove(depth); }
    void label(const std::string& name, bool anchor) { sprite ? sprite->frameLabel(name, anchor) : writer->frameLabel(name, anchor); }
    void doAction(const std::vector<uint8_t>& actions) { sprite ? sprite->doAction(actions) : writer->doAction(actions); }
    void show() { sprite ? sprite->showFrame() : writer->showFrame(); }
};

/// A layer's band of depths, and for a mask the masked layers' top.
struct Band
{
    int layer = -1;
    bool mask = false;
    int base = 0;
    /// Where each (element, part) goes, counted from base.
    std::map<std::pair<int, int>, int> slots;
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
        for (const Timeline* scene : scenes)
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
            const std::optional<std::string> source = options.findClass(name);
            if (!source)
                continue;
            sources[name] = *source;
            look(*source);
        }
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
        for (size_t i = 0; i < timeline->layers.size(); ++i)
        {
            const Layer* layer = timeline->layers[i];
            if (!layer || !usable(*timeline, *layer))
                continue;
            if (layer->layerType == Layer::Type::Mask)
            {
                problem(symbol->name + ": a button's mask is left out, what it masks drawn whole");
                continue;
            }
            for (int state = 0; state < 4; ++state)
                captured[i][static_cast<size_t>(state)] = drawer.layerFrame(*layer, state, writer);
        }
        std::vector<Band> bands = plan(*timeline, captured, false);
        std::vector<swf::ButtonRecord> records;
        for (const Band& band : bands)
        {
            for (int state = 0; state < 4; ++state)
            {
                std::map<int, int> parts;
                for (const Piece& piece : captured[static_cast<size_t>(band.layer)][static_cast<size_t>(state)])
                {
                    const int part = parts[piece.element]++;
                    Shown shown;
                    if (!showOf(piece, false, -1, shown))
                        continue;
                    const auto slot = band.slots.find({piece.element, part});
                    if (slot == band.slots.end())
                        continue;
                    swf::ButtonRecord record;
                    record.states = 1 << state;
                    record.character = shown.character;
                    record.depth = band.base + slot->second;
                    record.matrix = shown.matrix;
                    record.color = shown.color;
                    record.filters = shown.filters;
                    record.blend = shown.blend != 1 ? shown.blend : 0;
                    // The same in another state: one record for both.
                    bool merged = false;
                    for (swf::ButtonRecord& other : records)
                        if (other.character == record.character && other.depth == record.depth &&
                            sameMatrix(other.matrix, record.matrix) && sameColor(other.color, record.color) &&
                            filtersKey(other.filters) == filtersKey(record.filters) && other.blend == record.blend)
                        {
                            other.states |= record.states;
                            merged = true;
                            break;
                        }
                    if (!merged)
                        records.push_back(record);
                }
            }
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
            shown.color = colorTransform(piece.color);
            shown.filters = filters(piece.filters);
            shown.blend = blendMode(piece.blendMode);
        }
        else if (piece.text)
        {
            shown.character = fieldFor(*piece.text);
            shown.identity = piece.text;
            shown.name = piece.text->instanceName;
            shown.matrix = piece.matrix;
        }
        else
        {
            if (piece.shapes.empty())
                return false;
            const Piece::Shape& only = piece.shapes.front();
            shown.character = piece.shapes.size() == 1 && only.alpha >= 0.999 && only.blend == swf::Writer::Blend::Normal
                ? only.id : artSprite(piece.shapes);
            shown.identity = nullptr;
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

    /// Whether a layer goes in: guides and folders do not, nor hidden ones.
    bool usable(const Timeline& timeline, const Layer& layer) const
    {
        return layer.layerType != Layer::Type::Guide && layer.layerType != Layer::Type::Folder &&
            drawer.shows(timeline, layer);
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
            // Each element's parts, as many as it has at most.
            std::map<int, int> most;
            for (const std::vector<Piece>& frame : captured[static_cast<size_t>(i)])
            {
                std::map<int, int> parts;
                for (const Piece& piece : frame)
                    ++parts[piece.element];
                for (const auto& [element, n] : parts)
                    most[element] = std::max(most[element], n);
            }
            int offset = 0;
            for (const auto& [element, n] : most)
                for (int part = 0; part < n; ++part)
                    band.slots[{element, part}] = offset++;
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
                        captured[i][static_cast<size_t>(frame)] = drawer.layerFrame(*layer, frame, writer);
                }
            }
            const std::vector<Band> bands = plan(*timeline, captured, true);

            for (int frame = 0; frame < length; ++frame)
            {
                std::map<int, Shown> now;
                for (const Band& band : bands)
                {
                    const std::vector<Piece>& pieces = captured[static_cast<size_t>(band.layer)][static_cast<size_t>(frame)];
                    if (band.mask)
                    {
                        Shown mask;
                        std::string key;
                        mask.character = maskSprite(pieces, key);
                        if (!mask.character)
                            continue;
                        mask.identity = timeline->layers[static_cast<size_t>(band.layer)];
                        mask.clipDepth = band.clipTop;
                        if (main && (options.scaleX != 1.0 || options.scaleY != 1.0))
                        {
                            mask.matrix.a = options.scaleX;
                            mask.matrix.d = options.scaleY;
                        }
                        now[band.base] = mask;
                        continue;
                    }
                    std::map<int, int> parts;
                    for (const Piece& piece : pieces)
                    {
                        const int part = parts[piece.element]++;
                        const auto slot = band.slots.find({piece.element, part});
                        Shown shown;
                        if (slot == band.slots.end() || !showOf(piece, main, scene, shown))
                            continue;
                        now[band.base + slot->second] = shown;
                    }
                }
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
                    swf::Placement p;
                    p.depth = depth;
                    if (fresh)
                    {
                        p.character = shown.character;
                        p.hasMatrix = true;
                        p.matrix = shown.matrix;
                        p.hasColor = !shown.color.identity();
                        p.color = shown.color;
                        p.name = shown.name;
                        p.clipDepth = shown.clipDepth;
                        p.hasFilters = !shown.filters.empty();
                        p.filters = shown.filters;
                        p.hasBlend = shown.blend != 1;
                        p.blend = shown.blend;
                        p.clipActions = shown.clipActions;
                        out.place(p);
                        continue;
                    }
                    const Shown& before = was->second;
                    p.move = true;
                    p.hasMatrix = !sameMatrix(before.matrix, shown.matrix);
                    p.matrix = shown.matrix;
                    p.hasColor = !sameColor(before.color, shown.color);
                    p.color = shown.color;
                    p.hasFilters = before.filterKey != shown.filterKey;
                    p.filters = shown.filters;
                    p.hasBlend = before.blend != shown.blend;
                    p.blend = shown.blend;
                    if (p.hasMatrix || p.hasColor || p.hasFilters || p.hasBlend)
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
                    if (!layer || layer->layerType == Layer::Type::Folder)
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
            state.writer.setActionScript3(true);
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
                            scripted = scripted || (frame && frame->actionScript && !frame->actionScript->code.empty());
                if (scripted && symbol->symbolType == SymbolType::MovieClip)
                    if (const int id = state.spriteFor(symbol))
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
