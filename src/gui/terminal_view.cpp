#include "terminal_view.h"

#include "pty_process.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

extern "C" {
#include <vterm.h>
#include <cstdint>
}

namespace {

// Lines kept to scroll back to.
constexpr std::size_t kScrollbackLines = 5000;

// The Control key: Qt calls it Meta on macOS, where its Control is Command
// (which copies and pastes there).
#ifdef __APPLE__
constexpr Qt::KeyboardModifier kControl = Qt::MetaModifier;
#else
constexpr Qt::KeyboardModifier kControl = Qt::ControlModifier;
#endif

VTermModifier modifiersOf(Qt::KeyboardModifiers modifiers)
{
    int mod = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier)
        mod |= VTERM_MOD_SHIFT;
    if (modifiers & Qt::AltModifier)
        mod |= VTERM_MOD_ALT;
    if (modifiers & kControl)
        mod |= VTERM_MOD_CTRL;
    return static_cast<VTermModifier>(mod);
}

/// The terminal's key for \a key, or VTERM_KEY_NONE for a character key.
VTermKey keyOf(int key)
{
    switch (key)
    {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VTERM_KEY_ENTER;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return VTERM_KEY_TAB;
    case Qt::Key_Backspace:
        return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape:
        return VTERM_KEY_ESCAPE;
    case Qt::Key_Up:
        return VTERM_KEY_UP;
    case Qt::Key_Down:
        return VTERM_KEY_DOWN;
    case Qt::Key_Left:
        return VTERM_KEY_LEFT;
    case Qt::Key_Right:
        return VTERM_KEY_RIGHT;
    case Qt::Key_Insert:
        return VTERM_KEY_INS;
    case Qt::Key_Delete:
        return VTERM_KEY_DEL;
    case Qt::Key_Home:
        return VTERM_KEY_HOME;
    case Qt::Key_End:
        return VTERM_KEY_END;
    case Qt::Key_PageUp:
        return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown:
        return VTERM_KEY_PAGEDOWN;
    default:
        break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
    return VTERM_KEY_NONE;
}

} // namespace

struct TerminalView::Callbacks
{
    static TerminalView* view(void* user) { return static_cast<TerminalView*>(user); }

    static int damage(VTermRect, void* user)
    {
        view(user)->update();
        return 1;
    }

    static int moveCursor(VTermPos pos, VTermPos, int visible, void* user)
    {
        TerminalView* self = view(user);
        self->_cursor = QPoint(pos.col, pos.row);
        self->_cursorVisible = visible != 0;
        self->update();
        return 1;
    }

    static int termProp(VTermProp prop, VTermValue* value, void* user)
    {
        TerminalView* self = view(user);
        static QByteArray title;
        switch (prop)
        {
        case VTERM_PROP_CURSORVISIBLE:
            self->_cursorVisible = value->boolean != 0;
            self->update();
            break;
        case VTERM_PROP_TITLE:
            if (value->string.initial)
                title.clear();
            title.append(value->string.str, static_cast<qsizetype>(value->string.len));
            if (value->string.final)
                emit self->titleChanged(QString::fromUtf8(title));
            break;
        default:
            break;
        }
        return 1;
    }

    static int bell(void*) { return 1; }

    static int pushLine(int columns, const VTermScreenCell* cells, void* user)
    {
        TerminalView* self = view(user);
        Line line;
        line.reserve(static_cast<std::size_t>(columns));
        for (int i = 0; i < columns; ++i)
            line.push_back(self->toCell(&cells[i]));
        self->_scrollback.push_back(std::move(line));
        if (self->_scrollback.size() > kScrollbackLines)
        {
            self->_scrollback.pop_front();
            self->_selectFrom.ry() -= 1;
            self->_selectTo.ry() -= 1;
        }
        // What is being read back stays where it is.
        else if (self->_linesBack > 0)
            ++self->_linesBack;
        return 1;
    }

    static int popLine(int, VTermScreenCell*, void*) { return 0; }

    static int clearScrollback(void* user)
    {
        TerminalView* self = view(user);
        self->_scrollback.clear();
        self->_linesBack = 0;
        self->update();
        return 1;
    }

    static void output(const char* bytes, size_t length, void* user)
    {
        TerminalView* self = view(user);
        if (self->_pty)
            self->_pty->write(QByteArray(bytes, static_cast<qsizetype>(length)));
    }
};

TerminalView::TerminalView(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::IBeamCursor);

    _font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef _WIN32
    for (const char* family : {"Cascadia Mono", "Consolas"})
    {
        if (QFontDatabase::hasFamily(family))
        {
            _font = QFont(family);
            break;
        }
    }
#endif
    _font.setStyleHint(QFont::TypeWriter);
    _font.setFixedPitch(true);
    _font.setPointSizeF(10.0);
    _font.setKerning(false);
    _boldFont = _font;
    _boldFont.setBold(true);
    const QFontMetricsF metrics(_font);
    _cellWidth = std::max(1.0, metrics.horizontalAdvance(QLatin1Char('M')));
    _cellHeight = std::max(1, static_cast<int>(std::ceil(metrics.height())));
    _ascent = static_cast<int>(std::round(metrics.ascent()));

    _vterm = vterm_new(_rows, _columns);
    vterm_set_utf8(_vterm, 1);
    vterm_output_set_callback(_vterm, &Callbacks::output, this);
    _screen = vterm_obtain_screen(_vterm);
    static VTermScreenCallbacks callbacks = []() {
        VTermScreenCallbacks made{};
        made.damage = &Callbacks::damage;
        made.movecursor = &Callbacks::moveCursor;
        made.settermprop = &Callbacks::termProp;
        made.bell = &Callbacks::bell;
        made.sb_pushline = &Callbacks::pushLine;
        made.sb_popline = &Callbacks::popLine;
        made.sb_clear = &Callbacks::clearScrollback;
        return made;
    }();
    vterm_screen_set_callbacks(_screen, &callbacks, this);
    vterm_screen_enable_altscreen(_screen, 1);
    vterm_screen_reset(_screen, 1);

    _pty = new PtyProcess(this);
    connect(_pty, &PtyProcess::output, this, &TerminalView::receive);
    connect(_pty, &PtyProcess::finished, this, &TerminalView::finished);
}

TerminalView::~TerminalView()
{
    // Ended first: what it still writes has nowhere to go.
    delete _pty;
    _pty = nullptr;
    vterm_free(_vterm);
}

bool TerminalView::start(const QString& program, const QStringList& arguments, const QString& workingDirectory,
    QString& error)
{
    _pty->kill();
    fitToSize();
    return _pty->start(program, arguments, workingDirectory, _columns, _rows, error);
}

bool TerminalView::isRunning() const
{
    return _pty && _pty->isRunning();
}

void TerminalView::stop()
{
    _pty->kill();
}

void TerminalView::showText(const QString& text)
{
    QString lines = text;
    lines.replace("\r\n", "\n").replace("\n", "\r\n");
    receive(lines.toUtf8());
}

void TerminalView::clear()
{
    vterm_screen_reset(_screen, 1);
    _scrollback.clear();
    _linesBack = 0;
    _selectFrom = _selectTo = QPoint(-1, -1);
    update();
}

QSize TerminalView::sizeHint() const
{
    return QSize(static_cast<int>(std::ceil(_cellWidth * 80)), _cellHeight * 24);
}

void TerminalView::receive(const QByteArray& data)
{
    vterm_input_write(_vterm, data.constData(), static_cast<size_t>(data.size()));
    vterm_screen_flush_damage(_screen);
    update();
}

void TerminalView::fitToSize()
{
    const int columns = std::max(2, static_cast<int>(width() / _cellWidth));
    const int rows = std::max(2, height() / _cellHeight);
    if (columns == _columns && rows == _rows)
        return;
    _columns = columns;
    _rows = rows;
    vterm_set_size(_vterm, _rows, _columns);
    vterm_screen_flush_damage(_screen);
    if (_pty)
        _pty->resize(_columns, _rows);
}

TerminalView::Cell TerminalView::toCell(const void* from) const
{
    const VTermScreenCell& cell = *static_cast<const VTermScreenCell*>(from);
    Cell out;
    out.width = std::max(1, static_cast<int>(cell.width));
    // The second half of a wide character holds no character of its own.
    if (cell.chars[0] != 0 && cell.chars[0] != 0xFFFFFFFFu)
    {
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
        {
            if (cell.chars[i] <= 0x10FFFF)
            {
                const char32_t c = cell.chars[i];
                out.text += QString::fromUcs4(&c, 1);
            }
        }
    }
    VTermColor foreground = cell.fg;
    VTermColor background = cell.bg;
    const bool defaultForeground = VTERM_COLOR_IS_DEFAULT_FG(&foreground);
    const bool defaultBackground = VTERM_COLOR_IS_DEFAULT_BG(&background);
    vterm_screen_convert_color_to_rgb(_screen, &foreground);
    vterm_screen_convert_color_to_rgb(_screen, &background);
    out.foreground = defaultForeground ? QColor()
                                       : QColor(foreground.rgb.red, foreground.rgb.green, foreground.rgb.blue);
    out.background = defaultBackground ? QColor()
                                       : QColor(background.rgb.red, background.rgb.green, background.rgb.blue);
    if (cell.attrs.reverse)
    {
        const QColor text = out.foreground.isValid() ? out.foreground : palette().color(QPalette::Text);
        out.foreground = out.background.isValid() ? out.background : palette().color(QPalette::Base);
        out.background = text;
    }
    out.bold = cell.attrs.bold;
    out.italic = cell.attrs.italic;
    out.underline = cell.attrs.underline != 0;
    out.strike = cell.attrs.strike;
    if (cell.attrs.conceal)
        out.text.clear();
    return out;
}

TerminalView::Line TerminalView::screenLine(int row) const
{
    Line line;
    line.reserve(static_cast<std::size_t>(_columns));
    for (int column = 0; column < _columns; ++column)
    {
        VTermScreenCell cell{};
        vterm_screen_get_cell(_screen, VTermPos{row, column}, &cell);
        line.push_back(toCell(&cell));
    }
    return line;
}

TerminalView::Line TerminalView::shownLine(int row) const
{
    const int line = static_cast<int>(_scrollback.size()) - _linesBack + row;
    if (line >= 0 && line < static_cast<int>(_scrollback.size()))
        return _scrollback[static_cast<std::size_t>(line)];
    return screenLine(line - static_cast<int>(_scrollback.size()));
}

QPoint TerminalView::cellAt(const QPoint& point) const
{
    const int row = std::clamp(point.y() / _cellHeight, 0, _rows - 1);
    const int column = std::clamp(static_cast<int>(point.x() / _cellWidth), 0, _columns - 1);
    return QPoint(column, static_cast<int>(_scrollback.size()) - _linesBack + row);
}

bool TerminalView::selected(int line, int column) const
{
    if (_selectFrom.y() < 0 || _selectFrom == _selectTo)
        return false;
    QPoint from = _selectFrom, to = _selectTo;
    if (std::make_pair(to.y(), to.x()) < std::make_pair(from.y(), from.x()))
        std::swap(from, to);
    const auto at = std::make_pair(line, column);
    return at >= std::make_pair(from.y(), from.x()) && at <= std::make_pair(to.y(), to.x());
}

QString TerminalView::selectedText() const
{
    if (_selectFrom.y() < 0 || _selectFrom == _selectTo)
        return QString();
    QPoint from = _selectFrom, to = _selectTo;
    if (std::make_pair(to.y(), to.x()) < std::make_pair(from.y(), from.x()))
        std::swap(from, to);
    QStringList lines;
    const int kept = static_cast<int>(_scrollback.size());
    for (int index = std::max(0, from.y()); index <= to.y(); ++index)
    {
        const Line line = index < kept ? _scrollback[static_cast<std::size_t>(index)] : screenLine(index - kept);
        const int first = index == from.y() ? from.x() : 0;
        const int last = index == to.y() ? to.x() : static_cast<int>(line.size()) - 1;
        QString text;
        for (int column = first; column <= last && column < static_cast<int>(line.size()); ++column)
            text += line[static_cast<std::size_t>(column)].text.isEmpty() &&
                    line[static_cast<std::size_t>(column)].width == 1 ? QStringLiteral(" ")
                                                                       : line[static_cast<std::size_t>(column)].text;
        while (text.endsWith(' '))
            text.chop(1);
        lines << text;
    }
    return lines.join('\n');
}

void TerminalView::copy()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void TerminalView::paste()
{
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    text.replace("\r\n", "\r").replace('\n', '\r');
    scrollTo(0);
    vterm_keyboard_start_paste(_vterm);
    for (const char32_t c : text.toStdU32String())
        vterm_keyboard_unichar(_vterm, c, VTERM_MOD_NONE);
    vterm_keyboard_end_paste(_vterm);
}

void TerminalView::scrollTo(int linesBack)
{
    linesBack = std::clamp(linesBack, 0, static_cast<int>(_scrollback.size()));
    if (linesBack == _linesBack)
        return;
    _linesBack = linesBack;
    update();
}

bool TerminalView::event(QEvent* event)
{
    // Keys typed here are the program's, ahead of the window's shortcuts, and
    // Tab is not for moving between panels.
    if (event->type() == QEvent::ShortcutOverride)
    {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
        {
            keyPressEvent(key);
            return true;
        }
    }
    return QWidget::event(event);
}

void TerminalView::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool control = modifiers & kControl;
    const bool shift = modifiers & Qt::ShiftModifier;
    const int key = event->key();

#ifdef __APPLE__
    if ((modifiers & Qt::ControlModifier) && (key == Qt::Key_C || key == Qt::Key_V))
    {
        if (key == Qt::Key_C)
            copy();
        else
            paste();
        return;
    }
#endif
    // Copy and paste: Ctrl+Shift+C and V, Ctrl+C while text is selected, and
    // Ctrl+V or Shift+Insert while there is text to paste (otherwise Ctrl+V
    // goes to the program, which may paste a picture itself).
    if (control && shift && key == Qt::Key_C)
    {
        copy();
        return;
    }
    if (control && !shift && key == Qt::Key_C && !selectedText().isEmpty())
    {
        copy();
        _selectFrom = _selectTo = QPoint(-1, -1);
        update();
        return;
    }
    if ((control && key == Qt::Key_V && !QApplication::clipboard()->text().isEmpty()) ||
        (shift && key == Qt::Key_Insert))
    {
        paste();
        return;
    }
    if (shift && !control && (key == Qt::Key_PageUp || key == Qt::Key_PageDown))
    {
        scrollTo(_linesBack + (key == Qt::Key_PageUp ? 1 : -1) * std::max(1, _rows - 1));
        return;
    }
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta)
        return;

    scrollTo(0);
    VTermModifier mod = modifiersOf(modifiers);
    const VTermKey special = keyOf(key);
    if (special != VTERM_KEY_NONE)
    {
        if (key == Qt::Key_Backtab)
            mod = static_cast<VTermModifier>(mod | VTERM_MOD_SHIFT);
        vterm_keyboard_key(_vterm, special, mod);
        return;
    }
    if (control && key >= Qt::Key_A && key <= Qt::Key_Z)
    {
        vterm_keyboard_unichar(_vterm, static_cast<uint32_t>('a' + (key - Qt::Key_A)), mod);
        return;
    }
    const QString text = event->text();
    if (text.isEmpty())
    {
        QWidget::keyPressEvent(event);
        return;
    }
    // The character typed carries Shift (and AltGr's Ctrl+Alt) already.
    const VTermModifier rest =
        static_cast<VTermModifier>(control && (modifiers & Qt::AltModifier) ? VTERM_MOD_NONE : mod & VTERM_MOD_ALT);
    for (const char32_t c : text.toStdU32String())
        vterm_keyboard_unichar(_vterm, c, rest);
}

void TerminalView::inputMethodEvent(QInputMethodEvent* event)
{
    const QString text = event->commitString();
    if (!text.isEmpty())
    {
        scrollTo(0);
        for (const char32_t c : text.toStdU32String())
            vterm_keyboard_unichar(_vterm, c, VTERM_MOD_NONE);
    }
    event->accept();
}

void TerminalView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QColor base = palette().color(QPalette::Base);
    const QColor text = palette().color(QPalette::Text);
    const QColor highlight = palette().color(QPalette::Highlight);
    const QColor highlightText = palette().color(QPalette::HighlightedText);
    painter.fillRect(rect(), base);

    const int firstLine = static_cast<int>(_scrollback.size()) - _linesBack;
    for (int row = 0; row < _rows; ++row)
    {
        const Line line = shownLine(row);
        const int y = row * _cellHeight;
        for (int column = 0; column < static_cast<int>(line.size()); ++column)
        {
            const Cell& cell = line[static_cast<std::size_t>(column)];
            const bool isSelected = selected(firstLine + row, column);
            QColor background = isSelected ? highlight : cell.background;
            QColor foreground = isSelected ? highlightText : (cell.foreground.isValid() ? cell.foreground : text);
            const qreal x = column * _cellWidth;
            const QRectF box(x, y, _cellWidth * cell.width, _cellHeight);
            if (background.isValid())
                painter.fillRect(QRectF(x, y, _cellWidth, _cellHeight), background);
            if (cell.text.isEmpty() || cell.text == QLatin1String(" "))
                continue;
            painter.setFont(cell.bold ? _boldFont : _font);
            if (cell.italic)
            {
                QFont italic = painter.font();
                italic.setItalic(true);
                painter.setFont(italic);
            }
            painter.setPen(foreground);
            painter.drawText(QPointF(x, y + _ascent), cell.text);
            if (cell.underline)
                painter.drawLine(QPointF(x, y + _ascent + 2), QPointF(box.right(), y + _ascent + 2));
            if (cell.strike)
                painter.drawLine(QPointF(x, y + _cellHeight / 2.0), QPointF(box.right(), y + _cellHeight / 2.0));
        }
    }

    // The cursor: a block while typing goes here, an outline otherwise.
    if (_cursorVisible && _linesBack == 0 && _cursor.y() >= 0 && _cursor.y() < _rows)
    {
        const QRectF box(_cursor.x() * _cellWidth, _cursor.y() * _cellHeight, _cellWidth, _cellHeight);
        if (hasFocus())
        {
            painter.fillRect(box, text);
            const Line line = screenLine(_cursor.y());
            if (_cursor.x() < static_cast<int>(line.size()))
            {
                const Cell& cell = line[static_cast<std::size_t>(_cursor.x())];
                if (!cell.text.isEmpty())
                {
                    painter.setFont(cell.bold ? _boldFont : _font);
                    painter.setPen(base);
                    painter.drawText(QPointF(box.left(), box.top() + _ascent), cell.text);
                }
            }
        }
        else
        {
            painter.setPen(text);
            painter.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
}

void TerminalView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    fitToSize();
    scrollTo(_linesBack);
}

void TerminalView::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / 40;
    if (steps != 0)
        scrollTo(_linesBack + steps);
    event->accept();
}

void TerminalView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton)
    {
        _selectFrom = _selectTo = cellAt(event->position().toPoint());
        _selecting = true;
        update();
    }
    else if (event->button() == Qt::MiddleButton)
        paste();
}

void TerminalView::mouseMoveEvent(QMouseEvent* event)
{
    if (!_selecting)
        return;
    const QPoint at = event->position().toPoint();
    // Past the top or bottom, it scrolls.
    if (at.y() < 0)
        scrollTo(_linesBack + 1);
    else if (at.y() > height())
        scrollTo(_linesBack - 1);
    _selectTo = cellAt(at);
    update();
}

void TerminalView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        _selecting = false;
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    // The word under the pointer.
    const QPoint at = cellAt(event->position().toPoint());
    const int kept = static_cast<int>(_scrollback.size());
    const Line line = at.y() < kept ? _scrollback[static_cast<std::size_t>(at.y())] : screenLine(at.y() - kept);
    const auto isWord = [&line](int column) {
        if (column < 0 || column >= static_cast<int>(line.size()))
            return false;
        const QString& text = line[static_cast<std::size_t>(column)].text;
        return !text.isEmpty() && !text.at(0).isSpace();
    };
    if (!isWord(at.x()))
        return;
    int first = at.x(), last = at.x();
    while (isWord(first - 1))
        --first;
    while (isWord(last + 1))
        ++last;
    _selectFrom = QPoint(first, at.y());
    _selectTo = QPoint(last, at.y());
    _selecting = false;
    update();
}

void TerminalView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* copyAction = menu.addAction("&Copy", this, &TerminalView::copy);
    copyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyAction->setEnabled(!selectedText().isEmpty());
    QAction* pasteAction = menu.addAction("&Paste", this, &TerminalView::paste);
    pasteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    pasteAction->setEnabled(!QApplication::clipboard()->text().isEmpty());
    menu.addSeparator();
    menu.addAction("Select &All", this, [this]() {
        _selectFrom = QPoint(0, 0);
        _selectTo = QPoint(_columns - 1, static_cast<int>(_scrollback.size()) + _rows - 1);
        update();
    });
    menu.exec(event->globalPos());
}

void TerminalView::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void TerminalView::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    update();
}
