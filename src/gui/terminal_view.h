#pragma once

#include <QColor>
#include <QFont>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <deque>
#include <vector>

class PtyProcess;
struct VTerm;
struct VTermScreen;

/// A terminal in a panel: a program running in a pseudo-terminal
/// (PtyProcess), its screen kept by libvterm and drawn here, keys typed here
/// sent to it. Lines that scroll off the top are kept to scroll back to with
/// the wheel; text can be selected with the mouse and copied.
class TerminalView : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget* parent = nullptr);
    ~TerminalView() override;

    /// Starts \a program on this terminal, ending whatever ran on it before.
    /// False with \a error when it cannot.
    bool start(const QString& program, const QStringList& arguments, const QString& workingDirectory, QString& error);
    bool isRunning() const;
    /// Ends what runs on it.
    void stop();
    /// Shows \a text on the terminal as if the program had written it.
    void showText(const QString& text);
    void clear();

    QSize sizeHint() const override;

signals:
    void finished(int exitCode);
    void titleChanged(const QString& title);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    struct Cell
    {
        QString text;
        QColor foreground;
        QColor background;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strike = false;
        int width = 1;
    };
    using Line = std::vector<Cell>;

    // libvterm's callbacks, in terminal_view.cpp.
    struct Callbacks;
    friend struct Callbacks;

    void receive(const QByteArray& data);
    void fitToSize();
    /// Row \a row of the screen.
    Line screenLine(int row) const;
    /// A cell of libvterm's screen (a VTermScreenCell) as drawn here.
    Cell toCell(const void* cell) const;
    /// Row \a row of what is shown, counting the lines scrolled back to.
    Line shownLine(int row) const;
    /// The cell under \a point, in lines from the oldest kept, and columns.
    QPoint cellAt(const QPoint& point) const;
    bool selected(int line, int column) const;
    QString selectedText() const;
    void copy();
    void paste();
    void scrollTo(int linesBack);

    PtyProcess* _pty = nullptr;
    VTerm* _vterm = nullptr;
    VTermScreen* _screen = nullptr;
    QFont _font;
    QFont _boldFont;
    qreal _cellWidth = 8.0;
    int _cellHeight = 16;
    int _ascent = 12;
    int _columns = 80;
    int _rows = 24;
    QPoint _cursor;
    bool _cursorVisible = true;
    std::deque<Line> _scrollback;
    int _linesBack = 0;
    // A selection from one cell to another, in lines from the oldest kept.
    QPoint _selectFrom{-1, -1};
    QPoint _selectTo{-1, -1};
    bool _selecting = false;
};
