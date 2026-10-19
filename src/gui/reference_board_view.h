#pragma once

#include "tool.h"

#include "../edit/reference_board.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QTransform>

#include <functional>
#include <optional>

class PhoenixView;
class QMenu;
class QPainter;

namespace fla {
class Document;
}

/// The reference board on the stage (fla::references): draws the pinned
/// pictures over the art, finds the one under a point, picks a color from
/// it, and changes the board undoably. The pictures are only ever drawn in
/// the view, so nothing here reaches an export.
class ReferenceBoardView
{
public:
    /// \a document is the document in front (or null); \a push runs a
    /// command on its undo stack.
    ReferenceBoardView(PhoenixView* view, std::function<fla::Document*()> document,
        std::function<void(fla::CommandPtr)> push);

    fla::references::Board board() const;
    bool hasImages() const { return !board().images.empty(); }

    /// Draws the board in the view's pixels; \a rootToWidget maps the
    /// stage's coordinates there.
    void paint(QPainter& painter, const QTransform& rootToWidget);

    /// The topmost picture shown at \a stagePos, or -1.
    int imageAt(const QPointF& stagePos) const;
    /// The color of the topmost shown picture at \a stagePos, where it is not
    /// see-through.
    std::optional<QColor> colorAt(const QPointF& stagePos) const;

    /// Adds pictures from files with their middles at \a stagePos (or
    /// beside the stage when null), as one undo step. The names of the files
    /// that could not be read are returned.
    QStringList addFiles(const QStringList& paths, const QPointF* stagePos);
    /// Changes the board as one undo step named \a name; with a merge id,
    /// steps under the same name fold into one.
    void change(const fla::references::Board& board, const QString& name, int mergeId = -1);

    /// The picture's own size in pixels, and from its pixels to the stage.
    QSizeF imageSize(int index) const;
    QTransform imageToStage(const fla::references::Image& image) const;

    /// While Arrange Reference Images is the tool, the selected picture
    /// shows its handles.
    int selected() const { return _selected; }
    void select(int index);
    /// A board shown in place of the document's while a drag is under way.
    void setPreview(const std::optional<fla::references::Board>& board);

    /// Fills \a menu with what can be done to picture \a index.
    void fillMenu(QMenu* menu, int index);

    /// Every reference image file Phoenix can read.
    static bool isImageFile(const QString& path);

private:
    const QImage* image(const std::string& path) const;
    fla::Document* document() const { return _document ? _document() : nullptr; }

    PhoenixView* _view;
    std::function<fla::Document*()> _document;
    std::function<void(fla::CommandPtr)> _push;
    /// Decoded pictures, by document and path; the files never change under
    /// a path.
    mutable QHash<QString, QImage> _images;
    mutable const fla::Document* _imagesFor = nullptr;
    std::optional<fla::references::Board> _preview;
    int _selected = -1;
};

/// Arrange Reference Images: drag a picture to move it, a corner to size it,
/// the handle above it to turn it; right-click for its opacity, lock, order
/// and removal; Delete removes it. Esc puts the tool before it back.
class ReferenceTool : public Tool
{
public:
    ReferenceTool(ReferenceBoardView* board, std::function<void()> done)
        : _board(board)
        , _done(std::move(done))
    {
    }

    QString name() const override { return "Arrange Reference Images"; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }
    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool keyPress(PhoenixView& view, QKeyEvent* event) override;
    bool showsSelectionBounds() const override { return false; }
    void deactivate(PhoenixView& view) override;

private:
    enum class Drag
    {
        None,
        Move,
        Scale,
        Turn
    };

    ReferenceBoardView* _board;
    std::function<void()> _done;
    Drag _drag = Drag::None;
    fla::references::Board _start;
    fla::references::Board _live;
    QPointF _pressStage;
};
