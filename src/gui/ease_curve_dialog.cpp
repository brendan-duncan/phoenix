#include "ease_curve_dialog.h"
#include "file_pick.h"
#include "modal.h"
#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

/// The curve may run past 0 and 1 (an ease that overshoots), so the plot
/// shows a little beyond them.
constexpr double kBelow = -0.35;
constexpr double kAbove = 1.35;

QJsonArray toJson(const std::vector<QPointF>& points)
{
    QJsonArray array;
    for (const QPointF& p : points)
        array.append(QJsonArray{p.x(), p.y()});
    return array;
}

std::vector<QPointF> fromJson(const QJsonArray& array)
{
    std::vector<QPointF> points;
    for (const QJsonValue& value : array)
    {
        const QJsonArray pair = value.toArray();
        if (pair.size() == 2)
            points.emplace_back(pair.at(0).toDouble(), pair.at(1).toDouble());
    }
    return points;
}

bool validCurve(const std::vector<QPointF>& points)
{
    return points.size() >= 4 && (points.size() - 1) % 3 == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// The editor

EaseCurveEditor::EaseCurveEditor(QWidget* parent)
    : QWidget(parent)
    , _points(linear())
{
    setMinimumSize(260, 260);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip("Drag the points and handles; double-click the curve to add a point; Delete removes the picked one");
}

std::vector<QPointF> EaseCurveEditor::linear()
{
    return {QPointF(0, 0), QPointF(1.0 / 3.0, 1.0 / 3.0), QPointF(2.0 / 3.0, 2.0 / 3.0), QPointF(1, 1)};
}

void EaseCurveEditor::setPoints(const std::vector<QPointF>& points)
{
    _points = validCurve(points) ? points : linear();
    _picked = -1;
    update();
}

QRectF EaseCurveEditor::plot() const
{
    return QRectF(20, 12, width() - 32, height() - 32);
}

QPointF EaseCurveEditor::toWidget(const QPointF& point) const
{
    const QRectF r = plot();
    return QPointF(r.left() + point.x() * r.width(),
        r.bottom() - (point.y() - kBelow) / (kAbove - kBelow) * r.height());
}

QPointF EaseCurveEditor::toCurve(const QPointF& widget) const
{
    const QRectF r = plot();
    return QPointF((widget.x() - r.left()) / r.width(),
        kBelow + (r.bottom() - widget.y()) / r.height() * (kAbove - kBelow));
}

int EaseCurveEditor::pointAt(const QPointF& widget) const
{
    int best = -1;
    double bestDistance = 9.0;
    for (size_t i = 0; i < _points.size(); ++i)
    {
        const QPointF d = toWidget(_points[i]) - widget;
        const double distance = std::hypot(d.x(), d.y());
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

QPointF EaseCurveEditor::bezier(size_t segment, double t) const
{
    const QPointF& p0 = _points[segment * 3];
    const QPointF& p1 = _points[segment * 3 + 1];
    const QPointF& p2 = _points[segment * 3 + 2];
    const QPointF& p3 = _points[segment * 3 + 3];
    const double u = 1.0 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}

void EaseCurveEditor::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), theme::color("graph.background"));
    const QRectF unit(toWidget(QPointF(0, 1)), toWidget(QPointF(1, 0)));
    painter.fillRect(unit, theme::color("graph.area"));
    painter.setPen(QPen(theme::color("graph.grid"), 1.0));
    for (int i = 1; i < 4; ++i)
    {
        const double f = i / 4.0;
        painter.drawLine(toWidget(QPointF(f, 0)), toWidget(QPointF(f, 1)));
        painter.drawLine(toWidget(QPointF(0, f)), toWidget(QPointF(1, f)));
    }
    painter.setPen(QPen(QColor(110, 110, 110), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(unit);
    painter.setPen(theme::color("graph.text"));
    painter.drawText(QRectF(unit.left(), unit.bottom() + 2, 80, 14), Qt::AlignLeft, "Frames");
    painter.save();
    painter.translate(unit.left() - 4, unit.bottom());
    painter.rotate(-90);
    painter.drawText(QRectF(0, -14, 80, 14), Qt::AlignLeft, "Tween");
    painter.restore();

    // The curve.
    QPainterPath curve(toWidget(_points.front()));
    for (size_t i = 0; i + 3 < _points.size(); i += 3)
        curve.cubicTo(toWidget(_points[i + 1]), toWidget(_points[i + 2]), toWidget(_points[i + 3]));
    painter.setPen(QPen(theme::color("playhead"), 2.0));
    painter.drawPath(curve);

    // Handles, then anchors.
    painter.setPen(QPen(QColor(160, 160, 160), 1.0));
    for (size_t i = 0; i + 3 < _points.size(); i += 3)
    {
        painter.drawLine(toWidget(_points[i]), toWidget(_points[i + 1]));
        painter.drawLine(toWidget(_points[i + 3]), toWidget(_points[i + 2]));
    }
    for (size_t i = 0; i < _points.size(); ++i)
    {
        const bool anchor = i % 3 == 0;
        const bool picked = static_cast<int>(i) == _picked;
        painter.setPen(QPen(picked ? theme::color("playhead") : QColor(20, 20, 20), 1.2));
        painter.setBrush(anchor ? QColor(240, 240, 240) : QColor(160, 160, 160));
        const QPointF at = toWidget(_points[i]);
        if (anchor)
            painter.drawRect(QRectF(at.x() - 4, at.y() - 4, 8, 8));
        else
            painter.drawEllipse(at, 3.5, 3.5);
    }
}

void EaseCurveEditor::mousePressEvent(QMouseEvent* event)
{
    _dragging = pointAt(event->position());
    _picked = _dragging >= 0 && _dragging % 3 == 0 ? _dragging : _picked;
    update();
}

void EaseCurveEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (_dragging < 0)
        return;
    QPointF p = toCurve(event->position());
    const int last = static_cast<int>(_points.size()) - 1;
    if (_dragging == 0 || _dragging == last)
        return; // the ends stay at (0, 0) and (1, 1)
    p.setY(std::clamp(p.y(), kBelow, kAbove));
    if (_dragging % 3 == 0)
    {
        // An anchor keeps between its neighbors in time and takes its
        // handles along.
        const double lo = _points[static_cast<size_t>(_dragging - 3)].x() + 0.01;
        const double hi = _points[static_cast<size_t>(_dragging + 3)].x() - 0.01;
        p.setX(std::clamp(p.x(), lo, hi));
        const QPointF delta = p - _points[static_cast<size_t>(_dragging)];
        _points[static_cast<size_t>(_dragging)] = p;
        _points[static_cast<size_t>(_dragging - 1)] += delta;
        _points[static_cast<size_t>(_dragging + 1)] += delta;
    }
    else
    {
        // A handle stays within its segment's time.
        const int segment = _dragging / 3;
        p.setX(std::clamp(p.x(), _points[static_cast<size_t>(segment * 3)].x(), _points[static_cast<size_t>(segment * 3 + 3)].x()));
        _points[static_cast<size_t>(_dragging)] = p;
    }
    update();
    emit changed();
}

void EaseCurveEditor::mouseReleaseEvent(QMouseEvent*)
{
    _dragging = -1;
}

void EaseCurveEditor::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A new anchor where the curve passes nearest, the segment split there
    // so the curve keeps its shape.
    const QPointF click = event->position();
    size_t bestSegment = 0;
    double bestT = 0.5;
    double best = 1e9;
    for (size_t s = 0; s + 3 < _points.size(); s += 3)
    {
        for (int k = 1; k < 100; ++k)
        {
            const double t = k / 100.0;
            const QPointF d = toWidget(bezier(s / 3, t)) - click;
            const double distance = std::hypot(d.x(), d.y());
            if (distance < best)
            {
                best = distance;
                bestSegment = s / 3;
                bestT = t;
            }
        }
    }
    if (best > 12.0)
        return;
    const size_t i = bestSegment * 3;
    const QPointF p0 = _points[i], p1 = _points[i + 1], p2 = _points[i + 2], p3 = _points[i + 3];
    const double t = bestT;
    const QPointF a = p0 + (p1 - p0) * t, b = p1 + (p2 - p1) * t, c = p2 + (p3 - p2) * t;
    const QPointF d = a + (b - a) * t, e = b + (c - b) * t;
    const QPointF m = d + (e - d) * t;
    _points[i + 1] = a;
    _points[i + 2] = d;
    _points.insert(_points.begin() + static_cast<std::ptrdiff_t>(i + 3), {m, e, c});
    _picked = static_cast<int>(i + 3);
    update();
    emit changed();
}

void EaseCurveEditor::keyPressEvent(QKeyEvent* event)
{
    const int last = static_cast<int>(_points.size()) - 1;
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && _picked > 0 && _picked < last &&
        _picked % 3 == 0)
    {
        // The anchor and its two handles go; the neighbors' handles stay.
        _points.erase(_points.begin() + (_picked - 1), _points.begin() + (_picked + 2));
        _picked = -1;
        update();
        emit changed();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// The dialog

EaseCurveDialog::EaseCurveDialog(const std::vector<QPointF>& points, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Custom Ease");
    _editor = new EaseCurveEditor(this);
    _editor->setPoints(points);
    connect(_editor, &EaseCurveEditor::changed, this, [this]() { _presetName.clear(); });

    _presets = new QComboBox(this);
    _presets->setToolTip("Eases saved before");
    QPushButton* save = new QPushButton("Save...", this);
    QPushButton* remove = new QPushButton("Delete", this);
    QPushButton* reset = new QPushButton("Linear", this);
    QPushButton* exportButton = new QPushButton("Export...", this);
    QPushButton* importButton = new QPushButton("Import...", this);
    loadPresets();
    connect(_presets, &QComboBox::activated, this, [this](int index) {
        const QJsonArray all = QJsonDocument::fromJson(QSettings().value("easePresets").toByteArray()).array();
        if (index <= 0 || index - 1 >= all.size())
            return;
        const QJsonObject preset = all.at(index - 1).toObject();
        _editor->setPoints(fromJson(preset["points"].toArray()));
        _presetName = preset["name"].toString();
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        modal::getText(this, "Save Ease", "Name:", _presetName, [this](const QString& value) {
            const QString name = value.trimmed();
            if (name.isEmpty())
                return;
            QJsonArray all = QJsonDocument::fromJson(QSettings().value("easePresets").toByteArray()).array();
            for (int i = all.size() - 1; i >= 0; --i)
            {
                if (all.at(i).toObject()["name"].toString() == name)
                    all.removeAt(i);
            }
            all.append(QJsonObject{{"name", name}, {"points", toJson(_editor->points())}});
            QSettings().setValue("easePresets", QJsonDocument(all).toJson(QJsonDocument::Compact));
            _presetName = name;
            loadPresets();
        });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int index = _presets->currentIndex();
        QJsonArray all = QJsonDocument::fromJson(QSettings().value("easePresets").toByteArray()).array();
        if (index <= 0 || index - 1 >= all.size())
            return;
        all.removeAt(index - 1);
        QSettings().setValue("easePresets", QJsonDocument(all).toJson(QJsonDocument::Compact));
        loadPresets();
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        _editor->setPoints(EaseCurveEditor::linear());
        _presetName.clear();
    });
    // Shared as a file: the saved eases, to give to someone else.
    connect(exportButton, &QPushButton::clicked, this, [this]() {
        file_pick::save(this, "Export Eases", "eases.json", "Eases (*.json)", [](const QString& path) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
                file.write(QSettings().value("easePresets").toByteArray());
        });
    });
    connect(importButton, &QPushButton::clicked, this, [this]() {
        file_pick::open(this, "Import Eases", QString(), "Eases (*.json)", [this](const QString& path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return;
            const QJsonArray incoming = QJsonDocument::fromJson(file.readAll()).array();
            QJsonArray all = QJsonDocument::fromJson(QSettings().value("easePresets").toByteArray()).array();
            for (const QJsonValue& value : incoming)
            {
                const QJsonObject preset = value.toObject();
                if (preset["name"].toString().isEmpty() || !validCurve(fromJson(preset["points"].toArray())))
                    continue;
                all.append(preset);
            }
            QSettings().setValue("easePresets", QJsonDocument(all).toJson(QJsonDocument::Compact));
            loadPresets();
        });
    });

    QHBoxLayout* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel("Preset:", this));
    presetRow->addWidget(_presets, 1);
    presetRow->addWidget(save);
    presetRow->addWidget(remove);
    QHBoxLayout* fileRow = new QHBoxLayout();
    fileRow->addWidget(reset);
    fileRow->addStretch();
    fileRow->addWidget(importButton);
    fileRow->addWidget(exportButton);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(_editor, 1);
    layout->addLayout(presetRow);
    layout->addLayout(fileRow);
    layout->addWidget(buttons);
    resize(420, 520);
}

void EaseCurveDialog::loadPresets()
{
    _presets->clear();
    _presets->addItem(QString::fromUtf8("—"));
    const QJsonArray all = QJsonDocument::fromJson(QSettings().value("easePresets").toByteArray()).array();
    for (const QJsonValue& value : all)
        _presets->addItem(value.toObject()["name"].toString());
}

std::vector<QPointF> EaseCurveDialog::points() const
{
    return _editor->points();
}

QString EaseCurveDialog::presetName() const
{
    return _presetName;
}
