#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fla {

class CommandStack;
class Frame;
class Layer;
class Timeline;

/// Interactive states made without code: a movie clip's (or a scene's)
/// frame labels as the states it can be in, named inputs (switches,
/// numbers, triggers), and transitions from state to state that a click
/// on an instance, a key, the end of a state's animation, a timer or an
/// input's value sets off. Layers of states play alongside the timeline's
/// own, each moving a named movie clip on it through that clip's labels,
/// all going by the same inputs. The graph is turned into ordinary ActionScript
/// in the timeline's first frame (ActionScript 3 for an ActionScript 3
/// document, else ActionScript 1 and 2), between comment markers, so Test
/// Movie, SWF and HTML5 publishing and any other app play it with nothing
/// of Phoenix's. The graph itself is kept in the timeline's phoenixStates
/// attribute, which other apps ignore, so it can be edited again. Free of
/// Qt.
namespace states {

extern const char* const kAttribute;
/// The layer the scripts and the labels new states make go on.
extern const char* const kLayerName;

/// How a state plays its frames: from its label to the frame before the
/// next label (or to the end of its last label's range, \a until).
enum class Play
{
    Loop,
    Once,
    Hold,
    /// Shows the frame a number input picks: its first frame at the low
    /// value, its last at the high one, those between in proportion (a
    /// gauge, a dial, a face from sad to happy).
    Blend,
};

struct State
{
    /// The frame label it starts at, which is its name too.
    std::string label;
    /// A later label whose frames it plays through as well; empty for its
    /// own label's frames only.
    std::string until;
    Play play = Play::Loop;
    /// Where it sits in the graph.
    double x = 0.0;
    double y = 0.0;
    /// The layer it is in: 0 for the timeline's own, else 1 and up for
    /// Graph::layers.
    int layer = 0;
    /// For Play::Blend: the number input that picks the frame, and the
    /// values at its first and last frames.
    std::string blend;
    double low = 0.0;
    double high = 1.0;
};

/// An input shown on the stage: a named text field's text, or a named
/// movie clip's place, turn, fade, size, frame or visibility, kept up as
/// the input changes. A number maps from \a low .. \a high onto \a from ..
/// \a to (held at the ends); text shows the value, visible goes by
/// whether it is on (or not 0).
struct Binding
{
    std::string input;
    /// The instance name of the clip or text field.
    std::string target;
    /// text, visible, frame, x, y, rotation, alpha (percent) or scale
    /// (percent).
    std::string property;
    double low = 0.0;
    double high = 1.0;
    double from = 0.0;
    double to = 1.0;
};

/// A layer of states playing alongside the timeline's own: the movie clip
/// named \a target on the timeline moves through its own labels, by
/// transitions of its own, at the same time.
struct StateLayer
{
    std::string name;
    std::string target;
    /// The state it starts in (an index into Graph::states); -1 for its
    /// first.
    int entry = -1;
};

enum class InputKind
{
    Switch,
    Number,
    Trigger,
};

struct Input
{
    std::string name;
    InputKind kind = InputKind::Switch;
    /// Its starting value: 0 or 1 for a switch.
    double value = 0.0;
};

/// What sets a transition off, or a listener.
enum class When
{
    Click,
    Press,
    Release,
    HoverIn,
    HoverOut,
    Key,
    /// The state's animation got to its end (a loop each time round).
    Finished,
    /// A number of seconds in the state.
    Timer,
    /// Only the condition.
    Condition,
};

enum class Compare
{
    None,
    IsOn,
    IsOff,
    Fired,
    Equal,
    NotEqual,
    Less,
    More,
    AtLeast,
    AtMost,
};

/// An input's value tested: "hungry is on", "score > 3".
struct Condition
{
    std::string input;
    Compare compare = Compare::None;
    double value = 0.0;

    bool empty() const { return input.empty() || compare == Compare::None; }
};

struct Transition
{
    /// The state it leaves, -1 for any state; the state it goes to.
    int from = -1;
    int to = 0;
    When when = When::Click;
    /// The instance clicked or hovered, or the key (keyNames()).
    std::string target;
    double seconds = 1.0;
    /// Tested too when there is one (for When::Condition, all there is).
    Condition condition;
    /// Waits for the state's animation to finish before it goes.
    bool afterFinish = false;
};

enum class Change
{
    TurnOn,
    TurnOff,
    Toggle,
    Set,
    Add,
    Fire,
};

/// Sets an input when the pointer or a key does something.
struct Listener
{
    When when = When::Click;
    std::string target;
    std::string input;
    Change change = Change::TurnOn;
    double value = 0.0;
};

struct Graph
{
    std::vector<State> states;
    /// The state it starts in.
    int entry = 0;
    std::vector<Input> inputs;
    std::vector<Transition> transitions;
    std::vector<Listener> listeners;
    /// The layers beyond the timeline's own (layer 1 is layers[0]).
    std::vector<StateLayer> layers;
    std::vector<Binding> bindings;

    /// Nothing to turn into a script: no states and no bindings.
    bool empty() const { return states.empty() && bindings.empty(); }
    int stateNamed(const std::string& label) const;
    /// The state of layer \a layer with label \a label, or -1.
    int stateNamed(const std::string& label, int layer) const;
    const Input* input(const std::string& name) const;
    /// The state layer \a layer starts in, or -1 when it has none.
    int entryOf(int layer) const;
    /// Takes out state \a index and the transitions to and from it.
    void removeState(int index);
    /// Takes out layer \a layer (1 and up) and its states.
    void removeLayer(int layer);
    /// Takes out input \a name and the conditions, listeners and bindings
    /// on it.
    void removeInput(const std::string& name);
    /// Every condition, listener, blend and binding on input \a from goes
    /// by \a to instead.
    void renameInput(const std::string& from, const std::string& to);
    bool operator==(const Graph& other) const;
};

/// The graph as the attribute keeps it (XML), and back; false when \a text
/// is not a graph.
std::string encode(const Graph& graph);
bool decode(const std::string& text, Graph& graph);

/// The graph \a timeline keeps; empty when it has none.
Graph of(const Timeline& timeline);
/// \a timeline's attributes with \a graph in them, or without when empty.
std::vector<std::pair<std::string, std::string>> withGraph(const Timeline& timeline, const Graph& graph);

/// The keys a key transition or listener can name, and their key codes
/// (0 for any key).
const std::vector<std::pair<std::string, int>>& keyNames();

/// Words for the menus and the arrows: "is clicked", "loops"...
std::string words(When when);
std::string words(Play play);
std::string words(Compare compare);
std::string words(Change change);
std::string words(InputKind kind);
/// A transition or listener in a line: "feedButton is clicked",
/// "hungry is on", "after 2 s"...
std::string describe(const Graph& graph, const Transition& transition);
std::string describe(const Listener& listener);
/// "score shows in scoreText", "happy sets face's x (0 to 10 as 0 to 200)".
std::string describe(const Binding& binding);

/// The properties a binding can set, in menu order, and their words.
const std::vector<std::string>& bindProperties();
std::string words(const std::string& property);
/// Whether \a property takes a number mapped between two values.
bool mapped(const std::string& property);

/// A timeline's frame labels (comments left out) and the frames they
/// start at (from 0), in frame order; each name once, at its first frame.
std::vector<std::pair<std::string, int>> labels(const Timeline& timeline);
/// How many frames \a timeline runs to.
int length(const Timeline& timeline);
/// The frames (from 0) state \a state plays: first and last; false when
/// its label is not on the timeline.
bool range(const Timeline& timeline, const State& state, int& first, int& last);
/// The names of the movie clip and button instances on \a timeline, sorted.
std::vector<std::string> instanceNames(const Timeline& timeline);
/// Those and the named text fields: what a binding can show an input in.
std::vector<std::string> bindableNames(const Timeline& timeline);
/// The timeline of the movie clip named \a instance on \a timeline, or
/// null.
const Timeline* clipTimeline(const Timeline& timeline, const std::string& instance);
/// The timeline the states of \a layer go by: \a timeline itself for
/// layer 0, its layer's movie clip's for the rest; null when not there.
const Timeline* layerTimeline(const Timeline& timeline, const Graph& graph, int layer);

/// The script \a graph is turned into for \a timeline, in ActionScript
/// \a version (3, or 1 and 2), markers and all; empty for an empty graph.
/// \a frameRate turns a timer's seconds into frames.
std::string compile(const Graph& graph, const Timeline& timeline, int version, double frameRate);

/// The lines that start and end the script a graph is turned into.
extern const char* const kBegin;
extern const char* const kEnd;
/// \a script with \a block in place of the block it has (between the
/// markers), or after the rest when it has none; with the block taken out
/// when \a block is empty. What else the script has stays as it is.
std::string withBlock(const std::string& script, const std::string& block);
/// The block \a script has, markers and all; empty when none.
std::string blockOf(const std::string& script);

/// The layer of \a timeline the scripts go on, or null.
Layer* layerOf(const Timeline& timeline);
/// Whether the script on \a timeline is not what its graph turns into now
/// (labels moved, the document's ActionScript version changed...).
bool stale(const Timeline& timeline, int version, double frameRate);

/// What an edit of a graph does besides keeping it: labels to make
/// (a name and the frame, from 0) and labels to rename.
struct Edit
{
    std::vector<std::pair<std::string, int>> newLabels;
    std::vector<std::pair<std::string, std::string>> renames;
};

/// Keeps \a graph in \a timeline, makes and renames the labels \a edit
/// asks for, and puts the script it is turned into on the first frame of
/// the States layer (made at the top when there is none): one undo step,
/// named \a name, on \a stack.
void apply(CommandStack& stack, Timeline& timeline, const Graph& graph, const Edit& edit, int version,
    double frameRate, const std::string& name);

} // namespace states

} // namespace fla
