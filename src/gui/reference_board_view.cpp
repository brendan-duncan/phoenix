#include "reference_board_view.h"

#include "phoenix_view.h"

#include "../data/document.h"

#include <QActionGroup>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace refs = fla::references;

namespace {

/// How near, in screen pixels, a handle has to be to be grabbed.
constexpr double kHandleReach = 8.0;
/// How far above a picture its turning handle sits, in screen pixels.
constexpr double kTurnHandleOffset = 22.0;

double distance(const QPointF& a, const QPointF& b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

/// The picture's corners on screen, clockwise from its top left, and where
/// its turning handle is.
struct ScreenHandles
{
    QPointF corners[4];
    QPointF turn;
    QPointF center;
};

/// \a toWidget maps the picture's pixels to the screen.
ScreenHandles handlesOf(const std::function<QPointF(const QPointF&)>& toWidget, const QSizeF& size)
{
    ScreenHandles handles;
    handles.corners[0] = toWidget(QPointF(0, 0));
    handles.corners[1] = toWidget(QPointF(size.width(), 0));
    handles.corners[2] = toWidget(QPointF(size.width(), size.height()));
    handles.corners[3] = toWidget(QPointF(0, size.height()));
    handles.center = toWidget(QPointF(size.width() / 2.0, size.height() / 2.0));
    const QPointF top = (handles.corners[0] + handles.corners[1]) / 2.0;
    const QPointF out = top - handles.center;
    const double length = std::hypot(out.x(), out.y());
    handles.turn = length > 0.0 ? top + out / length * kTurnHandleOffset : top - QPointF(0, kTurnHandleOffset);
    return handles;
}

} // namespace

ReferenceBoardView::ReferenceBoardView(PhoenixView* view, std::function<fla::Document*()> document,
    std::function<void(fla::CommandPtr)> push)
    : _view(view)
    , _document(std::move(document))
    , _push(std::move(push))
{
}

refs::Board ReferenceBoardView::board() const
{
    if (_preview)
        return *_preview;
    const fla::Document* current = document();
    return current ? refs::of(*current) : refs::Board();
}

const QImage* ReferenceBoardView::image(const std::string& path) const
{
    const fla::Document* current = document();
    if (!current)
        return nullptr;
    if (_imagesFor != current)
    {
        _images.clear();
        _imagesFor = current;
    }
    const std::vector<uint8_t>& bytes = refs::bytesOf(*current, path);
    if (bytes.empty())
        return nullptr;
    // A path freed by a removal may be used again by a later picture: the
    // bytes' place tells them apart.
    const QString key = QString::fromStdString(path) + "@" +
        QString::number(reinterpret_cast<quintptr>(bytes.data()));
    auto found = _images.find(key);
    if (found == _images.end())
        found = _images.insert(key, QImage::fromData(bytes.data(), static_cast<int>(bytes.size())));
    return found->isNull() ? nullptr : &found.value();
}

QSizeF ReferenceBoardView::imageSize(int index) const
{
    const refs::Board current = board();
    if (index < 0 || index >= static_cast<int>(current.images.size()))
        return QSizeF();
    const QImage* picture = image(current.images[static_cast<size_t>(index)].path);
    return picture ? QSizeF(picture->size()) : QSizeF();
}

QTransform ReferenceBoardView::imageToStage(const refs::Image& picture) const
{
    const QImage* pixels = image(picture.path);
    const QSizeF size = pixels ? QSizeF(pixels->size()) : QSizeF(0, 0);
    QTransform transform;
    transform.translate(picture.x, picture.y);
    transform.rotate(picture.rotation);
    transform.scale(picture.scale, picture.scale);
    transform.translate(-size.width() / 2.0, -size.height() / 2.0);
    return transform;
}

void ReferenceBoardView::paint(QPainter& painter, const QTransform& rootToWidget)
{
    const refs::Board current = board();
    if (current.hidden || current.images.empty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const refs::Image& picture : current.images)
    {
        const QImage* pixels = picture.hidden ? nullptr : image(picture.path);
        if (!pixels)
            continue;
        painter.setTransform(imageToStage(picture) * rootToWidget);
        painter.setOpacity(picture.opacity);
        painter.drawImage(QPointF(0, 0), *pixels);
    }
    painter.setOpacity(1.0);
    painter.resetTransform();

    // The selected one's handles, while they are being arranged.
    const bool arranging = _view && dynamic_cast<ReferenceTool*>(_view->activeTool());
    if (arranging && _selected >= 0 && _selected < static_cast<int>(current.images.size()))
    {
        const refs::Image& picture = current.images[static_cast<size_t>(_selected)];
        const QImage* pixels = image(picture.path);
        if (pixels && !picture.hidden)
        {
            const QTransform toWidget = imageToStage(picture) * rootToWidget;
            const ScreenHandles handles =
                handlesOf([&toWidget](const QPointF& point) { return toWidget.map(point); }, pixels->size());
            const QPolygonF outline(QVector<QPointF>(std::begin(handles.corners), std::end(handles.corners)));
            const QColor accent(0, 160, 255);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(accent, 1.0, picture.locked ? Qt::DashLine : Qt::SolidLine));
            painter.drawPolygon(outline);
            if (!picture.locked)
            {
                painter.setBrush(Qt::white);
                for (const QPointF& corner : handles.corners)
                    painter.drawRect(QRectF(corner - QPointF(4, 4), QSizeF(8, 8)));
                const QPointF top = (handles.corners[0] + handles.corners[1]) / 2.0;
                painter.drawLine(top, handles.turn);
                painter.drawEllipse(handles.turn, 4.5, 4.5);
            }
        }
    }
    painter.restore();
}

int ReferenceBoardView::imageAt(const QPointF& stagePos) const
{
    const refs::Board current = board();
    if (current.hidden)
        return -1;
    for (int i = static_cast<int>(current.images.size()) - 1; i >= 0; --i)
    {
        const refs::Image& picture = current.images[static_cast<size_t>(i)];
        const QImage* pixels = picture.hidden ? nullptr : image(picture.path);
        if (!pixels)
            continue;
        bool invertible = false;
        const QPointF local = imageToStage(picture).inverted(&invertible).map(stagePos);
        if (invertible && QRectF(QPointF(0, 0), QSizeF(pixels->size())).contains(local))
            return i;
    }
    return -1;
}

std::optional<QColor> ReferenceBoardView::colorAt(const QPointF& stagePos) const
{
    const refs::Board current = board();
    if (current.hidden)
        return std::nullopt;
    for (int i = static_cast<int>(current.images.size()) - 1; i >= 0; --i)
    {
        const refs::Image& picture = current.images[static_cast<size_t>(i)];
        const QImage* pixels = picture.hidden ? nullptr : image(picture.path);
        if (!pixels)
            continue;
        bool invertible = false;
        const QPointF local = imageToStage(picture).inverted(&invertible).map(stagePos);
        const QPoint pixel(static_cast<int>(std::floor(local.x())), static_cast<int>(std::floor(local.y())));
        if (!invertible || !pixels->rect().contains(pixel))
            continue;
        // Its own color, whatever opacity it is shown at; through a clear
        // part to the picture below.
        const QColor color = pixels->pixelColor(pixel);
        if (color.alpha() > 0)
            return color;
    }
    return std::nullopt;
}

QStringList ReferenceBoardView::addFiles(const QStringList& paths, const QPointF* stagePos)
{
    QStringList failed;
    fla::Document* current = document();
    if (!current)
        return paths;
    refs::Board changed = refs::of(*current);
    refs::Files added;
    // Beside the stage, right of whatever is there already.
    double right = current->width;
    for (const refs::Image& picture : changed.images)
    {
        const QSizeF size = imageSize(static_cast<int>(&picture - changed.images.data()));
        right = std::max(right, picture.x + size.width() * picture.scale / 2.0);
    }
    int placed = 0;
    for (const QString& path : paths)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            failed << QFileInfo(path).fileName();
            continue;
        }
        QByteArray data = file.readAll();
        QImage pixels = QImage::fromData(data);
        if (pixels.isNull())
        {
            failed << QFileInfo(path).fileName();
            continue;
        }
        // PNG and JPEG are kept as they are; anything else is kept as PNG,
        // which every Phoenix can read back.
        QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == "jpeg")
            suffix = "jpg";
        if (suffix != "png" && suffix != "jpg")
        {
            data.clear();
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            pixels.save(&buffer, "PNG");
            suffix = "png";
        }
        refs::Image picture;
        // A path not in the package and not taken by this drop either.
        picture.path = refs::newPath(*current, suffix.toStdString(), added);
        picture.name = QFileInfo(path).completeBaseName().toStdString();
        // Large pictures come in at most about the stage's size.
        const double fit = std::min({1.0, current->height * 0.8 / std::max(1, pixels.height()),
            current->width * 0.8 / std::max(1, pixels.width())});
        picture.scale = fit > 0.0 ? fit : 1.0;
        const double width = pixels.width() * picture.scale;
        const double height = pixels.height() * picture.scale;
        if (stagePos)
        {
            picture.x = stagePos->x() + placed * 20.0;
            picture.y = stagePos->y() + placed * 20.0;
        }
        else
        {
            picture.x = right + 40.0 + width / 2.0;
            picture.y = height / 2.0;
            right = picture.x + width / 2.0;
        }
        added.emplace_back(picture.path,
            fla::LazyBytes(std::vector<uint8_t>(data.constData(), data.constData() + data.size())));
        changed.images.push_back(picture);
        ++placed;
    }
    if (placed > 0)
    {
        _push(fla::CommandPtr(new refs::SetBoardCommand(*current, changed, added,
            placed == 1 ? "Add Reference Image" : "Add Reference Images")));
        _selected = static_cast<int>(changed.images.size()) - 1;
        if (_view)
            _view->update();
    }
    return failed;
}

void ReferenceBoardView::change(const refs::Board& changed, const QString& name, int mergeId)
{
    fla::Document* current = document();
    if (!current || changed == refs::of(*current))
        return;
    _push(fla::CommandPtr(new refs::SetBoardCommand(*current, changed, {}, name.toStdString(), mergeId)));
    if (_selected >= static_cast<int>(changed.images.size()))
        _selected = -1;
    if (_view)
        _view->update();
}

void ReferenceBoardView::select(int index)
{
    if (_selected == index)
        return;
    _selected = index;
    if (_view)
        _view->update();
}

void ReferenceBoardView::setPreview(const std::optional<refs::Board>& preview)
{
    _preview = preview;
    if (_view)
        _view->update();
}

void ReferenceBoardView::fillMenu(QMenu* menu, int index)
{
    const refs::Board current = board();
    if (index < 0 || index >= static_cast<int>(current.images.size()))
        return;
    const refs::Image picture = current.images[static_cast<size_t>(index)];
    const auto edit = [this, index](const QString& name, const std::function<void(refs::Board&)>& how) {
        refs::Board changed = board();
        if (index >= static_cast<int>(changed.images.size()))
            return;
        how(changed);
        change(changed, name);
    };

    QMenu* opacity = menu->addMenu("&Opacity");
    QActionGroup* levels = new QActionGroup(opacity);
    for (const int percent : {100, 75, 50, 25})
    {
        QAction* level = opacity->addAction(QString("%1%").arg(percent));
        level->setCheckable(true);
        level->setChecked(std::lround(picture.opacity * 100.0) == percent);
        levels->addAction(level);
        QObject::connect(level, &QAction::triggered, menu, [edit, index, percent]() {
            edit("Reference Image Opacity", [index, percent](refs::Board& changed) {
                changed.images[static_cast<size_t>(index)].opacity = percent / 100.0;
            });
        });
    }
    QAction* lock = menu->addAction("&Lock");
    lock->setCheckable(true);
    lock->setChecked(picture.locked);
    QObject::connect(lock, &QAction::triggered, menu, [edit, index](bool on) {
        edit(on ? "Lock Reference Image" : "Unlock Reference Image",
            [index, on](refs::Board& changed) { changed.images[static_cast<size_t>(index)].locked = on; });
    });
    menu->addSeparator();
    QAction* front = menu->addAction("Bring to &Front");
    front->setEnabled(index + 1 < static_cast<int>(current.images.size()));
    QObject::connect(front, &QAction::triggered, menu, [this, edit, index]() {
        edit("Bring Reference Image to Front", [index](refs::Board& changed) {
            const refs::Image moved = changed.images[static_cast<size_t>(index)];
            changed.images.erase(changed.images.begin() + index);
            changed.images.push_back(moved);
        });
        _selected = static_cast<int>(board().images.size()) - 1;
    });
    QAction* back = menu->addAction("Send to &Back");
    back->setEnabled(index > 0);
    QObject::connect(back, &QAction::triggered, menu, [this, edit, index]() {
        edit("Send Reference Image to Back", [index](refs::Board& changed) {
            const refs::Image moved = changed.images[static_cast<size_t>(index)];
            changed.images.erase(changed.images.begin() + index);
            changed.images.insert(changed.images.begin(), moved);
        });
        _selected = 0;
    });
    QAction* reset = menu->addAction("&Reset Size and Turn");
    reset->setEnabled(!picture.locked && (picture.scale != 1.0 || picture.rotation != 0.0));
    QObject::connect(reset, &QAction::triggered, menu, [edit, index]() {
        edit("Reset Reference Image", [index](refs::Board& changed) {
            changed.images[static_cast<size_t>(index)].scale = 1.0;
            changed.images[static_cast<size_t>(index)].rotation = 0.0;
        });
    });
    menu->addSeparator();
    QAction* remove = menu->addAction("Re&move Reference Image");
    QObject::connect(remove, &QAction::triggered, menu, [this, edit, index]() {
        edit("Remove Reference Image",
            [index](refs::Board& changed) { changed.images.erase(changed.images.begin() + index); });
        _selected = -1;
    });
}

bool ReferenceBoardView::isImageFile(const QString& path)
{
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    return !suffix.isEmpty() && QImageReader::supportedImageFormats().contains(suffix);
}

bool ReferenceTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    if (event->button() != Qt::LeftButton)
        return false;
    const QPointF stage = view.widgetToStage(event->position());
    const refs::Board board = _board->board();
    _drag = Drag::None;
    const int selected = _board->selected();
    if (!board.hidden && selected >= 0 && selected < static_cast<int>(board.images.size()) &&
        !board.images[static_cast<size_t>(selected)].locked)
    {
        // The selected picture's handles first: they reach outside it.
        const QTransform toStage = _board->imageToStage(board.images[static_cast<size_t>(selected)]);
        const ScreenHandles screen = handlesOf(
            [&view, &toStage](const QPointF& point) { return view.stageToWidget(toStage.map(point)); },
            _board->imageSize(selected));
        if (distance(screen.turn, event->position()) <= kHandleReach)
            _drag = Drag::Turn;
        for (const QPointF& corner : screen.corners)
            if (_drag == Drag::None && distance(corner, event->position()) <= kHandleReach)
                _drag = Drag::Scale;
    }
    if (_drag == Drag::None)
    {
        const int hit = _board->imageAt(stage);
        _board->select(hit);
        // Empty space pans, as with the Hand tool.
        if (hit < 0)
            return false;
        if (!board.images[static_cast<size_t>(hit)].locked)
            _drag = Drag::Move;
    }
    _start = board;
    _live = board;
    _pressStage = stage;
    return true;
}

bool ReferenceTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    const int index = _board->selected();
    if (_drag == Drag::None || index < 0 || index >= static_cast<int>(_start.images.size()))
        return _drag != Drag::None;
    const QPointF stage = view.widgetToStage(event->position());
    const refs::Image& from = _start.images[static_cast<size_t>(index)];
    refs::Image& to = _live.images[static_cast<size_t>(index)];
    const QPointF center(from.x, from.y);
    switch (_drag)
    {
    case Drag::Move:
        to.x = from.x + stage.x() - _pressStage.x();
        to.y = from.y + stage.y() - _pressStage.y();
        break;
    case Drag::Scale:
    {
        const double was = distance(_pressStage, center);
        if (was > 0.0)
            to.scale = std::clamp(from.scale * distance(stage, center) / was, 0.01, 100.0);
        break;
    }
    case Drag::Turn:
    {
        const double before = std::atan2(_pressStage.y() - center.y(), _pressStage.x() - center.x());
        const double now = std::atan2(stage.y() - center.y(), stage.x() - center.x());
        double degrees = from.rotation + (now - before) * 180.0 / 3.14159265358979323846;
        // Shift turns in steps of 15 degrees.
        if (event->modifiers() & Qt::ShiftModifier)
            degrees = std::round(degrees / 15.0) * 15.0;
        degrees = std::fmod(degrees, 360.0);
        if (degrees > 180.0)
            degrees -= 360.0;
        if (degrees <= -180.0)
            degrees += 360.0;
        to.rotation = degrees;
        break;
    }
    case Drag::None:
        break;
    }
    _board->setPreview(_live);
    return true;
}

bool ReferenceTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)view;
    (void)event;
    (void)documentPos;
    if (_drag == Drag::None)
        return false;
    const Drag drag = _drag;
    _drag = Drag::None;
    _board->setPreview(std::nullopt);
    if (_live != _start)
        _board->change(_live, drag == Drag::Move ? "Move Reference Image"
                : drag == Drag::Scale            ? "Scale Reference Image"
                                                 : "Rotate Reference Image");
    return true;
}

bool ReferenceTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    (void)view;
    if (event->key() == Qt::Key_Escape)
    {
        if (_done)
            _done();
        return true;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        refs::Board changed = _board->board();
        const int index = _board->selected();
        if (index < 0 || index >= static_cast<int>(changed.images.size()) ||
            changed.images[static_cast<size_t>(index)].locked)
            return true;
        changed.images.erase(changed.images.begin() + index);
        _board->select(-1);
        _board->change(changed, "Remove Reference Image");
        return true;
    }
    return false;
}

void ReferenceTool::deactivate(PhoenixView& view)
{
    (void)view;
    _drag = Drag::None;
    _board->setPreview(std::nullopt);
}
