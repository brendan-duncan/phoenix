#include "code_editor.h"

#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QTextBlock>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

namespace {

constexpr int kIndent = 4;

// Where a block of the highlighter starts: inside nothing, a /* comment, or
// a Python triple-quoted string.
enum BlockState
{
    Normal = 0,
    InBlockComment = 1,
    InTripleDouble = 2,
    InTripleSingle = 3,
};

const QSet<QString>& keywords(CodeEditor::Language language)
{
    static const QSet<QString> actionScript = {"break", "case", "catch", "class", "const", "continue", "default",
        "delete", "do", "dynamic", "each", "else", "extends", "false", "finally", "for", "function", "get", "if",
        "implements", "import", "in", "instanceof", "interface", "internal", "is", "native", "new", "null",
        "override", "package", "private", "protected", "public", "return", "set", "static", "super", "switch",
        "this", "throw", "true", "try", "typeof", "undefined", "var", "void", "while", "with",
        // ActionScript 1 and 2.
        "on", "onClipEvent", "tellTarget", "ifFrameLoaded", "and", "or", "not", "eq", "ne", "lt", "gt", "le", "ge",
        "add"};
    static const QSet<QString> javaScript = {"async", "await", "break", "case", "catch", "class", "const",
        "continue", "debugger", "default", "delete", "do", "else", "export", "extends", "false", "finally", "for",
        "function", "if", "import", "in", "instanceof", "let", "new", "null", "of", "return", "super", "switch",
        "this", "throw", "true", "try", "typeof", "undefined", "var", "void", "while", "with", "yield"};
    static const QSet<QString> python = {"False", "None", "True", "and", "as", "assert", "async", "await", "break",
        "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if",
        "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with",
        "yield"};
    static const QSet<QString> none;
    switch (language)
    {
    case CodeEditor::Language::ActionScript:
        return actionScript;
    case CodeEditor::Language::JavaScript:
        return javaScript;
    case CodeEditor::Language::Python:
        return python;
    default:
        return none;
    }
}

const QSet<QString>& builtins(CodeEditor::Language language)
{
    static const QSet<QString> actionScript = {"stop", "play", "gotoAndPlay", "gotoAndStop", "nextFrame",
        "prevFrame", "nextScene", "prevScene", "trace", "getProperty", "setProperty", "duplicateMovieClip",
        "removeMovieClip", "attachMovie", "startDrag", "stopDrag", "getURL", "navigateToURL", "loadMovie",
        "loadMovieNum", "unloadMovie", "fscommand", "getTimer", "random", "int", "Number", "String", "Boolean",
        "Array", "Object", "Math", "MovieClip", "SimpleButton", "Sound", "Key", "Mouse", "Date", "Stage", "Event",
        "MouseEvent", "KeyboardEvent", "addEventListener", "removeEventListener", "_root", "_parent", "_global",
        "_level0", "_x", "_y", "_xscale", "_yscale", "_rotation", "_alpha", "_visible", "_width", "_height",
        "_currentframe", "_totalframes", "_name", "_droptarget", "_xmouse", "_ymouse", "press", "release",
        "releaseOutside", "rollOver", "rollOut", "dragOver", "dragOut", "keyPress", "load", "unload",
        "enterFrame", "mouseDown", "mouseUp", "mouseMove", "keyDown", "keyUp", "data"};
    static const QSet<QString> javaScript = {"fl", "FLfile", "MMExecute", "alert", "confirm", "prompt",
        "console", "Math", "JSON", "Object", "Array", "String", "Number", "Boolean", "Date", "RegExp",
        "parseInt", "parseFloat", "isNaN", "document", "timeline", "phoenix"};
    static const QSet<QString> python = {"print", "len", "range", "str", "int", "float", "bool", "list", "dict",
        "set", "tuple", "enumerate", "zip", "min", "max", "abs", "round", "sorted", "isinstance", "open", "self",
        "fl", "phoenix"};
    static const QSet<QString> none;
    switch (language)
    {
    case CodeEditor::Language::ActionScript:
        return actionScript;
    case CodeEditor::Language::JavaScript:
        return javaScript;
    case CodeEditor::Language::Python:
        return python;
    default:
        return none;
    }
}

bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

} // namespace

// --- CodeHighlighter --------------------------------------------------------

CodeHighlighter::CodeHighlighter(QTextDocument* document, CodeEditor::Language language)
    : QSyntaxHighlighter(document)
    , _language(language)
{}

void CodeHighlighter::setLanguage(CodeEditor::Language language)
{
    if (language == _language)
        return;
    _language = language;
    rehighlight();
}

void CodeHighlighter::refreshColors(const QPalette& palette)
{
    const bool dark = isDark(palette);
    _keyword = QTextCharFormat();
    _keyword.setForeground(dark ? QColor(86, 156, 214) : QColor(0, 0, 200));
    _builtin = QTextCharFormat();
    _builtin.setForeground(dark ? QColor(220, 220, 170) : QColor(121, 94, 38));
    _string = QTextCharFormat();
    _string.setForeground(dark ? QColor(206, 145, 120) : QColor(163, 21, 21));
    _number = QTextCharFormat();
    _number.setForeground(dark ? QColor(181, 206, 168) : QColor(9, 134, 88));
    _comment = QTextCharFormat();
    _comment.setForeground(dark ? QColor(106, 153, 85) : QColor(0, 128, 0));
    _comment.setFontItalic(true);
    rehighlight();
}

void CodeHighlighter::highlightBlock(const QString& text)
{
    if (_language == CodeEditor::Language::Plain)
        return;
    const bool python = _language == CodeEditor::Language::Python;
    const int length = static_cast<int>(text.size());
    int i = 0;
    int state = previousBlockState();
    setCurrentBlockState(Normal);

    // Carried over from the line before.
    if (state == InBlockComment)
    {
        const int end = text.indexOf("*/");
        if (end < 0)
        {
            setFormat(0, length, _comment);
            setCurrentBlockState(InBlockComment);
            return;
        }
        setFormat(0, end + 2, _comment);
        i = end + 2;
    }
    else if (state == InTripleDouble || state == InTripleSingle)
    {
        const QString quote = state == InTripleDouble ? "\"\"\"" : "'''";
        const int end = text.indexOf(quote);
        if (end < 0)
        {
            setFormat(0, length, _string);
            setCurrentBlockState(state);
            return;
        }
        setFormat(0, end + 3, _string);
        i = end + 3;
    }

    const QSet<QString>& words = keywords(_language);
    const QSet<QString>& known = builtins(_language);
    while (i < length)
    {
        const QChar c = text.at(i);
        // Comments.
        if (python ? c == '#' : (c == '/' && i + 1 < length && text.at(i + 1) == '/'))
        {
            setFormat(i, length - i, _comment);
            return;
        }
        if (!python && c == '/' && i + 1 < length && text.at(i + 1) == '*')
        {
            const int end = text.indexOf("*/", i + 2);
            if (end < 0)
            {
                setFormat(i, length - i, _comment);
                setCurrentBlockState(InBlockComment);
                return;
            }
            setFormat(i, end + 2 - i, _comment);
            i = end + 2;
            continue;
        }
        // Strings.
        if (c == '"' || c == '\'' || (c == '`' && _language == CodeEditor::Language::JavaScript))
        {
            if (python && text.mid(i, 3) == QString(3, c))
            {
                const QString quote(3, c);
                const int end = text.indexOf(quote, i + 3);
                if (end < 0)
                {
                    setFormat(i, length - i, _string);
                    setCurrentBlockState(c == '"' ? InTripleDouble : InTripleSingle);
                    return;
                }
                setFormat(i, end + 3 - i, _string);
                i = end + 3;
                continue;
            }
            int j = i + 1;
            while (j < length && text.at(j) != c)
                j += text.at(j) == '\\' ? 2 : 1;
            j = std::min(j + 1, length);
            setFormat(i, j - i, _string);
            i = j;
            continue;
        }
        // Numbers.
        if (c.isDigit() || (c == '.' && i + 1 < length && text.at(i + 1).isDigit()))
        {
            int j = i + 1;
            while (j < length && (text.at(j).isLetterOrNumber() || text.at(j) == '.'))
                ++j;
            setFormat(i, j - i, _number);
            i = j;
            continue;
        }
        // Words.
        if (c.isLetter() || c == '_' || c == '$')
        {
            int j = i + 1;
            while (j < length && (text.at(j).isLetterOrNumber() || text.at(j) == '_' || text.at(j) == '$'))
                ++j;
            const QString word = text.mid(i, j - i);
            // A property after a dot is the object's, not the language's.
            const bool member = i > 0 && text.at(i - 1) == '.';
            if (!member && words.contains(word))
                setFormat(i, j - i, _keyword);
            else if (known.contains(word))
                setFormat(i, j - i, _builtin);
            i = j;
            continue;
        }
        ++i;
    }
}

// --- CodeEditor -------------------------------------------------------------

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor* editor)
        : QWidget(editor)
        , _editor(editor)
    {}
    QSize sizeHint() const override { return QSize(_editor->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { _editor->paintGutter(event); }
    bool event(QEvent* event) override
    {
        // The error, on the mark that shows it.
        if (event->type() == QEvent::ToolTip && _editor->_errorLine > 0)
        {
            const auto* help = static_cast<QHelpEvent*>(event);
            const QTextBlock block = _editor->document()->findBlockByNumber(_editor->_errorLine - 1);
            const QRectF box =
                _editor->blockBoundingGeometry(block).translated(_editor->contentOffset());
            if (block.isValid() && help->pos().y() >= box.top() && help->pos().y() < box.bottom())
            {
                QToolTip::showText(help->globalPos(), _editor->_errorMessage, this);
                return true;
            }
            QToolTip::hideText();
            return true;
        }
        return QWidget::event(event);
    }

private:
    CodeEditor* _editor;
};

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setFont(codeFont());
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * kIndent);
    setTabChangesFocus(false);
    _gutter = new LineNumberArea(this);
    _highlighter = new CodeHighlighter(document(), _language);
    _highlighter->refreshColors(palette());
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this]() { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateGutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::updateMarks);
    setViewportMargins(gutterWidth(), 0, 0, 0);
    updateMarks();
}

CodeEditor::Language CodeEditor::languageFor(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "as")
        return Language::ActionScript;
    if (suffix == "jsfl" || suffix == "js")
        return Language::JavaScript;
    if (suffix == "py")
        return Language::Python;
    return Language::Plain;
}

QString CodeEditor::languageName(Language language)
{
    switch (language)
    {
    case Language::ActionScript:
        return "ActionScript";
    case Language::JavaScript:
        return "JSFL";
    case Language::Python:
        return "Python";
    default:
        return "Text";
    }
}

QFont CodeEditor::codeFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef _WIN32
    for (const char* family : {"Cascadia Mono", "Consolas"})
    {
        if (QFontDatabase::hasFamily(family))
        {
            font = QFont(family);
            break;
        }
    }
#endif
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    font.setPointSizeF(10.0);
    return font;
}

void CodeEditor::setLanguage(Language language)
{
    _language = language;
    _highlighter->setLanguage(language);
}

void CodeEditor::setErrorLine(int line, const QString& message)
{
    _errorLine = line;
    _errorMessage = message;
    updateMarks();
    _gutter->update();
}

void CodeEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(std::max(0, line - 1));
    if (!block.isValid())
        return;
    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

void CodeEditor::replaceAllText(const QString& text)
{
    if (text == toPlainText())
        return;
    const int position = textCursor().position();
    const int scroll = verticalScrollBar()->value();
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.setPosition(std::min(position, static_cast<int>(document()->characterCount()) - 1));
    setTextCursor(cursor);
    verticalScrollBar()->setValue(scroll);
}

QString CodeEditor::commentPrefix() const
{
    return _language == Language::Python ? "#" : "//";
}

QString CodeEditor::indentOf(const QTextBlock& block) const
{
    const QString text = block.text();
    int i = 0;
    while (i < text.size() && (text.at(i) == ' ' || text.at(i) == '\t'))
        ++i;
    return text.left(i);
}

void CodeEditor::toggleComment()
{
    if (_language == Language::Plain)
        return;
    QTextCursor cursor = textCursor();
    QTextBlock first = document()->findBlock(cursor.selectionStart());
    QTextBlock last = document()->findBlock(cursor.selectionEnd());
    // A selection that ends at the start of a line leaves that line alone.
    if (cursor.hasSelection() && last != first && cursor.selectionEnd() == last.position())
        last = last.previous();
    const QString prefix = commentPrefix();
    // Uncommented when every line with text is commented already.
    bool allCommented = true;
    for (QTextBlock block = first; block.isValid(); block = block.next())
    {
        const QString trimmed = block.text().trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(prefix))
            allCommented = false;
        if (block == last)
            break;
    }
    // The comment marks line up at the shallowest indent.
    int column = INT_MAX;
    for (QTextBlock block = first; block.isValid(); block = block.next())
    {
        if (!block.text().trimmed().isEmpty())
            column = std::min(column, static_cast<int>(indentOf(block).size()));
        if (block == last)
            break;
    }
    if (column == INT_MAX)
        column = 0;
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next())
    {
        const QString text = block.text();
        if (!text.trimmed().isEmpty())
        {
            if (allCommented)
            {
                const int at = static_cast<int>(text.indexOf(prefix));
                const int count = static_cast<int>(prefix.size()) +
                    (at + prefix.size() < text.size() && text.at(at + prefix.size()) == ' ' ? 1 : 0);
                edit.setPosition(block.position() + at);
                edit.setPosition(block.position() + at + count, QTextCursor::KeepAnchor);
                edit.removeSelectedText();
            }
            else
            {
                edit.setPosition(block.position() + column);
                edit.insertText(prefix + " ");
            }
        }
        if (block == last)
            break;
    }
    edit.endEditBlock();
}

void CodeEditor::indentLines(bool outdent)
{
    QTextCursor cursor = textCursor();
    QTextBlock first = document()->findBlock(cursor.selectionStart());
    QTextBlock last = document()->findBlock(cursor.selectionEnd());
    if (cursor.hasSelection() && last != first && cursor.selectionEnd() == last.position())
        last = last.previous();
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next())
    {
        if (outdent)
        {
            const QString indent = indentOf(block);
            int remove = 0;
            while (remove < indent.size() && remove < kIndent && indent.at(remove) == ' ')
                ++remove;
            if (remove == 0 && !indent.isEmpty() && indent.at(0) == '\t')
                remove = 1;
            if (remove > 0)
            {
                edit.setPosition(block.position());
                edit.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
                edit.removeSelectedText();
            }
        }
        else if (!block.text().isEmpty())
        {
            edit.setPosition(block.position());
            edit.insertText(QString(kIndent, ' '));
        }
        if (block == last)
            break;
    }
    edit.endEditBlock();
}

bool CodeEditor::event(QEvent* event)
{
    // Tab indents here rather than moving to the next field.
    if (event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
        {
            keyPressEvent(key);
            return true;
        }
    }
    return QPlainTextEdit::event(event);
}

void CodeEditor::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    QTextCursor cursor = textCursor();

    if (key == Qt::Key_Slash && (modifiers & Qt::ControlModifier))
    {
        toggleComment();
        return;
    }
    if (key == Qt::Key_Backtab || (key == Qt::Key_Tab && (modifiers & Qt::ShiftModifier)))
    {
        indentLines(true);
        return;
    }
    if (key == Qt::Key_Tab && !(modifiers & Qt::ControlModifier))
    {
        if (cursor.hasSelection() &&
            document()->findBlock(cursor.selectionStart()) != document()->findBlock(cursor.selectionEnd()))
        {
            indentLines(false);
            return;
        }
        // Spaces to the next tab stop.
        const int column = cursor.positionInBlock();
        cursor.insertText(QString(kIndent - column % kIndent, ' '));
        return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !(modifiers & Qt::ControlModifier))
    {
        // The new line keeps the indent, one deeper after an opening bracket
        // (or a Python colon), with the closing bracket on a line of its own
        // when the caret sat between the two.
        const QTextBlock block = cursor.block();
        const QString before = block.text().left(cursor.positionInBlock());
        const QString after = block.text().mid(cursor.positionInBlock());
        const QString indent = indentOf(block);
        const QString trimmed = before.trimmed();
        const bool opens = trimmed.endsWith('{') || trimmed.endsWith('(') || trimmed.endsWith('[') ||
            (_language == Language::Python && trimmed.endsWith(':'));
        const QString deeper = indent + QString(kIndent, ' ');
        cursor.beginEditBlock();
        if (opens && !after.trimmed().isEmpty() && QString("})]").contains(after.trimmed().at(0)))
        {
            cursor.insertText("\n" + deeper);
            const int middle = cursor.position();
            cursor.insertText("\n" + indent);
            cursor.setPosition(middle);
        }
        else
            cursor.insertText("\n" + (opens ? deeper : indent));
        cursor.endEditBlock();
        setTextCursor(cursor);
        ensureCursorVisible();
        return;
    }
    if ((key == Qt::Key_BraceRight || event->text() == QLatin1String("}")) && !cursor.hasSelection())
    {
        // A closing brace on a line of spaces comes back one indent.
        const QTextBlock block = cursor.block();
        const QString text = block.text();
        if (text.trimmed().isEmpty() && cursor.positionInBlock() == text.size() && text.size() >= kIndent &&
            text.endsWith(QString(kIndent, ' ')))
        {
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, kIndent);
            cursor.insertText("}");
            cursor.endEditBlock();
            setTextCursor(cursor);
            return;
        }
    }
    if (key == Qt::Key_Home && !(modifiers & Qt::ControlModifier))
    {
        // To the line's first character, then to its start.
        const int indent = static_cast<int>(indentOf(cursor.block()).size());
        const int target = cursor.positionInBlock() == indent ? 0 : indent;
        cursor.setPosition(cursor.block().position() + target,
            (modifiers & Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
        setTextCursor(cursor);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    _gutter->setGeometry(QRect(area.left(), area.top(), gutterWidth(), area.height()));
}

void CodeEditor::changeEvent(QEvent* event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
    {
        _highlighter->refreshColors(palette());
        updateMarks();
    }
}

int CodeEditor::gutterWidth() const
{
    int digits = 2;
    for (int count = std::max(1, blockCount()); count >= 100; count /= 10)
        ++digits;
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateGutter(const QRect& rect, int dy)
{
    if (dy)
        _gutter->scroll(0, dy);
    else
        _gutter->update(0, rect.y(), _gutter->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        setViewportMargins(gutterWidth(), 0, 0, 0);
}

void CodeEditor::paintGutter(QPaintEvent* event)
{
    QPainter painter(_gutter);
    const QPalette colors = palette();
    painter.fillRect(event->rect(), colors.color(QPalette::Window));
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    const int current = textCursor().blockNumber();
    painter.setFont(font());
    while (block.isValid() && top <= event->rect().bottom())
    {
        const qreal height = blockBoundingRect(block).height();
        if (block.isVisible() && top + height >= event->rect().top())
        {
            const QRectF box(0, top, _gutter->width() - 6, fontMetrics().height());
            if (number + 1 == _errorLine)
            {
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(220, 60, 60));
                const qreal size = fontMetrics().height() * 0.5;
                painter.drawEllipse(QRectF(3, top + (fontMetrics().height() - size) / 2, size, size));
            }
            QColor text = colors.color(QPalette::Text);
            if (number != current)
                text.setAlphaF(0.45);
            painter.setPen(text);
            painter.drawText(box, Qt::AlignRight | Qt::AlignVCenter, QString::number(number + 1));
        }
        block = block.next();
        top += height;
        ++number;
    }
}

void CodeEditor::updateMarks()
{
    QList<QTextEdit::ExtraSelection> marks;
    const bool dark = isDark(palette());

    QTextEdit::ExtraSelection line;
    line.format.setBackground(dark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 10));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor();
    line.cursor.clearSelection();
    marks << line;

    if (_errorLine > 0)
    {
        const QTextBlock block = document()->findBlockByNumber(_errorLine - 1);
        if (block.isValid())
        {
            QTextEdit::ExtraSelection error;
            error.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            error.format.setUnderlineColor(QColor(220, 60, 60));
            error.format.setBackground(QColor(220, 60, 60, dark ? 40 : 28));
            error.format.setProperty(QTextFormat::FullWidthSelection, true);
            error.cursor = QTextCursor(block);
            marks << error;
        }
    }

    // The bracket at the caret, and its match.
    const QString text = toPlainText();
    const int position = textCursor().position();
    const QString opens = "([{", closes = ")]}";
    int at = -1;
    if (position < text.size() && (opens.contains(text.at(position)) || closes.contains(text.at(position))))
        at = position;
    else if (position > 0 && (opens.contains(text.at(position - 1)) || closes.contains(text.at(position - 1))))
        at = position - 1;
    if (at >= 0)
    {
        const QChar bracket = text.at(at);
        const bool forward = opens.contains(bracket);
        const QChar other = forward ? closes.at(opens.indexOf(bracket)) : opens.at(closes.indexOf(bracket));
        int depth = 0, match = -1;
        for (int i = at; forward ? i < text.size() : i >= 0; i += forward ? 1 : -1)
        {
            if (text.at(i) == bracket)
                ++depth;
            else if (text.at(i) == other && --depth == 0)
            {
                match = i;
                break;
            }
        }
        for (int place : {at, match})
        {
            if (place < 0)
                continue;
            QTextEdit::ExtraSelection mark;
            mark.format.setBackground(match >= 0 ? (dark ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 35))
                                                 : QColor(220, 60, 60, 80));
            mark.cursor = QTextCursor(document());
            mark.cursor.setPosition(place);
            mark.cursor.setPosition(place + 1, QTextCursor::KeepAnchor);
            marks << mark;
        }
    }
    setExtraSelections(marks);
    _gutter->update();
}

// --- CodeView ---------------------------------------------------------------

CodeView::CodeView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    _editor = new CodeEditor(this);
    layout->addWidget(_editor, 1);

    _bar = new QWidget(this);
    auto* barLayout = new QVBoxLayout(_bar);
    barLayout->setContentsMargins(4, 3, 4, 3);
    barLayout->setSpacing(3);
    auto* findRow = new QHBoxLayout;
    _findText = new QLineEdit(_bar);
    _findText->setPlaceholderText("Find");
    _findText->setClearButtonEnabled(true);
    findRow->addWidget(_findText, 1);
    const auto button = [this](const QString& text, const QString& tip) {
        auto* made = new QToolButton(_bar);
        made->setText(text);
        made->setToolTip(tip);
        made->setAutoRaise(true);
        return made;
    };
    QToolButton* previous = button(QString(QChar(0x2191)), "Find the one before (Shift+Enter)");
    QToolButton* next = button(QString(QChar(0x2193)), "Find the next one (Enter)");
    findRow->addWidget(previous);
    findRow->addWidget(next);
    _matchCase = new QCheckBox("Aa", _bar);
    _matchCase->setToolTip("Match case");
    _wholeWord = new QCheckBox("Word", _bar);
    _wholeWord->setToolTip("Whole words only");
    findRow->addWidget(_matchCase);
    findRow->addWidget(_wholeWord);
    _findStatus = new QLabel(_bar);
    findRow->addWidget(_findStatus);
    QToolButton* close = button(QString(QChar(0x2715)), "Close (Esc)");
    findRow->addWidget(close);
    barLayout->addLayout(findRow);

    _replaceRow = new QWidget(_bar);
    auto* replaceLayout = new QHBoxLayout(_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    _replaceText = new QLineEdit(_replaceRow);
    _replaceText->setPlaceholderText("Replace with");
    replaceLayout->addWidget(_replaceText, 1);
    auto* replace = new QToolButton(_replaceRow);
    replace->setText("Replace");
    replace->setAutoRaise(true);
    auto* replaceAllButton = new QToolButton(_replaceRow);
    replaceAllButton->setText("Replace All");
    replaceAllButton->setAutoRaise(true);
    replaceLayout->addWidget(replace);
    replaceLayout->addWidget(replaceAllButton);
    barLayout->addWidget(_replaceRow);
    layout->addWidget(_bar);
    _bar->hide();

    connect(_findText, &QLineEdit::returnPressed, this, [this]() {
        find(QApplication::keyboardModifiers() & Qt::ShiftModifier);
    });
    connect(_findText, &QLineEdit::textEdited, this, [this]() {
        // As it is typed, from where the match began.
        QTextCursor cursor = _editor->textCursor();
        cursor.setPosition(cursor.selectionStart());
        _editor->setTextCursor(cursor);
        find(false);
    });
    connect(previous, &QToolButton::clicked, this, [this]() { find(true); });
    connect(next, &QToolButton::clicked, this, [this]() { find(false); });
    connect(close, &QToolButton::clicked, this, [this]() {
        _bar->hide();
        _editor->setFocus();
    });
    connect(_replaceText, &QLineEdit::returnPressed, this, &CodeView::replaceOne);
    connect(replace, &QToolButton::clicked, this, &CodeView::replaceOne);
    connect(replaceAllButton, &QToolButton::clicked, this, &CodeView::replaceAll);

    const auto shortcut = [this](const QKeySequence& keys, auto slot) {
        auto* made = new QShortcut(keys, this, slot);
        made->setContext(Qt::WidgetWithChildrenShortcut);
    };
    shortcut(QKeySequence::Find, [this]() { showFind(false); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_H), [this]() { showFind(true); });
    shortcut(QKeySequence::FindNext, [this]() { find(false); });
    shortcut(QKeySequence::FindPrevious, [this]() { find(true); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_G), [this]() { askGoToLine(); });
    shortcut(QKeySequence(Qt::Key_Escape), [this]() {
        if (_bar->isVisible())
        {
            _bar->hide();
            _editor->setFocus();
        }
    });
}

void CodeView::showFind(bool replace)
{
    _replaceRow->setVisible(replace);
    _bar->show();
    const QString selected = _editor->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        _findText->setText(selected);
    _findText->setFocus();
    _findText->selectAll();
}

QTextDocument::FindFlags CodeView::flags(bool backward) const
{
    QTextDocument::FindFlags made;
    if (backward)
        made |= QTextDocument::FindBackward;
    if (_matchCase->isChecked())
        made |= QTextDocument::FindCaseSensitively;
    if (_wholeWord->isChecked())
        made |= QTextDocument::FindWholeWords;
    return made;
}

void CodeView::find(bool backward)
{
    const QString text = _findText->text();
    if (text.isEmpty())
    {
        _findStatus->clear();
        return;
    }
    if (_editor->find(text, flags(backward)))
    {
        _findStatus->clear();
        return;
    }
    // Round from the other end.
    QTextCursor cursor = _editor->textCursor();
    cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    _editor->setTextCursor(cursor);
    _findStatus->setText(_editor->find(text, flags(backward)) ? QString() : "Not found");
}

void CodeView::replaceOne()
{
    const QString text = _findText->text();
    QTextCursor cursor = _editor->textCursor();
    const Qt::CaseSensitivity sense = _matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (!text.isEmpty() && cursor.hasSelection() && cursor.selectedText().compare(text, sense) == 0)
        cursor.insertText(_replaceText->text());
    find(false);
}

void CodeView::replaceAll()
{
    const QString text = _findText->text();
    if (text.isEmpty())
        return;
    QTextCursor edit(_editor->document());
    edit.beginEditBlock();
    int count = 0;
    QTextCursor found = _editor->document()->find(text, 0, flags(false));
    while (!found.isNull())
    {
        found.insertText(_replaceText->text());
        ++count;
        found = _editor->document()->find(text, found, flags(false));
    }
    edit.endEditBlock();
    _findStatus->setText(QString("%1 replaced").arg(count));
}

void CodeView::askGoToLine()
{
    bool ok = false;
    const int line = QInputDialog::getInt(this, "Go to Line", QString("Line (1 to %1):").arg(_editor->blockCount()),
        _editor->textCursor().blockNumber() + 1, 1, std::max(1, _editor->blockCount()), 1, &ok);
    if (ok)
        _editor->goToLine(line);
}
