#include "hot_text.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDialog>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <cmath>

HotText::HotText(QWidget* parent)
    : QWidget(parent)
    , _editor(new QLineEdit(this))
{
    setCursor(Qt::SizeHorCursor);
    // Tab reaches it, as it did a spin box; the wheel only steps it once it
    // has focus, so scrolling a panel never changes a number by accident.
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
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
    _maximum = std::max(minimum, maximum);
    _value = clamp(_value);
    update();
}

void HotText::setDecimals(int decimals)
{
    _decimals = std::max(0, decimals);
    _value = clamp(_value);
    updateGeometry();
    update();
}

void HotText::setSuffix(const QString& suffix)
{
    _suffix = suffix;
    updateGeometry();
    update();
}

void HotText::setPrefix(const QString& prefix)
{
    _prefix = prefix;
    updateGeometry();
    update();
}

void HotText::setStep(double perPixel)
{
    _step = perPixel;
    _stepSet = true;
}

void HotText::setSingleStep(double step)
{
    _singleStep = step > 0.0 ? step : 1.0;
    if (!_stepSet)
        _step = _singleStep;
}

void HotText::setPlaceholder(const QString& text)
{
    _placeholder = text;
    update();
}

void HotText::setSpecialValueText(const QString& text)
{
    _specialValueText = text;
    updateGeometry();
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

void HotText::reportValue(double value)
{
    const double next = clamp(value);
    if (next == _value && !_empty)
        return;
    _value = next;
    _empty = false;
    update();
    emit valueChanged(_value, false);
}

void HotText::selectAll()
{
    if (!_editorOpen)
        openEditor();
    else
        _editor->selectAll();
}

double HotText::clamp(double value) const
{
    if (!std::isfinite(value))
        value = _minimum > 0.0 ? _minimum : 0.0;
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
    if (!_specialValueText.isEmpty() && _value == _minimum)
        return _specialValueText;
    return _prefix + QString::number(_value, 'f', _decimals) + _suffix;
}

QSize HotText::sizeHint() const
{
    const QFontMetrics metrics(font());
    // Room for a typical value, not the widest possible one, so a row of
    // fields keeps to a sensible width; the editor needs a little more.
    const QString typical = _prefix + QString("-8888") + (_decimals > 0 ? "." + QString(_decimals, '8') : QString()) + _suffix;
    int width = std::max(metrics.horizontalAdvance(typical), metrics.horizontalAdvance(_specialValueText));
    width = std::max(width + 12, 48);
    return QSize(width, metrics.height() + 8);
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
    const QColor color = isEnabled() ? theme::color("hotText") : palette().color(QPalette::Disabled, QPalette::Text);

    // Keyboard focus shows as a faint box, so Tab order can be followed.
    if (hasFocus())
    {
        QColor ring = color;
        ring.setAlpha(110);
        painter.setPen(QPen(ring, 1.0));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
    }

    painter.setPen(color);
    const QString shown = text();
    const QRect area = rect().adjusted(4, 0, -4, 0);
    painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter, shown);

    // The dotted underline is what marks it as draggable.
    const QFontMetrics metrics(font());
    const int textWidth = std::min(metrics.horizontalAdvance(shown), area.width());
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

void HotText::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    // Tabbing in opens the number for typing, as a spin box takes typing.
    if (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason)
        openEditor();
    update();
}

void HotText::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange && !isEnabled() && _editorOpen)
        closeEditor(false);
}

void HotText::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Up:
        stepBy(1);
        return;
    case Qt::Key_Down:
        stepBy(-1);
        return;
    case Qt::Key_PageUp:
        stepBy(10);
        return;
    case Qt::Key_PageDown:
        stepBy(-10);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // In a dialog Enter is for its default button, as it is from a
        // spin box; F2 still opens the number for typing.
        if (passToDialog(event))
            return;
        openEditor();
        return;
    case Qt::Key_F2:
        openEditor();
        return;
    default:
        break;
    }
    // A digit or a sign starts typing a new value.
    const QString typed = event->text();
    if (!typed.isEmpty() && (typed.at(0).isDigit() || typed == "-" || typed == "." || typed == "+"))
    {
        openEditor();
        _editor->setText(typed);
        return;
    }
    QWidget::keyPressEvent(event);
}

void HotText::wheelEvent(QWheelEvent* event)
{
    if (!hasFocus() && !_editorOpen)
    {
        event->ignore();   // let the panel scroll
        return;
    }
    const int notches = event->angleDelta().y() / 120;
    if (notches != 0)
        stepBy(notches);
    event->accept();
}

void HotText::stepBy(int steps)
{
    if (!isEnabled() || steps == 0)
        return;
    if (_editorOpen)
    {
        // Step the number being typed, and keep typing.
        bool ok = false;
        const double typed = _editor->text().trimmed().toDouble(&ok);
        const double next = clamp((ok ? typed : _value) + steps * _singleStep);
        _editor->setText(QString::number(next, 'f', _decimals));
        _editor->selectAll();
        return;
    }
    emit editStarted();
    reportValue((_empty ? 0.0 : _value) + steps * _singleStep);
    emit editFinished();
}

bool HotText::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _editor && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        switch (key->key())
        {
        case Qt::Key_Escape:
            closeEditor(false);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // The value typed is kept, and in a dialog Enter goes on to its
            // default button.
            closeEditor(true);
            passToDialog(key);
            return true;
        case Qt::Key_Tab:
            // Commit, then move on from this field, not from its editor
            // (which would land back on the field and open it again).
            closeEditor(true);
            focusNextChild();
            return true;
        case Qt::Key_Backtab:
            closeEditor(true);
            focusPreviousChild();
            return true;
        case Qt::Key_Up:
            stepBy(1);
            return true;
        case Qt::Key_Down:
            stepBy(-1);
            return true;
        case Qt::Key_PageUp:
            stepBy(10);
            return true;
        case Qt::Key_PageDown:
            stepBy(-10);
            return true;
        default:
            break;
        }
    }
    if (watched == _editor && event->type() == QEvent::Wheel)
    {
        wheelEvent(static_cast<QWheelEvent*>(event));
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

bool HotText::passToDialog(QKeyEvent* key)
{
    QDialog* dialog = qobject_cast<QDialog*>(window());
    if (!dialog)
        return false;
    QKeyEvent copy(key->type(), key->key(), key->modifiers(), key->text());
    QCoreApplication::sendEvent(dialog, &copy);
    return true;
}

void HotText::openEditor()
{
    if (!isEnabled())
        return;
    if (_editorOpen)
    {
        _editor->selectAll();
        return;
    }
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
    // Hiding the editor while it has focus would make Qt move focus on to
    // the next field (and open that one); keep it here instead.
    if (_editor->hasFocus())
        setFocus(Qt::OtherFocusReason);
    _editor->hide();
    update();

    if (commit)
    {
        // A prefix or suffix typed along with the number is fine.
        QString typedText = _editor->text().trimmed();
        if (!_suffix.isEmpty() && typedText.endsWith(_suffix.trimmed()))
            typedText.chop(_suffix.trimmed().size());
        if (!_prefix.isEmpty() && typedText.startsWith(_prefix.trimmed()))
            typedText.remove(0, _prefix.trimmed().size());
        bool ok = false;
        const double typed = typedText.trimmed().toDouble(&ok);
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

HotSpinBox::HotSpinBox(QWidget* parent)
    : HotText(parent)
{
    setDecimals(0);
    HotText::setRange(0, 99);
    HotText::setSingleStep(1);
    // Whole numbers scrub a little slower than one per pixel, so a value
    // can be hit exactly.
    setStep(0.5);
}

HotDoubleSpinBox::HotDoubleSpinBox(QWidget* parent)
    : HotText(parent)
{
    setDecimals(2);
    HotText::setRange(0.0, 99.99);
    HotText::setSingleStep(1.0);
}
