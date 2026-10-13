#include "output_panel.h"

#include <QFileDialog>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextStream>
#include <QVBoxLayout>

OutputPanel::OutputPanel(QWidget* parent)
    : QWidget(parent)
    , _text(new QPlainTextEdit(this))
{
    _text->setReadOnly(true);
    _text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    // Long runs of output stay usable.
    _text->setMaximumBlockCount(100000);

    QPushButton* clearButton = new QPushButton("Clear", this);
    clearButton->setToolTip("Empty the Output panel");
    connect(clearButton, &QPushButton::clicked, this, &OutputPanel::clear);
    QPushButton* saveButton = new QPushButton("Save...", this);
    saveButton->setToolTip("Write what the panel shows to a text file");
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, "Save Output", "output.txt", "Text files (*.txt)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            file.write(text().toUtf8());
    });

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addStretch();
    buttons->addWidget(saveButton);
    buttons->addWidget(clearButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(_text, 1);
    layout->addLayout(buttons);
}

void OutputPanel::append(const QString& text)
{
    _text->appendPlainText(text);
    _text->verticalScrollBar()->setValue(_text->verticalScrollBar()->maximum());
    emit written();
}

void OutputPanel::appendError(const QString& text)
{
    // A colored line, so an error stands out from what the script traced.
    QTextCharFormat format;
    format.setForeground(QColor(224, 80, 80));
    QTextCursor cursor(_text->document());
    cursor.movePosition(QTextCursor::End);
    if (!_text->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(text, format);
    _text->verticalScrollBar()->setValue(_text->verticalScrollBar()->maximum());
    emit written();
}

void OutputPanel::clear()
{
    _text->clear();
}

QString OutputPanel::text() const
{
    return _text->toPlainText();
}
