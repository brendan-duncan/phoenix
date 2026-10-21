#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QSlider;
class QTreeWidget;
class JoystickPad;

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

/// The Controllers panel: master controllers. Each controller is a slider
/// or a joystick (a handle in a square, its across and down each driving
/// properties); Bind adds the current layer's property to what it drives,
/// between the values it has now and the ones typed. Moving the slider or
/// the handle sets them all on the frame shown, keying each layer there,
/// as one undo step.
class ControllersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControllersPanel(ControllersLinks links, QWidget* parent = nullptr);

    void refresh();

private:
    int currentController() const;
    void addController(bool joystick);
    void bind();
    void removeSelected();
    void moved(int position);
    /// Sets the current controller to \a x, \a y on the frame shown.
    void moveTo(double x, double y);
    /// The press and release that make a drag one undo step.
    void startDrag();
    void endDrag(double x, double y);

    ControllersLinks _links;
    QTreeWidget* _list;
    QWidget* _sliderRow;
    QSlider* _slider;
    QLabel* _value;
    JoystickPad* _pad;
    bool _dragging = false;
    bool _refreshing = false;
};
