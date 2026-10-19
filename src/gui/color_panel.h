#pragma once

#include "../edit/drawing_style.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class HotSpinBox;
class HsvWheel;
class RecentColorsBar;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace fla {
class FLADocument;
}

/// The bar of a gradient with its stops under it, as the Color panel
/// shows one: click the bar to add a stop, drag a stop to move it, drag it
/// off downward to take it away, double-click it for its color.
class GradientBar : public QWidget
{
    Q_OBJECT

public:
    explicit GradientBar(QWidget* parent = nullptr);

    void setStops(const std::vector<fla::DrawingStyle::Stop>& stops);
    const std::vector<fla::DrawingStyle::Stop>& stops() const { return _stops; }

    /// The stop picked, whose color the panel edits.
    int current() const { return _current; }
    void setCurrentColor(const QColor& color);

    QSize sizeHint() const override { return QSize(240, 44); }

signals:
    /// The stops changed; \a done is false while a stop is being dragged.
    void stopsChanged(bool done);
    void currentChanged(int index);
    void colorRequested(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRectF barRect() const;
    int stopAt(const QPointF& point) const;
    QColor colorAt(double ratio) const;

    std::vector<fla::DrawingStyle::Stop> _stops;
    int _current = 0;
    bool _dragging = false;
    bool _removing = false;
};

/// The Color panel, for what new art is filled with: a solid color,
/// a linear or radial gradient with its stops, flow and blending, or a
/// library bitmap; and Apply to Selection, which fills the selected shapes
/// with it. Under them, a color wheel and the colors drawn with lately,
/// for the fill or the stroke.
class ColorPanel : public QWidget
{
    Q_OBJECT

public:
    ColorPanel(fla::DrawingStyle* style, QWidget* parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    /// Re-reads the style and the library's bitmaps.
    void refresh();

signals:
    /// The style changed: the tool swatches and the like follow.
    void styleChanged();
    void applyToSelectionRequested();
    /// The selected shapes' strokes drawn with it: a gradient along a line.
    void applyToStrokesRequested();
    void importBitmapRequested();
    /// A color picked from the wheel or the recent colors for the fill or
    /// the stroke (\a stroke), done being dragged: the selection takes it.
    void colorChosen(bool stroke);

private:
    void showKind();
    void updateStopColor();
    void setStopColor(const QColor& color);
    void refreshBitmaps();
    /// The channel fields shown as the solid color is: red, green and blue,
    /// or hue, saturation and brightness, and alpha.
    void updateChannels();
    /// The wheel and recent colors: what they edit, the fill or stroke.
    void setupWheel(QVBoxLayout* layout);
    void updateWheel();
    void pickColor(const QColor& color, bool done);

    fla::DrawingStyle* _style;
    const fla::FLADocument* _document = nullptr;
    bool _updating = false;

    QComboBox* _kind = nullptr;
    QStackedWidget* _pages = nullptr;
    QToolButton* _solidSwatch = nullptr;
    QLineEdit* _solidHex = nullptr;
    QComboBox* _channelMode = nullptr;
    QLabel* _channelLabels[3] = {nullptr, nullptr, nullptr};
    HotSpinBox* _channels[3] = {nullptr, nullptr, nullptr};
    HotSpinBox* _alpha = nullptr;
    GradientBar* _bar = nullptr;
    QToolButton* _stopSwatch = nullptr;
    QLineEdit* _stopHex = nullptr;
    QComboBox* _flow = nullptr;
    QCheckBox* _linearRGB = nullptr;
    QListWidget* _bitmaps = nullptr;
    HsvWheel* _wheel = nullptr;
    RecentColorsBar* _recentColors = nullptr;
    QToolButton* _wheelFill = nullptr;
    QToolButton* _wheelStroke = nullptr;
    bool _wheelEditsStroke = false;
    bool _keepWheel = false;
};
