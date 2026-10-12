#include "arrange_panels.h"

#include "phoenix_view.h"
#include "../data/element.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/transform_parts.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

QTransform toQ(const fla::Transform& t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

fla::Transform fromQ(const QTransform& q)
{
    return fla::Transform(q.m11(), q.m12(), q.m21(), q.m22(), q.dx(), q.dy());
}

/// A small button with a drawn icon: \a draw paints into 20 x 20.
QToolButton* iconButton(QWidget* parent, const QString& tip, const std::function<void(QPainter&)>& draw)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(210, 210, 210));
    draw(painter);
    painter.end();
    QToolButton* button = new QToolButton(parent);
    button->setIcon(QIcon(pixmap));
    button->setIconSize(QSize(20, 20));
    button->setAutoRaise(true);
    button->setToolTip(tip);
    return button;
}

/// Moves and resizes elements as one undo step, \a change giving each its new
/// transform.
void transformAll(const ArrangeLinks& links, const std::vector<fla::Element*>& elements, const char* name,
    const std::function<fla::Transform(fla::Element*, const QRectF&)>& change)
{
    fla::CommandStack* stack = links.commands ? links.commands() : nullptr;
    if (!stack || !links.view || elements.empty())
        return;
    links.view->flushPendingMerge();
    stack->beginMacro(name);
    for (fla::Element* element : elements)
    {
        const QRectF bounds = links.view->elementBounds(element);
        const fla::Transform before = element->transform;
        const fla::Transform after = change(element, bounds);
        if (after.m11 == before.m11 && after.m12 == before.m12 && after.m21 == before.m21 && after.m22 == before.m22 &&
            after.tx == before.tx && after.ty == before.ty)
            continue;
        stack->push(links.view->transformCommand(element, before, after, name));
        if (element->elementType() == fla::Element::Type::Shape)
            links.view->markShapeEdited(static_cast<fla::Shape*>(element));
    }
    stack->endMacro();
    stack->breakMergeChain();
    links.view->invalidateBounds();
    if (links.changed)
        links.changed();
}

fla::Transform moved(const fla::Transform& t, double dx, double dy)
{
    fla::Transform result = t;
    result.tx += dx;
    result.ty += dy;
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Align

AlignPanel::AlignPanel(ArrangeLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    // The icons: bars standing for the objects, a line for the edge.
    const auto bars = [](bool horizontal, int line) {
        return [horizontal, line](QPainter& p) {
            p.setBrush(QColor(210, 210, 210));
            if (horizontal)
            {
                p.drawRect(QRectF(line == 0 ? 3 : line == 1 ? 6 : 9, 4, 8, 4));
                p.drawRect(QRectF(line == 0 ? 3 : line == 1 ? 4 : 5, 12, 12, 4));
                p.setBrush(QColor(41, 128, 238));
                p.drawRect(QRectF(line == 0 ? 2 : line == 1 ? 9.5 : 17, 1, 1.2, 18));
            }
            else
            {
                p.drawRect(QRectF(4, line == 0 ? 3 : line == 1 ? 6 : 9, 4, 8));
                p.drawRect(QRectF(12, line == 0 ? 3 : line == 1 ? 4 : 5, 4, 12));
                p.setBrush(QColor(41, 128, 238));
                p.drawRect(QRectF(1, line == 0 ? 2 : line == 1 ? 9.5 : 17, 18, 1.2));
            }
        };
    };
    const Edge edges[6] = {Edge::Left, Edge::HCenter, Edge::Right, Edge::Top, Edge::VCenter, Edge::Bottom};
    const char* alignTips[6] = {"Align left edges", "Align horizontal centers", "Align right edges", "Align top edges",
        "Align vertical centers", "Align bottom edges"};
    const char* spreadTips[6] = {"Distribute left edges", "Distribute horizontal centers", "Distribute right edges",
        "Distribute top edges", "Distribute vertical centers", "Distribute bottom edges"};

    QGridLayout* grid = new QGridLayout();
    grid->addWidget(new QLabel("Align", this), 0, 0, 1, 6);
    grid->addWidget(new QLabel("Distribute", this), 2, 0, 1, 6);
    for (int i = 0; i < 6; ++i)
    {
        QToolButton* alignButton = iconButton(this, alignTips[i], bars(i < 3, i % 3));
        connect(alignButton, &QToolButton::clicked, this, [this, edge = edges[i]]() { align(edge); });
        grid->addWidget(alignButton, 1, i);
        QToolButton* spreadButton = iconButton(this, spreadTips[i], bars(i < 3, i % 3));
        connect(spreadButton, &QToolButton::clicked, this, [this, edge = edges[i]]() { distribute(edge); });
        grid->addWidget(spreadButton, 3, i);
    }
    grid->addWidget(new QLabel("Match size", this), 4, 0, 1, 3);
    grid->addWidget(new QLabel("Space", this), 4, 3, 1, 3);
    QToolButton* width = iconButton(this, "Match width", [](QPainter& p) {
        p.drawRect(QRectF(2, 3, 16, 5));
        p.drawRect(QRectF(2, 12, 16, 5));
    });
    connect(width, &QToolButton::clicked, this, [this]() { matchSize(true, false); });
    QToolButton* height = iconButton(this, "Match height", [](QPainter& p) {
        p.drawRect(QRectF(3, 2, 5, 16));
        p.drawRect(QRectF(12, 2, 5, 16));
    });
    connect(height, &QToolButton::clicked, this, [this]() { matchSize(false, true); });
    QToolButton* both = iconButton(this, "Match width and height", [](QPainter& p) {
        p.drawRect(QRectF(2, 2, 7, 7));
        p.drawRect(QRectF(11, 11, 7, 7));
    });
    connect(both, &QToolButton::clicked, this, [this]() { matchSize(true, true); });
    QToolButton* spaceH = iconButton(this, "Space evenly across", [](QPainter& p) {
        p.drawRect(QRectF(1, 5, 4, 10));
        p.drawRect(QRectF(8, 5, 4, 10));
        p.drawRect(QRectF(15, 5, 4, 10));
    });
    connect(spaceH, &QToolButton::clicked, this, [this]() { space(true); });
    QToolButton* spaceV = iconButton(this, "Space evenly down", [](QPainter& p) {
        p.drawRect(QRectF(5, 1, 10, 4));
        p.drawRect(QRectF(5, 8, 10, 4));
        p.drawRect(QRectF(5, 15, 10, 4));
    });
    connect(spaceV, &QToolButton::clicked, this, [this]() { space(false); });
    grid->addWidget(width, 5, 0);
    grid->addWidget(height, 5, 1);
    grid->addWidget(both, 5, 2);
    grid->addWidget(spaceH, 5, 3);
    grid->addWidget(spaceV, 5, 4);

    _toStage = new QCheckBox("Align to stage", this);
    _toStage->setToolTip("Line up with the stage rather than with each other");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addLayout(grid);
    layout->addWidget(_toStage);
    layout->addStretch();
}

void AlignPanel::align(Edge edge)
{
    const std::vector<fla::Element*> elements = _links.elements ? _links.elements() : std::vector<fla::Element*>();
    if (elements.empty() || !_links.view)
        return;
    // The edge to line up with: the stage's, or the selection's own.
    QRectF target;
    if (_toStage->isChecked() && _links.stage)
    {
        target = _links.stage();
    }
    else
    {
        if (elements.size() < 2)
            return;
        for (fla::Element* element : elements)
        {
            const QRectF b = _links.view->elementBounds(element);
            target = target.isValid() ? target.united(b) : b;
        }
    }
    transformAll(_links, elements, "Align", [&](fla::Element* element, const QRectF& b) {
        double dx = 0.0, dy = 0.0;
        switch (edge)
        {
        case Edge::Left: dx = target.left() - b.left(); break;
        case Edge::HCenter: dx = target.center().x() - b.center().x(); break;
        case Edge::Right: dx = target.right() - b.right(); break;
        case Edge::Top: dy = target.top() - b.top(); break;
        case Edge::VCenter: dy = target.center().y() - b.center().y(); break;
        case Edge::Bottom: dy = target.bottom() - b.bottom(); break;
        }
        return moved(element->transform, dx, dy);
    });
}

void AlignPanel::distribute(Edge edge)
{
    std::vector<fla::Element*> elements = _links.elements ? _links.elements() : std::vector<fla::Element*>();
    if (elements.size() < 2 || !_links.view)
        return;
    const auto where = [&](fla::Element* element) {
        const QRectF b = _links.view->elementBounds(element);
        switch (edge)
        {
        case Edge::Left: return b.left();
        case Edge::HCenter: return b.center().x();
        case Edge::Right: return b.right();
        case Edge::Top: return b.top();
        case Edge::VCenter: return b.center().y();
        case Edge::Bottom: return b.bottom();
        }
        return 0.0;
    };
    std::stable_sort(elements.begin(), elements.end(), [&](fla::Element* a, fla::Element* b) { return where(a) < where(b); });
    // From the first to the last, or across the stage.
    double first = where(elements.front());
    double last = where(elements.back());
    if (_toStage->isChecked() && _links.stage)
    {
        const QRectF stage = _links.stage();
        const bool horizontal = edge == Edge::Left || edge == Edge::HCenter || edge == Edge::Right;
        first = horizontal ? stage.left() : stage.top();
        last = horizontal ? stage.right() : stage.bottom();
    }
    const bool horizontal = edge == Edge::Left || edge == Edge::HCenter || edge == Edge::Right;
    std::vector<double> targets(elements.size());
    for (size_t i = 0; i < elements.size(); ++i)
        targets[i] = first + (last - first) * static_cast<double>(i) / static_cast<double>(elements.size() - 1);
    size_t index = 0;
    std::vector<std::pair<fla::Element*, double>> moves;
    for (fla::Element* element : elements)
        moves.emplace_back(element, targets[index++] - where(element));
    transformAll(_links, elements, "Distribute", [&](fla::Element* element, const QRectF&) {
        for (const auto& move : moves)
        {
            if (move.first == element)
                return horizontal ? moved(element->transform, move.second, 0.0) : moved(element->transform, 0.0, move.second);
        }
        return element->transform;
    });
}

void AlignPanel::matchSize(bool width, bool height)
{
    const std::vector<fla::Element*> elements = _links.elements ? _links.elements() : std::vector<fla::Element*>();
    if (elements.empty() || !_links.view)
        return;
    // To the largest of them, or the stage's size.
    double w = 0.0, h = 0.0;
    if (_toStage->isChecked() && _links.stage)
    {
        w = _links.stage().width();
        h = _links.stage().height();
    }
    else
    {
        for (fla::Element* element : elements)
        {
            const QRectF b = _links.view->elementBounds(element);
            w = std::max(w, b.width());
            h = std::max(h, b.height());
        }
    }
    transformAll(_links, elements, "Match Size", [&](fla::Element* element, const QRectF& b) {
        if (b.width() <= 0.0 || b.height() <= 0.0)
            return element->transform;
        const double sx = width ? w / b.width() : 1.0;
        const double sy = height ? h / b.height() : 1.0;
        // Grown from its top-left corner, as Animate does.
        const QTransform about = QTransform::fromTranslate(-b.left(), -b.top()) * QTransform::fromScale(sx, sy) *
            QTransform::fromTranslate(b.left(), b.top());
        return fromQ(toQ(element->transform) * about);
    });
}

void AlignPanel::space(bool horizontal)
{
    std::vector<fla::Element*> elements = _links.elements ? _links.elements() : std::vector<fla::Element*>();
    if (elements.size() < 3 && !(elements.size() >= 2 && _toStage->isChecked()))
        return;
    std::vector<std::pair<fla::Element*, QRectF>> boxes;
    for (fla::Element* element : elements)
        boxes.emplace_back(element, _links.view->elementBounds(element));
    std::stable_sort(boxes.begin(), boxes.end(), [horizontal](const auto& a, const auto& b) {
        return horizontal ? a.second.left() < b.second.left() : a.second.top() < b.second.top();
    });
    // The same gap between each neighbor and the next.
    double start = horizontal ? boxes.front().second.left() : boxes.front().second.top();
    double end = horizontal ? boxes.back().second.right() : boxes.back().second.bottom();
    if (_toStage->isChecked() && _links.stage)
    {
        const QRectF stage = _links.stage();
        start = horizontal ? stage.left() : stage.top();
        end = horizontal ? stage.right() : stage.bottom();
    }
    double total = 0.0;
    for (const auto& box : boxes)
        total += horizontal ? box.second.width() : box.second.height();
    const double gap = (end - start - total) / static_cast<double>(boxes.size() - 1);
    std::vector<std::pair<fla::Element*, double>> moves;
    double at = start;
    for (const auto& box : boxes)
    {
        moves.emplace_back(box.first, at - (horizontal ? box.second.left() : box.second.top()));
        at += (horizontal ? box.second.width() : box.second.height()) + gap;
    }
    transformAll(_links, elements, "Space Evenly", [&](fla::Element* element, const QRectF&) {
        for (const auto& move : moves)
        {
            if (move.first == element)
                return horizontal ? moved(element->transform, move.second, 0.0) : moved(element->transform, 0.0, move.second);
        }
        return element->transform;
    });
}

// ---------------------------------------------------------------------------
// Transform

namespace {

using Decomposed = fla::TransformParts;

Decomposed decompose(const fla::Transform& t)
{
    return fla::TransformParts::of(t);
}

fla::Transform compose(const Decomposed& d, double tx, double ty)
{
    return d.matrix(tx, ty);
}

double clean(double value)
{
    return std::abs(value) < 1e-9 ? 0.0 : value;
}

} // namespace

TransformPanel::TransformPanel(ArrangeLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    const auto spin = [this](double minimum, double maximum, const QString& suffix) {
        QDoubleSpinBox* box = new QDoubleSpinBox(this);
        box->setRange(minimum, maximum);
        box->setDecimals(1);
        box->setSuffix(suffix);
        box->setKeyboardTracking(false);
        return box;
    };
    _scaleX = spin(-10000, 10000, "%");
    _scaleY = spin(-10000, 10000, "%");
    _constrain = new QCheckBox("Constrain", this);
    _constrain->setChecked(true);
    connect(_scaleX, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (_updating)
            return;
        if (_constrain->isChecked())
        {
            _updating = true;
            _scaleY->setValue(std::copysign(std::abs(value), _scaleY->value()));
            _updating = false;
        }
        apply(false);
    });
    connect(_scaleY, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (_updating)
            return;
        if (_constrain->isChecked())
        {
            _updating = true;
            _scaleX->setValue(std::copysign(std::abs(value), _scaleX->value()));
            _updating = false;
        }
        apply(false);
    });
    _rotateChoice = new QRadioButton("Rotate", this);
    _skewChoice = new QRadioButton("Skew", this);
    _rotateChoice->setChecked(true);
    _rotation = spin(-360, 360, QString::fromUtf8("°"));
    _skewX = spin(-360, 360, QString::fromUtf8("°"));
    _skewY = spin(-360, 360, QString::fromUtf8("°"));
    for (QDoubleSpinBox* box : {_rotation, _skewX, _skewY})
    {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, box]() {
            if (_updating)
                return;
            _rotateChoice->setChecked(box == _rotation);
            _skewChoice->setChecked(box != _rotation);
            apply(false);
        });
    }

    QPushButton* duplicate = new QPushButton("Duplicate and Transform", this);
    duplicate->setToolTip("Apply the values to a copy of the selection");
    connect(duplicate, &QPushButton::clicked, this, [this]() { apply(true); });
    QPushButton* reset = new QPushButton("Remove Transform", this);
    connect(reset, &QPushButton::clicked, this, [this]() {
        _updating = true;
        _scaleX->setValue(100.0);
        _scaleY->setValue(100.0);
        _rotation->setValue(0.0);
        _skewX->setValue(0.0);
        _skewY->setValue(0.0);
        _updating = false;
        _rotateChoice->setChecked(true);
        apply(false);
    });

    QGridLayout* grid = new QGridLayout();
    grid->addWidget(new QLabel("Scale", this), 0, 0);
    grid->addWidget(_scaleX, 0, 1);
    grid->addWidget(_scaleY, 0, 2);
    grid->addWidget(_constrain, 1, 1, 1, 2);
    grid->addWidget(_rotateChoice, 2, 0);
    grid->addWidget(_rotation, 2, 1);
    grid->addWidget(_skewChoice, 3, 0);
    grid->addWidget(_skewX, 3, 1);
    grid->addWidget(_skewY, 3, 2);
    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addWidget(duplicate);
    buttons->addWidget(reset);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addLayout(grid);
    layout->addLayout(buttons);
    layout->addStretch();
    refresh();
}

void TransformPanel::refresh()
{
    const std::vector<fla::Element*> elements = _links.elements ? _links.elements() : std::vector<fla::Element*>();
    setEnabled(!elements.empty());
    if (elements.empty())
        return;
    // The first selected object's values stand for the selection.
    const Decomposed d = decompose(elements.back()->transform);
    _updating = true;
    _scaleX->setValue(clean(d.scaleX));
    _scaleY->setValue(clean(d.scaleY));
    _rotation->setValue(clean(d.skewY));
    _skewX->setValue(clean(d.skewX));
    _skewY->setValue(clean(d.skewY));
    _rotateChoice->setChecked(std::abs(d.skewX - d.skewY) < 1e-6);
    _skewChoice->setChecked(!_rotateChoice->isChecked());
    _updating = false;
}

void TransformPanel::apply(bool duplicate)
{
    if (duplicate && _links.duplicate)
        _links.duplicate();
    const std::vector<fla::Element*> elements = _links.elements ? _links.elements() : std::vector<fla::Element*>();
    Decomposed d;
    d.scaleX = _scaleX->value();
    d.scaleY = _scaleY->value();
    if (_rotateChoice->isChecked())
        d.skewX = d.skewY = _rotation->value();
    else
    {
        d.skewX = _skewX->value();
        d.skewY = _skewY->value();
    }
    transformAll(_links, elements, "Transform", [&](fla::Element* element, const QRectF&) {
        // About the transformation point, which stays where it is.
        const fla::Transform& t = element->transform;
        const fla::Point& p = element->transformationPoint;
        const double px = t.m11 * p.x + t.m21 * p.y + t.tx;
        const double py = t.m12 * p.x + t.m22 * p.y + t.ty;
        fla::Transform after = compose(d, 0.0, 0.0);
        after.tx = px - (after.m11 * p.x + after.m21 * p.y);
        after.ty = py - (after.m12 * p.x + after.m22 * p.y);
        return after;
    });
}

// ---------------------------------------------------------------------------
// Info

InfoPanel::InfoPanel(ArrangeLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    _size = new QLabel(this);
    _place = new QLabel(this);
    _cursor = new QLabel(this);
    _color = new QLabel(this);
    _swatch = new QLabel(this);
    _swatch->setFixedSize(18, 18);
    QHBoxLayout* colorRow = new QHBoxLayout();
    colorRow->addWidget(_swatch);
    colorRow->addWidget(_color, 1);
    QFormLayout* form = new QFormLayout();
    form->addRow("Size:", _size);
    form->addRow("Position:", _place);
    form->addRow("Cursor:", _cursor);
    form->addRow("Color:", colorRow);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addLayout(form);
    layout->addStretch();
    refresh();
}

void InfoPanel::refresh()
{
    const std::vector<fla::Element*> elements = _links.elements ? _links.elements() : std::vector<fla::Element*>();
    QRectF bounds;
    for (fla::Element* element : elements)
    {
        const QRectF b = _links.view ? _links.view->elementBounds(element) : QRectF();
        if (b.isValid())
            bounds = bounds.isValid() ? bounds.united(b) : b;
    }
    if (!bounds.isValid())
    {
        _size->setText(QString::fromUtf8("—"));
        _place->setText(QString::fromUtf8("—"));
        return;
    }
    _size->setText(QString("W %1  H %2").arg(bounds.width(), 0, 'f', 2).arg(bounds.height(), 0, 'f', 2));
    _place->setText(QString("X %1  Y %2").arg(bounds.left(), 0, 'f', 2).arg(bounds.top(), 0, 'f', 2));
}

void InfoPanel::cursorMoved(const QPointF& documentPos, const QPoint& widgetPos)
{
    if (!isVisible())
        return;
    _cursor->setText(QString("X %1  Y %2").arg(documentPos.x(), 0, 'f', 1).arg(documentPos.y(), 0, 'f', 1));
    if (!_links.view)
        return;
    // The color under the cursor, as drawn on screen, a few times a second.
    static QElapsedTimer since;
    if (since.isValid() && since.elapsed() < 120)
        return;
    since.start();
    const QImage pixel = _links.view->grab(QRect(widgetPos, QSize(1, 1))).toImage();
    if (pixel.isNull())
        return;
    const QColor color = pixel.pixelColor(0, 0);
    _color->setText(QString("R %1  G %2  B %3  %4").arg(color.red()).arg(color.green()).arg(color.blue())
                        .arg(color.name().toUpper()));
    _swatch->setStyleSheet(QString("background: %1; border: 1px solid #777;").arg(color.name()));
}
