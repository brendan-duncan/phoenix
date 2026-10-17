#pragma once

#include <QAbstractButton>
#include <QBrush>
#include <QColor>
#include <QWidget>

class QToolButton;

/// One of the color well's two swatches: the fill a solid square, the
/// stroke a square ring around a hollow middle.
class ColorSwatch : public QAbstractButton
{
    Q_OBJECT

public:
    ColorSwatch(bool stroke, QWidget* parent = nullptr);

    /// What it shows: a color or gradient, or none (Qt::NoBrush), which
    /// is crossed out in red.
    void setBrush(const QBrush& brush);
    const QBrush& brush() const { return _brush; }
    bool isStroke() const { return _stroke; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool _stroke;
    QBrush _brush;
};

/// The stroke and fill colors in the Tools panel as one unit: two
/// overlapping swatches, the one last clicked on top, with Black and White
/// and Swap in the room they leave. One column wide, the buttons sit in a
/// row above the swatches; broader, the swatches grow and the buttons take
/// the corners.
class ColorWell : public QWidget
{
    Q_OBJECT

public:
    explicit ColorWell(QWidget* parent = nullptr);

    void setStroke(const QBrush& brush);
    void setFill(const QBrush& brush);

    /// Whether the stroke is the swatch on top.
    bool strokeSelected() const { return _strokeSelected; }
    void setStrokeSelected(bool stroke);

    ColorSwatch* strokeSwatch() const { return _strokeSwatch; }
    ColorSwatch* fillSwatch() const { return _fillSwatch; }

    /// The length along a panel's flow the well takes for \a breadth
    /// across it.
    static int lengthFor(int breadth, Qt::Orientation flow);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

signals:
    /// A swatch was clicked: it is now on top.
    void strokeClicked();
    void fillClicked();
    void blackWhiteClicked();
    void swapClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void arrange();

    ColorSwatch* _strokeSwatch;
    ColorSwatch* _fillSwatch;
    QToolButton* _blackWhite;
    QToolButton* _swap;
    bool _strokeSelected = false;
};
