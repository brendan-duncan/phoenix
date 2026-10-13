#include "scene_commands.h"

#include "layer_commands.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <set>

namespace fla {

SceneListCommand::SceneListCommand(Document* document, std::vector<Timeline*> after, int currentAfter,
    const std::string& name)
    : _document(document)
    , _before(document->timelines)
    , _after(std::move(after))
    , _currentBefore(document->currentSceneIndex())
    , _currentAfter(currentAfter)
    , _name(name)
{
    // Until the first redo, a scene only the edit has is the command's.
    for (Timeline* scene : _after)
        if (std::find(_before.begin(), _before.end(), scene) == _before.end())
            _out.push_back(scene);
}

SceneListCommand::~SceneListCommand()
{
    for (Timeline* scene : _out)
        delete scene;
}

void SceneListCommand::apply(const std::vector<Timeline*>& scenes, int current)
{
    _document->timelines = scenes;
    for (Timeline* scene : scenes)
        scene->parent = _document;
    // clamp with an empty list would have its bounds the wrong way round.
    _document->setCurrentScene(scenes.empty() ? 0 : std::clamp(current, 0, static_cast<int>(scenes.size()) - 1));
    // What is out of the document now is the command's to keep.
    _out.clear();
    std::set<Timeline*> in(scenes.begin(), scenes.end());
    for (const std::vector<Timeline*>* list : {&_before, &_after})
        for (Timeline* scene : *list)
            if (!in.count(scene) && std::find(_out.begin(), _out.end(), scene) == _out.end())
                _out.push_back(scene);
}

void SceneListCommand::redo()
{
    apply(_after, _currentAfter);
}

void SceneListCommand::undo()
{
    apply(_before, _currentBefore);
}

namespace scenes {

std::string freeName(const Document& document, const std::string& base)
{
    const auto taken = [&document](const std::string& name) {
        return std::any_of(document.timelines.begin(), document.timelines.end(),
            [&name](const Timeline* scene) { return scene && scene->name == name; });
    };
    // A name of its own (a copy's) as it is, when it is free.
    if (base != "Scene" && !taken(base))
        return base;
    for (int n = 1;; ++n)
    {
        const std::string name = base + " " + std::to_string(n);
        if (std::none_of(document.timelines.begin(), document.timelines.end(),
                [&name](const Timeline* scene) { return scene && scene->name == name; }))
            return name;
    }
}

Timeline* newScene(Document& document, const std::string& name)
{
    Timeline* scene = new Timeline(&document);
    scene->name = name;
    Layer* layer = newLayerFor(*scene, false);
    layer->parent = scene;
    layer->selected = true;
    scene->layers.push_back(layer);
    return scene;
}

Timeline* duplicate(const Timeline& scene, Document& document, const std::string& name)
{
    Timeline* copy = new Timeline(&document);
    copy->name = name;
    copy->layerDepthEnabled = scene.layerDepthEnabled;
    copy->guidesX = scene.guidesX;
    copy->guidesY = scene.guidesY;
    copy->extraChildren = scene.extraChildren;
    copy->extraAttributes = scene.extraAttributes;
    std::vector<const Layer*> layers(scene.layers.begin(), scene.layers.end());
    // Rigs renumbered from scratch, as the copy starts with none.
    const Timeline empty(nullptr);
    for (Layer* layer : layers::cloneLayers(layers, empty))
    {
        layer->parent = copy;
        copy->layers.push_back(layer);
    }
    for (Layer* layer : copy->layers)
    {
        const auto at = std::find(copy->layers.begin(), copy->layers.end(), layer->parentLayer);
        layer->parentLayerIndex = at == copy->layers.end() ? -1 : static_cast<int>(at - copy->layers.begin());
    }
    return copy;
}

} // namespace scenes

} // namespace fla
