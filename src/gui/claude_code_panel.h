#pragma once

#include <QString>
#include <QWidget>

class McpServer;
class QLabel;
class QToolButton;
class TerminalView;

/// The Claude Code panel: Claude Code running in a terminal docked in
/// Phoenix, connected to this Phoenix through MCP (see claude_code.h). What
/// is typed, what it answers and signing in all happen in the terminal;
/// Phoenix only starts it and shows it.
class ClaudeCodePanel : public QWidget
{
    Q_OBJECT

public:
    ClaudeCodePanel(McpServer& server, QWidget* parent = nullptr);

    /// Starts Claude Code in \a folder (home when there is none), unless it
    /// runs already. False when it cannot, having said why.
    bool start(const QString& folder);
    bool isRunning() const;
    /// Ends Claude Code, and starts it again in the folder it was in.
    void restart();
    void focusTerminal();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ended(int exitCode);

    McpServer& _server;
    TerminalView* _terminal = nullptr;
    QLabel* _status = nullptr;
    QToolButton* _restart = nullptr;
    QString _folder;
};
