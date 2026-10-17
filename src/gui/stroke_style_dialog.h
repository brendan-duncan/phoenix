#pragma once

#include "../edit/drawing_style.h"
#include "../edit/paint_brushes.h"

#include <QColor>
#include <QDialog>

#include <memory>

class QComboBox;
class QFormLayout;
class QLabel;
class QVBoxLayout;
class QWidget;

namespace fla {
class Document;
}

/// Edit > Custom Stroke Style: a stroke's kind and weight and the kind's
/// own settings, with a stroke drawn the way they make it. Dash and gap,
/// dot spacing, the ragged line's pattern and waves, the stipple's dots,
/// the hatching's lines; for a paint brush stroke, the brush (the
/// document's or the brush library's) and the brush's options, which
/// belong to the brush and so change every stroke drawn with it unless
/// they are kept as a new brush; for a symbol brush, the graphic symbol
/// and whether it stretches or repeats.
class StrokeStyleDialog : public QDialog
{
    Q_OBJECT

public:
    /// \a document gives the brushes and symbols to choose from; it may be
    /// null.
    StrokeStyleDialog(const fla::StrokeLook& look, double weight, const QColor& color,
        const fla::Document* document = nullptr, QWidget* parent = nullptr);

    /// The look as edited. A paint brush is named by its guid as well as
    /// its index in the document (0 for one of the brush library's).
    const fla::StrokeLook& look() const { return _look; }
    double weight() const { return _weight; }

    /// Whether the paint brush's options were changed, the brush as they
    /// leave it, and whether to keep it as a new brush for these strokes
    /// alone rather than change the brush every stroke using it draws with.
    bool brushEdited() const { return _brushEdited; }
    const fla::PaintBrushDefinition& brush() const { return _brush; }
    bool keepAsNewBrush() const { return _brushEdited && _asNew; }

private:
    /// The settings shown for the kind the look has now.
    void rebuild();
    void addChoice(QFormLayout* form, const QString& label, const char* attribute, const QStringList& values,
        const QString& fallback);
    void addPaintBrushSettings(QVBoxLayout* layout);
    void addSymbolBrushSettings(QVBoxLayout* layout);
    /// Chooses the paint brush with \a key (a guid, or "#" and an index).
    void choosePaintBrush(const QString& key);
    void showPreview();
    QString kindDescription() const;

    fla::StrokeLook _look;
    double _weight;
    QColor _color;
    const fla::Document* _document = nullptr;
    QLabel* _preview = nullptr;
    QComboBox* _kind = nullptr;
    QLabel* _about = nullptr;
    QWidget* _settings = nullptr;

    fla::PaintBrushDefinition _brush;
    std::shared_ptr<const fla::PaintBrushArt> _art;
    bool _brushEdited = false;
    bool _asNew = false;
};
