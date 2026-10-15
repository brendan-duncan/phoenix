#pragma once

#include <QDialog>
#include <QPointF>
#include <QWidget>

#include <vector>

class QComboBox;

/// The curve of a custom ease: a chain of cubic beziers from (0, 0) to
/// (1, 1), its points stored as the FLA's CustomEase keeps them (the start,
/// then two handles and an anchor per segment). Anchors and handles drag;
/// a double-click on the curve adds an anchor there; Delete takes the
/// picked anchor away.
class EaseCurveEditor : public QWidget
{
    Q_OBJECT

public:
    explicit EaseCurveEditor(QWidget* parent = nullptr);

    void setPoints(const std::vector<QPointF>& points);
    const std::vector<QPointF>& points() const { return _points; }

    /// A straight line, no ease.
    static std::vector<QPointF> linear();

    QSize sizeHint() const override { return QSize(360, 360); }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRectF plot() const;
    QPointF toWidget(const QPointF& point) const;
    QPointF toCurve(const QPointF& widget) const;
    int pointAt(const QPointF& widget) const;
    QPointF bezier(size_t segment, double t) const;

    std::vector<QPointF> _points;
    int _dragging = -1;
    int _picked = -1;
};

/// Edit Ease: the curve, and named presets kept between sessions and
/// shared as files.
class EaseCurveDialog : public QDialog
{
    Q_OBJECT

public:
    EaseCurveDialog(const std::vector<QPointF>& points, QWidget* parent = nullptr);

    std::vector<QPointF> points() const;
    /// The preset picked or saved, if any, as the ease's name.
    QString presetName() const;

private:
    void loadPresets();
    void savePresets();

    EaseCurveEditor* _editor = nullptr;
    QComboBox* _presets = nullptr;
    QString _presetName;
};
