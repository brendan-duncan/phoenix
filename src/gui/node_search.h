#pragma once

#include "../edit/node_graph.h"

#include <QFrame>
#include <QString>

#include <functional>

class QLineEdit;
class QListWidget;
class QLabel;

/// The menu of nodes to add: a search field over the kinds of node. Empty,
/// it lists them by category with the ones added lately first; typed into,
/// it lists those whose names (or categories, or descriptions) the letters
/// run through, best first. Up and Down pick, Enter adds, Esc closes.
class NodeSearch : public QFrame
{
    Q_OBJECT

public:
    /// \a accept limits the kinds listed (a link being dropped lists those
    /// that can take it); null lists them all.
    explicit NodeSearch(QWidget* parent, std::function<bool(const fla::nodegraph::Type&)> accept = {});

    /// Shows the menu with its top left at \a global, kept on the screen.
    void popup(const QPoint& global);

    /// How well \a query's letters run through \a text, higher better, or
    /// -1 when they do not.
    static int score(const QString& query, const QString& text);

signals:
    void chosen(const QString& type);
    /// Closed without choosing.
    void dismissed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void fill();
    void choose();
    void stepBy(int step);

    std::function<bool(const fla::nodegraph::Type&)> _accept;
    QLineEdit* _field = nullptr;
    QListWidget* _list = nullptr;
    QLabel* _description = nullptr;
    bool _done = false;
};
