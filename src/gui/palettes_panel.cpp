#include "palettes_panel.h"

#include "../data/document.h"
#include "../edit/command_stack.h"
#include "../edit/palettes.h"
#include "../edit/property_command.h"

#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <map>

struct PalettesState
{
    fla::palettes::Palettes palettes;
};

namespace {

using Attributes = std::vector<std::pair<std::string, std::string>>;

QIcon swatch(const uint8_t rgba[4])
{
    QPixmap pixmap(28, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    for (int y = 0; y < 16; y += 4)
        for (int x = 0; x < 28; x += 4)
            painter.fillRect(x, y, 4, 4, ((x + y) / 4) % 2 ? QColor(204, 204, 204) : Qt::white);
    painter.fillRect(pixmap.rect(), QColor(rgba[0], rgba[1], rgba[2], rgba[3]));
    painter.setPen(QColor(128, 128, 128));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    return QIcon(pixmap);
}

QPushButton* button(QWidget* parent, const QString& text, const QString& tip)
{
    QPushButton* made = new QPushButton(text, parent);
    made->setToolTip(tip);
    return made;
}

} // namespace

PalettesPanel::PalettesPanel(PalettesLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout* top = new QHBoxLayout();
    _palettes = new QComboBox(this);
    _palettes->setToolTip("The palette in use; choosing another recolors everything linked from it");
    QToolButton* more = new QToolButton(this);
    more->setText("...");
    more->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(more);
    menu->addAction("New Palette...", this, &PalettesPanel::newPalette);
    menu->addAction("Rename Palette...", this, &PalettesPanel::renamePalette);
    menu->addAction("Delete Palette", this, &PalettesPanel::removePalette);
    more->setMenu(menu);
    top->addWidget(_palettes, 1);
    top->addWidget(more);
    layout->addLayout(top);

    _colors = new QListWidget(this);
    _colors->setIconSize(QSize(28, 16));
    _colors->setToolTip("Double-click a color to change it everywhere it is linked");
    layout->addWidget(_colors, 1);

    QGridLayout* buttons = new QGridLayout();
    QPushButton* add = button(this, "Add...", "A new color, from the current fill color");
    QPushButton* edit = button(this, "Edit...", "Change the color everywhere it is linked");
    QPushButton* remove = button(this, "Remove", "Take the color out of every palette; what used it keeps its color");
    QPushButton* fill = button(this, "Fill", "Fill the selection with the color and link it");
    QPushButton* stroke = button(this, "Stroke", "Stroke the selection with the color and link it");
    QPushButton* match = button(this, "Link Matching", "Link every fill, stroke and text in the document that has this color");
    QPushButton* unlink = button(this, "Unlink", "Take the links off the selection");
    buttons->addWidget(add, 0, 0);
    buttons->addWidget(edit, 0, 1);
    buttons->addWidget(remove, 0, 2);
    buttons->addWidget(fill, 1, 0);
    buttons->addWidget(stroke, 1, 1);
    buttons->addWidget(unlink, 1, 2);
    buttons->addWidget(match, 2, 0, 1, 3);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, &PalettesPanel::addColor);
    connect(edit, &QPushButton::clicked, this, &PalettesPanel::editColor);
    connect(remove, &QPushButton::clicked, this, &PalettesPanel::removeColor);
    connect(fill, &QPushButton::clicked, this, [this]() { paint(true, false); });
    connect(stroke, &QPushButton::clicked, this, [this]() { paint(false, true); });
    connect(match, &QPushButton::clicked, this, &PalettesPanel::linkMatching);
    connect(unlink, &QPushButton::clicked, this, &PalettesPanel::unlinkSelection);
    connect(_colors, &QListWidget::itemDoubleClicked, this, &PalettesPanel::editColor);
    connect(_palettes, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        fla::Document* document = _links.document ? _links.document() : nullptr;
        if (_refreshing || !document || name.isEmpty())
            return;
        // Another palette: everything linked takes its colors.
        PalettesState state{fla::palettes::load(*document)};
        if (state.palettes.active == name.toStdString())
            return;
        state.palettes.active = name.toStdString();
        store(state, "Swap Palette");
    });
    refresh();
}

void PalettesPanel::refresh()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    _refreshing = true;
    const int keep = _colors->currentRow();
    _palettes->clear();
    _colors->clear();
    if (document)
    {
        const fla::palettes::Palettes palettes = fla::palettes::load(*document);
        std::map<std::string, int> uses;
        for (const auto& [name, count] : fla::palettes::usage(*document))
            uses[name] = count;
        for (const fla::palettes::Palette& palette : palettes.list)
            _palettes->addItem(QString::fromStdString(palette.name));
        if (const fla::palettes::Palette* active = palettes.activePalette())
        {
            _palettes->setCurrentText(QString::fromStdString(active->name));
            for (const fla::palettes::Color& color : active->colors)
            {
                const int count = uses.count(color.name) ? uses[color.name] : 0;
                QListWidgetItem* item = new QListWidgetItem(swatch(color.rgba),
                    QString("%1   (%2)").arg(QString::fromStdString(color.name)).arg(count), _colors);
                item->setToolTip(QString("#%1%2%3, used %4 time%5")
                                     .arg(color.rgba[0], 2, 16, QChar('0'))
                                     .arg(color.rgba[1], 2, 16, QChar('0'))
                                     .arg(color.rgba[2], 2, 16, QChar('0'))
                                     .arg(count)
                                     .arg(count == 1 ? "" : "s"));
            }
        }
    }
    if (keep >= 0 && keep < _colors->count())
        _colors->setCurrentRow(keep);
    _refreshing = false;
}

int PalettesPanel::currentColor() const
{
    return _colors->currentRow();
}

void PalettesPanel::store(const PalettesState& state, const QString& name)
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    if (!document || !commands)
        return;
    commands->beginMacro(name.toStdString());
    commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
        document->extraAttributes, fla::palettes::saved(*document, state.palettes), name.toStdString())));
    if (fla::CommandPtr recolor = fla::palettes::recolor(*document, state.palettes))
        commands->push(std::move(recolor));
    commands->endMacro();
    if (_links.changed)
        _links.changed();
    refresh();
}

void PalettesPanel::newPalette()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return;
    PalettesState state{fla::palettes::load(*document)};
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Palette",
        state.palettes.list.empty() ? QString("Name:") : QString("Name (a copy of the one in use, to recolor):"),
        QLineEdit::Normal, state.palettes.list.empty() ? QString("Day") : QString("Night"), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    for (const fla::palettes::Palette& palette : state.palettes.list)
        if (palette.name == name.toStdString())
        {
            QMessageBox::information(this, "New Palette", "There is a palette of that name already.");
            return;
        }
    fla::palettes::Palette palette;
    if (const fla::palettes::Palette* active = state.palettes.activePalette())
        palette = *active;
    palette.name = name.toStdString();
    state.palettes.list.push_back(palette);
    state.palettes.active = palette.name;
    store(state, "New Palette");
}

void PalettesPanel::renamePalette()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return;
    PalettesState state{fla::palettes::load(*document)};
    fla::palettes::Palette* active = state.palettes.activePalette();
    if (!active)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename Palette", "Name:", QLineEdit::Normal,
        QString::fromStdString(active->name), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    active->name = name.toStdString();
    state.palettes.active = active->name;
    store(state, "Rename Palette");
}

void PalettesPanel::removePalette()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return;
    PalettesState state{fla::palettes::load(*document)};
    const fla::palettes::Palette* active = state.palettes.activePalette();
    if (!active)
        return;
    const std::string gone = active->name;
    auto& list = state.palettes.list;
    list.erase(std::remove_if(list.begin(), list.end(), [&gone](const fla::palettes::Palette& p) { return p.name == gone; }),
        list.end());
    state.palettes.active = list.empty() ? std::string() : list.front().name;
    store(state, "Delete Palette");
}

void PalettesPanel::addColor()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return;
    PalettesState state{fla::palettes::load(*document)};
    if (state.palettes.list.empty())
    {
        fla::palettes::Palette first;
        first.name = "Palette";
        state.palettes.list.push_back(first);
        state.palettes.active = first.name;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Add Color", "Name (skin, hair, shadow...):", QLineEdit::Normal,
        QString("Color %1").arg(state.palettes.activePalette()->colors.size() + 1), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const QColor start = _links.fillColor ? _links.fillColor() : QColor(Qt::black);
    fla::palettes::Color color;
    color.name = name.toStdString();
    color.rgba[0] = static_cast<uint8_t>(start.red());
    color.rgba[1] = static_cast<uint8_t>(start.green());
    color.rgba[2] = static_cast<uint8_t>(start.blue());
    color.rgba[3] = static_cast<uint8_t>(start.alpha());
    // Every palette gets it, so a swap always has a color for it.
    for (fla::palettes::Palette& palette : state.palettes.list)
        if (std::none_of(palette.colors.begin(), palette.colors.end(),
                [&color](const fla::palettes::Color& c) { return c.name == color.name; }))
            palette.colors.push_back(color);
    store(state, "Add Color");
    _colors->setCurrentRow(_colors->count() - 1);
}

void PalettesPanel::editColor()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    const int index = currentColor();
    if (!document || index < 0)
        return;
    PalettesState state{fla::palettes::load(*document)};
    fla::palettes::Palette* active = state.palettes.activePalette();
    if (!active || index >= static_cast<int>(active->colors.size()))
        return;
    fla::palettes::Color& color = active->colors[static_cast<size_t>(index)];
    const QColor chosen = QColorDialog::getColor(QColor(color.rgba[0], color.rgba[1], color.rgba[2], color.rgba[3]), this,
        QString::fromStdString(color.name), QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;
    color.rgba[0] = static_cast<uint8_t>(chosen.red());
    color.rgba[1] = static_cast<uint8_t>(chosen.green());
    color.rgba[2] = static_cast<uint8_t>(chosen.blue());
    color.rgba[3] = static_cast<uint8_t>(chosen.alpha());
    store(state, "Edit Color");
}

void PalettesPanel::removeColor()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    const int index = currentColor();
    if (!document || index < 0)
        return;
    PalettesState state{fla::palettes::load(*document)};
    const fla::palettes::Palette* active = state.palettes.activePalette();
    if (!active || index >= static_cast<int>(active->colors.size()))
        return;
    const std::string gone = active->colors[static_cast<size_t>(index)].name;
    for (fla::palettes::Palette& palette : state.palettes.list)
        palette.colors.erase(std::remove_if(palette.colors.begin(), palette.colors.end(),
                                 [&gone](const fla::palettes::Color& c) { return c.name == gone; }),
            palette.colors.end());
    store(state, "Remove Color");
}

void PalettesPanel::paint(bool fills, bool strokes)
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    const int index = currentColor();
    const std::vector<fla::Element*> selected = _links.selection ? _links.selection() : std::vector<fla::Element*>();
    if (!document || !commands || index < 0 || selected.empty())
        return;
    const fla::palettes::Palettes palettes = fla::palettes::load(*document);
    const fla::palettes::Palette* active = palettes.activePalette();
    if (!active || index >= static_cast<int>(active->colors.size()))
        return;
    if (fla::CommandPtr command = fla::palettes::paint(selected, active->colors[static_cast<size_t>(index)], fills, strokes))
        commands->push(std::move(command));
    if (_links.changed)
        _links.changed();
    refresh();
}

void PalettesPanel::linkMatching()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    const int index = currentColor();
    if (!document || !commands || index < 0)
        return;
    const fla::palettes::Palettes palettes = fla::palettes::load(*document);
    const fla::palettes::Palette* active = palettes.activePalette();
    if (!active || index >= static_cast<int>(active->colors.size()))
        return;
    if (fla::CommandPtr command = fla::palettes::linkMatching(*document, active->colors[static_cast<size_t>(index)]))
        commands->push(std::move(command));
    refresh();
}

void PalettesPanel::unlinkSelection()
{
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    const std::vector<fla::Element*> selected = _links.selection ? _links.selection() : std::vector<fla::Element*>();
    if (!commands || selected.empty())
        return;
    if (fla::CommandPtr command = fla::palettes::unlink(selected))
        commands->push(std::move(command));
    refresh();
}
