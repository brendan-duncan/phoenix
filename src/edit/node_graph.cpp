#include "node_graph.h"

#include "adjustments.h"

#include "../data/timeline.h"
#include "../util/parallel.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>

namespace fla::nodegraph {

const char* const kAttribute = "phoenixNodeGraph";

namespace {

using behaviors::ParamSpec;

constexpr double kPi = 3.14159265358979323846;

std::string numberText(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.10g", value);
    return text;
}

ParamSpec number(const char* name, const char* label, double value, double min, double max, double step)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.value = value;
    spec.min = min;
    spec.max = max;
    spec.step = step;
    spec.text = numberText(value);
    return spec;
}

ParamSpec typed(const char* type, const char* name, const char* label, const std::string& value)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = type;
    spec.text = value;
    return spec;
}

ParamSpec choice(const char* name, const char* label, std::vector<std::string> options, const std::string& value = {})
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "choice";
    spec.choices = std::move(options);
    spec.text = !value.empty() ? value : spec.choices.empty() ? std::string() : spec.choices.front();
    return spec;
}

/// The category an adjustment effect goes under.
const char* effectCategory(const std::string& id)
{
    static const std::map<std::string, const char*> categories = {
        {"blur", "Blur"}, {"dirblur", "Blur"}, {"radialblur", "Blur"}, {"lensblur", "Blur"}, {"glow", "Blur"},
        {"color", "Color"}, {"tint", "Color"}, {"levels", "Color"}, {"curves", "Color"}, {"huesat", "Color"},
        {"tritone", "Color"}, {"vignette", "Color"}, {"grain", "Color"}, {"chromatic", "Color"},
        {"turbulent", "Distort"}, {"wave", "Distort"}, {"bulge", "Distort"}, {"ripple", "Distort"},
        {"bend", "Distort"}, {"displace", "Distort"}, {"ramp", "Generate"}, {"fourcolor", "Generate"},
        {"fill", "Generate"}, {"outline", "Generate"}, {"fractalnoise", "Generate"}, {"linearwipe", "Transition"},
        {"radialwipe", "Transition"}, {"blinds", "Transition"}, {"dissolve", "Transition"}};
    const auto found = categories.find(id);
    return found == categories.end() ? "Color" : found->second;
}

/// Where Displacement Map's map comes in, by the name the effect asks for.
const char* const kMapName = "\x01map";

std::vector<Type> makeTypes()
{
    std::vector<Type> list;
    const auto add = [&list](const char* id, const char* name, const char* category, const char* description,
                         std::vector<std::string> inputs, std::vector<ParamSpec> params) -> Type& {
        Type type;
        type.id = id;
        type.name = name;
        type.category = category;
        type.description = description;
        type.inputs = std::move(inputs);
        type.params = std::move(params);
        list.push_back(std::move(type));
        return list.back();
    };

    add("layers", "Other Layers", "Input",
        "The timeline's layers stacked as usual, all but those Layer nodes take out of the stack.", {}, {});
    add("layer", "Layer", "Input",
        "One layer of the timeline, or a folder's layers, taken out of the stack to work on alone.", {},
        {typed("layer", "layer", "Layer", "")});
    add("solid", "Solid Color", "Input", "A color over the whole picture.", {},
        {typed("color", "color", "Color", "#3060C0"), number("opacity", "Opacity (percent)", 100, 0, 100, 1)});

    add("merge", "Merge", "Combine", "One picture over another, blended by a mode, at an opacity.", {"Over", "Under"},
        {choice("mode", "Blend", blendModes(), "Normal"), number("opacity", "Opacity (percent)", 100, 0, 100, 1)});
    Type& composite = add("composite", "Composite", "Combine",
        "Pictures stacked: the first input on top, each one under the one before.", {"Layer"}, {});
    composite.growing = true;
    add("mix", "Mix", "Combine", "A blend from one picture to another: at 0 the first, at 100 the second.",
        {"A", "B"}, {number("amount", "Amount (percent)", 50, 0, 100, 1)});
    add("matte", "Matte", "Combine",
        "A picture shown through another: where the matte is solid (or light, by brightness) the picture shows.",
        {"Source", "Matte"}, {choice("kind", "Matte by", {"Alpha", "Alpha Inverted", "Luma", "Luma Inverted"})});

    add("transform", "Transform", "Transform", "Moves, scales and turns the picture about the middle of the stage.",
        {"In"},
        {number("x", "Move across (pixels)", 0, -10000, 10000, 1), number("y", "Move down (pixels)", 0, -10000, 10000, 1),
            number("scale", "Scale (percent)", 100, 0, 1000, 1), number("rotation", "Rotation (degrees)", 0, -3600, 3600, 1)});
    add("opacity", "Opacity", "Transform", "Fades the picture.", {"In"},
        {number("amount", "Opacity (percent)", 100, 0, 100, 1)});

    for (const behaviors::Definition& effect : compositing::definitions())
    {
        std::vector<ParamSpec> params;
        for (ParamSpec spec : effect.params)
        {
            // The map comes in as an input instead.
            if (effect.id == "displace" && spec.name == "layer")
                continue;
            const size_t hex = spec.label.find(" (#RRGGBB)");
            if (spec.type == "text" && hex != std::string::npos)
            {
                spec.type = "color";
                spec.label.erase(hex);
            }
            params.push_back(spec);
        }
        std::vector<std::string> inputs{"In"};
        if (effect.id == "displace")
            inputs.push_back("Map");
        // Said of the picture coming in, not of what is below a layer.
        std::string description = effect.description;
        for (const auto& [from, to] : {std::pair<const char*, const char*>{"everything below the layer", "the picture"},
                 {"everything drawn below", "what the picture holds"}, {" With art on the layer, it fills just the art.", ""}})
        {
            const size_t at = description.find(from);
            if (at != std::string::npos)
                description.replace(at, std::strlen(from), to);
        }
        if (effect.id == "displace")
            description = "Pushes the picture about by the picture linked into Map: lighter parts push one way, darker "
                          "the other. Fractal Noise or a gradient makes a good map.";
        add(effect.id.c_str(), effect.name.c_str(), effectCategory(effect.id), description.c_str(), inputs, params);
    }

    add("dot", "Reroute", "Utility", "A point a link passes through, to tidy the graph.", {"In"}, {});
    Type& output = add("output", "Output", "Output", "What the timeline shows: on the stage and in everything exported.",
        {"In"}, {});
    output.hasOutput = false;
    return list;
}

/// The picture's 2x3 matrix and back.
struct Affine
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;

    Affine operator*(const Affine& o) const
    {
        // this after o.
        Affine r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.tx = a * o.tx + c * o.ty + tx;
        r.ty = b * o.tx + d * o.ty + ty;
        return r;
    }
    bool invert(Affine& out) const
    {
        const double det = a * d - b * c;
        if (std::fabs(det) < 1e-12)
            return false;
        out.a = d / det;
        out.b = -b / det;
        out.c = -c / det;
        out.d = a / det;
        out.tx = -(out.a * tx + out.c * ty);
        out.ty = -(out.b * tx + out.d * ty);
        return true;
    }
};

uint32_t parseColor(const std::string& text, uint32_t fallback)
{
    std::string hex = text;
    if (!hex.empty() && hex[0] == '#')
        hex.erase(0, 1);
    if (hex.size() != 6)
        return fallback;
    char* end = nullptr;
    const unsigned long value = std::strtoul(hex.c_str(), &end, 16);
    return end && *end == '\0' ? static_cast<uint32_t>(value) : fallback;
}

double numberAt(const behaviors::Applied& applied, const char* name, int frame, double fallback)
{
    const std::string text = applied.valueAt(name, frame, std::string());
    if (text.empty())
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && std::isfinite(value) ? value : fallback;
}

/// A premultiplied pixel read between pixels, transparent outside.
inline void sample(const Image& image, double x, double y, double out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0.0;
    const double fx = x - 0.5, fy = y - 0.5;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const double tx = fx - x0, ty = fy - y0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
        {
            const int px = x0 + i, py = y0 + j;
            if (px < 0 || py < 0 || px >= image.width || py >= image.height)
                continue;
            const double w = (i ? tx : 1.0 - tx) * (j ? ty : 1.0 - ty);
            const uint32_t p = image.pixels[static_cast<size_t>(py) * image.width + px];
            out[0] += w * ((p >> 24) & 0xFF);
            out[1] += w * ((p >> 16) & 0xFF);
            out[2] += w * ((p >> 8) & 0xFF);
            out[3] += w * (p & 0xFF);
        }
}

inline uint32_t pack(double a, double r, double g, double b)
{
    const auto clamp = [](double v) { return static_cast<uint32_t>(std::min(255.0, std::max(0.0, v)) + 0.5); };
    const uint32_t alpha = clamp(a);
    const auto channel = [&](double v) { return std::min(alpha, clamp(v)); };
    return (alpha << 24) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

/// A separable blend of a source and backdrop color, both 0 to 1 and not
/// premultiplied.
double blendChannel(int mode, double cb, double cs)
{
    switch (mode)
    {
    case 1: // Multiply
        return cb * cs;
    case 2: // Screen
        return cb + cs - cb * cs;
    case 3: // Overlay
        return cb <= 0.5 ? 2.0 * cb * cs : 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs);
    case 4: // Darken
        return std::min(cb, cs);
    case 5: // Lighten
        return std::max(cb, cs);
    case 6: // Add
        return std::min(1.0, cb + cs);
    case 7: // Subtract
        return std::max(0.0, cb - cs);
    case 8: // Difference
        return std::fabs(cb - cs);
    case 9: // Hard Light
        return cs <= 0.5 ? 2.0 * cb * cs : 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs);
    case 10: // Soft Light
    {
        if (cs <= 0.5)
            return cb - (1.0 - 2.0 * cs) * cb * (1.0 - cb);
        const double dd = cb <= 0.25 ? ((16.0 * cb - 12.0) * cb + 4.0) * cb : std::sqrt(cb);
        return cb + (2.0 * cs - 1.0) * (dd - cb);
    }
    case 11: // Color Dodge
        return cb <= 0.0 ? 0.0 : cs >= 1.0 ? 1.0 : std::min(1.0, cb / (1.0 - cs));
    case 12: // Color Burn
        return cb >= 1.0 ? 1.0 : cs <= 0.0 ? 0.0 : 1.0 - std::min(1.0, (1.0 - cb) / cs);
    default: // Normal
        return cs;
    }
}

std::mutex g_cacheMutex;
std::unordered_map<const Timeline*, std::pair<std::string, std::shared_ptr<const Graph>>> g_cache;

const std::string* attributeOf(const Timeline& timeline)
{
    for (const auto& [name, value] : timeline.extraAttributes)
        if (name == kAttribute)
            return &value;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// The graph

Node* Graph::node(int id)
{
    for (Node& each : nodes)
        if (each.id == id)
            return &each;
    return nullptr;
}

const Node* Graph::node(int id) const
{
    for (const Node& each : nodes)
        if (each.id == id)
            return &each;
    return nullptr;
}

Backdrop* Graph::backdrop(int id)
{
    for (Backdrop& each : backdrops)
        if (each.id == id)
            return &each;
    return nullptr;
}

int Graph::output() const
{
    for (const Node& each : nodes)
        if (each.type() == "output")
            return each.id;
    return 0;
}

const Link* Graph::linkInto(int to, int input) const
{
    for (const Link& link : links)
        if (link.to == to && link.input == input)
            return &link;
    return nullptr;
}

int Graph::newId() const
{
    int highest = 0;
    for (const Node& each : nodes)
        highest = std::max(highest, each.id);
    for (const Backdrop& each : backdrops)
        highest = std::max(highest, each.id);
    return highest + 1;
}

bool Graph::wouldLoop(int from, int to) const
{
    // A loop when \a to already feeds \a from.
    if (from == to)
        return true;
    std::vector<int> stack{from};
    std::set<int> seen;
    while (!stack.empty())
    {
        const int at = stack.back();
        stack.pop_back();
        if (at == to)
            return true;
        if (!seen.insert(at).second)
            continue;
        for (const Link& link : links)
            if (link.to == at)
                stack.push_back(link.from);
    }
    return false;
}

void Graph::remove(int id)
{
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [id](const Node& n) { return n.id == id; }), nodes.end());
    links.erase(std::remove_if(links.begin(), links.end(), [id](const Link& l) { return l.from == id || l.to == id; }),
        links.end());
    backdrops.erase(std::remove_if(backdrops.begin(), backdrops.end(), [id](const Backdrop& b) { return b.id == id; }),
        backdrops.end());
}

bool Graph::connect(int from, int to, int input)
{
    const Node* source = node(from);
    Node* target = node(to);
    if (!source || !target || input < 0 || wouldLoop(from, to))
        return false;
    const Type* sourceType = type(source->type());
    if (sourceType && !sourceType->hasOutput)
        return false;
    if (input >= static_cast<int>(inputNames(*target).size()))
        return false;
    links.erase(std::remove_if(links.begin(), links.end(),
                    [to, input](const Link& l) { return l.to == to && l.input == input; }),
        links.end());
    links.push_back({from, to, input});
    // A growing node keeps one free input at its end.
    const Type* targetType = type(target->type());
    if (targetType && targetType->growing)
    {
        int used = 0;
        for (const Link& link : links)
            if (link.to == to)
                used = std::max(used, link.input + 1);
        target->inputCount = std::max(target->inputCount, used + 1);
    }
    return true;
}

std::vector<int> Graph::reaching() const
{
    std::vector<int> found;
    const int out = output();
    if (!out)
        return found;
    std::vector<int> stack{out};
    std::set<int> seen;
    while (!stack.empty())
    {
        const int at = stack.back();
        stack.pop_back();
        if (!seen.insert(at).second)
            continue;
        found.push_back(at);
        for (const Link& link : links)
            if (link.to == at)
                stack.push_back(link.from);
    }
    return found;
}

bool Graph::operator==(const Graph& other) const
{
    return encode(*this) == encode(other);
}

// ---------------------------------------------------------------------------
// Keeping it

std::string encode(const Graph& graph)
{
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLElement* root = doc.NewElement("graph");
    doc.InsertEndChild(root);
    root->SetAttribute("version", 1);
    if (!graph.enabled)
        root->SetAttribute("enabled", "false");
    for (const Node& node : graph.nodes)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("node");
        e->SetAttribute("id", node.id);
        e->SetAttribute("type", node.type().c_str());
        e->SetAttribute("x", numberText(node.x).c_str());
        e->SetAttribute("y", numberText(node.y).c_str());
        if (!node.label.empty())
            e->SetAttribute("label", node.label.c_str());
        if (node.bypassed())
            e->SetAttribute("bypass", "true");
        if (node.collapsed)
            e->SetAttribute("collapsed", "true");
        if (node.inputCount > 0)
            e->SetAttribute("inputs", node.inputCount);
        for (const auto& [name, value] : node.applied.params)
        {
            tinyxml2::XMLElement* p = e->InsertNewChildElement("set");
            p->SetAttribute("name", name.c_str());
            p->SetAttribute("value", value.c_str());
        }
    }
    for (const Link& link : graph.links)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("link");
        e->SetAttribute("from", link.from);
        e->SetAttribute("to", link.to);
        e->SetAttribute("input", link.input);
    }
    for (const Backdrop& backdrop : graph.backdrops)
    {
        tinyxml2::XMLElement* e = root->InsertNewChildElement("backdrop");
        e->SetAttribute("id", backdrop.id);
        e->SetAttribute("x", numberText(backdrop.x).c_str());
        e->SetAttribute("y", numberText(backdrop.y).c_str());
        e->SetAttribute("width", numberText(backdrop.width).c_str());
        e->SetAttribute("height", numberText(backdrop.height).c_str());
        e->SetAttribute("title", backdrop.title.c_str());
        char color[8];
        std::snprintf(color, sizeof(color), "%06X", backdrop.color & 0xFFFFFF);
        e->SetAttribute("color", color);
    }
    tinyxml2::XMLPrinter printer(nullptr, true);
    doc.Print(&printer);
    return printer.CStr();
}

bool decode(const std::string& text, Graph& graph)
{
    graph = Graph();
    tinyxml2::XMLDocument doc;
    if (doc.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS)
        return false;
    const tinyxml2::XMLElement* root = doc.RootElement();
    if (!root || std::string(root->Name()) != "graph")
        return false;
    graph.enabled = !root->Attribute("enabled", "false");
    for (const tinyxml2::XMLElement* e = root->FirstChildElement("node"); e; e = e->NextSiblingElement("node"))
    {
        Node node;
        node.id = e->IntAttribute("id");
        node.applied.type = e->Attribute("type") ? e->Attribute("type") : "";
        node.x = e->DoubleAttribute("x");
        node.y = e->DoubleAttribute("y");
        node.label = e->Attribute("label") ? e->Attribute("label") : "";
        node.applied.enabled = !e->BoolAttribute("bypass", false);
        node.collapsed = e->BoolAttribute("collapsed", false);
        node.inputCount = std::max(0, std::min(256, e->IntAttribute("inputs")));
        for (const tinyxml2::XMLElement* p = e->FirstChildElement("set"); p; p = p->NextSiblingElement("set"))
            if (p->Attribute("name"))
                node.applied.params[p->Attribute("name")] = p->Attribute("value") ? p->Attribute("value") : "";
        if (node.id > 0 && !node.applied.type.empty() && !graph.node(node.id))
            graph.nodes.push_back(std::move(node));
    }
    for (const tinyxml2::XMLElement* e = root->FirstChildElement("link"); e; e = e->NextSiblingElement("link"))
    {
        const Link link{e->IntAttribute("from"), e->IntAttribute("to"), e->IntAttribute("input")};
        // Links to nodes that are not there, a second link into an input,
        // and loops are left out.
        if (!graph.node(link.from) || !graph.node(link.to) || link.input < 0 || link.input > 256 ||
            graph.linkInto(link.to, link.input) || graph.wouldLoop(link.from, link.to))
            continue;
        graph.links.push_back(link);
    }
    for (const tinyxml2::XMLElement* e = root->FirstChildElement("backdrop"); e; e = e->NextSiblingElement("backdrop"))
    {
        Backdrop backdrop;
        backdrop.id = e->IntAttribute("id");
        backdrop.x = e->DoubleAttribute("x");
        backdrop.y = e->DoubleAttribute("y");
        backdrop.width = std::max(40.0, e->DoubleAttribute("width", 320.0));
        backdrop.height = std::max(40.0, e->DoubleAttribute("height", 200.0));
        backdrop.title = e->Attribute("title") ? e->Attribute("title") : "";
        backdrop.color = parseColor(e->Attribute("color") ? e->Attribute("color") : "", 0x5B8CFF);
        graph.backdrops.push_back(backdrop);
    }
    return true;
}

std::shared_ptr<const Graph> of(const Timeline& timeline)
{
    const std::string* text = attributeOf(timeline);
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (!text)
    {
        g_cache.erase(&timeline);
        return nullptr;
    }
    auto found = g_cache.find(&timeline);
    if (found != g_cache.end() && found->second.first == *text)
        return found->second.second;
    auto graph = std::make_shared<Graph>();
    if (!decode(*text, *graph))
        graph.reset();
    g_cache[&timeline] = {*text, graph};
    return graph;
}

bool active(const Timeline& timeline)
{
    if (!attributeOf(timeline))
        return false;
    const std::shared_ptr<const Graph> graph = of(timeline);
    return graph && graph->enabled && graph->output();
}

std::vector<std::pair<std::string, std::string>> withGraph(const Timeline& timeline, const Graph* graph)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& pair : timeline.extraAttributes)
        if (pair.first != kAttribute)
            attributes.push_back(pair);
    if (graph)
        attributes.emplace_back(kAttribute, encode(*graph));
    return attributes;
}

Graph starter()
{
    Graph graph;
    graph.nodes.push_back(make("layers", 1, 0.0, 0.0));
    graph.nodes.push_back(make("output", 2, 320.0, 0.0));
    graph.connect(1, 2, 0);
    return graph;
}

// ---------------------------------------------------------------------------
// Kinds of node

const std::vector<Type>& types()
{
    static const std::vector<Type> list = makeTypes();
    return list;
}

const Type* type(const std::string& id)
{
    for (const Type& each : types())
        if (each.id == id)
            return &each;
    return nullptr;
}

const std::vector<std::string>& categories()
{
    static const std::vector<std::string> list = {"Input", "Combine", "Transform", "Blur", "Color", "Distort",
        "Generate", "Transition", "Utility", "Output"};
    return list;
}

Node make(const std::string& id, int nodeId, double x, double y, int seed)
{
    Node node;
    node.id = nodeId;
    node.x = x;
    node.y = y;
    node.applied.type = id;
    if (const Type* t = type(id))
    {
        for (const ParamSpec& spec : t->params)
            node.applied.params[spec.name] = spec.type == "seed" ? std::to_string(seed) : spec.text;
        if (t->growing)
            node.inputCount = 2;
    }
    return node;
}

std::vector<std::string> inputNames(const Node& node)
{
    const Type* t = type(node.type());
    if (!t)
        return {};
    if (!t->growing)
        return t->inputs;
    std::vector<std::string> names;
    const std::string base = t->inputs.empty() ? std::string("Input") : t->inputs.front();
    const int count = std::max(1, node.inputCount);
    for (int i = 0; i < count; ++i)
        names.push_back(base + " " + std::to_string(i + 1));
    return names;
}

std::string title(const Node& node)
{
    if (!node.label.empty())
        return node.label;
    if (node.type() == "layer")
    {
        const auto found = node.applied.params.find("layer");
        if (found != node.applied.params.end() && !found->second.empty())
            return found->second;
    }
    const Type* t = type(node.type());
    return t ? t->name : node.type();
}

// ---------------------------------------------------------------------------
// Pictures

Image::Image(int w, int h)
    : width(std::max(0, w))
    , height(std::max(0, h))
    , pixels(static_cast<size_t>(std::max(0, w)) * static_cast<size_t>(std::max(0, h)), 0u)
{}

double Context::pixelsPerUnit() const
{
    return std::sqrt(std::fabs(a * d - b * c));
}

const std::vector<std::string>& blendModes()
{
    static const std::vector<std::string> modes = {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "Add", "Subtract", "Difference", "Hard Light", "Soft Light", "Color Dodge", "Color Burn"};
    return modes;
}

void blend(const Image& over, Image& under, const std::string& mode, double opacity)
{
    if (over.width != under.width || over.height != under.height || over.empty())
        return;
    const std::vector<std::string>& modes = blendModes();
    const int m = static_cast<int>(std::find(modes.begin(), modes.end(), mode) - modes.begin());
    const double o = std::min(1.0, std::max(0.0, opacity));
    if (o <= 0.0)
        return;
    parallelRows(under.height, under.width, [&](int y) {
        const size_t row = static_cast<size_t>(y) * under.width;
        for (int x = 0; x < under.width; ++x)
        {
            const uint32_t s = over.pixels[row + x];
            const double as = ((s >> 24) & 0xFF) / 255.0 * o;
            if (as <= 0.0)
                continue;
            uint32_t& bp = under.pixels[row + x];
            const double ab = ((bp >> 24) & 0xFF) / 255.0;
            const double sp[3] = {((s >> 16) & 0xFF) / 255.0 * o, ((s >> 8) & 0xFF) / 255.0 * o, (s & 0xFF) / 255.0 * o};
            const double bpre[3] = {((bp >> 16) & 0xFF) / 255.0, ((bp >> 8) & 0xFF) / 255.0, (bp & 0xFF) / 255.0};
            double out[3];
            for (int k = 0; k < 3; ++k)
            {
                if (m <= 0 || m >= static_cast<int>(modes.size()))
                {
                    out[k] = sp[k] + bpre[k] * (1.0 - as);
                    continue;
                }
                const double cs = sp[k] / as;
                const double cb = ab > 0.0 ? bpre[k] / ab : 0.0;
                out[k] = sp[k] * (1.0 - ab) + bpre[k] * (1.0 - as) + as * ab * blendChannel(m, cb, cs);
            }
            const double ao = as + ab * (1.0 - as);
            bp = pack(ao * 255.0, out[0] * 255.0, out[1] * 255.0, out[2] * 255.0);
        }
    });
}

// ---------------------------------------------------------------------------
// Working it out

Evaluation::Evaluation(const Graph& graph, const Context& context)
    : _graph(graph)
    , _context(context)
{
    for (int id : graph.reaching())
    {
        const Node* node = graph.node(id);
        if (!node || node->type() != "layer")
            continue;
        const std::string name = node->applied.valueAt("layer", context.frame, std::string());
        if (!name.empty() && std::find(_taken.begin(), _taken.end(), name) == _taken.end())
            _taken.push_back(name);
    }
}

Evaluation::~Evaluation() = default;

const Image& Evaluation::output()
{
    const int out = _graph.output();
    return out ? at(out) : _none;
}

const Image& Evaluation::at(int id)
{
    const auto found = _done.find(id);
    if (found != _done.end())
        return found->second;
    // A loop (which decode and connect keep out) gives nothing.
    if (std::find(_working.begin(), _working.end(), id) != _working.end())
        return _none;
    _working.push_back(id);
    const Image& made = compute(id);
    _working.pop_back();
    return made;
}

std::string Evaluation::problem(int id) const
{
    const auto found = _problems.find(id);
    return found == _problems.end() ? std::string() : found->second;
}

const Image& Evaluation::compute(int id)
{
    Image& out = _done[id];
    const Node* node = _graph.node(id);
    const int w = _context.width, h = _context.height;
    if (!node || w <= 0 || h <= 0)
        return out;
    const int frame = _context.frame;
    const behaviors::Applied& applied = node->applied;
    const std::string& kind = node->type();
    const auto input = [this, id](int index) -> const Image* {
        const Link* link = _graph.linkInto(id, index);
        if (!link)
            return nullptr;
        const Image& image = at(link->from);
        return image.empty() ? nullptr : &image;
    };
    const auto copyOf = [&](const Image* image) {
        if (image)
            out = *image;
        else
            out = Image(w, h);
    };

    // Bypassed: the first input through, as it came.
    if (node->bypassed() && kind != "output")
    {
        copyOf(input(0));
        return out;
    }

    if (kind == "layers")
    {
        out = Image(w, h);
        if (!_context.rest || !_context.rest(_taken, out))
            out = Image(w, h);
    }
    else if (kind == "layer")
    {
        out = Image(w, h);
        const std::string name = applied.valueAt("layer", frame, std::string());
        if (name.empty())
            _problems[id] = "Choose a layer.";
        else if (!_context.layer || !_context.layer(name, out))
        {
            out = Image(w, h);
            _problems[id] = "There is no layer named " + name + ".";
        }
    }
    else if (kind == "solid")
    {
        out = Image(w, h);
        const uint32_t rgb = parseColor(applied.valueAt("color", frame, "#3060C0"), 0x3060C0);
        const double alpha = std::min(1.0, std::max(0.0, numberAt(applied, "opacity", frame, 100.0) / 100.0));
        const uint32_t pixel = pack(alpha * 255.0, ((rgb >> 16) & 0xFF) * alpha, ((rgb >> 8) & 0xFF) * alpha, (rgb & 0xFF) * alpha);
        std::fill(out.pixels.begin(), out.pixels.end(), pixel);
    }
    else if (kind == "merge")
    {
        copyOf(input(1));
        if (const Image* over = input(0))
            blend(*over, out, applied.valueAt("mode", frame, "Normal"), numberAt(applied, "opacity", frame, 100.0) / 100.0);
    }
    else if (kind == "composite")
    {
        out = Image(w, h);
        const int count = static_cast<int>(inputNames(*node).size());
        for (int i = count - 1; i >= 0; --i)
            if (const Image* layer = input(i))
                blend(*layer, out, "Normal", 1.0);
    }
    else if (kind == "mix")
    {
        const Image* a = input(0);
        const Image* b = input(1);
        const double t = std::min(1.0, std::max(0.0, numberAt(applied, "amount", frame, 50.0) / 100.0));
        out = Image(w, h);
        parallelRows(h, w, [&](int y) {
            const size_t row = static_cast<size_t>(y) * w;
            for (int x = 0; x < w; ++x)
            {
                const uint32_t pa = a ? a->pixels[row + x] : 0u, pb = b ? b->pixels[row + x] : 0u;
                double ch[4];
                for (int k = 0; k < 4; ++k)
                {
                    const int shift = 24 - 8 * k;
                    ch[k] = ((pa >> shift) & 0xFF) * (1.0 - t) + ((pb >> shift) & 0xFF) * t;
                }
                out.pixels[row + x] = pack(ch[0], ch[1], ch[2], ch[3]);
            }
        });
    }
    else if (kind == "matte")
    {
        copyOf(input(0));
        const Image* matte = input(1);
        const std::string by = applied.valueAt("kind", frame, "Alpha");
        const compositing::Matte matteKind = by == "Alpha Inverted" ? compositing::Matte::AlphaInverted
                                           : by == "Luma"           ? compositing::Matte::Luma
                                           : by == "Luma Inverted"  ? compositing::Matte::LumaInverted
                                                                    : compositing::Matte::Alpha;
        // No matte: nothing shows through an alpha matte, everything
        // through an inverted one.
        const Image blank(w, h);
        const std::vector<uint8_t> strength =
            compositing::matteStrength((matte ? matte : &blank)->pixels.data(), w, h, matteKind);
        parallelPixels(out.pixels.size(), [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
            {
                const uint32_t p = out.pixels[i];
                const uint32_t s = strength[i];
                if (s == 255)
                    continue;
                const auto scale = [s](uint32_t v) { return (v * s + 127) / 255; };
                out.pixels[i] = (scale(p >> 24) << 24) | (scale((p >> 16) & 0xFF) << 16) | (scale((p >> 8) & 0xFF) << 8) |
                                scale(p & 0xFF);
            }
        });
    }
    else if (kind == "transform")
    {
        const Image* in = input(0);
        out = Image(w, h);
        if (in)
        {
            // About the stage's middle, in stage pixels, then into the
            // picture's pixels.
            const double cx = _context.stageWidth / 2.0, cy = _context.stageHeight / 2.0;
            const double s = numberAt(applied, "scale", frame, 100.0) / 100.0;
            const double r = numberAt(applied, "rotation", frame, 0.0) * kPi / 180.0;
            Affine stage;
            stage.a = s * std::cos(r);
            stage.b = s * std::sin(r);
            stage.c = -s * std::sin(r);
            stage.d = s * std::cos(r);
            stage.tx = cx + numberAt(applied, "x", frame, 0.0) - (stage.a * cx + stage.c * cy);
            stage.ty = cy + numberAt(applied, "y", frame, 0.0) - (stage.b * cx + stage.d * cy);
            const Affine toPixels{_context.a, _context.b, _context.c, _context.d, _context.tx, _context.ty};
            Affine fromPixels, inverse;
            if (toPixels.invert(fromPixels) && (toPixels * stage * fromPixels).invert(inverse))
            {
                parallelRows(h, w, [&](int y) {
                    const size_t row = static_cast<size_t>(y) * w;
                    for (int x = 0; x < w; ++x)
                    {
                        const double px = x + 0.5, py = y + 0.5;
                        const double sx = inverse.a * px + inverse.c * py + inverse.tx;
                        const double sy = inverse.b * px + inverse.d * py + inverse.ty;
                        double c4[4];
                        sample(*in, sx, sy, c4);
                        out.pixels[row + x] = pack(c4[0], c4[1], c4[2], c4[3]);
                    }
                });
            }
        }
    }
    else if (kind == "opacity")
    {
        copyOf(input(0));
        const double o = std::min(1.0, std::max(0.0, numberAt(applied, "amount", frame, 100.0) / 100.0));
        const uint32_t s = static_cast<uint32_t>(o * 255.0 + 0.5);
        if (s < 255)
            parallelPixels(out.pixels.size(), [&](size_t from, size_t to) {
                for (size_t i = from; i < to; ++i)
                {
                    const uint32_t p = out.pixels[i];
                    const auto scale = [s](uint32_t v) { return (v * s + 127) / 255; };
                    out.pixels[i] = (scale(p >> 24) << 24) | (scale((p >> 16) & 0xFF) << 16) |
                                    (scale((p >> 8) & 0xFF) << 8) | scale(p & 0xFF);
                }
            });
    }
    else if (kind == "dot" || kind == "output")
    {
        copyOf(input(0));
    }
    else if (compositing::find(kind))
    {
        copyOf(input(0));
        behaviors::Applied effect = applied;
        effect.enabled = true;
        const Image* map = nullptr;
        if (kind == "displace")
        {
            map = input(1);
            effect.params["layer"] = kMapName;
        }
        const compositing::effects::LayerPictures lookup = [map](const std::string& name) -> const std::vector<uint32_t>* {
            return map && name == kMapName ? &map->pixels : nullptr;
        };
        compositing::apply(out.pixels.data(), w, h, {effect}, frame, _context.pixelsPerUnit(), nullptr,
            _context.frameRate, &lookup);
    }
    else
    {
        out = Image(w, h);
        _problems[id] = "Phoenix does not know this kind of node (" + kind + ").";
    }
    return out;
}

} // namespace fla::nodegraph
