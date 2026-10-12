#include "start_dialog.h"

#include "new_document_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/// The presets offered on the start screen, by category and name, as
/// NewDocumentDialog lists them; the rest are a click on More Presets away.
const char* const kStartPresets[][2] = {
    {"Character Animation", "Full HD"},
    {"Character Animation", "HD"},
    {"Character Animation", "Standard"},
    {"Social", "Instagram"},
    {"Social", "Instagram Story"},
    {"Game", "Mobile Landscape"},
};

QLabel* heading(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 1.25);
    font.setBold(true);
    label->setFont(font);
    return label;
}

} // namespace

StartDialog::StartDialog(const QStringList& recentFiles, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Welcome to Phoenix");
    setMinimumSize(860, 520);

    // --- Create new ---
    _presets = new QListWidget(this);
    const auto& presets = NewDocumentDialog::presets();
    for (const auto& wanted : kStartPresets)
    {
        for (size_t i = 0; i < presets.size(); ++i)
        {
            const NewDocumentDialog::Preset& preset = presets[i];
            if (QString(preset.category) != wanted[0] || QString(preset.name) != wanted[1])
                continue;
            QListWidgetItem* item = new QListWidgetItem(QString("%1 %2\n%3 x %4, %5 fps")
                .arg(preset.category, preset.name).arg(preset.width).arg(preset.height).arg(preset.frameRate));
            item->setData(Qt::UserRole, static_cast<int>(i));
            _presets->addItem(item);
            break;
        }
    }
    _presets->setSpacing(3);
    connect(_presets, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        _preset = item->data(Qt::UserRole).toInt();
        choose(Choice::NewPreset);
    });

    QPushButton* create = new QPushButton("Create", this);
    create->setDefault(true);
    connect(create, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem* item = _presets->currentItem())
        {
            _preset = item->data(Qt::UserRole).toInt();
            choose(Choice::NewPreset);
        }
    });
    QPushButton* more = new QPushButton("More Presets...", this);
    connect(more, &QPushButton::clicked, this, [this]() { choose(Choice::NewCustom); });
    if (_presets->count() > 0)
        _presets->setCurrentRow(0);

    QHBoxLayout* newButtons = new QHBoxLayout();
    newButtons->addWidget(more);
    newButtons->addStretch();
    newButtons->addWidget(create);

    QVBoxLayout* newSide = new QVBoxLayout();
    newSide->addWidget(heading("Create New", this));
    newSide->addWidget(_presets, 1);
    newSide->addLayout(newButtons);

    // --- Open ---
    _recent = new QListWidget(this);
    for (const QString& file : recentFiles)
    {
        const QFileInfo info(file);
        QListWidgetItem* item = new QListWidgetItem(QString("%1\n%2").arg(info.fileName(), info.absolutePath()));
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
        if (!info.exists())
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        _recent->addItem(item);
    }
    if (_recent->count() == 0)
    {
        QListWidgetItem* none = new QListWidgetItem("No recent files");
        none->setFlags(Qt::NoItemFlags);
        _recent->addItem(none);
    }
    _recent->setSpacing(3);
    connect(_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const QString file = item->data(Qt::UserRole).toString();
        if (file.isEmpty())
            return;
        _path = file;
        choose(Choice::OpenRecent);
    });

    QPushButton* open = new QPushButton("Open...", this);
    connect(open, &QPushButton::clicked, this, [this]() { choose(Choice::Browse); });
    QHBoxLayout* openButtons = new QHBoxLayout();
    openButtons->addStretch();
    openButtons->addWidget(open);

    QVBoxLayout* openSide = new QVBoxLayout();
    openSide->addWidget(heading("Recent Files", this));
    openSide->addWidget(_recent, 1);
    openSide->addLayout(openButtons);

    QHBoxLayout* body = new QHBoxLayout();
    body->addLayout(newSide, 1);
    body->addSpacing(16);
    body->addLayout(openSide, 1);

    _showAtStartup = new QCheckBox("Show at startup", this);
    _showAtStartup->setChecked(true);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QHBoxLayout* footer = new QHBoxLayout();
    footer->addWidget(_showAtStartup);
    footer->addStretch();
    footer->addWidget(buttons);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addLayout(footer);
}

bool StartDialog::showAtStartup() const
{
    return _showAtStartup->isChecked();
}

void StartDialog::choose(Choice choice)
{
    _choice = choice;
    accept();
}
