// The terminal on the web: a browser runs no programs, so the panel that
// would hold one (the Claude Code panel) says so instead. The desktop's is
// terminal_view.cpp; this replaces it in the web build.

#include "terminal_view.h"

#include <QPainter>
#include <QPalette>

TerminalView::TerminalView(QWidget* parent)
    : QWidget(parent)
{
}

TerminalView::~TerminalView() = default;

bool TerminalView::start(const QString&, const QStringList&, const QString&, QString& error)
{
    error = "Phoenix in a browser cannot run other programs.";
    return false;
}

bool TerminalView::isRunning() const
{
    return false;
}

void TerminalView::stop()
{
}

void TerminalView::showText(const QString& text)
{
    _webText = text;
    update();
}

void TerminalView::clear()
{
    _webText.clear();
    update();
}

QSize TerminalView::sizeHint() const
{
    return QSize(480, 320);
}

bool TerminalView::event(QEvent* event)
{
    return QWidget::event(event);
}

void TerminalView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
        _webText.isEmpty() ? QString("Phoenix in a browser cannot run other programs.") : _webText);
}

void TerminalView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

void TerminalView::keyPressEvent(QKeyEvent* event)
{
    QWidget::keyPressEvent(event);
}

void TerminalView::inputMethodEvent(QInputMethodEvent* event)
{
    QWidget::inputMethodEvent(event);
}

void TerminalView::wheelEvent(QWheelEvent* event)
{
    QWidget::wheelEvent(event);
}

void TerminalView::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);
}

void TerminalView::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
}

void TerminalView::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent* event)
{
    QWidget::mouseDoubleClickEvent(event);
}

void TerminalView::contextMenuEvent(QContextMenuEvent* event)
{
    QWidget::contextMenuEvent(event);
}

void TerminalView::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
}

void TerminalView::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
}
