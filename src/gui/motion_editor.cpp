#include "motion_editor.h"
#include "theme.h"

#include "../data/frame.h"
#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"
#include "../edit/timeline_edit.h"

#include <QComboBox>
#include "hot_text.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

/// The properties the editor shows, as Animate names them, their ids in
/// the tween, units, and the value a tween without one has.
struct PropertyRow
{
    const char* label;
    const char* id;
    const char* unit;
    double fallback;
};

const PropertyRow kTransformRows[] = {
    {"X", "Motion_X", " px", 0.0},
    {"Y", "Motion_Y", " px", 0.0},
    {"Rotation", "Rotation_Z", "°", 0.0},
    {"Skew X", "Skew_X", "°", 0.0},
    {"Skew Y", "Skew_Y", "°", 0.0},
    {"Scale X", "Scale_X", "%", 100.0},
    {"Scale Y", "Scale_Y", "%", 100.0},
};

/// Eases a single Bezier holds, as CSS writes them (easings.net).
struct EasePreset
{
    const char* name;
    double x1, y1, x2, y2;
};

const EasePreset kEases[] = {
    {"Linear", 1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0},
    {"Sine In", 0.12, 0.0, 0.39, 0.0},
    {"Sine Out", 0.61, 1.0, 0.88, 1.0},
    {"Sine In Out", 0.37, 0.0, 0.63, 1.0},
    {"Quad In", 0.11, 0.0, 0.5, 0.0},
    {"Quad Out", 0.5, 1.0, 0.89, 1.0},
    {"Quad In Out", 0.45, 0.0, 0.55, 1.0},
    {"Cubic In", 0.32, 0.0, 0.67, 0.0},
    {"Cubic Out", 0.33, 1.0, 0.68, 1.0},
    {"Cubic In Out", 0.65, 0.0, 0.35, 1.0},
    {"Quart In", 0.5, 0.0, 0.75, 0.0},
    {"Quart Out", 0.25, 1.0, 0.5, 1.0},
    {"Quart In Out", 0.76, 0.0, 0.24, 1.0},
    {"Quint In", 0.64, 0.0, 0.78, 0.0},
    {"Quint Out", 0.22, 1.0, 0.36, 1.0},
    {"Quint In Out", 0.83, 0.0, 0.17, 1.0},
    {"Expo In", 0.7, 0.0, 0.84, 0.0},
    {"Expo Out", 0.16, 1.0, 0.3, 1.0},
    {"Expo In Out", 0.87, 0.0, 0.13, 1.0},
    {"Circ In", 0.55, 0.0, 1.0, 0.45},
    {"Circ Out", 0.0, 0.55, 0.45, 1.0},
    {"Circ In Out", 0.85, 0.0, 0.15, 1.0},
    {"Back In", 0.36, 0.0, 0.66, -0.56},
    {"Back Out", 0.34, 1.56, 0.64, 1.0},
    {"Back In Out", 0.68, -0.6, 0.32, 1.6},
};

constexpr double kKeyHalf = 4.0;
constexpr double kHandleRadius = 3.5;
constexpr double kReach = 7.0;

} // namespace

// ---------------------------------------------------------------------------

MotionCurveView::MotionCurveView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(240, 160);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
}

void MotionCurveView::setCurve(const std::vector<fla::motion::PropertyKey>& keys, int frames, double timePerFrame,
    int current)
{
    _keys = keys;
    _frames = std::max(1, frames);
    _timePerFrame = timePerFrame > 0.0 ? timePerFrame : 1.0;
    _current = current;
    if (_selected >= static_cast<int>(_keys.size()))
        _selected = -1;
    fitRange();
    update();
}

void MotionCurveView::setKeys(const std::vector<fla::motion::PropertyKey>& keys)
{
    _keys = keys;
    if (_selected >= static_cast<int>(_keys.size()))
        _selected = -1;
    fitRange();
    update();
}

void MotionCurveView::selectKey(int index)
{
    _selected = index >= 0 && index < static_cast<int>(_keys.size()) ? index : -1;
    update();
}

void MotionCurveView::fitRange()
{
    double low = 0.0;
    double high = 0.0;
    bool first = true;
    const auto take = [&](double value) {
        low = first ? value : std::min(low, value);
        high = first ? value : std::max(high, value);
        first = false;
    };
    for (const fla::motion::PropertyKey& key : _keys)
    {
        take(key.value);
        take(key.nextValue);
        take(key.previousValue);
    }
    for (int k = 0; k < _frames; ++k)
        take(fla::motion::valueOf(_keys, k * _timePerFrame));
    if (first)
        low = high = 0.0;
    const double pad = std::max((high - low) * 0.12, 1.0);
    _low = low - pad;
    _high = high + pad;
}

QRectF MotionCurveView::plot() const
{
    return QRectF(48.0, 10.0, std::max(10.0, width() - 60.0), std::max(10.0, height() - 32.0));
}

QPointF MotionCurveView::toScreen(double time, double value) const
{
    const QRectF r = plot();
    const double span = std::max(1, _frames - 1) * _timePerFrame;
    const double x = r.left() + time / span * r.width();
    const double y = r.bottom() - (value - _low) / std::max(_high - _low, 1.0e-9) * r.height();
    return QPointF(x, y);
}

double MotionCurveView::timeAt(double x) const
{
    const QRectF r = plot();
    const double span = std::max(1, _frames - 1) * _timePerFrame;
    return (x - r.left()) / r.width() * span;
}

double MotionCurveView::valueAt(double y) const
{
    const QRectF r = plot();
    return _low + (r.bottom() - y) / r.height() * (_high - _low);
}

void MotionCurveView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Base));
    const QRectF r = plot();
    const QColor grid = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::Text);

    // Frames along the bottom, every fifth marked, and the values up the side.
    QFont small = font();
    small.setPointSizeF(std::max(6.0, small.pointSizeF() * 0.85));
    painter.setFont(small);
    for (int k = 0; k < _frames; ++k)
    {
        const double x = toScreen(k * _timePerFrame, _low).x();
        const bool major = (k + 1) % 5 == 0 || k == 0;
        painter.setPen(QPen(grid, major ? 1.0 : 0.5, major ? Qt::SolidLine : Qt::DotLine));
        if (major || _frames <= 30)
            painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        if (major)
        {
            painter.setPen(text);
            painter.drawText(QRectF(x - 20, r.bottom() + 2, 40, 16), Qt::AlignHCenter | Qt::AlignTop,
                QString::number(k + 1));
        }
    }
    for (int i = 0; i <= 4; ++i)
    {
        const double value = _low + (_high - _low) * i / 4.0;
        const double y = toScreen(0.0, value).y();
        painter.setPen(QPen(grid, 0.5, Qt::DotLine));
        painter.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        painter.setPen(text);
        painter.drawText(QRectF(0, y - 8, r.left() - 4, 16), Qt::AlignRight | Qt::AlignVCenter,
            QString::number(value, 'f', std::fabs(_high - _low) < 10.0 ? 2 : 1));
    }

    // The playhead.
    const double playX = toScreen(_current * _timePerFrame, _low).x();
    painter.setPen(QPen(theme::color("playhead"), 1.0));
    painter.drawLine(QPointF(playX, r.top()), QPointF(playX, r.bottom()));

    // The curve.
    if (!_keys.empty())
    {
        QPainterPath path;
        const int steps = std::max(2, static_cast<int>(r.width() / 2.0));
        const double span = std::max(1, _frames - 1) * _timePerFrame;
        for (int i = 0; i <= steps; ++i)
        {
            const double time = span * i / steps;
            const QPointF p = toScreen(time, fla::motion::valueOf(_keys, time));
            if (i == 0)
                path.moveTo(p);
            else
                path.lineTo(p);
        }
        painter.setPen(QPen(pal.color(QPalette::Highlight), 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    // The selected keyframe's handles.
    if (_selected >= 0)
    {
        const fla::motion::PropertyKey& key = _keys[static_cast<size_t>(_selected)];
        const QPointF at = toScreen(key.time, key.value);
        painter.setPen(QPen(text, 1.0));
        if (_selected + 1 < static_cast<int>(_keys.size()))
        {
            const QPointF h = toScreen(key.time + key.nextTime, key.nextValue);
            painter.drawLine(at, h);
            painter.setBrush(pal.color(QPalette::Base));
            painter.drawEllipse(h, kHandleRadius, kHandleRadius);
        }
        if (_selected > 0)
        {
            const QPointF h = toScreen(key.time + key.previousTime, key.previousValue);
            painter.drawLine(at, h);
            painter.setBrush(pal.color(QPalette::Base));
            painter.drawEllipse(h, kHandleRadius, kHandleRadius);
        }
    }

    // The keyframes.
    for (size_t i = 0; i < _keys.size(); ++i)
    {
        const QPointF p = toScreen(_keys[i].time, _keys[i].value);
        painter.setPen(QPen(text, 1.0));
        painter.setBrush(static_cast<int>(i) == _selected ? pal.color(QPalette::Highlight) : pal.color(QPalette::Base));
        painter.drawRect(QRectF(p.x() - kKeyHalf, p.y() - kKeyHalf, 2 * kKeyHalf, 2 * kKeyHalf));
    }
}

void MotionCurveView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF at = event->position();
    _dragging = 0;
    // The selected keyframe's handles first, then the keyframes.
    if (_selected >= 0)
    {
        const fla::motion::PropertyKey& key = _keys[static_cast<size_t>(_selected)];
        if (_selected + 1 < static_cast<int>(_keys.size()) &&
            QLineF(at, toScreen(key.time + key.nextTime, key.nextValue)).length() <= kReach)
            _dragging = 2;
        else if (_selected > 0 && QLineF(at, toScreen(key.time + key.previousTime, key.previousValue)).length() <= kReach)
            _dragging = 3;
    }
    if (!_dragging)
    {
        int hit = -1;
        for (size_t i = 0; i < _keys.size(); ++i)
        {
            if (QLineF(at, toScreen(_keys[i].time, _keys[i].value)).length() <= kReach)
                hit = static_cast<int>(i);
        }
        _selected = hit;
        _dragging = hit >= 0 ? 1 : 0;
        emit selectionChanged();
    }
    update();
}

void MotionCurveView::mouseMoveEvent(QMouseEvent* event)
{
    if (!_dragging || _selected < 0)
        return;
    const QPointF at = event->position();
    const size_t index = static_cast<size_t>(_selected);
    fla::motion::PropertyKey& key = _keys[index];
    const double previousTime = index > 0 ? _keys[index - 1].time : 0.0;
    const double nextTime = index + 1 < _keys.size() ? _keys[index + 1].time : (_frames - 1) * _timePerFrame;
    if (_dragging == 1)
    {
        // In time by whole frames, between its neighbors; the first stays
        // at the start.
        if (index > 0)
        {
            double time = std::round(timeAt(at.x()) / _timePerFrame) * _timePerFrame;
            const double lowest = previousTime + _timePerFrame;
            const double highest = index + 1 < _keys.size() ? nextTime - _timePerFrame : (_frames - 1) * _timePerFrame;
            key.time = std::max(lowest, std::min(highest, time));
        }
        const double value = valueAt(at.y());
        const double change = value - key.value;
        key.value = value;
        key.nextValue += change;
        key.previousValue += change;
    }
    else if (_dragging == 2)
    {
        key.nextTime = std::max(0.0, std::min(nextTime - key.time, timeAt(at.x()) - key.time));
        key.nextValue = valueAt(at.y());
    }
    else
    {
        key.previousTime = std::min(0.0, std::max(previousTime - key.time, timeAt(at.x()) - key.time));
        key.previousValue = valueAt(at.y());
    }
    update();
    emit keysEdited(false);
}

void MotionCurveView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !_dragging)
        return;
    _dragging = 0;
    fitRange();
    update();
    emit keysEdited(true);
}

void MotionCurveView::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A keyframe at the frame clicked, on the curve as it was.
    const int frame = static_cast<int>(std::lround(timeAt(event->position().x()) / _timePerFrame));
    if (frame < 0 || frame >= _frames)
        return;
    _selected = static_cast<int>(fla::motion::splitAt(_keys, frame * _timePerFrame));
    _dragging = 0;
    update();
    emit selectionChanged();
    emit keysEdited(true);
}

void MotionCurveView::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && _selected > 0)
    {
        _keys.erase(_keys.begin() + _selected);
        _selected = -1;
        fitRange();
        update();
        emit selectionChanged();
        emit keysEdited(true);
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------

MotionEditorPanel::MotionEditorPanel(MotionEditorLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    _title = new QLabel(this);
    _title->setWordWrap(true);
    layout->addWidget(_title);

    QHBoxLayout* body = new QHBoxLayout();
    _properties = new QListWidget(this);
    _properties->setFixedWidth(120);
    _properties->setCurrentRow(0);
    body->addWidget(_properties);
    _curve = new MotionCurveView(this);
    body->addWidget(_curve, 1);
    layout->addLayout(body, 1);

    QHBoxLayout* keyRow = new QHBoxLayout();
    _add = new QPushButton("Add Keyframe", this);
    _add->setToolTip("A keyframe at the playhead for this property, the curve as it was");
    _remove = new QPushButton("Remove", this);
    _remove->setToolTip("Take the selected keyframe away (Delete)");
    _keyFrame = new HotSpinBox(this);
    _keyFrame->setPrefix("Frame ");
    _keyValue = new HotDoubleSpinBox(this);
    _keyValue->setRange(-1.0e6, 1.0e6);
    _keyValue->setDecimals(2);
    keyRow->addWidget(_add);
    keyRow->addWidget(_remove);
    keyRow->addWidget(_keyFrame);
    keyRow->addWidget(_keyValue, 1);
    layout->addLayout(keyRow);

    QFormLayout* eases = new QFormLayout();
    _ease = new QComboBox(this);
    _ease->addItem("Choose an ease…");
    for (const EasePreset& preset : kEases)
        _ease->addItem(preset.name);
    _ease->setToolTip("Ease the stretch after the selected keyframe, or the whole curve when none is selected");
    eases->addRow("Property ease", _ease);
    _spanEase = new HotDoubleSpinBox(this);
    _spanEase->setRange(-100.0, 100.0);
    _spanEase->setDecimals(0);
    _spanEase->setToolTip("Ease of the whole tween: below zero eases in, above zero eases out");
    eases->addRow("Tween ease", _spanEase);
    layout->addLayout(eases);

    connect(_properties, &QListWidget::currentRowChanged, this, [this](int) {
        if (_updating)
            return;
        _curve->selectKey(-1);
        showProperty();
    });
    connect(_curve, &MotionCurveView::keysEdited, this, [this](bool done) { writeKeys(done, "Edit Motion Curve"); });
    connect(_curve, &MotionCurveView::selectionChanged, this, &MotionEditorPanel::updateKeyFields);
    connect(_add, &QPushButton::clicked, this, &MotionEditorPanel::addKeyAtPlayhead);
    connect(_remove, &QPushButton::clicked, this, &MotionEditorPanel::removeKey);
    connect(_ease, &QComboBox::activated, this, [this](int index) {
        if (index > 0)
            applyEase(index - 1);
        _ease->setCurrentIndex(0);
    });
    connect(_keyValue, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
        const int index = _curve->selectedKey();
        if (_updating || index < 0)
            return;
        std::vector<fla::motion::PropertyKey> keys = _curve->keys();
        fla::motion::PropertyKey& key = keys[static_cast<size_t>(index)];
        const double change = value - key.value;
        key.value = value;
        key.nextValue += change;
        key.previousValue += change;
        _curve->setKeys(keys);
        writeKeys(true, "Keyframe Value");
    });
    connect(_keyFrame, &HotSpinBox::valueChanged, this, [this](int frame) {
        const int index = _curve->selectedKey();
        if (_updating || index <= 0 || !_frame)
            return;
        std::vector<fla::motion::PropertyKey> keys = _curve->keys();
        std::string* xml = fla::timeline::motionOf(*_frame);
        fla::MotionObject motion;
        if (!xml || !motion.parse(*xml))
            return;
        const double frames = motion.frames(_links.frameRate ? _links.frameRate() : 24.0);
        const double perFrame = frames > 0.0 ? motion.duration() / frames : 1.0;
        const size_t i = static_cast<size_t>(index);
        const double low = keys[i - 1].time + perFrame;
        const double high = i + 1 < keys.size() ? keys[i + 1].time - perFrame : (_frame->duration - 1) * perFrame;
        keys[i].time = std::max(low, std::min(high, (frame - 1) * perFrame));
        _curve->setKeys(keys);
        writeKeys(true, "Keyframe Frame");
    });
    connect(_spanEase, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
        if (_updating || !_frame)
            return;
        std::vector<std::string> children = _frame->extraChildren;
        for (std::string& child : children)
        {
            if (child.compare(0, 16, "<motionObjectXML") == 0)
                fla::motion::setSpanEase(child, value);
        }
        if (fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr)
            stack->push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
                _frame->extraChildren, _frame->extraChildren, children, "Tween Ease")));
        if (_links.changed)
            _links.changed();
    });
    refresh();
}

void MotionEditorPanel::refresh()
{
    // A drag in progress keeps its own copy until it is let go.
    if (_editing)
        return;
    _frame = _links.span ? _links.span() : nullptr;
    const bool on = _frame && fla::timeline::motionOf(*_frame);
    for (QWidget* widget : std::initializer_list<QWidget*>{_properties, _curve, _add, _remove, _keyFrame, _keyValue,
             _ease, _spanEase})
        widget->setEnabled(on);
    if (!on)
    {
        _frame = nullptr;
        _title->setText("Select an object with a motion tween, or put the playhead in one.");
        _curve->setCurve({}, 1, 1.0, 0);
        return;
    }
    _title->setText(QString("Motion tween, frames %1 to %2").arg(_frame->index + 1).arg(_frame->index + _frame->duration));
    buildRows();
    _updating = true;
    _spanEase->setValue(fla::motion::spanEase(*fla::timeline::motionOf(*_frame)));
    _keyFrame->setRange(1, std::max(1, _frame->duration));
    _updating = false;
    showProperty();
}

void MotionEditorPanel::showProperty()
{
    if (!_frame)
        return;
    std::string* xml = fla::timeline::motionOf(*_frame);
    fla::MotionObject motion;
    if (!xml || !motion.parse(*xml))
        return;
    const Row& row = currentRow();
    std::vector<fla::motion::PropertyKey> keys = fla::motion::keysOf(*xml, row.id.c_str());
    if (keys.empty())
        keys.push_back(fla::motion::PropertyKey::at(0.0, motion.valueAt(row.id, 0.0, row.fallback)));
    const double frames = motion.frames(_links.frameRate ? _links.frameRate() : 24.0);
    const double perFrame = frames > 0.0 ? motion.duration() / frames : 1.0;
    const int current = _links.currentFrame ? _links.currentFrame() - _frame->index : 0;
    _curve->setCurve(keys, _frame->duration, perFrame, std::max(0, std::min(_frame->duration - 1, current)));
    _keyValue->setSuffix(row.unit);
    updateKeyFields();
}

void MotionEditorPanel::updateKeyFields()
{
    const int index = _curve->selectedKey();
    _updating = true;
    _remove->setEnabled(_frame && index > 0);
    _keyValue->setEnabled(_frame && index >= 0);
    _keyFrame->setEnabled(_frame && index > 0);
    if (index >= 0 && _frame)
    {
        const fla::motion::PropertyKey& key = _curve->keys()[static_cast<size_t>(index)];
        _keyValue->setValue(key.value);
        std::string* xml = fla::timeline::motionOf(*_frame);
        fla::MotionObject motion;
        if (xml && motion.parse(*xml))
        {
            const double frames = motion.frames(_links.frameRate ? _links.frameRate() : 24.0);
            const double perFrame = frames > 0.0 ? motion.duration() / frames : 1.0;
            _keyFrame->setValue(static_cast<int>(std::lround(key.time / perFrame)) + 1);
        }
    }
    _updating = false;
}

void MotionEditorPanel::cancelDrag()
{
    if (!_editing)
        return;
    if (_frame)
        _frame->extraChildren = _before;
    _editing = false;
}

void MotionEditorPanel::writeKeys(bool done, const char* name)
{
    if (!_frame)
        return;
    if (!_editing)
    {
        _before = _frame->extraChildren;
        _editing = true;
    }
    const Row& row = currentRow();
    std::vector<std::string> children = _before;
    for (std::string& child : children)
    {
        if (child.compare(0, 16, "<motionObjectXML") == 0)
            fla::motion::setKeysOf(child, row.id, _curve->keys());
    }
    _frame->extraChildren = children;
    if (done)
    {
        _frame->extraChildren = _before;
        _editing = false;
        if (children != _before)
        {
            if (fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr)
            {
                stack->push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
                    _frame->extraChildren, _before, children, name)));
                stack->breakMergeChain();
            }
            else
            {
                _frame->extraChildren = children;
            }
        }
        updateKeyFields();
    }
    if (_links.changed)
        _links.changed();
}

void MotionEditorPanel::addKeyAtPlayhead()
{
    if (!_frame)
        return;
    std::string* xml = fla::timeline::motionOf(*_frame);
    fla::MotionObject motion;
    if (!xml || !motion.parse(*xml))
        return;
    const double frames = motion.frames(_links.frameRate ? _links.frameRate() : 24.0);
    const double perFrame = frames > 0.0 ? motion.duration() / frames : 1.0;
    const int at = _links.currentFrame ? _links.currentFrame() - _frame->index : 0;
    std::vector<fla::motion::PropertyKey> keys = _curve->keys();
    const size_t index = fla::motion::splitAt(keys, std::max(0, std::min(_frame->duration - 1, at)) * perFrame);
    _curve->setCurve(keys, _frame->duration, perFrame, at);
    _curve->selectKey(static_cast<int>(index));
    writeKeys(true, "Add Property Keyframe");
}

void MotionEditorPanel::removeKey()
{
    const int index = _curve->selectedKey();
    if (index <= 0)
        return;
    std::vector<fla::motion::PropertyKey> keys = _curve->keys();
    keys.erase(keys.begin() + index);
    _curve->selectKey(-1);
    _curve->setKeys(keys);
    writeKeys(true, "Remove Property Keyframe");
    showProperty();
}

void MotionEditorPanel::applyEase(int preset)
{
    if (preset < 0 || preset >= static_cast<int>(std::size(kEases)))
        return;
    const EasePreset& ease = kEases[preset];
    std::vector<fla::motion::PropertyKey> keys = _curve->keys();
    int selected = _curve->selectedKey();
    // The last keyframe has no stretch after it: the one before it, then.
    if (selected >= 0 && selected + 1 >= static_cast<int>(keys.size()))
        --selected;
    for (size_t i = 0; i + 1 < keys.size(); ++i)
    {
        if (selected < 0 || static_cast<int>(i) == selected)
            fla::motion::easeSegment(keys, i, ease.x1, ease.y1, ease.x2, ease.y2);
    }
    _curve->setKeys(keys);
    writeKeys(true, "Ease");
    showProperty();
}

void MotionEditorPanel::buildRows()
{
    // The transform's properties, the color effect's, and each filter's
    // numbers, at the values the object has when the tween does not set them.
    std::vector<Row> rows;
    for (const PropertyRow& row : kTransformRows)
        rows.push_back({row.label, row.id, row.unit, row.fallback});
    const auto* instance = _frame && _frame->elements.size() == 1
        ? dynamic_cast<const fla::SymbolInstance*>(_frame->elements.front()) : nullptr;
    if (instance)
    {
        const char* percents[] = {"Alpha_Amount", "Brightness_Amount", "Tint_Amount", "AdvClr_R_Pct", "AdvClr_G_Pct",
            "AdvClr_B_Pct", "AdvClr_A_Pct"};
        for (int i = 0; i < fla::motion::kColorChannelCount; ++i)
        {
            const std::string id = fla::motion::kColorChannels[i].id;
            const bool percent = std::find(std::begin(percents), std::end(percents), id) != std::end(percents);
            rows.push_back({fla::motion::kColorChannels[i].label, id, percent ? "%" : "",
                fla::motion::colorValue(instance->colorTransform, id)});
        }
        for (size_t f = 0; f < instance->filters.size(); ++f)
        {
            const fla::Filter& filter = instance->filters[f];
            QString name = QString(filter.elementName()).remove("Filter");
            for (const std::string& field : fla::motion::filterFields(filter))
            {
                QString label = QString::fromStdString(field);
                label[0] = label[0].toUpper();
                rows.push_back({(name + " " + label).toStdString(), fla::motion::filterPropertyId(filter, f, field),
                    field == "angle" || field == "hue" ? "\u00b0" : (field == "strength" ? "%" : ""),
                    fla::motion::filterValue(filter, field)});
            }
        }
    }
    const QString current = _properties->currentItem() ? _properties->currentItem()->text() : QString();
    bool same = rows.size() == _rows.size();
    for (size_t i = 0; same && i < rows.size(); ++i)
        same = rows[i].id == _rows[i].id;
    if (same)
    {
        _rows = rows;
        return;
    }
    _rows = rows;
    _updating = true;
    _properties->clear();
    for (const Row& row : _rows)
        _properties->addItem(QString::fromStdString(row.label));
    const QList<QListWidgetItem*> found = _properties->findItems(current, Qt::MatchExactly);
    _properties->setCurrentRow(found.isEmpty() ? 0 : _properties->row(found.front()));
    _updating = false;
}

const MotionEditorPanel::Row& MotionEditorPanel::currentRow() const
{
    static const Row none{"X", "Motion_X", " px", 0.0};
    const int index = _properties->currentRow();
    return index >= 0 && index < static_cast<int>(_rows.size()) ? _rows[static_cast<size_t>(index)] : none;
}
