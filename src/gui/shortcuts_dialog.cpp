#include "shortcuts_dialog.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "../edit/shortcut_sets.h"

#include <functional>
#include <set>

namespace Shortcuts {

namespace {

QString plain(QString text)
{
    text.remove('&');
    text.replace(QString::fromUtf8("\xE2\x80\xA6"), "...");
    if (text.endsWith("..."))
        text.chop(3);
    return text.trimmed();
}

std::map<QString, QKeySequence> read(const QString& group)
{
    std::map<QString, QKeySequence> keys;
    QSettings settings;
    settings.beginGroup(group);
    for (const QString& id : settings.childKeys())
        keys[QString(id).replace('|', '/')] = QKeySequence(settings.value(id).toString(), QKeySequence::PortableText);
    return keys;
}

void write(const QString& group, const std::map<QString, QKeySequence>& keys)
{
    QSettings settings;
    settings.remove(group);
    settings.beginGroup(group);
    // A "/" would start a subgroup.
    for (const auto& [id, key] : keys)
        settings.setValue(QString(id).replace('/', '|'), key.toString(QKeySequence::PortableText));
}

} // namespace

std::vector<ShortcutEntry> collect(const QMenuBar* menuBar, const std::vector<QAction*>& tools)
{
    std::vector<ShortcutEntry> entries;
    std::function<void(const QMenu*, const QString&)> walk = [&](const QMenu* menu, const QString& path) {
        for (QAction* action : menu->actions())
        {
            if (action->isSeparator() || action->text().isEmpty())
                continue;
            const QString id = path + " > " + plain(action->text());
            if (QMenu* sub = action->menu())
            {
                // Menus that list files or scenes change as they are used.
                const QString title = plain(action->text());
                if (title != "Open Recent" && title != "Scene" && title != "Go to Scene")
                    walk(sub, id);
                continue;
            }
            // Lists the menu fills as it opens (recent files, scenes) are
            // left out: they come and go.
            if (std::any_of(entries.begin(), entries.end(), [&id](const ShortcutEntry& e) { return e.id == id; }))
                continue;
            entries.push_back({id, action, action->shortcut()});
        }
    };
    for (QAction* top : menuBar->actions())
        if (top->menu())
            walk(top->menu(), plain(top->text()));
    for (QAction* tool : tools)
        if (tool)
            entries.push_back({"Tools > " + plain(tool->text()), tool, tool->shortcut()});
    return entries;
}

std::map<QString, QKeySequence> currentSet()
{
    return read("shortcuts/current");
}

void setCurrentSet(const std::map<QString, QKeySequence>& keys)
{
    write("shortcuts/current", keys);
}

void apply(const std::vector<ShortcutEntry>& entries)
{
    const std::map<QString, QKeySequence> keys = currentSet();
    for (const ShortcutEntry& entry : entries)
    {
        const auto found = keys.find(entry.id);
        entry.action->setShortcut(found != keys.end() ? found->second : entry.defaultKey);
    }
}

QByteArray toJson(const std::map<QString, QKeySequence>& keys)
{
    QJsonObject object;
    for (const auto& [id, key] : keys)
        object[id] = key.toString(QKeySequence::PortableText);
    return QJsonDocument(object).toJson();
}

std::map<QString, QKeySequence> fromJson(const QByteArray& json)
{
    std::map<QString, QKeySequence> keys;
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    for (auto it = object.begin(); it != object.end(); ++it)
        keys[it.key()] = QKeySequence(it.value().toString(), QKeySequence::PortableText);
    return keys;
}

} // namespace Shortcuts

ShortcutsDialog::ShortcutsDialog(const std::vector<ShortcutEntry>& entries, QWidget* parent)
    : QDialog(parent)
    , _entries(entries)
{
    setWindowTitle("Keyboard Shortcuts");
    for (const auto& [id, key] : Shortcuts::currentSet())
        _keys[id] = key;
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* setRow = new QHBoxLayout();
    setRow->addWidget(new QLabel("Set", this));
    _sets = new QComboBox(this);
    setRow->addWidget(_sets, 1);
    QPushButton* saveAs = new QPushButton("Save As...", this);
    QPushButton* remove = new QPushButton("Delete", this);
    QPushButton* exportSet = new QPushButton("Export...", this);
    QPushButton* importSet = new QPushButton("Import...", this);
    for (QPushButton* button : {saveAs, remove, exportSet, importSet})
        setRow->addWidget(button);
    layout->addLayout(setRow);

    _search = new QLineEdit(this);
    _search->setPlaceholderText("Search commands or keys");
    _search->setClearButtonEnabled(true);
    layout->addWidget(_search);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(2);
    _tree->setHeaderLabels({"Command", "Shortcut"});
    _tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _tree->setRootIsDecorated(false);
    _tree->setAlternatingRowColors(true);
    layout->addWidget(_tree, 1);

    QHBoxLayout* editRow = new QHBoxLayout();
    editRow->addWidget(new QLabel("Shortcut", this));
    _editor = new QKeySequenceEdit(this);
    _editor->setEnabled(false);
    editRow->addWidget(_editor, 1);
    QPushButton* clear = new QPushButton("Clear", this);
    QPushButton* reset = new QPushButton("Default", this);
    editRow->addWidget(clear);
    editRow->addWidget(reset);
    layout->addLayout(editRow);
    _conflict = new QLabel(this);
    _conflict->setWordWrap(true);
    layout->addWidget(_conflict);

    QHBoxLayout* bottom = new QHBoxLayout();
    QPushButton* resetAll = new QPushButton("Reset All", this);
    resetAll->setToolTip("Phoenix's own keys for every command");
    bottom->addWidget(resetAll);
    bottom->addStretch(1);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(_search, &QLineEdit::textChanged, this, &ShortcutsDialog::filter);
    connect(_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { edit(item); });
    const auto setKey = [this](const QKeySequence& key) {
        if (_editing.isEmpty())
            return;
        // A key moves: whatever held it lets it go.
        const QString holder = key.isEmpty() ? QString() : holderOf(key, _editing);
        if (!holder.isEmpty())
            _keys[holder] = QKeySequence();
        _keys[_editing] = key;
        _conflict->setText(holder.isEmpty() ? QString()
                                            : QString("%1 was taken from %2.").arg(key.toString(QKeySequence::NativeText), holder));
        fill();
    };
    connect(_editor, &QKeySequenceEdit::editingFinished, this, [this, setKey]() {
        // One chord at a time.
        const QKeySequence typed = _editor->keySequence();
        setKey(typed.isEmpty() ? QKeySequence() : QKeySequence(typed[0]));
    });
    connect(clear, &QPushButton::clicked, this, [setKey]() { setKey(QKeySequence()); });
    connect(reset, &QPushButton::clicked, this, [this]() {
        if (_editing.isEmpty())
            return;
        _keys.erase(_editing);
        _conflict->clear();
        fill();
    });
    connect(resetAll, &QPushButton::clicked, this, [this]() {
        _keys.clear();
        _conflict->clear();
        fill();
    });

    connect(_sets, &QComboBox::activated, this, [this](int index) {
        if (index <= 0)
        {
            _keys.clear();
        }
        else if (index == 1)
        {
            // Animate's own keys, on the commands Phoenix has.
            std::vector<std::pair<std::string, std::string>> bindings;
            for (const fla::shortcutsets::Binding& binding : fla::shortcutsets::animateStandard())
                bindings.emplace_back(binding.command, binding.key);
            _keys.clear();
            takeBindings(bindings, QString());
            return;
        }
        else
        {
            QSettings settings;
            _keys = Shortcuts::fromJson(settings.value("shortcuts/sets/" + _sets->itemText(index)).toByteArray());
        }
        fill();
    });
    connect(saveAs, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Save Shortcut Set", "Name:", QLineEdit::Normal,
            _sets->currentIndex() > 1 ? _sets->currentText() : QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        QSettings().setValue("shortcuts/sets/" + name, Shortcuts::toJson(chosen()));
        refreshSets();
        _sets->setCurrentText(name);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (_sets->currentIndex() <= 1)
            return;
        QSettings().remove("shortcuts/sets/" + _sets->currentText());
        refreshSets();
    });
    connect(exportSet, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, "Export Shortcuts", "shortcuts.json", "Shortcuts (*.json)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(Shortcuts::toJson(chosen()));
    });
    connect(importSet, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, "Import Shortcuts", QString(),
            "Shortcut sets (*.json *.xml *.txt);;Phoenix sets (*.json);;Animate shortcut files (*.xml *.txt);;All files (*)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QByteArray data = file.readAll();
        // A set Phoenix exported, or one from Animate.
        const std::map<QString, QKeySequence> keys = Shortcuts::fromJson(data);
        if (!keys.empty())
        {
            _keys = keys;
            fill();
            return;
        }
        std::vector<std::pair<std::string, std::string>> bindings;
        for (const fla::shortcutsets::Binding& binding : fla::shortcutsets::read(data.toStdString()))
            bindings.emplace_back(binding.command, binding.key);
        if (bindings.empty())
        {
            QMessageBox::information(this, "Import Shortcuts", "No shortcuts were found in that file.");
            return;
        }
        _keys.clear();
        takeBindings(bindings, QFileInfo(path).fileName());
    });

    refreshSets();
    fill();
    resize(620, 560);
}

void ShortcutsDialog::takeBindings(const std::vector<std::pair<std::string, std::string>>& given, const QString& from)
{
    std::vector<fla::shortcutsets::Binding> bindings;
    for (const auto& [command, key] : given)
        bindings.push_back({command, key});
    std::vector<std::string> ids;
    for (const ShortcutEntry& entry : _entries)
        ids.push_back(entry.id.toStdString());
    std::vector<std::string> unmatched;
    const std::map<std::string, std::string> matched = fla::shortcutsets::match(bindings, ids, &unmatched);
    std::set<QKeySequence> taken;
    for (const auto& [id, key] : matched)
    {
        const QKeySequence sequence(QString::fromStdString(key), QKeySequence::PortableText);
        _keys[QString::fromStdString(id)] = sequence;
        taken.insert(sequence);
    }
    // A key the set gives one command leaves the command that had it.
    for (const ShortcutEntry& entry : _entries)
        if (!matched.count(entry.id.toStdString()) && taken.count(entry.defaultKey))
            _keys[entry.id] = QKeySequence();
    _conflict->clear();
    fill();
    if (from.isEmpty())
        return;
    QString message = QString("%1 shortcuts from %2 were matched to Phoenix's commands.").arg(matched.size()).arg(from);
    if (!unmatched.empty())
    {
        QStringList names;
        for (size_t i = 0; i < unmatched.size() && i < 12; ++i)
            names << QString::fromStdString(unmatched[i]);
        message += QString("\n\n%1 are for commands Phoenix does not have or could not match:\n%2%3")
                       .arg(unmatched.size())
                       .arg(names.join("\n"))
                       .arg(unmatched.size() > 12 ? QString("\n...") : QString());
    }
    QMessageBox::information(this, "Import Shortcuts", message);
}

void ShortcutsDialog::refreshSets()
{
    _sets->clear();
    _sets->addItem("Phoenix Defaults");
    _sets->addItem("Animate Standard");
    QSettings settings;
    settings.beginGroup("shortcuts/sets");
    _sets->addItems(settings.childKeys());
}

QKeySequence ShortcutsDialog::keyOf(const QString& id) const
{
    const auto found = _keys.find(id);
    if (found != _keys.end())
        return found->second;
    for (const ShortcutEntry& entry : _entries)
        if (entry.id == id)
            return entry.defaultKey;
    return QKeySequence();
}

QString ShortcutsDialog::holderOf(const QKeySequence& key, const QString& except) const
{
    for (const ShortcutEntry& entry : _entries)
        if (entry.id != except && !key.isEmpty() && keyOf(entry.id) == key)
            return entry.id;
    return QString();
}

void ShortcutsDialog::fill()
{
    const QString current = _editing;
    _tree->clear();
    // Keys given twice are marked, so a clash is seen at once.
    std::map<QString, int> uses;
    for (const ShortcutEntry& entry : _entries)
    {
        const QString key = keyOf(entry.id).toString(QKeySequence::NativeText);
        if (!key.isEmpty())
            ++uses[key];
    }
    for (const ShortcutEntry& entry : _entries)
    {
        const QString key = keyOf(entry.id).toString(QKeySequence::NativeText);
        QTreeWidgetItem* item = new QTreeWidgetItem(_tree, {entry.id, key});
        item->setData(0, Qt::UserRole, entry.id);
        if (!key.isEmpty() && uses[key] > 1)
        {
            item->setForeground(1, QColor(220, 70, 50));
            item->setToolTip(1, "This key is given to more than one command");
        }
        if (keyOf(entry.id) != entry.defaultKey)
        {
            QFont font = item->font(1);
            font.setBold(true);
            item->setFont(1, font);
        }
        if (entry.id == current)
            _tree->setCurrentItem(item);
    }
    filter(_search->text());
}

void ShortcutsDialog::filter(const QString& text)
{
    for (int i = 0; i < _tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = _tree->topLevelItem(i);
        item->setHidden(!text.isEmpty() && !item->text(0).contains(text, Qt::CaseInsensitive) &&
            !item->text(1).contains(text, Qt::CaseInsensitive));
    }
}

void ShortcutsDialog::edit(QTreeWidgetItem* item)
{
    _editing = item ? item->data(0, Qt::UserRole).toString() : QString();
    _editor->setEnabled(item != nullptr);
    const QSignalBlocker block(_editor);
    _editor->setKeySequence(item ? keyOf(_editing) : QKeySequence());
}

std::map<QString, QKeySequence> ShortcutsDialog::chosen() const
{
    std::map<QString, QKeySequence> keys;
    for (const ShortcutEntry& entry : _entries)
    {
        const QKeySequence key = keyOf(entry.id);
        if (key != entry.defaultKey)
            keys[entry.id] = key;
    }
    return keys;
}
