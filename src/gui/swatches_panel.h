#pragma once

#include "../edit/swatches.h"

#include <QWidget>

class QCheckBox;

namespace fla {
class FLADocument;
}

/// The Swatches panel: the document's saved colors, or the default
/// palette when it has none. A click picks the fill color for new
/// art, or with Stroke ticked the stroke color; Add keeps the current fill
/// color among the document's swatches, and a right-click deletes one.
class SwatchesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SwatchesPanel(QWidget* parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    /// Redraws when the document's swatches changed.
    void refreshIfStale();

signals:
    void colorPicked(const QColor& color, bool stroke);
    void addRequested();
    void removeRequested(int index);
    /// The document's swatches to become \a colors, as one undo step
    /// named \a name (duplicating, sorting, importing...).
    void colorsEditRequested(const std::vector<fla::swatches::Color>& colors, const QString& name);
    /// Tag a swatch: it becomes a named color of the palette in use, with
    /// everything already in its color linked to it.
    void tagRequested(int index);
    /// Open the Palettes panel, where tagged colors are changed.
    void palettesRequested();

private:
    class Grid;
    void rebuild();
    /// The panel menu: sorting, clearing, defaults, and color tables in and
    /// out.
    void showMenu(const QPoint& where);
    void importColors(bool replace);
    void exportColors();

    const fla::FLADocument* _document = nullptr;
    Grid* _grid = nullptr;
    QCheckBox* _stroke = nullptr;
    std::vector<fla::swatches::Color> _shown;
    /// The names of the tagged swatches, by place; empty where untagged.
    std::vector<std::string> _tags;
    bool _ownColors = false;
};
