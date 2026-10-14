#pragma once

#include "../edit/trace_bitmap.h"

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QPainterPath>
#include <QWidget>

#include <memory>
#include <vector>

class HotSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

/// What a trace of the preview picture came to, drawn by TracePreview.
struct TraceResult
{
    /// Each area's outline, in its fill color.
    std::vector<std::pair<QPainterPath, QColor>> areas;
    /// Every edge of the traced artwork, for drawing over it.
    QPainterPath outlines;
    int points = 0;
};

/// The picture or its trace, fitted to the widget at first: the wheel zooms
/// about the cursor, a drag pans, a double-click fits it again.
class TracePreview : public QWidget
{
    Q_OBJECT

public:
    enum class Show
    {
        Traced,
        /// Each area in a color of its own, to see where they part.
        Areas,
        Original
    };

    explicit TracePreview(const QImage& image, QWidget* parent = nullptr);

    void setResult(std::shared_ptr<const TraceResult> result);
    void setShow(Show show);
    void setOutlines(bool on);
    QSize sizeHint() const override { return QSize(560, 460); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void fit();
    void changed();

    QImage _image;
    std::shared_ptr<const TraceResult> _result;
    Show _show = Show::Traced;
    bool _outlines = false;
    bool _fitted = true;
    double _zoom = 1.0;
    QPointF _offset;
    QPoint _dragFrom;
    /// The traced artwork drawn at the present zoom, as it is slow to draw
    /// with many areas.
    QImage _cache;
};

/// Modify > Bitmap > Trace Bitmap: the settings beside a preview of the
/// first bitmap selected, traced again as they change, with how many areas
/// and points it comes to.
class TraceBitmapDialog : public QDialog
{
    Q_OBJECT

public:
    /// \a image is the bitmap previewed; \a count is how many are traced.
    TraceBitmapDialog(const QImage& image, int count, QWidget* parent = nullptr);

    fla::trace::Options options() const;

private:
    void start();
    void finished(int generation, std::shared_ptr<const TraceResult> result);

    std::shared_ptr<const std::vector<uint8_t>> _pixels;
    int _width = 0;
    int _height = 0;
    TracePreview* _preview = nullptr;
    HotSpinBox* _threshold = nullptr;
    HotSpinBox* _area = nullptr;
    QComboBox* _fit = nullptr;
    QLabel* _status = nullptr;
    QLabel* _advice = nullptr;
    QTimer* _wait = nullptr;
    /// Changed with each change of the settings; a trace done for an older
    /// one is not shown, and the latest is started once it is done.
    int _generation = 0;
    bool _busy = false;
};
