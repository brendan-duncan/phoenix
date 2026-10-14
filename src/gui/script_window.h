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
    bool save(int tab, bool askForName);
    bool closeTab(int tab);
    void run();
    void check();

    std::function<bool(const QString&)> _run;
    QString _folder;
    QTabWidget* _tabs = nullptr;
    QLabel* _status = nullptr;
    int _untitled = 0;
};
