#pragma once

#include "../edit/swatches.h"

#include <QWidget>

class QCheckBox;

namespace fla {
class FLADocument;
}

/// Animate's Swatches panel: the document's saved colors, or Animate's
/// default palette when it has none. A click picks the fill color for new
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

private:
    class Grid;
    void rebuild();

    const fla::FLADocument* _document = nullptr;
    Grid* _grid = nullptr;
    QCheckBox* _stroke = nullptr;
    std::vector<fla::swatches::Color> _shown;
    bool _ownColors = false;
};
