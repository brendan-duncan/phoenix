#pragma once

#include <QString>

class McpServer;
class QWidget;

/// Commands > Open Claude Code: Claude Code, Anthropic's assistant for the
/// terminal, started in a terminal window of its own and already connected
/// to this Phoenix through MCP. Phoenix only starts it: what is typed, what
/// it answers and signing in all happen in the terminal.
namespace claude_code {

/// Where the claude command is installed, or empty when it is not found.
QString find();

/// Opens a terminal in \a folder running Claude Code with this Phoenix's
/// tools, the client let in without asking (McpServer::openSession). Says
/// what to do when Claude Code is not installed or no terminal opens.
void open(McpServer& server, const QString& folder, QWidget* parent);

} // namespace claude_code
