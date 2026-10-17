#pragma once

#include "../edit/brush_library.h"
#include "../edit/command.h"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace fla {
class Document;
}

/// Window > Document Brushes: the brushes the document's strokes are drawn
/// with. Its paint brushes (art and pattern brushes kept in the document,
/// used or not) and the graphic symbols drawn along strokes, each with a
/// picture, its kind and how many strokes use it; to draw with, select the
/// strokes of, rename, keep in the brush library, take off the strokes or,
/// for brushes no stroke uses, remove.
class DocumentBrushesDialog : public QDialog
{
    Q_OBJECT

public:
    /// What the dialog asks of the window it serves.
    struct Hooks
    {
        /// New strokes are drawn with the brush (the Paint Brush tool for a
        /// paint brush).
        std::function<void(const fla::DocumentBrush&)> drawWith;
        /// Selects what on the current frame has strokes drawn with the
        /// brush; how many were selected.
        std::function<int(const fla::DocumentBrush&)> selectStrokes;
        /// The strokes drawn with the brush become plain lines.
        std::function<void(const fla::DocumentBrush&)> takeOff;
        /// Pushes an edit of the document onto its history.
        std::function<void(fla::CommandPtr)> push;
    };

    DocumentBrushesDialog(fla::Document& document, Hooks hooks, QWidget* parent = nullptr);

    /// Lists the brushes again, as the document has them now.
    void refill();

private:
    /// The brush of the chosen row; null when none is chosen.
    const fla::DocumentBrush* chosen() const;
    void updateButtons();
    void rename();
    void removeUnused();
    void addToLibrary();

    fla::Document& _document;
    Hooks _hooks;
    std::vector<fla::DocumentBrush> _brushes;
    QTreeWidget* _list = nullptr;
    QLabel* _status = nullptr;
    QPushButton* _draw = nullptr;
    QPushButton* _select = nullptr;
    QPushButton* _rename = nullptr;
    QPushButton* _keep = nullptr;
    QPushButton* _takeOff = nullptr;
    QPushButton* _removeUnused = nullptr;
};
