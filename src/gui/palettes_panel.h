#pragma once

#include <QColor>
#include <QWidget>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QComboBox;
class QListWidget;

namespace fla {
class CommandStack;
class Document;
class Element;
}

struct PalettesLinks
{
    std::function<fla::CommandStack*()> commands;
    std::function<fla::Document*()> document;
    std::function<std::vector<fla::Element*>()> selection;
    /// The fill color new art is drawn with, offered for a new color.
    std::function<QColor()> fillColor;
    std::function<void()> changed;
    /// The document's folder, which palette files are found from; empty
    /// when it has not been saved.
    std::function<QString()> folder;
    /// Tells the user something in passing (the status bar).
    std::function<void(const QString&)> message;
};

/// The Palettes panel: linked palettes. Colors with names, in
/// palettes that can be swapped (day, night): pick one in the list and
/// Fill or Stroke links the selection to it; Link Matching links every
/// use of its color in the document; Edit changes it everywhere it is
/// linked; choosing another palette recolors the whole document from it.
/// A palette can be linked to a palette file that other documents link to
/// as well: what changed in the file comes in, and what changes here goes
/// out to it.
class PalettesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PalettesPanel(PalettesLinks links, QWidget* parent = nullptr);

    void refresh();

    /// Brings in linked palette files changed since this document last read
    /// them, and writes out linked palettes changed here.
    void syncFiles();

private:
    /// Saves \a palettes into the document and recolors from it, as one
    /// undo step named \a name.
    void store(const struct PalettesState& state, const QString& name);
    void newPalette();
    void renamePalette();
    void removePalette();
    void savePaletteFile();
    void linkPaletteFile();
    void unlinkPaletteFile();
    /// A palette's file as a full path, and a full path as a palette keeps
    /// it (relative to the document's folder when it has one).
    QString resolved(const std::string& file) const;
    std::string kept(const QString& path) const;
    void addColor();
    void editColor();
    void removeColor();
    void paint(bool fills, bool strokes);
    void linkMatching();
    void unlinkSelection();
    int currentColor() const;

    PalettesLinks _links;
    QComboBox* _palettes;
    QListWidget* _colors;
    class QLabel* _file;
    bool _refreshing = false;
    bool _syncing = false;
    bool _syncQueued = false;
    /// Each linked file as a document last read or wrote it, so a change
    /// on disk is told from a change here.
    std::map<std::pair<const void*, QString>, std::string> _known;
};
