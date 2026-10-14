#pragma once

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QWidget>

class QCheckBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QToolButton;

/// The editor for scripts: the Actions panel's ActionScript and the Script
/// window's JSFL and Python. Line numbers, colors for the language, indents
/// kept and made as code is typed, brackets matched, lines commented and
/// uncommented with Ctrl+/, and a line marked where a check found an error.
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    enum class Language
    {
        ActionScript,
        JavaScript,
        Python,
        Plain,
    };

    explicit CodeEditor(QWidget* parent = nullptr);

    /// The language a script file is in, from its extension.
    static Language languageFor(const QString& path);
    static QString languageName(Language language);
    /// The monospace font scripts and terminals are shown in.
    static QFont codeFont();

    void setLanguage(Language language);
    Language language() const { return _language; }

    /// Marks line \a line (from 1) as where \a message was found; 0 clears.
    void setErrorLine(int line, const QString& message = QString());
    int errorLine() const { return _errorLine; }

    void goToLine(int line);
    void toggleComment();
    /// Indents the selected lines, or outdents them with \a outdent.
    void indentLines(bool outdent);

    /// Replaces all the text as one step of the editor's own undo, keeping
    /// the caret near where it was.
    void replaceAllText(const QString& text);

    /// Words offered as hints besides the language's own: the instance
    /// names a script can reach, say.
    void setExtraWords(const QStringList& words);
    /// Shows the hints that fit where the caret is: after a dot, what can
    /// follow it; with \a asked (Ctrl+Space), every word that fits.
    void complete(bool asked);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    friend class LineNumberArea;
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);
    void updateGutter(const QRect& rect, int dy);
    void updateMarks();
    QString indentOf(const QTextBlock& block) const;
    QString commentPrefix() const;
    QString wordBeforeCursor() const;
    void insertCompletion(const QString& word);

    class LineNumberArea* _gutter = nullptr;
    class CodeHighlighter* _highlighter = nullptr;
    Language _language = Language::ActionScript;
    QCompleter* _completer = nullptr;
    QStringList _extraWords;
    int _errorLine = 0;
    QString _errorMessage;
};

/// The colors of a language's words, strings, numbers and comments.
class CodeHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    CodeHighlighter(QTextDocument* document, CodeEditor::Language language);
    void setLanguage(CodeEditor::Language language);
    /// The colors again, after the theme changed.
    void refreshColors(const QPalette& palette);

protected:
    void highlightBlock(const QString& text) override;

private:
    CodeEditor::Language _language;
    QTextCharFormat _keyword;
    QTextCharFormat _builtin;
    QTextCharFormat _string;
    QTextCharFormat _number;
    QTextCharFormat _comment;
};

/// A code editor with its find and replace bar (Ctrl+F, Ctrl+H) and Go to
/// Line (Ctrl+G).
class CodeView : public QWidget
{
    Q_OBJECT

public:
    explicit CodeView(QWidget* parent = nullptr);

    CodeEditor* editor() const { return _editor; }
    void showFind(bool replace);
    void askGoToLine();

private:
    void find(bool backward);
    void replaceOne();
    void replaceAll();
    QTextDocument::FindFlags flags(bool backward) const;

    CodeEditor* _editor = nullptr;
    QWidget* _bar = nullptr;
    QLineEdit* _findText = nullptr;
    QLineEdit* _replaceText = nullptr;
    QWidget* _replaceRow = nullptr;
    QCheckBox* _matchCase = nullptr;
    QCheckBox* _wholeWord = nullptr;
    QLabel* _findStatus = nullptr;
};
