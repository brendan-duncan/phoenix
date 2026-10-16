#pragma once

#include "../edit/behaviors.h"

#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QFormLayout;
class QLabel;
class QListWidget;
class QToolButton;

namespace fla {
class CommandStack;
class DOMElement;
class Layer;
class Shape;
class StaticText;
class SymbolInstance;
}

struct BehaviorsLinks
{
    std::function<fla::CommandStack*()> commands;
    /// The symbol instances selected (unlocked ones).
    std::function<std::vector<fla::SymbolInstance*>()> instances;
    /// The drawn shapes selected (unlocked ones), for their operators when
    /// no instance is selected.
    std::function<std::vector<fla::Shape*>()> shapes;
    /// The text fields selected, for their letter animators when no
    /// instance or shape is selected.
    std::function<std::vector<fla::StaticText*>()> texts;
    /// The current layer when it is an adjustment layer, for its effects
    /// when nothing on the stage is selected.
    std::function<fla::Layer*()> adjustmentLayer;
    /// After a change: the stage drawn again.
    std::function<void()> changed;
    /// The frame shown, on the timeline being edited: where keys go.
    std::function<int()> currentFrame;
    /// Bake to Keyframes for the selected instances.
    std::function<void()> bake;
    /// Opens a behavior file to edit.
    std::function<void(const QString& path)> openScript;
    /// The names of the instances on the timeline being edited, and of the
    /// master controllers, for settings that name one.
    std::function<QStringList()> objectNames;
    /// The next instance clicked on the stage, by its name (one is given
    /// it when it has none), for \a done.
    std::function<void(std::function<void(const QString&)> done)> pickObject;
    std::function<QStringList()> controllerNames;
};

/// The Behaviors panel: the behaviors on the selected symbol instances.
/// Add one from the list (Wiggle, Oscillate, Follow with Lag...), turn it
/// on or off, put it before or after another, take it away, and set what
/// it offers with sliders, boxes and lists; every change is a step to undo,
/// and a slider dragged is one step. With several instances selected, a
/// behavior added goes on each, and a setting changed changes on each that
/// has that behavior in that place. A selected instance's effects (the
/// adjustment layers' blurs, glows, distortions, color and wipes, on its
/// own picture) are on the panel's Effects side, worked the same way.
class BehaviorsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit BehaviorsPanel(BehaviorsLinks links, QWidget* parent = nullptr);

    /// Shows the selection's behaviors again.
    void refresh();

private:
    /// The first selected instance's list, which the panel shows.
    std::vector<fla::behaviors::Applied> shown() const;
    /// Whether it shows shapes' operators (shapes selected and no
    /// instance), the elements it changes, the attribute their list is
    /// kept in, and what an id names.
    enum class Mode
    {
        Behaviors,
        Shapes,
        Texts,
        Adjustments,
        InstanceEffects
    };
    Mode mode() const;
    bool shapeMode() const;
    std::vector<fla::behaviors::Definition> available() const;
    std::vector<fla::DOMElement*> targets() const;
    const char* attribute() const;
    const fla::behaviors::Definition* definitionOf(const std::string& id) const;
    void fillSettings();
    /// Changes each selected instance's list with \a change, as one step
    /// named \a name; \a merge folds a run of the same change together.
    void edit(const std::string& name, const std::function<void(std::vector<fla::behaviors::Applied>&)>& change,
        bool merge = false);
    void add(const std::string& id);
    /// Every behavior (or letter animator) and some made together, each
    /// seen moving on a sample, to add to the selection.
    void browsePresets();
    void move(int by);
    void remove();
    /// A behavior file of one's own, from an example, opened to edit.
    void newBehavior();
    void setParam(int row, const std::string& name, const std::string& value, bool merge);
    /// Keys the setting on the frame shown, or takes its key there away.
    void toggleKey(int row, const std::string& name);
    int frame() const;

    BehaviorsLinks _links;
    QLabel* _what;
    /// For instances: their behaviors, or their effects.
    QWidget* _sides;
    QToolButton* _behaviorsSide;
    QToolButton* _effectsSide;
    bool _effectsShown = false;
    QListWidget* _list;
    QToolButton* _add;
    QToolButton* _up;
    QToolButton* _down;
    QToolButton* _remove;
    QToolButton* _bake;
    QLabel* _description;
    QWidget* _settings;
    QFormLayout* _form;
    bool _refreshing = false;
};
