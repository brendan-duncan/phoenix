#include "test_util.h"

#include <cmath>

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/lip_sync.h"

#include <memory>
#include <cstdint>

TEST(lip_sync_reads_rhubarb_and_papagayo_and_keys_the_mouth)
{
    // Rhubarb: seconds and shapes.
    const std::vector<fla::lipsync::Cue> rhubarb =
        fla::lipsync::parse("0.00\tX\n0.25\tB\n0.30\tB\n0.50\tE\n1.00\tX\n", 24.0);
    CHECK(rhubarb.size() == 5 && rhubarb[2].time == 0.30 && rhubarb[3].shape == "E");
    // Papagayo: frames from 1.
    const std::vector<fla::lipsync::Cue> papagayo = fla::lipsync::parse("MohoSwitch1\n1 rest\n13 AI\n25 rest\n", 24.0);
    CHECK(papagayo.size() == 3 && papagayo[1].shape == "AI" && papagayo[1].time == 0.5);
    CHECK(fla::lipsync::parse("nothing here", 24.0).empty());

    const std::vector<std::string> shapes = fla::lipsync::shapesOf(rhubarb);
    CHECK((shapes == std::vector<std::string>{"B", "E", "X"}));
    std::map<std::string, int> chart = fla::lipsync::defaultChart(shapes);
    CHECK(chart["B"] == 1 && chart["X"] == 3);
    chart["X"] = 9;
    CHECK(fla::lipsync::chartFrom(fla::lipsync::chartText(chart)) == chart);

    // A mouth layer: one long span with the mouth graphic.
    fla::Layer layer(nullptr);
    fla::Frame* frame = new fla::Frame(&layer);
    frame->duration = 60;
    fla::SymbolInstance* mouth = new fla::SymbolInstance(frame);
    mouth->libraryItemName = "Mouth";
    frame->elements.push_back(mouth);
    layer.frames.push_back(frame);

    fla::CommandStack stack;
    stack.push(fla::lipsync::apply(layer, *mouth, rhubarb, chart, 10, 24.0));
    // Keys at 10 (X), 16 (B; the second B is no change), 22 (E), 34 (X).
    std::vector<std::pair<int, int>> keys;
    for (const fla::Frame* f : layer.frames)
        for (const fla::Element* element : f->elements)
            if (const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element))
                keys.emplace_back(f->index, instance->firstFrame + 1);
    CHECK(keys.size() == 5);
    CHECK(keys[0] == std::make_pair(0, 1));
    CHECK(keys[1] == std::make_pair(10, 9) && keys[2] == std::make_pair(16, 1) && keys[3] == std::make_pair(22, 2));
    CHECK(keys[4] == std::make_pair(34, 9));
    stack.undo();
    CHECK(layer.frames.size() == 1 && layer.frames[0]->duration == 60);

    // Times past any timeline, or not numbers at all, are held or dropped.
    const std::vector<fla::lipsync::Cue> wild = fla::lipsync::parse("nan\tA\ninf\tB\n1e300\tC\n-5\tD\n", 24.0);
    CHECK(wild.size() == 2 && wild[0].shape == "D" && wild[0].time == 0.0);
    CHECK(wild.size() == 2 && wild[1].time * 24.0 <= fla::kMaxFrames);
    stack.push(fla::lipsync::apply(layer, *mouth, wild, chart, 10, 24.0));
    for (const fla::Frame* f : layer.frames)
        CHECK(f->index >= 0 && f->index <= fla::kMaxFrames);
}

TEST(lip_sync_chart_follows_the_mouth_frame_labels)
{
    // A mouth graphic whose frames are labeled A, C, X (and a comment).
    fla::Symbol symbol(nullptr);
    fla::Timeline* timeline = new fla::Timeline(&symbol);
    symbol.timelines.push_back(timeline);
    fla::Layer* labels = new fla::Layer(timeline);
    timeline->layers.push_back(labels);
    const auto label = [labels](int index, const char* name, const char* type) {
        fla::Frame* frame = new fla::Frame(labels);
        frame->index = index;
        frame->name = name;
        frame->labelType = type;
        labels->frames.push_back(frame);
    };
    label(0, "A", "name");
    label(1, "notes", "comment");
    label(2, "c", "name");
    label(8, "X", "name");
    fla::SymbolInstance mouth(nullptr);
    mouth.symbol = &symbol;
    const std::map<std::string, int> found = fla::lipsync::labelsOf(mouth);
    CHECK(found.size() == 3 && found.at("A") == 1 && found.at("c") == 3 && found.at("X") == 9);

    // Labeled shapes go to their frames, case ignored; the rest in order.
    std::map<std::string, int> chart = fla::lipsync::defaultChart({"A", "B", "C", "X"}, found);
    CHECK(chart["A"] == 1 && chart["B"] == 2 && chart["C"] == 3 && chart["X"] == 9);
    chart = fla::lipsync::defaultChart({"B", "X"}, found);
    CHECK(chart["B"] == 1 && chart["X"] == 9);
}

TEST(lip_sync_listens_to_loudness_without_rhubarb)
{
    // Half a second of quiet, a loud low vowel, a quieter hiss, quiet.
    const int rate = 24000;
    std::vector<int16_t> samples;
    const auto add = [&](double seconds, double amplitude, double hertz) {
        const int count = static_cast<int>(seconds * rate);
        for (int i = 0; i < count; ++i)
            samples.push_back(static_cast<int16_t>(amplitude * 32000.0 * std::sin(2.0 * 3.14159265358979 * hertz * i / rate)));
    };
    add(0.5, 0.0, 0.0);
    add(0.5, 0.9, 220.0);
    add(0.5, 0.3, 6000.0);
    add(0.5, 0.0, 0.0);
    const std::vector<fla::lipsync::Cue> cues = fla::lipsync::listen(samples, 1, rate, 24.0);
    CHECK(cues.size() >= 4);
    if (cues.size() < 4)
        return;
    CHECK(cues.front().shape == "X" && cues.front().time == 0.0);
    // Open wide for the vowel, teeth together for the hiss, at rest after.
    const auto shapeAt = [&cues](double time) {
        std::string shape = cues.front().shape;
        for (const fla::lipsync::Cue& cue : cues)
            if (cue.time <= time)
                shape = cue.shape;
        return shape;
    };
    CHECK(shapeAt(0.75) == "D");
    CHECK(shapeAt(1.25) == "B");
    CHECK(shapeAt(1.9) == "X");
    CHECK(cues.back().shape == "X");

    // A voice that swells over its first frames opens the mouth on the
    // first of them, not once it is loud.
    samples.clear();
    add(0.5, 0.0, 0.0);
    add(1.0 / 24.0, 0.5, 220.0);
    add(1.0 / 24.0, 0.27, 220.0);
    add(0.75, 0.65, 220.0);
    add(0.5, 0.0, 0.0);
    const std::vector<fla::lipsync::Cue> swell = fla::lipsync::listen(samples, 1, rate, 24.0);
    CHECK(swell.size() >= 2 && swell[0].shape == "X" && swell[1].shape != "X");
    if (swell.size() >= 2)
        CHECK(std::abs(swell[1].time - 0.5) < 1.0e-9);

    // As Rhubarb writes it, and read back the same.
    const std::vector<fla::lipsync::Cue> again = fla::lipsync::parse(fla::lipsync::tsvOf(cues), 24.0);
    CHECK(again.size() == cues.size() && again[1].shape == cues[1].shape);
}
