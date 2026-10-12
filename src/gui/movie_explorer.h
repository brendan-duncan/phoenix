#pragma once

#include "../edit/find_replace.h"

#include <QWidget>

#include <functional>

class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace fla {
class Document;
class Element;
class Frame;
class Timeline;
}

/// The Movie Explorer: the whole document as a tree, each scene's layers,
/// their keyframes with labels and scripts, and what is on them (symbol
/// instances by symbol and instance name, text by what it says, bitmaps
/// and sounds), then every symbol's own contents. Buttons above choose
/// what is shown; the search narrows it to names and text that match.
/// Clicking an entry goes to it on the stage.
class MovieExplorer : public QWidget
{
    Q_OBJECT

public:
    explicit MovieExplorer(std::function<fla::Document*()> document, QWidget* parent = nullptr);

    /// Rebuilds the tree from the document as it is.
    void refresh();

    /// The text of every text field in the document, one after another.
    QString allText() const;

signals:
    /// An entry was clicked: where it is, as Find and Replace gives it.
    void goToRequested(const fla::find::Match& match);
    /// A symbol's library item, to show in the library.
    void showInLibraryRequested(const QString& name);

private:
    struct Shown
    {
        bool text = true;
        bool symbols = true;
        bool scripts = true;
        bool media = true;
        bool framesAndLayers = true;
    };

    void addTimeline(QTreeWidgetItem* parent, fla::Timeline* timeline, fla::Symbol* symbol, int scene);
    void addElement(QTreeWidgetItem* parent, fla::Element* element, fla::Element* top, const fla::find::Match& where);
    void applySearch();

    std::function<fla::Document*()> _document;
    QLineEdit* _search;
    QTreeWidget* _tree;
    Shown _shown;
    std::vector<fla::find::Match> _matches;
};
