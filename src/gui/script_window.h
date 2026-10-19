#pragma once

#include "code_editor.h"

#include <QMainWindow>

#include <functional>

class QLabel;
class QTabWidget;

/// Commands > Script Editor: script files (JSFL, Python, ActionScript) in
/// tabs, written, saved, and run on the open document (Ctrl+Enter), where
/// what they trace and the errors they stop on go to the Output panel.
class ScriptWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// \a run runs a saved script file; \a folder is where scripts are
    /// opened from and saved to at first (the Commands folder).
    ScriptWindow(std::function<bool(const QString&)> run, const QString& folder, QWidget* parent = nullptr);

    void newScript(CodeEditor::Language language);
    /// Opens \a path, or shows its tab when it is open already.
    bool openFile(const QString& path);
    void askOpen();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    CodeView* current() const;
    QString pathOf(int tab) const;
    void setPath(int tab, const QString& path);
    void updateTitle(int tab);
    /// Saves \a tab, asking for a name when it has none or \a askForName,
    /// then calls \a saved; not at all when it is not saved.
    void save(int tab, bool askForName, std::function<void()> saved = {});
    bool writeScript(CodeView* view, const QString& path);
    /// Closes \a tab, asking first to save changes, then calls \a closed;
    /// not at all when the tab is kept open.
    void closeTab(int tab, std::function<void()> closed = {});
    /// Closes the tabs from \a from to the first, then calls \a closed;
    /// not at all when one is kept open.
    void closeTabs(int from, std::function<void()> closed);
    void run();
    void check();

    std::function<bool(const QString&)> _run;
    QString _folder;
    QTabWidget* _tabs = nullptr;
    QLabel* _status = nullptr;
    int _untitled = 0;
};
