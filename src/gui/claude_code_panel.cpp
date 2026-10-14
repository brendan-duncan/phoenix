#include "claude_code_panel.h"

#include "claude_code.h"
#include "terminal_view.h"

#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

ClaudeCodePanel::ClaudeCodePanel(McpServer& server, QWidget* parent)
    : QWidget(parent)
    , _server(server)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 2, 2, 2);
    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bar->addWidget(_status, 1);
    _restart = new QToolButton(this);
    _restart->setText("Restart");
    _restart->setToolTip("End Claude Code and start it again, connected to this Phoenix");
    _restart->setAutoRaise(true);
    connect(_restart, &QToolButton::clicked, this, &ClaudeCodePanel::restart);
    bar->addWidget(_restart);
    layout->addLayout(bar);

    _terminal = new TerminalView(this);
    _terminal->installEventFilter(this);
    layout->addWidget(_terminal, 1);
    connect(_terminal, &TerminalView::finished, this, &ClaudeCodePanel::ended);
    setFocusProxy(_terminal);
}

bool ClaudeCodePanel::start(const QString& folder)
{
    if (_terminal->isRunning())
        return true;
    QString claude, config;
    if (!claude_code::prepare(_server, window(), claude, config))
        return false;
    _folder = claude_code::startFolder(folder);
    QString error;
    if (!_terminal->start(claude, {"--mcp-config", config}, _folder, error))
    {
        _terminal->showText(QString("\n%1\n").arg(error));
        _status->setText("Not running");
        return false;
    }
    _status->setText(QDir::toNativeSeparators(_folder));
    _status->setToolTip(QString("Claude Code is running in %1, connected to this Phoenix")
                            .arg(QDir::toNativeSeparators(_folder)));
    return true;
}

bool ClaudeCodePanel::isRunning() const
{
    return _terminal->isRunning();
}

void ClaudeCodePanel::restart()
{
    const QString folder = _folder;
    _terminal->stop();
    _terminal->clear();
    start(folder);
    focusTerminal();
}

void ClaudeCodePanel::focusTerminal()
{
    _terminal->setFocus(Qt::OtherFocusReason);
}

void ClaudeCodePanel::ended(int)
{
    _status->setText("Not running");
    _terminal->showText("\n\x1b[2mClaude Code has ended. Press Enter to start it again.\x1b[0m\n");
}

bool ClaudeCodePanel::eventFilter(QObject* watched, QEvent* event)
{
    // Once Claude Code has ended, Enter starts it again.
    if (watched == _terminal && event->type() == QEvent::KeyPress && !_terminal->isRunning())
    {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter)
        {
            restart();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
