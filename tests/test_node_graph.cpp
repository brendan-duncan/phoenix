#include "test_util.h"

#include "../src/data/timeline.h"
#include "../src/edit/node_graph.h"

#include <string>
#include <vector>

namespace {

using namespace fla::nodegraph;

constexpr uint32_t kRed = 0xFFFF0000u;
constexpr uint32_t kBlue = 0xFF0000FFu;

/// A context of \a w by \a h pixels whose layers are flat colors, by name;
/// what the rest gives is noted in \a taken.
struct Fixture
{
    Context context;
    std::vector<std::string> taken;
    std::map<std::string, uint32_t> layers{{"Red", kRed}, {"Blue", kBlue}};
    uint32_t rest = 0xFF00FF00u;

    Fixture(int w = 4, int h = 4)
    {
        context.width = w;
        context.height = h;
        context.stageWidth = w;
        context.stageHeight = h;
        context.layer = [this](const std::string& name, Image& into) {
            const auto found = layers.find(name);
            if (found == layers.end())
                return false;
            std::fill(into.pixels.begin(), into.pixels.end(), found->second);
            return true;
        };
        context.rest = [this](const std::vector<std::string>& except, Image& into) {
            taken = except;
            std::fill(into.pixels.begin(), into.pixels.end(), rest);
            return true;
        };
    }
};

uint32_t pixel(const Image& image, int x, int y)
{
    return image.pixels[static_cast<size_t>(y) * image.width + x];
}

} // namespace

TEST(node_graphs_are_kept_and_read_back)
{
    Graph graph = starter();
    graph.nodes.push_back(make("blur", graph.newId(), 100.0, 40.5));
    graph.nodes.back().label = "Soft & \"quoted\"";
    graph.nodes.back().applied.setKey("radius", 0, "2");
    graph.nodes.back().applied.setKey("radius", 10, "8");
    graph.nodes.back().collapsed = true;
    graph.backdrops.push_back({graph.newId(), -10, -20, 400, 300, "Background", 0x40C080});
    graph.enabled = false;
    const std::string text = encode(graph);
    Graph back;
    CHECK(decode(text, back));
    CHECK(back == graph);
    CHECK(!back.enabled && back.nodes.size() == 3 && back.links.size() == 1);
    CHECK(back.nodes[2].label == "Soft & \"quoted\"" && back.nodes[2].collapsed && back.nodes[2].x == 100.0);
    CHECK(back.nodes[2].applied.keyed("radius"));
    CHECK(back.backdrops.size() == 1 && back.backdrops[0].title == "Background" && back.backdrops[0].color == 0x40C080);
    CHECK(!decode("not a graph", back));
    // Links to missing nodes and loops are left out when read.
    CHECK(decode("<graph><node id='1' type='blur'/><node id='2' type='blur'/><link from='1' to='2' input='0'/>"
                 "<link from='2' to='1' input='0'/><link from='9' to='1' input='0'/></graph>",
        back));
    CHECK(back.links.size() == 1);
}

TEST(node_graphs_live_in_the_timeline)
{
    fla::Timeline timeline(nullptr);
    timeline.extraAttributes.emplace_back("other", "kept");
    CHECK(!of(timeline) && !active(timeline));
    const Graph graph = starter();
    timeline.extraAttributes = withGraph(timeline, &graph);
    CHECK(timeline.extraAttributes.size() == 2 && active(timeline));
    CHECK(of(timeline) && of(timeline)->nodes.size() == 2);
    // Read once, then the same graph while the attribute is the same.
    CHECK(of(timeline).get() == of(timeline).get());
    Graph off = graph;
    off.enabled = false;
    timeline.extraAttributes = withGraph(timeline, &off);
    CHECK(!active(timeline) && of(timeline) && !of(timeline)->enabled);
    timeline.extraAttributes = withGraph(timeline, nullptr);
    CHECK(timeline.extraAttributes.size() == 1 && !of(timeline));
}

TEST(node_graphs_connect_without_loops)
{
    Graph graph = starter();
    const int blur = graph.newId();
    graph.nodes.push_back(make("blur", blur, 0, 0));
    const int output = graph.output();
    CHECK(graph.connect(blur, output, 0));
    CHECK(graph.links.size() == 1 && graph.linkInto(output, 0)->from == blur);
    CHECK(!graph.connect(output, blur, 0));      // the Output gives nothing
    CHECK(!graph.connect(blur, blur, 0));        // a loop
    CHECK(!graph.connect(1, blur, 3));           // no such input
    const int composite = graph.newId();
    graph.nodes.push_back(make("composite", composite, 0, 0));
    CHECK(inputNames(*graph.node(composite)).size() == 2);
    CHECK(graph.connect(1, composite, 1));
    CHECK(inputNames(*graph.node(composite)).size() == 3 && inputNames(*graph.node(composite))[2] == "Layer 3");
    CHECK(graph.connect(composite, blur, 0));
    CHECK(!graph.connect(blur, composite, 0));   // would loop through it
    graph.remove(blur);
    CHECK(!graph.node(blur) && !graph.linkInto(output, 0));
}

TEST(a_starter_graph_shows_the_layers_as_they_were)
{
    Fixture f;
    const Graph graph = starter();
    Evaluation evaluation(graph, f.context);
    const Image& out = evaluation.output();
    CHECK(out.width == 4 && pixel(out, 2, 2) == f.rest);
    CHECK(f.taken.empty());
}

TEST(layer_nodes_take_their_layers_out_of_the_stack)
{
    Fixture f;
    Graph graph = starter();
    const int red = graph.newId();
    graph.nodes.push_back(make("layer", red, 0, 0));
    graph.node(red)->applied.params["layer"] = "Red";
    const int merge = graph.newId();
    graph.nodes.push_back(make("merge", merge, 0, 0));
    CHECK(graph.connect(red, merge, 0));
    CHECK(graph.connect(1, merge, 1));
    CHECK(graph.connect(merge, graph.output(), 0));
    {
        Evaluation evaluation(graph, f.context);
        CHECK(pixel(evaluation.output(), 0, 0) == kRed);
        CHECK(f.taken.size() == 1 && f.taken[0] == "Red");
    }
    // A Layer node that does not reach the output leaves its layer alone.
    graph.connect(1, graph.output(), 0);
    {
        Evaluation evaluation(graph, f.context);
        CHECK(pixel(evaluation.output(), 0, 0) == f.rest);
        CHECK(f.taken.empty());
    }
    // One naming no layer there says so.
    graph.node(red)->applied.params["layer"] = "Missing";
    Evaluation evaluation(graph, f.context);
    CHECK(evaluation.at(red).pixels[0] == 0u && !evaluation.problem(red).empty());
}

TEST(merge_blends_by_mode_and_opacity)
{
    Image under(1, 1);
    under.pixels[0] = 0xFF808080u;
    Image over(1, 1);
    over.pixels[0] = 0xFF808080u;
    Image multiplied = under;
    blend(over, multiplied, "Multiply", 1.0);
    CHECK(multiplied.pixels[0] == 0xFF404040u);
    Image screened = under;
    blend(over, screened, "Screen", 1.0);
    CHECK(screened.pixels[0] == 0xFFC0C0C0u);
    Image half = under;
    Image white(1, 1);
    white.pixels[0] = 0xFFFFFFFFu;
    blend(white, half, "Normal", 0.5);
    CHECK(half.pixels[0] == 0xFFC0C0C0u);
    // Over nothing, a half-transparent source stays as it was.
    Image empty(1, 1);
    Image faint(1, 1);
    faint.pixels[0] = 0x80800000u;
    blend(faint, empty, "Multiply", 1.0);
    CHECK(empty.pixels[0] == 0x80800000u);
}

TEST(composite_stacks_its_first_input_on_top)
{
    Fixture f;
    Graph graph;
    graph.nodes.push_back(make("layer", 1, 0, 0));
    graph.nodes[0].applied.params["layer"] = "Red";
    graph.nodes.push_back(make("layer", 2, 0, 0));
    graph.nodes[1].applied.params["layer"] = "Blue";
    graph.nodes.push_back(make("composite", 3, 0, 0));
    graph.nodes.push_back(make("output", 4, 0, 0));
    CHECK(graph.connect(1, 3, 0) && graph.connect(2, 3, 1) && graph.connect(3, 4, 0));
    Evaluation evaluation(graph, f.context);
    CHECK(pixel(evaluation.output(), 1, 1) == kRed);
    // Bypassed, a node passes its first input through.
    graph.node(3)->applied.enabled = false;
    Evaluation bypassed(graph, f.context);
    CHECK(pixel(bypassed.output(), 1, 1) == kRed);
}

TEST(matte_and_opacity_and_transform_nodes)
{
    Fixture f(8, 8);
    f.layers["Half"] = 0u;
    Graph graph;
    graph.nodes.push_back(make("layer", 1, 0, 0));
    graph.nodes[0].applied.params["layer"] = "Red";
    graph.nodes.push_back(make("transform", 2, 0, 0));
    graph.nodes[1].applied.params["x"] = "4";
    graph.nodes.push_back(make("output", 3, 0, 0));
    CHECK(graph.connect(1, 2, 0) && graph.connect(2, 3, 0));
    {
        // Moved 4 pixels right: the left half is empty now.
        Evaluation evaluation(graph, f.context);
        CHECK(pixel(evaluation.output(), 1, 4) == 0u && pixel(evaluation.output(), 6, 4) == kRed);
    }
    graph.nodes.push_back(make("opacity", 4, 0, 0));
    graph.node(4)->applied.params["amount"] = "50";
    CHECK(graph.connect(1, 4, 0) && graph.connect(4, 3, 0));
    {
        Evaluation evaluation(graph, f.context);
        CHECK(pixel(evaluation.output(), 0, 0) == 0x80800000u);
    }
    // Red through a blue matte by alpha: all of it; inverted, none.
    graph.nodes.push_back(make("layer", 5, 0, 0));
    graph.node(5)->applied.params["layer"] = "Blue";
    graph.nodes.push_back(make("matte", 6, 0, 0));
    CHECK(graph.connect(1, 6, 0) && graph.connect(5, 6, 1) && graph.connect(6, 3, 0));
    {
        Evaluation evaluation(graph, f.context);
        CHECK(pixel(evaluation.output(), 3, 3) == kRed);
    }
    graph.node(6)->applied.params["kind"] = "Alpha Inverted";
    Evaluation evaluation(graph, f.context);
    CHECK(pixel(evaluation.output(), 3, 3) == 0u);
}

TEST(effect_nodes_run_the_adjustment_effects)
{
    Fixture f(16, 16);
    Graph graph;
    graph.nodes.push_back(make("solid", 1, 0, 0));
    graph.nodes[0].applied.params["color"] = "#FF0000";
    graph.nodes.push_back(make("huesat", 2, 0, 0));
    graph.nodes[1].applied.params["saturation"] = "-100";
    graph.nodes.push_back(make("output", 3, 0, 0));
    CHECK(graph.connect(1, 2, 0) && graph.connect(2, 3, 0));
    Evaluation evaluation(graph, f.context);
    const uint32_t p = pixel(evaluation.output(), 8, 8);
    // Grey now: the three channels alike.
    CHECK(((p >> 16) & 0xFF) == ((p >> 8) & 0xFF) && ((p >> 8) & 0xFF) == (p & 0xFF));
    // Every effect is a node with its settings, colors as colors.
    const Type* tritone = type("tritone");
    CHECK(tritone && tritone->category == "Color" && !tritone->params.empty() && tritone->params[0].type == "color");
    const Type* displace = type("displace");
    CHECK(displace && displace->inputs.size() == 2 && displace->inputs[1] == "Map");
    for (const Type& each : types())
        CHECK(std::find(categories().begin(), categories().end(), each.category) != categories().end());
}

TEST(layer_nodes_follow_a_renamed_layer)
{
    fla::Timeline timeline(nullptr);
    Graph graph = starter();
    graph.nodes.push_back(make("layer", 3, 0, 0));
    graph.node(3)->applied.params["layer"] = "Hero";
    graph.nodes.push_back(make("layer", 4, 0, 0));
    graph.node(4)->applied.setKey("layer", 0, "Hero");
    graph.node(4)->applied.setKey("layer", 12, "Sidekick");
    timeline.extraAttributes = withGraph(timeline, &graph);
    // A layer no node names leaves the graph as it is.
    CHECK(withLayerRenamed(timeline, "Background", "Sky") == timeline.extraAttributes);
    timeline.extraAttributes = withLayerRenamed(timeline, "Hero", "Hero Walk");
    const std::shared_ptr<const Graph> renamed = of(timeline);
    CHECK(renamed && renamed->node(3)->applied.params.at("layer") == "Hero Walk");
    CHECK(renamed->node(4)->applied.valueAt("layer", 0, "") == "Hero Walk");
    CHECK(renamed->node(4)->applied.valueAt("layer", 12, "") == "Sidekick");
}
