#include "shot_breakdown.h"

#include "camera.h"
#include "frame_bake.h"
#include "library_commands.h"
#include "timeline_edit.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace fla {

namespace shots {

namespace {

int sceneEnd(const Timeline& scene)
{
    int end = 0;
    for (const Layer* layer : scene.layers)
        if (layer)
            end = std::max(end, timeline::layerEnd(*layer));
    return std::max(end, 1);
}

std::string sceneName(const Document& document, int index)
{
    const Timeline* scene = document.timelines[static_cast<size_t>(index)];
    return scene && !scene->name.empty() ? scene->name : "Scene " + std::to_string(index + 1);
}

/// Whether the span starting at \a span tweens.
bool tweens(const Frame& span)
{
    return span.tweenType != TweenType::None || !span.otherTweenType.empty();
}

/// Bakes the frames from \a first to \a last of whatever tweened span
/// covers \a at and runs past the cut there.
void bakeAcross(Layer& layer, int at, int first, int last, double frameRate)
{
    Frame* span = timeline::spanAt(layer, at);
    if (!span || !tweens(*span))
        return;
    const int spanFirst = span->index;
    const int spanLast = span->index + std::max(span->duration, 1) - 1;
    const int from = std::max(spanFirst, first);
    const int to = std::min(spanLast, last);
    if (from <= to)
        frames::convertToFrameByFrame(layer, from, to, frameRate);
}

void removeUnused(Document& document)
{
    // Repeated: a symbol used only by symbols that go goes next time round.
    for (bool removed = true; removed;)
    {
        removed = false;
        const std::set<std::string> used = library::usedItems(document);
        if (document.symbolList)
        {
            std::vector<Symbol*>& symbols = document.symbolList->symbols;
            for (auto it = symbols.begin(); it != symbols.end();)
            {
                if (*it && !used.count((*it)->name))
                {
                    document.symbolList->symbolMap.erase((*it)->name);
                    delete *it;
                    it = symbols.erase(it);
                    removed = true;
                }
                else
                {
                    ++it;
                }
            }
        }
        std::vector<Resource*>& resources = document.resources;
        resources.erase(std::remove_if(resources.begin(), resources.end(),
                            [&used](Resource* resource) {
                                const bool unused = resource && resource->resourceType() == Resource::Type::Bitmap &&
                                                    !used.count(static_cast<Bitmap*>(resource)->name);
                                // Out of the document, it would never be freed.
                                if (unused)
                                    delete resource;
                                return unused;
                            }),
            resources.end());
    }
}

} // namespace

std::string fileName(const std::string& name)
{
    std::string out;
    for (const char c : name)
        out += (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            ? '_' : c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    return out.empty() ? std::string("shot") : out;
}

std::vector<Shot> find(const Document& document, CutBy by)
{
    std::vector<Shot> found;
    for (int s = 0; s < static_cast<int>(document.timelines.size()); ++s)
    {
        const Timeline* scene = document.timelines[static_cast<size_t>(s)];
        if (!scene)
            continue;
        const int end = sceneEnd(*scene);
        const std::string name = sceneName(document, s);
        // Where shots start in the scene, and their names.
        std::map<int, std::string> starts;
        if (by == CutBy::Label)
        {
            for (const Layer* layer : scene->layers)
                for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                    if (frame && !frame->name.empty() && frame->labelType != "comment" && !starts.count(frame->index))
                        starts[frame->index] = frame->name;
        }
        else if (by == CutBy::Camera)
        {
            if (const Layer* camera = camera::cameraOf(*scene))
            {
                int cut = 0;
                const Frame* previous = nullptr;
                for (const Frame* frame : camera->frames)
                {
                    if (frame && previous && frame->index > 0 && !tweens(*previous))
                    {
                        char label[32];
                        std::snprintf(label, sizeof(label), "_cut%02d", ++cut + 1);
                        starts[frame->index] = name + label;
                    }
                    previous = frame;
                }
                if (!starts.empty())
                    starts[0] = name + "_cut01";
            }
        }
        if (starts.empty())
        {
            found.push_back({name, s, 0, end - 1});
            continue;
        }
        // Frames before the first label are a shot of their own.
        if (starts.begin()->first > 0)
            starts[0] = name + "_head";
        for (auto it = starts.begin(); it != starts.end(); ++it)
        {
            const auto next = std::next(it);
            const int last = (next == starts.end() ? end : next->first) - 1;
            if (it->first <= last)
                found.push_back({it->second, s, it->first, last});
        }
    }
    return found;
}

void keepOnly(Document& document, const Shot& shot)
{
    if (shot.scene < 0 || shot.scene >= static_cast<int>(document.timelines.size()))
        return;
    Timeline* scene = document.timelines[static_cast<size_t>(shot.scene)];
    // The other scenes go.
    for (size_t i = 0; i < document.timelines.size(); ++i)
        if (document.timelines[i] != scene)
            delete document.timelines[i];
    document.timelines = {scene};
    document.setCurrentScene(0);
    scene->name = shot.name;

    const double frameRate = document.frameRate > 0.0 ? document.frameRate : 24.0;
    Layer* cameraLayer = camera::cameraOf(*scene);
    // Where the camera stands at each cut, before anything moves.
    const camera::State atFirst = camera::stateAt(*scene, shot.first, document.width, document.height);
    const camera::State atLast = camera::stateAt(*scene, shot.last, document.width, document.height);

    for (Layer* layer : scene->layers)
    {
        if (!layer)
            continue;
        if (layer == cameraLayer)
        {
            // Keyed at both cuts as it was there; between, it moves as
            // before.
            for (const auto& [at, state] : {std::pair<int, camera::State>{shot.first, atFirst}, {shot.last, atLast}})
            {
                Frame* span = timeline::spanAt(*layer, at);
                const TweenType tween = span ? span->tweenType : TweenType::None;
                if (Frame* key = timeline::insertKeyframe(*layer, at, false))
                {
                    camera::set(key->extraAttributes, state);
                    key->tweenType = tween;
                }
            }
        }
        else
        {
            bakeAcross(*layer, shot.first, shot.first, shot.last, frameRate);
            bakeAcross(*layer, shot.last, shot.first, shot.last, frameRate);
            if (shot.first > 0)
                timeline::insertKeyframe(*layer, shot.first, false);
        }
        std::vector<Frame*> removed;
        if (shot.first > 0)
            timeline::removeFrames(*layer, 0, shot.first, removed);
        const int length = shot.last - shot.first + 1;
        const int end = timeline::layerEnd(*layer);
        if (end > length)
            timeline::removeFrames(*layer, length, end - length, removed);
        for (Frame* frame : removed)
            delete frame;
        // The last keyframe tweens to nothing now.
        if (!layer->frames.empty() && layer != cameraLayer)
            layer->frames.back()->tweenType = TweenType::None;
    }
    removeUnused(document);
}

} // namespace shots

} // namespace fla
