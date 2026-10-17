#include "brush_library_store.h"

#include "../edit/command_stack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

BrushLibraryStore& BrushLibraryStore::shared()
{
    static BrushLibraryStore* store = new BrushLibraryStore(
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("Brushes/BrushLibrary.xml"));
    return *store;
}

BrushLibraryStore::BrushLibraryStore(const QString& path, QObject* parent)
    : QObject(parent)
    , _path(path)
{
    load();
}

void BrushLibraryStore::load()
{
    _entries = fla::phoenixBrushes();
    if (_path.isEmpty())
        return;
    QFile file(_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray xml = file.readAll();
    for (fla::BrushLibraryEntry& entry : fla::readBrushLibrary(xml.toStdString()))
    {
        // Phoenix's own come with it; a copy in the file is left out.
        if (!find(entry.definition.guid()))
            _entries.push_back(std::move(entry));
    }
}

void BrushLibraryStore::save()
{
    _error.clear();
    if (!_path.isEmpty())
    {
        QDir().mkpath(QFileInfo(_path).absolutePath());
        QSaveFile file(_path);
        const std::string xml = fla::writeBrushLibrary(_entries);
        if (!file.open(QIODevice::WriteOnly) || file.write(xml.data(), static_cast<qint64>(xml.size())) < 0 ||
            !file.commit())
            _error = QString("The brush library could not be saved to %1.").arg(QDir::toNativeSeparators(_path));
    }
    emit changed();
}

QStringList BrushLibraryStore::categories() const
{
    QStringList names;
    for (const fla::BrushLibraryEntry& entry : _entries)
    {
        const QString name = QString::fromStdString(entry.category);
        if (!names.contains(name))
            names << name;
    }
    return names;
}

const fla::BrushLibraryEntry* BrushLibraryStore::find(const std::string& guid) const
{
    return fla::findInBrushLibrary(_entries, guid);
}

int BrushLibraryStore::add(const std::vector<fla::PaintBrushDefinition>& definitions, const QString& category)
{
    const int added = fla::addToBrushLibrary(_entries, definitions, category.toStdString());
    if (added > 0)
        save();
    return added;
}

bool BrushLibraryStore::renameCategory(const QString& from, const QString& to)
{
    const std::string name = to.trimmed().toStdString();
    if (name.empty() || from == to)
        return false;
    bool any = false;
    for (fla::BrushLibraryEntry& entry : _entries)
    {
        if (entry.category != from.toStdString())
            continue;
        if (entry.builtIn)
            return false;
        entry.category = name;
        any = true;
    }
    if (any)
        save();
    return any;
}

bool BrushLibraryStore::removeCategory(const QString& category)
{
    const std::string name = category.toStdString();
    const auto mine = [&](const fla::BrushLibraryEntry& entry) { return entry.category == name && !entry.builtIn; };
    const size_t before = _entries.size();
    _entries.erase(std::remove_if(_entries.begin(), _entries.end(), mine), _entries.end());
    if (_entries.size() == before)
        return false;
    save();
    return true;
}

bool BrushLibraryStore::renameBrush(const std::string& guid, const QString& name)
{
    for (fla::BrushLibraryEntry& entry : _entries)
    {
        if (entry.builtIn || entry.definition.guid() != guid)
            continue;
        const std::string text = name.trimmed().toStdString();
        if (text.empty() || text == entry.definition.name)
            return false;
        const int index = entry.definition.index;
        entry.definition = fla::withParam(entry.definition, "name", text);
        entry.definition.index = index;
        save();
        return true;
    }
    return false;
}

bool BrushLibraryStore::moveBrush(const std::string& guid, const QString& category)
{
    const std::string name = category.trimmed().toStdString();
    for (fla::BrushLibraryEntry& entry : _entries)
    {
        if (entry.builtIn || entry.definition.guid() != guid || name.empty() || entry.category == name)
            continue;
        entry.category = name;
        save();
        return true;
    }
    return false;
}

bool BrushLibraryStore::removeBrush(const std::string& guid)
{
    const auto it = std::find_if(_entries.begin(), _entries.end(), [&](const fla::BrushLibraryEntry& entry) {
        return !entry.builtIn && entry.definition.guid() == guid;
    });
    if (it == _entries.end())
        return false;
    _entries.erase(it);
    save();
    return true;
}

int paintBrushIndexFor(fla::Document& document, fla::CommandStack& stack, const std::string& guid)
{
    if (guid.empty())
        return 0;
    if (const auto own = fla::paintBrushesOf(document))
        for (const fla::PaintBrushDefinition& definition : own->definitions)
            if (definition.guid() == guid)
                return definition.index;
    const fla::BrushLibraryEntry* entry = BrushLibraryStore::shared().find(guid);
    if (!entry)
        return 0;
    std::map<int, int> indexes;
    if (fla::CommandPtr command = fla::adoptPaintBrushes(document, {entry->definition}, indexes, "Add Brush"))
        stack.push(std::move(command));
    const auto found = indexes.find(entry->definition.index);
    return found != indexes.end() ? found->second : 0;
}

bool paintBrushNamed(const fla::Document* document, const std::string& guid, fla::PaintBrushDefinition& definition)
{
    if (guid.empty())
        return false;
    if (const auto own = document ? fla::paintBrushesOf(*document) : nullptr)
        for (const fla::PaintBrushDefinition& candidate : own->definitions)
            if (candidate.guid() == guid)
            {
                definition = candidate;
                return true;
            }
    if (const fla::BrushLibraryEntry* entry = BrushLibraryStore::shared().find(guid))
    {
        definition = entry->definition;
        return true;
    }
    return false;
}

std::vector<std::pair<QPainterPath, QColor>> layPaintBrushPath(const fla::PaintBrushDefinition& definition,
    const fla::PaintBrushArt& art, const QPainterPath& centerline, double weight, const QColor& color,
    const std::vector<fla::StrokeStyle::WidthMarker>& profile)
{
    std::vector<std::pair<QPainterPath, QColor>> laid;
    const fla::PaintBrushArt::Color rgba{static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
        static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(color.alpha())};
    // Each run of the stroke on its own, closed where it comes back round.
    for (const QPolygonF& polygon : centerline.toSubpathPolygons())
    {
        std::vector<fla::PaintBrushArt::Point> points;
        points.reserve(polygon.size());
        for (const QPointF& point : polygon)
            points.push_back({point.x(), point.y()});
        const bool closed = polygon.size() > 2 && polygon.isClosed();
        for (const fla::PaintBrushArt::Piece& piece :
            fla::layPaintBrush(definition, art, points, closed, weight, rgba, profile))
        {
            QPainterPath area;
            area.setFillRule(piece.evenOdd ? Qt::OddEvenFill : Qt::WindingFill);
            for (const auto& ring : piece.rings)
            {
                QPolygonF outline;
                outline.reserve(static_cast<int>(ring.size()));
                for (const fla::PaintBrushArt::Point& point : ring)
                    outline << QPointF(point.x, point.y);
                area.addPolygon(outline);
                area.closeSubpath();
            }
            laid.emplace_back(area, QColor(piece.color[0], piece.color[1], piece.color[2], piece.color[3]));
        }
    }
    return laid;
}

QPixmap paintBrushPreview(const fla::PaintBrushDefinition& definition, const QSize& size, const QColor& color)
{
    // Drawn once for each brush, size and color: menus and lists of a few
    // hundred brushes are filled again often.
    static QHash<QString, QPixmap> drawn;
    const QString key = QString("%1 %2x%3 %4")
                            .arg(qHash(QByteArray::fromStdString(definition.json)))
                            .arg(size.width())
                            .arg(size.height())
                            .arg(color.rgba());
    const auto found = drawn.constFind(key);
    if (found != drawn.constEnd())
        return found.value();
    if (drawn.size() > 4000)
        drawn.clear();
    QPixmap& pixmap = drawn[key];
    pixmap = QPixmap(size);
    pixmap.fill(Qt::transparent);
    const fla::PaintBrushArt art = fla::paintBrushArt(definition);
    if (art.pieces.empty() || size.isEmpty())
        return pixmap;
    // A gentle curve across the box, the art as tall as a third of it.
    const double w = size.width();
    const double h = size.height();
    const double weight = h * (definition.pattern ? 0.3 : 0.4);
    QPainterPath line;
    line.moveTo(weight * 0.6, h * 0.62);
    line.cubicTo(w * 0.35, h * 0.2, w * 0.65, h * 0.85, w - weight * 0.6, h * 0.4);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    for (const auto& [area, shade] : layPaintBrushPath(definition, art, line, weight, color))
        painter.fillPath(area, shade);
    painter.end();
    return pixmap;
}
