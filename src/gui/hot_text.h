#pragma once

#include <QWidget>

class QLineEdit;

/// A number shown as Animate's "hot text": blue, underlined, and live.
///
/// Dragging left or right on it scrubs the value (shift for ten times the
/// speed, ctrl for a tenth); a click without a drag opens the number for
/// typing, where Return commits and Escape cancels. The value is clamped to
/// the range and rounded to the decimals.
///
/// A drag reports every step through valueChanged with \c live set, bracketed
/// by editStarted and editFinished, so the owner can apply the steps directly
/// and record one undo step at the end. A typed value arrives as one
/// valueChanged with \c live clear, followed by editFinished.
class HotText : public QWidget
{
    Q_OBJECT

public:
    explicit HotText(QWidget* parent = nullptr);

    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    /// Text drawn after the number, such as "%" or a degree sign.
    void setSuffix(const QString& suffix);
    /// How much one pixel of drag changes the value.
    void setStep(double perPixel);
    /// Shown in place of the number when no value applies.
    void setPlaceholder(const QString& text);

    double value() const { return _value; }
    /// Sets the shown value without reporting a change.
    void setValue(double value);
    void setEmpty(bool empty);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void editStarted();
    void valueChanged(double value, bool live);
    void editFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString text() const;
    double clamp(double value) const;
    void openEditor();
    void closeEditor(bool commit);

    QLineEdit* _editor;
    double _value = 0.0;
    double _minimum = -1.0e9;
    double _maximum = 1.0e9;
    int _decimals = 2;
    double _step = 1.0;
    QString _suffix;
    QString _placeholder;
    bool _empty = false;

    bool _pressed = false;
    bool _dragging = false;
    QPoint _pressPos;
    double _pressValue = 0.0;
    bool _editorOpen = false;
};
