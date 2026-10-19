#pragma once

#include <QFrame>

class QLabel;
class QTimer;

/// A small bubble over a corner of the stage with a next-step hint, shown
/// once the first time someone does a key thing (FirstUseHints). It goes
/// away on Got It, by itself after a while, or for good with Don't Show
/// Hints.
class HintBubble : public QFrame
{
    Q_OBJECT

public:
    /// Over \a stage, at its lower left.
    explicit HintBubble(QWidget* stage);

    void showHint(const QString& text);
    QString text() const;

signals:
    /// Don't Show Hints was clicked.
    void hintsTurnedOff();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();

    QWidget* _stage;
    QLabel* _text;
    QTimer* _timer;
};
