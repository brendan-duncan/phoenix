#include "swatch_popup.h"

#include "color_picker_widgets.h"
#include "modal.h"

#include "phoenix_view.h"

#include "../data/document.h"
#include "../data/fill_style.h"
#include "../data/linear_gradient.h"
#include "../data/radial_gradient.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../edit/element_commands.h"
#include "../edit/shape_merge.h"
#include "../edit/swatches.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/// The pop-ups open now.
std::vector<SwatchPopup*>& livePopups()
{
    static std::vector<SwatchPopup*> popups;
    return popups;
}

/// The grid: twelve swatches a row, each this many pixels with a pixel
/// between.
constexpr int kColumns = 12;
constexpr int kCell = 14;
constexpr int kPitch = kCell + 1;
/// The room between the colors, the gradients and the recent colors.
constexpr int kGap = 6;

QColor colorOf(const uint8_t rgba[4])
{
    return QColor(rgba[0], rgba[1], rgba[2], rgba[3]);
}

void paintChecker(QPainter& painter, const QRect& rect)
{
    painter.fillRect(rect, QColor(200, 200, 200));
    const int half = std::max(2, rect.width() / 4);
    painter.save();
    painter.setClipRect(rect);
    for (int y = rect.top(); y <= rect.bottom(); y += half)
        for (int x = rect.left() + (((y - rect.top()) / half) % 2) * half; x <= rect.right(); x += half * 2)
            painter.fillRect(QRect(x, y, half, half), QColor(150, 150, 150));
    painter.restore();
}

/// No color: white, crossed out in red.
void paintNone(QPainter& painter, const QRectF& rect)
{
    painter.fillRect(rect, Qt::white);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(220, 0, 0), 2.0));
    painter.drawLine(rect.bottomLeft(), rect.topRight());
    painter.restore();
}

QString hexOf(const QColor& color)
{
    return color.name(QColor::HexRgb).toUpper();
}

} // namespace

QBrush brushOf(const fla::swatches::Gradient& gradient)
{
    QGradient shape = gradient.radial ? QGradient(QRadialGradient(0.5, 0.5, 0.5)) : QGradient(QLinearGradient(0.0, 0.0, 1.0, 0.0));
    shape.setCoordinateMode(QGradient::ObjectBoundingMode);
    for (const fla::DrawingStyle::Stop& stop : gradient.stops)
        shape.setColorAt(std::clamp(stop.ratio, 0.0, 1.0), colorOf(stop.color));
    return QBrush(shape);
}

QBrush brushOf(const fla::FillStyle& fill)
{
    fla::swatches::Gradient gradient;
    switch (fill.type())
    {
    case fla::FillStyle::Type::SolidColor:
        return QBrush(colorOf(static_cast<const fla::SolidColor&>(fill).color));
    case fla::FillStyle::Type::LinearGradient:
        for (const fla::GradientEntry& entry : static_cast<const fla::LinearGradient&>(fill).entries)
        {
            fla::DrawingStyle::Stop stop;
            stop.ratio = entry.ratio;
            std::copy(entry.color, entry.color + 4, stop.color);
            gradient.stops.push_back(stop);
        }
        return brushOf(gradient);
    case fla::FillStyle::Type::RadialGradient:
        gradient.radial = true;
        for (const fla::RadialEntry& entry : static_cast<const fla::RadialGradient&>(fill).entries)
        {
            fla::DrawingStyle::Stop stop;
            stop.ratio = entry.ratio;
            std::copy(entry.color, entry.color + 4, stop.color);
            gradient.stops.push_back(stop);
        }
        return brushOf(gradient);
    case fla::FillStyle::Type::BitmapFill:
        break;
    }
    return QBrush(QColor(90, 90, 90), Qt::DiagCrossPattern);
}

QBrush brushOfStyleFill(const fla::DrawingStyle& style)
{
    using Kind = fla::DrawingStyle::FillKind;
    if ((style.fillKind == Kind::Linear || style.fillKind == Kind::Radial) && style.gradient.size() >= 2)
    {
        fla::swatches::Gradient gradient;
        gradient.radial = style.fillKind == Kind::Radial;
        gradient.stops = style.gradient;
        return brushOf(gradient);
    }
    if (style.fillKind == Kind::Bitmap && !style.fillBitmap.empty())
        return QBrush(QColor(90, 90, 90), Qt::DiagCrossPattern);
    return QBrush(colorOf(style.fillColor));
}

QIcon brushSwatchIcon(const QBrush& brush, bool stroke)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    if (stroke)
    {
        QPainterPath ring;
        ring.setFillRule(Qt::OddEvenFill);
        ring.addRect(0, 0, 20, 20);
        ring.addRect(6, 6, 8, 8);
        painter.setClipPath(ring);
    }
    // A checker under it so a transparent color reads as one.
    painter.fillRect(0, 0, 20, 20, QColor(120, 120, 120));
    painter.fillRect(0, 0, 10, 10, QColor(160, 160, 160));
    painter.fillRect(10, 10, 10, 10, QColor(160, 160, 160));
    if (brush.style() == Qt::NoBrush)
        paintNone(painter, QRectF(1, 1, 18, 18));
    else
        painter.fillRect(stroke ? QRect(0, 0, 20, 20) : QRect(1, 1, 18, 18), brush);
    painter.setClipping(false);
    painter.setPen(QColor(30, 30, 30));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, 19, 19);
    if (stroke)
        painter.drawRect(5, 5, 9, 9);
    return QIcon(pixmap);
}

/// The swatch the color or gradient under the pointer shows in, before
/// the hex field.
class SwatchPopup::Chip : public QWidget
{
public:
    explicit Chip(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(30, 20);
    }

    void show(const QBrush& brush)
    {
        _brush = brush;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const QRect box = rect().adjusted(0, 0, -1, -1);
        paintChecker(painter, box);
        if (_brush.style() == Qt::NoBrush)
            paintNone(painter, QRectF(box));
        else
            painter.fillRect(box, _brush);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(box);
    }

private:
    QBrush _brush;
};

/// The swatches themselves: the colors, then the gradients, then the
/// recent colors, each a block of rows.
class SwatchPopup::Grid : public QWidget
{
public:
    explicit Grid(SwatchPopup* popup)
        : QWidget(popup)
        , _popup(popup)
    {
        setObjectName("swatchPopupGrid");
        setMouseTracking(true);
    }

    void setItems(std::vector<Item> items)
    {
        // Laid out block by block, each starting a new row.
        int y = 0;
        Item::Kind block = items.empty() ? Item::Kind::Solid : items.front().kind;
        int column = 0;
        for (Item& item : items)
        {
            if (item.kind != block)
            {
                block = item.kind;
                if (column > 0)
                    y += kPitch;
                y += kGap;
                column = 0;
            }
            item.rect = QRect(1 + column * kPitch, 1 + y, kCell, kCell);
            if (++column == kColumns)
            {
                column = 0;
                y += kPitch;
            }
        }
        if (column > 0)
            y += kPitch;
        _items = std::move(items);
        _hover = -1;
        setFixedSize(kColumns * kPitch + 1, y + 1);
        update();
    }

    const std::vector<Item>& items() const { return _items; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        for (size_t i = 0; i < _items.size(); ++i)
        {
            const Item& item = _items[i];
            if (item.kind == Item::Kind::Gradient)
            {
                painter.fillRect(item.rect, brushOf(item.gradient));
            }
            else
            {
                if (item.color.alpha() < 255)
                    paintChecker(painter, item.rect);
                painter.fillRect(item.rect, item.color);
            }
        }
        // The one under the pointer outlined, light and dark so it shows
        // on any color.
        if (_hover >= 0 && _hover < static_cast<int>(_items.size()))
        {
            const QRect box = _items[_hover].rect.adjusted(-1, -1, 0, 0);
            painter.setPen(Qt::white);
            painter.drawRect(box);
            painter.setPen(Qt::black);
            painter.drawRect(box.adjusted(-1, -1, 1, 1));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int at = itemAt(event->position().toPoint());
        if (at == _hover)
            return;
        _hover = at;
        update();
        _popup->preview(at >= 0 ? &_items[at] : nullptr);
    }

    void leaveEvent(QEvent*) override
    {
        _hover = -1;
        update();
        _popup->preview(nullptr);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const int at = itemAt(event->position().toPoint());
        if (at >= 0)
            _popup->pick(_items[at]);
    }

private:
    int itemAt(const QPoint& point) const
    {
        for (size_t i = 0; i < _items.size(); ++i)
            if (_items[i].rect.adjusted(0, 0, 1, 1).contains(point))
                return static_cast<int>(i);
        return -1;
    }

    SwatchPopup* _popup;
    std::vector<Item> _items;
    int _hover = -1;
};

SwatchPopup::SwatchPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup)
{
    setObjectName("swatchPopup");
    setAttribute(Qt::WA_DeleteOnClose);
    livePopups().push_back(this);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    QHBoxLayout* top = new QHBoxLayout();
    top->setSpacing(4);
    _chip = new Chip(this);
    top->addWidget(_chip);
    _hex = new QLineEdit(this);
    _hex->setObjectName("swatchPopupHex");
    _hex->setFixedWidth(68);
    _hex->setToolTip("The color as hex; type one and press Enter to use it");
    connect(_hex, &QLineEdit::returnPressed, this, &SwatchPopup::pickTyped);
    top->addWidget(_hex);
    _alpha = new QSpinBox(this);
    _alpha->setObjectName("swatchPopupAlpha");
    _alpha->setRange(0, 100);
    _alpha->setSuffix("%");
    _alpha->setToolTip("Alpha: the opacity of the color picked; press Enter to use it with the current color");
    _alpha->setButtonSymbols(QAbstractSpinBox::NoButtons);
    _alpha->setFixedWidth(44);
    connect(_alpha, &QSpinBox::editingFinished, this, [this]() {
        // Enter in the field, not the focus leaving as the pop-up closes.
        if (isVisible() && _alpha->hasFocus() && withAlpha(_current) != _current)
            pickTyped();
    });
    top->addWidget(_alpha);
    top->addStretch();
    _none = new QToolButton(this);
    _none->setObjectName("swatchPopupNone");
    _none->setAutoRaise(true);
    _none->setIconSize(QSize(16, 16));
    _none->setIcon(brushSwatchIcon(QBrush(), false));
    _none->setToolTip("No color");
    connect(_none, &QToolButton::clicked, this, [this]() {
        previewShapes(nullptr);
        _previewElements.clear();
        emit nonePicked();
        close();
    });
    top->addWidget(_none);
    _picker = new QToolButton(this);
    _picker->setObjectName("swatchPopupPicker");
    _picker->setAutoRaise(true);
    _picker->setIconSize(QSize(16, 16));
    {
        // A hue ring for the full picker.
        QPixmap wheel(32, 32);
        wheel.fill(Qt::transparent);
        QPainter painter(&wheel);
        painter.setRenderHint(QPainter::Antialiasing);
        QConicalGradient hues(16, 16, 0);
        for (int i = 0; i <= 6; ++i)
            hues.setColorAt(i / 6.0, QColor::fromHsv((360 - i * 60) % 360, 255, 255));
        painter.setBrush(hues);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(2, 2, 28, 28));
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRectF(10, 10, 12, 12));
        _picker->setIcon(QIcon(wheel));
    }
    _picker->setToolTip("More colors: the full color picker");
    connect(_picker, &QToolButton::clicked, this, &SwatchPopup::openPicker);
    top->addWidget(_picker);
    layout->addLayout(top);

    _grid = new Grid(this);
    layout->addWidget(_grid, 0, Qt::AlignHCenter);
    setDocument(nullptr);
}

SwatchPopup* SwatchPopup::open(QWidget* anchor, const fla::Document* document, const QColor& current,
    const QString& title, const Offer& offer)
{
    // Owned by the window rather than the swatch, which a panel may rebuild
    // while a pick is being applied.
    SwatchPopup* popup = new SwatchPopup(anchor ? anchor->window() : nullptr);
    popup->_title = title;
    popup->setOffer(offer);
    popup->setDocument(document);
    popup->setCurrent(current);
    popup->popupUnder(anchor);
    return popup;
}

void SwatchPopup::getColor(QWidget* anchor, const fla::Document* document, const QColor& current,
    const QString& title, bool alpha, std::function<void(const QColor&)> done)
{
    Offer offer;
    offer.alpha = alpha;
    SwatchPopup* popup = open(anchor, document, current, title, offer);
    // The answer is the last color picked, given once the pop-up is gone,
    // the full picker it may open included.
    auto chosen = std::make_shared<QColor>();
    connect(popup, &SwatchPopup::colorPicked, popup, [chosen](const QColor& color) { *chosen = color; });
    if (modal::cannotWait())
    {
        // Answered after the pop-up's deletion rather than from inside it,
        // and not at all if what asked is gone by then.
        QObject* context = anchor ? static_cast<QObject*>(anchor) : QCoreApplication::instance();
        connect(popup, &QObject::destroyed, context, [chosen, done]() {
            if (chosen->isValid() && done)
                done(*chosen);
        }, Qt::QueuedConnection);
        return;
    }
    QEventLoop loop;
    connect(popup, &QObject::destroyed, &loop, &QEventLoop::quit);
    loop.exec();
    if (chosen->isValid() && done)
        done(*chosen);
}

QColor SwatchPopup::getColor(QWidget* anchor, const fla::Document* document, const QColor& current,
    const QString& title, bool alpha)
{
    QColor chosen;
    if (!modal::cannotWait())
        getColor(anchor, document, current, title, alpha, [&chosen](const QColor& color) { chosen = color; });
    return chosen;
}

void SwatchPopup::setDocument(const fla::Document* document)
{
    // The document's own colors and gradients, or the defaults where it
    // has none.
    std::vector<Item> items;
    for (const fla::swatches::Color& color : fla::swatches::shownColors(document))
    {
        Item item;
        item.color = QColor(color[0], color[1], color[2], color[3]);
        items.push_back(item);
    }
    if (_offersGradients)
    {
        for (const fla::swatches::Gradient& gradient : fla::swatches::shownGradients(document))
        {
            Item item;
            item.kind = Item::Kind::Gradient;
            item.gradient = gradient;
            items.push_back(item);
        }
    }
    const std::vector<fla::ColorHistory::Color>& recent = RecentColors::instance().colors();
    for (size_t i = 0; i < recent.size() && i < static_cast<size_t>(kColumns); ++i)
    {
        Item item;
        item.kind = Item::Kind::Recent;
        item.color = QColor(recent[i][0], recent[i][1], recent[i][2], recent[i][3]);
        items.push_back(item);
    }
    _grid->setItems(std::move(items));
    adjustSize();
}

void SwatchPopup::setOffer(const Offer& offer)
{
    _offersGradients = offer.gradients;
    _offersAlpha = offer.alpha;
    _alpha->setVisible(offer.alpha);
    _none->setVisible(offer.none);
}

void SwatchPopup::setCurrent(const QColor& color)
{
    _current = color;
    _currentBrush = color.isValid() ? QBrush(color) : QBrush();
    _alpha->setValue(color.isValid() ? qRound(color.alphaF() * 100.0) : 100);
    preview(nullptr);
}

void SwatchPopup::setCurrentBrush(const QBrush& brush)
{
    _currentBrush = brush;
    preview(nullptr);
}

void SwatchPopup::popupUnder(QWidget* anchor)
{
    adjustSize();
    QPoint at = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos();
    if (const QScreen* screen = QGuiApplication::screenAt(at))
    {
        // Kept on its screen: above the swatch when there is no room under it.
        const QRect room = screen->availableGeometry();
        if (anchor && at.y() + height() > room.bottom())
            at.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height());
        at.setX(std::clamp(at.x(), room.left(), std::max(room.left(), room.right() - width())));
        at.setY(std::clamp(at.y(), room.top(), std::max(room.top(), room.bottom() - height())));
    }
    move(at);
    show();
    raise();
    activateWindow();
}

SwatchPopup::~SwatchPopup()
{
    previewShapes(nullptr);
    std::vector<SwatchPopup*>& popups = livePopups();
    popups.erase(std::remove(popups.begin(), popups.end(), this), popups.end());
}

bool SwatchPopup::previewing()
{
    for (const SwatchPopup* popup : livePopups())
    {
        if (!popup->_previews.empty())
            return true;
    }
    return false;
}

void SwatchPopup::closeAll()
{
    // A copy: closing may delete them.
    const std::vector<QPointer<SwatchPopup>> popups(livePopups().begin(), livePopups().end());
    for (const QPointer<SwatchPopup>& popup : popups)
    {
        if (popup)
        {
            popup->previewShapes(nullptr);
            popup->close();
        }
    }
}

void SwatchPopup::previewOn(PhoenixView* view, const std::vector<fla::Element*>& elements, bool strokes)
{
    previewShapes(nullptr);
    _view = view;
    _previewElements = elements;
    _previewStrokes = strokes;
}

void SwatchPopup::previewShapes(const Item* item)
{
    // Each preview undone, putting its element back as it was.
    const bool restored = !_previews.empty();
    for (size_t i = _previews.size(); i-- > 0;)
    {
        _previews[i].first->undo();
        if (_view && _previews[i].second)
            _view->forgetShape(_previews[i].second);
    }
    _previews.clear();
    if (!item || !_view)
    {
        // Settled: whatever else drew the shapes is redrawn once.
        if (restored && _view)
        {
            _view->clearCaches();
            _view->update();
        }
        return;
    }
    const QColor color = withAlpha(item->color);
    const fla::swatches::Color rgba = {static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
        static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(color.alpha())};
    bool primitives = false;
    for (fla::Element* element : _previewElements)
    {
        if (!element)
            continue;
        if (fla::swatches::isPrimitive(*element))
        {
            // A rectangle or oval primitive keeps its own fill and stroke,
            // swapped in place.
            std::unique_ptr<fla::Command> paint(item->kind == Item::Kind::Gradient
                    ? fla::swatches::paintPrimitive(*element, _previewStrokes, item->gradient, "Preview")
                    : fla::swatches::paintPrimitive(*element, _previewStrokes, rgba, "Preview"));
            if (!paint)
                continue;
            paint->redo();
            _previews.emplace_back(std::move(paint), nullptr);
            primitives = true;
            continue;
        }
        if (element->elementType() != fla::Element::Type::Shape)
            continue;
        fla::Shape* shape = static_cast<fla::Shape*>(element);
        std::unique_ptr<fla::Shape> after(fla::cloneShape(*shape, nullptr));
        if (item->kind == Item::Kind::Gradient)
        {
            if (_previewStrokes)
                fla::swatches::paintStrokes(*after, item->gradient);
            else
                fla::swatches::paintFills(*after, item->gradient);
        }
        else if (_previewStrokes)
            fla::swatches::paintStrokes(*after, rgba);
        else
            fla::swatches::paintFills(*after, rgba);
        _previews.emplace_back(std::unique_ptr<fla::Command>(new fla::SetShapeContentsCommand(shape, *after, "Preview")), shape);
        _previews.back().first->redo();
        _view->forgetShape(shape);
    }
    if (primitives)
    {
        _view->clearCaches();
        _view->update();
    }
}

void SwatchPopup::hideEvent(QHideEvent* event)
{
    previewShapes(nullptr);
    QWidget::hideEvent(event);
}

void SwatchPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SwatchPopup::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void SwatchPopup::preview(const Item* item)
{
    previewShapes(item);
    if (!item)
    {
        _chip->show(_currentBrush);
        _hex->setText(_current.isValid() && _currentBrush.gradient() == nullptr ? hexOf(_current) : QString());
        return;
    }
    if (item->kind == Item::Kind::Gradient)
    {
        _chip->show(brushOf(item->gradient));
        _hex->setText(QString());
        return;
    }
    _chip->show(QBrush(withAlpha(item->color)));
    _hex->setText(hexOf(item->color));
}

QColor SwatchPopup::withAlpha(QColor color) const
{
    // A swatch saved with its own alpha keeps it; an opaque one takes the
    // alpha field's.
    if (!_offersAlpha)
        color.setAlpha(255);
    else if (color.alpha() == 255)
        color.setAlpha(qRound(_alpha->value() * 255.0 / 100.0));
    return color;
}

void SwatchPopup::pick(const Item& item)
{
    // Kept alive through the signal, which may rebuild what owns it. The
    // shapes go back as they were first, so the pick is one undo step.
    QPointer<SwatchPopup> self(this);
    previewShapes(nullptr);
    _previewElements.clear();
    if (item.kind == Item::Kind::Gradient)
    {
        emit gradientPicked(item.gradient);
    }
    else
    {
        const QColor color = withAlpha(item.color);
        RecentColors::instance().add(color);
        emit colorPicked(color);
    }
    if (self)
        close();
}

void SwatchPopup::pickTyped()
{
    QString text = _hex->text().trimmed();
    if (!text.startsWith('#'))
        text.prepend('#');
    QColor color(text);
    if (!color.isValid() || !QRegularExpression("^#[0-9A-Fa-f]{6}$").match(text).hasMatch())
        color = _current.isValid() ? _current : QColor(Qt::black);
    color.setAlpha(255);
    Item item;
    item.color = color;
    pick(item);
}

void SwatchPopup::openPicker()
{
    // The pop-up goes; the dialog opens over the window it was in.
    QWidget* owner = parentWidget();
    const QColor start = _current.isValid() ? _current : QColor(Qt::black);
    const QString title = _title;
    const QColorDialog::ColorDialogOptions options =
        _offersAlpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions();
    QPointer<SwatchPopup> self(this);
    hide();
    // Built by hand rather than with modal::getColor(): a cancel has to close
    // the pop-up too.
    QColorDialog* dialog = new QColorDialog(start, owner);
    dialog->setWindowTitle(title.isEmpty() ? QString("Color") : title);
    dialog->setOptions(options);
    modal::open(dialog, [self, dialog](int result) {
        if (!self)
            return;
        const QColor chosen = result == QDialog::Accepted ? dialog->selectedColor() : QColor();
        if (chosen.isValid())
        {
            RecentColors::instance().add(chosen);
            emit self->colorPicked(chosen);
        }
        if (self)
            self->close();
    });
}
