#pragma once

#include "../edit/tool_hints.h"

#include <QLabel>

#include <functional>

/// The status bar's hint for the active tool: what a click or a drag does
/// and what each modifier key adds. It follows the keys as they are pressed
/// and let go, showing just what the keys held do. The text comes from
/// fla::hints; the setting that turns it off is the person's, kept in the
/// application's settings.
class ToolHintLabel : public QLabel
{
public:
    explicit ToolHintLabel(QWidget* parent = nullptr);

    /// The tool, by the name the toolbox shows; one with no hint shows
    /// nothing.
    void setTool(const QString& name);

    /// What else the hint depends on, asked each time it is shown.
    void setContext(std::function<fla::hints::Context()> context);

    /// Whether hints are shown: View > Tool Hints.
    static bool enabledSetting();
    static void setEnabledSetting(bool on);
    void setHintsOn(bool on);

    /// Shows the hint again, for a change the label does not see.
    void refresh();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString _tool;
    std::function<fla::hints::Context()> _context;
    bool _on = true;
    bool _pending = false;
};
