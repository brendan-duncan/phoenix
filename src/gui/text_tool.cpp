#include "text_tool.h"

#include "phoenix_view.h"
#include "text_layout.h"

#include "../data/frame.h"
#include "../edit/command_stack.h"
#include "../edit/drawing_style.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"

#include <cstdint>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

/// How far a press has to travel, on screen, before it is a drag for a
/// fixed-width box rather than a click for an auto-expanding one.
const double kDragThreshold = 4.0;

/// Half the size, on screen, of the handle on the box's corner.
const double kHandle = 3.0;

/// The narrowest a box can be dragged to, in pixels.
const double kMinimumWidth = 8.0;

/// Which run a joined-text offset falls in, and how far into it. An offset
/// on the seam between two runs belongs to the earlier one, so typing there
/// carries on in its style.
struct RunPosition
{
    int run;
    int offset;
};

int runLength(const fla::TextRun& run)
{
    return static_cast<int>(QString::fromStdString(run.text).length());
}

RunPosition runPosition(const fla::StaticText& text, int index)
{
    int at = 0;
    for (size_t i = 0; i < text.runs.size(); ++i)
    {
        const int length = runLength(text.runs[i]);
        if (index <= at + length)
            return {static_cast<int>(i), index - at};
        at += length;
    }
    const int last = static_cast<int>(text.runs.size()) - 1;
    const int end = last < 0 ? 0 : runLength(text.runs[last]);
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

bool sameStyle(const fla::TextRun& a, const fla::TextRun& b)
{
    return a.face == b.face && a.size == b.size && a.alignment == b.alignment &&
        a.aliasText == b.aliasText && a.autoKern == b.autoKern &&
        a.letterSpacing == b.letterSpacing && a.lineHeight == b.lineHeight &&
        a.bitmapSize == b.bitmapSize &&
        std::memcmp(a.fillColor, b.fillColor, sizeof(a.fillColor)) == 0;
}

bool sameContents(const fla::StaticText& a, const fla::StaticText& b)
{
    if (a.left != b.left || a.top != b.top || a.width != b.width || a.height != b.height ||
        a.autoExpand != b.autoExpand || a.runs.size() != b.runs.size() ||
        a.textType != b.textType || a.instanceName != b.instanceName || a.lineType != b.lineType)
    {
        return false;
    }
    for (size_t i = 0; i < a.runs.size(); ++i)
    {
        if (a.runs[i].text != b.runs[i].text || !sameStyle(a.runs[i], b.runs[i]))
            return false;
    }
    return true;
}

bool isWordCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == QChar('_') || c == QChar('\'');
}

} // namespace

TextTool::~TextTool()
{
    if (_owned)
        delete _text;
    delete _before;
}

QPointF TextTool::handlePosition() const
{
    const QRectF bounds = TextLayout::localBounds(*_text);
    return QPointF(bounds.right(), bounds.top());
}

bool TextTool::onHandle(PhoenixView& view, const QPointF& local) const
{
    const double reach = (kHandle + 2.0) / std::max(view.zoom(), 1.0e-6);
    const QPointF handle = handlePosition();
    return std::hypot(local.x() - handle.x(), local.y() - handle.y()) <= reach;
}

bool TextTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    // Inside the open box: the caret moves there, and a drag from here
    // selects; on the handle, a drag sets the width.
    if (_text)
    {
        bool invertible = false;
        const QTransform toLocal = _toDocument.inverted(&invertible);
        if (invertible)
        {
            const QPointF local = toLocal.map(documentPos);
            if (onHandle(view, local))
            {
                _resizing = true;
                return true;
            }
            if (TextLayout::localBounds(*_text).contains(local))
            {
                _caret = TextLayout::indexAt(*_text, local);
                if (event->modifiers() & Qt::ShiftModifier)
                {
                    if (_anchor < 0)
                        _anchor = _caret;
                }
                else
                {
                    _anchor = _caret;
                }
                _selecting = true;
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
    if (_text && (_resizing || _selecting))
    {
        bool invertible = false;
        const QTransform toLocal = _toDocument.inverted(&invertible);
        if (!invertible)
            return true;
        const QPointF local = toLocal.map(documentPos);

        if (_resizing)
        {
            // The handle follows the pointer, and the box stops growing on
            // its own once it has been given a width.
            _text->autoExpand = false;
            _text->width = std::max(kMinimumWidth, local.x() - _text->left);
            refresh(view);
            return true;
        }

        _caret = TextLayout::indexAt(*_text, local);
        view.update();
        return true;
    }

    if (!_pressed)
        return false;
    _dragPos = documentPos;
    view.update();
    return true;
}

bool TextTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (_resizing)
    {
        _resizing = false;
        measure();
        refresh(view);
        return true;
    }
    if (_selecting)
    {
        _selecting = false;
        if (_anchor == _caret)
            _anchor = -1;
        view.update();
        return true;
    }

    if (!_pressed)
        return false;
    _pressed = false;

    const double zoom = std::max(view.zoom(), 1.0e-6);
    const QPointF travel = documentPos - _pressPos;
    const bool dragged = std::hypot(travel.x(), travel.y()) * zoom >= kDragThreshold;
    const QRectF box = QRectF(_pressPos, documentPos).normalized();
    beginNew(view, dragged ? box.topLeft() : _pressPos, dragged ? box.width() : 0.0);
    return true;
}

bool TextTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_text)
        return false;

    bool invertible = false;
    const QTransform toLocal = _toDocument.inverted(&invertible);
    if (!invertible)
        return false;
    const QPointF local = toLocal.map(documentPos);

    // The square handle of a fixed-width box, double-clicked, lets the box
    // grow with its text again, as in Animate.
    if (onHandle(view, local))
    {
        if (!_text->autoExpand)
        {
            _text->autoExpand = true;
            measure();
            refresh(view);
        }
        _selecting = false;
        return true;
    }

    if (!TextLayout::localBounds(*_text).contains(local))
        return false;

    // The word under the pointer.
    const QString joined = TextLayout::joinedText(*_text);
    int at = std::max(0, std::min(static_cast<int>(joined.length()), TextLayout::indexAt(*_text, local)));
    int from = at;
    int to = at;
    while (from > 0 && isWordCharacter(joined[from - 1]))
        --from;
    while (to < joined.length() && isWordCharacter(joined[to]))
        ++to;
    if (from == to)
        return true;

    _anchor = from;
    _caret = to;
    _selecting = false;
    view.update();
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
    text->textType = static_cast<fla::StaticText::TextType>(_style.textType);

    fla::TextRun run;
    run.face = _style.textFace;
    run.size = _style.textSize;
    std::copy(_style.fillColor, _style.fillColor + 4, run.fillColor);
    text->runs.push_back(run);

    _text = text;
    _owned = true;
    _frame = frame;
    _view = &view;
    _toDocument = QTransform();
    _caret = 0;
    _anchor = -1;

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
    _view = &view;
    _toDocument = toDocument;
    delete _before;
    _before = static_cast<fla::StaticText*>(fla::cloneElement(*text, nullptr));

    bool invertible = false;
    const QTransform toLocal = toDocument.inverted(&invertible);
    _caret = invertible ? TextLayout::indexAt(*text, toLocal.map(documentPos)) : 0;
    _anchor = -1;

    _selection.clear();
    refresh(view);
}

void TextTool::commit(PhoenixView& view)
{
    if (!_text)
        return;

    fla::StaticText* text = _text;
    _text = nullptr;
    _anchor = -1;
    _selecting = false;
    _resizing = false;

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

bool TextTool::selectedRange(int& from, int& to) const
{
    if (!_text || _anchor < 0 || _anchor == _caret)
        return false;
    from = std::min(_anchor, _caret);
    to = std::max(_anchor, _caret);
    return true;
}

void TextTool::removeRange(int from, int to)
{
    if (!_text || to <= from)
        return;

    int at = 0;
    for (fla::TextRun& run : _text->runs)
    {
        QString runText = QString::fromStdString(run.text);
        const int length = static_cast<int>(runText.length());
        const int start = std::max(from, at);
        const int end = std::min(to, at + length);
        if (end > start)
        {
            runText.remove(start - at, end - start);
            run.text = runText.toStdString();
        }
        at += length;
    }
    dropEmptyRuns(*_text);
}

void TextTool::deleteSelection()
{
    int from = 0;
    int to = 0;
    if (!selectedRange(from, to))
        return;
    removeRange(from, to);
    _caret = from;
    _anchor = -1;
}

void TextTool::insertText(const QString& inserted)
{
    if (!_text || inserted.isEmpty())
        return;
    deleteSelection();
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
    if (!_text)
        return;
    if (_anchor >= 0 && _anchor != _caret)
    {
        deleteSelection();
        return;
    }
    if (_caret <= 0)
        return;
    removeRange(_caret - 1, _caret);
    --_caret;
}

void TextTool::deleteForward()
{
    if (!_text)
        return;
    if (_anchor >= 0 && _anchor != _caret)
    {
        deleteSelection();
        return;
    }
    if (_caret >= static_cast<int>(TextLayout::joinedText(*_text).length()))
        return;
    removeRange(_caret, _caret + 1);
}

void TextTool::splitRunAt(int offset)
{
    if (!_text)
        return;
    int at = 0;
    for (size_t i = 0; i < _text->runs.size(); ++i)
    {
        const int length = runLength(_text->runs[i]);
        if (offset > at && offset < at + length)
        {
            fla::TextRun second = _text->runs[i];
            const QString runText = QString::fromStdString(_text->runs[i].text);
            _text->runs[i].text = runText.left(offset - at).toStdString();
            second.text = runText.mid(offset - at).toStdString();
            _text->runs.insert(_text->runs.begin() + static_cast<long>(i + 1), second);
            return;
        }
        at += length;
    }
}

void TextTool::mergeRuns()
{
    if (!_text)
        return;
    for (size_t i = 1; i < _text->runs.size();)
    {
        if (sameStyle(_text->runs[i - 1], _text->runs[i]))
        {
            _text->runs[i - 1].text += _text->runs[i].text;
            _text->runs.erase(_text->runs.begin() + static_cast<long>(i));
        }
        else
        {
            ++i;
        }
    }
}

template <typename Change>
bool TextTool::restyleSelection(Change change)
{
    int from = 0;
    int to = 0;
    if (!selectedRange(from, to))
        return false;

    splitRunAt(from);
    splitRunAt(to);

    int at = 0;
    for (fla::TextRun& run : _text->runs)
    {
        const int length = runLength(run);
        if (at >= from && at + length <= to && length > 0)
            change(run);
        at += length;
    }
    mergeRuns();
    if (_view)
        refresh(*_view);
    return true;
}

bool TextTool::applyFace(const std::string& face)
{
    return restyleSelection([&](fla::TextRun& run) { run.face = face; });
}

bool TextTool::applySize(double size)
{
    return restyleSelection([&](fla::TextRun& run) { run.size = size; });
}

bool TextTool::applyColor(const uint8_t color[4])
{
    return restyleSelection([&](fla::TextRun& run) { std::copy(color, color + 4, run.fillColor); });
}

bool TextTool::applyTextType(fla::StaticText::TextType type)
{
    if (!_text)
        return false;
    _text->textType = type;
    if (_view)
        refresh(*_view);
    return true;
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
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool control = (event->modifiers() & Qt::ControlModifier) != 0;

    // Moving with shift held stretches the selection from where it started;
    // moving without it drops the selection.
    const auto moveTo = [&](int index) {
        if (shift)
        {
            if (_anchor < 0)
                _anchor = _caret;
        }
        else
        {
            _anchor = -1;
        }
        _caret = std::max(0, std::min(total, index));
    };

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
        if (!shift && _anchor >= 0 && _anchor != _caret)
            moveTo(std::min(_anchor, _caret));
        else
            moveTo(_caret - 1);
        break;
    case Qt::Key_Right:
        if (!shift && _anchor >= 0 && _anchor != _caret)
            moveTo(std::max(_anchor, _caret));
        else
            moveTo(_caret + 1);
        break;
    case Qt::Key_Home:
        moveTo(0);
        break;
    case Qt::Key_End:
        moveTo(total);
        break;
    case Qt::Key_Tab:
        return true;
    case Qt::Key_A:
        if (control)
        {
            _anchor = 0;
            _caret = total;
            break;
        }
        // fall through: a plain letter is typed
        [[fallthrough]];
    default:
    {
        const bool shortcut = control && !(event->modifiers() & Qt::AltModifier);
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

    // The selected range, as a translucent band behind the glyphs.
    int from = 0;
    int to = 0;
    if (selectedRange(from, to))
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(51, 153, 255, 110));
        for (const QRectF& band : TextLayout::selectionRects(*_text, from, to))
            painter.drawRect(band);
    }

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
