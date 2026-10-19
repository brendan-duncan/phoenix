#include "start_dialog.h"

#include "new_document_dialog.h"
#include "starter_templates.h"

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
    setMinimumSize(860, 660);

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
    // New to Phoenix: lessons that walk through it step by step.
    QPushButton* tutorials = new QPushButton("Tutorials...", this);
    tutorials->setToolTip("Guided lessons: draw and animate step by step, with the control to use next circled");
    connect(tutorials, &QPushButton::clicked, this, [this]() { choose(Choice::Tutorials); });
    QHBoxLayout* openButtons = new QHBoxLayout();
    openButtons->addWidget(tutorials);
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
    addStarterTemplates(layout);
    addWorkspaceChoice(layout);
    layout->addLayout(footer);
}

void StartDialog::addStarterTemplates(QVBoxLayout* layout)
{
    // Documents with art already in them, to play and pull apart.
    const QList<StarterTemplate> templates = startertemplates::all();
    if (templates.isEmpty())
        return;
    _templates = new QListWidget(this);
    _templates->setObjectName("startTemplates");
    _templates->setViewMode(QListView::IconMode);
    _templates->setFlow(QListView::LeftToRight);
    _templates->setWrapping(false);
    _templates->setMovement(QListView::Static);
    _templates->setIconSize(QSize(160, 90));
    _templates->setSpacing(6);
    _templates->setFixedHeight(150);
    for (const StarterTemplate& entry : templates)
    {
        QListWidgetItem* item = new QListWidgetItem(QIcon(QPixmap::fromImage(entry.thumbnail)), entry.title);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(entry.summary);
        item->setSizeHint(QSize(176, 124));
        _templates->addItem(item);
    }
    connect(_templates, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        _template = item->data(Qt::UserRole).toString();
        choose(Choice::StarterTemplate);
    });
    layout->addWidget(heading("Starter Templates", this));
    QLabel* note = new QLabel("Ready-made animations to play, change and learn from. Double-click one to open a copy.", this);
    note->setEnabled(false);
    layout->addWidget(note);
    layout->addWidget(_templates);
}

void StartDialog::addWorkspaceChoice(QVBoxLayout* layout)
{
    // How much of Phoenix shows: just what a first animation needs, or
    // every panel. One click either way, here or on the edit bar.
    _simple = new QPushButton("Simple", this);
    _simple->setObjectName("startSimpleWorkspace");
    _simple->setToolTip("Just the stage, the timeline, the common tools, Properties and Color");
    _full = new QPushButton("Full", this);
    _full->setObjectName("startFullWorkspace");
    _full->setToolTip("Every panel and tool");
    for (QPushButton* button : {_simple, _full})
    {
        button->setCheckable(true);
        button->setAutoDefault(false);
    }
    connect(_simple, &QPushButton::clicked, this, [this]() {
        setWorkspace("Simple", false);
        emit workspaceChosen("Simple");
    });
    connect(_full, &QPushButton::clicked, this, [this]() {
        setWorkspace("Essentials", false);
        emit workspaceChosen("Essentials");
    });
    _workspaceNote = new QLabel(this);
    QHBoxLayout* row = new QHBoxLayout();
    row->addWidget(new QLabel("Layout:", this));
    row->addWidget(_simple);
    row->addWidget(_full);
    row->addWidget(_workspaceNote, 1);
    layout->addLayout(row);
    setWorkspace(QString(), false);
}

void StartDialog::setWorkspace(const QString& current, bool firstTime)
{
    if (!_simple)
        return;
    _simple->setChecked(current == "Simple");
    _full->setChecked(!current.isEmpty() && current != "Simple");
    _workspaceNote->setText(firstTime ? "New to animation? Simple shows only what you need to start; "
                                        "you can switch any time on the edit bar."
                                      : QString());
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
