#include "swatches_panel.h"
#include "modal.h"

#include "swatch_popup.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../edit/palettes.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QSettings>
#include <QMessageBox>
#include <QFileInfo>
#include <QFileDialog>
#include <QFile>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>
#include <algorithm>
#include <cstdint>

namespace {

constexpr int kCell = 14;

/// \a gradients as swatch list items, one after another.
std::string itemsOf(const std::vector<fla::swatches::Gradient>& gradients)
{
    std::string items;
    for (const fla::swatches::Gradient& gradient : gradients)
        items += fla::swatches::itemOf(gradient);
    return items;
}

} // namespace

/// The swatches as a grid of small squares, as many to a row as fit.
class SwatchesPanel::Grid : public QWidget
{
public:
    explicit Grid(QWidget* parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }

    void setColors(const std::vector<fla::swatches::Color>& colors, const std::vector<std::string>& tags = {})
    {
        _colors = colors;
        _tags = tags;
        updateGeometry();
        relayout();
        update();
    }

    /// Shows gradient swatches instead of colors.
    void setGradients(const std::vector<fla::swatches::Gradient>& gradients)
    {
        _gradients = gradients;
        _colors.assign(gradients.size(), fla::swatches::Color{0, 0, 0, 255});
        _tags.clear();
        updateGeometry();
        relayout();
        update();
    }

    std::function<void(int)> clicked;
    std::function<void(int, const QPoint&)> rightClicked;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        const int columns = std::max(1, width / kCell);
        return (static_cast<int>(_colors.size()) + columns - 1) / columns * kCell + 1;
    }
    QSize sizeHint() const override { return QSize(16 * kCell, heightForWidth(16 * kCell)); }

protected:
    void resizeEvent(QResizeEvent*) override { relayout(); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const int columns = std::max(1, width() / kCell);
        for (size_t i = 0; i < _colors.size(); ++i)
        {
            const int x = static_cast<int>(i) % columns * kCell;
            const int y = static_cast<int>(i) / columns * kCell;
            const auto& c = _colors[i];
            const QRect cell(x, y, kCell, kCell);
            if (i < _gradients.size())
            {
                painter.fillRect(cell, brushOf(_gradients[i]));
                painter.setPen(QColor(30, 30, 30));
                painter.drawRect(cell.adjusted(0, 0, -1, -1));
                continue;
            }
            if (c[3] < 255)
            {
                painter.fillRect(cell, Qt::white);
                painter.fillRect(QRect(x, y, kCell / 2, kCell / 2), QColor(204, 204, 204));
                painter.fillRect(QRect(x + kCell / 2, y + kCell / 2, kCell / 2, kCell / 2), QColor(204, 204, 204));
            }
            painter.fillRect(cell, QColor(c[0], c[1], c[2], c[3]));
            painter.setPen(QColor(30, 30, 30));
            painter.drawRect(cell.adjusted(0, 0, -1, -1));
            // A tagged swatch has a corner turned down.
            if (i < _tags.size() && !_tags[i].empty())
            {
                painter.setBrush(Qt::white);
                painter.setPen(QColor(30, 30, 30));
                painter.drawPolygon(QPolygon({QPoint(x + kCell - 6, y + kCell - 1), QPoint(x + kCell - 1, y + kCell - 1),
                    QPoint(x + kCell - 1, y + kCell - 6)}));
                painter.setBrush(Qt::NoBrush);
            }
        }
        if (_hover >= 0 && _hover < static_cast<int>(_colors.size()))
        {
            const int x = _hover % columns * kCell;
            const int y = _hover / columns * kCell;
            painter.setPen(Qt::white);
            painter.drawRect(QRect(x, y, kCell - 1, kCell - 1));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int at = indexAt(event->pos());
        if (at != _hover)
        {
            _hover = at;
            if (at >= 0 && at < static_cast<int>(_gradients.size()))
            {
                const fla::swatches::Gradient& gradient = _gradients[static_cast<size_t>(at)];
                setToolTip(QString("%1 gradient, %2 colors")
                               .arg(gradient.radial ? "Radial" : "Linear")
                               .arg(gradient.stops.size()));
            }
            else if (at >= 0)
            {
                const auto& c = _colors[static_cast<size_t>(at)];
                const QString tag = at < static_cast<int>(_tags.size()) && !_tags[static_cast<size_t>(at)].empty()
                    ? QString(" (tagged: %1)").arg(QString::fromStdString(_tags[static_cast<size_t>(at)]))
                    : QString();
                setToolTip(QColor(c[0], c[1], c[2]).name().toUpper() +
                    (c[3] < 255 ? QString(" %1%").arg(qRound(c[3] / 2.55)) : QString()) + tag);
            }
            update();
        }
    }

    void leaveEvent(QEvent*) override
    {
        _hover = -1;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        const int at = indexAt(event->pos());
        if (at < 0)
            return;
        if (event->button() == Qt::LeftButton && clicked)
            clicked(at);
        else if (event->button() == Qt::RightButton && rightClicked)
            rightClicked(at, event->globalPosition().toPoint());
    }

private:
    int indexAt(const QPoint& point) const
    {
        const int columns = std::max(1, width() / kCell);
        if (point.x() < 0 || point.x() >= columns * kCell || point.y() < 0)
            return -1;
        const int index = point.y() / kCell * columns + point.x() / kCell;
        return index < static_cast<int>(_colors.size()) ? index : -1;
    }

    void relayout()
    {
        setMinimumHeight(heightForWidth(width()));
    }

    std::vector<fla::swatches::Color> _colors;
    std::vector<fla::swatches::Gradient> _gradients;
    std::vector<std::string> _tags;
    int _hover = -1;
};

SwatchesPanel::SwatchesPanel(QWidget* parent)
    : QWidget(parent)
{
    // The colors, and the gradients in their own rows below them.
    QWidget* swatches = new QWidget(this);
    _grid = new Grid(swatches);
    _gradientGrid = new Grid(swatches);
    _gradientGrid->setObjectName("swatchesGradients");
    QVBoxLayout* rows = new QVBoxLayout(swatches);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(6);
    rows->addWidget(_grid);
    rows->addWidget(_gradientGrid);
    rows->addStretch(1);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidget(swatches);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    _gradientGrid->clicked = [this](int index) {
        if (index >= 0 && index < static_cast<int>(_gradients.size()))
            emit gradientPicked(_gradients[static_cast<size_t>(index)], _stroke->isChecked());
    };
    _gradientGrid->rightClicked = [this](int index, const QPoint& where) {
        // The default gradients become the document's own when one goes
        // or is copied.
        QMenu menu(this);
        QAction* duplicate = menu.addAction("Duplicate Swatch");
        QAction* remove = menu.addAction("Delete Swatch");
        QAction* chosen = menu.exec(where);
        if (chosen == remove)
            emit gradientRemoveRequested(index);
        else if (chosen == duplicate)
            emit gradientDuplicateRequested(index);
    };

    _grid->clicked = [this](int index) {
        if (index < 0 || index >= static_cast<int>(_shown.size()))
            return;
        const auto& c = _shown[static_cast<size_t>(index)];
        emit colorPicked(QColor(c[0], c[1], c[2], c[3]), _stroke->isChecked());
    };
    _grid->rightClicked = [this](int index, const QPoint& where) {
        // Only the document's own swatches can be changed; the default
        // palette is not saved anywhere.
        if (!_ownColors)
            return;
        QMenu menu(this);
        QAction* duplicate = menu.addAction("Duplicate Swatch");
        QAction* remove = menu.addAction("Delete Swatch");
        menu.addSeparator();
        const bool tagged = index < static_cast<int>(_tags.size()) && !_tags[static_cast<size_t>(index)].empty();
        QAction* tag = tagged ? nullptr : menu.addAction("Convert to Tagged Swatch...");
        QAction* edit = tagged ? menu.addAction("Edit Tagged Swatch...") : nullptr;
        QAction* chosen = menu.exec(where);
        if (tag && chosen == tag)
            emit tagRequested(index);
        else if (edit && chosen == edit)
            emit palettesRequested();
        if (chosen == remove)
            emit removeRequested(index);
        else if (chosen == duplicate)
        {
            std::vector<fla::swatches::Color> colors = _shown;
            colors.insert(colors.begin() + index + 1, colors[static_cast<size_t>(index)]);
            emit colorsEditRequested(colors, "Duplicate Swatch");
        }
    };

    _stroke = new QCheckBox("Stroke", this);
    _stroke->setToolTip("A click sets the stroke color instead of the fill color");
    QPushButton* add = new QPushButton("Add Swatch", this);
    add->setToolTip("Keep the fill, a color or a gradient, among the document's swatches");
    connect(add, &QPushButton::clicked, this, &SwatchesPanel::addRequested);

    QPushButton* more = new QPushButton(QString::fromUtf8("\u2630"), this);
    more->setToolTip("Sort, clear, default colors, and color tables in and out");
    more->setFixedWidth(30);
    connect(more, &QPushButton::clicked, this, [this, more]() { showMenu(more->mapToGlobal(QPoint(0, more->height()))); });

    QHBoxLayout* footer = new QHBoxLayout();
    footer->addWidget(_stroke);
    footer->addStretch();
    footer->addWidget(add);
    footer->addWidget(more);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(scroll, 1);
    layout->addLayout(footer);

    rebuild();
}

void SwatchesPanel::setDocument(const fla::FLADocument* document)
{
    _document = document;
    rebuild();
}

void SwatchesPanel::refreshIfStale()
{
    const fla::Document* doc = _document ? _document->document : nullptr;
    const std::vector<fla::swatches::Color> own = doc ? fla::swatches::of(*doc) : std::vector<fla::swatches::Color>();
    if ((_ownColors && own != _shown) || (!_ownColors && !own.empty()) || itemsOf(fla::swatches::shownGradients(doc)) != _gradientItems)
    {
        rebuild();
        return;
    }
    // A palette color changed: the tags may have.
    std::vector<std::string> tags(_shown.size());
    if (doc)
    {
        const fla::palettes::Palettes palettes = fla::palettes::load(*doc);
        if (const fla::palettes::Palette* active = palettes.activePalette())
            for (size_t i = 0; i < _shown.size(); ++i)
                for (const fla::palettes::Color& color : active->colors)
                    if (color.rgba[0] == _shown[i][0] && color.rgba[1] == _shown[i][1] && color.rgba[2] == _shown[i][2])
                        tags[i] = color.name;
    }
    if (tags != _tags)
        rebuild();
}

void SwatchesPanel::rebuild()
{
    const fla::Document* doc = _document ? _document->document : nullptr;
    _shown = doc ? fla::swatches::of(*doc) : std::vector<fla::swatches::Color>();
    _ownColors = !_shown.empty();
    if (!_ownColors)
        _shown = fla::swatches::defaults();
    // Tagged: a color of the palette in use has the swatch's color.
    _tags.assign(_shown.size(), std::string());
    if (doc)
    {
        const fla::palettes::Palettes palettes = fla::palettes::load(*doc);
        if (const fla::palettes::Palette* active = palettes.activePalette())
            for (size_t i = 0; i < _shown.size(); ++i)
                for (const fla::palettes::Color& color : active->colors)
                    if (color.rgba[0] == _shown[i][0] && color.rgba[1] == _shown[i][1] && color.rgba[2] == _shown[i][2])
                        _tags[i] = color.name;
    }
    _grid->setColors(_shown, _tags);
    _gradients = fla::swatches::shownGradients(doc);
    _gradientItems = itemsOf(_gradients);
    _gradientGrid->setGradients(_gradients);
}

namespace {

/// The default colors saved with Save as Default, or the built-in ones.
std::vector<fla::swatches::Color> savedDefaults()
{
    const QStringList saved = QSettings().value("swatches/default").toStringList();
    if (saved.isEmpty())
        return fla::swatches::defaults();
    std::vector<fla::swatches::Color> colors;
    for (const QString& name : saved)
    {
        const QColor color(name);
        if (color.isValid())
            colors.push_back({static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
                static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(color.alpha())});
    }
    return colors;
}

} // namespace

void SwatchesPanel::showMenu(const QPoint& where)
{
    QMenu menu(this);
    QAction* sort = menu.addAction("Sort by Color");
    QAction* clear = menu.addAction("Clear Colors");
    clear->setToolTip("Leaves black and white");
    menu.addSeparator();
    QAction* loadDefault = menu.addAction("Load Default Colors");
    QAction* saveDefault = menu.addAction("Save as Default");
    QAction* web = menu.addAction("Web 216");
    menu.addSeparator();
    QAction* addColors = menu.addAction("Add Colors...");
    QAction* replaceColors = menu.addAction("Replace Colors...");
    QAction* saveColors = menu.addAction("Save Colors...");
    QAction* chosen = menu.exec(where);
    if (!chosen)
        return;
    if (chosen == sort)
        emit colorsEditRequested(fla::swatches::sortedByColor(_shown), "Sort by Color");
    else if (chosen == clear)
        emit colorsEditRequested({{0, 0, 0, 255}, {255, 255, 255, 255}}, "Clear Colors");
    else if (chosen == loadDefault)
        emit colorsEditRequested(savedDefaults(), "Load Default Colors");
    else if (chosen == saveDefault)
    {
        QStringList names;
        for (const auto& c : _shown)
            names << QColor(c[0], c[1], c[2], c[3]).name(QColor::HexArgb);
        QSettings().setValue("swatches/default", names);
    }
    else if (chosen == web)
    {
        // The web-safe cube alone, from the built-in palette's end.
        const std::vector<fla::swatches::Color> all = fla::swatches::defaults();
        emit colorsEditRequested(std::vector<fla::swatches::Color>(all.end() - 216, all.end()), "Web 216");
    }
    else if (chosen == addColors)
        importColors(false);
    else if (chosen == replaceColors)
        importColors(true);
    else if (chosen == saveColors)
        exportColors();
}

void SwatchesPanel::importColors(bool replace)
{
    const QString path = QFileDialog::getOpenFileName(this, replace ? "Replace Colors" : "Add Colors", QString(),
        "Color tables (*.act *.gpl);;Adobe Color Table (*.act);;GIMP Palette (*.gpl)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    std::vector<fla::swatches::Color> read = path.endsWith(".gpl", Qt::CaseInsensitive)
        ? fla::swatches::readGpl(bytes.toStdString())
        : fla::swatches::readAct(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    if (read.empty())
    {
        modal::information(this, "Swatches", "No colors could be read from that file.");
        return;
    }
    std::vector<fla::swatches::Color> colors = replace ? std::vector<fla::swatches::Color>() : _shown;
    colors.insert(colors.end(), read.begin(), read.end());
    emit colorsEditRequested(colors, replace ? "Replace Colors" : "Add Colors");
}

void SwatchesPanel::exportColors()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Colors", "Swatches.act",
        "Adobe Color Table (*.act);;GIMP Palette (*.gpl)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    if (path.endsWith(".gpl", Qt::CaseInsensitive))
        file.write(QByteArray::fromStdString(fla::swatches::writeGpl(_shown, QFileInfo(path).completeBaseName().toStdString())));
    else
    {
        const std::vector<uint8_t> bytes = fla::swatches::writeAct(_shown);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
        if (_shown.size() > 256)
            modal::information(this, "Save Colors", "A color table holds 256 colors; the first 256 were saved.");
    }
}
