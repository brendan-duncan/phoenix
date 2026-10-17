#pragma once

#include "../edit/paint_brushes.h"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class BrushLibraryStore;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace fla {
class Document;
}

/// Window > Brush Library: the brushes any document can draw with, by
/// category, each with a picture. Choosing one draws with it at once
/// (the Paint Brush tool); the document takes a copy of the brush with
/// its first stroke. Phoenix's own brushes come with it; others are added
/// from a document that has them (Import Brushes from Document, or Add
/// Brushes from This Document), and can be renamed, filed under another
/// category or deleted.
class BrushLibraryDialog : public QDialog
{
    Q_OBJECT

public:
    /// What the dialog asks of the window it serves.
    struct Hooks
    {
        /// The document open in the window; null when none is.
        std::function<const fla::Document*()> document;
        /// Draws with the brush: the Paint Brush tool, its stroke drawing
        /// with it.
        std::function<void(const fla::PaintBrushDefinition&)> drawWith;
        /// Copies the brush into the open document now, without drawing.
        std::function<void(const fla::PaintBrushDefinition&)> addToDocument;
        /// The brush definitions of the document in the file at \a path;
        /// \a error says why there are none.
        std::function<std::vector<fla::PaintBrushDefinition>(const QString& path, QString& error)> readDocument;
    };

    BrushLibraryDialog(BrushLibraryStore& store, Hooks hooks, QWidget* parent = nullptr);

    /// Shows the library as it is now.
    void refill();

    /// Imports the brushes of the document at \a path; how many were added.
    int importFrom(const QString& path);

    /// Shows the brushes of \a category.
    void chooseCategory(const QString& category);

private:
    /// The guid of the chosen brush; empty when none is chosen.
    std::string chosenGuid() const;
    QString chosenCategory() const;
    void fillBrushes();
    void updateButtons();

    BrushLibraryStore& _store;
    Hooks _hooks;
    QLineEdit* _search = nullptr;
    QListWidget* _categories = nullptr;
    QListWidget* _brushes = nullptr;
    QLabel* _status = nullptr;
    QPushButton* _draw = nullptr;
    QPushButton* _add = nullptr;
    QPushButton* _rename = nullptr;
    QPushButton* _move = nullptr;
    QPushButton* _delete = nullptr;
    QPushButton* _renameCategory = nullptr;
    QPushButton* _deleteCategory = nullptr;
    QPushButton* _fromThis = nullptr;
};
