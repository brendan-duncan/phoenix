#include "review_notes.h"

#include "timeline_edit.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/timeline.h"

#include <algorithm>

namespace fla {

namespace review {

namespace {

const char* const kLayerMark = "phoenixReview";
const char* const kNote = "phoenixNote";
const char* const kAuthor = "phoenixNoteAuthor";
const char* const kDone = "phoenixNoteDone";

std::string attribute(const std::vector<std::pair<std::string, std::string>>& attributes, const char* name)
{
    for (const auto& [key, value] : attributes)
        if (key == name)
            return value;
    return std::string();
}

} // namespace

Layer* notesLayer(const Timeline& timeline)
{
    for (Layer* layer : timeline.layers)
        if (layer && layer->layerType == Layer::Type::Guide && attribute(layer->extraAttributes, kLayerMark) == "true")
            return layer;
    return nullptr;
}

Layer* newNotesLayer(const Timeline& timeline)
{
    Layer* layer = new Layer(nullptr);
    layer->name = "Review Notes";
    layer->layerType = Layer::Type::Guide;
    layer->extraAttributes.emplace_back(kLayerMark, "true");
    // A note's own color in the timeline: the yellow of a sticky note.
    layer->color[0] = 255;
    layer->color[1] = 196;
    layer->color[2] = 0;
    layer->color[3] = 255;
    int length = 1;
    for (const Layer* other : timeline.layers)
        if (other)
            length = std::max(length, timeline::layerEnd(*other));
    Frame* frame = new Frame(layer);
    frame->index = 0;
    frame->duration = length;
    layer->frames.push_back(frame);
    return layer;
}

std::vector<Note> notes(const Timeline& timeline)
{
    std::vector<Note> found;
    const Layer* layer = notesLayer(timeline);
    if (!layer)
        return found;
    for (Frame* frame : layer->frames)
    {
        if (!frame)
            continue;
        for (Element* element : frame->elements)
        {
            auto* text = dynamic_cast<StaticText*>(element);
            if (!text || !isNote(*text))
                continue;
            Note note;
            note.frame = frame;
            note.text = text;
            note.index = frame->index;
            note.author = authorOf(*text);
            for (const TextRun& run : text->runs)
                note.body += run.text;
            note.done = isDone(*text);
            found.push_back(note);
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Note& a, const Note& b) { return a.index < b.index; });
    return found;
}

StaticText* makeNote(const std::string& author, const std::string& body, double x, double y)
{
    StaticText* text = new StaticText(nullptr);
    text->left = x;
    text->top = y;
    text->width = 320.0;
    text->autoExpand = false;
    TextRun run;
    run.text = author.empty() ? body : author + ": " + body;
    run.face = "ArialMT";
    run.size = 18.0;
    // The red of a reviewer's pen, readable on most stages.
    run.fillColor[0] = 214;
    run.fillColor[1] = 40;
    run.fillColor[2] = 40;
    run.fillColor[3] = 255;
    text->runs.push_back(run);
    text->extraAttributes.emplace_back(kNote, "true");
    if (!author.empty())
        text->extraAttributes.emplace_back(kAuthor, author);
    return text;
}

bool isNote(const StaticText& text)
{
    return attribute(text.extraAttributes, kNote) == "true";
}

std::string authorOf(const StaticText& text)
{
    return attribute(text.extraAttributes, kAuthor);
}

bool isDone(const StaticText& text)
{
    return attribute(text.extraAttributes, kDone) == "true";
}

std::vector<std::pair<std::string, std::string>> withDone(const StaticText& text, bool done)
{
    std::vector<std::pair<std::string, std::string>> attributes = text.extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& pair) { return pair.first == kDone; }),
        attributes.end());
    if (done)
        attributes.emplace_back(kDone, "true");
    return attributes;
}

} // namespace review

} // namespace fla
