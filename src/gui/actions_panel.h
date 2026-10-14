#pragma once

#include <QTimer>
#include <QWidget>

#include <functional>
#include <vector>

class CodeEditor;
class CodeView;
class QLabel;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace fla {
class CommandStack;
class Document;
class Element;
class Frame;
class Layer;
class Timeline;
} // namespace fla

/// What the Actions panel reads and edits through. None of it is owned.
struct ActionsLinks
{
    std::function<fla::CommandStack*()> commands;
    std::function<fla::Document*()> document;
    /// The timeline being edited: a symbol's, or the current scene.
    std::function<fla::Timeline*()> timeline;
    std::function<int()> currentFrame;
    std::function<std::vector<fla::Element*>()> selection;
};

/// Window > Actions (F9): the script of the keyframe at the playhead on the
/// current layer, or of the button or movie clip instance selected on the
/// stage, edited as it is typed (each pause in typing one step to undo).
/// The script navigator lists every script in the document; the pin keeps
/// one script in the panel while the selection moves on.
class ActionsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ActionsPanel(const ActionsLinks& links, QWidget* parent = nullptr);

    /// Follows the selection, the playhead and edits made elsewhere (undo,
    /// scripts): shows the script they lead to, as it now is.
    void refresh();
    /// Puts what has been typed into the document now.
    void commit();
    void focusEditor();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Target
    {
        enum class Kind
        {
            None,
            Frame,
            Instance,
        };
        Kind kind = Kind::None;
        fla::Timeline* timeline = nullptr;
        fla::Layer* layer = nullptr;
        fla::Frame* frame = nullptr;
        fla::Element* element = nullptr;
        /// Why there is nothing to edit, for Kind::None.
        QString why;

        bool operator==(const Target& other) const
        {
            return kind == other.kind && frame == other.frame && element == other.element;
        }
    };

    Target followed() const;
    bool stillThere(const Target& target) const;
    QString describe(const Target& target) const;
    QString codeOf(const Target& target) const;
    void show(const Target& target);
    void textEdited();
    void check();
    void rebuildNavigator();
    void navigatorPicked(QTreeWidgetItem* item);
    void setPinned(bool pinned);

    ActionsLinks _links;
    CodeView* _view = nullptr;
    CodeEditor* _editor = nullptr;
    QLabel* _where = nullptr;
    QLabel* _status = nullptr;
    QToolButton* _pin = nullptr;
    QToolButton* _navigatorButton = nullptr;
    QSplitter* _splitter = nullptr;
    QTreeWidget* _navigator = nullptr;
    std::vector<Target> _navigatorTargets;
    Target _target;
    bool _pinned = false;
    bool _loading = false;
    bool _busy = false;
    bool _dirty = false;
    // Typing between two changes of script, or of focus, merges into one
    // step to undo.
    int _session = 0;
    QTimer _commitTimer;
};
