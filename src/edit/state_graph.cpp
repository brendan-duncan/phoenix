#include "state_graph.h"

#include "command_stack.h"
#include "frame_commands.h"
#include "layer_commands.h"
#include "property_command.h"
#include "script_edit.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>

namespace fla {
namespace states {

const char* const kAttribute = "phoenixStates";
const char* const kLayerName = "States";
const char* const kBegin = "// [States] Made in the States panel: what is between these lines is made again from there.";
const char* const kEnd = "// [/States]";

namespace {

using Attributes = std::vector<std::pair<std::string, std::string>>;

std::string numberText(double value)
{
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15)
        return std::to_string(static_cast<long long>(value));
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", value);
    return text;
}

double numberOf(const char* text, double fallback = 0.0)
{
    if (!text || !*text)
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    return end != text && std::isfinite(value) ? value : fallback;
}

/// The names the attribute spells each choice with.
const std::vector<std::pair<Play, const char*>> kPlays = {
    {Play::Loop, "loop"}, {Play::Once, "once"}, {Play::Hold, "hold"}, {Play::Blend, "blend"}};
const std::vector<std::pair<InputKind, const char*>> kKinds = {
    {InputKind::Switch, "switch"}, {InputKind::Number, "number"}, {InputKind::Trigger, "trigger"}};
const std::vector<std::pair<When, const char*>> kWhens = {{When::Click, "click"}, {When::Press, "press"},
    {When::Release, "release"}, {When::HoverIn, "hoverIn"}, {When::HoverOut, "hoverOut"}, {When::Key, "key"},
    {When::Finished, "finished"}, {When::Timer, "timer"}, {When::Condition, "condition"}};
const std::vector<std::pair<Compare, const char*>> kCompares = {{Compare::None, "none"}, {Compare::IsOn, "on"},
    {Compare::IsOff, "off"}, {Compare::Fired, "fired"}, {Compare::Equal, "eq"}, {Compare::NotEqual, "ne"},
    {Compare::Less, "lt"}, {Compare::More, "gt"}, {Compare::AtLeast, "ge"}, {Compare::AtMost, "le"}};
const std::vector<std::pair<Change, const char*>> kChanges = {{Change::TurnOn, "on"}, {Change::TurnOff, "off"},
    {Change::Toggle, "toggle"}, {Change::Set, "set"}, {Change::Add, "add"}, {Change::Fire, "fire"}};

template <typename T>
const char* spelled(const std::vector<std::pair<T, const char*>>& table, T value)
{
    for (const auto& [each, name] : table)
        if (each == value)
            return name;
    return table.front().second;
}

template <typename T>
T spelledAs(const std::vector<std::pair<T, const char*>>& table, const char* name)
{
    for (const auto& [each, spelling] : table)
        if (name && std::strcmp(spelling, name) == 0)
            return each;
    return table.front().first;
}

bool sameCondition(const Condition& a, const Condition& b)
{
    return a.input == b.input && a.compare == b.compare && a.value == b.value;
}

void writeCondition(tinyxml2::XMLElement* e, const Condition& condition)
{
    if (condition.empty())
        return;
    e->SetAttribute("input", condition.input.c_str());
    e->SetAttribute("compare", spelled(kCompares, condition.compare));
    if (condition.value != 0.0)
        e->SetAttribute("value", numberText(condition.value).c_str());
}

Condition readCondition(const tinyxml2::XMLElement* e)
{
    Condition condition;
    if (const char* input = e->Attribute("input"))
        condition.input = input;
    condition.compare = spelledAs(kCompares, e->Attribute("compare"));
    condition.value = numberOf(e->Attribute("value"));
    return condition;
}

bool pointer(When when)
{
    return when == When::Click || when == When::Press || when == When::Release || when == When::HoverIn ||
        when == When::HoverOut;
}

/// An ActionScript string literal.
std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out + "\"";
}

int keyCode(const std::string& name)
{
    for (const auto& [each, code] : keyNames())
        if (each == name)
            return code;
    return 0;
}

/// The name an event goes by in the script: in ActionScript 1 and 2 a
/// button's release is its click (both are onRelease).
std::string eventName(When when, bool as3)
{
    switch (when)
    {
    case When::Click: return "click";
    case When::Press: return "press";
    case When::Release: return as3 ? "release" : "click";
    case When::HoverIn: return "over";
    case When::HoverOut: return "out";
    default: return std::string();
    }
}

/// The ActionScript 3 event an event name listens to, and the
/// ActionScript 1 and 2 handler.
const char* as3Event(const std::string& name)
{
    if (name == "click")
        return "click";
    if (name == "press")
        return "mouseDown";
    if (name == "release")
        return "mouseUp";
    if (name == "over")
        return "rollOver";
    return "rollOut";
}

const char* as2Handler(const std::string& name)
{
    if (name == "click")
        return "onRelease";
    if (name == "press")
        return "onPress";
    if (name == "over")
        return "onRollOver";
    return "onRollOut";
}

//// One layer of states as the script runs it: the timeline's own (no
/// suffix on its names, no clip) or a movie clip's, its names ending in
/// the layer's number.
struct Lane
{
    std::string suffix;
    std::string target;
    const Timeline* timeline = nullptr;
    std::vector<int> states;
    int entry = -1;

    bool own() const { return target.empty(); }
};

/// Writes the script for one version: the names it gives what it keeps
/// (phx... as a timeline's variables), each line indented by tabs.
class Writer
{
public:
    Writer(const Graph& graph, const Timeline& timeline, int version, double frameRate)
        : _graph(graph)
        , _timeline(timeline)
        , _as3(version >= 3)
        , _frameRate(frameRate > 0.0 ? frameRate : 24.0)
    {
        // The instances listened to, each event once, in the order met.
        const auto hook = [this](When when, const std::string& target) {
            if (!pointer(when) || target.empty())
                return;
            const std::pair<std::string, std::string> each(target, eventName(when, _as3));
            if (std::find(_hooks.begin(), _hooks.end(), each) == _hooks.end())
                _hooks.push_back(each);
        };
        for (const Transition& transition : graph.transitions)
        {
            hook(transition.when, transition.target);
            _keys = _keys || transition.when == When::Key;
        }
        for (const Listener& listener : graph.listeners)
        {
            hook(listener.when, listener.target);
            _keys = _keys || listener.when == When::Key;
        }
        for (const Binding& binding : graph.bindings)
            if (!binding.target.empty() && graph.input(binding.input))
                _binds.push_back(binding);
        for (const Pin& pin : graph.pins)
            if (!pin.target.empty())
                _pins.push_back(pin);
        for (const Scroller& scroller : graph.scrollers)
            if (!scroller.target.empty() && (scroller.across || scroller.down))
                _scrolls.push_back(scroller);
        // The stage the pins were placed on.
        for (const DOMElement* up = &timeline; up; up = up->parent)
            if (up->domType() == DOMElement::DOMType::Document)
            {
                const auto* document = static_cast<const Document*>(up);
                _stageWidth = document->width > 0 ? document->width : 550;
                _stageHeight = document->height > 0 ? document->height : 400;
            }
        // The timeline's own states, then each layer whose clip is there.
        const int layers = static_cast<int>(graph.layers.size());
        _own.timeline = &timeline;
        for (size_t i = 0; i < graph.states.size(); ++i)
            if (graph.states[i].layer <= 0 || graph.states[i].layer > layers)
                _own.states.push_back(static_cast<int>(i));
        _own.entry = graph.entryOf(0);
        for (int k = 1; k <= layers; ++k)
        {
            Lane lane;
            lane.suffix = std::to_string(k);
            lane.target = graph.layers[static_cast<size_t>(k - 1)].target;
            lane.timeline = lane.target.empty() ? nullptr : clipTimeline(timeline, lane.target);
            for (size_t i = 0; i < graph.states.size(); ++i)
                if (graph.states[i].layer == k)
                    lane.states.push_back(static_cast<int>(i));
            lane.entry = graph.entryOf(k);
            if (lane.timeline && !lane.states.empty())
                _lanes.push_back(lane);
        }
    }

    std::string write()
    {
        line(kBegin);
        if (_as3)
            writeAs3();
        else
            writeAs2();
        line(kEnd);
        return _out.str();
    }

private:
    void line(const std::string& text)
    {
        for (int i = 0; i < _depth; ++i)
            _out << '\t';
        _out << text << '\n';
    }
    void open(const std::string& text)
    {
        line(text);
        ++_depth;
    }
    void close(const std::string& text = "}")
    {
        --_depth;
        line(text);
    }

    /// What a name of the timeline's is called in the script: bare in
    /// ActionScript 3's frame scripts, on the timeline (phxT) in 1 and 2.
    std::string me(const std::string& name) const { return _as3 ? name : "phxT." + name; }
    std::string input(const std::string& name) const { return me("phxInputs") + "[" + quoted(name) + "]"; }
    /// A name the layer keeps of its own: its state, its function.
    std::string mine(const Lane& lane, const std::string& name) const { return me(name + lane.suffix); }
    /// What the layer's clip is asked to do: the timeline's own playhead,
    /// or the clip's (phxC, fetched first).
    std::string clip(const Lane& lane, const std::string& method) const
    {
        return lane.own() ? me(method) : "phxC." + method;
    }
    std::string frameOf(const Lane& lane) const { return clip(lane, _as3 ? "currentFrame" : "_currentframe"); }
    /// The line fetching a layer's clip into phxC.
    std::string fetch(const Lane& lane) const
    {
        return _as3 ? "var phxC:* = getChildByName(" + quoted(lane.target) + ");"
                    : "var phxC = phxT[" + quoted(lane.target) + "];";
    }

    std::string test(const Condition& condition) const
    {
        const Input* in = _graph.input(condition.input);
        if (condition.empty() || !in)
            return std::string();
        const std::string value = input(condition.input);
        const std::string number = numberText(condition.value);
        switch (condition.compare)
        {
        case Compare::IsOn:
        case Compare::Fired: return value + " == true";
        case Compare::IsOff: return value + " != true";
        case Compare::Equal: return value + " == " + number;
        case Compare::NotEqual: return value + " != " + number;
        case Compare::Less: return value + " < " + number;
        case Compare::More: return value + " > " + number;
        case Compare::AtLeast: return value + " >= " + number;
        case Compare::AtMost: return value + " <= " + number;
        default: return std::string();
        }
    }

    static std::string both(const std::string& a, const std::string& b)
    {
        if (a.empty())
            return b;
        if (b.empty())
            return a;
        return a + " && " + b;
    }

    int ticks(double seconds) const { return std::max(1, static_cast<int>(std::lround(seconds * _frameRate))); }

    /// Whether \a state follows a number input (one that is there) over
    /// frames \a from to \a to.
    bool blends(const State& state, int from, int to) const
    {
        const Input* in = _graph.input(state.blend);
        return state.play == Play::Blend && in && in->kind == InputKind::Number && to > from;
    }

    /// The frame (from 1) a blending state shows for its input now.
    std::string blendFrame(const State& state, int from, int to) const
    {
        const double span = state.high != state.low ? state.high - state.low : 1.0;
        std::string share = "(" + input(state.blend) + " - " + numberText(state.low) + ") / " + numberText(span);
        std::string frame = std::to_string(from + 1) + " + Math.round(Math.max(0, Math.min(1, " + share + ")) * " +
            std::to_string(to - from) + ")";
        return _as3 ? "int(" + frame + ")" : frame;
    }

    /// What the event, key or check \a transition waits for, as a test;
    /// empty when it never goes.
    std::string trigger(const Lane& lane, const Transition& transition) const
    {
        switch (transition.when)
        {
        case When::Key:
        {
            const int code = keyCode(transition.target);
            return code ? "target == \"key\" && what == \"" + std::to_string(code) + "\"" : "target == \"key\"";
        }
        case When::Finished: return mine(lane, "phxDone");
        case When::Timer: return mine(lane, "phxTicks") + " >= " + std::to_string(ticks(transition.seconds));
        case When::Condition: return std::string();
        default:
            return transition.target.empty() ? std::string()
                                             : "target == " + quoted(transition.target) + " && what == \"" +
                    eventName(transition.when, _as3) + "\"";
        }
    }

    bool inLane(const Lane& lane, int state) const
    {
        return std::find(lane.states.begin(), lane.states.end(), state) != lane.states.end();
    }

    /// The transitions that leave state \a index (its own, then any
    /// state's of its layer), heard by the event handler or by the check
    /// each frame.
    std::vector<const Transition*> leaving(const Lane& lane, int index, bool events) const
    {
        std::vector<const Transition*> list;
        for (int pass = 0; pass < 2; ++pass)
            for (const Transition& transition : _graph.transitions)
            {
                if (transition.to < 0 || transition.to >= static_cast<int>(_graph.states.size()) ||
                    !inLane(lane, transition.to))
                    continue;
                const bool event = pointer(transition.when) || transition.when == When::Key;
                if (event != events)
                    continue;
                if (pass == 0 ? transition.from != index : (transition.from != -1 || transition.to == index))
                    continue;
                if (transition.when == When::Condition && transition.condition.empty())
                    continue;
                list.push_back(&transition);
            }
        return list;
    }

    void go(const Lane& lane, const Transition& transition)
    {
        const std::string test = both(trigger(lane, transition), this->test(transition.condition));
        if (test.empty() && transition.when != When::Finished)
            return;
        open("if (" + test + ") {");
        // A trigger is used up by the transition it sets off.
        const Input* in = _graph.input(transition.condition.input);
        if (in && in->kind == InputKind::Trigger && !transition.condition.empty())
            line(input(in->name) + " = false;");
        line(mine(lane, "phxTo") + "(" + quoted(_graph.states[static_cast<size_t>(transition.to)].label) + ", " +
            (transition.afterFinish ? "true" : "false") + ");");
        line("return;");
        close();
    }

    void listeners()
    {
        for (const Listener& listener : _graph.listeners)
        {
            const Input* in = _graph.input(listener.input);
            if (!in)
                continue;
            std::string test;
            if (listener.when == When::Key)
            {
                const int code = keyCode(listener.target);
                test = code ? "target == \"key\" && what == \"" + std::to_string(code) + "\"" : "target == \"key\"";
            }
            else if (pointer(listener.when) && !listener.target.empty())
                test = "target == " + quoted(listener.target) + " && what == \"" + eventName(listener.when, _as3) + "\"";
            if (test.empty())
                continue;
            const std::string value = input(in->name);
            std::string set;
            switch (listener.change)
            {
            case Change::TurnOn:
            case Change::Fire: set = value + " = true;"; break;
            case Change::TurnOff: set = value + " = false;"; break;
            case Change::Toggle: set = value + " = !" + value + ";"; break;
            case Change::Set: set = value + " = " + numberText(listener.value) + ";"; break;
            case Change::Add: set = value + " = " + value + " + " + numberText(listener.value) + ";"; break;
            }
            open("if (" + test + ") {");
            line(set);
            close();
        }
    }

    /// The bodies the two versions share: going to a state, each frame's
    /// look at where the playhead is, the events, the checks.
    void goBody(const Lane& lane)
    {
        line(mine(lane, "phxState") + " = state;");
        line(mine(lane, "phxTicks") + " = 0;");
        line(mine(lane, "phxDone") + " = false;");
        line(mine(lane, "phxWait") + " = null;");
        if (lane.own())
        {
            for (const Input& each : _graph.inputs)
                if (each.kind == InputKind::Trigger)
                    line(input(each.name) + " = false;");
        }
        else
        {
            // Its clip may not be on the stage yet: the state waits for it.
            line(fetch(lane));
            line(mine(lane, "phxSeen") + " = phxC;");
            open("if (phxC == null) {");
            line("return;");
            close();
        }
        bool first = true;
        for (const int index : lane.states)
        {
            const State& state = _graph.states[static_cast<size_t>(index)];
            int from = 0, to = 0;
            if (!range(*lane.timeline, state, from, to))
                continue;
            open(std::string(first ? "" : "} else ") + "if (state == " + quoted(state.label) + ") {");
            if (blends(state, from, to))
            {
                line(clip(lane, "gotoAndStop") + "(" + blendFrame(state, from, to) + ");");
                line(mine(lane, "phxDone") + " = true;");
            }
            else
            {
                const bool still = state.play == Play::Hold || state.play == Play::Blend || from == to;
                line(clip(lane, still ? "gotoAndStop" : "gotoAndPlay") + "(" + quoted(state.label) + ");");
                if (still)
                    line(mine(lane, "phxDone") + " = true;");
            }
            --_depth;
            first = false;
        }
        if (!first)
            line("}");
        line(mine(lane, "phxAt") + " = " + frameOf(lane) + ";");
        if (lane.own())
            line(me("phxHook") + "();");
    }

    void toBody(const Lane& lane)
    {
        open("if (after && !" + mine(lane, "phxDone") + ") {");
        line(mine(lane, "phxWait") + " = state;");
        close("} else {");
        ++_depth;
        line(mine(lane, "phxGo") + "(state);");
        close();
    }

    /// Each frame's look at where the layer's playhead is, with the frame
    /// shown in f: a loop goes round, one played once stops at its end, a
    /// blend follows its input.
    void playBody(const Lane& lane)
    {
        bool first = true;
        for (const int index : lane.states)
        {
            const State& state = _graph.states[static_cast<size_t>(index)];
            int from = 0, to = 0;
            if (!range(*lane.timeline, state, from, to))
                continue;
            const bool blend = blends(state, from, to);
            if (!blend && (state.play == Play::Hold || state.play == Play::Blend || from == to))
                continue;
            const std::string start = std::to_string(from + 1);
            const std::string end = std::to_string(to + 1);
            open(std::string(first ? "" : "} else ") + "if (" + mine(lane, "phxState") + " == " + quoted(state.label) + ") {");
            first = false;
            if (blend)
            {
                const std::string frame = blendFrame(state, from, to);
                open("if (f != " + frame + ") {");
                line(clip(lane, "gotoAndStop") + "(" + frame + ");");
                line("f = " + frameOf(lane) + ";");
                close();
            }
            else if (state.play == Play::Loop)
            {
                // Past its last frame, or round to its first: again.
                open("if (f < " + start + " || f > " + end + " || f < " + mine(lane, "phxAt") + ") {");
                line(mine(lane, "phxDone") + " = false;");
                open("if (f < " + start + " || f > " + end + ") {");
                line(clip(lane, "gotoAndPlay") + "(" + quoted(state.label) + ");");
                line("f = " + frameOf(lane) + ";");
                close();
                close();
                open("if (f == " + end + ") {");
                line(mine(lane, "phxDone") + " = true;");
                close();
            }
            else
            {
                open("if (f == " + end + ") {");
                line(clip(lane, "stop") + "();");
                line(mine(lane, "phxDone") + " = true;");
                close();
            }
            --_depth;
        }
        if (!first)
            line("}");
    }

    /// The value binding \a binding gives its property now.
    std::string bound(const Binding& binding) const
    {
        const std::string value = input(binding.input);
        if (!mapped(binding.property))
            return value;
        const double span = binding.high != binding.low ? binding.high - binding.low : 1.0;
        return numberText(binding.from) + " + " + numberText(binding.to - binding.from) + " * Math.max(0, Math.min(1, (" +
            value + " - " + numberText(binding.low) + ") / " + numberText(span) + "))";
    }

    /// The line setting \a binding's property on o.
    std::string setter(const Binding& binding) const
    {
        const std::string value = bound(binding);
        const std::string& p = binding.property;
        if (p == "text")
            return "o.text = String(" + value + ");";
        if (p == "visible")
            return std::string(_as3 ? "o.visible" : "o._visible") + " = Boolean(" + value + ");";
        if (p == "frame")
            return "o.gotoAndStop(" + std::string(_as3 ? "int(Math.round(" + value + "))" : "Math.round(" + value + ")") + ");";
        if (p == "alpha")
            return _as3 ? "o.alpha = (" + value + ") / 100;" : "o._alpha = " + value + ";";
        if (p == "scale")
            return _as3 ? "o.scaleX = o.scaleY = (" + value + ") / 100;" : "o._xscale = o._yscale = " + value + ";";
        return "o." + std::string(_as3 ? "" : "_") + p + " = " + value + ";";
    }

    void bindBody()
    {
        line(_as3 ? "var o:Object;" : "var o;");
        for (size_t i = 0; i < _binds.size(); ++i)
        {
            if (i == 0 || _binds[i].target != _binds[i - 1].target)
                line(_as3 ? "o = getChildByName(" + quoted(_binds[i].target) + ");"
                          : "o = phxT[" + quoted(_binds[i].target) + "];");
            open(std::string(_as3 ? "if (o != null) {" : "if (o != undefined) {"));
            line(setter(_binds[i]));
            close();
        }
    }

    /// A pinned instance's place and size along one axis: kept from the
    /// low edge, the high one, both (stretching) or the middle.
    void pinAxis(const std::string& at, bool low, bool high, const char* place, const char* size, int start, int extent,
        const char* change)
    {
        const std::string prop = std::string(_as3 ? "" : "_") + place;
        const std::string length = std::string(_as3 ? "" : "_") + size;
        const std::string was = at + "[" + std::to_string(start) + "]";
        if (low && high)
        {
            line("o." + prop + " = " + was + ";");
            line("o." + length + " = Math.max(1, " + at + "[" + std::to_string(extent) + "] + " + change + ");");
        }
        else if (high)
            line("o." + prop + " = " + was + " + " + change + ";");
        else if (low)
            line("o." + prop + " = " + was + ";");
        else
            line("o." + prop + " = " + was + " + " + change + " / 2;");
    }

    void layoutBody()
    {
        const std::string stage = _as3 ? "stage" : "Stage";
        if (_as3)
        {
            open("if (stage == null) {");
            line("return;");
            close();
        }
        open("if (!" + me("phxLaid") + ") {");
        line(me("phxLaid") + " = true;");
        line(me("phxPinned") + " = [];");
        if (_as3)
        {
            line("stage.scaleMode = \"noScale\";");
            line("stage.align = \"TL\";");
            line("stage.addEventListener(\"resize\", phxLayout);");
        }
        else
        {
            // A player with no Stage leaves everything where it is.
            open("if (Stage != undefined) {");
            line("Stage.scaleMode = \"noScale\";");
            line("Stage.align = \"TL\";");
            line("var l = {};");
            open("l.onResize = function () {");
            line("phxT.phxLayout();");
            close("};");
            open("if (Stage.addListener != undefined) {");
            line("Stage.addListener(l);");
            close();
            close();
        }
        close();
        // A window of no size (not shown yet) moves nothing.
        if (_as3)
        {
            line("var dw:Number = stage.stageWidth > 0 ? stage.stageWidth - " + numberText(_stageWidth) + " : 0;");
            line("var dh:Number = stage.stageHeight > 0 ? stage.stageHeight - " + numberText(_stageHeight) + " : 0;");
        }
        else
        {
            line("var dw = 0;");
            line("var dh = 0;");
            open("if (Stage != undefined && Stage.width > 0) {");
            line("dw = Stage.width - " + numberText(_stageWidth) + ";");
            line("dh = Stage.height - " + numberText(_stageHeight) + ";");
            close();
        }
        line(_as3 ? "var o:Object;" : "var o;");
        for (size_t i = 0; i < _pins.size(); ++i)
        {
            const Pin& pin = _pins[i];
            const std::string at = me("phxPinned") + "[" + std::to_string(i) + "]";
            line(_as3 ? "o = getChildByName(" + quoted(pin.target) + ");" : "o = phxT[" + quoted(pin.target) + "];");
            open(_as3 ? "if (o != null) {" : "if (o != undefined) {");
            // Where it was first found, as placed on the stage.
            open("if (" + at + " == null || " + at + "[4] != o) {");
            line(at + (_as3 ? " = [o.x, o.y, o.width, o.height, o];" : " = [o._x, o._y, o._width, o._height, o];"));
            close();
            pinAxis(at, pin.left, pin.right, "x", "width", 0, 2, "dw");
            pinAxis(at, pin.top, pin.bottom, "y", "height", 1, 3, "dh");
            close();
        }
    }

    /// What the scrolling clips are, as the script keeps them.
    std::string scrollList() const
    {
        std::string list = "[";
        for (size_t i = 0; i < _scrolls.size(); ++i)
        {
            const Scroller& s = _scrolls[i];
            if (i)
                list += ", ";
            list += "{n: " + quoted(s.target) + ", w: " + numberText(s.width) + ", h: " + numberText(s.height) +
                ", ac: " + (s.across ? "true" : "false") + ", dn: " + (s.down ? "true" : "false") +
                ", g: " + numberText(s.glide) + "}";
        }
        return list + "]";
    }

    /// Each frame: a scrolling clip follows the pointer while held, glides
    /// once let go, and springs back from past its ends.
    void scrollStepBody()
    {
        const std::string x = _as3 ? "x" : "_x";
        const std::string y = _as3 ? "y" : "_y";
        const std::string mouseX = _as3 ? "mouseX" : "phxT._xmouse";
        const std::string mouseY = _as3 ? "mouseY" : "phxT._ymouse";
        line(me("phxScrollHook") + "();");
        open(std::string("for (var i") + (_as3 ? ":int" : "") + " = 0; i < " + me("phxScrolls") + ".length; i++) {");
        line(std::string(_as3 ? "var s:Object = " : "var s = ") + me("phxScrolls") + "[i];");
        line(std::string(_as3 ? "var o:Object = " : "var o = ") + "s.o;");
        open(_as3 ? "if (o == null) {" : "if (o == undefined) {");
        line("continue;");
        close();
        line(std::string(_as3 ? "var lowX:Number = " : "var lowX = ") + "s.x0 - Math.max(0, o." + (_as3 ? "width" : "_width") + " - s.w);");
        line(std::string(_as3 ? "var lowY:Number = " : "var lowY = ") + "s.y0 - Math.max(0, o." + (_as3 ? "height" : "_height") + " - s.h);");
        open("if (s.held) {");
        line(std::string(_as3 ? "var dx:Number = " : "var dx = ") + "s.ac ? " + mouseX + " - s.px : 0;");
        line(std::string(_as3 ? "var dy:Number = " : "var dy = ") + "s.dn ? " + mouseY + " - s.py : 0;");
        line("s.px = " + mouseX + ";");
        line("s.py = " + mouseY + ";");
        line("// Past an end it goes half as far, as if held back by a spring.");
        open("if (o." + x + " > s.x0 || o." + x + " < lowX) {");
        line("dx = dx / 2;");
        close();
        open("if (o." + y + " > s.y0 || o." + y + " < lowY) {");
        line("dy = dy / 2;");
        close();
        line("o." + x + " += dx;");
        line("o." + y + " += dy;");
        line("s.vx = dx;");
        line("s.vy = dy;");
        close("} else {");
        ++_depth;
        line("o." + x + " += s.vx;");
        line("o." + y + " += s.vy;");
        line("s.vx = s.vx * s.g;");
        line("s.vy = s.vy * s.g;");
        for (const auto& [axis, low, high, speed] :
            {std::tuple<std::string, std::string, std::string, std::string>{x, "lowX", "s.x0", "s.vx"},
                {y, "lowY", "s.y0", "s.vy"}})
        {
            open("if (o." + axis + " > " + high + " || o." + axis + " < " + low + ") {");
            line(std::string(_as3 ? "var to" : "var to") + axis + (_as3 ? ":Number" : "") + " = o." + axis + " > " + high +
                " ? " + high + " : " + low + ";");
            line(speed + " = " + speed + " / 2;");
            line("o." + axis + " += (to" + axis + " - o." + axis + ") / 4;");
            open("if (Math.abs(to" + axis + " - o." + axis + ") < 0.5) {");
            line("o." + axis + " = to" + axis + ";");
            close();
            close();
        }
        close();
        close();
    }

    void scrollFunctionsAs3()
    {
        line("// Makes each scrolling clip scroll inside its box, again as it comes back on the stage.");
        open("function phxScrollHook():void {");
        open("for (var i:int = 0; i < phxScrolls.length; i++) {");
        line("var s:Object = phxScrolls[i];");
        line("var o:Object = getChildByName(s.n);");
        open("if (o == null || o == s.o) {");
        line("continue;");
        close();
        line("s.o = o;");
        line("s.x0 = o.x;");
        line("s.y0 = o.y;");
        line("s.vx = 0;");
        line("s.vy = 0;");
        line("s.held = false;");
        line("var m:Shape = new Shape();");
        line("m.graphics.beginFill(0);");
        line("m.graphics.drawRect(o.x, o.y, s.w, s.h);");
        line("m.graphics.endFill();");
        line("addChild(m);");
        line("o.mask = m;");
        line("o.addEventListener(\"mouseDown\", phxScrollDown);");
        line("o.addEventListener(\"mouseWheel\", phxScrollWheel);");
        close();
        close();
        open("function phxScrollDown(e:*):void {");
        open("for (var i:int = 0; i < phxScrolls.length; i++) {");
        line("var s:Object = phxScrolls[i];");
        open("if (s.o == e.currentTarget) {");
        line("s.held = true;");
        line("s.px = mouseX;");
        line("s.py = mouseY;");
        line("s.vx = 0;");
        line("s.vy = 0;");
        close();
        close();
        open("if (stage != null) {");
        line("stage.addEventListener(\"mouseUp\", phxScrollUp);");
        close();
        close();
        open("function phxScrollUp(e:*):void {");
        open("for (var i:int = 0; i < phxScrolls.length; i++) {");
        line("phxScrolls[i].held = false;");
        close();
        close();
        open("function phxScrollWheel(e:*):void {");
        open("for (var i:int = 0; i < phxScrolls.length; i++) {");
        line("var s:Object = phxScrolls[i];");
        open("if (s.o == e.currentTarget && s.dn) {");
        line("s.vy += e.delta * 4;");
        close("} else if (s.o == e.currentTarget) {");
        ++_depth;
        line("s.vx += e.delta * 4;");
        close();
        close();
        close();
        open("function phxScrollStep():void {");
        scrollStepBody();
        close();
    }

    void scrollFunctionsAs2()
    {
        line("// Makes each scrolling clip scroll inside its box, again as it comes back on the stage.");
        open("phxT.phxScrollHook = function () {");
        open("for (var i = 0; i < phxT.phxScrolls.length; i++) {");
        line("var s = phxT.phxScrolls[i];");
        line("var o = phxT[s.n];");
        open("if (o == undefined || o == s.o) {");
        line("continue;");
        close();
        line("s.o = o;");
        line("s.x0 = o._x;");
        line("s.y0 = o._y;");
        line("s.vx = 0;");
        line("s.vy = 0;");
        line("s.held = false;");
        line("var m = phxT.createEmptyMovieClip(\"phxScrollMask\" + i, 1048100 + i);");
        // A player that cannot draw shows the whole clip, still scrolling.
        open("if (m.beginFill != undefined) {");
        line("m.beginFill(0);");
        line("m.moveTo(o._x, o._y);");
        line("m.lineTo(o._x + s.w, o._y);");
        line("m.lineTo(o._x + s.w, o._y + s.h);");
        line("m.lineTo(o._x, o._y + s.h);");
        line("m.lineTo(o._x, o._y);");
        line("m.endFill();");
        line("o.setMask(m);");
        close();
        line("o.phxScroll = s;");
        open("o.onPress = function () {");
        line("this.phxScroll.held = true;");
        line("this.phxScroll.px = phxT._xmouse;");
        line("this.phxScroll.py = phxT._ymouse;");
        line("this.phxScroll.vx = 0;");
        line("this.phxScroll.vy = 0;");
        close("};");
        open("o.onRelease = o.onReleaseOutside = function () {");
        line("this.phxScroll.held = false;");
        close("};");
        close();
        close("};");
        open("phxT.phxScrollStep = function () {");
        scrollStepBody();
        close("};");
        line("var phxWheel = {};");
        open("phxWheel.onMouseWheel = function (delta) {");
        open("for (var i = 0; i < phxT.phxScrolls.length; i++) {");
        line("var s = phxT.phxScrolls[i];");
        open("if (s.o != undefined && s.o.hitTest(_root._xmouse, _root._ymouse, true)) {");
        open("if (s.dn) {");
        line("s.vy += delta * 4;");
        close("} else {");
        ++_depth;
        line("s.vx += delta * 4;");
        close();
        close();
        close();
        close("};");
        open("if (Mouse.addListener != undefined) {");
        line("Mouse.addListener(phxWheel);");
        close();
    }

    void frameBody()
    {
        line(me("phxTicks") + "++;");
        line(me("phxHook") + "();");
        if (!_binds.empty())
            line(me("phxBind") + "();");
        if (!_pins.empty())
            line(me("phxLayout") + "();");
        if (!_scrolls.empty())
            line(me("phxScrollStep") + "();");
        line(std::string(_as3 ? "var f:int = " : "var f = ") + me(_as3 ? "currentFrame" : "_currentframe") + ";");
        playBody(_own);
        line(me("phxAt") + " = f;");
        for (const Lane& lane : _lanes)
            line(mine(lane, "phxStep") + "();");
        line(me("phxCheck") + "();");
    }

    /// A layer's look each frame: its clip come back on the stage goes
    /// back to its state; else as the timeline's own.
    void stepBody(const Lane& lane)
    {
        line(mine(lane, "phxTicks") + "++;");
        line(fetch(lane));
        open("if (phxC == null) {");
        line("return;");
        close();
        open("if (phxC != " + mine(lane, "phxSeen") + ") {");
        line(mine(lane, "phxGo") + "(" + mine(lane, "phxState") + ");");
        line("return;");
        close();
        line(std::string(_as3 ? "var f:int = " : "var f = ") + frameOf(lane) + ";");
        playBody(lane);
        line(mine(lane, "phxAt") + " = f;");
    }

    void stateSwitch(const Lane& lane, bool events)
    {
        bool first = true;
        for (const int index : lane.states)
        {
            const std::vector<const Transition*> list = leaving(lane, index, events);
            if (list.empty())
                continue;
            open(std::string(first ? "" : "} else ") + "if (" + mine(lane, "phxState") + " == " +
                quoted(_graph.states[static_cast<size_t>(index)].label) + ") {");
            first = false;
            for (const Transition* transition : list)
                go(lane, *transition);
            --_depth;
        }
        if (!first)
            line("}");
    }

    void eventBody()
    {
        listeners();
        if (!_binds.empty())
            line(me("phxBind") + "();");
        for (const Lane& lane : _lanes)
            line(mine(lane, "phxEvent") + "(target, what);");
        open("if (" + me("phxWait") + " == null) {");
        stateSwitch(_own, true);
        close();
        line(me("phxCheck") + "();");
    }

    void laneEventBody(const Lane& lane)
    {
        open("if (" + mine(lane, "phxWait") + " == null) {");
        stateSwitch(lane, true);
        close();
    }

    void checkBody(const Lane& lane)
    {
        if (lane.own())
            for (const Lane& each : _lanes)
                line(mine(each, "phxCheck") + "();");
        open("if (" + mine(lane, "phxWait") + " != null) {");
        open("if (" + mine(lane, "phxDone") + ") {");
        line(mine(lane, "phxGo") + "(" + mine(lane, "phxWait") + ");");
        close();
        line("return;");
        close();
        stateSwitch(lane, false);
    }

    std::string entry(const Lane& lane) const
    {
        if (lane.entry < 0 || lane.entry >= static_cast<int>(_graph.states.size()))
            return quoted(std::string());
        return quoted(_graph.states[static_cast<size_t>(lane.entry)].label);
    }

    std::string startValue(const Input& each) const
    {
        if (each.kind == InputKind::Number)
            return numberText(each.value);
        if (each.kind == InputKind::Switch)
            return each.value != 0.0 ? "true" : "false";
        return "false";
    }

    void writeAs3()
    {
        for (const char* declared : {"phxState:String", "phxInputs:Object", "phxTicks:int", "phxDone:Boolean",
                 "phxWait:String", "phxAt:int", "phxReady:Boolean", "phxKeys:Boolean"})
            line(std::string("var ") + declared + ";");
        for (const Lane& lane : _lanes)
            for (const char* declared : {"phxState%:String", "phxTicks%:int", "phxDone%:Boolean", "phxWait%:String",
                     "phxAt%:int", "phxSeen%:Object"})
            {
                std::string text = declared;
                text.replace(text.find('%'), 1, lane.suffix);
                line("var " + text + ";");
            }
        line("// Goes to a state: to its label, playing or held.");
        open("function phxGo(state:String):void {");
        goBody(_own);
        close();
        line("// Goes now, or once this state's animation has finished.");
        open("function phxTo(state:String, after:Boolean):void {");
        toBody(_own);
        close();
        line("// Listens to the named buttons and clips, again as they come back on the stage.");
        open("function phxHook():void {");
        line("var o:Object;");
        for (size_t i = 0; i < _hooks.size(); ++i)
        {
            if (i == 0 || _hooks[i].first != _hooks[i - 1].first)
                line("o = getChildByName(" + quoted(_hooks[i].first) + ");");
            open("if (o != null) {");
            line("o.addEventListener(\"" + std::string(as3Event(_hooks[i].second)) + "\", phxOn" + std::to_string(i) + ");");
            close();
        }
        if (_keys)
        {
            open("if (stage != null && !phxKeys) {");
            line("phxKeys = true;");
            line("stage.addEventListener(\"keyDown\", phxKey);");
            close();
        }
        close();
        for (size_t i = 0; i < _hooks.size(); ++i)
        {
            open("function phxOn" + std::to_string(i) + "(e:*):void {");
            line("phxEvent(" + quoted(_hooks[i].first) + ", \"" + _hooks[i].second + "\");");
            close();
        }
        if (_keys)
        {
            open("function phxKey(e:*):void {");
            line("phxEvent(\"key\", String(e.keyCode));");
            close();
        }
        line("// Each frame: loops a state round, stops one played once at its end, and checks.");
        open("function phxFrame(e:*):void {");
        frameBody();
        close();
        line("// A click or a key: the listeners set inputs, then a transition may go.");
        open("function phxEvent(target:String, what:String):void {");
        eventBody();
        close();
        line("// The transitions that wait on an input, the animation's end or a timer.");
        open("function phxCheck():void {");
        checkBody(_own);
        close();
        if (!_binds.empty())
        {
            line("// Shows the inputs in the named clips and text fields.");
            open("function phxBind():void {");
            bindBody();
            close();
        }
        if (!_pins.empty())
        {
            line("// Keeps the pinned instances at their distance from the window's edges.");
            line("var phxLaid:Boolean;");
            line("var phxPinned:Array;");
            open("function phxLayout(e:* = null):void {");
            layoutBody();
            close();
        }
        if (!_scrolls.empty())
        {
            line("var phxScrolls:Array;");
            scrollFunctionsAs3();
        }
        for (const Lane& lane : _lanes)
        {
            line("// The " + quoted(lane.target) + " layer: the same for that movie clip's states.");
            open("function phxGo" + lane.suffix + "(state:String):void {");
            goBody(lane);
            close();
            open("function phxTo" + lane.suffix + "(state:String, after:Boolean):void {");
            toBody(lane);
            close();
            open("function phxStep" + lane.suffix + "():void {");
            stepBody(lane);
            close();
            open("function phxEvent" + lane.suffix + "(target:String, what:String):void {");
            laneEventBody(lane);
            close();
            open("function phxCheck" + lane.suffix + "():void {");
            checkBody(lane);
            close();
        }
        line("// Starts in the first state, the first time this frame plays.");
        open("if (!phxReady) {");
        line("phxReady = true;");
        line("phxInputs = {};");
        if (!_scrolls.empty())
            line("phxScrolls = " + scrollList() + ";");
        for (const Input& each : _graph.inputs)
            line(input(each.name) + " = " + startValue(each) + ";");
        line("addEventListener(\"enterFrame\", phxFrame);");
        line("phxGo(" + entry(_own) + ");");
        for (const Lane& lane : _lanes)
            line("phxGo" + lane.suffix + "(" + entry(lane) + ");");
        if (!_binds.empty())
            line("phxBind();");
        if (!_pins.empty())
            line("phxLayout();");
        if (!_scrolls.empty())
            line("phxScrollHook();");
        close();
    }

    void writeAs2()
    {
        line("// Starts in the first state, the first time this frame plays.");
        line("var phxT = this;");
        open("if (!phxT.phxReady) {");
        line("phxT.phxReady = true;");
        line("phxT.phxInputs = {};");
        for (const Input& each : _graph.inputs)
            line(input(each.name) + " = " + startValue(each) + ";");
        line("// Goes to a state: to its label, playing or held.");
        open("phxT.phxGo = function (state) {");
        goBody(_own);
        close("};");
        line("// Goes now, or once this state's animation has finished.");
        open("phxT.phxTo = function (state, after) {");
        toBody(_own);
        close("};");
        line("// Listens to the named buttons and clips, again as they come back on the stage.");
        open("phxT.phxHook = function () {");
        line("var o;");
        for (size_t i = 0; i < _hooks.size(); ++i)
        {
            if (i == 0 || _hooks[i].first != _hooks[i - 1].first)
                line("o = phxT[" + quoted(_hooks[i].first) + "];");
            open("if (o != undefined) {");
            line(std::string("o.") + as2Handler(_hooks[i].second) + " = phxT.phxOn" + std::to_string(i) + ";");
            close();
        }
        close("};");
        for (size_t i = 0; i < _hooks.size(); ++i)
        {
            open("phxT.phxOn" + std::to_string(i) + " = function () {");
            line("phxT.phxEvent(" + quoted(_hooks[i].first) + ", \"" + _hooks[i].second + "\");");
            close("};");
        }
        line("// Each frame: loops a state round, stops one played once at its end, and checks.");
        open("phxT.phxFrame = function () {");
        frameBody();
        close("};");
        line("// A click or a key: the listeners set inputs, then a transition may go.");
        open("phxT.phxEvent = function (target, what) {");
        eventBody();
        close("};");
        line("// The transitions that wait on an input, the animation's end or a timer.");
        open("phxT.phxCheck = function () {");
        checkBody(_own);
        close("};");
        if (!_binds.empty())
        {
            line("// Shows the inputs in the named clips and text fields.");
            open("phxT.phxBind = function () {");
            bindBody();
            close("};");
        }
        if (!_pins.empty())
        {
            line("// Keeps the pinned instances at their distance from the window's edges.");
            open("phxT.phxLayout = function () {");
            layoutBody();
            close("};");
        }
        if (!_scrolls.empty())
        {
            line("phxT.phxScrolls = " + scrollList() + ";");
            scrollFunctionsAs2();
        }
        for (const Lane& lane : _lanes)
        {
            line("// The " + quoted(lane.target) + " layer: the same for that movie clip's states.");
            open("phxT.phxGo" + lane.suffix + " = function (state) {");
            goBody(lane);
            close("};");
            open("phxT.phxTo" + lane.suffix + " = function (state, after) {");
            toBody(lane);
            close("};");
            open("phxT.phxStep" + lane.suffix + " = function () {");
            stepBody(lane);
            close("};");
            open("phxT.phxEvent" + lane.suffix + " = function (target, what) {");
            laneEventBody(lane);
            close("};");
            open("phxT.phxCheck" + lane.suffix + " = function () {");
            checkBody(lane);
            close("};");
        }
        // Each frame from a clip of its own, so the timeline's own
        // onEnterFrame stays free.
        line("phxT.createEmptyMovieClip(\"phxTicker\", 1048000);");
        open("phxT.phxTicker.onEnterFrame = function () {");
        line("phxT.phxFrame();");
        close("};");
        if (_keys)
        {
            line("var phxKeys = {};");
            open("phxKeys.onKeyDown = function () {");
            line("phxT.phxEvent(\"key\", String(Key.getCode()));");
            close("};");
            line("Key.addListener(phxKeys);");
        }
        line("phxT.phxGo(" + entry(_own) + ");");
        for (const Lane& lane : _lanes)
            line("phxT.phxGo" + lane.suffix + "(" + entry(lane) + ");");
        if (!_binds.empty())
            line("phxT.phxBind();");
        if (!_pins.empty())
            line("phxT.phxLayout();");
        if (!_scrolls.empty())
            line("phxT.phxScrollHook();");
        close();
    }

    const Graph& _graph;
    const Timeline& _timeline;
    bool _as3;
    double _frameRate;
    std::vector<std::pair<std::string, std::string>> _hooks;
    bool _keys = false;
    std::ostringstream _out;
    int _depth = 0;
    Lane _own;
    std::vector<Lane> _lanes;
    std::vector<Binding> _binds;
    std::vector<Pin> _pins;
    std::vector<Scroller> _scrolls;
    double _stageWidth = 550.0;
    double _stageHeight = 400.0;
};

} // namespace

int Graph::stateNamed(const std::string& label) const
{
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].label == label)
            return static_cast<int>(i);
    return -1;
}

int Graph::stateNamed(const std::string& label, int layer) const
{
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].label == label && states[i].layer == layer)
            return static_cast<int>(i);
    return -1;
}

const Input* Graph::input(const std::string& name) const
{
    for (const Input& each : inputs)
        if (each.name == name)
            return &each;
    return nullptr;
}

int Graph::entryOf(int layer) const
{
    const int count = static_cast<int>(states.size());
    const auto layerOf = [this](int index) {
        const int each = states[static_cast<size_t>(index)].layer;
        return each > 0 && each <= static_cast<int>(layers.size()) ? each : 0;
    };
    int wanted = layer == 0 ? entry
        : layer > 0 && layer <= static_cast<int>(layers.size()) ? layers[static_cast<size_t>(layer - 1)].entry
                                                                 : -1;
    if (wanted >= 0 && wanted < count && layerOf(wanted) == layer)
        return wanted;
    for (int i = 0; i < count; ++i)
        if (layerOf(i) == layer)
            return i;
    return -1;
}

void Graph::removeState(int index)
{
    if (index < 0 || index >= static_cast<int>(states.size()))
        return;
    states.erase(states.begin() + index);
    for (StateLayer& layer : layers)
    {
        if (layer.entry == index)
            layer.entry = -1;
        else if (layer.entry > index)
            --layer.entry;
    }
    std::vector<Transition> kept;
    for (Transition transition : transitions)
    {
        if (transition.from == index || transition.to == index)
            continue;
        if (transition.from > index)
            --transition.from;
        if (transition.to > index)
            --transition.to;
        kept.push_back(transition);
    }
    transitions = kept;
    if (entry == index)
        entry = 0;
    else if (entry > index)
        --entry;
}

void Graph::removeLayer(int layer)
{
    if (layer < 1 || layer > static_cast<int>(layers.size()))
        return;
    for (int i = static_cast<int>(states.size()) - 1; i >= 0; --i)
        if (states[static_cast<size_t>(i)].layer == layer)
            removeState(i);
    layers.erase(layers.begin() + (layer - 1));
    for (State& state : states)
        if (state.layer > layer)
            --state.layer;
}

void Graph::renameInput(const std::string& from, const std::string& to)
{
    for (Input& input : inputs)
        if (input.name == from)
            input.name = to;
    for (Transition& transition : transitions)
        if (transition.condition.input == from)
            transition.condition.input = to;
    for (Listener& listener : listeners)
        if (listener.input == from)
            listener.input = to;
    for (State& state : states)
        if (state.blend == from)
            state.blend = to;
    for (Binding& binding : bindings)
        if (binding.input == from)
            binding.input = to;
}

void Graph::removeInput(const std::string& name)
{
    inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [&](const Input& each) { return each.name == name; }),
        inputs.end());
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const Binding& each) { return each.input == name; }),
        bindings.end());
    for (State& state : states)
        if (state.blend == name)
            state.blend.clear();
    for (Transition& transition : transitions)
        if (transition.condition.input == name)
            transition.condition = Condition();
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                        [&](const Listener& each) { return each.input == name; }),
        listeners.end());
}

bool Graph::operator==(const Graph& other) const
{
    return encode(*this) == encode(other);
}

std::string encode(const Graph& graph)
{
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLElement* root = doc.NewElement("states");
    doc.InsertEndChild(root);
    root->SetAttribute("version", 1);
    root->SetAttribute("entry", graph.entry);
    for (const State& state : graph.states)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("state");
        e->SetAttribute("label", state.label.c_str());
        if (!state.until.empty())
            e->SetAttribute("until", state.until.c_str());
        e->SetAttribute("play", spelled(kPlays, state.play));
        e->SetAttribute("x", numberText(state.x).c_str());
        e->SetAttribute("y", numberText(state.y).c_str());
        if (state.layer > 0)
            e->SetAttribute("layer", state.layer);
        if (state.play == Play::Blend)
        {
            e->SetAttribute("blend", state.blend.c_str());
            e->SetAttribute("low", numberText(state.low).c_str());
            e->SetAttribute("high", numberText(state.high).c_str());
        }
    }
    for (const Binding& binding : graph.bindings)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("bind");
        e->SetAttribute("input", binding.input.c_str());
        e->SetAttribute("target", binding.target.c_str());
        e->SetAttribute("property", binding.property.c_str());
        if (mapped(binding.property))
        {
            e->SetAttribute("low", numberText(binding.low).c_str());
            e->SetAttribute("high", numberText(binding.high).c_str());
            e->SetAttribute("from", numberText(binding.from).c_str());
            e->SetAttribute("to", numberText(binding.to).c_str());
        }
    }
    for (const Pin& pin : graph.pins)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("pin");
        e->SetAttribute("target", pin.target.c_str());
        std::string edges;
        for (const auto& [on, letter] : {std::pair<bool, char>{pin.left, 'l'}, {pin.right, 'r'}, {pin.top, 't'},
                 {pin.bottom, 'b'}})
            if (on)
                edges += letter;
        e->SetAttribute("edges", edges.c_str());
    }
    for (const Scroller& scroller : graph.scrollers)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("scroll");
        e->SetAttribute("target", scroller.target.c_str());
        e->SetAttribute("width", numberText(scroller.width).c_str());
        e->SetAttribute("height", numberText(scroller.height).c_str());
        e->SetAttribute("across", scroller.across);
        e->SetAttribute("down", scroller.down);
        e->SetAttribute("glide", numberText(scroller.glide).c_str());
    }
    for (const StateLayer& layer : graph.layers)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("layer");
        e->SetAttribute("name", layer.name.c_str());
        e->SetAttribute("target", layer.target.c_str());
        if (layer.entry >= 0)
            e->SetAttribute("entry", layer.entry);
    }
    for (const Input& input : graph.inputs)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("input");
        e->SetAttribute("name", input.name.c_str());
        e->SetAttribute("kind", spelled(kKinds, input.kind));
        if (input.value != 0.0)
            e->SetAttribute("value", numberText(input.value).c_str());
    }
    for (const Transition& transition : graph.transitions)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("transition");
        e->SetAttribute("from", transition.from);
        e->SetAttribute("to", transition.to);
        e->SetAttribute("when", spelled(kWhens, transition.when));
        if (!transition.target.empty())
            e->SetAttribute("target", transition.target.c_str());
        if (transition.when == When::Timer)
            e->SetAttribute("seconds", numberText(transition.seconds).c_str());
        if (transition.afterFinish)
            e->SetAttribute("afterFinish", "true");
        writeCondition(e, transition.condition);
    }
    for (const Listener& listener : graph.listeners)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("listener");
        e->SetAttribute("when", spelled(kWhens, listener.when));
        if (!listener.target.empty())
            e->SetAttribute("target", listener.target.c_str());
        e->SetAttribute("input", listener.input.c_str());
        e->SetAttribute("change", spelled(kChanges, listener.change));
        if (listener.value != 0.0)
            e->SetAttribute("value", numberText(listener.value).c_str());
    }
    tinyxml2::XMLPrinter printer(nullptr, true);
    doc.Print(&printer);
    return printer.CStr();
}

bool decode(const std::string& text, Graph& graph)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS)
        return false;
    const tinyxml2::XMLElement* root = doc.FirstChildElement("states");
    if (!root)
        return false;
    Graph read;
    read.entry = root->IntAttribute("entry", 0);
    for (const tinyxml2::XMLElement* e = root->FirstChildElement(); e; e = e->NextSiblingElement())
    {
        const std::string name = e->Name();
        if (name == "state")
        {
            State state;
            state.label = e->Attribute("label") ? e->Attribute("label") : "";
            state.until = e->Attribute("until") ? e->Attribute("until") : "";
            state.play = spelledAs(kPlays, e->Attribute("play"));
            state.x = numberOf(e->Attribute("x"));
            state.y = numberOf(e->Attribute("y"));
            state.layer = e->IntAttribute("layer", 0);
            state.blend = e->Attribute("blend") ? e->Attribute("blend") : "";
            state.low = numberOf(e->Attribute("low"), 0.0);
            state.high = numberOf(e->Attribute("high"), 1.0);
            read.states.push_back(state);
        }
        else if (name == "pin")
        {
            Pin pin;
            pin.target = e->Attribute("target") ? e->Attribute("target") : "";
            const std::string edges = e->Attribute("edges") ? e->Attribute("edges") : "lt";
            pin.left = edges.find('l') != std::string::npos;
            pin.right = edges.find('r') != std::string::npos;
            pin.top = edges.find('t') != std::string::npos;
            pin.bottom = edges.find('b') != std::string::npos;
            read.pins.push_back(pin);
        }
        else if (name == "scroll")
        {
            Scroller scroller;
            scroller.target = e->Attribute("target") ? e->Attribute("target") : "";
            scroller.width = std::max(1.0, numberOf(e->Attribute("width"), 200.0));
            scroller.height = std::max(1.0, numberOf(e->Attribute("height"), 200.0));
            scroller.across = e->BoolAttribute("across", false);
            scroller.down = e->BoolAttribute("down", true);
            scroller.glide = std::clamp(numberOf(e->Attribute("glide"), 0.92), 0.0, 0.99);
            read.scrollers.push_back(scroller);
        }
        else if (name == "bind")
        {
            Binding binding;
            binding.input = e->Attribute("input") ? e->Attribute("input") : "";
            binding.target = e->Attribute("target") ? e->Attribute("target") : "";
            binding.property = e->Attribute("property") ? e->Attribute("property") : "text";
            binding.low = numberOf(e->Attribute("low"), 0.0);
            binding.high = numberOf(e->Attribute("high"), 1.0);
            binding.from = numberOf(e->Attribute("from"), 0.0);
            binding.to = numberOf(e->Attribute("to"), 1.0);
            read.bindings.push_back(binding);
        }
        else if (name == "layer")
        {
            StateLayer layer;
            layer.name = e->Attribute("name") ? e->Attribute("name") : "";
            layer.target = e->Attribute("target") ? e->Attribute("target") : "";
            layer.entry = e->IntAttribute("entry", -1);
            read.layers.push_back(layer);
        }
        else if (name == "input")
        {
            Input input;
            input.name = e->Attribute("name") ? e->Attribute("name") : "";
            input.kind = spelledAs(kKinds, e->Attribute("kind"));
            input.value = numberOf(e->Attribute("value"));
            read.inputs.push_back(input);
        }
        else if (name == "transition")
        {
            Transition transition;
            transition.from = e->IntAttribute("from", -1);
            transition.to = e->IntAttribute("to", 0);
            transition.when = spelledAs(kWhens, e->Attribute("when"));
            transition.target = e->Attribute("target") ? e->Attribute("target") : "";
            transition.seconds = numberOf(e->Attribute("seconds"), 1.0);
            transition.afterFinish = e->BoolAttribute("afterFinish", false);
            transition.condition = readCondition(e);
            read.transitions.push_back(transition);
        }
        else if (name == "listener")
        {
            Listener listener;
            listener.when = spelledAs(kWhens, e->Attribute("when"));
            listener.target = e->Attribute("target") ? e->Attribute("target") : "";
            listener.input = e->Attribute("input") ? e->Attribute("input") : "";
            listener.change = spelledAs(kChanges, e->Attribute("change"));
            listener.value = numberOf(e->Attribute("value"));
            read.listeners.push_back(listener);
        }
    }
    // Transitions to states that are not there are left out.
    const int count = static_cast<int>(read.states.size());
    read.transitions.erase(std::remove_if(read.transitions.begin(), read.transitions.end(),
                               [count](const Transition& t) { return t.to < 0 || t.to >= count || t.from < -1 || t.from >= count; }),
        read.transitions.end());
    if (read.entry < 0 || read.entry >= count)
        read.entry = 0;
    // States of a layer that is not there are the timeline's own.
    for (State& state : read.states)
        if (state.layer < 0 || state.layer > static_cast<int>(read.layers.size()))
            state.layer = 0;
    for (StateLayer& layer : read.layers)
        if (layer.entry >= count)
            layer.entry = -1;
    graph = read;
    return true;
}

Graph of(const Timeline& timeline)
{
    Graph graph;
    for (const auto& [name, value] : timeline.extraAttributes)
        if (name == kAttribute)
            decode(value, graph);
    return graph;
}

Attributes withGraph(const Timeline& timeline, const Graph& graph)
{
    Attributes attributes;
    bool placed = false;
    for (const auto& attribute : timeline.extraAttributes)
    {
        if (attribute.first != kAttribute)
            attributes.push_back(attribute);
        else if (!graph.empty() && !placed)
        {
            attributes.emplace_back(kAttribute, encode(graph));
            placed = true;
        }
    }
    if (!graph.empty() && !placed)
        attributes.emplace_back(kAttribute, encode(graph));
    return attributes;
}

const std::vector<std::pair<std::string, int>>& keyNames()
{
    static const std::vector<std::pair<std::string, int>> keys = []() {
        std::vector<std::pair<std::string, int>> list = {{"Any key", 0}, {"Space", 32}, {"Enter", 13}, {"Left arrow", 37},
            {"Up arrow", 38}, {"Right arrow", 39}, {"Down arrow", 40}, {"Escape", 27}, {"Tab", 9}};
        for (char c = 'A'; c <= 'Z'; ++c)
            list.emplace_back(std::string(1, c), c);
        for (char c = '0'; c <= '9'; ++c)
            list.emplace_back(std::string(1, c), c);
        return list;
    }();
    return keys;
}

std::string words(When when)
{
    switch (when)
    {
    case When::Click: return "is clicked";
    case When::Press: return "is pressed";
    case When::Release: return "is released";
    case When::HoverIn: return "has the pointer come over it";
    case When::HoverOut: return "has the pointer leave it";
    case When::Key: return "a key is pressed";
    case When::Finished: return "its animation finishes";
    case When::Timer: return "some time has gone by";
    case When::Condition: return "an input changes";
    }
    return std::string();
}

std::string words(Play play)
{
    switch (play)
    {
    case Play::Loop: return "loops";
    case Play::Once: return "plays once";
    case Play::Hold: return "holds its first frame";
    case Play::Blend: return "follows a number";
    }
    return std::string();
}

std::string words(Compare compare)
{
    switch (compare)
    {
    case Compare::None: return "(anything)";
    case Compare::IsOn: return "is on";
    case Compare::IsOff: return "is off";
    case Compare::Fired: return "is fired";
    case Compare::Equal: return "=";
    case Compare::NotEqual: return "is not";
    case Compare::Less: return "<";
    case Compare::More: return ">";
    case Compare::AtLeast: return "is at least";
    case Compare::AtMost: return "is at most";
    }
    return std::string();
}

std::string words(Change change)
{
    switch (change)
    {
    case Change::TurnOn: return "turn on";
    case Change::TurnOff: return "turn off";
    case Change::Toggle: return "flip";
    case Change::Set: return "set to";
    case Change::Add: return "add";
    case Change::Fire: return "fire";
    }
    return std::string();
}

std::string words(InputKind kind)
{
    switch (kind)
    {
    case InputKind::Switch: return "On/off switch";
    case InputKind::Number: return "Number";
    case InputKind::Trigger: return "Trigger";
    }
    return std::string();
}

namespace {

std::string conditionWords(const Condition& condition)
{
    if (condition.empty())
        return std::string();
    std::string text = condition.input + " " + words(condition.compare);
    if (condition.compare >= Compare::Equal)
        text += " " + numberText(condition.value);
    return text;
}

std::string eventWords(When when, const std::string& target, double seconds)
{
    switch (when)
    {
    case When::Key: return target.empty() || target == "Any key" ? "any key is pressed" : target + " is pressed";
    case When::Finished: return "it finishes";
    case When::Timer: return "after " + numberText(seconds) + " s";
    case When::Condition: return std::string();
    case When::HoverIn: return "pointer over " + (target.empty() ? std::string("?") : target);
    case When::HoverOut: return "pointer off " + (target.empty() ? std::string("?") : target);
    default: return (target.empty() ? std::string("?") : target) + " " + words(when);
    }
}

} // namespace

std::string describe(const Graph& graph, const Transition& transition)
{
    (void)graph;
    std::string text = eventWords(transition.when, transition.target, transition.seconds);
    const std::string condition = conditionWords(transition.condition);
    if (!condition.empty())
        text = text.empty() ? condition : text + ", if " + condition;
    if (text.empty())
        text = "(never)";
    if (transition.afterFinish && transition.when != When::Finished)
        text += " (at its end)";
    return text;
}

std::string describe(const Listener& listener)
{
    const std::string input = listener.input.empty() ? std::string("?") : listener.input;
    std::string change;
    if (listener.change == Change::Set)
        change = "set " + input + " to " + numberText(listener.value);
    else if (listener.change == Change::Add)
        change = "add " + numberText(listener.value) + " to " + input;
    else
        change = words(listener.change) + " " + input;
    return "When " + eventWords(listener.when, listener.target, 0.0) + ": " + change;
}

const std::vector<std::string>& bindProperties()
{
    static const std::vector<std::string> list = {"text", "visible", "frame", "x", "y", "rotation", "alpha", "scale"};
    return list;
}

std::string words(const std::string& property)
{
    if (property == "text")
        return "text";
    if (property == "visible")
        return "visibility";
    if (property == "frame")
        return "frame";
    if (property == "rotation")
        return "turn";
    if (property == "alpha")
        return "fade (%)";
    if (property == "scale")
        return "size (%)";
    return property;
}

bool mapped(const std::string& property)
{
    return property != "text" && property != "visible";
}

std::string describe(const Binding& binding)
{
    const std::string input = binding.input.empty() ? std::string("?") : binding.input;
    const std::string target = binding.target.empty() ? std::string("?") : binding.target;
    if (binding.property == "text")
        return input + " shows in " + target;
    if (binding.property == "visible")
        return input + " shows or hides " + target;
    return input + " sets " + target + "'s " + words(binding.property) + " (" + numberText(binding.low) + " to " +
        numberText(binding.high) + " as " + numberText(binding.from) + " to " + numberText(binding.to) + ")";
}

std::string describe(const Pin& pin)
{
    const auto axis = [](bool low, bool high, const char* lowName, const char* highName) {
        if (low && high)
            return std::string(lowName) + " and " + highName + " (stretches)";
        if (low)
            return std::string(lowName);
        if (high)
            return std::string(highName);
        return std::string("the middle");
    };
    return (pin.target.empty() ? std::string("?") : pin.target) + ": " + axis(pin.left, pin.right, "left", "right") +
        ", " + axis(pin.top, pin.bottom, "top", "bottom");
}

std::string describe(const Scroller& scroller)
{
    const std::string way = scroller.across && scroller.down ? "both ways" : scroller.across ? "across" : "down";
    return (scroller.target.empty() ? std::string("?") : scroller.target) + " scrolls " + way + " in " +
        numberText(scroller.width) + " x " + numberText(scroller.height);
}

std::vector<std::pair<std::string, int>> labels(const Timeline& timeline)
{
    std::map<std::string, int> first;
    for (const Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        for (const Frame* frame : layer->frames)
        {
            if (!frame || frame->name.empty() || frame->labelType == "comment")
                continue;
            const auto found = first.find(frame->name);
            if (found == first.end() || frame->index < found->second)
                first[frame->name] = frame->index;
        }
    }
    std::vector<std::pair<std::string, int>> list(first.begin(), first.end());
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return list;
}

int length(const Timeline& timeline)
{
    int end = 0;
    for (const Layer* layer : timeline.layers)
        if (layer)
            for (const Frame* frame : layer->frames)
                if (frame)
                    end = std::max(end, frame->index + frame->duration);
    return end;
}

bool range(const Timeline& timeline, const State& state, int& first, int& last)
{
    const std::vector<std::pair<std::string, int>> all = labels(timeline);
    int start = -1, through = -1;
    for (const auto& [name, frame] : all)
    {
        if (name == state.label)
            start = frame;
        if (!state.until.empty() && name == state.until)
            through = frame;
    }
    if (start < 0)
        return false;
    if (through < start)
        through = start;
    int end = std::max(length(timeline), start + 1) - 1;
    for (const auto& [name, frame] : all)
        if (frame > through)
        {
            end = frame - 1;
            break;
        }
    first = start;
    last = std::max(start, end);
    return true;
}

std::vector<std::string> instanceNames(const Timeline& timeline)
{
    std::set<std::string> names;
    for (const Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        for (const Frame* frame : layer->frames)
        {
            if (!frame)
                continue;
            for (const Element* element : frame->elements)
            {
                if (!element || element->elementType() != Element::Type::SymbolInstance)
                    continue;
                const auto* instance = static_cast<const SymbolInstance*>(element);
                if (!instance->name.empty() &&
                    (instance->symbolType == SymbolType::MovieClip || instance->symbolType == SymbolType::Button))
                    names.insert(instance->name);
            }
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

std::vector<std::string> bindableNames(const Timeline& timeline)
{
    std::set<std::string> names;
    for (const std::string& name : instanceNames(timeline))
        names.insert(name);
    for (const Layer* layer : timeline.layers)
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                if (element && element->elementType() == Element::Type::StaticText)
                {
                    const auto* text = static_cast<const StaticText*>(element);
                    if (!text->instanceName.empty())
                        names.insert(text->instanceName);
                }
    return std::vector<std::string>(names.begin(), names.end());
}

const Timeline* clipTimeline(const Timeline& timeline, const std::string& instance)
{
    if (instance.empty())
        return nullptr;
    for (const Layer* layer : timeline.layers)
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            for (const Element* element : frame ? frame->elements : std::vector<Element*>())
            {
                if (!element || element->elementType() != Element::Type::SymbolInstance)
                    continue;
                const auto* placed = static_cast<const SymbolInstance*>(element);
                if (placed->name == instance && placed->symbol && !placed->symbol->timelines.empty())
                    return placed->symbol->timelines.front();
            }
    return nullptr;
}

const Timeline* layerTimeline(const Timeline& timeline, const Graph& graph, int layer)
{
    if (layer <= 0 || layer > static_cast<int>(graph.layers.size()))
        return &timeline;
    return clipTimeline(timeline, graph.layers[static_cast<size_t>(layer - 1)].target);
}

std::string compile(const Graph& graph, const Timeline& timeline, int version, double frameRate)
{
    if (graph.empty())
        return std::string();
    return Writer(graph, timeline, version, frameRate).write();
}

namespace {

/// Where the block is in \a script: the offset its first line starts at
/// and the one after its last line; false when there is none.
bool blockAt(const std::string& script, size_t& from, size_t& to)
{
    const std::string begin = std::string(kBegin).substr(0, 11);
    size_t start = script.find(begin);
    while (start != std::string::npos && start > 0 && script[start - 1] != '\n')
        start = script.find(begin, start + 1);
    if (start == std::string::npos)
        return false;
    size_t end = script.find(kEnd, start);
    if (end == std::string::npos)
        return false;
    end += std::strlen(kEnd);
    if (end < script.size() && script[end] == '\r')
        ++end;
    if (end < script.size() && script[end] == '\n')
        ++end;
    from = start;
    to = end;
    return true;
}

} // namespace

std::string blockOf(const std::string& script)
{
    size_t from = 0, to = 0;
    return blockAt(script, from, to) ? script.substr(from, to - from) : std::string();
}

std::string withBlock(const std::string& script, const std::string& block)
{
    size_t from = 0, to = 0;
    if (blockAt(script, from, to))
    {
        std::string out = script.substr(0, from) + block + script.substr(to);
        if (block.empty())
        {
            // The blank line put before it goes too.
            if (from >= 1 && out.compare(from - 1, 1, "\n") == 0 && (from < 2 || out[from - 2] == '\n'))
                out.erase(from - 1, 1);
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
                out.pop_back();
            if (!out.empty())
                out += '\n';
        }
        return out;
    }
    if (block.empty())
        return script;
    std::string out = script;
    if (!out.empty())
    {
        if (out.back() != '\n')
            out += '\n';
        out += '\n';
    }
    return out + block;
}

Layer* layerOf(const Timeline& timeline)
{
    for (Layer* layer : timeline.layers)
        if (layer && layer->name == kLayerName && layer->layerType != Layer::Type::Folder)
            return layer;
    return nullptr;
}

namespace {

Frame* keyframeAt(Layer& layer, int index)
{
    for (Frame* frame : layer.frames)
        if (frame && frame->index == index)
            return frame;
    return nullptr;
}

} // namespace

bool stale(const Timeline& timeline, int version, double frameRate)
{
    const Graph graph = of(timeline);
    Layer* layer = layerOf(timeline);
    Frame* first = layer ? keyframeAt(*layer, 0) : nullptr;
    const std::string script = first && first->actionScript ? first->actionScript->code : std::string();
    return blockOf(script) != compile(graph, timeline, version, frameRate);
}

void apply(CommandStack& stack, Timeline& timeline, const Graph& graph, const Edit& edit, int version, double frameRate,
    const std::string& name)
{
    stack.beginMacro(name);
    Layer* layer = layerOf(timeline);
    if (!layer && (!graph.empty() || !edit.newLabels.empty()))
    {
        Layer* made = newLayerFor(timeline, false);
        made->name = kLayerName;
        made->autoNamed = false;
        stack.push(CommandPtr(new AddLayerCommand(&timeline, 0, made, name)));
        layer = made;
    }
    for (const auto& [from, to] : edit.renames)
    {
        if (from.empty() || to.empty() || from == to)
            continue;
        for (Layer* each : timeline.layers)
            if (each)
                for (Frame* frame : each->frames)
                    if (frame && frame->name == from && frame->labelType != "comment")
                        stack.push(CommandPtr(new SetPropertyCommand<std::string>(frame->name, frame->name, to, name)));
    }
    if (layer)
    {
        for (const auto& [label, at] : edit.newLabels)
        {
            auto insert = std::make_unique<InsertKeyframeCommand>(layer, std::max(0, at), name);
            InsertKeyframeCommand* inserting = insert.get();
            stack.push(std::move(insert));
            Frame* frame = inserting->frame();
            if (!frame)
                continue;
            stack.push(CommandPtr(new SetPropertyCommand<std::string>(frame->name, frame->name, label, name)));
            if (frame->labelType != "name")
                stack.push(CommandPtr(new SetPropertyCommand<std::string>(frame->labelType, frame->labelType, "name", name)));
        }
    }
    const Attributes after = withGraph(timeline, graph);
    if (after != timeline.extraAttributes)
        stack.push(CommandPtr(new SetPropertyCommand<Attributes>(timeline.extraAttributes, timeline.extraAttributes, after, name)));
    if (layer)
    {
        Frame* first = keyframeAt(*layer, 0);
        if (!first && !graph.empty())
        {
            auto insert = std::make_unique<InsertKeyframeCommand>(layer, 0, name);
            InsertKeyframeCommand* inserting = insert.get();
            stack.push(std::move(insert));
            first = inserting->frame();
        }
        if (first)
        {
            const std::string before = first->actionScript ? first->actionScript->code : std::string();
            const std::string code = withBlock(before, compile(graph, timeline, version, frameRate));
            if (code != before)
                stack.push(CommandPtr(new scripts::SetFrameScriptCommand(first, code)));
        }
    }
    stack.endMacro();
}

} // namespace states
} // namespace fla
