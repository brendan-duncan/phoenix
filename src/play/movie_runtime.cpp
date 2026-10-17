#include "movie_runtime.h"
#include "avm1_player.h"

#include "../data/action_script.h"
#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/raw_element.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../edit/script_edit.h"
#include "canvas/scripts.h"
#include "display/backend.h"
#include "display/bitmap_filter.h"
#include "display/display_object.h"
#include "display/stage.h"
#include "display/text_field.h"
#include "script/bindings.h"

#include "abc/abc_domain.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/compiler.h"
#include "bytecode/vm.h"
#include "parser/parser.h"
#include "runtime/value.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fla {

namespace play {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// How many frames a timeline runs to.
int lengthOf(const Timeline* timeline)
{
    int length = 1;
    if (!timeline)
        return length;
    for (const Layer* layer : timeline->layers)
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            if (frame)
                length = std::max(length, frame->index + std::max(1, frame->duration));
    return length;
}

/// The keyframe of \a layer showing at \a index, or null.
const Frame* keyframeAt(const Layer& layer, int index)
{
    for (const Frame* frame : layer.frames)
        if (frame && frame->index <= index && index < frame->index + std::max(1, frame->duration))
            return frame;
    return nullptr;
}

/// A layer's position from the top, for keys of unnamed instances.
int layerIndex(const Timeline& timeline, const Layer* layer)
{
    for (size_t i = 0; i < timeline.layers.size(); ++i)
        if (timeline.layers[i] == layer)
            return static_cast<int>(i);
    return -1;
}

/// A layer whose content plays: not a guide (nor a folder, which holds
/// none).
bool plays(const Layer& layer)
{
    return layer.layerType != Layer::Type::Guide && layer.layerType != Layer::Type::Folder;
}

/// The key of an instance among its parent's: its name, or where it is.
std::string keyOf(const Timeline& timeline, const Layer& layer, const Frame& frame, const SymbolInstance& instance,
    const Element* element = nullptr)
{
    if (!instance.name.empty())
        return instance.name;
    const Element* placed = element ? element : &instance;
    const auto at = std::find(frame.elements.begin(), frame.elements.end(), placed);
    std::ostringstream key;
    key << "#" << layerIndex(timeline, &layer) << ":" << (at - frame.elements.begin()) << ":"
        << (instance.symbol ? instance.symbol->name : instance.libraryItemName);
    return key.str();
}

double scaleOf(double a, double b)
{
    return std::sqrt(a * a + b * b);
}

/// \a transform applied to (\a x, \a y).
void apply(const Transform& transform, double& x, double& y)
{
    const double px = x;
    x = transform.m11 * px + transform.m21 * y + transform.tx;
    y = transform.m12 * px + transform.m22 * y + transform.ty;
}

/// A text field's instance name: as written, or the name the file gives it.
const std::string& nameOf(const StaticText& field)
{
    if (!field.instanceName.empty())
        return field.instanceName;
    for (const auto& [key, value] : field.extraAttributes)
        if (key == "name")
            return value;
    return field.instanceName;
}

/// The text field named \a name on \a timeline's frame \a frame: a dynamic
/// or input one; null when there is none.
const StaticText* fieldNamed(const Timeline* timeline, int frame, const std::string& name)
{
    if (!timeline || name.empty())
        return nullptr;
    for (const Layer* layer : timeline->layers)
    {
        const Frame* key = layer ? keyframeAt(*layer, frame) : nullptr;
        for (const Element* element : key ? key->elements : std::vector<Element*>())
        {
            const auto* text = dynamic_cast<const StaticText*>(element);
            if (text && text->textType != StaticText::TextType::Static && nameOf(*text) == name)
                return text;
        }
    }
    return nullptr;
}

/// A text field's text as drawn.
std::string drawnText(const StaticText& field)
{
    std::string text;
    for (const TextRun& run : field.runs)
        text += run.text;
    return text;
}

/// \a html with its tags taken out, as a field that does not render HTML
/// shows it.
std::string withoutTags(const std::string& html)
{
    std::string text;
    bool inTag = false;
    for (const char c : html)
    {
        if (c == '<')
            inTag = true;
        else if (c == '>')
            inTag = false;
        else if (!inTag)
            text += c;
    }
    return text;
}

/// The value of attribute \a key among \a attributes, or empty.
std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& key)
{
    for (const auto& [name, value] : attributes)
        if (name == key)
            return value;
    return std::string();
}

/// The events a clip hears from the mouse in ActionScript 3.
display::Matrix toMatrix(const Transform& t)
{
    return display::Matrix(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

Transform toTransform(const display::Matrix& m)
{
    return Transform(m.a, m.b, m.c, m.d, m.tx, m.ty);
}

/// An instance's color effect as a color transform: a tint, a brightness,
/// or multipliers and offsets.
display::ColorTransform toColor(const ColorTransform& color)
{
    display::ColorTransform out;
    if (color.tintMultiplier != 0.0)
    {
        const double t = color.tintMultiplier;
        out.redMultiplier = out.greenMultiplier = out.blueMultiplier = 1.0 - t;
        out.redOffset = color.tintColor[0] * t;
        out.greenOffset = color.tintColor[1] * t;
        out.blueOffset = color.tintColor[2] * t;
    }
    else if (color.brightness != 0.0)
    {
        const double b = color.brightness;
        out.redMultiplier = out.greenMultiplier = out.blueMultiplier = 1.0 - std::fabs(b);
        if (b > 0.0)
            out.redOffset = out.greenOffset = out.blueOffset = 255.0 * b;
    }
    else
    {
        out.redMultiplier = color.redMultiplier;
        out.greenMultiplier = color.greenMultiplier;
        out.blueMultiplier = color.blueMultiplier;
        out.redOffset = color.redOffset;
        out.greenOffset = color.greenOffset;
        out.blueOffset = color.blueOffset;
    }
    out.alphaMultiplier = color.alphaMultiplier;
    out.alphaOffset = color.alphaOffset;
    return out;
}

uint32_t rgbOf(const uint8_t color[4])
{
    return (uint32_t(color[0]) << 16) | (uint32_t(color[1]) << 8) | color[2];
}

/// An instance's filters as the display engine has them, as Flash Player
/// makes them of a movie's: Adjust Color becomes a color matrix.
std::vector<display::BitmapFilter> toFilters(const std::vector<Filter>& filters)
{
    using display::BitmapFilter;
    std::vector<BitmapFilter> out;
    for (const Filter& f : filters)
    {
        if (!f.enabled)
            continue;
        switch (f.type)
        {
        case Filter::Type::Blur:
            out.push_back(BitmapFilter::blur(f.blurX, f.blurY, f.quality));
            break;
        case Filter::Type::DropShadow:
            out.push_back(BitmapFilter::dropShadow(f.distance, f.angle, rgbOf(f.color), f.color[3] / 255.0, f.blurX,
                f.blurY, f.strength, f.quality, f.inner, f.knockout, f.hideObject));
            break;
        case Filter::Type::Glow:
            out.push_back(BitmapFilter::glow(rgbOf(f.color), f.color[3] / 255.0, f.blurX, f.blurY, f.strength, f.quality,
                f.inner, f.knockout));
            break;
        case Filter::Type::Bevel:
            out.push_back(BitmapFilter::bevel(f.distance, f.angle, rgbOf(f.highlightColor), f.highlightColor[3] / 255.0,
                rgbOf(f.shadowColor), f.shadowColor[3] / 255.0, f.blurX, f.blurY, f.strength, f.quality,
                f.bevelType.empty() ? std::string("inner") : f.bevelType, f.knockout));
            break;
        case Filter::Type::GradientGlow:
        case Filter::Type::GradientBevel:
        {
            BitmapFilter g = BitmapFilter::bevel(f.distance, f.angle, 0xffffff, 1.0, 0, 1.0, f.blurX, f.blurY, f.strength,
                f.quality, f.bevelType.empty() ? std::string(f.type == Filter::Type::GradientGlow ? "outer" : "inner")
                                               : f.bevelType,
                f.knockout);
            g.type = f.type == Filter::Type::GradientGlow ? BitmapFilter::Type::GradientGlow
                                                          : BitmapFilter::Type::GradientBevel;
            for (const GradientEntry& entry : f.entries)
            {
                display::Gradient::Stop stop;
                stop.color = rgbOf(entry.color);
                stop.alpha = entry.color[3] / 255.0;
                stop.ratio = static_cast<int>(std::lround(std::max(0.0, std::min(1.0, entry.ratio)) * 255.0));
                g.stops.push_back(stop);
            }
            out.push_back(g);
            break;
        }
        case Filter::Type::AdjustColor:
            out.push_back(BitmapFilter::adjustColor(f.brightness, f.contrast, f.saturation, f.hue));
            break;
        }
    }
    return out;
}

/// \a source with an empty statement after each import outside a package:
/// libas3 keeps only the first of a run of imports that open a script, so
/// each is ended on its own (the lines stay where they were).
std::string separateImports(const std::string& source)
{
    std::string out;
    out.reserve(source.size() + 64);
    size_t i = 0;
    const auto nameChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; };
    while (i < source.size())
    {
        const bool atWord = source.compare(i, 6, "import") == 0 && (i == 0 || !nameChar(source[i - 1])) &&
            i + 6 < source.size() && std::isspace(static_cast<unsigned char>(source[i + 6]));
        if (!atWord)
        {
            out += source[i++];
            continue;
        }
        // import a.b.C; or import a.b.*; as written.
        size_t end = i + 6;
        while (end < source.size() && (source[end] == ' ' || source[end] == '\t'))
            ++end;
        const size_t nameStart = end;
        while (end < source.size() && (nameChar(source[end]) || source[end] == '.' || source[end] == '*'))
            ++end;
        if (end == nameStart)
        {
            out += source[i++];
            continue;
        }
        size_t after = end;
        while (after < source.size() && (source[after] == ' ' || source[after] == '\t'))
            ++after;
        out.append(source, i, after - i);
        if (after < source.size() && source[after] == ';')
        {
            out += "; ;";
            i = after + 1;
        }
        else
        {
            out += ";;";
            i = after;
        }
    }
    return out;
}

bool isMouseEvent(const std::string& type)
{
    return type == "click" || type == "mouseDown" || type == "mouseUp" || type == "mouseOver" ||
        type == "mouseOut" || type == "rollOver" || type == "rollOut" || type == "doubleClick";
}

/// The key an AS2 keyPress handler names: "<Left>" and the like, or one
/// character; true when \a code (or \a character) is that key.
bool keyPressMatches(const std::string& named, int code, int character)
{
    static const std::map<std::string, int> keys = {{"<Left>", 37}, {"<Right>", 39}, {"<Up>", 38},
        {"<Down>", 40}, {"<Enter>", 13}, {"<Space>", 32}, {"<Home>", 36}, {"<End>", 35}, {"<Insert>", 45},
        {"<Delete>", 46}, {"<Backspace>", 8}, {"<Escape>", 27}, {"<Tab>", 9}, {"<PageUp>", 33},
        {"<PageDown>", 34}};
    const auto found = keys.find(named);
    if (found != keys.end())
        return found->second == code;
    return named.size() == 1 && character != 0 && static_cast<unsigned char>(named[0]) == character;
}

} // namespace

bool Clip::listensFor(const std::string& type) const
{
    const auto found = listeners.find(type);
    return found != listeners.end() && !found->second.empty();
}

bool Clip::wantsMouse() const
{
    if (button)
        return true;
    // A clip with on() handlers for the pointer, as a button is.
    for (const char* event : {"press", "release", "releaseOutside", "rollOver", "rollOut", "dragOver", "dragOut"})
        if (clipEvents.count(event))
            return true;
    for (const auto& [type, list] : listeners)
        if (!list.empty() && isMouseEvent(type))
            return true;
    if (display)
        for (const char* type : {"click", "mouseDown", "mouseUp", "mouseOver", "mouseOut", "rollOver", "rollOut",
                 "doubleClick"})
            if (display->hasEventListener(type))
                return true;
    return false;
}

std::string instanceScript(const SymbolInstance& instance)
{
    return fla::scripts::instanceScript(instance);
}

std::map<std::string, std::string> classFiles(const Document& document, const std::string& folder,
    std::string& documentClass)
{
    documentClass.clear();
    // The publish settings name the document class and the source paths.
    // Paths are UTF-8, as Phoenix keeps them.
    const std::filesystem::path base = std::filesystem::u8path(folder);
    std::vector<std::filesystem::path> roots{base};
    for (const auto& [path, bytes] : document.extraFiles)
    {
        if (path != "PublishSettings.xml")
            continue;
        const std::vector<uint8_t>& data = bytes.get();
        tinyxml2::XMLDocument xml;
        if (xml.Parse(reinterpret_cast<const char*>(data.data()), data.size()) != tinyxml2::XML_SUCCESS)
            break;
        std::function<const tinyxml2::XMLElement*(const tinyxml2::XMLElement*, const char*)> find =
            [&](const tinyxml2::XMLElement* at, const char* name) -> const tinyxml2::XMLElement* {
            for (const tinyxml2::XMLElement* child = at ? at->FirstChildElement() : nullptr; child;
                 child = child->NextSiblingElement())
            {
                if (std::strcmp(child->Name(), name) == 0)
                    return child;
                if (const tinyxml2::XMLElement* deeper = find(child, name))
                    return deeper;
            }
            return nullptr;
        };
        const tinyxml2::XMLElement* top = xml.RootElement();
        if (const tinyxml2::XMLElement* named = find(top, "DocumentClass"); named && named->GetText())
            documentClass = named->GetText();
        if (const tinyxml2::XMLElement* paths = find(top, "AS3PackagePaths"); paths && paths->GetText())
        {
            std::stringstream list(paths->GetText());
            std::string entry;
            while (std::getline(list, entry, ';'))
            {
                if (entry.empty() || entry == "." || entry.find('$') != std::string::npos)
                    continue;
                const std::filesystem::path root = std::filesystem::u8path(entry);
                roots.push_back(root.is_absolute() ? root : base / root);
            }
        }
        break;
    }

    // The classes wanted: the document's, and those symbols are linked to.
    std::vector<std::string> wanted;
    if (!documentClass.empty())
        wanted.push_back(documentClass);
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
        {
            for (const auto& [key, value] : symbol->extraAttributes)
                if (key == "linkageClassName" && !value.empty())
                    wanted.push_back(value);
        }

    std::map<std::string, std::string> sources;
    for (const std::string& name : wanted)
    {
        std::string relative = name;
        std::replace(relative.begin(), relative.end(), '.', '/');
        relative += ".as";
        for (const std::filesystem::path& root : roots)
        {
            const std::filesystem::path file = root / std::filesystem::u8path(relative);
            std::ifstream in(file, std::ios::binary);
            if (!in)
                continue;
            std::stringstream text;
            text << in.rdbuf();
            // The class goes by its own name, without its package.
            const size_t dot = name.find_last_of('.');
            sources[dot == std::string::npos ? name : name.substr(dot + 1)] = text.str();
            break;
        }
    }
    const size_t dot = documentClass.find_last_of('.');
    if (dot != std::string::npos)
        documentClass = documentClass.substr(dot + 1);
    return sources;
}

bool checkSyntax(const std::string& source, int& line, std::string& message)
{
    line = 0;
    message.clear();
    as3::Parser parser(source);
    parser.parse();
    if (!parser.hasErrors())
        return true;
    // The first of them: "Parse error at line N, column C: what".
    std::string first = parser.getErrors();
    first = first.substr(0, first.find('\n'));
    const std::string at = "at line ";
    const std::size_t place = first.find(at);
    if (place != std::string::npos)
        line = std::atoi(first.c_str() + place + at.size());
    const std::size_t colon = first.find(": ");
    message = colon != std::string::npos ? first.substr(colon + 2) : first;
    return false;
}

Clip* Clip::child(const std::string& wanted) const
{
    for (const auto& clip : children)
        if (clip->key == wanted)
            return clip.get();
    return nullptr;
}

Clip* Clip::named(const std::string& wanted) const
{
    for (const auto& clip : children)
        if (!clip->name.empty() && clip->name == wanted)
            return clip.get();
    return nullptr;
}

Transform Clip::transform(const Transform& placedTransform) const
{
    if (!display)
        return placedTransform;
    return toTransform(display->matrixOver(toMatrix(placedTransform)));
}

bool Clip::shown() const
{
    return !display || display->visible();
}

double Clip::opacity() const
{
    if (!display || !display->alphaScripted())
        return 1.0;
    return std::max(0.0, std::min(1.0, display->alpha()));
}

std::optional<double> Clip::scriptedX() const
{
    return display ? display->scriptedX() : std::nullopt;
}

std::optional<double> Clip::scriptedY() const
{
    return display ? display->scriptedY() : std::nullopt;
}

/// ActionScript 2's _global: what a script sets on it is a global every
/// script sees by its bare name, and what it reads is the VM's globals and
/// definitions (Math, Array...).
class GlobalObject : public as3::HostObject
{
public:
    explicit GlobalObject(as3::VM& vm)
        : _vm(vm)
    {}

    as3::ValuePtr get(const std::string& name) override { return _vm.getGlobal(name); }
    bool set(const std::string& name, as3::ValuePtr value) override
    {
        _vm.setGlobal(name, std::move(value));
        return true;
    }

private:
    as3::VM& _vm;
};

/// A clip as scripts see it: its properties worked out from the clip, its
/// methods bound to it, its children by name.
class ClipObject : public as3::HostObject
{
public:
    ClipObject(MovieRuntime& runtime, Clip& clip)
        : _runtime(runtime)
        , _life(clip.life)
    {}

    // Once the clip is freed, only what scripts set on the object is left.
    as3::ValuePtr get(const std::string& name) override
    {
        Clip* clip = *_life;
        return clip ? _runtime.property(*clip, name) : nullptr;
    }
    bool set(const std::string& name, as3::ValuePtr value) override
    {
        Clip* clip = *_life;
        return clip && _runtime.setProperty(*clip, name, value);
    }
    Clip* clip() const { return *_life; }

private:
    MovieRuntime& _runtime;
    std::shared_ptr<Clip*> _life;
};

Clip::~Clip()
{
    *life = nullptr;
    if (display)
        display->clip = nullptr;
}

MovieRuntime::MovieRuntime(const Timeline& root, double frameRate)
    : MovieRuntime(std::vector<const Timeline*>{&root}, frameRate)
{}

MovieRuntime::MovieRuntime(const std::vector<const Timeline*>& scenes, double frameRate)
    : _frameRate(frameRate > 0.0 ? frameRate : 24.0)
{
    for (const Timeline* scene : scenes)
        if (scene)
            _scenes.push_back(scene);
    start();
}

MovieRuntime::~MovieRuntime()
{
    // The script objects first, while the VM they belong to is about.
    _bindings.reset();
    _canvas.reset();
    _abcDomain.reset();
    _actions.reset();
}

bool MovieRuntime::mouseHidden() const
{
    return _bindings && _bindings->mouseHidden;
}

std::string MovieRuntime::mouseCursor() const
{
    return _bindings ? _bindings->mouseCursor : std::string("auto");
}

bool MovieRuntime::scriptsLookLikeActionScript2() const
{
    // Scripts on instances, and what only ActionScript 1 and 2 write.
    static const char* marks[] = {"setProperty(", "getProperty(", "tellTarget", "ifFrameLoaded", "onClipEvent",
        "_root.", "_parent.", "._visible", "._x", "attachMovie("};
    std::vector<const Timeline*> timelines(_scenes.begin(), _scenes.end());
    for (const Symbol* symbol : _library)
        if (symbol)
            timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                for (const Element* element : frame->elements)
                    if (const auto* instance = dynamic_cast<const SymbolInstance*>(element))
                        if (!instanceScript(*instance).empty())
                            return true;
                if (frame->actionScript)
                    for (const char* mark : marks)
                        if (frame->actionScript->code.find(mark) != std::string::npos)
                            return true;
            }
    return false;
}

bool MovieRuntime::scriptsLookLikeActionScript3() const
{
    // What only ActionScript 3 writes.
    static const char* marks[] = {"import flash.", "import fl.", "addEventListener", ":void", "package ",
        "MouseEvent.", "Event.ENTER_FRAME", "new Sprite(", "new MovieClip("};
    const auto marked = [](const std::string& code) {
        for (const char* mark : marks)
            if (code.find(mark) != std::string::npos)
                return true;
        return false;
    };
    std::vector<const Timeline*> timelines(_scenes.begin(), _scenes.end());
    for (const Symbol* symbol : _library)
        if (symbol)
            timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                if (frame && frame->actionScript && marked(frame->actionScript->code))
                    return true;
    return false;
}

void MovieRuntime::start()
{
    _compiled.clear();
    _buttonHandlers.clear();
    _hovered = _pressed = nullptr;
    _sent.clear();
    _sceneMove = SceneMove();
    _scene = 0;
    _keysDown.clear();
    _lastKey = _lastCharacter = 0;
    _keyListeners.clear();
    _drag = Drag();
    _focus = TextFocus();
    _madeToEnter.clear();
    _offStage.clear();
    _gone.clear();
    _madeCount = 0;
    _started = std::chrono::steady_clock::now();
    _pointerOver.reset();
    _pointerPressed.reset();
    if (stopAllSounds)
        stopAllSounds();
    // The last run's script objects go before the VM they belong to.
    _bindings.reset();
    _canvas.reset();
    _abcDomain.reset();
    _actions.reset();
    _initsRun.clear();
    _vm = std::make_unique<as3::VM>();
    if (!_stage)
        _stage = std::make_shared<display::Stage>();
    _stage->setFrameRate(_frameRate);
    _stage->stopDrag();
    _stage->setFocus(nullptr);
    _stage->removeChildren();
    _stage->removeAllEventListeners();
    _stage->script.reset();
    _root = std::make_unique<Clip>();
    _root->timeline = _scenes.empty() ? nullptr : _scenes.front();
    _root->length = lengthOf(_root->timeline);
    _root->name = "_level0";
    attachDisplay(*_root);
    _stage->addChildAt(_root->display, 0);
    _stage->setRootObject(_root->display.get());
    if (_javaScript)
    {
        // JavaScript: CreateJS's classes over the timelines and the display
        // list, the frames' scripts run by them.
        _as3 = false;
        _canvas = std::make_unique<canvas::Scripts>(*this);
        std::vector<Clip*> entered{_root.get()};
        enter(entered);
        return;
    }
    // A movie whose settings say ActionScript 3 but whose scripts are older
    // (a file saved again with new settings) plays as its scripts are.
    _as3 = _version == 3 ? !scriptsLookLikeActionScript2() || scriptsLookLikeActionScript3()
                         : _version == 0 && (!_classSources.empty() || !_documentClass.empty() ||
                                                scriptsLookLikeActionScript3());
    std::vector<Clip*> entered{_root.get()};
    if (_as3)
    {
        // ActionScript 3: the flash.* classes over the display list, and
        // the main timeline an instance of the document class (or of
        // MovieClip), made with what the first frame places.
        _bindings = std::make_unique<script::Bindings>(*this, *_vm);
        setupActionScript3();
        _bindings->install();
        // A published movie's compiled classes: found before class files,
        // their scripts run as they are first needed.
        if (!_abcFiles.empty())
        {
            // Classes Phoenix has in place of the movie's copies, which
            // need what the movie does not have (the text layout framework
            // of a shared library): defined first, so they are found first.
            for (const char* name : {"fl.text.RuntimeManager"})
            {
                try
                {
                    _vm->findDefinition(name);
                }
                catch (as3::ScriptException& exception)
                {
                    _bindings->report(exception, name);
                }
            }
            _abcDomain = std::make_unique<as3::abc::Domain>(*_vm);
            for (const std::vector<uint8_t>& abc : _abcFiles)
            {
                std::string error;
                if (!_abcDomain->load(abc, error))
                    tell("The movie's compiled ActionScript cannot be read: " + error, nullptr, 0);
            }
        }
        syncChildren(*_root, entered);
        runClass(*_root);
        enter(entered);
        return;
    }
    setupGlobals();
    loadClasses();
    // The document class runs before the first frame's scripts, with what
    // the first frame has placed.
    if (_classes.count(_documentClass))
    {
        syncChildren(*_root, entered);
        runClass(*_root);
    }
    enter(entered);
}

void MovieRuntime::setClasses(const std::map<std::string, std::string>& sources, const std::string& documentClass)
{
    _classSources = sources;
    _documentClass = documentClass;
}

void MovieRuntime::loadClasses()
{
    _classes.clear();
    for (const auto& [name, source] : _classSources)
    {
        as3::Parser parser(source);
        std::unique_ptr<as3::Program> program = parser.parse();
        if (parser.hasErrors())
        {
            const std::string errors = parser.getErrors();
            const std::size_t at = errors.find("at line ");
            tell(name + ".as: " + errors, nullptr, at == std::string::npos ? 0 : std::atoi(errors.c_str() + at + 8));
            continue;
        }
        std::shared_ptr<as3::BytecodeFunction> code;
        try
        {
            as3::Compiler compiler;
            code = compiler.compile(program.get());
        }
        catch (const std::exception& failed)
        {
            tell(name + ".as: " + failed.what(), nullptr, 0);
            continue;
        }
        _vm->execute(code, nullptr);
        report(name + ".as");
        const as3::ValuePtr made = _vm->getGlobal(name);
        if (made && made->isClass())
            _classes[name] = made;
    }
    // A symbol whose class has a file: new makes a clip of it, the class
    // run on it there and then.
    for (const Symbol* symbol : _library)
    {
        const std::string className = attributeOf(symbol->extraAttributes, "linkageClassName");
        const size_t dot = className.find_last_of('.');
        const std::string shortName = dot == std::string::npos ? className : className.substr(dot + 1);
        if (!_classes.count(shortName) || symbol->symbolType == SymbolType::Graphic)
            continue;
        _vm->registerBuiltin(shortName, [this, symbol](const std::vector<as3::ValuePtr>&) {
            Clip* made = makeClip(nullptr, *symbol, std::string());
            runClass(*made);
            return objectFor(*made);
        });
    }
}

void MovieRuntime::runClass(Clip& clip)
{
    if (_as3)
    {
        if (!clip.object && clip.display)
            _bindings->clipObject(clip);
        return;
    }
    if (clip.classed)
        return;
    std::string name;
    if (&clip == _root.get())
        name = _documentClass;
    else if (clip.symbol)
    {
        name = attributeOf(clip.symbol->extraAttributes, "linkageClassName");
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
            name = name.substr(dot + 1);
    }
    const auto found = _classes.find(name);
    if (found == _classes.end())
        return;
    clip.classed = true;
    // Its methods and fields, on it; then its constructor, with it as this.
    const as3::ValuePtr object = objectFor(clip);
    for (const auto& [key, value] : found->second->properties)
        if (key != "__class__")
            object->properties[key] = value;
    const as3::ValuePtr constructor = found->second->getProperty(name);
    if (constructor && constructor->isFunction())
        runFunction(constructor, {}, clip, name + " constructor");
}

as3::ValuePtr MovieRuntime::objectFor(Clip& clip)
{
    if (_as3 && !clip.object && clip.display)
        return _bindings->clipObject(clip);
    if (!clip.object)
        clip.object = as3::Value::makeHostObject(std::make_shared<ClipObject>(*this, clip));
    return clip.object;
}

void MovieRuntime::setupActionScript3()
{
    // What the scripts trace, each value as its toString gives it.
    _vm->defineFunction("trace", [this](as3::VM& vm, const as3::Val&, as3::Args args) {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i)
            line += (i ? " " : "") + vm.toString(args[i]);
        if (trace)
            trace(line);
        return as3::Val();
    });
}

void MovieRuntime::setupGlobals()
{
    using as3::Value;
    using as3::ValuePtr;
    using Args = std::vector<ValuePtr>;
    as3::VM& vm = *_vm;
    const auto current = [this]() -> Clip& { return _current ? *_current : *_root; };
    const auto arg = [](const Args& args, size_t i) { return i < args.size() ? args[i] : Value::makeUndefined(); };

    vm.registerBuiltin("trace", [this](const Args& args) {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i)
            line += (i ? " " : "") + args[i]->toString();
        if (trace)
            trace(line);
        return Value::makeUndefined();
    });
    // The timeline the script runs on.
    vm.registerBuiltin("stop", [current](const Args&) {
        current().playing = false;
        return Value::makeUndefined();
    });
    vm.registerBuiltin("play", [current](const Args&) {
        current().playing = true;
        return Value::makeUndefined();
    });
    const auto gotoFrame = [this, current, arg](bool play) {
        return [this, current, arg, play](const Args& args) {
            Clip& clip = current();
            // gotoAndPlay(scene, frame) names a scene of the movie.
            if (args.size() >= 2)
            {
                const int scene = sceneNamed(arg(args, 0)->toString());
                if (scene >= 0)
                {
                    goToScene(scene, std::max(0, frameIn(_scenes[static_cast<size_t>(scene)], arg(args, 1))), play);
                    return Value::makeUndefined();
                }
            }
            const ValuePtr target = args.size() >= 2 ? arg(args, 1) : arg(args, 0);
            const int frame = frameOf(clip, target);
            if (frame >= 0)
            {
                goTo(clip, frame, play);
                return Value::makeUndefined();
            }
            // On the main timeline, a label may be in another scene.
            if (&clip == _root.get() && target && !target->isNumber())
            {
                for (size_t scene = 0; scene < _scenes.size(); ++scene)
                {
                    const int there = frameIn(_scenes[scene], target);
                    if (there >= 0)
                    {
                        goToScene(static_cast<int>(scene), there, play);
                        return Value::makeUndefined();
                    }
                }
            }
            clip.playing = play;
            return Value::makeUndefined();
        };
    };
    vm.registerBuiltin("gotoAndPlay", gotoFrame(true));
    vm.registerBuiltin("gotoAndStop", gotoFrame(false));
    // The first frame of the next or the scene before, stopped there.
    vm.registerBuiltin("nextScene", [this](const Args&) {
        if (_scene + 1 < sceneCount())
            goToScene(_scene + 1, 0, false);
        return Value::makeUndefined();
    });
    vm.registerBuiltin("prevScene", [this](const Args&) {
        if (_scene > 0)
            goToScene(_scene - 1, 0, false);
        return Value::makeUndefined();
    });
    vm.registerBuiltin("nextFrame", [this, current](const Args&) {
        goTo(current(), std::min(current().frame + 1, current().length - 1), false);
        return Value::makeUndefined();
    });
    vm.registerBuiltin("prevFrame", [this, current](const Args&) {
        goTo(current(), std::max(current().frame - 1, 0), false);
        return Value::makeUndefined();
    });
    // Everything is loaded at once.
    vm.registerBuiltin("ifFrameLoaded", [](const Args&) { return Value::makeBoolean(true); });
    vm.registerBuiltin("getProperty", [this, current, arg](const Args& args) {
        Clip* clip = resolve(current(), arg(args, 0));
        return clip ? property(*clip, arg(args, 1)->toString()) : Value::makeUndefined();
    });
    vm.registerBuiltin("setProperty", [this, current, arg](const Args& args) {
        if (Clip* clip = resolve(current(), arg(args, 0)))
            setProperty(*clip, arg(args, 1)->toString(), arg(args, 2));
        return Value::makeUndefined();
    });
    vm.registerBuiltin("getURL", [this, arg](const Args& args) {
        if (getURL)
            getURL(arg(args, 0)->toString(), args.size() > 1 ? arg(args, 1)->toString() : std::string());
        return Value::makeUndefined();
    });
    vm.registerBuiltin("fscommand", [this, arg](const Args& args) {
        if (trace)
            trace("fscommand(" + arg(args, 0)->toString() + ", " + arg(args, 1)->toString() + ")");
        return Value::makeUndefined();
    });
    // Milliseconds since the movie started.
    vm.registerBuiltin("getTimer", [this](const Args&) {
        return Value::makeNumber(static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _started).count()));
    });
    vm.registerBuiltin("random", [](const Args& args) {
        const int range = args.empty() ? 0 : static_cast<int>(args[0]->toNumber());
        return Value::makeNumber(range > 0 ? std::rand() % range : 0);
    });
    // tellTarget (target) { ... }: the block's bare timeline calls go to
    // the target.
    vm.registerBuiltin("__tellTarget", [this, current, arg](const Args& args) {
        Clip* target = resolve(current(), arg(args, 0));
        if (!target)
            return Value::makeUndefined();
        Clip* was = _current;
        _current = target;
        _vm->call(arg(args, 1), {}, objectFor(*target), objectFor(*target));
        if (_vm->hasError())
            report("tellTarget");
        _current = was;
        return Value::makeUndefined();
    });
    vm.setGlobal("_root", objectFor(*_root));
    vm.setGlobal("_level0", objectFor(*_root));
    if (!_as3)
        vm.setGlobal("_global", as3::Value::makeHostObject(std::make_shared<GlobalObject>(vm)));
    // ActionScript 3 sees the main timeline as the stage too.
    vm.setGlobal("stage", objectFor(*_root));

    // The event types ActionScript 3 names by constant.
    const auto constants = [](std::initializer_list<std::pair<const char*, ValuePtr>> entries) {
        ValuePtr object = Value::makeObject();
        for (const auto& [name, value] : entries)
            object->setProperty(name, value);
        return object;
    };
    const auto text = [](const char* value) { return Value::makeString(value); };
    vm.setGlobal("MouseEvent", constants({{"CLICK", text("click")}, {"DOUBLE_CLICK", text("doubleClick")},
        {"MOUSE_DOWN", text("mouseDown")}, {"MOUSE_UP", text("mouseUp")}, {"MOUSE_OVER", text("mouseOver")},
        {"MOUSE_OUT", text("mouseOut")}, {"MOUSE_MOVE", text("mouseMove")}, {"ROLL_OVER", text("rollOver")},
        {"ROLL_OUT", text("rollOut")}}));
    vm.setGlobal("Event", constants({{"ENTER_FRAME", text("enterFrame")}, {"ADDED_TO_STAGE", text("addedToStage")},
        {"COMPLETE", text("complete")}}));
    vm.setGlobal("KeyboardEvent", constants({{"KEY_DOWN", text("keyDown")}, {"KEY_UP", text("keyUp")}}));
    const auto number = [](double value) { return Value::makeNumber(value); };
    std::initializer_list<std::pair<const char*, ValuePtr>> keyCodes = {{"BACKSPACE", number(8)}, {"TAB", number(9)},
        {"ENTER", number(13)}, {"SHIFT", number(16)}, {"CONTROL", number(17)}, {"ESCAPE", number(27)},
        {"SPACE", number(32)}, {"PGUP", number(33)}, {"PGDN", number(34)}, {"END", number(35)}, {"HOME", number(36)},
        {"LEFT", number(37)}, {"UP", number(38)}, {"RIGHT", number(39)}, {"DOWN", number(40)},
        {"INSERT", number(45)}, {"DELETEKEY", number(46)}, {"DELETE", number(46)}, {"PAGE_UP", number(33)},
        {"PAGE_DOWN", number(34)}};
    vm.setGlobal("Keyboard", constants(keyCodes));
    // AS2's Key: the keys held, the last one pressed, and listeners.
    ValuePtr key = constants(keyCodes);
    key->setProperty("isDown", Value::makeNativeFunction([this, arg](const Args& args) {
        const int code = static_cast<int>(arg(args, 0)->toNumber());
        return Value::makeBoolean(std::find(_keysDown.begin(), _keysDown.end(), code) != _keysDown.end());
    }));
    key->setProperty("getCode", Value::makeNativeFunction([this](const Args&) { return Value::makeNumber(_lastKey); }));
    key->setProperty("getAscii", Value::makeNativeFunction([this](const Args&) {
        return Value::makeNumber(_lastCharacter);
    }));
    key->setProperty("addListener", Value::makeNativeFunction([this, arg](const Args& args) {
        const ValuePtr listener = arg(args, 0);
        if (listener->isObject() && std::find(_keyListeners.begin(), _keyListeners.end(), listener) == _keyListeners.end())
            _keyListeners.push_back(listener);
        return Value::makeUndefined();
    }));
    key->setProperty("removeListener", Value::makeNativeFunction([this, arg](const Args& args) {
        _keyListeners.erase(std::remove(_keyListeners.begin(), _keyListeners.end(), arg(args, 0)), _keyListeners.end());
        return Value::makeUndefined();
    }));
    vm.setGlobal("Key", key);
    // Casts, which change nothing here: MovieClip(root).score.
    for (const char* cast : {"MovieClip", "SimpleButton", "Sprite", "DisplayObject", "DisplayObjectContainer", "TextField"})
        vm.registerBuiltin(cast, [arg](const Args& args) { return arg(args, 0); });

    // Dragging, AS2's way: startDrag(target, lockCenter, left, top, right,
    // bottom) and stopDrag().
    vm.registerBuiltin("startDrag", [this, current, arg](const Args& args) {
        if (Clip* clip = resolve(current(), arg(args, 0)))
        {
            std::vector<double> bounds;
            if (args.size() >= 6)
                for (size_t i = 2; i < 6; ++i)
                    bounds.push_back(args[i]->toNumber());
            startDrag(*clip, arg(args, 1)->toBoolean(), bounds);
        }
        return Value::makeUndefined();
    });
    vm.registerBuiltin("stopDrag", [this](const Args&) {
        stopDrag();
        return Value::makeUndefined();
    });
    vm.registerBuiltin("duplicateMovieClip", [this, current, arg](const Args& args) {
        Clip* clip = resolve(current(), arg(args, 0));
        if (clip && clip->symbol && clip->parent)
        {
            if (Value* object = objectFor(*clip).get())
                return object->getProperty("duplicateMovieClip")->nativeFunc({arg(args, 1), arg(args, 2)});
        }
        return Value::makeUndefined();
    });
    vm.registerBuiltin("removeMovieClip", [this, current, arg](const Args& args) {
        Clip* clip = resolve(current(), arg(args, 0));
        if (clip && clip->made)
            takeOff(*clip, false);
        return Value::makeUndefined();
    });
    vm.registerBuiltin("updateAfterEvent", [](const Args&) { return Value::makeUndefined(); });
    // AS2's Selection: setFocus("_root.name_txt") and getFocus().
    ValuePtr selection = Value::makeObject();
    selection->setProperty("setFocus", Value::makeNativeFunction([this, current, arg](const Args& args) {
        const std::string path = arg(args, 0)->toString();
        const size_t dot = path.find_last_of("./");
        Clip* owner = dot == std::string::npos ? &current() : resolve(current(), path.substr(0, dot));
        const std::string name = dot == std::string::npos ? path : path.substr(dot + 1);
        focusText(owner, owner ? fieldNamed(owner->timeline, owner->frame, name) : nullptr);
        return Value::makeBoolean(_focus.field != nullptr);
    }));
    selection->setProperty("getFocus", Value::makeNativeFunction([this](const Args&) {
        if (!_focus.clip || !_focus.field)
            return Value::makeNull();
        const std::string path = slashPath(*_focus.clip);
        std::string dotted = "_level0";
        for (const char c : path == "/" ? std::string() : path)
            dotted += c == '/' ? '.' : c;
        return Value::makeString(dotted + "." + nameOf(*_focus.field));
    }));
    vm.setGlobal("Selection", selection);
    ValuePtr mouse = Value::makeObject();
    mouse->setProperty("hide", Value::makeNativeFunction([](const Args&) { return Value::makeUndefined(); }));
    mouse->setProperty("show", Value::makeNativeFunction([](const Args&) { return Value::makeUndefined(); }));
    vm.setGlobal("Mouse", mouse);
    // new Rectangle(x, y, width, height), for ActionScript 3's startDrag.
    vm.registerBuiltin("Rectangle", [arg](const Args& args) {
        ValuePtr rectangle = Value::makeObject();
        const char* names[] = {"x", "y", "width", "height"};
        for (size_t i = 0; i < 4; ++i)
            rectangle->setProperty(names[i], Value::makeNumber(args.size() > i ? args[i]->toNumber() : 0.0));
        return rectangle;
    });

    // Sounds: AS2's Sound with attachSound and start, ActionScript 3's
    // classes linked to library sounds with play, and stopping them all.
    vm.registerBuiltin("Sound", [this](const Args&) { return soundObject(std::string(), false); });
    vm.registerBuiltin("stopAllSounds", [this](const Args&) {
        if (stopAllSounds)
            stopAllSounds();
        return Value::makeUndefined();
    });
    ValuePtr mixer = Value::makeObject();
    mixer->setProperty("stopAll", Value::makeNativeFunction([this](const Args&) {
        if (stopAllSounds)
            stopAllSounds();
        return Value::makeUndefined();
    }));
    vm.setGlobal("SoundMixer", mixer);
    for (const std::string& linkage : _soundClasses)
        vm.registerBuiltin(linkage, [this, linkage](const Args&) { return soundObject(linkage, true); });

    // ActionScript 3's classes linked to library movie clips: new Hat()
    // makes one, off the stage until addChild puts it on.
    for (const Symbol* symbol : _library)
    {
        const std::string className = attributeOf(symbol->extraAttributes, "linkageClassName");
        if (className.empty() || symbol->symbolType == SymbolType::Graphic)
            continue;
        vm.registerBuiltin(className, [this, symbol](const Args&) {
            Clip* made = makeClip(nullptr, *symbol, std::string());
            return made ? objectFor(*made) : Value::makeNull();
        });
    }
}

void MovieRuntime::setLibrary(const std::vector<const Symbol*>& symbols, const std::vector<std::string>& soundIdentifiers,
    const std::vector<std::string>& soundClasses)
{
    _library = symbols;
    _soundIdentifiers = soundIdentifiers;
    _soundClasses = soundClasses;
}

as3::ValuePtr MovieRuntime::soundObject(const std::string& linkage, bool actionScript3)
{
    using as3::Value;
    using Args = std::vector<as3::ValuePtr>;
    as3::ValuePtr sound = Value::makeObject();
    // The object's own state, read back by its methods: held weakly, as
    // the methods are on it, and one a script keeps may outlive it.
    const std::weak_ptr<Value> weak = sound;
    sound->setProperty("__linkage", Value::makeString(linkage));
    sound->setProperty("__volume", Value::makeNumber(100));
    sound->setProperty("__channel", Value::makeNumber(-1));
    const auto start = [this, weak](double offset, int loops) {
        const as3::ValuePtr self = weak.lock();
        const std::string linked = self ? self->getProperty("__linkage")->toString() : std::string();
        if (linked.empty() || !playSound)
            return -1;
        const int channel = playSound(linked, offset, std::max(1, loops), self->getProperty("__volume")->toNumber() / 100.0);
        self->setProperty("__channel", Value::makeNumber(channel));
        return channel;
    };
    sound->setProperty("attachSound", Value::makeNativeFunction([weak](const Args& args) {
        if (const as3::ValuePtr self = weak.lock())
            self->setProperty("__linkage", Value::makeString(args.empty() ? std::string() : args[0]->toString()));
        return Value::makeUndefined();
    }));
    // AS2: start(seconds in, loops).
    sound->setProperty("start", Value::makeNativeFunction([start](const Args& args) {
        start(args.empty() ? 0.0 : args[0]->toNumber(), args.size() > 1 ? static_cast<int>(args[1]->toNumber()) : 1);
        return Value::makeUndefined();
    }));
    sound->setProperty("stop", Value::makeNativeFunction([this, weak](const Args&) {
        const as3::ValuePtr self = weak.lock();
        if (!self)
            return Value::makeUndefined();
        const int channel = static_cast<int>(self->getProperty("__channel")->toNumber());
        if (channel >= 0 && stopSound)
            stopSound(channel);
        else if (channel < 0 && stopAllSounds && self->getProperty("__linkage")->toString().empty())
            stopAllSounds();
        return Value::makeUndefined();
    }));
    sound->setProperty("setVolume", Value::makeNativeFunction([weak](const Args& args) {
        if (const as3::ValuePtr self = weak.lock())
            self->setProperty("__volume", Value::makeNumber(args.empty() ? 100 : args[0]->toNumber()));
        return Value::makeUndefined();
    }));
    sound->setProperty("getVolume", Value::makeNativeFunction([weak](const Args&) {
        const as3::ValuePtr self = weak.lock();
        return self ? self->getProperty("__volume") : Value::makeUndefined();
    }));
    // ActionScript 3: play(milliseconds in, loops), a SoundChannel back.
    sound->setProperty("play", Value::makeNativeFunction([this, start](const Args& args) {
        const int channel =
            start(args.empty() ? 0.0 : args[0]->toNumber() / 1000.0, args.size() > 1 ? static_cast<int>(args[1]->toNumber()) : 1);
        as3::ValuePtr soundChannel = Value::makeObject();
        soundChannel->setProperty("stop", Value::makeNativeFunction([this, channel](const Args&) {
            if (channel >= 0 && stopSound)
                stopSound(channel);
            return Value::makeUndefined();
        }));
        return soundChannel;
    }));
    (void)actionScript3;
    return sound;
}

Transform MovieRuntime::toStage(const Clip& clip) const
{
    Transform parent = clip.placement.known ? clip.placement.parentToStage
                                             : (clip.parent ? toStage(*clip.parent) : Transform());
    const Transform placed = clip.placed ? clip.placed->transform : Transform();
    return parent.concatenate(clip.transform(placed));
}

void MovieRuntime::stageToClip(const Clip& clip, double& x, double& y) const
{
    const Transform t = toStage(clip);
    const double det = t.m11 * t.m22 - t.m21 * t.m12;
    if (std::abs(det) < 1e-12)
        return;
    const double dx = x - t.tx;
    const double dy = y - t.ty;
    x = (t.m22 * dx - t.m21 * dy) / det;
    y = (-t.m12 * dx + t.m11 * dy) / det;
}

bool MovieRuntime::hits(const Clip& clip, double x, double y, bool shape) const
{
    if (!clip.placement.known)
        return false;
    if (shape && hitShape)
        return hitShape(clip, x, y);
    const Clip::Placement& box = clip.placement;
    return x >= box.left && x <= box.right && y >= box.top && y <= box.bottom;
}

bool MovieRuntime::overlaps(const Clip& a, const Clip& b) const
{
    if (!a.placement.known || !b.placement.known)
        return false;
    return a.placement.left <= b.placement.right && b.placement.left <= a.placement.right &&
        a.placement.top <= b.placement.bottom && b.placement.top <= a.placement.bottom;
}

Clip* MovieRuntime::clipUnderPointer(const Clip* except)
{
    Clip* best = nullptr;
    int bestDepth = -1;
    eachClip([&](Clip& clip) {
        if (&clip == _root.get() || !clip.shown())
            return;
        int depth = 0;
        for (const Clip* at = &clip; at; at = at->parent)
        {
            if (at == except)
                return;
            ++depth;
        }
        if (depth >= bestDepth && hits(clip, _mouseX, _mouseY, true))
        {
            best = &clip;
            bestDepth = depth;
        }
    });
    return best;
}

std::string MovieRuntime::slashPath(const Clip& clip) const
{
    std::string path;
    for (const Clip* at = &clip; at && at->parent; at = at->parent)
        path = "/" + (at->name.empty() ? at->key : at->name) + path;
    return path.empty() ? "/" : path;
}

void MovieRuntime::startDrag(Clip& clip, bool lockCenter, const std::vector<double>& bounds)
{
    _drag = Drag();
    _drag.clip = &clip;
    _drag.lock = lockCenter;
    double px = _mouseX, py = _mouseY;
    if (clip.parent)
        stageToClip(*clip.parent, px, py);
    const Transform shown = clip.transform(clip.placed ? clip.placed->transform : Transform());
    if (!lockCenter)
    {
        _drag.dx = shown.tx - px;
        _drag.dy = shown.ty - py;
    }
    if (bounds.size() == 4)
    {
        _drag.bounded = true;
        _drag.left = bounds[0];
        _drag.top = bounds[1];
        _drag.right = bounds[2];
        _drag.bottom = bounds[3];
    }
    pointerAt(_mouseX, _mouseY);
}

void MovieRuntime::stopDrag()
{
    Clip* dragged = _drag.clip;
    _drag = Drag();
    if (!dragged)
        return;
    // Where it was let go: the clip under it, for _droptarget.
    Clip* under = clipUnderPointer(dragged);
    objectFor(*dragged)->setProperty("__dropTarget", under ? objectFor(*under) : as3::Value::makeNull());
}

void MovieRuntime::pointerAt(double x, double y)
{
    _mouseX = x;
    _mouseY = y;
    if (_drag.clip)
    {
        Clip& clip = *_drag.clip;
        double px = x, py = y;
        if (clip.parent)
            stageToClip(*clip.parent, px, py);
        double nx = px + _drag.dx;
        double ny = py + _drag.dy;
        if (_drag.bounded)
        {
            nx = std::clamp(nx, std::min(_drag.left, _drag.right), std::max(_drag.left, _drag.right));
            ny = std::clamp(ny, std::min(_drag.top, _drag.bottom), std::max(_drag.top, _drag.bottom));
        }
        if (clip.display)
        {
            clip.display->setX(nx);
            clip.display->setY(ny);
        }
    }
    // mouseMove: AS2's clip event, ActionScript 3's listeners.
    eachClip([this](Clip& clip) {
        if (!clip.clipEvents.empty())
            runClipEvent(clip, "mouseMove");
        if (clip.listensFor("mouseMove"))
            dispatch(clip, "mouseMove", false);
    });
    settle();
}

const Symbol* MovieRuntime::linked(const std::string& linkage) const
{
    if (linkage.empty())
        return nullptr;
    for (const Symbol* symbol : _library)
    {
        if (attributeOf(symbol->extraAttributes, "linkageIdentifier") == linkage ||
            attributeOf(symbol->extraAttributes, "linkageClassName") == linkage)
            return symbol;
    }
    return nullptr;
}

Clip* MovieRuntime::makeClip(Clip* parent, const Symbol& symbol, const std::string& name)
{
    auto instance = std::make_shared<SymbolInstance>(nullptr);
    instance->symbol = const_cast<Symbol*>(&symbol);
    instance->libraryItemName = symbol.name;
    instance->symbolType = symbol.symbolType;
    instance->name = name;
    auto made = std::make_unique<Clip>();
    made->symbol = &symbol;
    made->timeline = symbol.timelines.empty() ? nullptr : symbol.timelines.front();
    made->parent = parent;
    made->placed = instance.get();
    made->made = instance;
    made->name = name;
    made->key = "@" + std::to_string(++_madeCount) + ":" + name;
    made->button = symbol.symbolType == SymbolType::Button;
    made->playing = !made->button;
    made->length = lengthOf(made->timeline);
    attachDisplay(*made);
    Clip* clip = made.get();
    if (parent)
    {
        parent->children.push_back(std::move(made));
        if (parent->display && parent->display->isContainer())
            static_cast<display::DisplayObjectContainer*>(parent->display.get())->addChild(clip->display);
    }
    else
        _offStage.push_back(std::move(made));
    _madeToEnter.push_back(clip);
    // A clip attachMovie or duplicateMovieClip made is of its symbol's
    // class at once.
    if (!_as3)
        registeredClass(*clip);
    if (changed)
        changed();
    return clip;
}

void MovieRuntime::takeOff(Clip& clip, bool keep)
{
    std::vector<std::unique_ptr<Clip>>& from = clip.parent ? clip.parent->children : _offStage;
    auto found = std::find_if(from.begin(), from.end(), [&clip](const std::unique_ptr<Clip>& c) { return c.get() == &clip; });
    if (found == from.end())
        return;
    std::unique_ptr<Clip> owned = std::move(*found);
    from.erase(found);
    if (!keep)
        forget(clip);
    if (clip.display && clip.display->parent())
        clip.display->parent()->removeChild(clip.display.get());
    clip.parent = nullptr;
    clip.placement.known = false;
    // Kept, even when gone for good, so a script still running on it or
    // holding it finds it.
    _offStage.push_back(std::move(owned));
    if (changed)
        changed();
}

Clip* MovieRuntime::clipOfValue(const as3::ValuePtr& value)
{
    if (!value || !value->host)
        return nullptr;
    Clip* found = nullptr;
    eachClip([&](Clip& clip) {
        if (!found && clip.object == value)
            found = &clip;
    });
    for (const auto& clip : _offStage)
        if (!found && clip && clip->object == value)
            found = clip.get();
    return found;
}

bool MovieRuntime::alive(const Clip* clip) const
{
    if (!clip || !_root)
        return false;
    std::function<bool(const Clip&)> walk = [&](const Clip& at) {
        if (&at == clip)
            return true;
        for (const auto& child : at.children)
            if (child && walk(*child))
                return true;
        return false;
    };
    return walk(*_root);
}

std::vector<Clip*> MovieRuntime::madeChildren(const Clip& clip) const
{
    std::vector<Clip*> made;
    for (const auto& child : clip.children)
        if (child && child->made)
            made.push_back(child.get());
    return made;
}

Clip* MovieRuntime::clipOfMade(const SymbolInstance& instance) const
{
    Clip* found = nullptr;
    std::function<void(Clip&)> walk = [&](Clip& clip) {
        if (clip.made.get() == &instance)
            found = &clip;
        for (const auto& child : clip.children)
            if (!found && child)
                walk(*child);
    };
    if (_root)
        walk(*_root);
    return found;
}

std::optional<std::string> MovieRuntime::textOf(Clip& clip, const StaticText& field)
{
    if (field.textType == StaticText::TextType::Static)
        return std::nullopt;
    if (!nameOf(field).empty())
    {
        const auto set = clip.texts.find(nameOf(field));
        if (set != clip.texts.end())
            return set->second;
    }
    // AS2's variable fields show a variable's value: "score", or a path to
    // one, "_root.score".
    std::string variable = attributeOf(field.extraAttributes, "variableName");
    if (variable.empty())
        return std::nullopt;
    Clip* owner = &clip;
    const size_t dot = variable.find_last_of("./");
    if (dot != std::string::npos)
    {
        owner = resolve(clip, variable.substr(0, dot));
        variable = variable.substr(dot + 1);
    }
    if (!owner)
        return std::nullopt;
    const as3::ValuePtr value = objectFor(*owner)->getProperty(variable);
    if (!value || value->isUndefined() || value->isNull())
        return std::nullopt;
    return value->toString();
}

/// A text field as scripts see it: its text read and set.
class TextFieldObject : public as3::HostObject
{
public:
    TextFieldObject(MovieRuntime& runtime, Clip& clip, const std::string& name)
        : _runtime(runtime)
        , _life(clip.life)
        , _name(name)
    {}

    /// The clip it is on, or null once that is freed.
    Clip* clip() const { return *_life; }
    const std::string& name() const { return _name; }

    as3::ValuePtr get(const std::string& property) override
    {
        Clip* clip = *_life;
        if (property == "text" || property == "htmlText")
        {
            if (!clip)
                return as3::Value::makeString(std::string());
            const auto set = clip->texts.find(_name);
            if (set != clip->texts.end())
                return as3::Value::makeString(set->second);
            const StaticText* field = fieldNamed(clip->timeline, clip->frame, _name);
            return as3::Value::makeString(field ? drawnText(*field) : std::string());
        }
        if (property == "length")
        {
            const as3::ValuePtr text = get("text");
            return as3::Value::makeNumber(static_cast<double>(text->toString().size()));
        }
        if (property == "_name" || property == "name")
            return as3::Value::makeString(_name);
        return nullptr;
    }

    bool set(const std::string& property, as3::ValuePtr value) override
    {
        if (property != "text" && property != "htmlText")
            return false;
        Clip* clip = *_life;
        if (!clip)
            return true;
        const std::string text = value ? value->toString() : std::string();
        clip->texts[_name] = property == "htmlText" ? withoutTags(text) : text;
        if (const StaticText* field = fieldNamed(clip->timeline, clip->frame, _name))
            if (display::TextField* shown = _runtime.displayField(*clip, *field))
            {
                shown->setText(clip->texts[_name]);
                shown->timelineRevision = shown->layoutRevision();
            }
        if (_runtime.changed)
            _runtime.changed();
        return true;
    }

private:
    MovieRuntime& _runtime;
    std::shared_ptr<Clip*> _life;
    std::string _name;
};

as3::ValuePtr MovieRuntime::textFieldObject(Clip& clip, const std::string& name)
{
    return as3::Value::makeHostObject(std::make_shared<TextFieldObject>(*this, clip, name));
}

void MovieRuntime::focusText(Clip* clip, const StaticText* field)
{
    _focus = TextFocus();
    if (clip && field && field->textType == StaticText::TextType::Input)
        _focus = TextFocus{clip, field};
    if (changed)
        changed();
}

void MovieRuntime::setFieldText(Clip& clip, const StaticText& field, const std::string& text)
{
    if (!nameOf(field).empty())
        clip.texts[nameOf(field)] = text;
    if (display::TextField* shown = displayField(clip, field))
    {
        shown->setText(text);
        shown->timelineRevision = shown->layoutRevision();
    }
    std::string variable = attributeOf(field.extraAttributes, "variableName");
    if (!variable.empty())
    {
        Clip* owner = &clip;
        const size_t dot = variable.find_last_of("./");
        if (dot != std::string::npos)
        {
            owner = resolve(clip, variable.substr(0, dot));
            variable = variable.substr(dot + 1);
        }
        if (owner)
            objectFor(*owner)->setProperty(variable, as3::Value::makeString(text));
    }
    if (changed)
        changed();
}

const StaticText* MovieRuntime::textField(const Clip& clip, const std::string& name) const
{
    return fieldNamed(clip.timeline, clip.frame, name);
}

std::string MovieRuntime::fieldText(Clip& clip, const StaticText& field)
{
    const std::optional<std::string> shown = textOf(clip, field);
    return shown ? *shown : drawnText(field);
}

void MovieRuntime::type(int code, int character)
{
    Clip& clip = *_focus.clip;
    const StaticText& field = *_focus.field;
    std::string text = textOf(clip, field).value_or(drawnText(field));
    if (code == 8)
    {
        // The last character, whole: UTF-8 continuation bytes go with it.
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
            text.pop_back();
        if (!text.empty())
            text.pop_back();
    }
    else if (code == 13)
    {
        if (field.lineType.find("multiline") == std::string::npos)
            return;
        text += "\n";
    }
    else if (character >= 32)
    {
        // As UTF-8.
        const unsigned int c = static_cast<unsigned int>(character);
        if (c < 0x80)
            text += static_cast<char>(c);
        else if (c < 0x800)
        {
            text += static_cast<char>(0xC0 | (c >> 6));
            text += static_cast<char>(0x80 | (c & 0x3F));
        }
        else
        {
            text += static_cast<char>(0xE0 | (c >> 12));
            text += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    else
        return;
    setFieldText(clip, field, text);
}

void MovieRuntime::eachClip(const std::function<void(Clip&)>& visit)
{
    std::function<void(Clip&)> walk = [&](Clip& clip) {
        visit(clip);
        // A copy: a visit may not change what is on the stage, but keep safe.
        std::vector<Clip*> children;
        for (const auto& child : clip.children)
            if (child)
                children.push_back(child.get());
        for (Clip* child : children)
            if (!child->gone)
                walk(*child);
    };
    walk(*_root);
}

void MovieRuntime::runFunction(const as3::ValuePtr& function, const std::vector<as3::ValuePtr>& args, Clip& scope,
    const std::string& where, const DOMElement* source)
{
    if (!function || !function->isFunction())
        return;
    ++_scriptRuns;
    Clip* was = _current;
    const DOMElement* wasSource = _source;
    _current = &scope;
    if (source)
        _source = source;
    _vm->call(function, args, objectFor(scope), objectFor(scope));
    report(where);
    _current = was;
    _source = wasSource;
}

void MovieRuntime::tell(const std::string& message, const DOMElement* source, int line)
{
    if (error)
        error(message);
    if (scriptError)
        scriptError(ScriptError{message, source, line});
}

void MovieRuntime::dispatch(Clip& target, const std::string& type, bool bubbles,
    const std::function<void(const as3::ValuePtr&)>& fill)
{
    using as3::Value;
    // Nothing to do unless someone on the way listens.
    bool heard = false;
    for (Clip* at = &target; at && !heard; at = bubbles ? at->parent : nullptr)
        heard = at->listensFor(type);
    if (!heard)
        return;
    as3::ValuePtr event = Value::makeObject();
    event->setProperty("type", Value::makeString(type));
    event->setProperty("target", objectFor(target));
    event->setProperty("bubbles", Value::makeBoolean(bubbles));
    auto stopped = std::make_shared<bool>(false);
    const auto stop = Value::makeNativeFunction([stopped](const std::vector<as3::ValuePtr>&) {
        *stopped = true;
        return Value::makeUndefined();
    });
    event->setProperty("stopPropagation", stop);
    event->setProperty("stopImmediatePropagation", stop);
    if (fill)
        fill(event);
    for (Clip* at = &target; at && !*stopped; at = bubbles ? at->parent : nullptr)
    {
        const auto found = at->listeners.find(type);
        if (found == at->listeners.end())
            continue;
        event->setProperty("currentTarget", objectFor(*at));
        // A listener may add or take off others: these are the ones there were.
        const std::vector<Clip::Listener> listeners = found->second;
        for (const Clip::Listener& listener : listeners)
            if (!at->gone && !(listener.scope && listener.scope->gone))
                runFunction(listener.function, {event}, listener.scope ? *listener.scope : *at,
                    (at->name.empty() ? at->key : at->name) + ", " + type, listener.source);
    }
}

void MovieRuntime::report(const std::string& where)
{
    if (!_vm->hasError())
        return;
    std::string message = where;
    if (_vm->getErrorLine() >= 0)
        message += ", line " + std::to_string(_vm->getErrorLine());
    message += ": " + _vm->getError();
    // ActionScript 3's errors carry where they were thrown, as the debug
    // player shows them.
    std::string stack = _as3 ? _vm->getErrorStack() : std::string();
    while (!stack.empty() && (stack.back() == '\n' || stack.back() == '\r'))
        stack.pop_back();
    if (!stack.empty())
        message += "\n" + stack;
    tell(message, _source, std::max(0, _vm->getErrorLine()));
}

std::shared_ptr<as3::BytecodeFunction> MovieRuntime::compiled(const DOMElement* key, const std::string& source,
    const std::string& where)
{
    const auto found = _compiled.find(key);
    if (found != _compiled.end())
        return found->second;
    std::shared_ptr<as3::BytecodeFunction> code;
    const std::string text = _as3 ? separateImports(source) : source;
    as3::Parser parser(text, where);
    std::unique_ptr<as3::Program> program = parser.parse();
    if (parser.hasErrors())
    {
        // "Parse error at line N, column C: what", for each.
        const std::string errors = parser.getErrors();
        const std::size_t at = errors.find("at line ");
        tell(where + ": " + errors, key, at == std::string::npos ? 0 : std::atoi(errors.c_str() + at + 8));
    }
    else if (program)
    {
        // What does not compile is told, and the script left out.
        try
        {
            as3::Compiler compiler;
            code = compiler.compile(program.get());
        }
        catch (const std::exception& failed)
        {
            std::string message = failed.what();
            while (!message.empty() && message.back() == '\n')
                message.pop_back();
            const std::size_t at = message.find("line ");
            tell(where + ": " + message, key, at == std::string::npos ? 0 : std::atoi(message.c_str() + at + 5));
            code = nullptr;
        }
    }
    _compiled[key] = code;
    return code;
}

void MovieRuntime::run(const DOMElement* key, const std::string& source, Clip& scope, const std::string& where)
{
    std::shared_ptr<as3::BytecodeFunction> code = compiled(key, source, where);
    if (!code)
        return;
    ++_scriptRuns;
    Clip* was = _current;
    const DOMElement* wasSource = _source;
    _current = &scope;
    _source = key;
    _vm->execute(code, objectFor(scope));
    report(where);
    _current = was;
    _source = wasSource;
}

void MovieRuntime::runFrameScripts(Clip& clip)
{
    if (!clip.timeline)
        return;
    // A frame addFrameScript gave a function: it runs, as the frame's script.
    const auto given = clip.frameScripts.find(clip.frame);
    if (_as3 && given != clip.frameScripts.end())
    {
        for (const Layer* layer : clip.timeline->layers)
        {
            const Frame* frame = layer && plays(*layer) ? keyframeAt(*layer, clip.frame) : nullptr;
            if (frame && frame->index == clip.frame && frameSound && !attributeOf(frame->extraAttributes, "soundName").empty())
                frameSound(*frame);
        }
        ++_scriptRuns;
        Clip* was = _current;
        _current = &clip;
        const as3::Val function = given->second;
        _bindings->callSafely(function, objectFor(clip), {},
            (clip.symbol ? clip.symbol->name : clip.timeline->name) + ", frame " + std::to_string(clip.frame + 1));
        _current = was;
        return;
    }
    // Every layer's script that starts on this frame, top layer first, and
    // the sounds of its keyframes.
    for (const Layer* layer : clip.timeline->layers)
    {
        if (!layer || !plays(*layer))
            continue;
        const Frame* frame = keyframeAt(*layer, clip.frame);
        if (frame && frame->index == clip.frame && frameSound && !attributeOf(frame->extraAttributes, "soundName").empty())
            frameSound(*frame);
        if (!frame || frame->index != clip.frame)
            continue;
        const std::string where = (clip.symbol ? clip.symbol->name : clip.timeline->name) + ", frame " +
            std::to_string(clip.frame + 1);
        if (!_canvas && !frame->extraChildren.empty() && compiledActionsOf(frame, frame->extraChildren))
        {
            runCompiled(clip, *frame, false, where);
            continue;
        }
        if (!frame->actionScript || frame->actionScript->code.empty())
            continue;
        if (_canvas)
            _canvas->runFrame(clip, *frame, where);
        else
            run(frame, frame->actionScript->code, clip, where);
    }
}

void MovieRuntime::runFrameActions(Clip& clip, int index)
{
    if (!clip.timeline)
        return;
    for (const Layer* layer : clip.timeline->layers)
    {
        const Frame* frame = layer && plays(*layer) ? keyframeAt(*layer, index) : nullptr;
        if (!frame || frame->index != index)
            continue;
        const std::string where = (clip.symbol ? clip.symbol->name : clip.timeline->name) + ", frame " +
            std::to_string(index + 1);
        if (!frame->extraChildren.empty() && compiledActionsOf(frame, frame->extraChildren))
            runCompiled(clip, *frame, false, where);
        else if (frame->actionScript && !frame->actionScript->code.empty())
            run(frame, frame->actionScript->code, clip, where);
    }
}

const CompiledActions* MovieRuntime::compiledActionsOf(const void* element, const std::vector<std::string>& extraChildren)
{
    const auto found = _compiledActions.find(element);
    if (found != _compiledActions.end())
        return found->second.get();
    std::shared_ptr<const CompiledActions> compiled = compiledActionsIn(extraChildren);
    _compiledActions[element] = compiled;
    return compiled.get();
}

ActionsPlayer& MovieRuntime::actionsPlayer(int version)
{
    if (!_actions)
        _actions = std::make_unique<ActionsPlayer>(*this, *_vm, version);
    return *_actions;
}

void MovieRuntime::runInitActions(Clip& clip)
{
    for (const Layer* layer : clip.timeline->layers)
    {
        const Frame* frame = layer && plays(*layer) ? keyframeAt(*layer, clip.frame) : nullptr;
        if (!frame || frame->index != clip.frame || frame->extraChildren.empty())
            continue;
        if (std::find(_initsRun.begin(), _initsRun.end(), frame) != _initsRun.end())
            continue;
        if (!compiledActionsOf(frame, frame->extraChildren))
            continue;
        _initsRun.push_back(frame);
        runCompiled(clip, *frame, true,
            (clip.symbol ? clip.symbol->name : clip.timeline->name) + ", frame " + std::to_string(clip.frame + 1));
    }
}

void MovieRuntime::runCompiled(Clip& clip, const Frame& frame, bool init, const std::string& where)
{
    const CompiledActions* compiled = compiledActionsOf(&frame, frame.extraChildren);
    if (!compiled)
        return;
    const CompiledActions::Block::Kind kind = init ? CompiledActions::Block::Kind::Init : CompiledActions::Block::Kind::Frame;
    const DOMElement* wasSource = _source;
    _source = &frame;
    for (const CompiledActions::Block& block : compiled->blocks)
        if (block.kind == kind)
            actionsPlayer(compiled->version).run(block, clip, init ? where + ", #initclip " + block.symbol : where);
    _source = wasSource;
}

void MovieRuntime::registeredClass(Clip& clip)
{
    if (!_actions || !clip.symbol || clip.classed)
        return;
    std::string linkage = attributeOf(clip.symbol->extraAttributes, "linkageIdentifier");
    if (linkage.empty())
        linkage = attributeOf(clip.symbol->extraAttributes, "linkageClassName");
    const as3::Val constructor = linkage.empty() ? as3::Val() : _actions->interpreter().registeredClass(linkage);
    if (!constructor.isFunction())
        return;
    clip.classed = true;
    Clip* was = _current;
    _current = &clip;
    _actions->interpreter().applyClass(objectFor(clip), constructor);
    _current = was;
}

Clip* MovieRuntime::clipOfObject(const as3::Val& value)
{
    const ClipObject* object = value.host<ClipObject>();
    return object ? object->clip() : nullptr;
}

void MovieRuntime::syncChildren(Clip& clip, std::vector<Clip*>& entered)
{
    if (!clip.timeline)
        return;
    if (!_as3 && !_canvas)
        runInitActions(clip);
    std::vector<std::unique_ptr<Clip>> kept;
    for (const Layer* layer : clip.timeline->layers)
    {
        if (!layer || !plays(*layer))
            continue;
        const Frame* frame = keyframeAt(*layer, clip.frame);
        if (!frame)
            continue;
        for (const Element* element : frame->elements)
        {
            const SymbolInstance* instance = instanceOf(element);
            if (!instance || !instance->symbol || instance->symbolType == SymbolType::Graphic)
                continue;
            const std::string key = keyOf(*clip.timeline, *layer, *frame, *instance, element);
            // Already about, from the frame before: it goes on as it was.
            auto found = std::find_if(clip.children.begin(), clip.children.end(),
                [&key](const std::unique_ptr<Clip>& c) { return c && c->key == key; });
            if (found != clip.children.end())
            {
                (*found)->placed = instance;
                (*found)->layer = layer;
                kept.push_back(std::move(*found));
                continue;
            }
            // JavaScript: one kept off the stage comes back as it was.
            auto parked = std::find_if(clip.parked.begin(), clip.parked.end(),
                [&key](const std::unique_ptr<Clip>& c) { return c && c->key == key; });
            if (parked != clip.parked.end())
            {
                (*parked)->placed = instance;
                (*parked)->layer = layer;
                Clip* back = parked->get();
                kept.push_back(std::move(*parked));
                clip.parked.erase(parked);
                revive(*back, entered);
                continue;
            }
            auto made = std::make_unique<Clip>();
            made->symbol = instance->symbol;
            made->timeline = instance->symbol->timelines.empty() ? nullptr : instance->symbol->timelines.front();
            made->parent = &clip;
            made->layer = layer;
            made->placed = instance;
            made->name = instance->name;
            made->key = key;
            made->button = instance->symbolType == SymbolType::Button;
            made->playing = !made->button;
            made->length = lengthOf(made->timeline);
            attachDisplay(*made);
            entered.push_back(made.get());
            // Its own children are placed now too, so a script anywhere
            // finds the whole of what is on the stage.
            syncChildren(*made, entered);
            // The class Object.registerClass gave its symbol, made before
            // the frame's scripts run.
            if (!_as3 && !_canvas)
                registeredClass(*made);
            kept.push_back(std::move(made));
        }
    }
    // Clips gone from the frame are let go; those scripts made stay, over
    // the rest.
    for (auto& gone : clip.children)
    {
        if (gone && gone->made)
            kept.push_back(std::move(gone));
        else if (gone && _javaScript && !gone->name.empty())
        {
            // CreateJS keeps a named instance for when a frame adds it back;
            // the display list lets it go below.
            _sent.erase(std::remove(_sent.begin(), _sent.end(), gone.get()), _sent.end());
            clip.parked.push_back(std::move(gone));
        }
        else if (gone)
        {
            forget(*gone);
            _gone.push_back(std::move(gone));
        }
    }
    clip.children = std::move(kept);
    syncDisplay(clip);
}

void MovieRuntime::revive(Clip& clip, std::vector<Clip*>& entered)
{
    std::vector<Clip*> had;
    for (const auto& child : clip.children)
        if (child && !child->made)
            had.push_back(child.get());
    clip.frame = 0;
    clip.playing = !clip.button;
    clip.placement.known = false;
    entered.push_back(&clip);
    // What its first frame places: those it had from before start over too.
    syncChildren(clip, entered);
    for (const auto& child : clip.children)
        if (child && std::find(had.begin(), had.end(), child.get()) != had.end())
            revive(*child, entered);
}

Clip* MovieRuntime::canvasInstance(Clip& clip, const std::string& name)
{
    if (name.empty())
        return nullptr;
    if (Clip* on = clip.named(name))
        return on;
    for (const auto& kept : clip.parked)
        if (kept && kept->name == name)
            return kept.get();
    if (!_javaScript || !clip.timeline)
        return nullptr;
    // Its first keyframe: made from it now, kept until a frame places it.
    for (const Layer* layer : clip.timeline->layers)
    {
        if (!layer || !plays(*layer))
            continue;
        for (const Frame* frame : layer->frames)
            for (const Element* element : frame ? frame->elements : std::vector<Element*>())
            {
                const SymbolInstance* instance = instanceOf(element);
                if (!instance || !instance->symbol || instance->symbolType == SymbolType::Graphic ||
                    instance->name != name)
                    continue;
                auto made = std::make_unique<Clip>();
                made->symbol = instance->symbol;
                made->timeline = instance->symbol->timelines.empty() ? nullptr : instance->symbol->timelines.front();
                made->parent = &clip;
                made->layer = layer;
                made->placed = instance;
                made->name = instance->name;
                made->key = keyOf(*clip.timeline, *layer, *frame, *instance, element);
                made->button = instance->symbolType == SymbolType::Button;
                made->playing = !made->button;
                made->length = lengthOf(made->timeline);
                attachDisplay(*made);
                // What is in it is made too, as CreateJS makes it all.
                std::vector<Clip*> inside;
                syncChildren(*made, inside);
                clip.parked.push_back(std::move(made));
                return clip.parked.back().get();
            }
    }
    return nullptr;
}

void MovieRuntime::attachDisplay(Clip& clip)
{
    if (clip.button)
    {
        // A button's states are its frames: up, over, down and hit (the
        // up frame when the hit frame is empty).
        auto button = std::make_shared<display::SimpleButton>();
        const auto state = [&clip](int frame) {
            auto art = std::make_shared<display::TimelineArt>();
            art->frameOf = clip.timeline;
            art->frame = frame;
            art->clip = &clip;
            return art;
        };
        bool hitFrame = false;
        for (const Layer* layer : clip.timeline ? clip.timeline->layers : std::vector<Layer*>())
            if (const Frame* frame = layer && plays(*layer) ? keyframeAt(*layer, 3) : nullptr)
                hitFrame = hitFrame || (frame->index == 3 && !frame->elements.empty());
        button->upState = state(0);
        button->overState = state(clip.length > 1 ? 1 : 0);
        button->downState = state(clip.length > 2 ? 2 : 0);
        button->hitTestState = state(hitFrame ? 3 : 0);
        clip.display = button;
    }
    else
        clip.display = std::make_shared<display::MovieClip>();
    clip.display->clip = &clip;
    clip.display->setName(clip.name);
    if (clip.placed)
    {
        clip.display->setPlacedMatrix(toMatrix(clip.placed->transform));
        clip.display->setPlacedColorTransform(toColor(clip.placed->colorTransform));
        clip.display->setPlacedFilters(toFilters(clip.placed->filters));
        display::BlendMode mode = display::BlendMode::Normal;
        display::blendModeNamed(clip.placed->blendMode, mode);
        clip.display->setPlacedBlendMode(mode);
        clip.display->setVisible(clip.placed->visible);
    }
}

void MovieRuntime::syncDisplay(Clip& clip)
{
    if (!clip.display || !clip.display->isContainer() || !clip.timeline)
        return;
    auto* container = static_cast<display::DisplayObjectContainer*>(clip.display.get());
    // The keyframes showing: when they are the ones there were, so is what
    // they place, but for the children clips made since.
    std::vector<const Frame*> frames;
    for (const Layer* layer : clip.timeline->layers)
        frames.push_back(layer && plays(*layer) ? keyframeAt(*layer, clip.frame) : nullptr);
    bool same = frames == clip.timelineFrames;
    if (same)
        for (const auto& child : clip.children)
            if (child && !child->made && child->display &&
                std::find(clip.timelineObjects.begin(), clip.timelineObjects.end(), child->display) ==
                    clip.timelineObjects.end())
                same = false;
    if (same)
        return;
    clip.timelineFrames = frames;

    std::vector<std::shared_ptr<display::DisplayObject>> now;
    const auto had = [&clip](const std::string& key) -> std::shared_ptr<display::DisplayObject> {
        for (const auto& object : clip.timelineObjects)
            if (object && object->timeline && object->timeline->key == key)
                return object;
        return nullptr;
    };
    int depth = 0;
    // Bottom layer first, as they draw.
    for (size_t li = clip.timeline->layers.size(); li-- > 0;)
    {
        const Layer* layer = clip.timeline->layers[li];
        const Frame* frame = frames[li];
        if (!layer || !frame)
            continue;
        for (size_t ei = 0; ei < frame->elements.size(); ++ei)
        {
            const Element* element = frame->elements[ei];
            if (!element)
                continue;
            ++depth;
            std::shared_ptr<display::DisplayObject> object;
            std::string key;
            const SymbolInstance* instance = instanceOf(element);
            const auto* text = dynamic_cast<const StaticText*>(element);
            if (instance && instance->symbol && instance->symbolType != SymbolType::Graphic)
            {
                key = keyOf(*clip.timeline, *layer, *frame, *instance, element);
                Clip* child = clip.child(key);
                if (!child || !child->display)
                    continue;
                object = child->display;
                object->setPlacedMatrix(toMatrix(instance->transform));
                object->setPlacedColorTransform(toColor(instance->colorTransform));
                object->setPlacedFilters(toFilters(instance->filters));
                display::BlendMode mode = display::BlendMode::Normal;
                display::blendModeNamed(instance->blendMode, mode);
                object->setPlacedBlendMode(mode);
            }
            else if (text && text->textType != StaticText::TextType::Static)
            {
                key = nameOf(*text).empty() ? "#" + std::to_string(li) + ":" + std::to_string(ei) + ":text"
                                                 : nameOf(*text);
                object = had(key);
                auto* field = object && object->kind() == display::ObjectKind::TextField
                    ? static_cast<display::TextField*>(object.get()) : nullptr;
                if (!field)
                {
                    auto made = std::make_shared<display::TextField>();
                    made->setFromElement(*text);
                    made->setName(nameOf(*text));
                    // What a script put in it already.
                    const auto set = nameOf(*text).empty() ? clip.texts.end() : clip.texts.find(nameOf(*text));
                    if (set != clip.texts.end())
                        made->setText(set->second);
                    made->timelineRevision = made->layoutRevision();
                    object = made;
                }
                // Where the frame puts it: its box is in its own space.
                object->setPlacedMatrix(toMatrix(text->transform));
            }
            else
            {
                key = "#" + std::to_string(li) + ":" + std::to_string(ei);
                object = had(key);
                if (!object || object->kind() != display::ObjectKind::TimelineArt)
                    object = std::make_shared<display::TimelineArt>();
                auto* art = static_cast<display::TimelineArt*>(object.get());
                art->element = element;
                art->maskArt = layer->layerType == Layer::Type::Mask;
            }
            object->timeline = display::TimelinePlacement{element, layer, depth, key};
            now.push_back(object);
        }
    }
    // What the frame no longer places comes off; what it places now goes
    // on, unless a script took it off since it was placed.
    for (const auto& object : clip.timelineObjects)
        if (object && object->parent() == container && std::find(now.begin(), now.end(), object) == now.end())
            container->removeTimelineChild(object.get());
    const std::vector<std::shared_ptr<display::DisplayObject>> before = clip.timelineObjects;
    clip.timelineObjects = now;
    for (const auto& object : now)
    {
        if (object->parent() == container)
            continue;
        if (!object->parent() && std::find(before.begin(), before.end(), object) == before.end())
            container->placeTimelineChild(object);
    }
    // The variables the clip's class declares for its instances.
    if (_as3 && clip.object)
        _bindings->declareInstances(clip);
}

const SymbolInstance* MovieRuntime::instanceOf(const Element* element) const
{
    if (const auto* instance = dynamic_cast<const SymbolInstance*>(element))
        return instance;
    // A component placed on the stage, kept as its XML: an instance of its
    // symbol, read once.
    const auto* raw = dynamic_cast<const RawElement*>(element);
    if (!raw || raw->xml.find("<DOMComponentInstance") == std::string::npos)
        return nullptr;
    const auto found = _components.find(raw);
    if (found != _components.end())
        return found->second.get();
    tinyxml2::XMLDocument xml;
    if (xml.Parse(raw->xml.c_str(), raw->xml.size()) != tinyxml2::XML_SUCCESS || !xml.RootElement())
        return nullptr;
    const tinyxml2::XMLElement* root = xml.RootElement();
    const char* item = root->Attribute("libraryItemName");
    const Symbol* symbol = nullptr;
    for (const Symbol* each : _library)
        if (each && item && each->name == item)
            symbol = each;
    if (!symbol)
        return nullptr;
    std::unique_ptr<SymbolInstance>& made = _components[raw];
    made = std::make_unique<SymbolInstance>(raw->parent);
    made->symbol = const_cast<Symbol*>(symbol);
    made->libraryItemName = symbol->name;
    made->symbolType = symbol->symbolType == SymbolType::Graphic ? SymbolType::MovieClip : symbol->symbolType;
    made->name = root->Attribute("name") ? root->Attribute("name") : "";
    if (const tinyxml2::XMLElement* matrix = root->FirstChildElement("matrix"))
        if (const tinyxml2::XMLElement* m = matrix->FirstChildElement("Matrix"))
            made->transform = Transform(m->DoubleAttribute("a", 1.0), m->DoubleAttribute("b", 0.0), m->DoubleAttribute("c", 0.0),
                m->DoubleAttribute("d", 1.0), m->DoubleAttribute("tx", 0.0), m->DoubleAttribute("ty", 0.0));
    if (const tinyxml2::XMLElement* parameters = root->FirstChildElement("parametersAsXML"))
        if (parameters->GetText())
            _componentParameters[made.get()] = parameters->GetText();
    return made.get();
}

display::DisplayObject* MovieRuntime::timelineObject(const Clip& clip, const Element* element) const
{
    for (const auto& object : clip.timelineObjects)
        if (object && object->timeline && object->timeline->element == element)
            return object.get();
    return nullptr;
}

display::TextField* MovieRuntime::displayField(const Clip& clip, const StaticText& field) const
{
    for (const auto& object : clip.timelineObjects)
        if (object && object->kind() == display::ObjectKind::TextField && object->timeline &&
            (object->timeline->element == &field ||
                (!nameOf(field).empty() && object->timeline->key == nameOf(field))))
            return static_cast<display::TextField*>(object.get());
    return nullptr;
}

void MovieRuntime::forget(Clip& clip)
{
    // Off the display list with it.
    if (clip.display && clip.display->parent())
        clip.display->parent()->removeChild(clip.display.get());
    std::vector<Clip*> going;
    std::function<void(Clip&)> mark = [&](Clip& at) {
        at.gone = true;
        going.push_back(&at);
        for (const auto& child : at.children)
            if (child)
                mark(*child);
        for (const auto& child : at.parked)
            if (child)
                mark(*child);
    };
    mark(clip);
    letGo(going);
}

void MovieRuntime::letGo(std::vector<Clip*> going)
{
    std::sort(going.begin(), going.end());
    const auto among = [&going](const Clip* clip) { return std::binary_search(going.begin(), going.end(), clip); };
    if (among(_hovered))
        _hovered = nullptr;
    if (among(_pressed))
        _pressed = nullptr;
    _sent.erase(std::remove_if(_sent.begin(), _sent.end(), among), _sent.end());
    _madeToEnter.erase(std::remove_if(_madeToEnter.begin(), _madeToEnter.end(), among), _madeToEnter.end());
    if (among(_drag.clip))
        _drag = Drag();
    if (among(_focus.clip))
        _focus = TextFocus();
    // Listeners whose script was on one go with it, wherever they are.
    std::function<void(Clip&)> drop = [&](Clip& at) {
        if (among(&at))
            return;
        for (auto& [type, list] : at.listeners)
            list.erase(std::remove_if(list.begin(), list.end(),
                           [&among](const Clip::Listener& listener) { return among(listener.scope); }),
                list.end());
        for (const auto& child : at.children)
            if (child)
                drop(*child);
    };
    if (_root)
        drop(*_root);
    for (const auto& clip : _offStage)
        if (clip)
            drop(*clip);
}

void MovieRuntime::freeGone()
{
    // Only once no script is running: one may be at work on them.
    if (_current || _gone.empty())
        return;
    std::vector<std::unique_ptr<Clip>> gone;
    gone.swap(_gone);
    // What was added to them since they went goes with them.
    std::vector<Clip*> going;
    std::function<void(Clip&)> gather = [&](Clip& at) {
        going.push_back(&at);
        for (const auto& child : at.children)
            if (child)
                gather(*child);
    };
    for (const auto& clip : gone)
        if (clip)
            gather(*clip);
    letGo(going);
}

void MovieRuntime::enter(std::vector<Clip*>& entered)
{
    if (_as3)
    {
        // Each clip's children placed and its object made (its class's
        // constructor run) before any of their frame scripts run.
        for (size_t i = 0; i < entered.size(); ++i)
        {
            if (entered[i]->gone)
                continue;
            syncChildren(*entered[i], entered);
            runClass(*entered[i]);
        }
        for (size_t i = 0; i < entered.size(); ++i)
            if (!entered[i]->gone && !entered[i]->button)
                runFrameScripts(*entered[i]);
        settle();
        return;
    }
    // A clip entered for the first time: its onClipEvent handlers taken,
    // load run, its first frame's scripts run, and its own children made.
    for (size_t i = 0; i < entered.size(); ++i)
    {
        // One a script before it took off is passed over.
        if (entered[i]->gone)
            continue;
        Clip& clip = *entered[i];
        if (clip.placed && !clip.button && clip.parent)
        {
            const std::string script = instanceScript(*clip.placed);
            if (!script.empty())
            {
                // Run on the clip with __onClipEvent and __on gathering the
                // handlers, into the clip as long as it lives: a clip's
                // on() and onClipEvent() handlers run on the clip, as a
                // player runs a placed clip's actions.
                const std::shared_ptr<Clip*> life = clip.life;
                const auto gather = [life](const std::vector<as3::ValuePtr>& args) {
                    Clip* gathering = *life;
                    if (gathering && args.size() >= 2)
                        for (const as3::ValuePtr& event : args[0]->elements)
                            gathering->clipEvents[event->toString()] = args[1];
                    return as3::Value::makeUndefined();
                };
                _vm->registerBuiltin("__onClipEvent", gather);
                _vm->registerBuiltin("__on", gather);
                // The handlers' names are the clip's own, as a player has
                // them: _name, _x and _parent are its.
                run(clip.placed, script, clip, clip.name.empty() ? clip.key : clip.name);
            }
            // Handlers a SWF had compiled, by event.
            const CompiledActions* compiled = clip.placed->extraChildren.empty()
                ? nullptr
                : compiledActionsOf(clip.placed, clip.placed->extraChildren);
            if (compiled)
            {
                std::map<std::string, std::vector<const CompiledActions::Block*>> byEvent;
                for (const CompiledActions::Block& block : compiled->blocks)
                    if (block.kind == CompiledActions::Block::Kind::Clip)
                        for (const std::string& event : clipEventNames(block.events, block.key))
                            byEvent[event].push_back(&block);
                ActionsPlayer& player = actionsPlayer(compiled->version);
                for (const auto& [event, blocks] : byEvent)
                    clip.clipEvents[event] = player.handler(blocks,
                        (clip.name.empty() ? clip.key : clip.name) + ", onClipEvent(" + event + ")");
            }
            registeredClass(clip);
            runClipEvent(clip, "initialize");
            runClipEvent(clip, "construct");
            runClipEvent(clip, "load");
            if (_actions)
                _actions->callHandler(clip, "onLoad");
        }
        // Its children are placed before its frame's scripts run, so the
        // scripts find them; its class, when it has one, runs first.
        syncChildren(clip, entered);
        runClass(clip);
        if (!clip.button)
            runFrameScripts(clip);
    }
    settle();
}

void MovieRuntime::goTo(Clip& clip, int frame, bool play)
{
    clip.playing = play;
    if (frame == clip.frame)
        return;
    clip.frame = std::clamp(frame, 0, std::max(0, clip.length - 1));
    display::markDisplayChanged();
    if (std::find(_sent.begin(), _sent.end(), &clip) == _sent.end())
        _sent.push_back(&clip);
}

void MovieRuntime::goToScene(int scene, int frame, bool play)
{
    if (scene < 0 || scene >= sceneCount())
        return;
    if (scene == _scene && _sceneMove.scene < 0)
    {
        goTo(*_root, frame, play);
        return;
    }
    _sceneMove = SceneMove{scene, frame, play};
}

void MovieRuntime::applyScene(int scene, int frame, bool play)
{
    Clip& root = *_root;
    for (auto& child : root.children)
        if (child)
        {
            forget(*child);
            _gone.push_back(std::move(child));
        }
    root.children.clear();
    for (auto& child : root.parked)
        if (child)
        {
            forget(*child);
            _gone.push_back(std::move(child));
        }
    root.parked.clear();
    _scene = scene;
    root.timeline = _scenes[static_cast<size_t>(scene)];
    root.length = lengthOf(root.timeline);
    root.frame = std::clamp(frame, 0, std::max(0, root.length - 1));
    root.playing = play;
}

int MovieRuntime::sceneNamed(const std::string& name) const
{
    for (size_t i = 0; i < _scenes.size(); ++i)
        if (_scenes[i]->name == name)
            return static_cast<int>(i);
    return -1;
}

void MovieRuntime::settle()
{
    if (_as3 || _canvas)
        placeMadeClips();
    // A script sent clips to other frames: their scripts run there, which
    // may send them on again; a chain is cut short at a few.
    for (int round = 0; round < 8 && (!_sent.empty() || _sceneMove.scene >= 0 || !_madeToEnter.empty()); ++round)
    {
        // Clips scripts made: their first frames, entered now.
        if (!_madeToEnter.empty())
        {
            std::vector<Clip*> made;
            made.swap(_madeToEnter);
            for (size_t i = 0; i < made.size(); ++i)
            {
                if (made[i]->gone)
                    continue;
                syncChildren(*made[i], made);
                runClass(*made[i]);
                if (!made[i]->button)
                    runFrameScripts(*made[i]);
            }
        }
        // A move to another scene first: what it takes off the stage is
        // forgotten, and the root goes where it was sent.
        if (_sceneMove.scene >= 0)
        {
            const SceneMove move = _sceneMove;
            _sceneMove = SceneMove();
            applyScene(move.scene, move.frame, move.play);
            if (std::find(_sent.begin(), _sent.end(), _root.get()) == _sent.end())
                _sent.push_back(_root.get());
        }
        std::vector<Clip*> sent;
        sent.swap(_sent);
        std::vector<Clip*> entered;
        for (Clip* clip : sent)
        {
            if (clip->gone)
                continue;
            syncChildren(*clip, entered);
            runFrameScripts(*clip);
        }
        for (size_t i = 0; i < entered.size(); ++i)
        {
            if (entered[i]->gone)
                continue;
            Clip& clip = *entered[i];
            syncChildren(clip, entered);
            runClass(clip);
            if (!clip.button)
                runFrameScripts(clip);
        }
        if (_as3 || _canvas)
            placeMadeClips();
    }
    freeGone();
    if (changed)
        changed();
}

void MovieRuntime::advance(Clip& clip, std::vector<Clip*>& entered, std::vector<Clip*>& scripts)
{
    // Children already about go on; new ones are entered after.
    std::vector<Clip*> before;
    for (const auto& child : clip.children)
        before.push_back(child.get());
    // The main timeline's last frame leads on to the next scene, and the
    // last scene's back to the first.
    if (&clip == _root.get() && clip.playing && sceneCount() > 1 && clip.frame + 1 >= clip.length)
    {
        applyScene((_scene + 1) % sceneCount(), 0, true);
        syncChildren(clip, entered);
        scripts.push_back(&clip);
        return;
    }
    // Time remapped: on the frame its behavior says, whatever it would play.
    const int held = heldFrame && !clip.button && clip.parent && clip.placed ? heldFrame(clip) : -1;
    if (held >= 0)
    {
        const int to = std::clamp(held, 0, std::max(0, clip.length - 1));
        const bool moved = to != clip.frame;
        clip.frame = to;
        if (moved)
            display::markDisplayChanged();
        syncChildren(clip, entered);
        if (moved)
            scripts.push_back(&clip);
    }
    else if (clip.playing && !clip.button && clip.length > 1)
    {
        clip.frame = (clip.frame + 1) % clip.length;
        // What shows has moved on: drawn again.
        display::markDisplayChanged();
        syncChildren(clip, entered);
        scripts.push_back(&clip);
    }
    else
    {
        syncChildren(clip, entered);
    }
    // Scripts may add children or take them off as they go: these are the
    // ones there were, each still on it when its turn comes.
    std::vector<Clip*> now;
    for (const auto& child : clip.children)
        if (child && std::find(before.begin(), before.end(), child.get()) != before.end())
            now.push_back(child.get());
    for (Clip* child : now)
        if (!child->gone && child->parent == &clip)
            advance(*child, entered, scripts);
}

void MovieRuntime::afterEvents()
{
    if (_canvas)
        _canvas->afterEvents();
    settle();
}

void MovieRuntime::seek(int frame)
{
    goTo(*_root, frame, _root->playing);
    settle();
}

void MovieRuntime::step()
{
    // Timers due and loads finished are told of as the frame starts.
    if (_as3)
    {
        _bindings->frameStarted();
        placeMadeClips();
    }
    else if (_actions)
    {
        // ActionScript 1's setInterval and setTimeout.
        _vm->runTimers(static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _started).count()));
        settle();
    }
    // Every playhead on a frame, what the frames place on the display list.
    std::vector<Clip*> entered;
    std::vector<Clip*> scripts;
    advance(*_root, entered, scripts);
    // enterFrame for every clip about, AS2's handlers and AS3's listeners,
    // before the frames' scripts.
    _stage->enterFrame();
    // The objects of the clips the frames placed, made before their scripts.
    if (_as3)
        for (size_t i = 0; i < entered.size(); ++i)
            if (!entered[i]->gone)
                runClass(*entered[i]);
    eachClip([this](Clip& clip) {
        if (!clip.clipEvents.empty())
            runClipEvent(clip, "enterFrame");
        if (_actions && !_as3)
            _actions->callHandler(clip, "onEnterFrame");
        if (clip.listensFor("enterFrame"))
            dispatch(clip, "enterFrame", false);
    });
    _stage->frameConstructed();
    for (Clip* clip : scripts)
        if (!clip->gone)
            runFrameScripts(*clip);
    enter(entered);
    // JavaScript's tick, once the frames' scripts have run, as the stage
    // updates in CreateJS; and where it sent clips.
    if (_canvas)
    {
        _canvas->tick();
        settle();
    }
    _stage->exitFrame();
    _stage->render();
}

void MovieRuntime::keyDown(int code, int character, bool shift, bool ctrl, bool alt)
{
    // The display list's: to the focus, and on into a field typed in.
    _stage->keyDown(static_cast<uint32_t>(code), static_cast<uint32_t>(std::max(0, character)),
        display::Modifiers{shift, ctrl, alt});
    // Typing goes into the focused field first.
    if (_focus.clip && _focus.field)
        type(code, character);
    if (std::find(_keysDown.begin(), _keysDown.end(), code) == _keysDown.end())
        _keysDown.push_back(code);
    _lastKey = code;
    _lastCharacter = character;
    eachClip([&](Clip& clip) {
        if (!clip.clipEvents.empty())
        {
            runClipEvent(clip, "keyDown");
            // onClipEvent(keyPress "<Left>"), as a SWF has it.
            std::vector<std::string> pressed;
            for (const auto& [event, handler] : clip.clipEvents)
                if (event.compare(0, 9, "keyPress ") == 0 && keyPressMatches(event.substr(9), code, character))
                    pressed.push_back(event);
            for (const std::string& event : pressed)
                runClipEvent(clip, event);
        }
        // A button's keyPress handlers: on (keyPress "<Left>").
        if (clip.button)
        {
            const std::map<std::string, as3::ValuePtr> handlers = buttonHandlers(clip);
            for (const auto& [event, handler] : handlers)
            {
                const std::string prefix = "keyPress ";
                if (event.compare(0, prefix.size(), prefix) == 0 &&
                    keyPressMatches(event.substr(prefix.size()), code, character) && clip.parent)
                    runFunction(handler, {}, *clip.parent,
                        "button " + (clip.name.empty() ? clip.key : clip.name) + ", on(" + event + ")", clip.placed);
            }
        }
        if (clip.listensFor("keyDown"))
            dispatch(clip, "keyDown", false, [code, character](const as3::ValuePtr& event) {
                event->setProperty("keyCode", as3::Value::makeNumber(code));
                event->setProperty("charCode", as3::Value::makeNumber(character));
            });
    });
    const std::vector<as3::ValuePtr> listeners = _keyListeners;
    for (const as3::ValuePtr& listener : listeners)
        runFunction(listener->getProperty("onKeyDown"), {}, *_root, "Key listener, onKeyDown");
    settle();
}

void MovieRuntime::keyUp(int code, int character, bool shift, bool ctrl, bool alt)
{
    _stage->keyUp(static_cast<uint32_t>(code), static_cast<uint32_t>(std::max(0, character)),
        display::Modifiers{shift, ctrl, alt});
    _keysDown.erase(std::remove(_keysDown.begin(), _keysDown.end(), code), _keysDown.end());
    eachClip([&](Clip& clip) {
        if (!clip.clipEvents.empty())
            runClipEvent(clip, "keyUp");
        if (clip.listensFor("keyUp"))
            dispatch(clip, "keyUp", false, [code, character](const as3::ValuePtr& event) {
                event->setProperty("keyCode", as3::Value::makeNumber(code));
                event->setProperty("charCode", as3::Value::makeNumber(character));
            });
    });
    const std::vector<as3::ValuePtr> listeners = _keyListeners;
    for (const as3::ValuePtr& listener : listeners)
        runFunction(listener->getProperty("onKeyUp"), {}, *_root, "Key listener, onKeyUp");
    settle();
}

void MovieRuntime::runClipEvent(Clip& clip, const std::string& event)
{
    const auto found = clip.clipEvents.find(event);
    if (found == clip.clipEvents.end())
        return;
    Clip* was = _current;
    const DOMElement* wasSource = _source;
    _current = &clip;
    _source = clip.placed;
    _vm->call(found->second, {}, objectFor(clip), objectFor(clip));
    report((clip.name.empty() ? clip.key : clip.name) + ", onClipEvent(" + event + ")");
    _current = was;
    _source = wasSource;
}

std::map<std::string, as3::ValuePtr>& MovieRuntime::buttonHandlers(Clip& button)
{
    // The handlers kept on the instance, gathered once.
    auto handlers = _buttonHandlers.find(button.placed);
    if (handlers != _buttonHandlers.end())
        return handlers->second;
    std::map<std::string, as3::ValuePtr>& gathered = _buttonHandlers[button.placed];
    const std::string script = button.placed ? instanceScript(*button.placed) : std::string();
    if (!script.empty() && button.parent)
    {
        _vm->registerBuiltin("__on", [&gathered](const std::vector<as3::ValuePtr>& args) {
            if (args.size() >= 2)
                for (const as3::ValuePtr& name : args[0]->elements)
                    gathered[name->toString()] = args[1];
            return as3::Value::makeUndefined();
        });
        run(button.placed, script, *button.parent, "button " + (button.name.empty() ? button.key : button.name));
    }
    const CompiledActions* compiled = button.placed && !button.placed->extraChildren.empty()
        ? compiledActionsOf(button.placed, button.placed->extraChildren)
        : nullptr;
    if (compiled && button.parent)
    {
        std::map<std::string, std::vector<const CompiledActions::Block*>> byEvent;
        for (const CompiledActions::Block& block : compiled->blocks)
            if (block.kind == CompiledActions::Block::Kind::Button)
                for (const std::string& event : buttonEventNames(block.events))
                    byEvent[event].push_back(&block);
        ActionsPlayer& player = actionsPlayer(compiled->version);
        for (const auto& [event, blocks] : byEvent)
            gathered[event] = player.handler(blocks, "button " + (button.name.empty() ? button.key : button.name) +
                ", on(" + event + ")");
    }
    return gathered;
}

void MovieRuntime::runButtonEvent(Clip& button, const std::string& event)
{
    if (!button.placed || !button.parent)
        return;
    std::map<std::string, as3::ValuePtr>& handlers = buttonHandlers(button);
    const auto handler = handlers.find(event);
    if (handler == handlers.end())
        return;
    // A button's handlers run on the timeline the button is on.
    Clip& scope = *button.parent;
    Clip* was = _current;
    const DOMElement* wasSource = _source;
    _current = &scope;
    _source = button.placed;
    _vm->call(handler->second, {}, objectFor(scope), objectFor(scope));
    report("button " + (button.name.empty() ? button.key : button.name) + ", on(" + event + ")");
    _current = was;
    _source = wasSource;
    settle();
}

// The AS3 listeners first: an AS2 handler settles what it changed, which may
// take the clip off the stage.
void MovieRuntime::pointerOver(Clip* button)
{
    if (button == _hovered)
        return;
    Clip* left = _hovered;
    _hovered = button;
    if (_as3 && !_stagePointer)
    {
        // The display list's events, to the clips named.
        const std::shared_ptr<display::DisplayObject> from = left && left->display ? left->display : nullptr;
        const std::shared_ptr<display::DisplayObject> to = button && button->display ? button->display : nullptr;
        _pointerOver = to;
        if (from)
        {
            sendPointer("mouseOut", from.get(), true, to.get());
            sendPointer("rollOut", from.get(), false, to.get());
        }
        if (to)
        {
            sendPointer("mouseOver", to.get(), true, from.get());
            sendPointer("rollOver", to.get(), false, from.get());
        }
    }
    if (left)
    {
        dispatch(*left, "mouseOut", true);
        dispatch(*left, "rollOut", false);
        if (left->button)
        {
            if (_pressed != left)
                buttonSound(*left, 0);
            runButtonEvent(*left, _pressed == left ? "dragOut" : "rollOut");
        }
        else
            runClipEvent(*left, _pressed == left ? "dragOut" : "rollOut");
    }
    // What the pointer moved onto may be gone once those settled.
    if (button && _hovered == button)
    {
        dispatch(*button, "mouseOver", true);
        dispatch(*button, "rollOver", false);
        if (button->button)
        {
            if (_pressed != button)
                buttonSound(*button, 1);
            runButtonEvent(*button, _pressed == button ? "dragOver" : "rollOver");
        }
        else
            runClipEvent(*button, _pressed == button ? "dragOver" : "rollOver");
    }
    settle();
}

void MovieRuntime::pointerDown()
{
    // AS2's mouseDown clip events, wherever the pointer is.
    eachClip([this](Clip& clip) {
        if (!clip.clipEvents.empty())
            runClipEvent(clip, "mouseDown");
    });
    _pressed = _hovered;
    if (_as3 && !_stagePointer)
    {
        const std::shared_ptr<display::DisplayObject> over = _pointerOver.lock();
        _pointerPressed = over;
        if (over)
            sendPointer("mouseDown", over.get(), true);
    }
    if (_pressed)
    {
        Clip& pressed = *_pressed;
        dispatch(pressed, "mouseDown", true);
        if (pressed.button)
        {
            buttonSound(pressed, 2);
            runButtonEvent(pressed, "press");
        }
        else
            runClipEvent(pressed, "press");
    }
    settle();
}

void MovieRuntime::pointerUp()
{
    eachClip([this](Clip& clip) {
        if (!clip.clipEvents.empty())
            runClipEvent(clip, "mouseUp");
    });
    Clip* pressed = _pressed;
    _pressed = nullptr;
    if (_as3 && !_stagePointer)
    {
        const std::shared_ptr<display::DisplayObject> over = _pointerOver.lock();
        const std::shared_ptr<display::DisplayObject> down = _pointerPressed.lock();
        _pointerPressed.reset();
        if (over)
        {
            sendPointer("mouseUp", over.get(), true);
            // A click is a press and a release on the same one.
            if (down == over)
                sendPointer("click", over.get(), true);
        }
    }
    Clip* hovered = _hovered;
    if (hovered)
    {
        dispatch(*hovered, "mouseUp", true);
        // A click is a press and a release on the same one.
        if (pressed == hovered)
            dispatch(*hovered, "click", true);
    }
    if (pressed && pressed->button)
    {
        if (pressed == hovered)
            buttonSound(*pressed, 3);
        runButtonEvent(*pressed, pressed == hovered ? "release" : "releaseOutside");
    }
    else if (pressed)
        runClipEvent(*pressed, pressed == hovered ? "release" : "releaseOutside");
    settle();
}

void MovieRuntime::buttonSound(const Clip& button, int state)
{
    // A sound on a button's state frame plays as the button changes: Up's
    // on rolling out, Over's on rolling over, Down's on pressing and Hit's
    // on releasing.
    if (!button.button || !button.timeline || !frameSound)
        return;
    for (const Layer* layer : button.timeline->layers)
    {
        if (!layer || !plays(*layer))
            continue;
        const Frame* frame = keyframeAt(*layer, state);
        if (frame && frame->index == state && !attributeOf(frame->extraAttributes, "soundName").empty())
            frameSound(*frame);
    }
}

void MovieRuntime::sendPointer(const char* type, display::DisplayObject* target, bool bubbles,
    display::DisplayObject* related)
{
    if (!target)
        return;
    std::shared_ptr<display::DisplayObject> keep = target->shared();
    display::MouseEvent event(type, bubbles);
    event.stageX = _mouseX;
    event.stageY = _mouseY;
    const display::Point local = target->globalToLocal(display::Point{_mouseX, _mouseY});
    event.localX = local.x;
    event.localY = local.y;
    event.buttonDown = _pressed != nullptr || !_pointerPressed.expired();
    event.relatedObject = related;
    target->dispatchEvent(event);
}

void MovieRuntime::placeMadeClips()
{
    // Every clip a script made, and where it is kept now.
    struct Made
    {
        Clip* clip;
        std::vector<std::unique_ptr<Clip>>* in;
    };
    std::vector<Made> made;
    std::function<void(std::vector<std::unique_ptr<Clip>>&)> gather = [&](std::vector<std::unique_ptr<Clip>>& list) {
        for (const auto& clip : list)
        {
            if (!clip)
                continue;
            if (clip->made && !clip->gone)
                made.push_back({clip.get(), &list});
            gather(clip->children);
        }
    };
    gather(_root->children);
    gather(_offStage);
    for (const Made& each : made)
    {
        Clip& clip = *each.clip;
        // The clip whose display object holds this one's.
        Clip* holder = nullptr;
        for (display::DisplayObject* at = clip.display ? clip.display->parent() : nullptr; at && !holder; at = at->parent())
            if (at->clip && at->clip != &clip &&
                (at->kind() == display::ObjectKind::MovieClip || at->kind() == display::ObjectKind::SimpleButton))
                holder = at->clip;
        // Not into what it holds itself.
        for (Clip* at = holder; at; at = at->parent)
            if (at == &clip)
                holder = nullptr;
        if (holder == clip.parent && (holder || each.in == &_offStage))
            continue;
        auto found = std::find_if(each.in->begin(), each.in->end(),
            [&clip](const std::unique_ptr<Clip>& c) { return c.get() == &clip; });
        if (found == each.in->end())
            continue;
        std::unique_ptr<Clip> owned = std::move(*found);
        each.in->erase(found);
        clip.parent = holder;
        clip.placement.known = false;
        (holder ? holder->children : _offStage).push_back(std::move(owned));
    }
}

int MovieRuntime::buttonFrame(const Clip& button) const
{
    if (&button == _hovered)
        return _pressed == &button ? 2 : 1;
    return 0;
}

Clip* MovieRuntime::clipOf(const Clip& parent, const Layer& layer, const SymbolInstance& instance) const
{
    if (!parent.timeline || instance.symbolType == SymbolType::Graphic)
        return nullptr;
    const Frame* frame = keyframeAt(layer, parent.frame);
    if (!frame)
        return nullptr;
    return parent.child(keyOf(*parent.timeline, layer, *frame, instance));
}

int MovieRuntime::frameOf(const Clip& clip, const as3::ValuePtr& target) const
{
    return frameIn(clip.timeline, target);
}

int MovieRuntime::frameIn(const Timeline* timeline, const as3::ValuePtr& target) const
{
    if (!target || target->isUndefined())
        return -1;
    if (target->isNumber())
        return static_cast<int>(target->numberValue) - 1;
    const std::string text = target->toString();
    // A number given as a string is a frame number.
    const double number = target->toNumber();
    if (!std::isnan(number))
        return static_cast<int>(number) - 1;
    if (!timeline)
        return -1;
    for (const Layer* layer : timeline->layers)
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            if (frame && frame->name == text && frame->labelType != "comment")
                return frame->index;
    return -1;
}

Clip* MovieRuntime::resolve(Clip& from, const as3::ValuePtr& target)
{
    if (!target || target->isUndefined())
        return &from;
    if (target->isObject() && target->host)
    {
        // One of the runtime's own clips.
        std::function<Clip*(Clip&)> find = [&](Clip& clip) -> Clip* {
            if (clip.object == target)
                return &clip;
            for (const auto& child : clip.children)
                if (Clip* found = child ? find(*child) : nullptr)
                    return found;
            return nullptr;
        };
        return find(*_root);
    }
    return resolve(from, target->toString());
}

Clip* MovieRuntime::resolve(Clip& from, const std::string& target)
{
    if (target.empty() || target == "this")
        return &from;
    Clip* at = &from;
    std::string path = target;
    // Flash 4 slash paths and Flash 5 dot paths alike.
    if (path[0] == '/')
    {
        at = _root.get();
        path = path.substr(1);
    }
    std::replace(path.begin(), path.end(), '/', '.');
    std::stringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '.'))
    {
        if (part.empty())
            continue;
        if (part == "_root" || part == "_level0")
            at = _root.get();
        else if (part == "_parent" || part == "..")
            at = at->parent ? at->parent : at;
        else if (part == "this")
            continue;
        else
            at = at->named(part);
        if (!at)
            return nullptr;
    }
    return at;
}

as3::ValuePtr MovieRuntime::property(Clip& clip, const std::string& name)
{
    using as3::Value;
    const Transform placed = clip.placed ? clip.placed->transform : Transform();
    const Transform shown = clip.transform(placed);
    if (name == "_x")
        return Value::makeNumber(shown.tx);
    if (name == "_y")
        return Value::makeNumber(shown.ty);
    if (name == "_visible")
        return Value::makeBoolean(clip.shown());
    if (name == "_alpha")
        return Value::makeNumber(clip.display && clip.display->alphaScripted()
                ? clip.display->alpha() * 100.0
                : (clip.placed ? clip.placed->colorTransform.alphaMultiplier * 100.0 : 100.0));
    if (name == "_xscale")
        return Value::makeNumber(scaleOf(shown.m11, shown.m12) * 100.0);
    if (name == "_yscale")
        return Value::makeNumber(scaleOf(shown.m21, shown.m22) * 100.0);
    if (name == "_rotation")
        return Value::makeNumber(std::atan2(shown.m12, shown.m11) * 180.0 / kPi);
    if (name == "_currentframe")
        return Value::makeNumber(clip.frame + 1);
    if (name == "_totalframes" || name == "_framesloaded")
        return Value::makeNumber(clip.length);
    if (name == "_name")
        return Value::makeString(clip.name);
    // A text field on its timeline, by its instance name.
    if (fieldNamed(clip.timeline, clip.frame, name))
        return textFieldObject(clip, name);
    // The pointer, in its space.
    if (name == "_xmouse" || name == "_ymouse" || name == "mouseX" || name == "mouseY")
    {
        double px = _mouseX, py = _mouseY;
        stageToClip(clip, px, py);
        return Value::makeNumber(name == "_xmouse" || name == "mouseX" ? px : py);
    }
    if (name == "_width" || name == "width" || name == "_height" || name == "height")
    {
        const Clip::Placement& box = clip.placement;
        const bool across = name == "_width" || name == "width";
        return Value::makeNumber(box.known ? (across ? box.right - box.left : box.bottom - box.top) : 0.0);
    }
    if (name == "_droptarget" || name == "dropTarget")
    {
        const as3::ValuePtr under = objectFor(clip)->properties.count("__dropTarget")
            ? as3::ValuePtr(objectFor(clip)->properties["__dropTarget"])
            : Value::makeNull();
        if (name == "dropTarget")
            return under;
        Clip* target = clipOfValue(under);
        return Value::makeString(target ? slashPath(*target) : std::string());
    }
    if (name == "_parent")
        return clip.parent ? objectFor(*clip.parent) : Value::makeUndefined();
    if (name == "_root")
        return objectFor(*_root);
    // ActionScript 3's names for the same: alpha and the scales are
    // fractions there, not percents.
    if (name == "x")
        return Value::makeNumber(shown.tx);
    if (name == "y")
        return Value::makeNumber(shown.ty);
    if (name == "visible")
        return Value::makeBoolean(clip.shown());
    if (name == "alpha")
        return Value::makeNumber(clip.display && clip.display->alphaScripted()
                ? clip.display->alpha()
                : (clip.placed ? clip.placed->colorTransform.alphaMultiplier : 1.0));
    if (name == "scaleX")
        return Value::makeNumber(scaleOf(shown.m11, shown.m12));
    if (name == "scaleY")
        return Value::makeNumber(scaleOf(shown.m21, shown.m22));
    if (name == "rotation")
        return Value::makeNumber(std::atan2(shown.m12, shown.m11) * 180.0 / kPi);
    if (name == "currentFrame")
        return Value::makeNumber(clip.frame + 1);
    if (name == "totalFrames" || name == "framesLoaded")
        return Value::makeNumber(clip.length);
    if (name == "name")
        return Value::makeString(clip.name);
    if (name == "parent")
        return clip.parent ? objectFor(*clip.parent) : Value::makeNull();
    if (name == "root" || name == "stage")
        return objectFor(*_root);
    if (name == "numChildren")
        return Value::makeNumber(static_cast<double>(clip.children.size()));
    if (name == "currentLabel" || name == "currentFrameLabel")
    {
        std::string label;
        for (const Layer* layer : clip.timeline ? clip.timeline->layers : std::vector<Layer*>())
        {
            const Frame* frame = layer ? keyframeAt(*layer, clip.frame) : nullptr;
            if (frame && !frame->name.empty() && frame->labelType != "comment" &&
                (name == "currentLabel" || frame->index == clip.frame))
                label = frame->name;
        }
        return label.empty() ? Value::makeNull() : Value::makeString(label);
    }
    // The methods below are bound to the clip through its life, so one a
    // script keeps after the clip is freed does nothing.
    const std::shared_ptr<Clip*> life = clip.life;
    // addEventListener(type, listener): the listener's names are looked up
    // where the script that added it runs.
    if (name == "addEventListener" || name == "removeEventListener" || name == "hasEventListener")
    {
        const std::string which = name;
        return Value::makeNativeFunction([this, life, which](const std::vector<as3::ValuePtr>& args) {
            Clip* target = *life;
            if (!target)
                return Value::makeUndefined();
            const std::string type = args.empty() ? std::string() : args[0]->toString();
            std::vector<Clip::Listener>& list = target->listeners[type];
            if (which == "hasEventListener")
                return Value::makeBoolean(!list.empty());
            const as3::ValuePtr function = args.size() > 1 ? args[1] : nullptr;
            const auto same = [&function](const Clip::Listener& listener) { return listener.function == function; };
            if (which == "removeEventListener")
                list.erase(std::remove_if(list.begin(), list.end(), same), list.end());
            else if (function && function->isFunction() && std::none_of(list.begin(), list.end(), same))
                list.push_back({function, _current ? _current : target, _source});
            if (changed)
                changed();
            return Value::makeUndefined();
        });
    }
    // Dragging, and what is under a point or another clip.
    if (name == "startDrag")
    {
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>& args) {
            Clip* self = *life;
            if (!self)
                return Value::makeUndefined();
            std::vector<double> bounds;
            if (args.size() >= 5)
            {
                // AS2: startDrag(lockCenter, left, top, right, bottom).
                for (size_t i = 1; i < 5; ++i)
                    bounds.push_back(args[i]->toNumber());
            }
            else if (args.size() >= 2 && args[1]->isObject())
            {
                // ActionScript 3: startDrag(lockCenter, new Rectangle(...)).
                const double x = args[1]->getProperty("x")->toNumber();
                const double y = args[1]->getProperty("y")->toNumber();
                bounds = {x, y, x + args[1]->getProperty("width")->toNumber(), y + args[1]->getProperty("height")->toNumber()};
            }
            startDrag(*self, !args.empty() && args[0]->toBoolean(), bounds);
            return Value::makeUndefined();
        });
    }
    if (name == "stopDrag")
        return Value::makeNativeFunction([this](const std::vector<as3::ValuePtr>&) {
            stopDrag();
            return Value::makeUndefined();
        });
    if (name == "hitTest" || name == "hitTestPoint" || name == "hitTestObject")
    {
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>& args) {
            Clip* self = *life;
            if (!self)
                return Value::makeBoolean(false);
            // hitTest(x, y, shapeFlag) and hitTestPoint: a point on the
            // stage; hitTest(target) and hitTestObject: another clip.
            if (args.size() >= 2 && args[0]->isNumber())
                return Value::makeBoolean(
                    hits(*self, args[0]->toNumber(), args[1]->toNumber(), args.size() > 2 && args[2]->toBoolean()));
            Clip* other = args.empty() ? nullptr : (args[0]->host ? clipOfValue(args[0]) : resolve(*self, args[0]));
            return Value::makeBoolean(other && overlaps(*self, *other));
        });
    }
    // Clips made by scripts.
    if (name == "attachMovie")
    {
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>& args) {
            Clip* self = *life;
            if (!self || self->gone)
                return Value::makeUndefined();
            const Symbol* symbol = linked(args.empty() ? std::string() : args[0]->toString());
            if (!symbol)
                return Value::makeUndefined();
            const std::string newName = args.size() > 1 ? args[1]->toString() : std::string();
            // One of the same name is replaced.
            if (Clip* old = self->named(newName); old && old->made)
                takeOff(*old, false);
            Clip* made = makeClip(self, *symbol, newName);
            const as3::ValuePtr object = objectFor(*made);
            if (args.size() > 3 && args[3]->isObject())
                for (const auto& [key, value] : args[3]->properties)
                    object->setProperty(key, value);
            return object;
        });
    }
    if (name == "duplicateMovieClip")
    {
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>& args) {
            Clip* self = *life;
            if (!self || self->gone || !self->symbol || !self->parent)
                return Value::makeUndefined();
            Clip* made = makeClip(self->parent, *self->symbol, args.empty() ? std::string() : args[0]->toString());
            made->made->transform = self->placed ? self->placed->transform : Transform();
            if (made->display && self->display)
            {
                made->display->setPlacedMatrix(toMatrix(made->made->transform));
                if (self->display->transformScripted())
                    made->display->setMatrix(self->display->matrix());
                if (self->display->alphaScripted())
                    made->display->setAlpha(self->display->alpha());
                made->display->setVisible(self->display->visible());
            }
            return objectFor(*made);
        });
    }
    if (name == "removeMovieClip")
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>&) {
            if (Clip* self = *life; self && self->made)
                takeOff(*self, false);
            return Value::makeUndefined();
        });
    if (name == "addChild" || name == "addChildAt")
    {
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>& args) {
            Clip* self = *life;
            Clip* child = args.empty() || !self ? nullptr : clipOfValue(args[0]);
            if (!child || !child->made || child->gone || self->gone)
                return args.empty() ? Value::makeNull() : args[0];
            // Not into itself or what is in it: the clip would own itself.
            for (const Clip* at = self; at; at = at->parent)
                if (at == child)
                    return args[0];
            if (child->parent != self)
            {
                takeOff(*child, true);
                auto found = std::find_if(_offStage.begin(), _offStage.end(),
                    [child](const std::unique_ptr<Clip>& c) { return c.get() == child; });
                if (found != _offStage.end())
                {
                    std::unique_ptr<Clip> owned = std::move(*found);
                    _offStage.erase(found);
                    owned->parent = self;
                    self->children.push_back(std::move(owned));
                }
            }
            // On the display list, on top (or at the index asked).
            if (self->display && self->display->isContainer() && child->display)
            {
                auto* container = static_cast<display::DisplayObjectContainer*>(self->display.get());
                if (args.size() > 1 && args[1]->isNumber())
                    container->addChildAt(child->display,
                        std::max(0, std::min(container->numChildren(), static_cast<int>(args[1]->toNumber()))));
                else
                    container->addChild(child->display);
            }
            if (changed)
                changed();
            return args[0];
        });
    }
    if (name == "removeChild")
    {
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>& args) {
            Clip* self = *life;
            Clip* child = args.empty() || !self ? nullptr : clipOfValue(args[0]);
            if (child && child->made && child->parent == self)
                takeOff(*child, true);
            return args.empty() ? Value::makeNull() : args[0];
        });
    }
    if (name == "getChildByName")
    {
        return Value::makeNativeFunction([this, life](const std::vector<as3::ValuePtr>& args) {
            Clip* target = *life;
            Clip* child = args.empty() || !target ? nullptr : target->named(args[0]->toString());
            return child ? objectFor(*child) : Value::makeNull();
        });
    }
    // Its timeline methods, bound to it.
    const auto method = [life](std::function<void(Clip&, const std::vector<as3::ValuePtr>&)> act) {
        return Value::makeNativeFunction([act, life](const std::vector<as3::ValuePtr>& args) {
            if (Clip* target = *life)
                act(*target, args);
            return Value::makeUndefined();
        });
    };
    if (name == "stop")
        return method([](Clip& c, const std::vector<as3::ValuePtr>&) { c.playing = false; });
    if (name == "play")
        return method([](Clip& c, const std::vector<as3::ValuePtr>&) { c.playing = true; });
    if (name == "gotoAndStop" || name == "gotoAndPlay")
    {
        const bool play = name == "gotoAndPlay";
        return method([this, play](Clip& c, const std::vector<as3::ValuePtr>& args) {
            const int frame = frameOf(c, args.size() >= 2 ? args[1] : (args.empty() ? nullptr : args[0]));
            if (frame >= 0)
                goTo(c, frame, play);
            else
                c.playing = play;
        });
    }
    if (name == "nextFrame")
        return method([this](Clip& c, const std::vector<as3::ValuePtr>&) { goTo(c, std::min(c.frame + 1, c.length - 1), false); });
    if (name == "prevFrame")
        return method([this](Clip& c, const std::vector<as3::ValuePtr>&) { goTo(c, std::max(c.frame - 1, 0), false); });
    // A child clip by its instance name.
    if (Clip* child = clip.named(name))
        return objectFor(*child);
    return nullptr;
}

bool MovieRuntime::setProperty(Clip& clip, const std::string& name, const as3::ValuePtr& value)
{
    const auto number = [&value]() { return value ? value->toNumber() : 0.0; };
    if (!clip.display)
        return false;
    display::DisplayObject& shown = *clip.display;
    // A NaN _x or _y lets the timeline place it again.
    const auto setX = [&](double x) {
        if (std::isnan(x))
            shown.clearScriptedX();
        else
            shown.setX(x);
    };
    const auto setY = [&](double y) {
        if (std::isnan(y))
            shown.clearScriptedY();
        else
            shown.setY(y);
    };
    if (name == "_visible")
    {
        // "0" and "1", as Flash 4 scripts give it, are numbers here.
        shown.setVisible(value && (value->isBoolean() ? value->boolValue : !std::isnan(number()) && number() != 0.0));
    }
    else if (name == "_x" || name == "x")
        setX(number());
    else if (name == "_y" || name == "y")
        setY(number());
    else if (name == "_xscale")
        shown.setScaleX(number() / 100.0);
    else if (name == "_yscale")
        shown.setScaleY(number() / 100.0);
    else if (name == "_rotation" || name == "rotation")
        shown.setRotation(number());
    else if (name == "_alpha")
        shown.setAlpha(number() / 100.0);
    else if (name == "visible")
        shown.setVisible(value && value->toBoolean());
    else if (name == "scaleX")
        shown.setScaleX(number());
    else if (name == "scaleY")
        shown.setScaleY(number());
    else if (name == "alpha")
        shown.setAlpha(number());
    else if (name == "focus")
    {
        // ActionScript 3's stage.focus = name_txt.
        const auto field = value && value->host ? std::dynamic_pointer_cast<TextFieldObject>(value->host) : nullptr;
        if (field && field->clip())
            focusText(field->clip(), fieldNamed(field->clip()->timeline, field->clip()->frame, field->name()));
        else
            focusText(nullptr, nullptr);
    }
    else
        return false;
    if (changed)
        changed();
    return true;
}

} // namespace play

} // namespace fla
