#pragma once

#include <QPointF>
#include <QWidget>

#include <functional>

class QTreeWidget;
class QPushButton;

namespace fla {
class CommandStack;
class Layer;
class Selection;
class Timeline;
}

/// What the Review Notes panel works on.
struct NotesLinks
{
    std::function<fla::CommandStack*()> commands;
    /// The scene (or symbol) the notes are on.
    std::function<fla::Timeline*()> timeline;
    std::function<int()> currentFrame;
    std::function<void(int)> setCurrentFrame;
    /// Makes a layer the one drawn on.
    std::function<void(const fla::Layer*)> setCurrentLayer;
    std::function<fla::Selection*()> selection;
    /// Where on the stage a new note goes: the corner of what is in view.
    std::function<QPointF()> corner;
    std::function<void()> changed;
};

/// The Review Notes panel: notes written on frames by whoever reviews a
/// shot, listed by frame. Add Note writes one on the frame shown; a
/// click goes to its frame; Done ticks it off; Draw makes the notes layer
/// the one drawn on, for marks over the art. Notes are kept on a guide
/// layer, so they never export.
class NotesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NotesPanel(NotesLinks links, QWidget* parent = nullptr);

    void refresh();

private:
    void addNote();
    void setDone(bool done);
    void removeNote();
    void drawOnNotes();
    /// The notes layer, made (one undo step with what follows) when there
    /// is none.
    fla::Layer* ensureLayer(fla::Timeline& timeline);

    NotesLinks _links;
    QTreeWidget* _list;
    QPushButton* _done;
    QPushButton* _remove;
};
