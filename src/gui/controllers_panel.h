#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QSlider;
class QTreeWidget;

namespace fla {
class CommandStack;
class Document;
class Layer;
class Timeline;
}

struct ControllersLinks
{
    std::function<fla::CommandStack*()> commands;
    std::function<fla::Document*()> document;
    std::function<int()> currentFrame;
    /// The layer picked in the timeline, and the timeline it is on.
    std::function<fla::Layer*(fla::Timeline*&)> currentLayer;
    std::function<void()> changed;
};

/// The Controllers panel: master controllers. Each controller is
/// a slider; Bind adds the current layer's property to what it drives,
/// between the values it has now and the ones typed. Moving the slider
/// sets them all on the frame shown, keying each layer there, as one undo
/// step.
class ControllersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControllersPanel(ControllersLinks links, QWidget* parent = nullptr);

    void refresh();

private:
    int currentController() const;
    void addController();
    void bind();
    void removeSelected();
    void moved(int position);

    ControllersLinks _links;
    QTreeWidget* _list;
    QSlider* _slider;
    QLabel* _value;
    bool _dragging = false;
    bool _refreshing = false;
};
