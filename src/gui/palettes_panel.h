#pragma once

#include <QColor>
#include <QWidget>

#include <functional>
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
};

/// The Palettes panel: Harmony's linked palettes. Colors with names, in
/// palettes that can be swapped (day, night): pick one in the list and
/// Fill or Stroke links the selection to it; Link Matching links every
/// use of its color in the document; Edit changes it everywhere it is
/// linked; choosing another palette recolors the whole document from it.
class PalettesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PalettesPanel(PalettesLinks links, QWidget* parent = nullptr);

    void refresh();

private:
    /// Saves \a palettes into the document and recolors from it, as one
    /// undo step named \a name.
    void store(const struct PalettesState& state, const QString& name);
    void newPalette();
    void renamePalette();
    void removePalette();
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
    bool _refreshing = false;
};
