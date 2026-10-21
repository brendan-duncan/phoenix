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
/// frame labels are its states, each looping, playing once, holding or
/// following a number input; arrows between them are the transitions, set
/// off by a click on an instance, a key, the end of a state, a timer or an
/// input; listeners set the inputs. Layers add states for movie clips on
/// the timeline that play alongside. Every change is one undo step, and
/// turns the graph into ordinary ActionScript on the timeline's first
/// frame (see fla::states).
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
        Binding,
        Pin,
        Scroller,
    };

    fla::Timeline* timeline() const;
    /// The timeline whose labels the layer shown goes by: the edited one,
    /// or its layer's movie clip's.
    const fla::Timeline* layerTimeline(int layer) const;
    /// The layer state \a index is in.
    int layerOf(int index) const;
    /// The states of the layer shown, in order.
    std::vector<int> shownStates() const;
    void addLayer();
    void removeLayer();
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
    void addBinding();
    void addPin();
    void addScroller();
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
    QWidget* buildBindingEditor();
    void bindingEdited();
    QWidget* buildPinEditor();
    void pinEdited();
    QWidget* buildScrollerEditor();
    void scrollerEdited();
    /// A list of instance names to pick from, with Use Selected.
    QWidget* targetPicker(QWidget* parent, QComboBox*& combo, const std::function<void()>& edited);
    void fillBindable(QComboBox* combo, const std::string& current, bool clipsOnly);
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

    int _layerShown = 0;
    QComboBox* _layer;
    QPushButton* _removeLayer;
    QLabel* _title;
    QWidget* _notice;
    QLabel* _noticeText;
    StatesView* _view;
    QPushButton* _addTransition;
    QPushButton* _remove;
    QListWidget* _inputs;
    QListWidget* _listeners;
    QListWidget* _bindings;
    QListWidget* _pins;
    QListWidget* _scrollers;
    QStackedWidget* _editors;

    // The state editor.
    QLineEdit* _stateName;
    QComboBox* _statePlay;
    QComboBox* _stateUntil;
    QCheckBox* _stateEntry;
    QLabel* _stateFrames;
    QFormLayout* _stateForm;
    QWidget* _blendRow;
    QComboBox* _blendInput;
    QDoubleSpinBox* _blendLow;
    QDoubleSpinBox* _blendHigh;

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

    // The binding editor.
    QComboBox* _bindInput;
    QComboBox* _bindTarget;
    QComboBox* _bindProperty;
    QWidget* _bindRangeRow;
    QDoubleSpinBox* _bindLow;
    QDoubleSpinBox* _bindHigh;
    QWidget* _bindValuesRow;
    QDoubleSpinBox* _bindFrom;
    QDoubleSpinBox* _bindTo;
    QFormLayout* _bindForm;

    // The pin editor.
    QComboBox* _pinTarget;
    QCheckBox* _pinLeft;
    QCheckBox* _pinRight;
    QCheckBox* _pinTop;
    QCheckBox* _pinBottom;

    // The scroller editor.
    QComboBox* _scrollTarget;
    QDoubleSpinBox* _scrollWidth;
    QDoubleSpinBox* _scrollHeight;
    QComboBox* _scrollWay;
    QDoubleSpinBox* _scrollGlide;
};
