#pragma once

#include <QWidget>

class QKeyEvent;
class QLineEdit;

/// A number shown as Animate's "hot text": blue, underlined, and live. It is
/// Phoenix's one numeric field, used everywhere a number is edited, in place
/// of spin boxes and their arrow buttons.
///
/// Dragging left or right on it scrubs the value (shift for ten times the
/// speed, ctrl for a tenth); a click without a drag opens the number for
/// typing, where Return commits and Escape cancels. Tab reaches it and opens
/// it for typing; Up and Down (Page Up and Page Down for ten) step it, and so
/// does the mouse wheel while it has focus. The value is clamped to the range
/// and rounded to the decimals.
///
/// A drag reports every step through valueChanged with \c live set, bracketed
/// by editStarted and editFinished, so the owner can apply the steps directly
/// and record one undo step at the end. A typed or stepped value arrives as
/// one valueChanged with \c live clear, followed by editFinished.
class HotText : public QWidget
{
    Q_OBJECT

public:
    explicit HotText(QWidget* parent = nullptr);

    void setRange(double minimum, double maximum);
    void setMinimum(double minimum) { setRange(minimum, _maximum); }
    void setMaximum(double maximum) { setRange(_minimum, maximum); }
    double minimum() const { return _minimum; }
    double maximum() const { return _maximum; }
    void setDecimals(int decimals);
    int decimals() const { return _decimals; }
    /// Text drawn after the number, such as "%" or a degree sign.
    void setSuffix(const QString& suffix);
    /// Text drawn before the number.
    void setPrefix(const QString& prefix);
    /// How much one pixel of drag changes the value.
    void setStep(double perPixel);
    /// How much a key press or a wheel notch changes the value; also the
    /// drag step, unless setStep gave one of its own.
    void setSingleStep(double step);
    /// Shown in place of the number when no value applies.
    void setPlaceholder(const QString& text);
    /// Shown in place of the number while the value is the minimum, as a
    /// spin box's special value text ("Auto", "None").
    void setSpecialValueText(const QString& text);

    double value() const { return _value; }
    /// Sets the shown value without reporting a change.
    void setValue(double value);
    void setEmpty(bool empty);

    /// Opens the number for typing with all of it selected.
    void selectAll();

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
    void focusInEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    /// Rounds to the decimals and clamps to the range.
    double clamp(double value) const;
    /// Sets the value and reports it through valueChanged, if it changed, as
    /// a spin box's setValue does.
    void reportValue(double value);

    /// Steps by \a steps single steps (keys and the wheel): the shown
    /// value, or the one being typed while the editor is open. A field that
    /// steps through presets instead overrides it.
    virtual void stepBy(int steps);
    bool editorOpen() const { return _editorOpen; }

private:
    QString text() const;
    void openEditor();
    /// Sends Enter on to the dialog the field is in, for its default
    /// button; false outside a dialog.
    bool passToDialog(QKeyEvent* key);
    void closeEditor(bool commit);

    QLineEdit* _editor;
    double _value = 0.0;
    double _minimum = -1.0e9;
    double _maximum = 1.0e9;
    int _decimals = 2;
    double _step = 1.0;
    bool _stepSet = false;
    double _singleStep = 1.0;
    QString _suffix;
    QString _prefix;
    QString _placeholder;
    QString _specialValueText;
    bool _empty = false;

    bool _pressed = false;
    bool _dragging = false;
    QPoint _pressPos;
    double _pressValue = 0.0;
    bool _editorOpen = false;
};

/// A whole-number HotText with a spin box's interface, for code that worked
/// with QSpinBox: setValue reports the change, as QSpinBox's does (block the
/// signals to set it quietly).
class HotSpinBox : public HotText
{
    Q_OBJECT

public:
    explicit HotSpinBox(QWidget* parent = nullptr);

    int value() const { return static_cast<int>(HotText::value()); }
    void setValue(int value) { reportValue(value); }
    void setRange(int minimum, int maximum) { HotText::setRange(minimum, maximum); }
    void setMinimum(int minimum) { HotText::setMinimum(minimum); }
    void setMaximum(int maximum) { HotText::setMaximum(maximum); }
    int minimum() const { return static_cast<int>(HotText::minimum()); }
    int maximum() const { return static_cast<int>(HotText::maximum()); }
    void setSingleStep(int step) { HotText::setSingleStep(step); }
};

/// A HotText with QDoubleSpinBox's interface: setValue reports the change, as
/// QDoubleSpinBox's does (block the signals to set it quietly).
class HotDoubleSpinBox : public HotText
{
    Q_OBJECT

public:
    explicit HotDoubleSpinBox(QWidget* parent = nullptr);

    void setValue(double value) { reportValue(value); }
};
