#pragma once

#include "behaviors.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Timeline;

/// Node graphs: a timeline's layers composited through a graph of nodes
/// rather than stacked in order. Layer nodes take layers out of the stack;
/// what is left comes in through the Other Layers node; effect, merge,
/// matte and transform nodes work on the pictures; the Output node's
/// picture is what the timeline shows, on the stage and in everything
/// exported. A graph is Phoenix's own: kept in the timeline's
/// phoenixNodeGraph attribute, which other apps that open the FLA ignore,
/// showing the layers stacked as usual. Free of Qt.
namespace nodegraph {

extern const char* const kAttribute;

/// A node: its id, its kind and settings (Applied's type and params, keyed
/// as behaviors are), where it sits in the graph (its top left corner), a
/// name of its own when it has one, and how it shows.
struct Node
{
    int id = 0;
    /// The kind in type, the settings in params; enabled false is a node
    /// bypassed, passing its first input through unchanged.
    behaviors::Applied applied;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    /// Its picture hidden in the graph, to keep it small.
    bool collapsed = false;
    /// For a node whose inputs grow as they are used (Composite): how many
    /// it has now.
    int inputCount = 0;

    const std::string& type() const { return applied.type; }
    bool bypassed() const { return !applied.enabled; }
};

/// A connection: node \a from's picture into input \a input (counted from
/// 0) of node \a to.
struct Link
{
    int from = 0;
    int to = 0;
    int input = 0;
    bool operator==(const Link& other) const { return from == other.from && to == other.to && input == other.input; }
};

/// A frame drawn behind nodes to group them, with a title and a color
/// (0xRRGGBB); moving it moves the nodes inside.
struct Backdrop
{
    int id = 0;
    double x = 0.0, y = 0.0, width = 320.0, height = 200.0;
    std::string title;
    uint32_t color = 0x5B8CFF;
};

struct Graph
{
    /// Whether the timeline is composited through the graph; off, its
    /// layers stack as usual and the graph is kept for later.
    bool enabled = true;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Backdrop> backdrops;

    Node* node(int id);
    const Node* node(int id) const;
    Backdrop* backdrop(int id);
    /// The Output node's id, or 0.
    int output() const;
    /// The link into input \a input of node \a to, or null.
    const Link* linkInto(int to, int input) const;
    /// A new id for a node or backdrop.
    int newId() const;
    /// Whether linking \a from into \a to would make a loop.
    bool wouldLoop(int from, int to) const;
    /// Takes node \a id away with its links.
    void remove(int id);
    /// Links \a from into input \a input of \a to, in place of what was
    /// linked there; false when that would make a loop or either is
    /// missing. A growing node gets another input when its last is used.
    bool connect(int from, int to, int input);
    /// The ids of the nodes whose pictures reach the output.
    std::vector<int> reaching() const;

    bool operator==(const Graph& other) const;
};

/// The graph as the attribute keeps it (XML), and back; false when \a text
/// is not a graph.
std::string encode(const Graph& graph);
bool decode(const std::string& text, Graph& graph);

/// The graph \a timeline keeps, if any, decoded once for each change of it.
/// Null when it has none.
std::shared_ptr<const Graph> of(const Timeline& timeline);
/// Whether \a timeline is composited through a graph now.
bool active(const Timeline& timeline);
/// \a timeline's attributes with \a graph in them, or without one when
/// \a graph is null.
std::vector<std::pair<std::string, std::string>> withGraph(const Timeline& timeline, const Graph* graph);

/// A new graph for a timeline: its layers through Other Layers into the
/// Output, which shows them as they were.
Graph starter();

/// Layer nodes naming \a from name \a to instead, keys too; false when
/// none did.
bool renameLayer(Graph& graph, const std::string& from, const std::string& to);
/// \a timeline's attributes with its graph's Layer nodes following a layer
/// renamed from \a from to \a to; its attributes as they are when its graph
/// names no such layer.
std::vector<std::pair<std::string, std::string>> withLayerRenamed(const Timeline& timeline, const std::string& from,
    const std::string& to);

/// A kind of node.
struct Type
{
    std::string id;
    std::string name;
    /// "Input", "Combine", "Transform", "Time", "Blur", "Color", "Distort",
    /// "Generate", "Transition", "Utility" or "Output".
    std::string category;
    std::string description;
    /// Its inputs' names, top to bottom.
    std::vector<std::string> inputs;
    /// Inputs that grow as they are used, named after the first ("Layer 1",
    /// "Layer 2"...).
    bool growing = false;
    /// Whether it has a picture to give (all but the Output).
    bool hasOutput = true;
    /// Its settings. Beyond the behavior types: "color" (#RRGGBB),
    /// "layer" (a layer's name) and "blend" (a blend mode's name).
    std::vector<behaviors::ParamSpec> params;
};

const std::vector<Type>& types();
const Type* type(const std::string& id);
/// The categories in the order menus show them.
const std::vector<std::string>& categories();
/// A new node of type \a id at (\a x, \a y), with its type's settings.
Node make(const std::string& id, int nodeId, double x, double y, int seed = 1);
/// The names of \a node's inputs now (a growing node's grow).
std::vector<std::string> inputNames(const Node& node);
/// What \a node is called in the graph: its own name, a Layer node's
/// layer, or its type's name.
std::string title(const Node& node);

/// A picture: premultiplied ARGB, a row after another.
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    Image() = default;
    Image(int width, int height);
    bool empty() const { return pixels.empty(); }
};

/// What an evaluation draws from and at what size.
struct Context
{
    int width = 0;
    int height = 0;
    /// The timeline's frame, from 0, for keyed settings and effects that
    /// move.
    int frame = 0;
    double frameRate = 24.0;
    /// The stage, in stage pixels, for transforms about its middle.
    double stageWidth = 550.0;
    double stageHeight = 400.0;
    /// Stage pixels to the picture's pixels: x' = a x + c y + tx,
    /// y' = b x + d y + ty.
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;
    /// A layer (or a folder's layers) by name, drawn the size of the
    /// picture on \a frame (the context's own, or another for a time node);
    /// false when there is none of that name.
    std::function<bool(const std::string& layer, int frame, Image& into)> layer;
    /// The timeline's layers but \a taken, stacked as usual, on \a frame.
    std::function<bool(const std::vector<std::string>& taken, int frame, Image& into)> rest;

    double pixelsPerUnit() const;
};

/// One frame's pictures of a graph's nodes, each worked out when first
/// asked for and kept.
class Evaluation
{
public:
    Evaluation(const Graph& graph, const Context& context);
    ~Evaluation();
    Evaluation(const Evaluation&) = delete;
    Evaluation& operator=(const Evaluation&) = delete;

    /// Node \a id's picture; an empty one for a node with nothing to give.
    const Image& at(int id);
    /// What the Output node shows.
    const Image& output();
    /// The layers Layer nodes reaching the output take out of the stack.
    const std::vector<std::string>& taken() const { return _taken; }
    /// Why node \a id shows nothing, when something is wrong with it (a
    /// layer that is not there); empty when nothing is.
    std::string problem(int id) const;

private:
    const Image& compute(int id);

    const Graph& _graph;
    const Context& _context;
    std::map<int, Image> _done;
    std::map<int, std::string> _problems;
    std::vector<int> _working;
    std::vector<std::string> _taken;
    Image _none;
};

/// Blend mode names, as Merge offers them.
const std::vector<std::string>& blendModes();
/// \a over blended onto \a under (both premultiplied, the same size) by
/// \a mode at \a opacity (0 to 1), into \a under.
void blend(const Image& over, Image& under, const std::string& mode, double opacity);

} // namespace nodegraph

} // namespace fla
