#include "theme_editor.h"
#include "file_pick.h"
#include "modal.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include "hot_text.h"
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int KeyRole = Qt::UserRole;

QIcon swatch(const QColor& color)
{
    QPixmap pixmap(28, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    // A checkerboard under a see-through color.
    for (int y = 0; y < 16; y += 4)
        for (int x = 0; x < 28; x += 4)
            painter.fillRect(x, y, 4, 4, ((x + y) / 4) % 2 ? QColor(204, 204, 204) : Qt::white);
    painter.fillRect(pixmap.rect(), color);
    painter.setPen(QColor(128, 128, 128));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    return QIcon(pixmap);
}

} // namespace

ThemeEditor::ThemeEditor(QWidget* parent)
    : QDialog(parent)
    , _was(theme::currentName())
{
    setWindowTitle("Edit Theme");
    QVBoxLayout* layout = new QVBoxLayout(this);

    QFormLayout* form = new QFormLayout();
    _start = new QComboBox(this);
    _start->addItems(theme::names());
    _start->setCurrentText(_was);
    form->addRow("Start from:", _start);
    _base = new QComboBox(this);
    _base->addItems({"Dark", "Light"});
    _base->setToolTip("The mode the window's controls are drawn in; roles left alone take its colors");
    form->addRow("Mode:", _base);
    _fontSize = new HotSpinBox(this);
    _fontSize->setRange(0, 24);
    _fontSize->setSpecialValueText("System");
    _fontSize->setSuffix(" pt");
    form->addRow("Font size:", _fontSize);
    _scale = new HotDoubleSpinBox(this);
    _scale->setRange(75, 250);
    _scale->setSingleStep(25);
    _scale->setDecimals(0);
    _scale->setSuffix(" %");
    _scale->setToolTip("The size of everything in Phoenix's window; used from the next start");
    form->addRow("UI scale:", _scale);
    layout->addLayout(form);

    _roles = new QTreeWidget(this);
    _roles->setColumnCount(2);
    _roles->setHeaderLabels({"Role", "Color"});
    _roles->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _roles->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _roles->setIconSize(QSize(28, 16));
    _roles->setMinimumHeight(360);
    _roles->setToolTip("Double-click a color to change it; right-click to put back the mode's own");
    _roles->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(_roles, 1);

    QHBoxLayout* files = new QHBoxLayout();
    QPushButton* import = new QPushButton("Import...", this);
    QPushButton* exportButton = new QPushButton("Export...", this);
    QPushButton* remove = new QPushButton("Delete", this);
    files->addWidget(import);
    files->addWidget(exportButton);
    files->addWidget(remove);
    files->addStretch(1);
    layout->addLayout(files);

    QDialogButtonBox* buttons = new QDialogButtonBox(this);
    QPushButton* save = buttons->addButton("Save As...", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(_start, &QComboBox::currentTextChanged, this, &ThemeEditor::startFrom);
    connect(_base, &QComboBox::currentTextChanged, this, [this](const QString& base) {
        _theme.base = base;
        changed();
    });
    connect(_fontSize, &HotSpinBox::valueChanged, this, [this](int size) {
        _theme.fontSize = size;
        changed();
    });
    connect(_scale, &HotDoubleSpinBox::valueChanged, this, [this](double percent) { _theme.uiScale = percent / 100.0; });
    connect(_roles, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { pick(item); });
    connect(_roles, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QTreeWidgetItem* item = _roles->itemAt(at);
        const QString key = item ? item->data(0, KeyRole).toString() : QString();
        if (key.isEmpty())
            return;
        QMenu* menu = new QMenu(this);
        QAction* change = menu->addAction("Change...");
        QAction* reset = menu->addAction(QString("Use %1's Own").arg(_theme.base));
        reset->setEnabled(_theme.colors.contains(key));
        modal::popup(menu, _roles->viewport()->mapToGlobal(at), [this, item, key, change, reset](QAction* chosen) {
            if (chosen == change)
            {
                pick(item);
            }
            else if (chosen == reset)
            {
                _theme.colors.remove(key);
                changed();
            }
        });
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        saveAs([this]() { accept(); });
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &ThemeEditor::reject);
    connect(import, &QPushButton::clicked, this, [this]() {
        file_pick::open(this, "Import Theme", QString(), "Phoenix themes (*.phxtheme);;All files (*)",
            [this](const QString& path) {
                QFile file(path);
                bool ok = false;
                theme::Theme imported = file.open(QIODevice::ReadOnly)
                    ? theme::Theme::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &ok) : theme::Theme();
                if (!ok)
                {
                    modal::warning(this, "Import Theme", "That file is not a Phoenix theme.");
                    return;
                }
                if (imported.name.isEmpty())
                    imported.name = QFileInfo(path).completeBaseName();
                const QString base = imported.name;
                for (int n = 2; theme::names().contains(imported.name); ++n)
                    imported.name = QString("%1 %2").arg(base).arg(n);
                if (!theme::save(imported))
                {
                    modal::warning(this, "Import Theme", "The theme could not be saved.");
                    return;
                }
                _start->blockSignals(true);
                _start->clear();
                _start->addItems(theme::names());
                _start->blockSignals(false);
                _start->setCurrentText(imported.name);
            });
    });
    connect(exportButton, &QPushButton::clicked, this, [this]() {
        const QString name = _theme.name.isEmpty() ? QString("Theme") : _theme.name;
        file_pick::save(this, "Export Theme", name + ".phxtheme", "Phoenix themes (*.phxtheme)",
            [this](const QString& path) {
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(_theme.toJson()).toJson()) < 0)
                    modal::warning(this, "Export Theme", QString("Could not write %1.").arg(path));
            });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const QString name = _start->currentText();
        if (theme::builtInNames().contains(name))
        {
            modal::information(this, "Delete Theme", "The built-in themes stay.");
            return;
        }
        modal::ask(this, QMessageBox::Question, "Delete Theme", QString("Delete the theme %1?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::NoButton, [this, name](QMessageBox::StandardButton answer) {
                if (answer != QMessageBox::Yes)
                    return;
                theme::remove(name);
                if (_was == name)
                    _was = "Follow System";
                _start->clear();
                _start->addItems(theme::names());
                _start->setCurrentText(_was);
            });
    });

    startFrom(_was);
    resize(440, 640);
}

void ThemeEditor::startFrom(const QString& name)
{
    _theme = theme::load(name);
    // Follow System resolves to a mode; edited, it is a theme of its own.
    if (theme::builtInNames().contains(name))
        _theme.name.clear();
    for (QWidget* widget : std::initializer_list<QWidget*>{_base, _fontSize, _scale})
        widget->blockSignals(true);
    _base->setCurrentText(_theme.base);
    _fontSize->setValue(_theme.fontSize);
    _scale->setValue(_theme.uiScale * 100.0);
    for (QWidget* widget : std::initializer_list<QWidget*>{_base, _fontSize, _scale})
        widget->blockSignals(false);
    changed();
}

void ThemeEditor::fill()
{
    QMap<QString, QTreeWidgetItem*> areas;
    _roles->clear();
    for (const theme::Role& role : theme::roles())
    {
        QTreeWidgetItem*& area = areas[role.area];
        if (!area)
        {
            area = new QTreeWidgetItem(_roles);
            area->setText(0, role.area);
            area->setFlags(Qt::ItemIsEnabled);
            QFont font = area->font(0);
            font.setBold(true);
            area->setFont(0, font);
        }
        QTreeWidgetItem* item = new QTreeWidgetItem(area);
        const QColor color = _theme.color(role.key);
        item->setText(0, role.label);
        item->setIcon(1, swatch(color));
        item->setText(1, color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb).toUpper());
        item->setData(0, KeyRole, QString(role.key));
        // Roles this theme sets itself stand out.
        if (_theme.colors.contains(role.key))
        {
            QFont font = item->font(0);
            font.setItalic(true);
            item->setFont(0, font);
        }
    }
    _roles->expandAll();
}

void ThemeEditor::pick(QTreeWidgetItem* item)
{
    const QString key = item ? item->data(0, KeyRole).toString() : QString();
    if (key.isEmpty())
        return;
    modal::getColor(_theme.color(key), this, item->text(0), [this, key](const QColor& chosen) {
        if (!chosen.isValid())
            return;
        _theme.colors[key] = chosen;
        changed();
    }, QColorDialog::ShowAlphaChannel);
}

void ThemeEditor::changed()
{
    // Live on the real window.
    theme::preview(_theme);
    fill();
}

void ThemeEditor::saveAs(std::function<void()> saved)
{
    modal::getText(this, "Save Theme", "Theme name:", _theme.name.isEmpty() ? QString("My Theme") : _theme.name,
        [this, saved](const QString& typed) {
            const QString name = typed.trimmed();
            if (name.isEmpty())
                return;
            if (theme::builtInNames().contains(name))
            {
                modal::information(this, "Save Theme", "Pick a name other than the built-in themes'.");
                return;
            }
            if (theme::names().contains(name) && name != _theme.name)
            {
                modal::ask(this, QMessageBox::Question, "Save Theme", QString("Replace the theme %1?").arg(name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::NoButton,
                    [this, name, saved](QMessageBox::StandardButton answer) {
                        if (answer == QMessageBox::Yes)
                            saveUnder(name, saved);
                    });
                return;
            }
            saveUnder(name, saved);
        });
}

void ThemeEditor::saveUnder(const QString& name, std::function<void()> saved)
{
    _theme.name = name;
    if (!theme::save(_theme))
    {
        modal::warning(this, "Save Theme", "The theme could not be saved.");
        return;
    }
    // The onion skin takes the theme's colors from now on.
    QSettings settings;
    settings.remove("onion/beforeColor");
    settings.remove("onion/afterColor");
    _saved = true;
    theme::use(name);
    if (_theme.uiScale != 1.0)
        modal::information(this, "Save Theme", "The UI scale is used from the next start.");
    if (saved)
        saved();
}

void ThemeEditor::reject()
{
    // Nothing saved: the theme in use before comes back.
    if (!_saved)
        theme::use(_was);
    QDialog::reject();
}
