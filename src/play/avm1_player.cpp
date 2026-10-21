#include "avm1_player.h"

#include "movie_runtime.h"
#include "as2_library.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/timeline.h"

#include "avm1/avm1.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"

#include <tinyxml2.h>

#include "display/display_object.h"
#include "display/backend.h"
#include "display/stage.h"
#include "display/text_field.h"
#include <limits>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>

namespace fla {

namespace play {

namespace {

std::vector<uint8_t> fromHex(const char* text)
{
    std::vector<uint8_t> bytes;
    if (!text)
        return bytes;
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    int high = -1;
    for (const char* at = text; *at; ++at)
    {
        const int value = digit(*at);
        if (value < 0)
            continue;
        if (high < 0)
            high = value;
        else
        {
            bytes.push_back(static_cast<uint8_t>(high * 16 + value));
            high = -1;
        }
    }
    return bytes;
}

/// A key as a keyPress names it, from SWF's key codes.
std::string keyName(int key)
{
    static const std::pair<int, const char*> named[] = {{1, "<Left>"}, {2, "<Right>"}, {3, "<Home>"}, {4, "<End>"},
        {5, "<Insert>"}, {6, "<Delete>"}, {8, "<Backspace>"}, {13, "<Enter>"}, {14, "<Up>"}, {15, "<Down>"},
        {16, "<PageUp>"}, {17, "<PageDown>"}, {18, "<Tab>"}, {19, "<Escape>"}, {32, "<Space>"}};
    for (const auto& [code, name] : named)
        if (code == key)
            return name;
    return key > 32 && key < 127 ? std::string(1, static_cast<char>(key)) : std::string();
}

bool sameIgnoringCase(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

std::shared_ptr<const CompiledActions> compiledActionsIn(const std::vector<std::string>& extraChildren)
{
    for (const std::string& text : extraChildren)
    {
        if (text.find("<CompiledActions") == std::string::npos)
            continue;
        tinyxml2::XMLDocument doc;
        if (doc.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS)
            continue;
        const tinyxml2::XMLElement* holder = doc.FirstChildElement("CompiledActions");
        if (!holder)
            continue;
        auto compiled = std::make_shared<CompiledActions>();
        compiled->version = holder->IntAttribute("swfVersion", 8);
        for (const tinyxml2::XMLElement* element = holder->FirstChildElement(); element;
             element = element->NextSiblingElement())
        {
            CompiledActions::Block block;
            const std::string name = element->Name();
            if (name == "DoAction")
                block.kind = CompiledActions::Block::Kind::Frame;
            else if (name == "DoInitAction")
                block.kind = CompiledActions::Block::Kind::Init;
            else if (name == "ClipAction")
                block.kind = CompiledActions::Block::Kind::Clip;
            else if (name == "ButtonAction")
                block.kind = CompiledActions::Block::Kind::Button;
            else
                continue;
            if (const char* symbol = element->Attribute("symbol"))
                block.symbol = symbol;
            block.events = element->UnsignedAttribute(block.kind == CompiledActions::Block::Kind::Button ? "conditions" : "events");
            block.key = element->IntAttribute("key");
            block.actions = std::make_shared<const std::vector<uint8_t>>(fromHex(element->GetText()));
            compiled->blocks.push_back(std::move(block));
        }
        return compiled;
    }
    return nullptr;
}

std::vector<std::string> clipEventNames(uint32_t events, int key)
{
    static const char* const names[] = {"load", "enterFrame", "unload", "mouseMove", "mouseDown", "mouseUp", "keyDown",
        "keyUp", "data", "initialize", "press", "release", "releaseOutside", "rollOver", "rollOut", "dragOver",
        "dragOut", "keyPress", "construct"};
    std::vector<std::string> out;
    for (size_t bit = 0; bit < std::size(names); ++bit)
    {
        if (!(events & (1u << bit)))
            continue;
        if (std::strcmp(names[bit], "keyPress") == 0)
        {
            const std::string named = keyName(key);
            if (!named.empty())
                out.push_back("keyPress " + named);
        }
        else
            out.push_back(names[bit]);
    }
    return out;
}

std::vector<std::string> buttonEventNames(uint32_t conditions)
{
    // IdleToOverUp, OverUpToIdle, OverUpToOverDown, OverDownToOverUp,
    // OverDownToOutDown, OutDownToOverDown, OutDownToIdle, and the menu
    // kinds IdleToOverDown and OverDownToIdle.
    static const char* const names[] = {"rollOver", "rollOut", "press", "release", "dragOut", "dragOver",
        "releaseOutside", "dragOver", "dragOut"};
    std::vector<std::string> out;
    for (size_t bit = 0; bit < std::size(names); ++bit)
        if ((conditions & (1u << bit)) && std::find(out.begin(), out.end(), names[bit]) == out.end())
            out.push_back(names[bit]);
    const std::string named = keyName(static_cast<int>(conditions >> 9));
    if (!named.empty())
        out.push_back("keyPress " + named);
    return out;
}

/// What the interpreter asks of the movie, answered by the runtime: its
/// clips by their objects, their timelines, the pointer and the player.
class ActionsHost : public as3::avm1::Host
{
public:
    ActionsHost(MovieRuntime& runtime, int version)
        : _runtime(runtime)
        , _version(version)
    {}

    Clip* clipOf(const as3::Val& value) const { return MovieRuntime::clipOfObject(value); }
    as3::Val objectOf(Clip* clip) { return clip ? as3::Val(_runtime.objectFor(*clip)) : as3::Val(); }

    void trace(const std::string& text) override
    {
        if (_runtime.trace)
            _runtime.trace(text);
    }
    void error(const std::string& message) override { _runtime.tell(message, _runtime._source, 0); }
    bool isClip(const as3::Val& value) override
    {
        return clipOf(value) != nullptr || textFieldOfObject(value) != nullptr;
    }
    std::string className(const as3::Val& value) override
    {
        if (textFieldOfObject(value))
            return "TextField";
        Clip* clip = clipOf(value);
        return clip && clip->button ? "Button" : "MovieClip";
    }
    /// The clip a text field is on, or null.
    static Clip* ownerOfField(const as3::Val& value)
    {
        const std::shared_ptr<display::TextField> field = textFieldOfObject(value);
        return field && field->parent() ? field->parent()->clip : nullptr;
    }
    as3::Val root(const as3::Val&) override { return objectOf(_runtime._root.get()); }
    as3::Val level(int number) override { return number == 0 ? objectOf(_runtime._root.get()) : as3::Val(); }
    as3::Val parent(const as3::Val& value) override
    {
        if (Clip* owner = ownerOfField(value))
            return objectOf(owner);
        Clip* clip = clipOf(value);
        return clip ? objectOf(clip->parent) : as3::Val();
    }
    as3::Val child(const as3::Val& value, const std::string& name) override
    {
        Clip* clip = clipOf(value);
        if (!clip)
            return as3::Val();
        if (Clip* found = clip->named(name))
            return objectOf(found);
        // Before SWF 7, names are found whatever their case.
        if (_version < 7)
            for (const auto& each : clip->children)
                if (each && !each->gone && sameIgnoringCase(each->name, name))
                    return objectOf(each.get());
        if (_runtime._actions)
            return _runtime._actions->textFieldNamed(*clip, name);
        return as3::Val();
    }
    std::string targetPath(const as3::Val& value) override
    {
        if (const std::shared_ptr<display::TextField> field = textFieldOfObject(value))
        {
            Clip* owner = ownerOfField(value);
            return owner ? targetPath(objectOf(owner)) + "." + field->name() : field->name();
        }
        Clip* clip = clipOf(value);
        if (!clip)
            return std::string();
        std::string path = _runtime.slashPath(*clip);
        std::replace(path.begin(), path.end(), '/', '.');
        return path == "." ? std::string("_level0") : "_level0" + path;
    }
    void childNames(const as3::Val& value, std::vector<std::string>& names) override
    {
        Clip* clip = clipOf(value);
        if (!clip)
            return;
        for (const auto& each : clip->children)
            if (each && !each->gone && !each->name.empty())
                names.push_back(each->name);
        if (clip->display && clip->display->isContainer())
            for (const auto& child : static_cast<display::DisplayObjectContainer*>(clip->display.get())->children())
                if (child && child->kind() == display::ObjectKind::TextField && !child->name().empty())
                    names.push_back(child->name());
    }

    void gotoFrame(const as3::Val& value, int frame, bool play) override
    {
        Clip* clip = clipOf(value);
        if (!clip)
            return;
        // The main timeline's frames count through its scenes.
        if (clip == _runtime._root.get() && _runtime.sceneCount() > 1)
        {
            int start = 0;
            for (int scene = 0; scene < _runtime.sceneCount(); ++scene)
            {
                const int length = std::max(1, lengthOfScene(scene));
                if (frame < start + length || scene + 1 == _runtime.sceneCount())
                {
                    _runtime.goToScene(scene, std::max(0, frame - start), play);
                    return;
                }
                start += length;
            }
        }
        _runtime.goTo(*clip, std::max(0, frame), play);
    }
    bool gotoLabel(const as3::Val& value, const std::string& label, bool play) override
    {
        Clip* clip = clipOf(value);
        if (!clip)
            return false;
        const as3::ValuePtr name = as3::Value::makeString(label);
        const int frame = _runtime.frameOf(*clip, name);
        if (frame >= 0)
        {
            _runtime.goTo(*clip, frame, play);
            return true;
        }
        if (clip == _runtime._root.get())
            for (int scene = 0; scene < _runtime.sceneCount(); ++scene)
            {
                const int there = _runtime.frameIn(_runtime._scenes[static_cast<size_t>(scene)], name);
                if (there >= 0)
                {
                    _runtime.goToScene(scene, there, play);
                    return true;
                }
            }
        return false;
    }
    void setPlaying(const as3::Val& value, bool playing) override
    {
        if (Clip* clip = clipOf(value))
            clip->playing = playing;
    }
    void nextFrame(const as3::Val& value) override
    {
        if (Clip* clip = clipOf(value))
            _runtime.goTo(*clip, std::min(clip->frame + 1, clip->length - 1), false);
    }
    void prevFrame(const as3::Val& value) override
    {
        if (Clip* clip = clipOf(value))
            _runtime.goTo(*clip, std::max(clip->frame - 1, 0), false);
    }
    int framesLoaded(const as3::Val& value) override
    {
        Clip* clip = clipOf(value);
        return clip ? clip->length : 0;
    }
    void callFrame(const as3::Val& value, const as3::Val& frame) override
    {
        Clip* clip = clipOf(value);
        if (!clip || !clip->timeline)
            return;
        const int index = _runtime.frameOf(*clip, frame);
        if (index < 0)
            return;
        _runtime.runFrameActions(*clip, index);
    }

    // CloneSprite's depth counts from 0, the methods' from -16384.
    void cloneSprite(const as3::Val& value, const std::string& name, int depth) override
    {
        if (Clip* clip = clipOf(value))
            _runtime.duplicateClip(*clip, name, static_cast<int>(int64_t(depth) - 16384));
    }
    void removeSprite(const as3::Val& value) override
    {
        if (Clip* clip = clipOf(value); clip && _runtime._actions)
            _runtime._actions->removeIfScripted(*clip);
    }
    void startDrag(const as3::Val& value, bool lockCenter, bool constrain, double left, double top, double right,
        double bottom) override
    {
        if (Clip* clip = clipOf(value))
            _runtime.startDrag(*clip, lockCenter,
                constrain ? std::vector<double>{left, top, right, bottom} : std::vector<double>());
    }
    void endDrag() override { _runtime.stopDrag(); }
    void getURL(const std::string& url, const std::string& target, int method, bool loadTarget,
        bool loadVariables) override
    {
        (void)method;
        if (url.size() >= 10 && sameIgnoringCase(url.substr(0, 10), "FSCommand:"))
        {
            // As the fscommand function tells of it.
            if (_runtime.trace)
                _runtime.trace("fscommand(" + url.substr(10) + ", " + target + ")");
            return;
        }
        if (loadTarget || loadVariables || target.rfind("_level", 0) == 0)
        {
            if (_runtime.notice)
                _runtime.notice("The movie asked to load " + url + ", which playing it here does not do.");
            return;
        }
        if (_runtime.getURL)
            _runtime.getURL(url, target);
    }
    void stopSounds() override
    {
        if (_runtime.stopAllSounds)
            _runtime.stopAllSounds();
    }
    double time() override
    {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _runtime._started).count());
    }
    // A panel compiled elsewhere reaches Phoenix's JSFL, as one Phoenix
    // compiled does.
    std::string mmExecute(const std::string& code) override
    {
        return _runtime.mmExecute ? _runtime.mmExecute(code) : std::string();
    }
    void targetChanged(const as3::Val& value) override
    {
        if (Clip* clip = clipOf(value))
            _runtime._current = clip;
    }

private:
    MovieRuntime& _runtime;
    int _version;

    int lengthOfScene(int scene) const
    {
        const Timeline* timeline = _runtime._scenes[static_cast<size_t>(scene)];
        int length = 0;
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                if (frame)
                    length = std::max(length, frame->index + frame->duration);
        return length;
    }
};

ActionsPlayer::ActionsPlayer(MovieRuntime& runtime, as3::VM& vm, int version)
    : _runtime(runtime)
    , _vm(vm)
    , _host(std::make_unique<ActionsHost>(runtime, version))
    , _interpreter(std::make_unique<as3::avm1::Interpreter>(vm, *_host, version))
{
    // The player's own ActionScript 1 and 2 classes: XML from SWF 5,
    // SharedObject from 6, flash.geom and the rest of flash.* from 8.
    if (version >= 5)
    {
        // Compiled once; what stops it compiling is a mistake in Phoenix,
        // told as the movie starts.
        static std::string problems;
        static const std::shared_ptr<const std::vector<uint8_t>> library = []() {
            as3::avm1::Compiler compiler;
            auto bytes = std::make_shared<std::vector<uint8_t>>(compiler.compileScript(as2LibrarySource(), "library"));
            for (const std::string& error : compiler.errors())
                problems += error + "\n";
            return std::shared_ptr<const std::vector<uint8_t>>(bytes);
        }();
        if (!problems.empty())
            _runtime.tell("Phoenix's ActionScript 2 library does not compile: " + problems, nullptr, 0);
        if (!library->empty() && _runtime._root)
        {
            _vm.setGlobal("$phoenix", natives());
            _interpreter->run(library, _runtime.objectFor(*_runtime._root), "library");
            _vm.setGlobal("$phoenix", as3::Val());
        }
    }
    // The fields placed before the movie's first actions ran.
    std::function<void(display::DisplayObject&)> visit = [&](display::DisplayObject& object) {
        if (object.kind() == display::ObjectKind::TextField && object.timeline)
            fieldPlaced(std::static_pointer_cast<display::TextField>(object.shared()));
        if (object.isContainer())
            for (const auto& child : static_cast<display::DisplayObjectContainer&>(object).children())
                if (child)
                    visit(*child);
    };
    if (_runtime._stage)
        visit(*_runtime._stage);
}

namespace {

/// A color transform as a display object holds it: multipliers in 256ths
/// and offsets whole, each in 16 bits.
struct FixedColor
{
    int16_t multiply[4];
    int16_t add[4];
};

int16_t saturated(double value)
{
    if (std::isnan(value))
        return 0;
    return static_cast<int16_t>(std::clamp(std::trunc(value), -32768.0, 32767.0));
}

FixedColor fixedOf(const display::ColorTransform& c)
{
    const double multipliers[4] = {c.redMultiplier, c.greenMultiplier, c.blueMultiplier, c.alphaMultiplier};
    const double offsets[4] = {c.redOffset, c.greenOffset, c.blueOffset, c.alphaOffset};
    FixedColor out{};
    for (int i = 0; i < 4; ++i)
    {
        out.multiply[i] = saturated(multipliers[i] * 256.0);
        out.add[i] = saturated(offsets[i]);
    }
    return out;
}

display::ColorTransform colorOf(const FixedColor& f)
{
    display::ColorTransform c;
    double* multipliers[4] = {&c.redMultiplier, &c.greenMultiplier, &c.blueMultiplier, &c.alphaMultiplier};
    double* offsets[4] = {&c.redOffset, &c.greenOffset, &c.blueOffset, &c.alphaOffset};
    for (int i = 0; i < 4; ++i)
    {
        *multipliers[i] = f.multiply[i] / 256.0;
        *offsets[i] = f.add[i];
    }
    return c;
}

///  outer applied after  inner, in the 16-bit arithmetic the player
/// uses, which wraps.
FixedColor combined(const FixedColor& outer, const FixedColor& inner)
{
    FixedColor out{};
    for (int i = 0; i < 4; ++i)
    {
        out.multiply[i] = static_cast<int16_t>((int32_t(outer.multiply[i]) * inner.multiply[i]) >> 8);
        out.add[i] = static_cast<int16_t>(outer.add[i] + ((int32_t(outer.multiply[i]) * inner.add[i]) >> 8));
    }
    return out;
}

} // namespace

as3::Val ActionsPlayer::natives()
{
    // What the library's classes ask of the player about movie clips.
    MovieRuntime& runtime = _runtime;
    as3::VM& vm = _vm;
    const as3::Val natives = vm.newObject();
    const auto define = [&](const char* name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };
    const auto clipOf = [](const as3::Val& value) { return MovieRuntime::clipOfObject(value); };
    const auto numbers = [&vm](std::initializer_list<double> values) {
        std::vector<as3::Val> list;
        for (double value : values)
            list.push_back(as3::Val::number(value));
        return vm.newArray(std::move(list));
    };
    const auto colors = [numbers](const display::ColorTransform& c) {
        return numbers({c.redMultiplier, c.greenMultiplier, c.blueMultiplier, c.alphaMultiplier, c.redOffset,
            c.greenOffset, c.blueOffset, c.alphaOffset});
    };
    const auto arg = [](as3::VM& vm, as3::Args args, size_t i) {
        return i < args.size() ? vm.toNumber(args[i]) : std::numeric_limits<double>::quiet_NaN();
    };
    define("isClip", [clipOf](as3::VM&, const as3::Val&, as3::Args args) {
        return as3::Val::boolean(!args.empty() && clipOf(args[0]) != nullptr);
    });
    define("matrix", [clipOf, numbers](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : clipOf(args[0]);
        if (!clip)
            return as3::Val();
        const Transform m = clip->transform(clip->placedTransform());
        return numbers({m.m11, m.m12, m.m21, m.m22, m.tx, m.ty});
    });
    define("setMatrix", [clipOf, arg](as3::VM& vm, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : clipOf(args[0]);
        if (clip && clip->display)
        {
            clip->display->setMatrix(display::Matrix(arg(vm, args, 1), arg(vm, args, 2), arg(vm, args, 3),
                arg(vm, args, 4), arg(vm, args, 5), arg(vm, args, 6)));
            display::markDisplayChanged();
        }
        return as3::Val();
    });
    define("concatenatedMatrix", [&runtime, clipOf, numbers](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : clipOf(args[0]);
        if (!clip)
            return as3::Val();
        const Transform m = runtime.toStage(*clip);
        return numbers({m.m11, m.m12, m.m21, m.m22, m.tx, m.ty});
    });
    define("colorTransform", [clipOf, colors](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : clipOf(args[0]);
        return clip && clip->display ? colors(clip->display->colorTransform()) : as3::Val();
    });
    define("setColorTransform", [clipOf, arg](as3::VM& vm, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : clipOf(args[0]);
        if (clip && clip->display)
        {
            display::ColorTransform c;
            c.redMultiplier = arg(vm, args, 1);
            c.greenMultiplier = arg(vm, args, 2);
            c.blueMultiplier = arg(vm, args, 3);
            c.alphaMultiplier = arg(vm, args, 4);
            c.redOffset = arg(vm, args, 5);
            c.greenOffset = arg(vm, args, 6);
            c.blueOffset = arg(vm, args, 7);
            c.alphaOffset = arg(vm, args, 8);
            clip->display->setColorTransform(colorOf(fixedOf(c)));
            display::markDisplayChanged();
        }
        return as3::Val();
    });
    // Its own and each parent's, the parent's applied after.
    define("concatenatedColorTransform", [clipOf, colors](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : clipOf(args[0]);
        if (!clip || !clip->display)
            return as3::Val();
        FixedColor total = fixedOf(clip->display->colorTransform());
        for (Clip* up = clip->parent; up; up = up->parent)
            if (up->display)
                total = combined(fixedOf(up->display->colorTransform()), total);
        return colors(colorOf(total));
    });
    define("pixelBounds", [&runtime, clipOf, numbers](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : clipOf(args[0]);
        if (!clip || !clip->display)
            return as3::Val();
        const display::Rect box = clip->display->getBounds(&runtime.stage());
        if (box.isEmpty())
            return numbers({0, 0, 0, 0});
        return numbers({box.x, box.y, box.width, box.height});
    });
    natives->properties["bitmap"] = bitmapNatives();
    natives->properties["clip"] = clipNatives();
    natives->properties["text"] = textNatives();
    const as3::Val filters = filterNatives();
    for (const auto& [name, function] : filters->properties)
        natives->properties[name] = function;
    natives->properties["noObject"] = _interpreter->noObject();
    natives->properties["version"] = as3::Val::number(_interpreter->version());
    natives->properties["sharedObject"] = sharedObjectNatives();
    natives->properties["xml"] = xmlNatives();
    return natives;
}

ActionsPlayer::~ActionsPlayer()
{
    // What scripts kept in SharedObjects is saved as the movie ends.
    saveSharedObjects();
    *_alive = false;
    _posted.clear();
    _interpreter.reset();
}

void ActionsPlayer::run(const CompiledActions::Block& block, Clip& clip, const std::string& where)
{
    ++_runtime._scriptRuns;
    Clip* was = _runtime._current;
    _runtime._current = &clip;
    _interpreter->run(block.actions, _runtime.objectFor(clip), where);
    _runtime._current = was;
}

as3::Val ActionsPlayer::handler(const std::vector<const CompiledActions::Block*>& blocks, const std::string& where)
{
    std::vector<as3::Val> functions;
    for (const CompiledActions::Block* block : blocks)
        functions.push_back(_interpreter->handler(block->actions, where));
    if (functions.size() == 1)
        return functions.front();
    return _vm.makeFunction("handlers", [functions](as3::VM& vm, const as3::Val& self, as3::Args) {
        for (const as3::Val& function : functions)
            vm.callFunction(function, self, std::vector<as3::Val>());
        return as3::Val();
    });
}

void ActionsPlayer::broadcast(const std::string& broadcaster, const std::string& message,
    const std::vector<as3::Val>& args)
{
    const as3::Val object = _interpreter->getMember(_interpreter->global(), broadcaster);
    if (!object.isObject())
        return;
    std::vector<as3::Val> all{as3::Val::string(message)};
    all.insert(all.end(), args.begin(), args.end());
    ++_runtime._scriptRuns;
    _interpreter->callMethod(object, "broadcastMessage", all, broadcaster + " listener, " + message);
}

void ActionsPlayer::callHandler(Clip& clip, const std::string& name)
{
    if (!clip.object)
        return;
    const as3::Val object = clip.object;
    const as3::Val function = _interpreter->getMember(object, name);
    if (!function.isFunction())
        return;
    ++_runtime._scriptRuns;
    Clip* was = _runtime._current;
    _runtime._current = &clip;
    _interpreter->call(function, object, {}, (clip.name.empty() ? clip.key : clip.name) + ", " + name);
    _runtime._current = was;
}

} // namespace play

} // namespace fla
