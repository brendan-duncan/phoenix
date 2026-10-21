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

#include <algorithm>
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
    bool isClip(const as3::Val& value) override { return clipOf(value) != nullptr; }
    std::string className(const as3::Val& value) override
    {
        Clip* clip = clipOf(value);
        return clip && clip->button ? "Button" : "MovieClip";
    }
    as3::Val root(const as3::Val&) override { return objectOf(_runtime._root.get()); }
    as3::Val level(int number) override { return number == 0 ? objectOf(_runtime._root.get()) : as3::Val(); }
    as3::Val parent(const as3::Val& value) override
    {
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
        return as3::Val();
    }
    std::string targetPath(const as3::Val& value) override
    {
        Clip* clip = clipOf(value);
        if (!clip)
            return std::string();
        std::string path = _runtime.slashPath(*clip);
        std::replace(path.begin(), path.end(), '/', '.');
        return path == "." ? std::string("_level0") : "_level0" + path;
    }
    void childNames(const as3::Val& value, std::vector<std::string>& names) override
    {
        if (Clip* clip = clipOf(value))
            for (const auto& each : clip->children)
                if (each && !each->gone && !each->name.empty())
                    names.push_back(each->name);
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

    void cloneSprite(const as3::Val& value, const std::string& name, int depth) override
    {
        Clip* clip = clipOf(value);
        if (!clip)
            return;
        const as3::ValuePtr duplicate = _runtime.property(*clip, "duplicateMovieClip");
        if (duplicate && duplicate->nativeFunc)
            duplicate->nativeFunc({as3::Value::makeString(name), as3::Value::makeNumber(depth)});
    }
    void removeSprite(const as3::Val& value) override
    {
        Clip* clip = clipOf(value);
        if (clip && clip->made)
            _runtime.takeOff(*clip, false);
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
    // The player's own ActionScript 2 classes (flash.geom...), which a
    // movie made for SWF 8 or later sees.
    if (version >= 8)
    {
        static const std::shared_ptr<const std::vector<uint8_t>> library = []() {
            as3::avm1::Compiler compiler;
            auto bytes = std::make_shared<std::vector<uint8_t>>(compiler.compileScript(as2LibrarySource(), "library"));
            return std::shared_ptr<const std::vector<uint8_t>>(bytes);
        }();
        if (!library->empty() && _runtime._root)
            _interpreter->run(library, _runtime.objectFor(*_runtime._root), "library");
    }
}

ActionsPlayer::~ActionsPlayer()
{
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
