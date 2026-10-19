#include "tool_hint_label.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QSettings>
#include <QTimer>

namespace {

const char* const kSetting = "view/toolHints";

} // namespace

ToolHintLabel::ToolHintLabel(QWidget* parent)
    : QLabel(parent)
    , _on(enabledSetting())
{
    setObjectName("toolHint");
    setTextFormat(Qt::PlainText);
    // Long hints are cut short rather than widening the window.
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setContentsMargins(4, 0, 4, 0);
    // Modifier keys are seen wherever the focus is.
    if (qApp)
        qApp->installEventFilter(this);
}

void ToolHintLabel::setTool(const QString& name)
{
    _tool = name;
    refresh();
}

void ToolHintLabel::setContext(std::function<fla::hints::Context()> context)
{
    _context = std::move(context);
    refresh();
}

bool ToolHintLabel::enabledSetting()
{
    return QSettings().value(kSetting, true).toBool();
}

void ToolHintLabel::setEnabledSetting(bool on)
{
    QSettings().setValue(kSetting, on);
}

void ToolHintLabel::setHintsOn(bool on)
{
    _on = on;
    refresh();
}

void ToolHintLabel::refresh()
{
    _pending = false;
    if (!_on || _tool.isEmpty())
    {
        clear();
        setToolTip(QString());
        return;
    }
    const Qt::KeyboardModifiers keys = QGuiApplication::queryKeyboardModifiers();
    fla::hints::Modifiers held;
    held.shift = keys & Qt::ShiftModifier;
    held.alt = keys & Qt::AltModifier;
    held.ctrl = keys & Qt::ControlModifier;
    const QString hint = QString::fromStdString(fla::hints::hintFor(
        _tool.toStdString(), held, _context ? _context() : fla::hints::Context()));
    setText(hint);
    setToolTip(hint);
}

bool ToolHintLabel::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
    case QEvent::ApplicationStateChange:
    case QEvent::WindowActivate:
        // Once the key has landed, so the keys read are the ones held now;
        // one refresh for however many widgets pass the event along.
        if (_on && !_pending)
        {
            _pending = true;
            QTimer::singleShot(0, this, [this]() { refresh(); });
        }
        break;
    default:
        break;
    }
    return QLabel::eventFilter(watched, event);
}
