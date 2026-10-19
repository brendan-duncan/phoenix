#pragma once

#include "../edit/state_graph.h"
#include "states_view.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace fla {
class CommandStack;
class Document;
class Timeline;
}

struct StatesLinks
{
    std::function<fla::CommandStack*()> commands;
    std::function<fla::Document*()> document;
    /// The timeline being edited: a scene, or the symbol opened.
    std::function<fla::Timeline*()> timeline;
    /// The frame at the playhead, from 0.
    std::function<int()> currentFrame;
    /// The instance name of the movie clip or button selected on the
    /// stage; empty for none.
    std::function<QString()> selectedInstance;
    std::function<void()> changed;
    /// Test Movie.
    std::function<void()> test;
};

/// The States panel: interactivity made without code. The timeline's
/// frame labels are its states, each looping, playing once or holding;
/// arrows between them are the transitions, set off by a click on an
/// instance, a key, the end of a state, a timer or an input; listeners set
/// the inputs. Every change is one undo step, and turns the graph into
/// ordinary ActionScript on the timeline's first frame (see fla::states).
class StatesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StatesPanel(StatesLinks links, QWidget* parent = nullptr);

    /// Shows the edited timeline's graph again, as an edit or an undo left it.
    void refresh();

private:
    enum class Picked
    {
        None,
        State,
        Transition,
        Input,
        Listener,
    };

    fla::Timeline* timeline() const;
    int version() const;
    double frameRate() const;
    /// Keeps \a graph with the labels \a edit makes, as one step.
    void commit(const fla::states::Graph& graph, const fla::states::Edit& edit, const QString& name);
    void pick(Picked kind, int index);
    void showEditor();

    void addState();
    void addTransition(int from, int to);
    void addInput();
    void addListener();
    void removePicked();
    void updateScripts();

    void fillTargets(QComboBox* combo, const std::string& current);
    void fillInputs(QComboBox* combo, const std::string& current, bool none);
    void fillCompare(const fla::states::Input* input, fla::states::Compare current);
    void fillChanges(const fla::states::Input* input, fla::states::Change current);

    QWidget* buildStateEditor();
    QWidget* buildTransitionEditor();
    QWidget* buildInputEditor();
    QWidget* buildListenerEditor();
    void stateEdited();
    void transitionEdited();
    void inputEdited();
    void listenerEdited();

    StatesLinks _links;
    fla::states::Graph _graph;
    const fla::Timeline* _shown = nullptr;
    Picked _kind = Picked::None;
    int _index = -1;
    bool _refreshing = false;

    QLabel* _title;
    QWidget* _notice;
    QLabel* _noticeText;
    StatesView* _view;
    QPushButton* _addTransition;
    QPushButton* _remove;
    QListWidget* _inputs;
    QListWidget* _listeners;
    QStackedWidget* _editors;

    // The state editor.
    QLineEdit* _stateName;
    QComboBox* _statePlay;
    QComboBox* _stateUntil;
    QCheckBox* _stateEntry;
    QLabel* _stateFrames;

    // The transition editor.
    QComboBox* _from;
    QComboBox* _to;
    QComboBox* _when;
    QFormLayout* _transitionForm;
    QWidget* _targetRow;
    QComboBox* _target;
    QComboBox* _key;
    QDoubleSpinBox* _seconds;
    QComboBox* _conditionInput;
    QComboBox* _compare;
    QDoubleSpinBox* _compareValue;
    QCheckBox* _afterFinish;

    // The input editor.
    QLineEdit* _inputName;
    QComboBox* _inputKind;
    QComboBox* _inputSwitch;
    QDoubleSpinBox* _inputNumber;
    QFormLayout* _inputForm;

    // The listener editor.
    QComboBox* _listenerWhen;
    QWidget* _listenerTargetRow;
    QComboBox* _listenerTarget;
    QComboBox* _listenerKey;
    QComboBox* _listenerInput;
    QComboBox* _listenerChange;
    QDoubleSpinBox* _listenerValue;
    QFormLayout* _listenerForm;
};
