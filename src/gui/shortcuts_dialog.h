#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QString>

#include <map>
#include <vector>

class QAction;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QComboBox;
class QMenuBar;
class QTreeWidget;
class QTreeWidgetItem;

/// Every command a key can call: its place ("Edit > Undo", "Tools >
/// Pen"), its action, and the key Phoenix gives it.
struct ShortcutEntry
{
    QString id;
    QAction* action = nullptr;
    QKeySequence defaultKey;
};

/// Keyboard shortcuts: the set chosen, kept in the settings by command, and
/// named sets saved beside it.
namespace Shortcuts {

/// The menu bar's commands, each menu's in order, then \a tools under
/// "Tools", with their keys as they stand now (the defaults).
std::vector<ShortcutEntry> collect(const QMenuBar* menuBar, const std::vector<QAction*>& tools);

/// The keys the current set gives, by command; a command left out keeps
/// its default.
std::map<QString, QKeySequence> currentSet();
void setCurrentSet(const std::map<QString, QKeySequence>& keys);

/// Puts the current set's keys on the actions.
void apply(const std::vector<ShortcutEntry>& entries);

/// A set as JSON (command to key, in portable text), and read back.
QByteArray toJson(const std::map<QString, QKeySequence>& keys);
std::map<QString, QKeySequence> fromJson(const QByteArray& json);

} // namespace Shortcuts

/// Edit > Keyboard Shortcuts: every command and its key, searchable by
/// name or key; a key already taken is pointed out and can be moved; named
/// sets saved, switched, exported and imported; Reset gives Phoenix's own.
class ShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    ShortcutsDialog(const std::vector<ShortcutEntry>& entries, QWidget* parent = nullptr);

    /// The keys chosen, by command, differing from the defaults.
    std::map<QString, QKeySequence> chosen() const;

private:
    void fill();
    void filter(const QString& text);
    void edit(QTreeWidgetItem* item);
    QKeySequence keyOf(const QString& id) const;
    QString holderOf(const QKeySequence& key, const QString& except) const;
    void refreshSets();

    std::vector<ShortcutEntry> _entries;
    std::map<QString, QKeySequence> _keys;
    QComboBox* _sets;
    QLineEdit* _search;
    QTreeWidget* _tree;
    QKeySequenceEdit* _editor;
    QLabel* _conflict;
    QString _editing;
};
