#pragma once

#include <QString>

class McpServer;
class QWidget;

/// Commands > Open Claude Code: Claude Code, Anthropic's assistant for the
/// terminal, started in the Claude Code panel (ClaudeCodePanel) or a
/// terminal window of its own, already connected to this Phoenix through
/// MCP. Phoenix only starts it: what is typed, what
/// it answers and signing in all happen in the terminal.
namespace claude_code {

/// Where the claude command is installed, or empty when it is not found.
QString find();

/// Gets ready to start Claude Code with this Phoenix's tools: finds the
/// claude command and writes the MCP configuration that lets it in without
/// asking (McpServer::openSession). False, having said what to do, when
/// Claude Code is not installed or Phoenix cannot get ready.
bool prepare(McpServer& server, QWidget* parent, QString& claude, QString& config);

/// Where Claude Code starts: \a folder when there is one, else home.
QString startFolder(const QString& folder);

/// Opens a terminal window in \a folder running Claude Code with this
/// Phoenix's tools. Says what to do when no terminal opens.
void open(McpServer& server, const QString& folder, QWidget* parent);

} // namespace claude_code
