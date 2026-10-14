#include "stage_controls.h"

#include "phoenix_view.h"
#include "tool_icons.h"

#include <QAction>
#include <QActionGroup>
#include "hot_text.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace {

/// The magnification presets the zoom box's arrows step through, as
/// fractions.
const double kPresets[] = {0.08, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};

/// A percentage field whose Up and Down keys (and the wheel) go from preset
/// to preset rather than by one percent; dragging it still zooms freely.
class ZoomBox : public HotDoubleSpinBox
{
public:
    using HotDoubleSpinBox::HotDoubleSpinBox;

    void stepBy(int steps) override
    {
        double zoom = value() / 100.0;
        for (; steps > 0; --steps)
        {
            for (double preset : kPresets)
                if (preset > zoom * 1.001)
                {
                    zoom = preset;
                    break;
                }
        }
        for (; steps < 0; ++steps)
        {
            double below = zoom;
            for (double preset : kPresets)
                if (preset < zoom * 0.999)
                    below = preset;
            zoom = below;
        }
        setValue(zoom * 100.0);
    }
};

/// What the clip button shows: artwork off the stage shown, dimmed, or cut
/// away.
enum class ClipMode
{
    Show,
    Dim,
    Clip
};

/// A 16 pixel icon drawn in \a color: the stage as a rectangle, with a
/// disc reaching out of it drawn as \a mode leaves it.
QIcon clipIcon(ClipMode mode, const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF stage(2.5, 4.5, 9.0, 8.0);
    QPainterPath disc;
    disc.addEllipse(QPointF(11.0, 6.0), 4.0, 4.0);
    QPainterPath inside;
    inside.addRect(stage);
    painter.setPen(Qt::NoPen);
    if (mode != ClipMode::Clip)
    {
        QColor outside = color;
        if (mode == ClipMode::Dim)
            outside.setAlphaF(0.35);
        painter.setBrush(outside);
        painter.drawPath(disc.subtracted(inside));
    }
    painter.setBrush(color);
    painter.drawPath(disc.intersected(inside));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, 1.0));
    painter.drawRect(stage);
    return QIcon(pixmap);
}

/// Center Stage's icon: the stage with a mark at its middle.
QIcon centerIcon(const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.0));
    painter.drawRect(QRectF(1.5, 3.5, 13.0, 9.0));
    painter.drawLine(QPointF(8.0, 5.5), QPointF(8.0, 10.5));
    painter.drawLine(QPointF(5.5, 8.0), QPointF(10.5, 8.0));
    return QIcon(pixmap);
}

} // namespace

StageControls::StageControls(PhoenixView* view, QAction* clip, QAction* dim, std::function<void()> pickRotationTool,
    QWidget* parent)
    : QWidget(parent)
    , _view(view)
    , _clip(clip)
    , _dim(dim)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(1);

    _center = new QToolButton(this);
    _center->setObjectName("stageCenter");
    _center->setAutoRaise(true);
    _center->setToolTip("Center Stage: bring the stage to the middle of the view");
    connect(_center, &QToolButton::clicked, _view, &PhoenixView::centerStage);
    layout->addWidget(_center);

    _rotation = new QToolButton(this);
    _rotation->setObjectName("stageRotation");
    _rotation->setAutoRaise(true);
    _rotation->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(_rotation, &QToolButton::clicked, this, [this, pickRotationTool]() {
        // Turned, the button sets the stage straight; straight, it picks the
        // tool that turns it.
        if (_view->rotation() != 0.0)
            _view->setRotation(0.0);
        else if (pickRotationTool)
            pickRotationTool();
    });
    connect(_view, &PhoenixView::rotationChanged, this, &StageControls::showRotation);
    layout->addWidget(_rotation);

    _clipButton = new QToolButton(this);
    _clipButton->setObjectName("stageClip");
    _clipButton->setAutoRaise(true);
    _clipButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* clipMenu = new QMenu(_clipButton);
    QActionGroup* modes = new QActionGroup(clipMenu);
    const struct
    {
        const char* label;
        ClipMode mode;
    } choices[] = {
        {"Show Content Outside Stage", ClipMode::Show},
        {"Dim Content Outside Stage", ClipMode::Dim},
        {"Clip Content Outside Stage", ClipMode::Clip},
    };
    for (const auto& choice : choices)
    {
        QAction* action = clipMenu->addAction(choice.label);
        action->setCheckable(true);
        action->setData(static_cast<int>(choice.mode));
        modes->addAction(action);
        const ClipMode mode = choice.mode;
        connect(action, &QAction::triggered, this, [this, mode]() {
            if (_dim)
                _dim->setChecked(mode == ClipMode::Dim);
            if (_clip)
                _clip->setChecked(mode == ClipMode::Clip);
        });
    }
    connect(clipMenu, &QMenu::aboutToShow, this, [this, clipMenu]() {
        const ClipMode mode = _clip && _clip->isChecked() ? ClipMode::Clip
            : _dim && _dim->isChecked()                   ? ClipMode::Dim
                                                          : ClipMode::Show;
        for (QAction* action : clipMenu->actions())
            action->setChecked(action->data().toInt() == static_cast<int>(mode));
    });
    _clipButton->setMenu(clipMenu);
    if (_clip)
        connect(_clip, &QAction::toggled, this, &StageControls::showClipMode);
    if (_dim)
        connect(_dim, &QAction::toggled, this, &StageControls::showClipMode);
    layout->addWidget(_clipButton);

    _zoom = new ZoomBox(this);
    _zoom->setObjectName("stageZoom");
    _zoom->setRange(5.0, 10000.0);
    _zoom->setDecimals(0);
    _zoom->setSuffix("%");
    // Room for the widest percentage.
    _zoom->setFixedWidth(_zoom->fontMetrics().horizontalAdvance("10000%") + 16);
    _zoom->setToolTip("Zoom: drag to zoom, click to type a percentage, or step through the "
                      "magnifications with Up and Down (Ctrl+= and Ctrl+-)");
    connect(_zoom, &HotDoubleSpinBox::valueChanged, this, [this](double percent) {
        // Only what was typed or stepped: refresh() sets it silently.
        if (std::fabs(percent / 100.0 - _view->zoom()) > 0.004)
            _view->setZoomLevel(percent / 100.0);
    });
    layout->addWidget(_zoom);

    _zoomMenu = new QToolButton(this);
    _zoomMenu->setObjectName("stageZoomMenu");
    _zoomMenu->setAutoRaise(true);
    _zoomMenu->setPopupMode(QToolButton::InstantPopup);
    _zoomMenu->setArrowType(Qt::DownArrow);
    // The arrow is the button: no second one for its menu.
    _zoomMenu->setStyleSheet("QToolButton::menu-indicator { image: none; width: 0px; }");
    _zoomMenu->setToolTip("Magnification: Fit in Window, Show All, or a preset");
    QMenu* zoomMenu = new QMenu(_zoomMenu);
    zoomMenu->addAction("Fit in Window\tCtrl+2", _view, &PhoenixView::zoomToFit);
    zoomMenu->addAction("Show Frame", _view, &PhoenixView::zoomToFit);
    zoomMenu->addAction("Show All\tCtrl+3", _view, &PhoenixView::zoomToShowAll);
    zoomMenu->addSeparator();
    for (double preset : {0.25, 0.5, 1.0, 2.0, 4.0, 8.0})
        zoomMenu->addAction(QString("%1%").arg(preset * 100.0), this,
            [this, preset]() { _view->setZoomLevel(preset); });
    _zoomMenu->setMenu(zoomMenu);
    layout->addWidget(_zoomMenu);

    connect(_view, &PhoenixView::zoomChanged, this, &StageControls::refresh);

    drawIcons();
    showClipMode();
    refresh();
}

void StageControls::refresh()
{
    const QSignalBlocker quiet(_zoom);
    if (!_zoom->hasFocus())
        _zoom->setValue(_view->zoom() * 100.0);
    showRotation(_view->rotation());
}

void StageControls::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
    {
        drawIcons();
        showClipMode();
    }
    QWidget::changeEvent(event);
}

void StageControls::showClipMode()
{
    const QColor color = palette().color(QPalette::ButtonText);
    if (_clip && _clip->isChecked())
    {
        _clipButton->setIcon(clipIcon(ClipMode::Clip, color));
        _clipButton->setToolTip("Content outside the stage is clipped. Click to show or dim it instead");
    }
    else if (_dim && _dim->isChecked())
    {
        _clipButton->setIcon(clipIcon(ClipMode::Dim, color));
        _clipButton->setToolTip("Content outside the stage is dimmed. Click to show or clip it instead");
    }
    else
    {
        _clipButton->setIcon(clipIcon(ClipMode::Show, color));
        _clipButton->setToolTip("Content outside the stage is shown. Click to dim or clip it instead");
    }
}

void StageControls::showRotation(double degrees)
{
    if (degrees == 0.0)
    {
        _rotation->setText(QString());
        _rotation->setToolButtonStyle(Qt::ToolButtonIconOnly);
        _rotation->setToolTip("Rotation tool: turn the view of the stage (Shift+H)");
    }
    else
    {
        _rotation->setText(QString::fromUtf8("%1°").arg(std::round(degrees * 10.0) / 10.0));
        _rotation->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        _rotation->setToolTip("The stage is turned. Click to set it straight (View > Reset Rotation)");
    }
}

void StageControls::drawIcons()
{
    const QColor color = palette().color(QPalette::ButtonText);
    _center->setIcon(centerIcon(color));
    _rotation->setIcon(ToolIcons::icon(ToolIcons::Tool::Rotation));
}
