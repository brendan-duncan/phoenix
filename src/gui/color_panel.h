#pragma once

#include "../edit/drawing_style.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace fla {
class FLADocument;
}

/// The bar of a gradient with its stops under it, as Animate's Color panel
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

/// Animate's Color panel, for what new art is filled with: a solid color,
/// a linear or radial gradient with its stops, flow and blending, or a
/// library bitmap; and Apply to Selection, which fills the selected shapes
/// with it.
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
    void importBitmapRequested();

private:
    void showKind();
    void updateStopColor();
    void setStopColor(const QColor& color);
    void refreshBitmaps();

    fla::DrawingStyle* _style;
    const fla::FLADocument* _document = nullptr;
    bool _updating = false;

    QComboBox* _kind = nullptr;
    QStackedWidget* _pages = nullptr;
    QToolButton* _solidSwatch = nullptr;
    QLineEdit* _solidHex = nullptr;
    GradientBar* _bar = nullptr;
    QToolButton* _stopSwatch = nullptr;
    QLineEdit* _stopHex = nullptr;
    QComboBox* _flow = nullptr;
    QCheckBox* _linearRGB = nullptr;
    QListWidget* _bitmaps = nullptr;
};
