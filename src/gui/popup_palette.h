#pragma once

#include <QColor>
#include <QList>
#include <QStringList>
#include <QWidget>

class HsvWheel;
class QAction;
class QToolButton;

/// The pop-up palette: a round panel that opens at the cursor (on the
/// ` key, or a right-click with a drawing tool when Preferences asks for
/// it) with the favorite tools around its edge, the recent colors inside
/// them and a color wheel in the middle for the fill or the stroke. Picking
/// a tool or a recent color closes it, as does a click outside it or Esc.
class PopupPalette : public QWidget
{
    Q_OBJECT

public:
    explicit PopupPalette(QWidget* parent = nullptr);

    /// Every tool, for editing the favorites, and the favorites shown, by
    /// name, in order around the edge.
    void setTools(const QList<QAction*>& tools, const QStringList& favorites);
    QStringList favorites() const { return _favorites; }

    /// The colors drawn with now, and which one the wheel sets.
    void setColors(const QColor& fill, const QColor& stroke);
    void setEditsStroke(bool stroke);
    bool editsStroke() const { return _stroke; }

    /// Opens centered on \a globalPos, kept on its screen.
    void popupAt(const QPoint& globalPos);

    /// How many favorites fit around the edge.
    static int mostFavorites() { return 10; }
    /// The favorites a new Phoenix starts with.
    static QStringList defaultFavorites();

signals:
    /// A color for the fill or \a stroke; \a done is false while the wheel
    /// is being dragged.
    void colorPicked(const QColor& color, bool stroke, bool done);
    /// The favorites were edited.
    void favoritesChanged(const QStringList& favorites);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void arrange();
    void editFavorites();
    QPointF middle() const;
    QRectF recentRect(int index) const;
    QRectF fillChip() const;
    QRectF strokeChip() const;
    QRectF editChip() const;
    int recentAt(const QPointF& point) const;

    QList<QAction*> _tools;
    QStringList _favorites;
    QList<QToolButton*> _buttons;
    HsvWheel* _wheel;
    QColor _fill;
    QColor _strokeColor;
    bool _stroke = false;
    int _hoverRecent = -1;
};
