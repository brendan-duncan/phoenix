#include "lip_sync.h"

#include "element_clone.h"
#include "timeline_edit.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol_instance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <memory>

namespace fla {

namespace lipsync {

namespace {

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

std::vector<Cue> listen(const std::vector<int16_t>& samples, int channels, int sampleRate, double frameRate)
{
    std::vector<Cue> cues;
    channels = std::max(channels, 1);
    if (samples.empty() || sampleRate <= 0 || frameRate <= 0.0)
        return cues;
    // One window a frame: its loudness (RMS, 0 to 1) and how often the
    // wave crosses zero (high for hissing, low for voiced vowels).
    const size_t frames = samples.size() / static_cast<size_t>(channels);
    const size_t window = std::max<size_t>(1, static_cast<size_t>(sampleRate / frameRate));
    std::vector<double> loudness;
    std::vector<double> crossings;
    for (size_t start = 0; start < frames; start += window)
    {
        const size_t end = std::min(frames, start + window);
        double sum = 0.0;
        int crossed = 0;
        double previous = 0.0;
        for (size_t i = start; i < end; ++i)
        {
            double value = 0.0;
            for (int c = 0; c < channels; ++c)
                value += samples[i * static_cast<size_t>(channels) + static_cast<size_t>(c)];
            value /= channels * 32768.0;
            sum += value * value;
            if (i > start && ((value >= 0.0) != (previous >= 0.0)))
                ++crossed;
            previous = value;
        }
        loudness.push_back(std::sqrt(sum / static_cast<double>(end - start)));
        crossings.push_back(static_cast<double>(crossed) / static_cast<double>(end - start));
    }
    // Measured against the loud parts of this recording, so a quiet one
    // still opens the mouth.
    std::vector<double> sorted = loudness;
    std::sort(sorted.begin(), sorted.end());
    const double peak = std::max(sorted[static_cast<size_t>((sorted.size() - 1) * 0.95)], 1.0e-4);
    std::vector<std::string> shapes;
    for (size_t i = 0; i < loudness.size(); ++i)
    {
        const double level = loudness[i] / peak;
        std::string shape;
        if (level < 0.08)
            shape = "X";
        else if (crossings[i] > 0.25 && level < 0.6)
            shape = "B";
        else if (level < 0.25)
            shape = "A";
        else if (level < 0.5)
            shape = "C";
        else if (level < 0.8)
            shape = "E";
        else
            shape = "D";
        shapes.push_back(shape);
    }
    // A dip to closed between two open shapes reads as a syllable break;
    // anything else held under two frames takes the shape before it.
    for (size_t i = 1; i < shapes.size(); ++i)
    {
        const bool lone = shapes[i] != shapes[i - 1] && (i + 1 >= shapes.size() || shapes[i + 1] != shapes[i]);
        if (lone && shapes[i] != "A")
            shapes[i] = shapes[i - 1];
    }
    for (size_t i = 0; i < shapes.size(); ++i)
    {
        if (cues.empty() || cues.back().shape != shapes[i])
            cues.push_back({static_cast<double>(i) / frameRate, shapes[i]});
    }
    // Rhubarb ends on the rest shape.
    if (!cues.empty() && cues.back().shape != "X")
        cues.push_back({static_cast<double>(shapes.size()) / frameRate, "X"});
    return cues;
}

std::string tsvOf(const std::vector<Cue>& cues)
{
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    for (const Cue& cue : cues)
        text << cue.time << '\t' << cue.shape << '\n';
    return text.str();
}

std::vector<Cue> parse(const std::string& text, double frameRate)
{
    std::vector<Cue> cues;
    std::istringstream in(text);
    std::string line;
    bool moho = false;
    bool first = true;
    while (std::getline(in, line))
    {
        line = trimmed(line);
        if (line.empty())
            continue;
        if (first)
        {
            first = false;
            if (line.compare(0, 10, "MohoSwitch") == 0)
            {
                moho = true;
                continue;
            }
        }
        std::istringstream fields(line);
        std::string when;
        std::string shape;
        if (!(fields >> when >> shape))
            continue;
        char* end = nullptr;
        const double value = std::strtod(when.c_str(), &end);
        if (end == when.c_str() || !std::isfinite(value))
            continue;
        Cue cue;
        cue.shape = shape;
        // Papagayo counts frames from 1; Rhubarb gives seconds. Either is
        // held within the longest timeline there can be.
        const double fps = std::max(frameRate, 1.0);
        cue.time = moho ? (value - 1.0) / fps : value;
        cue.time = std::clamp(cue.time, 0.0, kMaxFrames / fps);
        cues.push_back(cue);
    }
    std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.time < b.time; });
    return cues;
}

std::vector<std::string> shapesOf(const std::vector<Cue>& cues)
{
    static const std::vector<std::string> order = {"A", "B", "C", "D", "E", "F", "G", "H", "X", "AI", "O", "U", "WQ",
        "MBP", "FV", "L", "etc", "rest"};
    std::vector<std::string> shapes;
    for (const std::string& known : order)
        if (std::any_of(cues.begin(), cues.end(), [&known](const Cue& cue) { return cue.shape == known; }))
            shapes.push_back(known);
    for (const Cue& cue : cues)
        if (std::find(shapes.begin(), shapes.end(), cue.shape) == shapes.end())
            shapes.push_back(cue.shape);
    return shapes;
}

std::map<std::string, int> defaultChart(const std::vector<std::string>& shapes)
{
    std::map<std::string, int> chart;
    int frame = 1;
    for (const std::string& shape : shapes)
        chart[shape] = frame++;
    return chart;
}

std::string chartText(const std::map<std::string, int>& chart)
{
    std::string text;
    for (const auto& [shape, frame] : chart)
        text += (text.empty() ? "" : ";") + shape + "=" + std::to_string(frame);
    return text;
}

std::map<std::string, int> chartFrom(const std::string& text)
{
    std::map<std::string, int> chart;
    std::istringstream in(text);
    std::string pair;
    while (std::getline(in, pair, ';'))
    {
        const size_t equals = pair.find('=');
        if (equals != std::string::npos)
            chart[pair.substr(0, equals)] = std::atoi(pair.c_str() + equals + 1);
    }
    return chart;
}

CommandPtr apply(Layer& layer, const SymbolInstance& mouth, const std::vector<Cue>& cues,
    const std::map<std::string, int>& chart, int start, double frameRate)
{
    if (cues.empty())
        return nullptr;
    const double fps = frameRate > 0.0 ? frameRate : 24.0;
    // A keyframe where the mouth changes, on the frame its cue falls on.
    std::vector<std::pair<int, int>> keys;
    for (const Cue& cue : cues)
    {
        const auto found = chart.find(cue.shape);
        const int shown = found == chart.end() ? 1 : std::max(1, found->second);
        const double at = cue.time * fps;
        const int frame = clampFrameIndex(
            static_cast<int64_t>(start) + (at > 0.0 ? std::llround(std::min(at, static_cast<double>(kMaxFrames))) : 0));
        if (!keys.empty() && keys.back().first == frame)
            keys.back().second = shown;
        else if (keys.empty() || keys.back().second != shown)
            keys.emplace_back(frame, shown);
    }
    if (keys.empty())
        return nullptr;
    const int last = keys.back().first;
    // The copy each keyframe holds is made now, from the mouth as it is.
    std::shared_ptr<Element> model(cloneElement(mouth, nullptr));
    Layer* target = &layer;
    return CommandPtr(new EditFramesCommand({target}, "Lip Sync", [target, keys, start, last, model](std::vector<Frame*>& removed) {
        bool keyedAfter = false;
        for (const Frame* frame : target->frames)
            keyedAfter = keyedAfter || (frame && frame->index == last + 1);
        timeline::clearFrames(*target, start, last, removed);
        // The last shape holds on to the next keyframe there was, rather
        // than the mouth as it was coming back after it.
        if (!keyedAfter)
            timeline::clearKeyframe(*target, last + 1, removed);
        for (const auto& [frame, shown] : keys)
        {
            Frame* key = timeline::insertKeyframe(*target, frame, true);
            if (!key)
                continue;
            Element* copy = cloneElement(*model, key);
            if (auto* instance = dynamic_cast<SymbolInstance*>(copy))
            {
                instance->firstFrame = shown - 1;
                instance->loopType = LoopType::SingleFrame;
            }
            key->elements.push_back(copy);
        }
    }));
}

} // namespace lipsync

} // namespace fla
