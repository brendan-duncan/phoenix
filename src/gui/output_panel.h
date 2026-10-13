#pragma once

#include <QWidget>

class QPlainTextEdit;

/// Window > Output: what scripts trace (fl.trace) and the errors they stop
/// on, with the file and line. Clear empties it; Save writes it to a file.
class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget* parent = nullptr);

    void append(const QString& text);
    /// An error, set apart from the script's own output.
    void appendError(const QString& text);
    void clear();
    QString text() const;

signals:
    /// Something was added, so the panel's dock can come into view.
    void written();

private:
    QPlainTextEdit* _text;
};
