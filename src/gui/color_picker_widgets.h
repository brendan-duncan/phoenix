#pragma once

#include "../edit/color_history.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QWidget>

/// The colors drawn with lately, shared by the Color panel and the pop-up
/// palette, and kept between runs.
class RecentColors : public QObject
{
    Q_OBJECT

public:
    static RecentColors& instance();

    const std::vector<fla::ColorHistory::Color>& colors() const { return _history.colors(); }
    QList<QColor> qcolors() const;
    /// Puts \a color first and saves the list.
    void add(const QColor& color);
    void add(const uint8_t rgba[4]);
    void clear();

signals:
    void changed();

private:
    RecentColors();
    void save();

    fla::ColorHistory _history;
};

/// A hue ring around a triangle of that hue's saturation and brightness:
/// drag around the ring for the hue, in the triangle for how strong and how
/// light. The triangle's tip is the pure hue, its other corners white and
/// black.
class HsvWheel : public QWidget
{
    Q_OBJECT

public:
    explicit HsvWheel(QWidget* parent = nullptr);

    /// Shows \a color; the hue of a gray is kept as it was.
    void setColor(const QColor& color);
    QColor color() const;

    QSize sizeHint() const override { return QSize(150, 150); }
    QSize minimumSizeHint() const override { return QSize(90, 90); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return width; }

signals:
    /// The color picked; \a done is false while dragging.
    void colorChanged(const QColor& color, bool done);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class Part
    {
        None,
        Ring,
        Triangle
    };

    QPointF center() const;
    double outerRadius() const;
    double innerRadius() const;
    /// The triangle's corners: the hue, white, black.
    void corners(QPointF& hue, QPointF& white, QPointF& black) const;
    void pick(const QPointF& point, bool done);

    fla::Hsv _hsv;
    int _alpha = 255;
    Part _dragging = Part::None;
    QImage _triangle;
    double _triangleHue = -1.0;
};

/// A row of small swatches: the recent colors. Click one to use it.
class RecentColorsBar : public QWidget
{
    Q_OBJECT

public:
    explicit RecentColorsBar(QWidget* parent = nullptr);

    /// How many swatches fit in a row before it wraps.
    void setColumns(int columns);

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

signals:
    void colorPicked(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect swatchRect(int index, int width) const;
    int swatchAt(const QPoint& point) const;
    int columnsFor(int width) const;

    int _columns = 0;
    int _hover = -1;
};
