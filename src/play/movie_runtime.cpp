#include "movie_runtime.h"

#include "../data/action_script.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../edit/script_edit.h"

#include "bytecode/compiler.h"
#include "bytecode/vm.h"
#include "parser/parser.h"
#include "runtime/value.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
std::string keyOf(const Timeline& timeline, const Layer& layer, const Frame& frame, const SymbolInstance& instance)
{
    if (!instance.name.empty())
        return instance.name;
    const auto at = std::find(frame.elements.begin(), frame.elements.end(), &instance);
    std::ostringstream key;
    key << "#" << layerIndex(timeline, &layer) << ":" << (at - frame.elements.begin()) << ":"
        << (instance.symbol ? instance.symbol->name : instance.libraryItemName);
    return key.str();
}

double scaleOf(double a, double b)
{
    return std::sqrt(a * a + b * b);
}

} // namespace

std::string instanceScript(const SymbolInstance& instance)
{
    return fla::scripts::instanceScript(instance);
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
    Transform shown = placedTransform;
    if (xscale || yscale || rotation)
    {
        // Built again from scale and rotation, as Flash does once a script
        // sets one of them (any skew is dropped).
        const double sx = xscale ? *xscale / 100.0 : scaleOf(shown.m11, shown.m12);
        const double sy = yscale ? *yscale / 100.0 : scaleOf(shown.m21, shown.m22);
        const double turn = (rotation ? *rotation : std::atan2(shown.m12, shown.m11) * 180.0 / kPi) * kPi / 180.0;
        shown.m11 = sx * std::cos(turn);
        shown.m12 = sx * std::sin(turn);
        shown.m21 = -sy * std::sin(turn);
        shown.m22 = sy * std::cos(turn);
    }
    if (x)
        shown.tx = *x;
    if (y)
        shown.ty = *y;
    return shown;
}

/// A clip as scripts see it: its properties worked out from the clip, its
/// methods bound to it, its children by name.
class ClipObject : public as3::HostObject
{
public:
    ClipObject(MovieRuntime& runtime, Clip& clip)
        : _runtime(runtime)
        , _clip(clip)
    {}

    as3::ValuePtr get(const std::string& name) override { return _runtime.property(_clip, name); }
    bool set(const std::string& name, as3::ValuePtr value) override { return _runtime.setProperty(_clip, name, value); }

private:
    MovieRuntime& _runtime;
    Clip& _clip;
};

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

MovieRuntime::~MovieRuntime() = default;

void MovieRuntime::start()
{
    _compiled.clear();
    _buttonHandlers.clear();
    _hovered = _pressed = nullptr;
    _sent.clear();
    _sceneMove = SceneMove();
    _scene = 0;
    _vm = std::make_unique<as3::VM>();
    _root = std::make_unique<Clip>();
    _root->timeline = _scenes.empty() ? nullptr : _scenes.front();
    _root->length = lengthOf(_root->timeline);
    _root->name = "_level0";
    setupGlobals();
    std::vector<Clip*> entered{_root.get()};
    enter(entered);
}

as3::ValuePtr MovieRuntime::objectFor(Clip& clip)
{
    if (!clip.object)
        clip.object = as3::Value::makeHostObject(std::make_shared<ClipObject>(*this, clip));
    return clip.object;
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
    vm.registerBuiltin("getTimer", [](const Args&) { return Value::makeNumber(0); });
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
}

void MovieRuntime::report(const std::string& where)
{
    if (!_vm->hasError())
        return;
    std::string message = where;
    if (_vm->getErrorLine() >= 0)
        message += ", line " + std::to_string(_vm->getErrorLine());
    message += ": " + _vm->getError();
    if (error)
        error(message);
}

std::shared_ptr<as3::BytecodeFunction> MovieRuntime::compiled(const void* key, const std::string& source,
    const std::string& where)
{
    const auto found = _compiled.find(key);
    if (found != _compiled.end())
        return found->second;
    std::shared_ptr<as3::BytecodeFunction> code;
    as3::Parser parser(source);
    std::unique_ptr<as3::Program> program = parser.parse();
    if (parser.hasErrors())
    {
        if (error)
            error(where + ": " + parser.getErrors());
    }
    else if (program)
    {
        as3::Compiler compiler;
        code = compiler.compile(program.get());
    }
    _compiled[key] = code;
    return code;
}

void MovieRuntime::run(const void* key, const std::string& source, Clip& scope, const std::string& where)
{
    std::shared_ptr<as3::BytecodeFunction> code = compiled(key, source, where);
    if (!code)
        return;
    Clip* was = _current;
    _current = &scope;
    _vm->execute(code, objectFor(scope));
    report(where);
    _current = was;
}

void MovieRuntime::runFrameScripts(Clip& clip)
{
    if (!clip.timeline)
        return;
    // Every layer's script that starts on this frame, top layer first.
    for (const Layer* layer : clip.timeline->layers)
    {
        if (!layer || !plays(*layer))
            continue;
        const Frame* frame = keyframeAt(*layer, clip.frame);
        if (!frame || frame->index != clip.frame || !frame->actionScript || frame->actionScript->code.empty())
            continue;
        const std::string where = (clip.symbol ? clip.symbol->name : clip.timeline->name) + ", frame " +
            std::to_string(clip.frame + 1);
        run(frame, frame->actionScript->code, clip, where);
    }
}

void MovieRuntime::syncChildren(Clip& clip, std::vector<Clip*>& entered)
{
    if (!clip.timeline)
        return;
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
            const auto* instance = dynamic_cast<const SymbolInstance*>(element);
            if (!instance || !instance->symbol || instance->symbolType == SymbolType::Graphic)
                continue;
            const std::string key = keyOf(*clip.timeline, *layer, *frame, *instance);
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
            entered.push_back(made.get());
            // Its own children are placed now too, so a script anywhere
            // finds the whole of what is on the stage.
            syncChildren(*made, entered);
            kept.push_back(std::move(made));
        }
    }
    // Clips gone from the frame are let go.
    for (auto& gone : clip.children)
        if (gone)
            forget(*gone);
    clip.children = std::move(kept);
}

void MovieRuntime::forget(Clip& clip)
{
    if (_hovered && (_hovered == &clip))
        _hovered = nullptr;
    if (_pressed && (_pressed == &clip))
        _pressed = nullptr;
    _sent.erase(std::remove(_sent.begin(), _sent.end(), &clip), _sent.end());
    for (auto& child : clip.children)
        if (child)
            forget(*child);
}

void MovieRuntime::enter(std::vector<Clip*>& entered)
{
    // A clip entered for the first time: its onClipEvent handlers taken,
    // load run, its first frame's scripts run, and its own children made.
    for (size_t i = 0; i < entered.size(); ++i)
    {
        Clip& clip = *entered[i];
        if (clip.placed && !clip.button)
        {
            const std::string script = instanceScript(*clip.placed);
            if (!script.empty())
            {
                // Run with __onClipEvent gathering the handlers.
                std::map<std::string, as3::ValuePtr>& handlers = clip.clipEvents;
                _vm->registerBuiltin("__onClipEvent", [&handlers](const std::vector<as3::ValuePtr>& args) {
                    if (args.size() >= 2)
                        for (const as3::ValuePtr& event : args[0]->elements)
                            handlers[event->toString()] = args[1];
                    return as3::Value::makeUndefined();
                });
                run(clip.placed, script, *clip.parent, clip.name.empty() ? clip.key : clip.name);
            }
            runClipEvent(clip, "load");
        }
        // Its children are placed before its frame's scripts run, so the
        // scripts find them.
        syncChildren(clip, entered);
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
            forget(*child);
    root.children.clear();
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
    // A script sent clips to other frames: their scripts run there, which
    // may send them on again; a chain is cut short at a few.
    for (int round = 0; round < 8 && (!_sent.empty() || _sceneMove.scene >= 0); ++round)
    {
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
            syncChildren(*clip, entered);
            runFrameScripts(*clip);
        }
        for (size_t i = 0; i < entered.size(); ++i)
        {
            Clip& clip = *entered[i];
            syncChildren(clip, entered);
            if (!clip.button)
                runFrameScripts(clip);
        }
    }
    if (changed)
        changed();
}

void MovieRuntime::advance(Clip& clip, std::vector<Clip*>& entered)
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
        runFrameScripts(clip);
        return;
    }
    if (clip.playing && !clip.button && clip.length > 1)
    {
        clip.frame = (clip.frame + 1) % clip.length;
        syncChildren(clip, entered);
        runFrameScripts(clip);
    }
    else
    {
        syncChildren(clip, entered);
    }
    for (const auto& child : clip.children)
        if (std::find(before.begin(), before.end(), child.get()) != before.end())
            advance(*child, entered);
}

void MovieRuntime::step()
{
    std::vector<Clip*> entered;
    advance(*_root, entered);
    // enterFrame for every clip about.
    std::function<void(Clip&)> each = [&](Clip& clip) {
        if (!clip.clipEvents.empty())
            runClipEvent(clip, "enterFrame");
        for (const auto& child : clip.children)
            if (child)
                each(*child);
    };
    each(*_root);
    enter(entered);
}

void MovieRuntime::runClipEvent(Clip& clip, const std::string& event)
{
    const auto found = clip.clipEvents.find(event);
    if (found == clip.clipEvents.end())
        return;
    Clip* was = _current;
    _current = &clip;
    _vm->call(found->second, {}, objectFor(clip), objectFor(clip));
    report((clip.name.empty() ? clip.key : clip.name) + ", onClipEvent(" + event + ")");
    _current = was;
}

void MovieRuntime::runButtonEvent(Clip& button, const std::string& event)
{
    if (!button.placed || !button.parent)
        return;
    // The handlers kept on the instance, gathered once.
    auto handlers = _buttonHandlers.find(button.placed);
    if (handlers == _buttonHandlers.end())
    {
        std::map<std::string, as3::ValuePtr>& gathered = _buttonHandlers[button.placed];
        const std::string script = instanceScript(*button.placed);
        if (!script.empty())
        {
            _vm->registerBuiltin("__on", [&gathered](const std::vector<as3::ValuePtr>& args) {
                if (args.size() >= 2)
                    for (const as3::ValuePtr& name : args[0]->elements)
                        gathered[name->toString()] = args[1];
                return as3::Value::makeUndefined();
            });
            run(button.placed, script, *button.parent, "button " + (button.name.empty() ? button.key : button.name));
        }
        handlers = _buttonHandlers.find(button.placed);
    }
    const auto handler = handlers->second.find(event);
    if (handler == handlers->second.end())
        return;
    // A button's handlers run on the timeline the button is on.
    Clip& scope = *button.parent;
    Clip* was = _current;
    _current = &scope;
    _vm->call(handler->second, {}, objectFor(scope), objectFor(scope));
    report("button " + (button.name.empty() ? button.key : button.name) + ", on(" + event + ")");
    _current = was;
    settle();
}

void MovieRuntime::pointerOver(Clip* button)
{
    if (button == _hovered)
        return;
    Clip* left = _hovered;
    _hovered = button;
    if (left)
        runButtonEvent(*left, _pressed == left ? "dragOut" : "rollOut");
    if (button)
        runButtonEvent(*button, _pressed == button ? "dragOver" : "rollOver");
    if (changed)
        changed();
}

void MovieRuntime::pointerDown()
{
    _pressed = _hovered;
    if (_pressed)
        runButtonEvent(*_pressed, "press");
    if (changed)
        changed();
}

void MovieRuntime::pointerUp()
{
    Clip* pressed = _pressed;
    _pressed = nullptr;
    if (pressed)
        runButtonEvent(*pressed, pressed == _hovered ? "release" : "releaseOutside");
    if (changed)
        changed();
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
        return Value::makeNumber(clip.alpha ? *clip.alpha
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
    if (name == "_parent")
        return clip.parent ? objectFor(*clip.parent) : Value::makeUndefined();
    if (name == "_root")
        return objectFor(*_root);
    // Its timeline methods, bound to it.
    const auto method = [this, &clip](std::function<void(Clip&, const std::vector<as3::ValuePtr>&)> act) {
        Clip* target = &clip;
        return Value::makeNativeFunction([act, target](const std::vector<as3::ValuePtr>& args) {
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
    if (name == "_visible")
    {
        // "0" and "1", as Flash 4 scripts give it, are numbers here.
        clip.visible = value && (value->isBoolean() ? value->boolValue : !std::isnan(number()) && number() != 0.0);
    }
    else if (name == "_x")
        clip.x = number();
    else if (name == "_y")
        clip.y = number();
    else if (name == "_xscale")
        clip.xscale = number();
    else if (name == "_yscale")
        clip.yscale = number();
    else if (name == "_rotation")
        clip.rotation = number();
    else if (name == "_alpha")
        clip.alpha = number();
    else
        return false;
    if (std::isnan(clip.x.value_or(0.0)))
        clip.x.reset();
    if (std::isnan(clip.y.value_or(0.0)))
        clip.y.reset();
    if (changed)
        changed();
    return true;
}

} // namespace play

} // namespace fla
