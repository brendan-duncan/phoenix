#pragma once

#include "../edit/brush_library.h"

#include <QColor>
#include <QObject>
#include <QPainterPath>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringList>

#include <map>
#include <string>
#include <vector>

/// The brush library: Phoenix's own brushes, and those a person has added
/// (from a document of theirs, say), kept between sessions in a file in
/// Phoenix's app data folder and filed under categories they can rename.
/// The Paint Brush tool draws with any of them; a brush is copied into a
/// document the first time one of its strokes uses it.
class BrushLibraryStore : public QObject
{
    Q_OBJECT

public:
    /// The library kept in the app data folder (Brushes/BrushLibrary.xml),
    /// read the first time it is asked for.
    static BrushLibraryStore& shared();

    /// A library kept in the file at \a path; an empty path keeps it only
    /// for as long as it lives.
    explicit BrushLibraryStore(const QString& path, QObject* parent = nullptr);

    /// Phoenix's own brushes first, then the person's, in their order.
    const std::vector<fla::BrushLibraryEntry>& entries() const { return _entries; }

    /// The categories, in the order their first brush comes.
    QStringList categories() const;

    /// The brush with \a guid; null when the library has none.
    const fla::BrushLibraryEntry* find(const std::string& guid) const;

    /// Adds \a definitions, filed under \a category or by their names; those
    /// the library has are left out. How many were added.
    int add(const std::vector<fla::PaintBrushDefinition>& definitions, const QString& category = QString());

    /// Renames a category, or files its brushes with another's of that
    /// name; false for Phoenix's own, or a name in use for nothing.
    bool renameCategory(const QString& from, const QString& to);
    /// Takes a category out with its brushes.
    bool removeCategory(const QString& category);
    bool renameBrush(const std::string& guid, const QString& name);
    bool moveBrush(const std::string& guid, const QString& category);
    bool removeBrush(const std::string& guid);

    /// Where the library is kept; empty when it is not.
    QString path() const { return _path; }
    /// Why the last change could not be saved; empty when it was.
    QString error() const { return _error; }

signals:
    void changed();

private:
    void load();
    void save();

    QString _path;
    QString _error;
    std::vector<fla::BrushLibraryEntry> _entries;
};

namespace fla {
class CommandStack;
class Document;
}

/// The index in \a document of the paint brush with \a guid: the
/// document's own, or the brush library's, copied into the document first
/// as a step pushed onto \a stack. 0 when neither has it.
int paintBrushIndexFor(fla::Document& document, fla::CommandStack& stack, const std::string& guid);

/// The definition of the brush with \a guid, from \a document's brushes
/// or else the brush library; false when neither has it.
bool paintBrushNamed(const fla::Document* document, const std::string& guid, fla::PaintBrushDefinition& definition);

/// A small picture of \a definition: its art laid along a gentle curve
/// \a size big, in \a color, over a transparent ground.
QPixmap paintBrushPreview(const fla::PaintBrushDefinition& definition, const QSize& size, const QColor& color);

/// \a definition's art laid along \a centerline, flattened, at \a weight in
/// \a color, with an optional width \a profile, as areas and their colors.
std::vector<std::pair<QPainterPath, QColor>> layPaintBrushPath(const fla::PaintBrushDefinition& definition,
    const fla::PaintBrushArt& art, const QPainterPath& centerline, double weight, const QColor& color,
    const std::vector<fla::StrokeStyle::WidthMarker>& profile = {});
