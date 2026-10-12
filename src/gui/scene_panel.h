#pragma once

#include <QWidget>

#include <functional>
#include <vector>

class QListWidget;
class QListWidgetItem;

namespace fla {
class CommandStack;
class Document;
class Timeline;
}

struct SceneLinks
{
    std::function<fla::Document*()> document;
    std::function<fla::CommandStack*()> commands;
    /// Shows the scene at an index, or the current one again after the
    /// list changed.
    std::function<void(int)> showScene;
};

/// The Scene panel: the document's scenes in playing order. Click one to
/// edit it, drag to reorder, double-click to rename; add, duplicate and
/// delete with the buttons under the list. Every change is one undo step.
class ScenePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScenePanel(SceneLinks links, QWidget* parent = nullptr);

    /// Reads the scenes again.
    void refresh();

    /// Adds a new scene after the current one, or a copy of it.
    void addScene(bool duplicate);

private:
    void deleteScene();
    void renamed(QListWidgetItem* item);
    void reordered();
    void push(std::vector<fla::Timeline*> scenes, int current, const char* name);

    SceneLinks _links;
    QListWidget* _list;
    bool _refreshing = false;
};
