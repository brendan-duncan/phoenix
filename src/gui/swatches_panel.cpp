#include "swatches_panel.h"

#include "../data/document.h"
#include "../data/fla_document.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kCell = 14;

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

    void setColors(const std::vector<fla::swatches::Color>& colors)
    {
        _colors = colors;
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
            if (c[3] < 255)
            {
                painter.fillRect(cell, Qt::white);
                painter.fillRect(QRect(x, y, kCell / 2, kCell / 2), QColor(204, 204, 204));
                painter.fillRect(QRect(x + kCell / 2, y + kCell / 2, kCell / 2, kCell / 2), QColor(204, 204, 204));
            }
            painter.fillRect(cell, QColor(c[0], c[1], c[2], c[3]));
            painter.setPen(QColor(30, 30, 30));
            painter.drawRect(cell.adjusted(0, 0, -1, -1));
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
            if (at >= 0)
            {
                const auto& c = _colors[static_cast<size_t>(at)];
                setToolTip(QColor(c[0], c[1], c[2]).name().toUpper() +
                    (c[3] < 255 ? QString(" %1%").arg(qRound(c[3] / 2.55)) : QString()));
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
    int _hover = -1;
};

SwatchesPanel::SwatchesPanel(QWidget* parent)
    : QWidget(parent)
{
    _grid = new Grid(this);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidget(_grid);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    _grid->clicked = [this](int index) {
        if (index < 0 || index >= static_cast<int>(_shown.size()))
            return;
        const auto& c = _shown[static_cast<size_t>(index)];
        emit colorPicked(QColor(c[0], c[1], c[2], c[3]), _stroke->isChecked());
    };
    _grid->rightClicked = [this](int index, const QPoint& where) {
        // Only the document's own swatches can be deleted; the default
        // palette is not saved anywhere.
        if (!_ownColors)
            return;
        QMenu menu(this);
        QAction* remove = menu.addAction("Delete Swatch");
        if (menu.exec(where) == remove)
            emit removeRequested(index);
    };

    _stroke = new QCheckBox("Stroke", this);
    _stroke->setToolTip("A click sets the stroke color instead of the fill color");
    QPushButton* add = new QPushButton("Add Swatch", this);
    add->setToolTip("Keep the fill color among the document's swatches");
    connect(add, &QPushButton::clicked, this, &SwatchesPanel::addRequested);

    QHBoxLayout* footer = new QHBoxLayout();
    footer->addWidget(_stroke);
    footer->addStretch();
    footer->addWidget(add);

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
    if ((_ownColors && own != _shown) || (!_ownColors && !own.empty()))
        rebuild();
}

void SwatchesPanel::rebuild()
{
    const fla::Document* doc = _document ? _document->document : nullptr;
    _shown = doc ? fla::swatches::of(*doc) : std::vector<fla::swatches::Color>();
    _ownColors = !_shown.empty();
    if (!_ownColors)
        _shown = fla::swatches::defaults();
    _grid->setColors(_shown);
}
