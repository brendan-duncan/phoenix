#pragma once

#include <QWidget>

#include <functional>

class PhoenixView;
class QAction;
class HotDoubleSpinBox;
class QToolButton;

/// The view controls at the right of the edit bar: Center Stage, the
/// Rotation tool (showing the angle while the stage is turned, and setting
/// it straight when clicked then), what happens to artwork off the stage
/// (shown, dimmed or clipped), the safe zones shown or not, and a zoom box
/// that follows every other way of zooming.
class StageControls : public QWidget
{
    Q_OBJECT

public:
    /// \a clip and \a dim are the View menu's Clip Content Outside Stage and
    /// Dim Outside Document, which the clip button sets and follows;
    /// \a pickRotationTool makes the Rotation tool the current one;
    /// \a safeZones is View > Safe Zones, which a button toggles.
    StageControls(PhoenixView* view, QAction* clip, QAction* dim, std::function<void()> pickRotationTool,
        QAction* safeZones = nullptr, QWidget* parent = nullptr);

    /// Shows the view's zoom and rotation as they are now.
    void refresh();

protected:
    void changeEvent(QEvent* event) override;

private:
    void showClipMode();
    void showRotation(double degrees);
    void drawIcons();

    PhoenixView* _view;
    QAction* _clip;
    QAction* _dim;
    QAction* _safeZones = nullptr;
    QToolButton* _center = nullptr;
    QToolButton* _rotation = nullptr;
    QToolButton* _clipButton = nullptr;
    QToolButton* _safeZonesButton = nullptr;
    HotDoubleSpinBox* _zoom = nullptr;
    QToolButton* _zoomMenu = nullptr;
};
