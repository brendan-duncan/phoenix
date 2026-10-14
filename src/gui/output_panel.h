#pragma once

#include <QWidget>

#include <functional>

class QPlainTextEdit;

/// Window > Output: what scripts trace (fl.trace) and the errors they stop
/// on, with the file and line. Clear empties it; Save writes it to a file.
class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget* parent = nullptr);

    void append(const QString& text);
    /// An error, set apart from the script's own output. With \a open,
    /// double-clicking the line runs it (to show the script the error is
    /// in).
    void appendError(const QString& text, std::function<void()> open = {});
    void clear();
    QString text() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /// Something was added, so the panel's dock can come into view.
    void written();

private:
    QPlainTextEdit* _text;
};
