#include "state_graph.h"

#include "command_stack.h"
#include "frame_commands.h"
#include "layer_commands.h"
#include "property_command.h"
#include "script_edit.h"

#include "../data/action_script.h"
#include "../data/frame.h"
#include "../data/layer.h"
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
const std::vector<std::pair<Play, const char*>> kPlays = {{Play::Loop, "loop"}, {Play::Once, "once"}, {Play::Hold, "hold"}};
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

    /// What the event, key or check \a transition waits for, as a test;
    /// empty when it never goes.
    std::string trigger(const Transition& transition) const
    {
        switch (transition.when)
        {
        case When::Key:
        {
            const int code = keyCode(transition.target);
            return code ? "target == \"key\" && what == \"" + std::to_string(code) + "\"" : "target == \"key\"";
        }
        case When::Finished: return me("phxDone");
        case When::Timer: return me("phxTicks") + " >= " + std::to_string(ticks(transition.seconds));
        case When::Condition: return std::string();
        default:
            return transition.target.empty() ? std::string()
                                             : "target == " + quoted(transition.target) + " && what == \"" +
                    eventName(transition.when, _as3) + "\"";
        }
    }

    /// The transitions that leave state \a index (its own, then any
    /// state's), heard by the event handler or by the check each frame.
    std::vector<const Transition*> leaving(int index, bool events) const
    {
        std::vector<const Transition*> list;
        for (int pass = 0; pass < 2; ++pass)
            for (const Transition& transition : _graph.transitions)
            {
                if (transition.to < 0 || transition.to >= static_cast<int>(_graph.states.size()))
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

    void go(const Transition& transition)
    {
        const std::string test = both(trigger(transition), this->test(transition.condition));
        if (test.empty() && transition.when != When::Finished)
            return;
        open("if (" + test + ") {");
        // A trigger is used up by the transition it sets off.
        const Input* in = _graph.input(transition.condition.input);
        if (in && in->kind == InputKind::Trigger && !transition.condition.empty())
            line(input(in->name) + " = false;");
        line(me("phxTo") + "(" + quoted(_graph.states[static_cast<size_t>(transition.to)].label) + ", " +
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
    void goBody()
    {
        line(me("phxState") + " = state;");
        line(me("phxTicks") + " = 0;");
        line(me("phxDone") + " = false;");
        line(me("phxWait") + " = null;");
        for (const Input& each : _graph.inputs)
            if (each.kind == InputKind::Trigger)
                line(input(each.name) + " = false;");
        bool first = true;
        for (const State& state : _graph.states)
        {
            int from = 0, to = 0;
            if (!range(_timeline, state, from, to))
                continue;
            open(std::string(first ? "" : "} else ") + "if (state == " + quoted(state.label) + ") {");
            const bool still = state.play == Play::Hold || from == to;
            line(me(still ? "gotoAndStop" : "gotoAndPlay") + "(" + quoted(state.label) + ");");
            if (still)
                line(me("phxDone") + " = true;");
            --_depth;
            first = false;
        }
        if (!first)
            line("}");
        line(me("phxAt") + " = " + me(_as3 ? "currentFrame" : "_currentframe") + ";");
        line(me("phxHook") + "();");
    }

    void toBody()
    {
        open("if (after && !" + me("phxDone") + ") {");
        line(me("phxWait") + " = state;");
        close("} else {");
        ++_depth;
        line(me("phxGo") + "(state);");
        close();
    }

    void frameBody()
    {
        line(me("phxTicks") + "++;");
        line(me("phxHook") + "();");
        line(std::string(_as3 ? "var f:int = " : "var f = ") + me(_as3 ? "currentFrame" : "_currentframe") + ";");
        bool first = true;
        for (const State& state : _graph.states)
        {
            int from = 0, to = 0;
            if (!range(_timeline, state, from, to) || state.play == Play::Hold || from == to)
                continue;
            const std::string start = std::to_string(from + 1);
            const std::string end = std::to_string(to + 1);
            open(std::string(first ? "" : "} else ") + "if (" + me("phxState") + " == " + quoted(state.label) + ") {");
            first = false;
            if (state.play == Play::Loop)
            {
                // Past its last frame, or round to its first: again.
                open("if (f < " + start + " || f > " + end + " || f < " + me("phxAt") + ") {");
                line(me("phxDone") + " = false;");
                open("if (f < " + start + " || f > " + end + ") {");
                line(me("gotoAndPlay") + "(" + quoted(state.label) + ");");
                line("f = " + me(_as3 ? "currentFrame" : "_currentframe") + ";");
                close();
                close();
                open("if (f == " + end + ") {");
                line(me("phxDone") + " = true;");
                close();
            }
            else
            {
                open("if (f == " + end + ") {");
                line(me("stop") + "();");
                line(me("phxDone") + " = true;");
                close();
            }
            --_depth;
        }
        if (!first)
            line("}");
        line(me("phxAt") + " = f;");
        line(me("phxCheck") + "();");
    }

    void stateSwitch(bool events)
    {
        bool first = true;
        for (size_t i = 0; i < _graph.states.size(); ++i)
        {
            const std::vector<const Transition*> list = leaving(static_cast<int>(i), events);
            if (list.empty())
                continue;
            open(std::string(first ? "" : "} else ") + "if (" + me("phxState") + " == " + quoted(_graph.states[i].label) +
                ") {");
            first = false;
            for (const Transition* transition : list)
                go(*transition);
            --_depth;
        }
        if (!first)
            line("}");
    }

    void eventBody()
    {
        listeners();
        open("if (" + me("phxWait") + " == null) {");
        stateSwitch(true);
        close();
        line(me("phxCheck") + "();");
    }

    void checkBody()
    {
        open("if (" + me("phxWait") + " != null) {");
        open("if (" + me("phxDone") + ") {");
        line(me("phxGo") + "(" + me("phxWait") + ");");
        close();
        line("return;");
        close();
        stateSwitch(false);
    }

    std::string entry() const
    {
        const size_t index = _graph.entry >= 0 && _graph.entry < static_cast<int>(_graph.states.size())
            ? static_cast<size_t>(_graph.entry)
            : 0;
        return quoted(_graph.states[index].label);
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
        line("// Goes to a state: to its label, playing or held.");
        open("function phxGo(state:String):void {");
        goBody();
        close();
        line("// Goes now, or once this state's animation has finished.");
        open("function phxTo(state:String, after:Boolean):void {");
        toBody();
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
        checkBody();
        close();
        line("// Starts in the first state, the first time this frame plays.");
        open("if (!phxReady) {");
        line("phxReady = true;");
        line("phxInputs = {};");
        for (const Input& each : _graph.inputs)
            line(input(each.name) + " = " + startValue(each) + ";");
        line("addEventListener(\"enterFrame\", phxFrame);");
        line("phxGo(" + entry() + ");");
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
        goBody();
        close("};");
        line("// Goes now, or once this state's animation has finished.");
        open("phxT.phxTo = function (state, after) {");
        toBody();
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
        checkBody();
        close("};");
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
        line("phxT.phxGo(" + entry() + ");");
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
};

} // namespace

int Graph::stateNamed(const std::string& label) const
{
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].label == label)
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

void Graph::removeState(int index)
{
    if (index < 0 || index >= static_cast<int>(states.size()))
        return;
    states.erase(states.begin() + index);
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

void Graph::removeInput(const std::string& name)
{
    inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [&](const Input& each) { return each.name == name; }),
        inputs.end());
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
            read.states.push_back(state);
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
