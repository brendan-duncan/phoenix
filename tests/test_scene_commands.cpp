#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/scene_commands.h"

#include <memory>

TEST(scenes_add_duplicate_delete_and_reorder_undoably)
{
    fla::Document document(nullptr);
    fla::Timeline* first = fla::scenes::newScene(document, fla::scenes::freeName(document));
    document.timelines.push_back(first);
    document.setCurrentScene(0);
    CHECK(first->name == "Scene 1" && first->layers.size() == 1 && first->layers[0]->frames.size() == 1);
    first->layers[0]->frames[0]->duration = 12;

    fla::CommandStack stack;
    // Add after it, and it becomes current.
    fla::Timeline* second = fla::scenes::newScene(document, fla::scenes::freeName(document));
    CHECK(second->name == "Scene 2");
    stack.push(fla::CommandPtr(new fla::SceneListCommand(&document, {first, second}, 1, "Add Scene")));
    CHECK(document.timelines.size() == 2 && document.currentScene() == second);

    // A duplicate is a deep copy.
    fla::Timeline* copy = fla::scenes::duplicate(*first, document, "Scene 1 copy");
    CHECK(copy->layers.size() == 1 && copy->layers[0] != first->layers[0]);
    CHECK(copy->layers[0]->frames[0]->duration == 12 && copy->layers[0]->parent == copy);
    stack.push(fla::CommandPtr(new fla::SceneListCommand(&document, {first, copy, second}, 1, "Duplicate Scene")));
    CHECK(document.timelines[1] == copy && document.currentSceneIndex() == 1);

    // Reorder, delete, and back again.
    stack.push(fla::CommandPtr(new fla::SceneListCommand(&document, {second, first, copy}, 2, "Reorder Scenes")));
    CHECK(document.timelines[0] == second && document.currentScene() == copy);
    stack.push(fla::CommandPtr(new fla::SceneListCommand(&document, {second, first}, 1, "Delete Scene")));
    CHECK(document.timelines.size() == 2 && document.currentScene() == first);
    stack.undo();
    CHECK(document.timelines.size() == 3 && document.currentScene() == copy);
    stack.undo();
    stack.undo();
    stack.undo();
    CHECK(document.timelines.size() == 1 && document.currentScene() == first);
    // Redo brings the added scene back; the stack owns what is out.
    stack.redo();
    CHECK(document.timelines.size() == 2 && document.timelines[1] == second);
}
