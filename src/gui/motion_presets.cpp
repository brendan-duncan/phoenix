#include "motion_presets.h"

#include "../edit/motion_object.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char* const kGroup = "motionPresets";

} // namespace

MotionPresetsDialog::MotionPresetsDialog(std::function<std::string()> current,
    std::function<bool(const std::string&)> apply, QWidget* parent)
    : QDialog(parent)
    , _current(std::move(current))
    , _apply(std::move(apply))
{
    setWindowTitle("Motion Presets");
    QHBoxLayout* layout = new QHBoxLayout(this);
    _list = new QListWidget(this);
    _list->setMinimumWidth(220);
    layout->addWidget(_list, 1);

    QVBoxLayout* buttons = new QVBoxLayout();
    _applyButton = new QPushButton("Apply", this);
    _applyButton->setToolTip("Move the selected object by this motion");
    QPushButton* save = new QPushButton("Save Current…", this);
    save->setToolTip("Save the motion of the selected tween as a preset");
    _deleteButton = new QPushButton("Delete", this);
    QPushButton* importButton = new QPushButton("Import…", this);
    importButton->setToolTip("Presets from Animate preset files (.xml)");
    QPushButton* importFolderButton = new QPushButton("Import Folder…", this);
    importFolderButton->setToolTip("Every preset in a folder, such as Animate's Motion Presets folder");
    _exportButton = new QPushButton("Export…", this);
    QPushButton* close = new QPushButton("Close", this);
    for (QPushButton* button : {_applyButton, save, _deleteButton, importButton, importFolderButton, _exportButton})
        buttons->addWidget(button);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(_applyButton, &QPushButton::clicked, this, &MotionPresetsDialog::applySelected);
    connect(_list, &QListWidget::itemDoubleClicked, this, &MotionPresetsDialog::applySelected);
    connect(save, &QPushButton::clicked, this, &MotionPresetsDialog::saveCurrent);
    connect(_deleteButton, &QPushButton::clicked, this, &MotionPresetsDialog::removeSelected);
    connect(importButton, &QPushButton::clicked, this, &MotionPresetsDialog::importFiles);
    connect(importFolderButton, &QPushButton::clicked, this, &MotionPresetsDialog::importFolder);
    connect(_exportButton, &QPushButton::clicked, this, &MotionPresetsDialog::exportSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(_list, &QListWidget::currentRowChanged, this, [this](int row) {
        for (QPushButton* button : {_applyButton, _deleteButton, _exportButton})
            button->setEnabled(row >= 0);
    });
    reload();
}

void MotionPresetsDialog::reload()
{
    const QString selected = _list->currentItem() ? _list->currentItem()->text() : QString();
    _list->clear();
    QSettings settings;
    settings.beginGroup(kGroup);
    QStringList names = settings.childKeys();
    settings.endGroup();
    names.sort(Qt::CaseInsensitive);
    for (const QString& name : names)
    {
        // Keys are kept percent-encoded, so any name will do.
        _list->addItem(QUrl::fromPercentEncoding(name.toUtf8()));
    }
    const QList<QListWidgetItem*> found = _list->findItems(selected, Qt::MatchExactly);
    if (!found.isEmpty())
        _list->setCurrentItem(found.front());
    for (QPushButton* button : {_applyButton, _deleteButton, _exportButton})
        button->setEnabled(_list->currentRow() >= 0);
}

bool MotionPresetsDialog::addPreset(const QString& name, const std::string& text)
{
    const std::string motion = fla::motion::wrapped(text);
    fla::MotionObject check;
    if (motion.empty() || !check.parse(motion) || name.trimmed().isEmpty())
        return false;
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue(QString::fromUtf8(QUrl::toPercentEncoding(name.trimmed())), QString::fromStdString(motion));
    settings.endGroup();
    return true;
}

void MotionPresetsDialog::saveCurrent()
{
    const std::string motion = _current ? _current() : std::string();
    if (motion.empty())
    {
        QMessageBox::information(this, "Motion Presets", "Select an object with a motion tween to save its motion.");
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save Motion Preset", "Preset name:", QLineEdit::Normal,
        QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    addPreset(name, motion);
    reload();
    const QList<QListWidgetItem*> found = _list->findItems(name.trimmed(), Qt::MatchExactly);
    if (!found.isEmpty())
        _list->setCurrentItem(found.front());
}

void MotionPresetsDialog::applySelected()
{
    QListWidgetItem* item = _list->currentItem();
    if (!item || !_apply)
        return;
    QSettings settings;
    settings.beginGroup(kGroup);
    const std::string motion =
        settings.value(QString::fromUtf8(QUrl::toPercentEncoding(item->text()))).toString().toStdString();
    settings.endGroup();
    if (!_apply(motion))
        QMessageBox::information(this, "Motion Presets",
            "A motion preset moves one symbol instance or text block. Select one alone on its frame.");
}

void MotionPresetsDialog::removeSelected()
{
    QListWidgetItem* item = _list->currentItem();
    if (!item)
        return;
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.remove(QString::fromUtf8(QUrl::toPercentEncoding(item->text())));
    settings.endGroup();
    reload();
}

void MotionPresetsDialog::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, "Import Motion Presets", QString(),
        "Motion Presets (*.xml)");
    QStringList failed;
    for (const QString& path : files)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) ||
            !addPreset(QFileInfo(path).completeBaseName(), file.readAll().toStdString()))
            failed << QFileInfo(path).fileName();
    }
    reload();
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Motion Presets", "These hold no motion:\n" + failed.join("\n"));
}

void MotionPresetsDialog::importFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Import a Folder of Motion Presets");
    if (folder.isEmpty())
        return;
    int count = 0;
    QDirIterator it(folder, {"*.xml"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString path = it.next();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && addPreset(QFileInfo(path).completeBaseName(), file.readAll().toStdString()))
            ++count;
    }
    reload();
    QMessageBox::information(this, "Motion Presets", QString("Imported %1 presets.").arg(count));
}

void MotionPresetsDialog::exportSelected()
{
    QListWidgetItem* item = _list->currentItem();
    if (!item)
        return;
    const QString path = QFileDialog::getSaveFileName(this, "Export Motion Preset", item->text() + ".xml",
        "Motion Presets (*.xml)");
    if (path.isEmpty())
        return;
    QSettings settings;
    settings.beginGroup(kGroup);
    const QString motion = settings.value(QString::fromUtf8(QUrl::toPercentEncoding(item->text()))).toString();
    settings.endGroup();
    // As Animate keeps a preset: the AnimationCore on its own.
    QString core = motion;
    const int from = core.indexOf("<AnimationCore");
    const int to = core.lastIndexOf("</motionObjectXML>");
    if (from >= 0 && to > from)
        core = core.mid(from, to - from);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(core.toUtf8()) < 0)
        QMessageBox::warning(this, "Motion Presets", "Could not write " + QDir::toNativeSeparators(path) + ".");
}
