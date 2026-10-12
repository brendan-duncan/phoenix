#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Document;
class Timeline;

/// Puts a document's scenes into a new order, with scenes added or taken
/// out, and makes one of them current: adding, duplicating, deleting and
/// reordering scenes, each one undo step. A scene that is out of the
/// document (deleted, or added and then undone) belongs to the command.
class SceneListCommand : public Command
{
public:
    SceneListCommand(Document* document, std::vector<Timeline*> after, int currentAfter, const std::string& name);
    ~SceneListCommand() override;

    SceneListCommand(const SceneListCommand&) = delete;
    SceneListCommand& operator=(const SceneListCommand&) = delete;

    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

private:
    void apply(const std::vector<Timeline*>& scenes, int current);

    Document* _document;
    std::vector<Timeline*> _before;
    std::vector<Timeline*> _after;
    int _currentBefore;
    int _currentAfter;
    std::string _name;
    /// The scenes out of the document as it stands, which the command owns.
    std::vector<Timeline*> _out;
};

namespace scenes {

/// A name no scene has: "Scene N" for the first free N, or \a base itself
/// (as "Scene 1 copy") when that is free.
std::string freeName(const Document& document, const std::string& base = "Scene");

/// A new, empty scene with one layer as long as one frame.
Timeline* newScene(Document& document, const std::string& name);

/// A deep copy of \a scene, its layers and frames, named \a name.
Timeline* duplicate(const Timeline& scene, Document& document, const std::string& name);

} // namespace scenes

} // namespace fla
