#pragma once

#include "../edit/find_replace.h"

#include <QColor>
#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

/// Find and Replace: text, a font, a color or a symbol, looked
/// for on every scene and inside every symbol. Find All lists where each
/// is, and clicking one goes there; Replace All replaces them all as one
/// undoable step.
class FindReplaceDialog : public QDialog
{
    Q_OBJECT

public:
    /// \a document gives the document to search (null when none is open).
    FindReplaceDialog(std::function<fla::Document*()> document, QWidget* parent = nullptr);

    /// Fills the font and symbol lists from the document, and forgets the
    /// results, which may point at what is no longer there.
    void refresh();

    /// Starts the color search from \a color.
    void setFindColor(const QColor& color);

    /// Edit > Find Next: goes to the next place what the dialog asks for is
    /// found, from the first when the search has changed. False when there
    /// is none.
    bool findNext();

signals:
    /// Replace All, to be carried out undoably.
    void replaceRequested(const fla::find::Query& query);
    /// A result was clicked.
    void goToRequested(const fla::find::Match& match);

private:
    fla::find::Query query() const;
    void findAll();
    void pickColor(QPushButton* button, QColor& color);
    void showColor(QPushButton* button, const QColor& color);

    std::function<fla::Document*()> _document;
    QComboBox* _kind;
    QStackedWidget* _pages;
    QLineEdit* _findText;
    QLineEdit* _replaceText;
    QCheckBox* _matchCase;
    QCheckBox* _wholeWord;
    QComboBox* _findFont;
    QComboBox* _replaceFont;
    QPushButton* _findColorButton;
    QPushButton* _replaceColorButton;
    QColor _findColor = Qt::black;
    QColor _replaceColor = Qt::white;
    QComboBox* _findSymbol;
    QComboBox* _findSound = nullptr;
    QComboBox* _replaceSound = nullptr;
    QComboBox* _findBitmap = nullptr;
    QComboBox* _replaceBitmap = nullptr;
    QComboBox* _replaceSymbol;
    QTreeWidget* _results;
    QLabel* _status;
    std::vector<fla::find::Match> _matches;
    /// Where Find Next goes on from, and the search it was for.
    size_t _next = 0;
    std::string _nextKey;
};
