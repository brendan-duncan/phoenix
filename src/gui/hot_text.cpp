#include "hot_text.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <cmath>

namespace {

/// Animate's hot-text blue, legible on the dark and the light theme.
const QColor kHotColor(86, 156, 255);

} // namespace

HotText::HotText(QWidget* parent)
    : QWidget(parent)
    , _editor(new QLineEdit(this))
{
    setCursor(Qt::SizeHorCursor);
    setFocusPolicy(Qt::ClickFocus);
    _editor->hide();
    _editor->setFrame(true);
    _editor->installEventFilter(this);
    connect(_editor, &QLineEdit::editingFinished, this, [this]() {
        if (_editorOpen)
            closeEditor(true);
    });
}

void HotText::setRange(double minimum, double maximum)
{
    _minimum = minimum;
    _maximum = maximum;
    _value = clamp(_value);
    update();
}

void HotText::setDecimals(int decimals)
{
    _decimals = decimals;
    update();
}

void HotText::setSuffix(const QString& suffix)
{
    _suffix = suffix;
    updateGeometry();
    update();
}

void HotText::setStep(double perPixel)
{
    _step = perPixel;
}

void HotText::setPlaceholder(const QString& text)
{
    _placeholder = text;
    update();
}

void HotText::setValue(double value)
{
    _value = clamp(value);
    _empty = false;
    update();
}

void HotText::setEmpty(bool empty)
{
    _empty = empty;
    update();
}

double HotText::clamp(double value) const
{
    const double scale = std::pow(10.0, _decimals);
    value = std::round(value * scale) / scale;
    if (value < _minimum)
        value = _minimum;
    if (value > _maximum)
        value = _maximum;
    // No negative zero in the display.
    if (value == 0.0)
        value = 0.0;
    return value;
}

QString HotText::text() const
{
    if (_empty)
        return _placeholder;
    return QString::number(_value, 'f', _decimals) + _suffix;
}

QSize HotText::sizeHint() const
{
    const QFontMetrics metrics(font());
    // Room for a typical value, not the widest possible one, so a row of
    // fields keeps to a sensible width.
    const int width = metrics.horizontalAdvance(QString("-8888.") + QString(_decimals, '8') + _suffix) + 8;
    return QSize(width, metrics.height() + 6);
}

QSize HotText::minimumSizeHint() const
{
    return sizeHint();
}

void HotText::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (_editorOpen)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const QColor color = isEnabled() ? kHotColor : palette().color(QPalette::Disabled, QPalette::Text);
    painter.setPen(color);

    const QString shown = text();
    const QRect area = rect().adjusted(4, 0, -4, 0);
    painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter, shown);

    // The dotted underline is what marks it as draggable.
    const QFontMetrics metrics(font());
    const int textWidth = metrics.horizontalAdvance(shown);
    const int baseline = area.center().y() + metrics.ascent() / 2 + 2;
    QPen pen(color, 1.0, Qt::DotLine);
    painter.setPen(pen);
    painter.drawLine(area.left(), baseline, area.left() + textWidth, baseline);
}

void HotText::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !isEnabled())
    {
        QWidget::mousePressEvent(event);
        return;
    }
    _pressed = true;
    _dragging = false;
    _pressPos = event->pos();
    _pressValue = _empty ? 0.0 : _value;
}

void HotText::mouseMoveEvent(QMouseEvent* event)
{
    if (!_pressed)
        return;

    const int dx = event->pos().x() - _pressPos.x();
    if (!_dragging)
    {
        if (std::abs(dx) < 3)
            return;
        _dragging = true;
        emit editStarted();
    }

    double step = _step;
    if (event->modifiers() & Qt::ShiftModifier)
        step *= 10.0;
    if (event->modifiers() & Qt::ControlModifier)
        step *= 0.1;

    const double next = clamp(_pressValue + dx * step);
    if (next == _value && !_empty)
        return;
    _value = next;
    _empty = false;
    update();
    emit valueChanged(_value, true);
}

void HotText::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !_pressed)
        return;
    _pressed = false;
    if (_dragging)
    {
        _dragging = false;
        emit editFinished();
    }
    else
    {
        openEditor();
    }
}

void HotText::mouseDoubleClickEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    if (!_editorOpen)
        openEditor();
}

void HotText::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    _editor->setGeometry(rect());
}

bool HotText::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _editor && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape)
        {
            closeEditor(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HotText::openEditor()
{
    _editorOpen = true;
    _editor->setGeometry(rect());
    _editor->setText(_empty ? QString() : QString::number(_value, 'f', _decimals));
    _editor->show();
    _editor->selectAll();
    _editor->setFocus();
    update();
    emit editStarted();
}

void HotText::closeEditor(bool commit)
{
    if (!_editorOpen)
        return;
    _editorOpen = false;
    _editor->hide();
    update();

    if (commit)
    {
        bool ok = false;
        const double typed = _editor->text().trimmed().toDouble(&ok);
        if (ok)
        {
            const double next = clamp(typed);
            if (next != _value || _empty)
            {
                _value = next;
                _empty = false;
                update();
                emit valueChanged(_value, false);
            }
        }
    }
    emit editFinished();
}
