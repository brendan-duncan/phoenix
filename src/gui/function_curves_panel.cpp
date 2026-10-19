#include "function_curves_panel.h"
#include "behavior_library.h"
#include "../jsfl/behavior_frames.h"

#include "modal.h"
#include "theme.h"

#include "../data/layer.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/timeline_edit.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdlib>

/// The graph itself: frames across, the property's value up.
class FunctionGraph : public QWidget
{
public:
    explicit FunctionGraph(FunctionCurvesPanel* panel)
        : QWidget(panel)
        , _panel(panel)
    {
        setMinimumHeight(140);
        setMouseTracking(true);
    }

    /// The curve and keys, read again.
    void load()
    {
        _samples.clear();
        _driven.clear();
        _keys.clear();
        fla::Timeline* timeline = nullptr;
        fla::Layer* layer = _panel->_links.currentLayer ? _panel->_links.currentLayer(timeline) : nullptr;
        _key = _panel->_property->currentData().toString().toStdString();
        if (layer && !_key.empty())
        {
            const fla::curves::Context context = _panel->_links.context();
            _frames = std::max(1, fla::timeline::layerEnd(*layer));
            for (int frame = 0; frame < _frames; ++frame)
            {
                double value = 0.0;
                _samples.push_back(fla::curves::valueAt(*layer, frame, _key, context, value) ? value : std::nan(""));
                // What behaviors make of it, where they change it.
                double driven = 0.0;
                _driven.push_back(fla::behaviors::drivenValue(BehaviorLibrary::instance().engine(), *layer, frame, _key,
                                      context.frameRate, driven) && !std::isnan(value) && std::fabs(driven - value) > 1e-9
                        ? driven
                        : std::nan(""));
            }
            _keys = fla::curves::keys(*layer, _key, context);
        }
        fitRange();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), theme::color("graph.background"));
        const QRectF area = plotArea();
        painter.fillRect(area, theme::color("graph.area"));
        painter.setFont(QFont(font().family(), 8));
        // A line every so often each way, labeled.
        painter.setPen(QPen(theme::color("graph.grid"), 1.0));
        for (int i = 0; i <= 4; ++i)
        {
            const double value = _low + (_high - _low) * i / 4.0;
            const double y = yOf(value);
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            painter.setPen(theme::color("graph.text"));
            painter.drawText(QRectF(0, y - 8, area.left() - 4, 16), Qt::AlignRight | Qt::AlignVCenter,
                QString::number(value, 'g', 4));
            painter.setPen(QPen(theme::color("graph.grid"), 1.0));
        }
        const int step = _frames > 200 ? 50 : _frames > 60 ? 10 : 5;
        for (int frame = 0; frame < _frames; frame += step)
        {
            const double x = xOf(frame);
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            painter.setPen(theme::color("graph.text"));
            painter.drawText(QRectF(x - 20, area.bottom() + 2, 40, 14), Qt::AlignCenter, QString::number(frame + 1));
            painter.setPen(QPen(theme::color("graph.grid"), 1.0));
        }
        // The playhead.
        const int current = _panel->_links.currentFrame ? _panel->_links.currentFrame() : 0;
        painter.setPen(QPen(theme::color("playhead"), 1.0));
        painter.drawLine(QPointF(xOf(current), area.top()), QPointF(xOf(current), area.bottom()));

        if (_samples.empty())
        {
            painter.setPen(theme::color("graph.text"));
            painter.drawText(area, Qt::AlignCenter, "Pick a layer with symbol instances, or the camera");
            return;
        }
        // The curve, broken where the layer shows nothing.
        QPainterPath path;
        bool drawing = false;
        for (int frame = 0; frame < static_cast<int>(_samples.size()); ++frame)
        {
            const double value = _samples[static_cast<size_t>(frame)];
            if (std::isnan(value))
            {
                drawing = false;
                continue;
            }
            const QPointF point(xOf(frame), yOf(value));
            if (drawing)
                path.lineTo(point);
            else
                path.moveTo(point);
            drawing = true;
        }
        painter.setPen(QPen(theme::color("accent"), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        // With behaviors, dashed, where they change the value.
        QPainterPath driven;
        bool on = false;
        for (int frame = 0; frame < static_cast<int>(_driven.size()); ++frame)
        {
            const double value = _driven[static_cast<size_t>(frame)];
            if (std::isnan(value))
            {
                on = false;
                continue;
            }
            const QPointF point(xOf(frame), yOf(value));
            if (on)
                driven.lineTo(point);
            else
                driven.moveTo(point);
            on = true;
        }
        if (!driven.isEmpty())
        {
            painter.setPen(QPen(theme::color("accent"), 1.5, Qt::DashLine));
            painter.drawPath(driven);
        }
        // The keyframes.
        for (size_t i = 0; i < _keys.size(); ++i)
        {
            const double value = static_cast<int>(i) == _dragged ? _dragValue : _keys[i].value;
            const QPointF at(xOf(_keys[i].frame), yOf(value));
            painter.setPen(QPen(theme::color("text"), 1.5));
            painter.setBrush(static_cast<int>(i) == _dragged ? theme::color("playhead") : theme::color("graph.area"));
            painter.drawEllipse(at, 4.5, 4.5);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        _dragged = keyAt(event->position());
        if (_dragged >= 0)
        {
            _dragValue = _keys[static_cast<size_t>(_dragged)].value;
            _grabY = event->position().y();
            update();
            return;
        }
        if (_panel->_links.setCurrentFrame)
            _panel->_links.setCurrentFrame(frameAt(event->position().x()));
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (_dragged < 0)
        {
            const int key = keyAt(event->position());
            if (key >= 0)
                QToolTip::showText(event->globalPosition().toPoint(),
                    QString("Frame %1: %2").arg(_keys[static_cast<size_t>(key)].frame + 1).arg(_keys[static_cast<size_t>(key)].value, 0, 'g', 6), this);
            return;
        }
        // Up and down by the value the graph's scale gives a pixel.
        const double perPixel = (_high - _low) / std::max(1.0, plotArea().height());
        _dragValue = _keys[static_cast<size_t>(_dragged)].value + (_grabY - event->position().y()) * perPixel;
        QToolTip::showText(event->globalPosition().toPoint(), QString::number(_dragValue, 'g', 6), this);
        update();
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (_dragged < 0)
            return;
        const fla::curves::Key key = _keys[static_cast<size_t>(_dragged)];
        _dragged = -1;
        if (std::abs(_dragValue - key.value) > 1.0e-9)
            commit(key.frame, _dragValue);
        else
            update();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const int index = keyAt(event->position());
        if (index < 0)
            return;
        const fla::curves::Key key = _keys[static_cast<size_t>(index)];
        modal::getDouble(this, "Keyframe Value",
            QString("%1 on frame %2:").arg(_panel->_property->currentText()).arg(key.frame + 1), key.value, -1.0e7, 1.0e7, 3,
            [this, key](double value) { commit(key.frame, value); });
    }

private:
    void commit(int frame, double value)
    {
        fla::Timeline* timeline = nullptr;
        fla::Layer* layer = _panel->_links.currentLayer ? _panel->_links.currentLayer(timeline) : nullptr;
        fla::CommandStack* commands = _panel->_links.commands ? _panel->_links.commands() : nullptr;
        if (!layer || !commands || layer->isLocked())
            return;
        if (fla::CommandPtr command = fla::curves::setKey(*layer, frame, _key, value, _panel->_links.context()))
            commands->push(std::move(command));
        if (_panel->_links.changed)
            _panel->_links.changed();
        load();
    }

    void fitRange()
    {
        double low = 1.0e300;
        double high = -1.0e300;
        for (double value : _driven)
            if (!std::isnan(value))
            {
                low = std::min(low, value);
                high = std::max(high, value);
            }
        for (double value : _samples)
            if (!std::isnan(value))
            {
                low = std::min(low, value);
                high = std::max(high, value);
            }
        for (const fla::curves::Key& key : _keys)
        {
            low = std::min(low, key.value);
            high = std::max(high, key.value);
        }
        if (low > high)
        {
            low = 0.0;
            high = 1.0;
        }
        const double margin = std::max((high - low) * 0.15, 1.0);
        _low = low - margin;
        _high = high + margin;
    }

    QRectF plotArea() const { return QRectF(46.0, 8.0, std::max(10, width() - 54), std::max(10, height() - 26)); }
    double xOf(int frame) const
    {
        const QRectF area = plotArea();
        return area.left() + area.width() * (frame + 0.5) / std::max(1, _frames);
    }
    double yOf(double value) const
    {
        const QRectF area = plotArea();
        return area.bottom() - area.height() * (value - _low) / std::max(1.0e-9, _high - _low);
    }
    int frameAt(double x) const
    {
        const QRectF area = plotArea();
        return std::clamp(static_cast<int>((x - area.left()) / area.width() * _frames), 0, std::max(0, _frames - 1));
    }
    int keyAt(const QPointF& at) const
    {
        for (size_t i = 0; i < _keys.size(); ++i)
            if (QLineF(at, QPointF(xOf(_keys[i].frame), yOf(_keys[i].value))).length() <= 7.0)
                return static_cast<int>(i);
        return -1;
    }

    FunctionCurvesPanel* _panel;
    std::string _key;
    std::vector<double> _samples;
    /// The value with behaviors on, NaN where they leave it as keyed.
    std::vector<double> _driven;
    std::vector<fla::curves::Key> _keys;
    int _frames = 1;
    double _low = 0.0;
    double _high = 1.0;
    int _dragged = -1;
    double _dragValue = 0.0;
    double _grabY = 0.0;
};

FunctionCurvesPanel::FunctionCurvesPanel(FunctionCurvesLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    QHBoxLayout* top = new QHBoxLayout();
    _layerName = new QLabel(this);
    _property = new QComboBox(this);
    _property->setToolTip("The property shown");
    top->addWidget(_layerName, 1);
    top->addWidget(_property);
    layout->addLayout(top);
    _graph = new FunctionGraph(this);
    layout->addWidget(_graph, 1);
    connect(_property, &QComboBox::currentIndexChanged, this, [this]() { _graph->load(); });
    refresh();
}

void FunctionCurvesPanel::refresh()
{
    fla::Timeline* timeline = nullptr;
    const fla::Layer* layer = _links.currentLayer ? _links.currentLayer(timeline) : nullptr;
    _layerName->setText(layer ? QString::fromStdString(layer->name) : QString("No layer"));
    if (layer != _layer)
    {
        // Another layer: its properties, keeping the one shown when it has it.
        _layer = layer;
        const QString was = _property->currentData().toString();
        const QSignalBlocker blocker(_property);
        _property->clear();
        if (layer)
            for (const fla::curves::Property& property : fla::curves::propertiesOf(*layer))
                _property->addItem(QString::fromStdString(property.label), QString::fromStdString(property.key));
        const int keep = _property->findData(was);
        _property->setCurrentIndex(keep >= 0 ? keep : 0);
    }
    _graph->load();
}
