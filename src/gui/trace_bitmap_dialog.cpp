#include "trace_bitmap_dialog.h"

#include "hot_text.h"

#include "../data/edge.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../geom/fill_tracing.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace {

/// Traces \a pixels and turns the artwork into what the preview draws.
std::shared_ptr<const TraceResult> traceForPreview(const std::vector<uint8_t>& pixels, int width, int height,
    const fla::trace::Options& options)
{
    auto result = std::make_shared<TraceResult>();
    std::unique_ptr<fla::Shape> shape(fla::trace::traceBitmap(pixels.data(), width, height, options));
    if (!shape)
        return result;
    const auto pathPoints = [](const fla::Path* path) {
        std::vector<QPointF> points;
        for (const fla::PathSegment* segment : path->segments)
            if (!segment->points.empty())
                points.emplace_back(segment->points.back().x, segment->points.back().y);
        return points;
    };
    for (const auto& [fill, loops] : fla::traceFills(*shape))
    {
        const auto* solid = dynamic_cast<const fla::SolidColor*>(shape->fillsMap.count(fill) ? shape->fillsMap[fill] : nullptr);
        if (!solid)
            continue;
        QPainterPath area;
        area.setFillRule(Qt::OddEvenFill);
        for (const fla::TracedLoop& loop : loops)
        {
            bool started = false;
            for (const fla::TracedLeg& leg : loop.legs)
            {
                std::vector<QPointF> points = pathPoints(leg.path);
                if (leg.reversed)
                    std::reverse(points.begin(), points.end());
                for (const QPointF& point : points)
                {
                    if (!started)
                        area.moveTo(point);
                    else
                        area.lineTo(point);
                    started = true;
                }
            }
            if (started)
                area.closeSubpath();
        }
        result->areas.emplace_back(area, QColor(solid->color[0], solid->color[1], solid->color[2]));
    }
    for (const fla::Edge* edge : shape->edges)
        for (const fla::Path* path : edge->paths)
        {
            const std::vector<QPointF> points = pathPoints(path);
            result->points += static_cast<int>(points.size());
            for (size_t i = 0; i < points.size(); ++i)
            {
                if (i == 0)
                    result->outlines.moveTo(points[i]);
                else
                    result->outlines.lineTo(points[i]);
            }
        }
    return result;
}

/// A color of its own for area \a index, far from its neighbors' in hue.
QColor areaColor(size_t index)
{
    return QColor::fromHsv(static_cast<int>((index * 137) % 360), 110 + static_cast<int>(index * 53 % 120),
        200 + static_cast<int>(index * 31 % 56));
}

} // namespace

TracePreview::TracePreview(const QImage& image, QWidget* parent)
    : QWidget(parent)
    , _image(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
{
    setMinimumSize(240, 200);
    setCursor(Qt::OpenHandCursor);
    setToolTip("Scroll to zoom, drag to move, double-click to fit");
}

void TracePreview::setResult(std::shared_ptr<const TraceResult> result)
{
    _result = std::move(result);
    changed();
}

void TracePreview::setShow(Show show)
{
    _show = show;
    changed();
}

void TracePreview::setOutlines(bool on)
{
    _outlines = on;
    changed();
}

void TracePreview::changed()
{
    _cache = QImage();
    update();
}

void TracePreview::fit()
{
    if (_image.isNull())
        return;
    const double margin = 8.0;
    _zoom = std::min((width() - 2 * margin) / _image.width(), (height() - 2 * margin) / _image.height());
    _zoom = std::max(_zoom, 0.01);
    _offset = QPointF((width() - _image.width() * _zoom) / 2.0, (height() - _image.height() * _zoom) / 2.0);
    _fitted = true;
    changed();
}

void TracePreview::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (_fitted)
        fit();
    else
        changed();
}

void TracePreview::wheelEvent(QWheelEvent* event)
{
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const double zoom = std::clamp(_zoom * factor, 0.01, 64.0);
    const QPointF at = event->position();
    _offset = at - (at - _offset) * (zoom / _zoom);
    _zoom = zoom;
    _fitted = false;
    changed();
}

void TracePreview::mousePressEvent(QMouseEvent* event)
{
    _dragFrom = event->pos();
    setCursor(Qt::ClosedHandCursor);
}

void TracePreview::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    _offset += event->pos() - _dragFrom;
    _dragFrom = event->pos();
    _fitted = false;
    changed();
}

void TracePreview::mouseDoubleClickEvent(QMouseEvent*)
{
    fit();
}

void TracePreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    if (_image.isNull())
        return;
    const QRectF bounds(_offset, QSizeF(_image.width() * _zoom, _image.height() * _zoom));
    // A checkerboard behind, where the picture is clear.
    painter.save();
    painter.setClipRect(bounds);
    const int cell = 8;
    for (int y = 0; y * cell < height(); ++y)
        for (int x = 0; x * cell < width(); ++x)
            painter.fillRect(x * cell, y * cell, cell, cell, (x + y) % 2 ? QColor(204, 204, 204) : QColor(255, 255, 255));
    painter.restore();

    QTransform transform;
    transform.translate(_offset.x(), _offset.y());
    transform.scale(_zoom, _zoom);
    if (_show == Show::Original || !_result)
    {
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, _zoom < 2.0);
        painter.setTransform(transform);
        painter.drawImage(0, 0, _image);
        painter.restore();
    }
    else
    {
        if (_cache.size() != size() * devicePixelRatioF())
        {
            _cache = QImage(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
            _cache.setDevicePixelRatio(devicePixelRatioF());
            _cache.fill(Qt::transparent);
            QPainter cache(&_cache);
            cache.setRenderHint(QPainter::Antialiasing);
            cache.setTransform(transform);
            cache.setPen(Qt::NoPen);
            for (size_t i = 0; i < _result->areas.size(); ++i)
            {
                const auto& [area, color] = _result->areas[i];
                cache.setBrush(_show == Show::Areas ? areaColor(i) : color);
                cache.drawPath(area);
            }
        }
        painter.drawImage(0, 0, _cache);
    }
    if (_outlines && _result && _show != Show::Original)
    {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setTransform(transform);
        QPen pen(QColor(255, 0, 140, 220), 1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(_result->outlines);
    }
}

TraceBitmapDialog::TraceBitmapDialog(const QImage& image, int count, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Trace Bitmap");
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    _width = rgba.width();
    _height = rgba.height();
    auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(_width) * _height * 4);
    for (int y = 0; y < _height; ++y)
        std::memcpy(pixels->data() + static_cast<size_t>(y) * _width * 4, rgba.constScanLine(y),
            static_cast<size_t>(_width) * 4);
    _pixels = pixels;

    QHBoxLayout* layout = new QHBoxLayout(this);
    QVBoxLayout* left = new QVBoxLayout;
    layout->addLayout(left, 1);
    _preview = new TracePreview(image, this);
    left->addWidget(_preview, 1);
    QHBoxLayout* shows = new QHBoxLayout;
    left->addLayout(shows);
    const std::tuple<const char*, TracePreview::Show, const char*> showings[] = {
        {"Traced", TracePreview::Show::Traced, "The artwork the trace makes"},
        {"Areas", TracePreview::Show::Areas, "Each area in a color of its own, to see which pixels were put together"},
        {"Original", TracePreview::Show::Original, "The bitmap as it is"}};
    std::vector<QPushButton*> showButtons;
    for (const auto& [label, show, tip] : showings)
    {
        QPushButton* button = new QPushButton(label, this);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setToolTip(tip);
        button->setChecked(show == TracePreview::Show::Traced);
        const TracePreview::Show which = show;
        connect(button, &QPushButton::toggled, this, [this, which](bool on) {
            if (on)
                _preview->setShow(which);
        });
        shows->addWidget(button);
        showButtons.push_back(button);
    }
    QPushButton* outlines = new QPushButton("Outlines", this);
    outlines->setCheckable(true);
    outlines->setToolTip("Draw the traced edges over the artwork");
    connect(outlines, &QPushButton::toggled, _preview, &TracePreview::setOutlines);
    shows->addSpacing(12);
    shows->addWidget(outlines);
    shows->addStretch(1);

    QVBoxLayout* right = new QVBoxLayout;
    layout->addLayout(right);
    QFormLayout* form = new QFormLayout;
    right->addLayout(form);
    QSettings settings;
    _threshold = new HotSpinBox(this);
    _threshold->setRange(1, 500);
    _threshold->setValue(settings.value("trace/threshold", 100).toInt());
    _threshold->setToolTip("How different two colors may be and still make one area; higher makes fewer areas");
    form->addRow("Color threshold", _threshold);
    _area = new HotSpinBox(this);
    _area->setRange(1, 1000);
    _area->setSuffix(" px");
    _area->setValue(settings.value("trace/minimumArea", 8).toInt());
    _area->setToolTip("Areas smaller than this join their neighbors");
    form->addRow("Minimum area", _area);
    _fit = new QComboBox(this);
    for (const fla::trace::CurveFit& fit : fla::trace::kCurveFits)
        _fit->addItem(fit.label, fit.tolerance);
    _fit->setCurrentIndex(std::clamp(settings.value("trace/curveFit", 3).toInt(), 0, 5));
    _fit->setToolTip("How closely the outlines follow the pixels; smoother outlines have fewer points");
    form->addRow("Curve fit", _fit);

    _status = new QLabel(this);
    right->addSpacing(8);
    right->addWidget(_status);
    _advice = new QLabel(this);
    _advice->setWordWrap(true);
    _advice->setMaximumWidth(260);
    _advice->setForegroundRole(QPalette::PlaceholderText);
    right->addWidget(_advice);
    if (count > 1)
    {
        QLabel* first = new QLabel(QString("Shows the first of the %1 bitmaps selected; all are traced alike.").arg(count), this);
        first->setWordWrap(true);
        first->setMaximumWidth(260);
        first->setForegroundRole(QPalette::PlaceholderText);
        right->addWidget(first);
    }
    right->addStretch(1);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Trace");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings settings;
        settings.setValue("trace/threshold", _threshold->value());
        settings.setValue("trace/minimumArea", _area->value());
        settings.setValue("trace/curveFit", _fit->currentIndex());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(buttons);

    // Traced again a moment after the settings stop changing, so dragging a
    // value does not queue up a trace for every step.
    _wait = new QTimer(this);
    _wait->setSingleShot(true);
    _wait->setInterval(120);
    connect(_wait, &QTimer::timeout, this, &TraceBitmapDialog::start);
    const auto settingsChanged = [this] {
        ++_generation;
        _wait->start();
    };
    connect(_threshold, &HotSpinBox::valueChanged, this, settingsChanged);
    connect(_area, &HotSpinBox::valueChanged, this, settingsChanged);
    connect(_fit, &QComboBox::currentIndexChanged, this, settingsChanged);
    resize(900, 560);
    start();
}

fla::trace::Options TraceBitmapDialog::options() const
{
    fla::trace::Options options;
    options.colorThreshold = _threshold->value();
    options.minimumArea = _area->value();
    options.curveFit = _fit->currentData().toDouble();
    return options;
}

void TraceBitmapDialog::start()
{
    if (_busy)
        return;
    _busy = true;
    _status->setText("Tracing...");
    const int generation = _generation;
    QPointer<TraceBitmapDialog> self(this);
    std::thread([self, generation, pixels = _pixels, width = _width, height = _height, options = options()] {
        std::shared_ptr<const TraceResult> result = traceForPreview(*pixels, width, height, options);
        QMetaObject::invokeMethod(qApp, [self, generation, result] {
            if (self)
                self->finished(generation, result);
        }, Qt::QueuedConnection);
    }).detach();
}

void TraceBitmapDialog::finished(int generation, std::shared_ptr<const TraceResult> result)
{
    _busy = false;
    if (generation != _generation)
    {
        // The settings changed while it traced: trace again with them,
        // unless the wait for them to settle will.
        if (!_wait->isActive())
            start();
        return;
    }
    const QLocale locale;
    const int areas = static_cast<int>(result->areas.size());
    _status->setText(QString("%1 %2, %3 points")
                         .arg(locale.toString(areas), areas == 1 ? "area" : "areas", locale.toString(result->points)));
    // Some advice from the count against the picture's size.
    const double pixelsPerArea = areas > 0 ? static_cast<double>(_width) * _height / areas : 0.0;
    if (areas == 0)
        _advice->setText("Nothing to trace: the bitmap is clear.");
    else if (areas > 4000 || pixelsPerArea < 60.0)
        _advice->setText("Many small areas, which is slow to draw and edit. Raise Color threshold to merge shades, or "
                         "Minimum area to fold specks into their neighbors.");
    else if (areas < 4 && _width * _height > 2500)
        _advice->setText("Very few areas: colors that should stay apart may have run together. Lower Color threshold.");
    else
        _advice->setText("Areas shows where each one ends. Outlines shows how many points the shapes have.");
    _preview->setResult(std::move(result));
}
