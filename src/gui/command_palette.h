#pragma once

#include <QFrame>
#include <QString>
#include <QStringList>

#include <vector>

#include "shortcuts_dialog.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;

/// Find Command (Help menu, Ctrl+Shift+P): a box over the window that finds
/// any menu command or tool by typing part of its name, and runs it. The
/// commands are those the keyboard shortcuts know (Shortcuts::collect), each
/// shown with where it lives in the menus and its key. With nothing typed it
/// lists the commands run from it last. It does not block, so it works the
/// same in the web editor.
class CommandPalette : public QFrame
{
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* window);

    /// Opens it over the top of \a window with \a commands to choose from.
    void open(const std::vector<ShortcutEntry>& commands);

    /// How well \a query matches a command named \a name found under
    /// \a path ("Modify > Transform"): higher is better, below 0 no match.
    /// Every word of the query must be in the name or the path, or the
    /// query's letters must come in order in the name.
    static int score(const QString& query, const QString& name, const QString& path);

    /// The ids (ShortcutEntry::id) of the commands run from it, the last
    /// first, kept in the settings.
    static QStringList recent();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refill();
    void run(QListWidgetItem* item);
    void place();

    QLineEdit* _search = nullptr;
    QListWidget* _list = nullptr;
    std::vector<ShortcutEntry> _commands;
};
