#pragma once

#include "../edit/drawing_style.h"
#include "../edit/swatches.h"

#include <QBrush>
#include <QColor>
#include <QIcon>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <utility>
#include <vector>

class PhoenixView;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace fla {
class Command;
class Document;
class Element;
class FillStyle;
class Shape;
}

/// A gradient swatch, or a fill, as a brush to draw a swatch with: the
/// gradient across the box it fills (ObjectBoundingMode), a solid color,
/// or a bitmap fill as a hatch.
QBrush brushOf(const fla::swatches::Gradient& gradient);
QBrush brushOf(const fla::FillStyle& fill);
/// What the style fills new art with, as a brush: the gradient, or the
/// fill color (a bitmap fill shows as its hatch).
QBrush brushOfStyleFill(const fla::DrawingStyle& style);

/// A 20 pixel swatch of \a brush over a checker, filled or, with
/// \a stroke, as a square ring around a hollow middle.
QIcon brushSwatchIcon(const QBrush& brush, bool stroke);

/// The swatch pop-up that opens under a fill or stroke swatch: the
/// document's colors (or the default palette), its gradient swatches
/// (or the default gradients) below them, and the recent colors; a hex
/// and an alpha field showing the swatch under the pointer, No Color, and
/// a button for the full color picker. Picking anything closes it; so do
/// a click outside and Esc. It deletes itself when closed.
class SwatchPopup : public QWidget
{
    Q_OBJECT

public:
    /// What the pop-up offers besides the solid colors.
    struct Offer
    {
        bool gradients = false;
        bool none = false;
        /// Whether the color picked may be see-through; without it the
        /// alpha field is hidden and every color comes back opaque.
        bool alpha = true;
    };

    explicit SwatchPopup(QWidget* parent);
    ~SwatchPopup() override;

    /// Opens a pop-up under \a anchor, showing \a current, with the
    /// swatches of \a document (null for the defaults).
    static SwatchPopup* open(QWidget* anchor, const fla::Document* document, const QColor& current,
        const QString& title, const Offer& offer);

    /// A color from a pop-up under \a anchor, solid colors only, waited
    /// for as a color dialog is: the color picked, or an invalid one when
    /// it closed without a pick. With \a alpha the color may be
    /// see-through.
    static QColor getColor(QWidget* anchor, const fla::Document* document, const QColor& current,
        const QString& title, bool alpha);

    void setDocument(const fla::Document* document);
    void setOffer(const Offer& offer);
    void setCurrent(const QColor& color);
    /// The current fill shown in the chip when it is not a solid color.
    void setCurrentBrush(const QBrush& brush);
    void popupUnder(QWidget* anchor);

    /// Shows the swatch under the pointer on \a elements in \a view (shapes
    /// and rectangle and oval primitives), on their fills or with \a strokes
    /// their strokes, without an undo step. Moving off the swatches or
    /// closing puts them back; a pick puts them back before it is applied,
    /// so it is one undo step from how they were.
    void previewOn(PhoenixView* view, const std::vector<fla::Element*>& elements, bool strokes);

    /// Whether a pop-up is showing a swatch on the document now, which a
    /// save must not write.
    static bool previewing();
    /// Closes every pop-up open, putting back what they show.
    static void closeAll();

    /// The pop-up's swatches, for the grid that draws them.
    struct Item
    {
        enum class Kind
        {
            Solid,
            Gradient,
            Recent
        };
        Kind kind = Kind::Solid;
        QColor color;
        fla::swatches::Gradient gradient;
        QRect rect;
    };

signals:
    void colorPicked(const QColor& color);
    void gradientPicked(const fla::swatches::Gradient& gradient);
    void nonePicked();

protected:
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    class Grid;
    class Chip;

    /// Shows \a item in the chip and fields while the pointer is on it;
    /// null goes back to the current color.
    void preview(const Item* item);
    /// The shapes previewed showing \a item, or as they were for null.
    void previewShapes(const Item* item);
    void pick(const Item& item);
    void pickTyped();
    void openPicker();
    QColor withAlpha(QColor color) const;

    Grid* _grid;
    Chip* _chip;
    QLineEdit* _hex;
    QSpinBox* _alpha;
    QToolButton* _none;
    QToolButton* _picker;
    QString _title;
    QColor _current;
    QBrush _currentBrush;
    bool _offersGradients = false;
    bool _offersAlpha = true;
    QPointer<PhoenixView> _view;
    std::vector<fla::Element*> _previewElements;
    bool _previewStrokes = false;
    /// The previews showing, each undone to put its element back, with
    /// the shape it changed (null for a primitive).
    std::vector<std::pair<std::unique_ptr<fla::Command>, fla::Shape*>> _previews;
};
