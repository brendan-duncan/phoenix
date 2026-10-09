#include "text_tool.h"

#include "phoenix_view.h"
#include "text_layout.h"

#include "../data/frame.h"
#include "../data/static_text.h"
#include "../edit/command_stack.h"
#include "../edit/drawing_style.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{

/// How far a press has to travel, on screen, before it is a drag for a
/// fixed-width box rather than a click for an auto-expanding one.
const double kDragThreshold = 4.0;

/// Half the size, on screen, of the handle on the box's corner.
const double kHandle = 3.0;

/// Which run a joined-text offset falls in, and how far into it. An offset
/// on the seam between two runs belongs to the earlier one, so typing there
/// carries on in its style.
struct RunPosition
{
    int run;
    int offset;
};

RunPosition runPosition(const fla::StaticText& text, int index)
{
    int at = 0;
    for (size_t i = 0; i < text.runs.size(); ++i)
    {
        const int length = static_cast<int>(QString::fromStdString(text.runs[i].text).length());
        if (index <= at + length)
            return {static_cast<int>(i), index - at};
        at += length;
    }
    const int last = static_cast<int>(text.runs.size()) - 1;
    const int end = last < 0 ? 0 : static_cast<int>(QString::fromStdString(text.runs[last].text).length());
    return {last, end};
}

void dropEmptyRuns(fla::StaticText& text)
{
    for (size_t i = text.runs.size(); i > 1; --i)
    {
        if (text.runs[i - 1].text.empty())
            text.runs.erase(text.runs.begin() + static_cast<long>(i - 1));
    }
}

bool sameContents(const fla::StaticText& a, const fla::StaticText& b)
{
    if (a.left != b.left || a.top != b.top || a.width != b.width || a.height != b.height ||
        a.autoExpand != b.autoExpand || a.runs.size() != b.runs.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.runs.size(); ++i)
    {
        const fla::TextRun& x = a.runs[i];
        const fla::TextRun& y = b.runs[i];
        if (x.text != y.text || x.face != y.face || x.size != y.size ||
            std::memcmp(x.fillColor, y.fillColor, sizeof(x.fillColor)) != 0)
        {
            return false;
        }
    }
    return true;
}

} // namespace

TextTool::~TextTool()
{
    if (_owned)
        delete _text;
    delete _before;
}

bool TextTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    // Inside the open box: the caret moves there.
    if (_text)
    {
        bool invertible = false;
        const QTransform toLocal = _toDocument.inverted(&invertible);
        if (invertible)
        {
            const QPointF local = toLocal.map(documentPos);
            if (TextLayout::localBounds(*_text).contains(local))
            {
                _caret = TextLayout::indexAt(*_text, local);
                view.update();
                return true;
            }
        }
        commit(view);
    }

    // On another text: edit it in place.
    const HitResult hit = view.hitTest(documentPos, 4.0 / std::max(view.zoom(), 1.0e-6));
    if (hit && hit.element->elementType() == fla::Element::Type::StaticText && hit.frame)
    {
        beginExisting(view, static_cast<fla::StaticText*>(hit.element), hit.frame,
            hit.elementToDocument, documentPos);
        return true;
    }

    // Empty stage: a box opens on release, once it is known whether this is
    // a click or a drag.
    _pressed = true;
    _pressPos = documentPos;
    _dragPos = documentPos;
    return true;
}

bool TextTool::mouseMove(PhoenixView& view, QMouseEvent*, const QPointF& documentPos)
{
    if (!_pressed)
        return false;
    _dragPos = documentPos;
    view.update();
    return true;
}

bool TextTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_pressed || event->button() != Qt::LeftButton)
        return false;
    _pressed = false;

    const double zoom = std::max(view.zoom(), 1.0e-6);
    const QPointF travel = documentPos - _pressPos;
    const bool dragged = std::hypot(travel.x(), travel.y()) * zoom >= kDragThreshold;
    const QRectF box = QRectF(_pressPos, documentPos).normalized();
    beginNew(view, dragged ? box.topLeft() : _pressPos, dragged ? box.width() : 0.0);
    return true;
}

void TextTool::beginNew(PhoenixView& view, const QPointF& documentPos, double width)
{
    fla::Frame* frame = view.activeFrame();
    if (!frame)
        return;

    fla::StaticText* text = new fla::StaticText(nullptr);
    text->left = documentPos.x();
    text->top = documentPos.y();
    text->autoExpand = width <= 0.0;
    text->width = width;

    fla::TextRun run;
    run.face = _style.textFace;
    run.size = _style.textSize;
    std::copy(_style.fillColor, _style.fillColor + 4, run.fillColor);
    text->runs.push_back(run);

    _text = text;
    _owned = true;
    _frame = frame;
    _toDocument = QTransform();
    _caret = 0;

    // The box is the only thing selected while it is open.
    _selection.clear();
    refresh(view);
}

void TextTool::beginExisting(PhoenixView& view, fla::StaticText* text, fla::Frame* frame,
    const QTransform& toDocument, const QPointF& documentPos)
{
    if (text->runs.empty())
    {
        fla::TextRun run;
        run.face = _style.textFace;
        run.size = _style.textSize;
        std::copy(_style.fillColor, _style.fillColor + 4, run.fillColor);
        text->runs.push_back(run);
    }

    _text = text;
    _owned = false;
    _frame = frame;
    _toDocument = toDocument;
    delete _before;
    _before = static_cast<fla::StaticText*>(fla::cloneElement(*text, nullptr));

    bool invertible = false;
    const QTransform toLocal = toDocument.inverted(&invertible);
    _caret = invertible ? TextLayout::indexAt(*text, toLocal.map(documentPos)) : 0;

    _selection.clear();
    refresh(view);
}

void TextTool::commit(PhoenixView& view)
{
    if (!_text)
        return;

    fla::StaticText* text = _text;
    _text = nullptr;

    if (_owned)
    {
        _owned = false;
        if (TextLayout::joinedText(*text).trimmed().isEmpty() || !_frame)
        {
            delete text;
        }
        else
        {
            // Measured before it goes in, so the file records the box as it is.
            _text = text;
            measure();
            _text = nullptr;
            text->parent = _frame;
            _commandStack.push(fla::CommandPtr(
                new fla::AddElementCommand(_frame, text, "Text", &_selection)));
            _commandStack.breakMergeChain();
            _selection.select(text);
        }
    }
    else if (_before)
    {
        _text = text;
        measure();
        _text = nullptr;
        if (TextLayout::joinedText(*text).isEmpty())
        {
            // Everything typed away: the text goes, as Animate removes an
            // emptied box.
            fla::setTextContents(*text, *_before);
            _commandStack.push(fla::CommandPtr(
                new fla::RemoveElementCommand(_frame, text, "Delete Text")));
        }
        else if (!sameContents(*text, *_before))
        {
            _commandStack.push(fla::CommandPtr(
                new fla::SetTextContentsCommand(text, *_before, *text, "Edit Text")));
            _commandStack.breakMergeChain();
            _selection.select(text);
        }
        else
        {
            _selection.select(text);
        }
        delete _before;
        _before = nullptr;
    }

    _frame = nullptr;
    _caret = 0;
    refresh(view);
}

void TextTool::insertText(const QString& inserted)
{
    if (!_text || inserted.isEmpty())
        return;
    const RunPosition at = runPosition(*_text, _caret);
    if (at.run < 0)
        return;
    QString runText = QString::fromStdString(_text->runs[at.run].text);
    runText.insert(at.offset, inserted);
    _text->runs[at.run].text = runText.toStdString();
    _caret += static_cast<int>(inserted.length());
}

void TextTool::deleteBackward()
{
    if (!_text || _caret <= 0)
        return;
    const RunPosition at = runPosition(*_text, _caret);
    if (at.run < 0 || at.offset <= 0)
        return;
    QString runText = QString::fromStdString(_text->runs[at.run].text);
    runText.remove(at.offset - 1, 1);
    _text->runs[at.run].text = runText.toStdString();
    dropEmptyRuns(*_text);
    --_caret;
}

void TextTool::deleteForward()
{
    if (!_text || _caret >= static_cast<int>(TextLayout::joinedText(*_text).length()))
        return;
    const RunPosition at = runPosition(*_text, _caret + 1);
    if (at.run < 0 || at.offset <= 0)
        return;
    QString runText = QString::fromStdString(_text->runs[at.run].text);
    runText.remove(at.offset - 1, 1);
    _text->runs[at.run].text = runText.toStdString();
    dropEmptyRuns(*_text);
}

void TextTool::measure()
{
    if (!_text)
        return;
    const QRectF bounds = TextLayout::localBounds(*_text);
    _text->width = bounds.width();
    _text->height = bounds.height();
    _text->localBounds = fla::Rect(fla::Point(bounds.left(), bounds.top()),
        fla::Point(bounds.right(), bounds.bottom()));
    _text->bounds = _text->localBounds;
}

void TextTool::refresh(PhoenixView& view)
{
    view.invalidateBounds();
    view.clearCaches();
    view.update();
}

bool TextTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_text)
        return false;

    const int total = static_cast<int>(TextLayout::joinedText(*_text).length());
    switch (event->key())
    {
    case Qt::Key_Escape:
        commit(view);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insertText(QString(QChar('\r')));
        break;
    case Qt::Key_Backspace:
        deleteBackward();
        break;
    case Qt::Key_Delete:
        deleteForward();
        break;
    case Qt::Key_Left:
        _caret = std::max(0, _caret - 1);
        break;
    case Qt::Key_Right:
        _caret = std::min(total, _caret + 1);
        break;
    case Qt::Key_Home:
        _caret = 0;
        break;
    case Qt::Key_End:
        _caret = total;
        break;
    case Qt::Key_Tab:
        return true;
    default:
    {
        const Qt::KeyboardModifiers modifiers = event->modifiers();
        const bool shortcut = (modifiers & Qt::ControlModifier) && !(modifiers & Qt::AltModifier);
        const QString typed = event->text();
        if (shortcut || typed.isEmpty() || !typed[0].isPrint())
            return false;
        insertText(typed);
        break;
    }
    }

    refresh(view);
    return true;
}

void TextTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    // The box being dragged out.
    if (_pressed)
    {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.drawRect(QRectF(_pressPos, _dragPos).normalized());
    }

    if (!_text)
        return;

    painter.save();
    painter.setTransform(_toDocument, true);

    // A new box is not in the frame yet, so the stage does not draw it.
    if (_owned)
        TextLayout::draw(painter, *_text);

    // Animate's edit box: a thin outline with a handle on the top right, a
    // circle for a box that grows and a square for one with a fixed width.
    const QRectF bounds = TextLayout::localBounds(*_text);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
    painter.drawRect(bounds);

    const double half = kHandle * scale;
    painter.setBrush(QColor(255, 255, 255));
    const QPointF corner(bounds.right(), bounds.top());
    if (_text->autoExpand)
        painter.drawEllipse(corner, half, half);
    else
        painter.drawRect(QRectF(corner.x() - half, corner.y() - half, half * 2.0, half * 2.0));

    const QRectF caret = TextLayout::caretRect(*_text, _caret);
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
    painter.drawLine(QPointF(caret.left(), caret.top()), QPointF(caret.left(), caret.bottom()));

    painter.restore();
}

void TextTool::deactivate(PhoenixView& view)
{
    _pressed = false;
    commit(view);
}
